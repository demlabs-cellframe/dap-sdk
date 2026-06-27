/*
 * C sponge wrappers for scalar BMI2 Keccak-f[1600] permutation.
 *
 * The permute kernel is in keccak_f1600_scalar_bmi2.S.
 * These functions implement fused absorb/squeeze with zero-init,
 * calling the scalar permute for each block.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "dap_hash_keccak.h"

extern void dap_hash_keccak_permute_scalar_bmi2(dap_hash_keccak_state_t *state);

#define KECCAK_ABSORB_IMPL(RATE, NWORDS)                                       \
void dap_keccak_absorb_##RATE##_scalar_bmi2(uint64_t *a_state,                 \
    const uint8_t *a_data, size_t a_len, uint8_t a_suffix)                     \
{                                                                              \
    memset(a_state, 0, 25 * sizeof(uint64_t));                                 \
    while (a_len >= RATE) {                                                    \
        const uint64_t *l_src = (const uint64_t *)a_data;                      \
        for (int i = 0; i < NWORDS; i++)                                       \
            a_state[i] ^= l_src[i];                                            \
        dap_hash_keccak_permute_scalar_bmi2(                                   \
            (dap_hash_keccak_state_t *)a_state);                               \
        a_data += RATE;                                                        \
        a_len  -= RATE;                                                        \
    }                                                                          \
    uint8_t l_pad[200] = {0};                                                  \
    memcpy(l_pad, a_data, a_len);                                              \
    l_pad[a_len] = a_suffix;                                                   \
    l_pad[RATE - 1] |= 0x80;                                                  \
    const uint64_t *l_pad64 = (const uint64_t *)l_pad;                         \
    for (int i = 0; i < NWORDS; i++)                                           \
        a_state[i] ^= l_pad64[i];                                             \
    dap_hash_keccak_permute_scalar_bmi2(                                       \
        (dap_hash_keccak_state_t *)a_state);                                   \
}

KECCAK_ABSORB_IMPL(136, 17)
KECCAK_ABSORB_IMPL(168, 21)
KECCAK_ABSORB_IMPL(72, 9)

/* See dap_keccak_ref.c (KECCAK_SQUEEZE_REF_IMPL) for the FIPS 202 sponge
 * convention this implements: extract first, then permute, so the state is
 * positioned at the next output block ready for streaming callers. */
#define KECCAK_SQUEEZE_IMPL(RATE, NWORDS)                                      \
void dap_keccak_squeeze_##RATE##_scalar_bmi2(uint64_t *a_state,                \
    uint8_t *a_out, size_t a_nblocks)                                          \
{                                                                              \
    (void)NWORDS;                                                              \
    for (size_t i = 0; i < a_nblocks; i++) {                                   \
        memcpy(a_out, a_state, RATE);                                          \
        a_out += RATE;                                                         \
        dap_hash_keccak_permute_scalar_bmi2(                                   \
            (dap_hash_keccak_state_t *)a_state);                               \
    }                                                                          \
}

KECCAK_SQUEEZE_IMPL(136, 17)
KECCAK_SQUEEZE_IMPL(168, 21)
KECCAK_SQUEEZE_IMPL(72, 9)
