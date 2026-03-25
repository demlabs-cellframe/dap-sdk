/*
 * Optimized scalar Keccak-f[1600] for x86-64 with BMI2.
 *
 * Two-buffer fused Theta+Rho+Pi+Chi+Iota.  24 rounds fully unrolled
 * via a macro that alternates between A[] and E[] named-variable sets.
 * With -mbmi -mbmi2 -O3 the compiler emits rorx/andn.
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
 * Fused round: Theta+Rho+Pi+Chi+Iota.
 * Reads 25 input vars (prefix I), writes 25 output vars (prefix O).
 * RC is the round constant.
 */
#define ROUND(                                                             \
    I0, I1, I2, I3, I4, I5, I6, I7, I8, I9,                              \
    I10,I11,I12,I13,I14,I15,I16,I17,I18,I19,                             \
    I20,I21,I22,I23,I24,                                                  \
    O0, O1, O2, O3, O4, O5, O6, O7, O8, O9,                              \
    O10,O11,O12,O13,O14,O15,O16,O17,O18,O19,                             \
    O20,O21,O22,O23,O24, RC)                                              \
do {                                                                      \
    uint64_t c0_ = I0^I5^I10^I15^I20, c1_ = I1^I6^I11^I16^I21,          \
             c2_ = I2^I7^I12^I17^I22, c3_ = I3^I8^I13^I18^I23,          \
             c4_ = I4^I9^I14^I19^I24;                                     \
    uint64_t d0_ = c4_ ^ ROL64(c1_, 1), d1_ = c0_ ^ ROL64(c2_, 1),      \
             d2_ = c1_ ^ ROL64(c3_, 1), d3_ = c2_ ^ ROL64(c4_, 1),      \
             d4_ = c3_ ^ ROL64(c0_, 1);                                   \
    uint64_t b0_, b1_, b2_, b3_, b4_;                                     \
    /* Row 0: pi_inv = {0,6,12,18,24} */                                  \
    b0_ = I0  ^ d0_;                                                      \
    b1_ = ROL64(I6  ^ d1_, 44);                                          \
    b2_ = ROL64(I12 ^ d2_, 43);                                          \
    b3_ = ROL64(I18 ^ d3_, 21);                                          \
    b4_ = ROL64(I24 ^ d4_, 14);                                          \
    O0 = b0_ ^ (~b1_ & b2_) ^ (RC);                                      \
    O1 = b1_ ^ (~b2_ & b3_);                                             \
    O2 = b2_ ^ (~b3_ & b4_);                                             \
    O3 = b3_ ^ (~b4_ & b0_);                                             \
    O4 = b4_ ^ (~b0_ & b1_);                                             \
    /* Row 1: pi_inv = {3,9,10,16,22} */                                  \
    b0_ = ROL64(I3  ^ d3_, 28);                                          \
    b1_ = ROL64(I9  ^ d4_, 20);                                          \
    b2_ = ROL64(I10 ^ d0_,  3);                                          \
    b3_ = ROL64(I16 ^ d1_, 45);                                          \
    b4_ = ROL64(I22 ^ d2_, 61);                                          \
    O5 = b0_ ^ (~b1_ & b2_);                                             \
    O6 = b1_ ^ (~b2_ & b3_);                                             \
    O7 = b2_ ^ (~b3_ & b4_);                                             \
    O8 = b3_ ^ (~b4_ & b0_);                                             \
    O9 = b4_ ^ (~b0_ & b1_);                                             \
    /* Row 2: pi_inv = {1,7,13,19,20} */                                  \
    b0_ = ROL64(I1  ^ d1_,  1);                                          \
    b1_ = ROL64(I7  ^ d2_,  6);                                          \
    b2_ = ROL64(I13 ^ d3_, 25);                                          \
    b3_ = ROL64(I19 ^ d4_,  8);                                          \
    b4_ = ROL64(I20 ^ d0_, 18);                                          \
    O10 = b0_ ^ (~b1_ & b2_);                                            \
    O11 = b1_ ^ (~b2_ & b3_);                                            \
    O12 = b2_ ^ (~b3_ & b4_);                                            \
    O13 = b3_ ^ (~b4_ & b0_);                                            \
    O14 = b4_ ^ (~b0_ & b1_);                                            \
    /* Row 3: pi_inv = {4,5,11,17,23} */                                  \
    b0_ = ROL64(I4  ^ d4_, 27);                                          \
    b1_ = ROL64(I5  ^ d0_, 36);                                          \
    b2_ = ROL64(I11 ^ d1_, 10);                                          \
    b3_ = ROL64(I17 ^ d2_, 15);                                          \
    b4_ = ROL64(I23 ^ d3_, 56);                                          \
    O15 = b0_ ^ (~b1_ & b2_);                                            \
    O16 = b1_ ^ (~b2_ & b3_);                                            \
    O17 = b2_ ^ (~b3_ & b4_);                                            \
    O18 = b3_ ^ (~b4_ & b0_);                                            \
    O19 = b4_ ^ (~b0_ & b1_);                                            \
    /* Row 4: pi_inv = {2,8,14,15,21} */                                  \
    b0_ = ROL64(I2  ^ d2_, 62);                                          \
    b1_ = ROL64(I8  ^ d3_, 55);                                          \
    b2_ = ROL64(I14 ^ d4_, 39);                                          \
    b3_ = ROL64(I15 ^ d0_, 41);                                          \
    b4_ = ROL64(I21 ^ d1_,  2);                                          \
    O20 = b0_ ^ (~b1_ & b2_);                                            \
    O21 = b1_ ^ (~b2_ & b3_);                                            \
    O22 = b2_ ^ (~b3_ & b4_);                                            \
    O23 = b3_ ^ (~b4_ & b0_);                                            \
    O24 = b4_ ^ (~b0_ & b1_);                                            \
} while (0)

/* Two full rounds: A → E → A */
#define TWO_ROUNDS(r)                                                      \
    ROUND(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,                                 \
          a10,a11,a12,a13,a14,a15,a16,a17,a18,a19,                        \
          a20,a21,a22,a23,a24,                                             \
          e0,e1,e2,e3,e4,e5,e6,e7,e8,e9,                                 \
          e10,e11,e12,e13,e14,e15,e16,e17,e18,e19,                        \
          e20,e21,e22,e23,e24, s_rc[(r)]);                                \
    ROUND(e0,e1,e2,e3,e4,e5,e6,e7,e8,e9,                                 \
          e10,e11,e12,e13,e14,e15,e16,e17,e18,e19,                        \
          e20,e21,e22,e23,e24,                                             \
          a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,                                 \
          a10,a11,a12,a13,a14,a15,a16,a17,a18,a19,                        \
          a20,a21,a22,a23,a24, s_rc[(r)+1])

void dap_hash_keccak_permute_scalar_bmi2(dap_hash_keccak_state_t *a_state)
{
    uint64_t a0  = a_state->lanes[0],  a1  = a_state->lanes[1],
             a2  = a_state->lanes[2],  a3  = a_state->lanes[3],
             a4  = a_state->lanes[4],  a5  = a_state->lanes[5],
             a6  = a_state->lanes[6],  a7  = a_state->lanes[7],
             a8  = a_state->lanes[8],  a9  = a_state->lanes[9],
             a10 = a_state->lanes[10], a11 = a_state->lanes[11],
             a12 = a_state->lanes[12], a13 = a_state->lanes[13],
             a14 = a_state->lanes[14], a15 = a_state->lanes[15],
             a16 = a_state->lanes[16], a17 = a_state->lanes[17],
             a18 = a_state->lanes[18], a19 = a_state->lanes[19],
             a20 = a_state->lanes[20], a21 = a_state->lanes[21],
             a22 = a_state->lanes[22], a23 = a_state->lanes[23],
             a24 = a_state->lanes[24];

    uint64_t e0, e1, e2, e3, e4, e5, e6, e7, e8, e9,
             e10, e11, e12, e13, e14, e15, e16, e17, e18, e19,
             e20, e21, e22, e23, e24;

    TWO_ROUNDS(0);   TWO_ROUNDS(2);   TWO_ROUNDS(4);
    TWO_ROUNDS(6);   TWO_ROUNDS(8);   TWO_ROUNDS(10);
    TWO_ROUNDS(12);  TWO_ROUNDS(14);  TWO_ROUNDS(16);
    TWO_ROUNDS(18);  TWO_ROUNDS(20);  TWO_ROUNDS(22);

    a_state->lanes[0]  = a0;   a_state->lanes[1]  = a1;
    a_state->lanes[2]  = a2;   a_state->lanes[3]  = a3;
    a_state->lanes[4]  = a4;   a_state->lanes[5]  = a5;
    a_state->lanes[6]  = a6;   a_state->lanes[7]  = a7;
    a_state->lanes[8]  = a8;   a_state->lanes[9]  = a9;
    a_state->lanes[10] = a10;  a_state->lanes[11] = a11;
    a_state->lanes[12] = a12;  a_state->lanes[13] = a13;
    a_state->lanes[14] = a14;  a_state->lanes[15] = a15;
    a_state->lanes[16] = a16;  a_state->lanes[17] = a17;
    a_state->lanes[18] = a18;  a_state->lanes[19] = a19;
    a_state->lanes[20] = a20;  a_state->lanes[21] = a21;
    a_state->lanes[22] = a22;  a_state->lanes[23] = a23;
    a_state->lanes[24] = a24;
}
