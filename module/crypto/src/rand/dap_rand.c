/*
 * Deterministic pseudo-random number generation using SHISHUA PRNG.
 *
 * Used for consensus-critical code where deterministic sequences
 * from a shared seed are required.
 *
 * OS-level secure random is in module/math/src/dap_rand.c.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_rand.h"
#include "dap_math_ops.h"
#include <stdatomic.h>

#define SHISHUA_TARGET 0
#include "shishua.h"

#define DAP_SHISHUA_BUFF_SIZE 4

static prng_state s_shishua_state = {0};
static uint256_t s_shishua_out[DAP_SHISHUA_BUFF_SIZE] __attribute__((aligned(128)));
static atomic_uint_fast8_t s_shishua_idx = 0;

void dap_pseudo_random_seed(uint256_t a_seed)
{
    uint64_t l_seed[4] = {a_seed._hi.a, a_seed._hi.b, a_seed._lo.a, a_seed._lo.b};
    prng_init(&s_shishua_state, l_seed);
    s_shishua_idx = 0;
}

uint256_t dap_pseudo_random_get(uint256_t a_rand_max, uint256_t *a_raw_result)
{
    uint256_t l_tmp, l_ret, l_rand_ceil;
    atomic_uint_fast8_t l_prev_idx = atomic_fetch_add(&s_shishua_idx, 1);
    int l_buf_pos = l_prev_idx % DAP_SHISHUA_BUFF_SIZE;
    if (l_buf_pos == 0)
        prng_gen(&s_shishua_state, (uint8_t *)s_shishua_out, DAP_SHISHUA_BUFF_SIZE * sizeof(uint256_t));
    if (IS_ZERO_256(a_rand_max))
        return uint256_0;
    uint256_t l_out_raw = s_shishua_out[l_buf_pos];
    if (a_raw_result)
        *a_raw_result = l_out_raw;
    if (EQUAL_256(a_rand_max, uint256_max))
        return l_out_raw;
    SUM_256_256(a_rand_max, uint256_1, &l_rand_ceil);
    divmod_impl_256(l_out_raw, l_rand_ceil, &l_tmp, &l_ret);
    return l_ret;
}
