/*
 * LoTRS — parameter set definitions.
 */

#include "lotrs_params.h"

const lotrs_params_t LOTRS_PARAMS_TEST = {
    .d          = 32,
    .q          = 4194389ULL,       /* ~2^22 */
    .q_hat      = 7000061ULL,       /* ~2^23 */
    .beta       = 4,
    .kappa      = 1,
    .T          = 2,
    .k          = 2,
    .l          = 2,
    .l_prime    = 3,
    .n_hat      = 2,
    .k_hat      = 3,
    .w          = 4,
    .eta        = 1,
    .phi        = 12.0,
    .phi_a      = 12.0,
    .phi_b      = 12.0,
    .K_A        = 13,
    .K_B        = 4,
    .K_w        = 5,
    .x_seed_len = 16,
};

const lotrs_params_t LOTRS_PARAMS_BENCH_4OF32 = {
    .d          = 128,
    .q          = 274877906837ULL,   /* ~2^48 */
    .q_hat      = 274877906837ULL,
    .beta       = 32,
    .kappa      = 1,
    .T          = 4,
    .k          = 12,
    .l          = 5,
    .l_prime    = 6,
    .n_hat      = 11,
    .k_hat      = 8,
    .w          = 31,
    .eta        = 1,
    .phi        = 88.0,
    .phi_a      = 24.0,
    .phi_b      = 4.0,
    .K_A        = 28,
    .K_B        = 5,
    .K_w        = 5,
    .x_seed_len = 16,
};

const lotrs_params_t LOTRS_PARAMS_BENCH = {
    .d          = 128,
    .q          = 274877906837ULL,
    .q_hat      = 274877906837ULL,
    .beta       = 32,
    .kappa      = 1,
    .T          = 16,
    .k          = 12,
    .l          = 5,
    .l_prime    = 6,
    .n_hat      = 11,
    .k_hat      = 8,
    .w          = 31,
    .eta        = 1,
    .phi        = 352.0,
    .phi_a      = 24.0,
    .phi_b      = 4.0,
    .K_A        = 28,
    .K_B        = 5,
    .K_w        = 5,
    .x_seed_len = 16,
};
