{{! ================================================================ }}
{{! ML-KEM NTT Assembly — Shared Data Framework                     }}
{{! ================================================================ }}
{{! This file defines all ML-KEM NTT constants: Q, QINV, Barrett,  }}
{{! zeta tables (forward, inverse, basemul), nttpack/unpack indices. }}
{{! Architecture-specific code is in ARCH_IMPL (passed via CMake).  }}
{{!                                                                 }}
{{! Adding a new arch: create arch/<family>/mlkem_ntt_asm_<isa>.tpl }}
{{! that uses the data labels below (ntt_q, ntt_qinv, fwd_z7, etc) }}
{{! ================================================================ }}

#ifdef __APPLE__
#define CDECL(x) _##x
#else
#define CDECL(x) x
#endif

/* ═══════════════════════════════════════════════════════════════════
 * READ-ONLY DATA — ML-KEM (Kyber) NTT constants
 * Q = 3329, QINV = -3327, Barrett V = 20159, f_scale = 1441
 * All tables are architecture-independent.
 * ═══════════════════════════════════════════════════════════════════ */
.section .rodata
.align 32

.macro BCAST16 name, val
\name: .short \val, \val, \val, \val, \val, \val, \val, \val
       .short \val, \val, \val, \val, \val, \val, \val, \val
.endm

.macro HALF16 name, lo, hi
\name: .short \lo, \lo, \lo, \lo, \lo, \lo, \lo, \lo
       .short \hi, \hi, \hi, \hi, \hi, \hi, \hi, \hi
.endm

.macro QUAD16 name, q0, q1, q2, q3
\name: .short \q0, \q0, \q0, \q0, \q1, \q1, \q1, \q1
       .short \q2, \q2, \q2, \q2, \q3, \q3, \q3, \q3
.endm

.macro PAIR16 name, z0,z1,z2,z3,z4,z5,z6,z7
\name: .short \z0, \z0, \z1, \z1, \z2, \z2, \z3, \z3
       .short \z4, \z4, \z5, \z5, \z6, \z6, \z7, \z7
.endm

BCAST16 ntt_q, 3329
BCAST16 ntt_qinv, -3327
BCAST16 ntt_barrett_v, 20159
BCAST16 ntt_f_scale, 1441

/* Outer layer zetas (layers 7-4) */
BCAST16 fwd_z7, 2571
BCAST16 fwd_z6_0, 2970
BCAST16 fwd_z6_1, 1812
BCAST16 fwd_z5_0, 1493
BCAST16 fwd_z5_1, 1422
BCAST16 fwd_z5_2, 287
BCAST16 fwd_z5_3, 202
BCAST16 fwd_z4_0, 3158
BCAST16 fwd_z4_1, 622
BCAST16 fwd_z4_2, 1577
BCAST16 fwd_z4_3, 182
BCAST16 fwd_z4_4, 962
BCAST16 fwd_z4_5, 2127
BCAST16 fwd_z4_6, 1855
BCAST16 fwd_z4_7, 1468

/* Inverse outer layer zetas (layers 4-7) */
BCAST16 inv_z4_0, 1861
BCAST16 inv_z4_1, 1474
BCAST16 inv_z4_2, 1202
BCAST16 inv_z4_3, 2367
BCAST16 inv_z4_4, 3147
BCAST16 inv_z4_5, 1752
BCAST16 inv_z4_6, 2707
BCAST16 inv_z4_7, 171
BCAST16 inv_z5_0, 3127
BCAST16 inv_z5_1, 3042
BCAST16 inv_z5_2, 1907
BCAST16 inv_z5_3, 1836
BCAST16 inv_z6_0, 1517
BCAST16 inv_z6_1, 359
BCAST16 inv_z7, 758

/*
 * Inner layer zeta table for forward NTT.
 * 8 pairs × 3 vectors (layer3_half, layer2_quad, layer1_pair).
 * Stride = 96 bytes per pair.
 */
.align 32
fwd_inner_zetas:
/* pair 0: s_zetas[16..17], [32..35], [64..71] */
HALF16 .Lfwd_p0_l3,  573, 2004
QUAD16 .Lfwd_p0_l2,  1223, 652, 2777, 1015
PAIR16 .Lfwd_p0_l1,  2226, 430, 555, 843, 2078, 871, 1550, 105
/* pair 1 */
HALF16 .Lfwd_p1_l3,  264, 383
QUAD16 .Lfwd_p1_l2,  2036, 1491, 3047, 1785
PAIR16 .Lfwd_p1_l1,  422, 587, 177, 3094, 3038, 2869, 1574, 1653
/* pair 2 */
HALF16 .Lfwd_p2_l3,  2500, 1458
QUAD16 .Lfwd_p2_l2,  516, 3321, 3009, 2663
PAIR16 .Lfwd_p2_l1,  3083, 778, 1159, 3182, 2552, 1483, 2727, 1119
/* pair 3 */
HALF16 .Lfwd_p3_l3,  1727, 3199
QUAD16 .Lfwd_p3_l2,  1711, 2167, 126, 1469
PAIR16 .Lfwd_p3_l1,  1739, 644, 2457, 349, 418, 329, 3173, 3254
/* pair 4 */
HALF16 .Lfwd_p4_l3,  2648, 1017
QUAD16 .Lfwd_p4_l2,  2476, 3239, 3058, 830
PAIR16 .Lfwd_p4_l1,  817, 1097, 603, 610, 1322, 2044, 1864, 384
/* pair 5 */
HALF16 .Lfwd_p5_l3,  732, 608
QUAD16 .Lfwd_p5_l2,  107, 1908, 3082, 2378
PAIR16 .Lfwd_p5_l1,  2114, 3193, 1218, 1994, 2455, 220, 2142, 1670
/* pair 6 */
HALF16 .Lfwd_p6_l3,  1787, 411
QUAD16 .Lfwd_p6_l2,  2931, 961, 1821, 2604
PAIR16 .Lfwd_p6_l1,  2144, 1799, 2051, 794, 1819, 2475, 2459, 478
/* pair 7 */
HALF16 .Lfwd_p7_l3,  3124, 1758
QUAD16 .Lfwd_p7_l2,  448, 2264, 677, 2054
PAIR16 .Lfwd_p7_l1,  3221, 3021, 996, 991, 958, 1869, 1522, 1628

/*
 * Inner layer zeta table for inverse NTT.
 * 8 pairs × 3 vectors (layer1_pair, layer2_quad, layer3_half).
 * Stride = 96 bytes per pair.
 */
.align 32
inv_inner_zetas:
/* pair 0: s_zetas_inv[0..7], [64..67], [96..97] */
PAIR16 .Linv_p0_l1,  1701, 1807, 1460, 2371, 2338, 2333, 308, 108
QUAD16 .Linv_p0_l2,  1275, 2652, 1065, 2881
HALF16 .Linv_p0_l3,  1571, 205
/* pair 1 */
PAIR16 .Linv_p1_l1,  2851, 870, 854, 1510, 2535, 1278, 1530, 1185
QUAD16 .Linv_p1_l2,  725, 1508, 2368, 398
HALF16 .Linv_p1_l3,  2918, 1542
/* pair 2 */
PAIR16 .Linv_p2_l1,  1659, 1187, 3109, 874, 1335, 2111, 136, 1215
QUAD16 .Linv_p2_l2,  951, 247, 1421, 3222
HALF16 .Linv_p2_l3,  2721, 2597
/* pair 3 */
PAIR16 .Linv_p3_l1,  2945, 1465, 1285, 2007, 2719, 2726, 2232, 2512
QUAD16 .Linv_p3_l2,  2499, 271, 90, 853
HALF16 .Linv_p3_l3,  2312, 681
/* pair 4 */
PAIR16 .Linv_p4_l1,  75, 156, 3000, 2911, 2980, 872, 2685, 1590
QUAD16 .Linv_p4_l2,  1860, 3203, 1162, 1618
HALF16 .Linv_p4_l3,  130, 1602
/* pair 5 */
PAIR16 .Linv_p5_l1,  2210, 602, 1846, 777, 147, 2170, 2551, 246
QUAD16 .Linv_p5_l2,  666, 320, 8, 2813
HALF16 .Linv_p5_l3,  1871, 829
/* pair 6 */
PAIR16 .Linv_p6_l1,  1676, 1755, 460, 291, 235, 3152, 2742, 2907
QUAD16 .Linv_p6_l2,  1544, 282, 1838, 1293
HALF16 .Linv_p6_l3,  2946, 3065
/* pair 7 */
PAIR16 .Linv_p7_l1,  3224, 1779, 2458, 1251, 2486, 2774, 2899, 1103
QUAD16 .Linv_p7_l2,  2314, 552, 2677, 2106
HALF16 .Linv_p7_l3,  1325, 2756

/* vpermt2w index vectors for nttpack/nttunpack */
.align 32
nttpack_even_idx:
    .short 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30
nttpack_odd_idx:
    .short 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31
nttunpack_lo_idx:
    .short 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23
nttunpack_hi_idx:
    .short 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31

/* Basemul: adjacent int16 swap mask for vpshufb */
.align 32
basemul_swap_mask:
    .byte 2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13
    .byte 2,3,0,1, 6,7,4,5, 10,11,8,9, 14,15,12,13

/* Basemul: zetas[64..127] expanded to [+z, +z, -z, -z] per dword */
.macro BASEMUL_ZETA4 z0, z1, z2, z3
    .short \z0, \z0, -\z0, -\z0, \z1, \z1, -\z1, -\z1
    .short \z2, \z2, -\z2, -\z2, \z3, \z3, -\z3, -\z3
.endm

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


{{! === Include architecture-specific implementation === }}
{{#include ARCH_IMPL}}


#if defined(__ELF__)
.section .note.GNU-stack, "", @progbits
#endif
