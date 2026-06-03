/*
 * Optimized scalar Keccak-f[1600] for x86-64 with BMI2.
 *
 * Array-based two-buffer Theta+Rho+Pi+Chi+Iota, looped 12×2 rounds.
 * Compact code (~2KB) fits in L1I.  With -mbmi -mbmi2 -O2 the compiler
 * emits rorx/andn while keeping the loop body tight.
 */

#include <stdint.h>
#include <string.h>
#include "dap_hash_keccak.h"

static const uint64_t s_rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL
};

#define ROL64(a, n) (((a) << (n)) | ((a) >> (64 - (n))))

/*
 * One fused round: reads from A[25], writes to E[25].
 * Theta parity → D values → (Theta+Rho+Pi) per row → Chi → Iota.
 */
static inline void s_round(const uint64_t *restrict A, uint64_t *restrict E,
                           uint64_t rc)
{
    uint64_t c0 = A[0]^A[5]^A[10]^A[15]^A[20];
    uint64_t c1 = A[1]^A[6]^A[11]^A[16]^A[21];
    uint64_t c2 = A[2]^A[7]^A[12]^A[17]^A[22];
    uint64_t c3 = A[3]^A[8]^A[13]^A[18]^A[23];
    uint64_t c4 = A[4]^A[9]^A[14]^A[19]^A[24];

    uint64_t d0 = c4 ^ ROL64(c1, 1);
    uint64_t d1 = c0 ^ ROL64(c2, 1);
    uint64_t d2 = c1 ^ ROL64(c3, 1);
    uint64_t d3 = c2 ^ ROL64(c4, 1);
    uint64_t d4 = c3 ^ ROL64(c0, 1);

    uint64_t b0, b1, b2, b3, b4;

    b0 = A[ 0] ^ d0;
    b1 = ROL64(A[ 6] ^ d1, 44);
    b2 = ROL64(A[12] ^ d2, 43);
    b3 = ROL64(A[18] ^ d3, 21);
    b4 = ROL64(A[24] ^ d4, 14);
    E[0] = b0 ^ (~b1 & b2) ^ rc;
    E[1] = b1 ^ (~b2 & b3);
    E[2] = b2 ^ (~b3 & b4);
    E[3] = b3 ^ (~b4 & b0);
    E[4] = b4 ^ (~b0 & b1);

    b0 = ROL64(A[ 3] ^ d3, 28);
    b1 = ROL64(A[ 9] ^ d4, 20);
    b2 = ROL64(A[10] ^ d0,  3);
    b3 = ROL64(A[16] ^ d1, 45);
    b4 = ROL64(A[22] ^ d2, 61);
    E[5] = b0 ^ (~b1 & b2);
    E[6] = b1 ^ (~b2 & b3);
    E[7] = b2 ^ (~b3 & b4);
    E[8] = b3 ^ (~b4 & b0);
    E[9] = b4 ^ (~b0 & b1);

    b0 = ROL64(A[ 1] ^ d1,  1);
    b1 = ROL64(A[ 7] ^ d2,  6);
    b2 = ROL64(A[13] ^ d3, 25);
    b3 = ROL64(A[19] ^ d4,  8);
    b4 = ROL64(A[20] ^ d0, 18);
    E[10] = b0 ^ (~b1 & b2);
    E[11] = b1 ^ (~b2 & b3);
    E[12] = b2 ^ (~b3 & b4);
    E[13] = b3 ^ (~b4 & b0);
    E[14] = b4 ^ (~b0 & b1);

    b0 = ROL64(A[ 4] ^ d4, 27);
    b1 = ROL64(A[ 5] ^ d0, 36);
    b2 = ROL64(A[11] ^ d1, 10);
    b3 = ROL64(A[17] ^ d2, 15);
    b4 = ROL64(A[23] ^ d3, 56);
    E[15] = b0 ^ (~b1 & b2);
    E[16] = b1 ^ (~b2 & b3);
    E[17] = b2 ^ (~b3 & b4);
    E[18] = b3 ^ (~b4 & b0);
    E[19] = b4 ^ (~b0 & b1);

    b0 = ROL64(A[ 2] ^ d2, 62);
    b1 = ROL64(A[ 8] ^ d3, 55);
    b2 = ROL64(A[14] ^ d4, 39);
    b3 = ROL64(A[15] ^ d0, 41);
    b4 = ROL64(A[21] ^ d1,  2);
    E[20] = b0 ^ (~b1 & b2);
    E[21] = b1 ^ (~b2 & b3);
    E[22] = b2 ^ (~b3 & b4);
    E[23] = b3 ^ (~b4 & b0);
    E[24] = b4 ^ (~b0 & b1);
}

__attribute__((target("bmi,bmi2")))
void dap_hash_keccak_permute_scalar_bmi2(dap_hash_keccak_state_t *a_state)
{
    uint64_t E[25];
    uint64_t *A = a_state->lanes;

    for (int r = 0; r < 24; r += 2) {
        s_round(A, E, s_rc[r]);
        s_round(E, A, s_rc[r + 1]);
    }
}
