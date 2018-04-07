#include "bspmm.h"

int setup(int rank, int nprocs, int argc, char **argv, int *mat_dim_ptr)
{
    int mat_dim;

    if (argc != 2) {
        if (!rank)
            printf("usage: bspmm_mpi <m>\n");
        return 1;
    }

    mat_dim = atoi(argv[1]);    /* matrix dimension */

    if (mat_dim % BLK_DIM != 0) {
        int new_mat_dim = ((mat_dim + BLK_DIM - 1) / BLK_DIM) * BLK_DIM;
        if (!rank)
            printf("mat_dim: %d -> %d\n", mat_dim, new_mat_dim);
        mat_dim = new_mat_dim;
    }

    (*mat_dim_ptr) = mat_dim;
    return 0;
}

void init_mats(int mat_dim, double *mem, double **mat_a_ptr, double **mat_b_ptr, double **mat_c_ptr)
{
    int i, j, bi, bj;
    double *mat_a, *mat_b, *mat_c;

    srand(time(NULL));

    mat_a = mem;
    mat_b = mat_a + mat_dim * mat_dim;
    mat_c = mat_b + mat_dim * mat_dim;

    for (bj = 0; bj < mat_dim; bj += BLK_DIM) {
        for (bi = 0; bi < mat_dim; bi += BLK_DIM) {
            /* initialize mat_a */
            if (rand() < SPARSITY_A * RAND_MAX) {
                for (j = bj; j < bj + BLK_DIM; j++)
                    for (i = bi; i < bi + BLK_DIM; i++)
                        mat_a[j + i * mat_dim] = 0.0;
            } else {
                for (j = bj; j < bj + BLK_DIM; j++)
                    for (i = bi; i < bi + BLK_DIM; i++)
                        mat_a[j + i * mat_dim] = (double) rand() / (RAND_MAX / RAND_RANGE + 1);
            }
            /* initialize mat_b */
            if (rand() < SPARSITY_B * RAND_MAX) {
                for (j = bj; j < bj + BLK_DIM; j++)
                    for (i = bi; i < bi + BLK_DIM; i++)
                        mat_b[j + i * mat_dim] = 0.0;
            } else {
                for (j = bj; j < bj + BLK_DIM; j++)
                    for (i = bi; i < bi + BLK_DIM; i++)
                        mat_b[j + i * mat_dim] = (double) rand() / (RAND_MAX / RAND_RANGE + 1);
            }
            mat_c[j + i * mat_dim] = 0.0;
        }
    }
    (*mat_a_ptr) = mat_a;
    (*mat_b_ptr) = mat_b;
    (*mat_c_ptr) = mat_c;
}

void dgemm(double *local_a, double *local_b, double *local_c)
{
    int i, j, k;

    memset(local_c, 0, BLK_DIM * BLK_DIM * sizeof(double));

    for (j = 0; j < BLK_DIM; j++) {
        for (i = 0; i < BLK_DIM; i++) {
            for (k = 0; k < BLK_DIM; k++)
                local_c[j + i * BLK_DIM] += local_a[k + i * BLK_DIM] * local_b[j + k * BLK_DIM];
        }
    }
}

int is_zero_local(double *local_mat)
{
    int i, j;

    for (i = 0; i < BLK_DIM; i++) {
        for (j = 0; j < BLK_DIM; j++) {
            if (local_mat[j + i * BLK_DIM] != 0.0)
                return 0;
        }
    }
    return 1;
}

int is_zero_global(double *global_mat, int mat_dim, int global_i, int global_j)
{
    int i, j;
    int offset = global_i * BLK_DIM * mat_dim + global_j * BLK_DIM;

    for (i = 0; i < BLK_DIM; i++) {
        for (j = 0; j < BLK_DIM; j++) {
            if (global_mat[offset + j + i * mat_dim] != 0.0)
                return 0;
        }
    }
    return 1;
}

void pack_global_to_local(double *local_mat, double *global_mat, int mat_dim, int global_i,
                          int global_j)
{
    int i, j;
    int offset = global_i * BLK_DIM * mat_dim + global_j * BLK_DIM;

    for (i = 0; i < BLK_DIM; i++) {
        for (j = 0; j < BLK_DIM; j++)
            local_mat[j + i * BLK_DIM] = global_mat[offset + j + i * mat_dim];
    }
}

void unpack_local_to_global(double *global_mat, double *local_mat, int mat_dim, int global_i,
                            int global_j)
{
    int i, j;
    int offset = global_i * BLK_DIM * mat_dim + global_j * BLK_DIM;

    for (i = 0; i < BLK_DIM; i++) {
        for (j = 0; j < BLK_DIM; j++)
            global_mat[offset + j + i * mat_dim] = local_mat[j + i * BLK_DIM];
    }
}

void add_local_to_global(double *global_mat, double *local_mat, int mat_dim, int global_i,
                         int global_j)
{
    int i, j;
    int offset = global_i * BLK_DIM * mat_dim + global_j * BLK_DIM;

    for (i = 0; i < BLK_DIM; i++) {
        for (j = 0; j < BLK_DIM; j++)
            global_mat[offset + j + i * mat_dim] += local_mat[j + i * BLK_DIM];
    }
}

void check_mats(double *mat_a, double *mat_b, double *mat_c, int mat_dim)
{
    int i, j, k, r;
    int bogus = 0;
    double temp_c;
    double diff, max_diff = 0.0;

    /* pick up 1000 values to check correctness */
    for (r = 0; r < 1000; r++) {
        i = rand() % mat_dim;
        j = rand() % mat_dim;
        temp_c = 0.0;
        for (k = 0; k < mat_dim; k++)
            temp_c += mat_a[k + i * mat_dim] * mat_b[j + k * mat_dim];
        diff = mat_c[j + i * mat_dim] - temp_c;
        if (fabs(diff) > 0.00001) {
            bogus = 1;
            if (fabs(diff) > fabs(max_diff))
                max_diff = diff;
        }
    }

    if (bogus)
        printf("\nTEST FAILED: (%.5f MAX diff)\n\n", max_diff);
    else
        printf("\nTEST PASSED\n\n");
}
