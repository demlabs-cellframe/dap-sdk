/*
 * dap_mlkem_ntt_avx2.S — Hand-tuned AVX2+AVX-512VL NTT for ML-KEM (Kyber)
 *
 * Uses EVEX encoding for 32 YMM registers (ymm0-ymm31).
 * Requires: AVX2, AVX-512F, AVX-512VL, AVX-512BW.
 *
 * Register allocation:
 *   ymm0-ymm15   coefficient vectors r0-r15 (256 int16)
 *   ymm16-ymm19  scratch group A (Montgomery multiply)
 *   ymm20         current zeta broadcast
 *   ymm21-ymm24  scratch group B (Montgomery hi products)
 *   ymm25-ymm27  scratch for inner layers
 *   ymm28         Barrett constant V=20159 / NTT scale f=1441
 *   ymm29         extra scratch
 *   ymm30         QINV = -3327 broadcast
 *   ymm31         Q = 3329 broadcast
 *
 *   k1 = 0xCC    dword blend mask for layer 4
 *   k2 = 0xAA    dword blend mask for layer 2
 *   k3 = 0x0C    qword blend mask for layer 8 (upper 2 qwords from src2)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifdef __APPLE__
#define CDECL(x) _##x
#else
#define CDECL(x) x
#endif

.macro CONST_BROADCAST name, val
\name:
    .short \val, \val, \val, \val, \val, \val, \val, \val
    .short \val, \val, \val, \val, \val, \val, \val, \val
.endm

.macro CONST_HALF_BROADCAST name, lo, hi
\name:
    .short \lo, \lo, \lo, \lo, \lo, \lo, \lo, \lo
    .short \hi, \hi, \hi, \hi, \hi, \hi, \hi, \hi
.endm

.macro CONST_QUAD_BROADCAST name, q0, q1, q2, q3
\name:
    .short \q0, \q0, \q0, \q0
    .short \q1, \q1, \q1, \q1
    .short \q2, \q2, \q2, \q2
    .short \q3, \q3, \q3, \q3
.endm

.macro BASEMUL_ZETA4 z0, z1, z2, z3
    .short \z0, \z0, -\z0, -\z0, \z1, \z1, -\z1, -\z1
    .short \z2, \z2, -\z2, -\z2, \z3, \z3, -\z3, -\z3
.endm


/* ═══════════════════════════════════════════════════════════════════
 * READ-ONLY DATA
 * ═══════════════════════════════════════════════════════════════════ */
.section .rodata
.align 32

CONST_BROADCAST ntt_q, 3329
CONST_BROADCAST ntt_qinv, -3327
CONST_BROADCAST ntt_barrett_v, 20159
CONST_BROADCAST ntt_f_scale, 1441

/* Forward NTT zetas */
CONST_BROADCAST fwd_z_128_0, 2571

CONST_BROADCAST fwd_z_64_0, 2970
CONST_BROADCAST fwd_z_64_1, 1812

CONST_BROADCAST fwd_z_32_0, 1493
CONST_BROADCAST fwd_z_32_1, 1422
CONST_BROADCAST fwd_z_32_2, 287
CONST_BROADCAST fwd_z_32_3, 202

CONST_BROADCAST fwd_z_16_0, 3158
CONST_BROADCAST fwd_z_16_1, 622
CONST_BROADCAST fwd_z_16_2, 1577
CONST_BROADCAST fwd_z_16_3, 182
CONST_BROADCAST fwd_z_16_4, 962
CONST_BROADCAST fwd_z_16_5, 2127
CONST_BROADCAST fwd_z_16_6, 1855
CONST_BROADCAST fwd_z_16_7, 1468

CONST_BROADCAST fwd_z_8_0, 573
CONST_BROADCAST fwd_z_8_1, 2004
CONST_BROADCAST fwd_z_8_2, 264
CONST_BROADCAST fwd_z_8_3, 383
CONST_BROADCAST fwd_z_8_4, 2500
CONST_BROADCAST fwd_z_8_5, 1458
CONST_BROADCAST fwd_z_8_6, 1727
CONST_BROADCAST fwd_z_8_7, 3199
CONST_BROADCAST fwd_z_8_8, 2648
CONST_BROADCAST fwd_z_8_9, 1017
CONST_BROADCAST fwd_z_8_10, 732
CONST_BROADCAST fwd_z_8_11, 608
CONST_BROADCAST fwd_z_8_12, 1787
CONST_BROADCAST fwd_z_8_13, 411
CONST_BROADCAST fwd_z_8_14, 3124
CONST_BROADCAST fwd_z_8_15, 1758

CONST_HALF_BROADCAST fwd_z_4_0, 1223, 652
CONST_HALF_BROADCAST fwd_z_4_1, 2777, 1015
CONST_HALF_BROADCAST fwd_z_4_2, 2036, 1491
CONST_HALF_BROADCAST fwd_z_4_3, 3047, 1785
CONST_HALF_BROADCAST fwd_z_4_4, 516, 3321
CONST_HALF_BROADCAST fwd_z_4_5, 3009, 2663
CONST_HALF_BROADCAST fwd_z_4_6, 1711, 2167
CONST_HALF_BROADCAST fwd_z_4_7, 126, 1469
CONST_HALF_BROADCAST fwd_z_4_8, 2476, 3239
CONST_HALF_BROADCAST fwd_z_4_9, 3058, 830
CONST_HALF_BROADCAST fwd_z_4_10, 107, 1908
CONST_HALF_BROADCAST fwd_z_4_11, 3082, 2378
CONST_HALF_BROADCAST fwd_z_4_12, 2931, 961
CONST_HALF_BROADCAST fwd_z_4_13, 1821, 2604
CONST_HALF_BROADCAST fwd_z_4_14, 448, 2264
CONST_HALF_BROADCAST fwd_z_4_15, 677, 2054

CONST_QUAD_BROADCAST fwd_z_2_0, 2226, 430, 555, 843
CONST_QUAD_BROADCAST fwd_z_2_1, 2078, 871, 1550, 105
CONST_QUAD_BROADCAST fwd_z_2_2, 422, 587, 177, 3094
CONST_QUAD_BROADCAST fwd_z_2_3, 3038, 2869, 1574, 1653
CONST_QUAD_BROADCAST fwd_z_2_4, 3083, 778, 1159, 3182
CONST_QUAD_BROADCAST fwd_z_2_5, 2552, 1483, 2727, 1119
CONST_QUAD_BROADCAST fwd_z_2_6, 1739, 644, 2457, 349
CONST_QUAD_BROADCAST fwd_z_2_7, 418, 329, 3173, 3254
CONST_QUAD_BROADCAST fwd_z_2_8, 817, 1097, 603, 610
CONST_QUAD_BROADCAST fwd_z_2_9, 1322, 2044, 1864, 384
CONST_QUAD_BROADCAST fwd_z_2_10, 2114, 3193, 1218, 1994
CONST_QUAD_BROADCAST fwd_z_2_11, 2455, 220, 2142, 1670
CONST_QUAD_BROADCAST fwd_z_2_12, 2144, 1799, 2051, 794
CONST_QUAD_BROADCAST fwd_z_2_13, 1819, 2475, 2459, 478
CONST_QUAD_BROADCAST fwd_z_2_14, 3221, 3021, 996, 991
CONST_QUAD_BROADCAST fwd_z_2_15, 958, 1869, 1522, 1628

/* Inverse NTT zetas */
CONST_QUAD_BROADCAST inv_z_2_0, 1701, 1807, 1460, 2371
CONST_QUAD_BROADCAST inv_z_2_1, 2338, 2333, 308, 108
CONST_QUAD_BROADCAST inv_z_2_2, 2851, 870, 854, 1510
CONST_QUAD_BROADCAST inv_z_2_3, 2535, 1278, 1530, 1185
CONST_QUAD_BROADCAST inv_z_2_4, 1659, 1187, 3109, 874
CONST_QUAD_BROADCAST inv_z_2_5, 1335, 2111, 136, 1215
CONST_QUAD_BROADCAST inv_z_2_6, 2945, 1465, 1285, 2007
CONST_QUAD_BROADCAST inv_z_2_7, 2719, 2726, 2232, 2512
CONST_QUAD_BROADCAST inv_z_2_8, 75, 156, 3000, 2911
CONST_QUAD_BROADCAST inv_z_2_9, 2980, 872, 2685, 1590
CONST_QUAD_BROADCAST inv_z_2_10, 2210, 602, 1846, 777
CONST_QUAD_BROADCAST inv_z_2_11, 147, 2170, 2551, 246
CONST_QUAD_BROADCAST inv_z_2_12, 1676, 1755, 460, 291
CONST_QUAD_BROADCAST inv_z_2_13, 235, 3152, 2742, 2907
CONST_QUAD_BROADCAST inv_z_2_14, 3224, 1779, 2458, 1251
CONST_QUAD_BROADCAST inv_z_2_15, 2486, 2774, 2899, 1103

CONST_HALF_BROADCAST inv_z_4_0, 1275, 2652
CONST_HALF_BROADCAST inv_z_4_1, 1065, 2881
CONST_HALF_BROADCAST inv_z_4_2, 725, 1508
CONST_HALF_BROADCAST inv_z_4_3, 2368, 398
CONST_HALF_BROADCAST inv_z_4_4, 951, 247
CONST_HALF_BROADCAST inv_z_4_5, 1421, 3222
CONST_HALF_BROADCAST inv_z_4_6, 2499, 271
CONST_HALF_BROADCAST inv_z_4_7, 90, 853
CONST_HALF_BROADCAST inv_z_4_8, 1860, 3203
CONST_HALF_BROADCAST inv_z_4_9, 1162, 1618
CONST_HALF_BROADCAST inv_z_4_10, 666, 320
CONST_HALF_BROADCAST inv_z_4_11, 8, 2813
CONST_HALF_BROADCAST inv_z_4_12, 1544, 282
CONST_HALF_BROADCAST inv_z_4_13, 1838, 1293
CONST_HALF_BROADCAST inv_z_4_14, 2314, 552
CONST_HALF_BROADCAST inv_z_4_15, 2677, 2106

CONST_BROADCAST inv_z_8_0, 1571
CONST_BROADCAST inv_z_8_1, 205
CONST_BROADCAST inv_z_8_2, 2918
CONST_BROADCAST inv_z_8_3, 1542
CONST_BROADCAST inv_z_8_4, 2721
CONST_BROADCAST inv_z_8_5, 2597
CONST_BROADCAST inv_z_8_6, 2312
CONST_BROADCAST inv_z_8_7, 681
CONST_BROADCAST inv_z_8_8, 130
CONST_BROADCAST inv_z_8_9, 1602
CONST_BROADCAST inv_z_8_10, 1871
CONST_BROADCAST inv_z_8_11, 829
CONST_BROADCAST inv_z_8_12, 2946
CONST_BROADCAST inv_z_8_13, 3065
CONST_BROADCAST inv_z_8_14, 1325
CONST_BROADCAST inv_z_8_15, 2756

CONST_BROADCAST inv_z_16_0, 1861
CONST_BROADCAST inv_z_16_1, 1474
CONST_BROADCAST inv_z_16_2, 1202
CONST_BROADCAST inv_z_16_3, 2367
CONST_BROADCAST inv_z_16_4, 3147
CONST_BROADCAST inv_z_16_5, 1752
CONST_BROADCAST inv_z_16_6, 2707
CONST_BROADCAST inv_z_16_7, 171

CONST_BROADCAST inv_z_32_0, 3127
CONST_BROADCAST inv_z_32_1, 3042
CONST_BROADCAST inv_z_32_2, 1907
CONST_BROADCAST inv_z_32_3, 1836

CONST_BROADCAST inv_z_64_0, 1517
CONST_BROADCAST inv_z_64_1, 359

CONST_BROADCAST inv_z_128_0, 758

/* Basemul: adjacent int16 swap mask for vpshufb */
.align 32
basemul_swap_mask:
    .byte 2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13
    .byte 2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13

/* Basemul: zetas[64..127] expanded to [+z, +z, -z, -z] per dword */
.align 32
basemul_zetas:
    BASEMUL_ZETA4 2226, 430, 555, 843
    BASEMUL_ZETA4 2078, 871, 1550, 105
    BASEMUL_ZETA4 422, 587, 177, 3094
    BASEMUL_ZETA4 3038, 2869, 1574, 1653
    BASEMUL_ZETA4 3083, 778, 1159, 3182
    BASEMUL_ZETA4 2552, 1483, 2727, 1119
    BASEMUL_ZETA4 1739, 644, 2457, 349
    BASEMUL_ZETA4 418, 329, 3173, 3254
    BASEMUL_ZETA4 817, 1097, 603, 610
    BASEMUL_ZETA4 1322, 2044, 1864, 384
    BASEMUL_ZETA4 2114, 3193, 1218, 1994
    BASEMUL_ZETA4 2455, 220, 2142, 1670
    BASEMUL_ZETA4 2144, 1799, 2051, 794
    BASEMUL_ZETA4 1819, 2475, 2459, 478
    BASEMUL_ZETA4 3221, 3021, 996, 991
    BASEMUL_ZETA4 958, 1869, 1522, 1628


/* ═══════════════════════════════════════════════════════════════════
 * MACROS — All use EVEX-compatible instructions
 * ═══════════════════════════════════════════════════════════════════ */

/*
 * Cooley-Tukey butterfly:  t = fqmul(z, b);  a' = a+t;  b' = a-t
 * Scratch: ymm16, ymm17.  Order: SUB before ADD preserves old a.
 */
.macro CT_BF a, b, z
    vpmullw \z, \b, %ymm16
    vpmulhw \z, \b, %ymm17
    vpmullw %ymm30, %ymm16, %ymm16
    vpmulhw %ymm31, %ymm16, %ymm16
    vpsubw  %ymm16, %ymm17, %ymm16
    vpsubw  %ymm16, \a, \b
    vpaddw  %ymm16, \a, \a
.endm

/*
 * 4-wide interleaved CT butterfly (same zeta) — hides 3-cycle vpmul latency.
 * Scratch: ymm16-ymm19, ymm21-ymm24.
 */
.macro CT_BF4 a0, b0, a1, b1, a2, b2, a3, b3, z
    vpmullw \z, \b0, %ymm16
    vpmullw \z, \b1, %ymm17
    vpmullw \z, \b2, %ymm18
    vpmullw \z, \b3, %ymm19
    vpmulhw \z, \b0, %ymm21
    vpmulhw \z, \b1, %ymm22
    vpmulhw \z, \b2, %ymm23
    vpmulhw \z, \b3, %ymm24
    vpmullw %ymm30, %ymm16, %ymm16
    vpmullw %ymm30, %ymm17, %ymm17
    vpmullw %ymm30, %ymm18, %ymm18
    vpmullw %ymm30, %ymm19, %ymm19
    vpmulhw %ymm31, %ymm16, %ymm16
    vpmulhw %ymm31, %ymm17, %ymm17
    vpmulhw %ymm31, %ymm18, %ymm18
    vpmulhw %ymm31, %ymm19, %ymm19
    vpsubw  %ymm16, %ymm21, %ymm16
    vpsubw  %ymm17, %ymm22, %ymm17
    vpsubw  %ymm18, %ymm23, %ymm18
    vpsubw  %ymm19, %ymm24, %ymm19
    vpsubw  %ymm16, \a0, \b0
    vpaddw  %ymm16, \a0, \a0
    vpsubw  %ymm17, \a1, \b1
    vpaddw  %ymm17, \a1, \a1
    vpsubw  %ymm18, \a2, \b2
    vpaddw  %ymm18, \a2, \a2
    vpsubw  %ymm19, \a3, \b3
    vpaddw  %ymm19, \a3, \a3
.endm

/*
 * Gentleman-Sande butterfly:  t = a-b;  a' = barrett(a+b);  b' = fqmul(z, t)
 * Scratch: ymm16, ymm17, ymm25.
 */
.macro GS_BF a, b, z
    vpsubw  \b, \a, %ymm25
    vpaddw  \b, \a, \a
    BARRETT_REDUCE \a
    vpmullw \z, %ymm25, %ymm16
    vpmulhw \z, %ymm25, %ymm17
    vpmullw %ymm30, %ymm16, %ymm16
    vpmulhw %ymm31, %ymm16, %ymm16
    vpsubw  %ymm16, %ymm17, \b
.endm

/*
 * 4-wide interleaved GS butterfly with Barrett reduction on sums.
 * Scratch: ymm16-ymm19, ymm21-ymm27, ymm29.
 */
.macro GS_BF4 a0, b0, a1, b1, a2, b2, a3, b3, z
    vpsubw  \b0, \a0, %ymm25
    vpsubw  \b1, \a1, %ymm26
    vpsubw  \b2, \a2, %ymm27
    vpsubw  \b3, \a3, %ymm29
    vpaddw  \b0, \a0, \a0
    vpaddw  \b1, \a1, \a1
    vpaddw  \b2, \a2, \a2
    vpaddw  \b3, \a3, \a3
    BARRETT_REDUCE \a0
    BARRETT_REDUCE \a1
    BARRETT_REDUCE \a2
    BARRETT_REDUCE \a3
    vpmullw \z, %ymm25, %ymm16
    vpmullw \z, %ymm26, %ymm17
    vpmullw \z, %ymm27, %ymm18
    vpmullw \z, %ymm29, %ymm19
    vpmulhw \z, %ymm25, %ymm21
    vpmulhw \z, %ymm26, %ymm22
    vpmulhw \z, %ymm27, %ymm23
    vpmulhw \z, %ymm29, %ymm24
    vpmullw %ymm30, %ymm16, %ymm16
    vpmullw %ymm30, %ymm17, %ymm17
    vpmullw %ymm30, %ymm18, %ymm18
    vpmullw %ymm30, %ymm19, %ymm19
    vpmulhw %ymm31, %ymm16, %ymm16
    vpmulhw %ymm31, %ymm17, %ymm17
    vpmulhw %ymm31, %ymm18, %ymm18
    vpmulhw %ymm31, %ymm19, %ymm19
    vpsubw  %ymm16, %ymm21, \b0
    vpsubw  %ymm17, %ymm22, \b1
    vpsubw  %ymm18, %ymm23, \b2
    vpsubw  %ymm19, %ymm24, \b3
.endm

/*
 * Barrett reduction: a' = a - round(a * v / 2^26) * q
 * Scratch: ymm16.
 */
.macro BARRETT_REDUCE reg
    vpmulhw %ymm28, \reg, %ymm16
    vpsraw  $10, %ymm16, %ymm16
    vpmullw %ymm31, %ymm16, %ymm16
    vpsubw  %ymm16, \reg, \reg
.endm

/*
 * fqmul: dst = Montgomery_reduce(z * src)
 * Scratch: ymm16, ymm17.
 */
.macro FQMUL dst, z, src
    vpmullw \z, \src, %ymm16
    vpmulhw \z, \src, %ymm17
    vpmullw %ymm30, %ymm16, %ymm16
    vpmulhw %ymm31, %ymm16, %ymm16
    vpsubw  %ymm16, %ymm17, \dst
.endm

/*
 * Forward layer 8: butterfly across 128-bit halves.
 * vpermq (EVEX) replaces vperm2i128 (VEX only).
 * vpblendmq with k3 replaces VEX-only vpblendd for 128-bit halves.
 * Scratch: ymm16, ymm17, ymm20, ymm25, ymm26.
 */
.macro FWD_LAYER8 v, zeta_addr
    vmovdqa32 \zeta_addr(%rip), %ymm20
    vpermq  $0x44, \v, %ymm25
    vpermq  $0xEE, \v, %ymm26
    vpmullw %ymm20, %ymm26, %ymm16
    vpmulhw %ymm20, %ymm26, %ymm17
    vpmullw %ymm30, %ymm16, %ymm16
    vpmulhw %ymm31, %ymm16, %ymm16
    vpsubw  %ymm16, %ymm17, %ymm16
    vpsubw  %ymm16, %ymm25, %ymm26
    vpaddw  %ymm16, %ymm25, %ymm25
    vpblendmq %ymm26, %ymm25, \v{%k3}
.endm

/*
 * Forward layer 4: butterfly across 64-bit quadrants.
 * Scratch: ymm16, ymm17, ymm20, ymm25, ymm26.
 */
.macro FWD_LAYER4 v, zeta_addr
    vmovdqa32 \zeta_addr(%rip), %ymm20
    vpshufd $0x44, \v, %ymm25
    vpshufd $0xEE, \v, %ymm26
    vpmullw %ymm20, %ymm26, %ymm16
    vpmulhw %ymm20, %ymm26, %ymm17
    vpmullw %ymm30, %ymm16, %ymm16
    vpmulhw %ymm31, %ymm16, %ymm16
    vpsubw  %ymm16, %ymm17, %ymm16
    vpaddw  %ymm16, %ymm25, %ymm17
    vpsubw  %ymm16, %ymm25, %ymm25
    vpblendmd %ymm25, %ymm17, \v{%k1}
.endm

/*
 * Forward layer 2: butterfly across 32-bit pairs.
 * Scratch: ymm16, ymm17, ymm20, ymm25, ymm26.
 */
.macro FWD_LAYER2 v, zeta_addr
    vmovdqa32 \zeta_addr(%rip), %ymm20
    vpshufd $0xA0, \v, %ymm25
    vpshufd $0xF5, \v, %ymm26
    vpmullw %ymm20, %ymm26, %ymm16
    vpmulhw %ymm20, %ymm26, %ymm17
    vpmullw %ymm30, %ymm16, %ymm16
    vpmulhw %ymm31, %ymm16, %ymm16
    vpsubw  %ymm16, %ymm17, %ymm16
    vpaddw  %ymm16, %ymm25, %ymm17
    vpsubw  %ymm16, %ymm25, %ymm25
    vpblendmd %ymm25, %ymm17, \v{%k2}
.endm

/*
 * Inverse layer 2: GS butterfly across 32-bit pairs.
 * Barrett reduction on sum, fqmul on diff.
 * Scratch: ymm16, ymm17, ymm18, ymm20, ymm25, ymm26.
 */
.macro INV_LAYER2 v, zeta_addr
    vmovdqa32 \zeta_addr(%rip), %ymm20
    vpshufd $0xA0, \v, %ymm25
    vpshufd $0xF5, \v, %ymm26
    vpaddw  %ymm26, %ymm25, %ymm17
    vpsubw  %ymm26, %ymm25, %ymm25
    BARRETT_REDUCE %ymm17
    vpmullw %ymm20, %ymm25, %ymm16
    vpmulhw %ymm20, %ymm25, %ymm18
    vpmullw %ymm30, %ymm16, %ymm16
    vpmulhw %ymm31, %ymm16, %ymm16
    vpsubw  %ymm16, %ymm18, %ymm25
    vpblendmd %ymm25, %ymm17, \v{%k2}
.endm

/*
 * Inverse layer 4: GS butterfly across 64-bit quadrants.
 * Barrett reduction on sum, fqmul on diff.
 * Scratch: ymm16, ymm17, ymm18, ymm20, ymm25, ymm26.
 */
.macro INV_LAYER4 v, zeta_addr
    vmovdqa32 \zeta_addr(%rip), %ymm20
    vpshufd $0x44, \v, %ymm25
    vpshufd $0xEE, \v, %ymm26
    vpaddw  %ymm26, %ymm25, %ymm17
    vpsubw  %ymm26, %ymm25, %ymm25
    BARRETT_REDUCE %ymm17
    vpmullw %ymm20, %ymm25, %ymm16
    vpmulhw %ymm20, %ymm25, %ymm18
    vpmullw %ymm30, %ymm16, %ymm16
    vpmulhw %ymm31, %ymm16, %ymm16
    vpsubw  %ymm16, %ymm18, %ymm25
    vpblendmd %ymm25, %ymm17, \v{%k1}
.endm

/*
 * Inverse layer 8: GS butterfly across 128-bit halves.
 * Barrett reduction on sum, fqmul on diff.
 * Scratch: ymm16, ymm17, ymm18, ymm20, ymm25, ymm26.
 */
.macro INV_LAYER8 v, zeta_addr
    vmovdqa32 \zeta_addr(%rip), %ymm20
    vpermq  $0x44, \v, %ymm25
    vpermq  $0xEE, \v, %ymm26
    vpaddw  %ymm26, %ymm25, %ymm17
    vpsubw  %ymm26, %ymm25, %ymm25
    BARRETT_REDUCE %ymm17
    vpmullw %ymm20, %ymm25, %ymm16
    vpmulhw %ymm20, %ymm25, %ymm18
    vpmullw %ymm30, %ymm16, %ymm16
    vpmulhw %ymm31, %ymm16, %ymm16
    vpsubw  %ymm16, %ymm18, %ymm25
    vpblendmq %ymm25, %ymm17, \v{%k3}
.endm


/* ═══════════════════════════════════════════════════════════════════
 * FORWARD NTT
 * void dap_mlkem_ntt_forward_asm(int16_t coeffs[256]);
 * ═══════════════════════════════════════════════════════════════════ */

.text
.globl CDECL(dap_mlkem_ntt_forward_asm)
.type  CDECL(dap_mlkem_ntt_forward_asm), @function
.align 64
CDECL(dap_mlkem_ntt_forward_asm):
    /* Set up k-mask registers (used by inner layer blends) */
    movl    $0xCC, %eax
    kmovb   %eax, %k1
    movl    $0xAA, %eax
    kmovb   %eax, %k2
    movl    $0x0C, %eax
    kmovb   %eax, %k3

    /* Load constants */
    vmovdqa32 ntt_q(%rip), %ymm31
    vmovdqa32 ntt_qinv(%rip), %ymm30
    vmovdqa32 ntt_barrett_v(%rip), %ymm28

    /* Load 256 coefficients into ymm0-ymm15 */
    vmovdqu     (%rdi), %ymm0
    vmovdqu  32(%rdi), %ymm1
    vmovdqu  64(%rdi), %ymm2
    vmovdqu  96(%rdi), %ymm3
    vmovdqu 128(%rdi), %ymm4
    vmovdqu 160(%rdi), %ymm5
    vmovdqu 192(%rdi), %ymm6
    vmovdqu 224(%rdi), %ymm7
    vmovdqu 256(%rdi), %ymm8
    vmovdqu 288(%rdi), %ymm9
    vmovdqu 320(%rdi), %ymm10
    vmovdqu 352(%rdi), %ymm11
    vmovdqu 384(%rdi), %ymm12
    vmovdqu 416(%rdi), %ymm13
    vmovdqu 448(%rdi), %ymm14
    vmovdqu 480(%rdi), %ymm15

    /* ── Layer 128: 8 butterflies, zeta[1] ── */
    vmovdqa32 fwd_z_128_0(%rip), %ymm20
    CT_BF4 %ymm0, %ymm8,  %ymm1, %ymm9,  %ymm2, %ymm10, %ymm3, %ymm11, %ymm20
    CT_BF4 %ymm4, %ymm12, %ymm5, %ymm13, %ymm6, %ymm14, %ymm7, %ymm15, %ymm20

    /* ── Layer 64 ── */
    vmovdqa32 fwd_z_64_0(%rip), %ymm20
    CT_BF4 %ymm0, %ymm4, %ymm1, %ymm5, %ymm2, %ymm6, %ymm3, %ymm7, %ymm20
    vmovdqa32 fwd_z_64_1(%rip), %ymm20
    CT_BF4 %ymm8, %ymm12, %ymm9, %ymm13, %ymm10, %ymm14, %ymm11, %ymm15, %ymm20

    /* ── Layer 32 ── */
    vmovdqa32 fwd_z_32_0(%rip), %ymm20
    CT_BF %ymm0, %ymm2, %ymm20
    CT_BF %ymm1, %ymm3, %ymm20
    vmovdqa32 fwd_z_32_1(%rip), %ymm20
    CT_BF %ymm4, %ymm6, %ymm20
    CT_BF %ymm5, %ymm7, %ymm20
    vmovdqa32 fwd_z_32_2(%rip), %ymm20
    CT_BF %ymm8,  %ymm10, %ymm20
    CT_BF %ymm9,  %ymm11, %ymm20
    vmovdqa32 fwd_z_32_3(%rip), %ymm20
    CT_BF %ymm12, %ymm14, %ymm20
    CT_BF %ymm13, %ymm15, %ymm20

    /* ── Layer 16 ── */
    vmovdqa32 fwd_z_16_0(%rip), %ymm20
    CT_BF %ymm0,  %ymm1,  %ymm20
    vmovdqa32 fwd_z_16_1(%rip), %ymm20
    CT_BF %ymm2,  %ymm3,  %ymm20
    vmovdqa32 fwd_z_16_2(%rip), %ymm20
    CT_BF %ymm4,  %ymm5,  %ymm20
    vmovdqa32 fwd_z_16_3(%rip), %ymm20
    CT_BF %ymm6,  %ymm7,  %ymm20
    vmovdqa32 fwd_z_16_4(%rip), %ymm20
    CT_BF %ymm8,  %ymm9,  %ymm20
    vmovdqa32 fwd_z_16_5(%rip), %ymm20
    CT_BF %ymm10, %ymm11, %ymm20
    vmovdqa32 fwd_z_16_6(%rip), %ymm20
    CT_BF %ymm12, %ymm13, %ymm20
    vmovdqa32 fwd_z_16_7(%rip), %ymm20
    CT_BF %ymm14, %ymm15, %ymm20

    /* ── Layer 8: in-register, 128-bit half butterfly ── */
    FWD_LAYER8 %ymm0,  fwd_z_8_0
    FWD_LAYER8 %ymm1,  fwd_z_8_1
    FWD_LAYER8 %ymm2,  fwd_z_8_2
    FWD_LAYER8 %ymm3,  fwd_z_8_3
    FWD_LAYER8 %ymm4,  fwd_z_8_4
    FWD_LAYER8 %ymm5,  fwd_z_8_5
    FWD_LAYER8 %ymm6,  fwd_z_8_6
    FWD_LAYER8 %ymm7,  fwd_z_8_7
    FWD_LAYER8 %ymm8,  fwd_z_8_8
    FWD_LAYER8 %ymm9,  fwd_z_8_9
    FWD_LAYER8 %ymm10, fwd_z_8_10
    FWD_LAYER8 %ymm11, fwd_z_8_11
    FWD_LAYER8 %ymm12, fwd_z_8_12
    FWD_LAYER8 %ymm13, fwd_z_8_13
    FWD_LAYER8 %ymm14, fwd_z_8_14
    FWD_LAYER8 %ymm15, fwd_z_8_15

    /* ── Layer 4: in-register, 64-bit quadrant butterfly ── */
    FWD_LAYER4 %ymm0,  fwd_z_4_0
    FWD_LAYER4 %ymm1,  fwd_z_4_1
    FWD_LAYER4 %ymm2,  fwd_z_4_2
    FWD_LAYER4 %ymm3,  fwd_z_4_3
    FWD_LAYER4 %ymm4,  fwd_z_4_4
    FWD_LAYER4 %ymm5,  fwd_z_4_5
    FWD_LAYER4 %ymm6,  fwd_z_4_6
    FWD_LAYER4 %ymm7,  fwd_z_4_7
    FWD_LAYER4 %ymm8,  fwd_z_4_8
    FWD_LAYER4 %ymm9,  fwd_z_4_9
    FWD_LAYER4 %ymm10, fwd_z_4_10
    FWD_LAYER4 %ymm11, fwd_z_4_11
    FWD_LAYER4 %ymm12, fwd_z_4_12
    FWD_LAYER4 %ymm13, fwd_z_4_13
    FWD_LAYER4 %ymm14, fwd_z_4_14
    FWD_LAYER4 %ymm15, fwd_z_4_15

    /* ── Layer 2: in-register, 32-bit pair butterfly ── */
    FWD_LAYER2 %ymm0,  fwd_z_2_0
    FWD_LAYER2 %ymm1,  fwd_z_2_1
    FWD_LAYER2 %ymm2,  fwd_z_2_2
    FWD_LAYER2 %ymm3,  fwd_z_2_3
    FWD_LAYER2 %ymm4,  fwd_z_2_4
    FWD_LAYER2 %ymm5,  fwd_z_2_5
    FWD_LAYER2 %ymm6,  fwd_z_2_6
    FWD_LAYER2 %ymm7,  fwd_z_2_7
    FWD_LAYER2 %ymm8,  fwd_z_2_8
    FWD_LAYER2 %ymm9,  fwd_z_2_9
    FWD_LAYER2 %ymm10, fwd_z_2_10
    FWD_LAYER2 %ymm11, fwd_z_2_11
    FWD_LAYER2 %ymm12, fwd_z_2_12
    FWD_LAYER2 %ymm13, fwd_z_2_13
    FWD_LAYER2 %ymm14, fwd_z_2_14
    FWD_LAYER2 %ymm15, fwd_z_2_15

    /* ── Fused Barrett reduction ── */
    BARRETT_REDUCE %ymm0
    BARRETT_REDUCE %ymm1
    BARRETT_REDUCE %ymm2
    BARRETT_REDUCE %ymm3
    BARRETT_REDUCE %ymm4
    BARRETT_REDUCE %ymm5
    BARRETT_REDUCE %ymm6
    BARRETT_REDUCE %ymm7
    BARRETT_REDUCE %ymm8
    BARRETT_REDUCE %ymm9
    BARRETT_REDUCE %ymm10
    BARRETT_REDUCE %ymm11
    BARRETT_REDUCE %ymm12
    BARRETT_REDUCE %ymm13
    BARRETT_REDUCE %ymm14
    BARRETT_REDUCE %ymm15

    /* ── Store results ── */
    vmovdqu %ymm0,      (%rdi)
    vmovdqu %ymm1,   32(%rdi)
    vmovdqu %ymm2,   64(%rdi)
    vmovdqu %ymm3,   96(%rdi)
    vmovdqu %ymm4,  128(%rdi)
    vmovdqu %ymm5,  160(%rdi)
    vmovdqu %ymm6,  192(%rdi)
    vmovdqu %ymm7,  224(%rdi)
    vmovdqu %ymm8,  256(%rdi)
    vmovdqu %ymm9,  288(%rdi)
    vmovdqu %ymm10, 320(%rdi)
    vmovdqu %ymm11, 352(%rdi)
    vmovdqu %ymm12, 384(%rdi)
    vmovdqu %ymm13, 416(%rdi)
    vmovdqu %ymm14, 448(%rdi)
    vmovdqu %ymm15, 480(%rdi)

    vzeroupper
    ret
.size CDECL(dap_mlkem_ntt_forward_asm), .-CDECL(dap_mlkem_ntt_forward_asm)


/* ═══════════════════════════════════════════════════════════════════
 * INVERSE NTT
 * void dap_mlkem_ntt_inverse_asm(int16_t coeffs[256]);
 * ═══════════════════════════════════════════════════════════════════ */

.align 64
.globl CDECL(dap_mlkem_ntt_inverse_asm)
.type  CDECL(dap_mlkem_ntt_inverse_asm), @function
CDECL(dap_mlkem_ntt_inverse_asm):
    movl    $0xCC, %eax
    kmovb   %eax, %k1
    movl    $0xAA, %eax
    kmovb   %eax, %k2
    movl    $0x0C, %eax
    kmovb   %eax, %k3

    vmovdqa32 ntt_q(%rip), %ymm31
    vmovdqa32 ntt_qinv(%rip), %ymm30
    vmovdqa32 ntt_barrett_v(%rip), %ymm28

    vmovdqu     (%rdi), %ymm0
    vmovdqu  32(%rdi), %ymm1
    vmovdqu  64(%rdi), %ymm2
    vmovdqu  96(%rdi), %ymm3
    vmovdqu 128(%rdi), %ymm4
    vmovdqu 160(%rdi), %ymm5
    vmovdqu 192(%rdi), %ymm6
    vmovdqu 224(%rdi), %ymm7
    vmovdqu 256(%rdi), %ymm8
    vmovdqu 288(%rdi), %ymm9
    vmovdqu 320(%rdi), %ymm10
    vmovdqu 352(%rdi), %ymm11
    vmovdqu 384(%rdi), %ymm12
    vmovdqu 416(%rdi), %ymm13
    vmovdqu 448(%rdi), %ymm14
    vmovdqu 480(%rdi), %ymm15

    /* ── Layer 2 ── */
    INV_LAYER2 %ymm0,  inv_z_2_0
    INV_LAYER2 %ymm1,  inv_z_2_1
    INV_LAYER2 %ymm2,  inv_z_2_2
    INV_LAYER2 %ymm3,  inv_z_2_3
    INV_LAYER2 %ymm4,  inv_z_2_4
    INV_LAYER2 %ymm5,  inv_z_2_5
    INV_LAYER2 %ymm6,  inv_z_2_6
    INV_LAYER2 %ymm7,  inv_z_2_7
    INV_LAYER2 %ymm8,  inv_z_2_8
    INV_LAYER2 %ymm9,  inv_z_2_9
    INV_LAYER2 %ymm10, inv_z_2_10
    INV_LAYER2 %ymm11, inv_z_2_11
    INV_LAYER2 %ymm12, inv_z_2_12
    INV_LAYER2 %ymm13, inv_z_2_13
    INV_LAYER2 %ymm14, inv_z_2_14
    INV_LAYER2 %ymm15, inv_z_2_15

    /* ── Layer 4 ── */
    INV_LAYER4 %ymm0,  inv_z_4_0
    INV_LAYER4 %ymm1,  inv_z_4_1
    INV_LAYER4 %ymm2,  inv_z_4_2
    INV_LAYER4 %ymm3,  inv_z_4_3
    INV_LAYER4 %ymm4,  inv_z_4_4
    INV_LAYER4 %ymm5,  inv_z_4_5
    INV_LAYER4 %ymm6,  inv_z_4_6
    INV_LAYER4 %ymm7,  inv_z_4_7
    INV_LAYER4 %ymm8,  inv_z_4_8
    INV_LAYER4 %ymm9,  inv_z_4_9
    INV_LAYER4 %ymm10, inv_z_4_10
    INV_LAYER4 %ymm11, inv_z_4_11
    INV_LAYER4 %ymm12, inv_z_4_12
    INV_LAYER4 %ymm13, inv_z_4_13
    INV_LAYER4 %ymm14, inv_z_4_14
    INV_LAYER4 %ymm15, inv_z_4_15

    /* ── Layer 8 ── */
    INV_LAYER8 %ymm0,  inv_z_8_0
    INV_LAYER8 %ymm1,  inv_z_8_1
    INV_LAYER8 %ymm2,  inv_z_8_2
    INV_LAYER8 %ymm3,  inv_z_8_3
    INV_LAYER8 %ymm4,  inv_z_8_4
    INV_LAYER8 %ymm5,  inv_z_8_5
    INV_LAYER8 %ymm6,  inv_z_8_6
    INV_LAYER8 %ymm7,  inv_z_8_7
    INV_LAYER8 %ymm8,  inv_z_8_8
    INV_LAYER8 %ymm9,  inv_z_8_9
    INV_LAYER8 %ymm10, inv_z_8_10
    INV_LAYER8 %ymm11, inv_z_8_11
    INV_LAYER8 %ymm12, inv_z_8_12
    INV_LAYER8 %ymm13, inv_z_8_13
    INV_LAYER8 %ymm14, inv_z_8_14
    INV_LAYER8 %ymm15, inv_z_8_15

    /* ── Layer 16 ── */
    vmovdqa32 inv_z_16_0(%rip), %ymm20
    GS_BF %ymm0,  %ymm1,  %ymm20
    vmovdqa32 inv_z_16_1(%rip), %ymm20
    GS_BF %ymm2,  %ymm3,  %ymm20
    vmovdqa32 inv_z_16_2(%rip), %ymm20
    GS_BF %ymm4,  %ymm5,  %ymm20
    vmovdqa32 inv_z_16_3(%rip), %ymm20
    GS_BF %ymm6,  %ymm7,  %ymm20
    vmovdqa32 inv_z_16_4(%rip), %ymm20
    GS_BF %ymm8,  %ymm9,  %ymm20
    vmovdqa32 inv_z_16_5(%rip), %ymm20
    GS_BF %ymm10, %ymm11, %ymm20
    vmovdqa32 inv_z_16_6(%rip), %ymm20
    GS_BF %ymm12, %ymm13, %ymm20
    vmovdqa32 inv_z_16_7(%rip), %ymm20
    GS_BF %ymm14, %ymm15, %ymm20

    /* ── Layer 32 ── */
    vmovdqa32 inv_z_32_0(%rip), %ymm20
    GS_BF %ymm0,  %ymm2,  %ymm20
    GS_BF %ymm1,  %ymm3,  %ymm20
    vmovdqa32 inv_z_32_1(%rip), %ymm20
    GS_BF %ymm4,  %ymm6,  %ymm20
    GS_BF %ymm5,  %ymm7,  %ymm20
    vmovdqa32 inv_z_32_2(%rip), %ymm20
    GS_BF %ymm8,  %ymm10, %ymm20
    GS_BF %ymm9,  %ymm11, %ymm20
    vmovdqa32 inv_z_32_3(%rip), %ymm20
    GS_BF %ymm12, %ymm14, %ymm20
    GS_BF %ymm13, %ymm15, %ymm20

    /* ── Layer 64 ── */
    vmovdqa32 inv_z_64_0(%rip), %ymm20
    GS_BF4 %ymm0, %ymm4, %ymm1, %ymm5, %ymm2, %ymm6, %ymm3, %ymm7, %ymm20
    vmovdqa32 inv_z_64_1(%rip), %ymm20
    GS_BF4 %ymm8, %ymm12, %ymm9, %ymm13, %ymm10, %ymm14, %ymm11, %ymm15, %ymm20

    /* ── Layer 128 ── */
    vmovdqa32 inv_z_128_0(%rip), %ymm20
    GS_BF4 %ymm0, %ymm8,  %ymm1, %ymm9,  %ymm2, %ymm10, %ymm3, %ymm11, %ymm20
    GS_BF4 %ymm4, %ymm12, %ymm5, %ymm13, %ymm6, %ymm14, %ymm7, %ymm15, %ymm20

    /* ── Scale by f = 1441 ── */
    vmovdqa32 ntt_f_scale(%rip), %ymm20
    FQMUL %ymm0,  %ymm20, %ymm0
    FQMUL %ymm1,  %ymm20, %ymm1
    FQMUL %ymm2,  %ymm20, %ymm2
    FQMUL %ymm3,  %ymm20, %ymm3
    FQMUL %ymm4,  %ymm20, %ymm4
    FQMUL %ymm5,  %ymm20, %ymm5
    FQMUL %ymm6,  %ymm20, %ymm6
    FQMUL %ymm7,  %ymm20, %ymm7
    FQMUL %ymm8,  %ymm20, %ymm8
    FQMUL %ymm9,  %ymm20, %ymm9
    FQMUL %ymm10, %ymm20, %ymm10
    FQMUL %ymm11, %ymm20, %ymm11
    FQMUL %ymm12, %ymm20, %ymm12
    FQMUL %ymm13, %ymm20, %ymm13
    FQMUL %ymm14, %ymm20, %ymm14
    FQMUL %ymm15, %ymm20, %ymm15

    /* ── Store results ── */
    vmovdqu %ymm0,      (%rdi)
    vmovdqu %ymm1,   32(%rdi)
    vmovdqu %ymm2,   64(%rdi)
    vmovdqu %ymm3,   96(%rdi)
    vmovdqu %ymm4,  128(%rdi)
    vmovdqu %ymm5,  160(%rdi)
    vmovdqu %ymm6,  192(%rdi)
    vmovdqu %ymm7,  224(%rdi)
    vmovdqu %ymm8,  256(%rdi)
    vmovdqu %ymm9,  288(%rdi)
    vmovdqu %ymm10, 320(%rdi)
    vmovdqu %ymm11, 352(%rdi)
    vmovdqu %ymm12, 384(%rdi)
    vmovdqu %ymm13, 416(%rdi)
    vmovdqu %ymm14, 448(%rdi)
    vmovdqu %ymm15, 480(%rdi)

    vzeroupper
    ret
.size CDECL(dap_mlkem_ntt_inverse_asm), .-CDECL(dap_mlkem_ntt_inverse_asm)


/* ═══════════════════════════════════════════════════════════════════
 * BASEMUL — single polynomial basemul_montgomery
 * void dap_mlkem_basemul_asm(int16_t *r, const int16_t *a,
 *                            const int16_t *b, const int16_t *zetas);
 *   rdi=r, rsi=a, rdx=b, rcx=zetas (ignored, embedded table used)
 *
 * Processes 2 chunks (32 elements) per iteration, 8 iterations = 256.
 * ═══════════════════════════════════════════════════════════════════ */

.align 64
.globl CDECL(dap_mlkem_basemul_asm)
.type  CDECL(dap_mlkem_basemul_asm), @function
CDECL(dap_mlkem_basemul_asm):
    vmovdqa32 ntt_q(%rip), %ymm31
    vmovdqa32 ntt_qinv(%rip), %ymm30
    vmovdqa32 basemul_swap_mask(%rip), %ymm29

    lea basemul_zetas(%rip), %rax
    xor %r10d, %r10d

.Lbm_loop:
    vmovdqu (%rsi, %r10), %ymm0
    vmovdqu 32(%rsi, %r10), %ymm1
    vmovdqu (%rdx, %r10), %ymm2
    vmovdqu 32(%rdx, %r10), %ymm3
    vmovdqa32 (%rax, %r10), %ymm24
    vmovdqa32 32(%rax, %r10), %ymm25

    vpshufb %ymm29, %ymm2, %ymm4
    vpshufb %ymm29, %ymm3, %ymm5

    /* diag = fqmul(va, vb) — 2 chunks interleaved */
    vpmullw %ymm0, %ymm2, %ymm6
    vpmullw %ymm1, %ymm3, %ymm7
    vpmulhw %ymm0, %ymm2, %ymm8
    vpmulhw %ymm1, %ymm3, %ymm9
    vpmullw %ymm30, %ymm6, %ymm6
    vpmullw %ymm30, %ymm7, %ymm7
    vpmulhw %ymm31, %ymm6, %ymm6
    vpmulhw %ymm31, %ymm7, %ymm7
    vpsubw  %ymm6, %ymm8, %ymm6
    vpsubw  %ymm7, %ymm9, %ymm7

    /* cross = fqmul(va, vbs) — 2 chunks interleaved */
    vpmullw %ymm0, %ymm4, %ymm8
    vpmullw %ymm1, %ymm5, %ymm9
    vpmulhw %ymm0, %ymm4, %ymm10
    vpmulhw %ymm1, %ymm5, %ymm11
    vpmullw %ymm30, %ymm8, %ymm8
    vpmullw %ymm30, %ymm9, %ymm9
    vpmulhw %ymm31, %ymm8, %ymm8
    vpmulhw %ymm31, %ymm9, %ymm9
    vpsubw  %ymm8, %ymm10, %ymm8
    vpsubw  %ymm9, %ymm11, %ymm9

    /* diagz = fqmul(diag, vz) — 2 chunks interleaved */
    vpmullw %ymm6, %ymm24, %ymm10
    vpmullw %ymm7, %ymm25, %ymm11
    vpmulhw %ymm6, %ymm24, %ymm12
    vpmulhw %ymm7, %ymm25, %ymm13
    vpmullw %ymm30, %ymm10, %ymm10
    vpmullw %ymm30, %ymm11, %ymm11
    vpmulhw %ymm31, %ymm10, %ymm10
    vpmulhw %ymm31, %ymm11, %ymm11
    vpsubw  %ymm10, %ymm12, %ymm10
    vpsubw  %ymm11, %ymm13, %ymm11

    /* Combine: r_diag = diag + swap(diagz) */
    vpshufb %ymm29, %ymm10, %ymm12
    vpshufb %ymm29, %ymm11, %ymm13
    vpaddw  %ymm12, %ymm6, %ymm6
    vpaddw  %ymm13, %ymm7, %ymm7

    /* r_cross = cross + swap(cross) */
    vpshufb %ymm29, %ymm8, %ymm12
    vpshufb %ymm29, %ymm9, %ymm13
    vpaddw  %ymm12, %ymm8, %ymm8
    vpaddw  %ymm13, %ymm9, %ymm9

    /* result = blend_odd(r_diag, r_cross) */
    vpblendw $0xAA, %ymm8, %ymm6, %ymm6
    vpblendw $0xAA, %ymm9, %ymm7, %ymm7

    vmovdqu %ymm6, (%rdi, %r10)
    vmovdqu %ymm7, 32(%rdi, %r10)

    add $64, %r10d
    cmp $512, %r10d
    jl .Lbm_loop

    vzeroupper
    ret
.size CDECL(dap_mlkem_basemul_asm), .-CDECL(dap_mlkem_basemul_asm)


/* ═══════════════════════════════════════════════════════════════════
 * BASEMUL_ACC — fused K × basemul + accumulate + Barrett reduce
 * void dap_mlkem_basemul_acc_asm(int16_t *r,
 *     const int16_t * const *pa, const int16_t * const *pb,
 *     unsigned count);
 *   rdi=r, rsi=pa, rdx=pb, ecx=count
 *
 * Hot path for polyvec_pointwise_acc_montgomery.
 * Outer loop: 8 iterations × 2 chunks = 16 × 16 = 256 elements.
 * Inner loop: K basemul pairs accumulated, then Barrett reduced.
 * ═══════════════════════════════════════════════════════════════════ */

.align 64
.globl CDECL(dap_mlkem_basemul_acc_asm)
.type  CDECL(dap_mlkem_basemul_acc_asm), @function
CDECL(dap_mlkem_basemul_acc_asm):
    vmovdqa32 ntt_q(%rip), %ymm31
    vmovdqa32 ntt_qinv(%rip), %ymm30
    vmovdqa32 basemul_swap_mask(%rip), %ymm29
    vmovdqa32 ntt_barrett_v(%rip), %ymm28

    lea basemul_zetas(%rip), %rax
    xor %r10d, %r10d

.Lacc_outer:
    /* Load zeta vectors for 2 chunks (persistent across K) */
    vmovdqa32 (%rax, %r10), %ymm24
    vmovdqa32 32(%rax, %r10), %ymm25

    vpxord %ymm26, %ymm26, %ymm26
    vpxord %ymm27, %ymm27, %ymm27

    xor %r11d, %r11d

.Lacc_k:
    mov (%rsi, %r11, 8), %r8
    mov (%rdx, %r11, 8), %r9

    vmovdqu (%r8, %r10), %ymm0
    vmovdqu 32(%r8, %r10), %ymm1
    vmovdqu (%r9, %r10), %ymm2
    vmovdqu 32(%r9, %r10), %ymm3

    vpshufb %ymm29, %ymm2, %ymm4
    vpshufb %ymm29, %ymm3, %ymm5

    /* diag = fqmul(va, vb) — 2 chunks interleaved */
    vpmullw %ymm0, %ymm2, %ymm6
    vpmullw %ymm1, %ymm3, %ymm7
    vpmulhw %ymm0, %ymm2, %ymm8
    vpmulhw %ymm1, %ymm3, %ymm9
    vpmullw %ymm30, %ymm6, %ymm6
    vpmullw %ymm30, %ymm7, %ymm7
    vpmulhw %ymm31, %ymm6, %ymm6
    vpmulhw %ymm31, %ymm7, %ymm7
    vpsubw  %ymm6, %ymm8, %ymm6
    vpsubw  %ymm7, %ymm9, %ymm7

    /* cross = fqmul(va, vbs) — 2 chunks interleaved */
    vpmullw %ymm0, %ymm4, %ymm8
    vpmullw %ymm1, %ymm5, %ymm9
    vpmulhw %ymm0, %ymm4, %ymm10
    vpmulhw %ymm1, %ymm5, %ymm11
    vpmullw %ymm30, %ymm8, %ymm8
    vpmullw %ymm30, %ymm9, %ymm9
    vpmulhw %ymm31, %ymm8, %ymm8
    vpmulhw %ymm31, %ymm9, %ymm9
    vpsubw  %ymm8, %ymm10, %ymm8
    vpsubw  %ymm9, %ymm11, %ymm9

    /* diagz = fqmul(diag, vz) — 2 chunks interleaved */
    vpmullw %ymm6, %ymm24, %ymm10
    vpmullw %ymm7, %ymm25, %ymm11
    vpmulhw %ymm6, %ymm24, %ymm12
    vpmulhw %ymm7, %ymm25, %ymm13
    vpmullw %ymm30, %ymm10, %ymm10
    vpmullw %ymm30, %ymm11, %ymm11
    vpmulhw %ymm31, %ymm10, %ymm10
    vpmulhw %ymm31, %ymm11, %ymm11
    vpsubw  %ymm10, %ymm12, %ymm10
    vpsubw  %ymm11, %ymm13, %ymm11

    /* Combine: r_diag = diag + swap(diagz) */
    vpshufb %ymm29, %ymm10, %ymm12
    vpshufb %ymm29, %ymm11, %ymm13
    vpaddw  %ymm12, %ymm6, %ymm6
    vpaddw  %ymm13, %ymm7, %ymm7

    /* r_cross = cross + swap(cross) */
    vpshufb %ymm29, %ymm8, %ymm12
    vpshufb %ymm29, %ymm9, %ymm13
    vpaddw  %ymm12, %ymm8, %ymm8
    vpaddw  %ymm13, %ymm9, %ymm9

    /* result = blend_odd(r_diag, r_cross) */
    vpblendw $0xAA, %ymm8, %ymm6, %ymm6
    vpblendw $0xAA, %ymm9, %ymm7, %ymm7

    /* Accumulate */
    vpaddw  %ymm6, %ymm26, %ymm26
    vpaddw  %ymm7, %ymm27, %ymm27

    inc %r11d
    cmp %ecx, %r11d
    jl .Lacc_k

    /* Barrett reduce accumulators */
    vpmulhw %ymm28, %ymm26, %ymm16
    vpmulhw %ymm28, %ymm27, %ymm17
    vpsraw  $10, %ymm16, %ymm16
    vpsraw  $10, %ymm17, %ymm17
    vpmullw %ymm31, %ymm16, %ymm16
    vpmullw %ymm31, %ymm17, %ymm17
    vpsubw  %ymm16, %ymm26, %ymm26
    vpsubw  %ymm17, %ymm27, %ymm27

    vmovdqu32 %ymm26, (%rdi, %r10)
    vmovdqu32 %ymm27, 32(%rdi, %r10)

    add $64, %r10d
    cmp $512, %r10d
    jl .Lacc_outer

    vzeroupper
    ret
.size CDECL(dap_mlkem_basemul_acc_asm), .-CDECL(dap_mlkem_basemul_acc_asm)


#if defined(__ELF__)
.section .note.GNU-stack, "", @progbits
#endif
