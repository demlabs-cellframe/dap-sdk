/*
 * Dilithium NTT — AVX2 register-resident fused butterfly (CRYSTALS-style).
 * All 8 layers fused into 2 phases: levels0t1 (stride 128/64) and
 * levels2t7 (stride 32..1) with shuffle-based in-register rearrangement.
 *
 * Output is in CRYSTALS shuffled element order — paired with the fused
 * invNTT which expects this format.  Pointwise multiply is order-agnostic.
 *
 * void dap_dilithium_ntt_fwd_fused_{{ARCH_LOWER}}(int32_t coeffs[256]);
 * void dap_dilithium_nttunpack_{{ARCH_LOWER}}(int32_t coeffs[256]);
 *
 * @generated from dap_dilithium_ntt_fused_avx2.S.tpl
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define DIL_Q 8380417

/* ---- Signed Montgomery butterfly (CRYSTALS convention) ----
 *
 * Register convention (matching CRYSTALS exactly):
 *   ymm0 = Q (broadcast, persistent)
 *   zl0/zl1 = zetas_qinv register(s)   (#zeta group — computes zqinv*h)
 *   zh0/zh1 = zetas register(s)         (#qinv*zeta group — computes zeta*h)
 *
 * Formula: mont(zeta,h) = hi32(zeta*h) - hi32(Q * lo32(zqinv*h))
 */
.macro butterfly l, h, zl0=1, zl1=1, zh0=2, zh1=2
    vpmuldq     %ymm\zl0, %ymm\h, %ymm13
    vmovshdup   %ymm\h, %ymm12
    vpmuldq     %ymm\zl1, %ymm12, %ymm14
    vpmuldq     %ymm\zh0, %ymm\h, %ymm\h
    vpmuldq     %ymm\zh1, %ymm12, %ymm12
    vpmuldq     %ymm0, %ymm13, %ymm13
    vpmuldq     %ymm0, %ymm14, %ymm14
    vmovshdup   %ymm\h, %ymm\h
    vpblendd    $0xAA, %ymm12, %ymm\h, %ymm\h
    vpsubd      %ymm\h, %ymm\l, %ymm12
    vpaddd      %ymm\h, %ymm\l, %ymm\l
    vmovshdup   %ymm13, %ymm13
    vpblendd    $0xAA, %ymm14, %ymm13, %ymm13
    vpaddd      %ymm13, %ymm12, %ymm\h
    vpsubd      %ymm13, %ymm\l, %ymm\l
.endm

/* ---- Shuffle macros for cross-layer data rearrangement ---- */
.macro shuffle8 a, b, t0, t1
    vperm2i128  $0x20, %ymm\b, %ymm\a, %ymm\t0
    vperm2i128  $0x31, %ymm\b, %ymm\a, %ymm\t1
.endm

.macro shuffle4 a, b, t0, t1
    vpunpcklqdq %ymm\b, %ymm\a, %ymm\t0
    vpunpckhqdq %ymm\b, %ymm\a, %ymm\t1
.endm

.macro shuffle2 a, b, t0, t1
    vmovsldup   %ymm\b, %ymm\t0
    vpblendd    $0xAA, %ymm\t0, %ymm\a, %ymm\t0
    vpsrlq      $32, %ymm\a, %ymm\a
    vpblendd    $0xAA, %ymm\b, %ymm\a, %ymm\t1
.endm

/* ---- Phase 1: levels 0-1 (stride 128 and 64) ----
 * ymm0 = Q, ymm1 = zqinv (broadcast), ymm2 = zeta (broadcast)
 */
.macro levels0t1 off
    vmovdqu     0+32*\off(%rdi), %ymm4
    vmovdqu     128+32*\off(%rdi), %ymm5
    vmovdqu     256+32*\off(%rdi), %ymm6
    vmovdqu     384+32*\off(%rdi), %ymm7
    vmovdqu     512+32*\off(%rdi), %ymm8
    vmovdqu     640+32*\off(%rdi), %ymm9
    vmovdqu     768+32*\off(%rdi), %ymm10
    vmovdqu     896+32*\off(%rdi), %ymm11

    /* Level 0: stride 128, zeta[1] */
    vpbroadcastd s_fwd_zqinv+1*4(%rip), %ymm1
    vpbroadcastd s_fwd_zeta+1*4(%rip), %ymm2
    butterfly 4, 8
    butterfly 5, 9
    butterfly 6, 10
    butterfly 7, 11

    /* Level 1: stride 64 */
    vpbroadcastd s_fwd_zqinv+2*4(%rip), %ymm1
    vpbroadcastd s_fwd_zeta+2*4(%rip), %ymm2
    butterfly 4, 6
    butterfly 5, 7

    vpbroadcastd s_fwd_zqinv+3*4(%rip), %ymm1
    vpbroadcastd s_fwd_zeta+3*4(%rip), %ymm2
    butterfly 8, 10
    butterfly 9, 11

    vmovdqu     %ymm4,  0+32*\off(%rdi)
    vmovdqu     %ymm5,  128+32*\off(%rdi)
    vmovdqu     %ymm6,  256+32*\off(%rdi)
    vmovdqu     %ymm7,  384+32*\off(%rdi)
    vmovdqu     %ymm8,  512+32*\off(%rdi)
    vmovdqu     %ymm9,  640+32*\off(%rdi)
    vmovdqu     %ymm10, 768+32*\off(%rdi)
    vmovdqu     %ymm11, 896+32*\off(%rdi)
.endm

/* ---- Phase 2: levels 2-7 with de-shuffle (one quarter = 64 coeffs) ----
 * ymm0 = Q, ymm1 = zqinv (vector), ymm2 = zeta (vector)
 * For levels 5-7: ymm10 = zqinv_odd, ymm15 = zeta_odd
 */
.macro levels2t7 off
    vmovdqu     256*\off+  0(%rdi), %ymm4
    vmovdqu     256*\off+ 32(%rdi), %ymm5
    vmovdqu     256*\off+ 64(%rdi), %ymm6
    vmovdqu     256*\off+ 96(%rdi), %ymm7
    vmovdqu     256*\off+128(%rdi), %ymm8
    vmovdqu     256*\off+160(%rdi), %ymm9
    vmovdqu     256*\off+192(%rdi), %ymm10
    vmovdqu     256*\off+224(%rdi), %ymm11

    /* Level 2: broadcast zeta for this quarter */
    vpbroadcastd s_fwd_zqinv+(4+\off)*4(%rip), %ymm1
    vpbroadcastd s_fwd_zeta+(4+\off)*4(%rip), %ymm2
    butterfly 4, 8
    butterfly 5, 9
    butterfly 6, 10
    butterfly 7, 11

    shuffle8 4, 8, 3, 8
    shuffle8 5, 9, 4, 9
    shuffle8 6, 10, 5, 10
    shuffle8 7, 11, 6, 11

    /* Level 3 */
    vmovdqa     s_fwd_zqinv+(8+8*\off)*4(%rip), %ymm1
    vmovdqa     s_fwd_zeta+(8+8*\off)*4(%rip), %ymm2
    butterfly 3, 5
    butterfly 8, 10
    butterfly 4, 6
    butterfly 9, 11

    shuffle4 3, 5, 7, 5
    shuffle4 8, 10, 3, 10
    shuffle4 4, 6, 8, 6
    shuffle4 9, 11, 4, 11

    /* Level 4 */
    vmovdqa     s_fwd_zqinv+(40+8*\off)*4(%rip), %ymm1
    vmovdqa     s_fwd_zeta+(40+8*\off)*4(%rip), %ymm2
    butterfly 7, 8
    butterfly 5, 6
    butterfly 3, 4
    butterfly 10, 11

    shuffle2 7, 8, 9, 8
    shuffle2 5, 6, 7, 6
    shuffle2 3, 4, 5, 4
    shuffle2 10, 11, 3, 11

    /* Level 5: per-element zetas (even/odd split) */
    vmovdqa     s_fwd_zqinv+(72+8*\off)*4(%rip), %ymm1
    vmovdqa     s_fwd_zeta+(72+8*\off)*4(%rip), %ymm2
    vpsrlq      $32, %ymm1, %ymm10
    vmovshdup   %ymm2, %ymm15
    butterfly 9, 5, 1, 10, 2, 15
    butterfly 8, 4, 1, 10, 2, 15
    butterfly 7, 3, 1, 10, 2, 15
    butterfly 6, 11, 1, 10, 2, 15

    /* Level 6: 2 groups */
    vmovdqa     s_fwd_zqinv+(104+8*\off)*4(%rip), %ymm1
    vmovdqa     s_fwd_zeta+(104+8*\off)*4(%rip), %ymm2
    vpsrlq      $32, %ymm1, %ymm10
    vmovshdup   %ymm2, %ymm15
    butterfly 9, 7, 1, 10, 2, 15
    butterfly 8, 6, 1, 10, 2, 15

    vmovdqa     s_fwd_zqinv+(136+8*\off)*4(%rip), %ymm1
    vmovdqa     s_fwd_zeta+(136+8*\off)*4(%rip), %ymm2
    vpsrlq      $32, %ymm1, %ymm10
    vmovshdup   %ymm2, %ymm15
    butterfly 5, 3, 1, 10, 2, 15
    butterfly 4, 11, 1, 10, 2, 15

    /* Level 7: 4 groups */
    vmovdqa     s_fwd_zqinv+(168+8*\off)*4(%rip), %ymm1
    vmovdqa     s_fwd_zeta+(168+8*\off)*4(%rip), %ymm2
    vpsrlq      $32, %ymm1, %ymm10
    vmovshdup   %ymm2, %ymm15
    butterfly 9, 8, 1, 10, 2, 15

    vmovdqa     s_fwd_zqinv+(200+8*\off)*4(%rip), %ymm1
    vmovdqa     s_fwd_zeta+(200+8*\off)*4(%rip), %ymm2
    vpsrlq      $32, %ymm1, %ymm10
    vmovshdup   %ymm2, %ymm15
    butterfly 7, 6, 1, 10, 2, 15

    vmovdqa     s_fwd_zqinv+(232+8*\off)*4(%rip), %ymm1
    vmovdqa     s_fwd_zeta+(232+8*\off)*4(%rip), %ymm2
    vpsrlq      $32, %ymm1, %ymm10
    vmovshdup   %ymm2, %ymm15
    butterfly 5, 4, 1, 10, 2, 15

    vmovdqa     s_fwd_zqinv+(264+8*\off)*4(%rip), %ymm1
    vmovdqa     s_fwd_zeta+(264+8*\off)*4(%rip), %ymm2
    vpsrlq      $32, %ymm1, %ymm10
    vmovshdup   %ymm2, %ymm15
    butterfly 3, 11, 1, 10, 2, 15

    /* Store in CRYSTALS shuffled order (paired with fused invNTT) */
    vmovdqu     %ymm9,  256*\off+  0(%rdi)
    vmovdqu     %ymm8,  256*\off+ 32(%rdi)
    vmovdqu     %ymm7,  256*\off+ 64(%rdi)
    vmovdqu     %ymm6,  256*\off+ 96(%rdi)
    vmovdqu     %ymm5,  256*\off+128(%rdi)
    vmovdqu     %ymm4,  256*\off+160(%rdi)
    vmovdqu     %ymm3,  256*\off+192(%rdi)
    vmovdqu     %ymm11, 256*\off+224(%rdi)
.endm


{{#include ASM_MACROS}}

.text
.globl dap_dilithium_ntt_fwd_fused_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_ntt_fwd_fused_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_ntt_fwd_fused_{{ARCH_LOWER}}:
    vpbroadcastd s_fwd_q(%rip), %ymm0

    levels0t1 0
    levels0t1 1
    levels0t1 2
    levels0t1 3

    levels2t7 0
    levels2t7 1
    levels2t7 2
    levels2t7 3

    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_ntt_fwd_fused_{{ARCH_LOWER}})

/* ---- nttunpack: convert standard order → CRYSTALS shuffled order ----
 * Applies shuffle8→shuffle4→shuffle2 to each quarter (64 coeffs).
 * Must be called on NTT-domain data generated in standard order
 * (e.g. expand_mat output) before pointwise with shuffled NTT output.
 */
.macro nttunpack128
    vmovdqu       0(%rdi), %ymm4
    vmovdqu      32(%rdi), %ymm5
    vmovdqu      64(%rdi), %ymm6
    vmovdqu      96(%rdi), %ymm7
    vmovdqu     128(%rdi), %ymm8
    vmovdqu     160(%rdi), %ymm9
    vmovdqu     192(%rdi), %ymm10
    vmovdqu     224(%rdi), %ymm11

    shuffle8 4, 8, 3, 8
    shuffle8 5, 9, 4, 9
    shuffle8 6, 10, 5, 10
    shuffle8 7, 11, 6, 11

    shuffle4 3, 5, 7, 5
    shuffle4 8, 10, 3, 10
    shuffle4 4, 6, 8, 6
    shuffle4 9, 11, 4, 11

    shuffle2 7, 8, 9, 8
    shuffle2 5, 6, 7, 6
    shuffle2 3, 4, 5, 4
    shuffle2 10, 11, 3, 11

    vmovdqu     %ymm9,    0(%rdi)
    vmovdqu     %ymm8,   32(%rdi)
    vmovdqu     %ymm7,   64(%rdi)
    vmovdqu     %ymm6,   96(%rdi)
    vmovdqu     %ymm5,  128(%rdi)
    vmovdqu     %ymm4,  160(%rdi)
    vmovdqu     %ymm3,  192(%rdi)
    vmovdqu     %ymm11, 224(%rdi)
.endm

.globl dap_dilithium_nttunpack_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_nttunpack_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_nttunpack_{{ARCH_LOWER}}:
    nttunpack128
    add     $256, %rdi
    nttunpack128
    add     $256, %rdi
    nttunpack128
    add     $256, %rdi
    nttunpack128
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_nttunpack_{{ARCH_LOWER}})

GNU_STACK

/* ================================================================
 * Constants and zeta tables — CRYSTALS layout, signed Montgomery form.
 * Generated by tools/gen_ntt_consts.py from reference Dilithium zetas.
 * ================================================================ */
.section .rodata
.p2align 5

s_fwd_q:
    .long DIL_Q

.p2align 5
s_fwd_zqinv:
    .long   -151046689,   1830765815,  -1929875198,  -1927777021,   1640767044,   1477910808,   1612161320,   1640734244
    .long    308362795,    308362795,    308362795,    308362795,  -1815525077,  -1815525077,  -1815525077,  -1815525077
    .long  -1374673747,  -1374673747,  -1374673747,  -1374673747,  -1091570561,  -1091570561,  -1091570561,  -1091570561
    .long  -1929495947,  -1929495947,  -1929495947,  -1929495947,    515185417,    515185417,    515185417,    515185417
    .long   -285697463,   -285697463,   -285697463,   -285697463,    625853735,    625853735,    625853735,    625853735
    .long   1727305304,   1727305304,   2082316400,   2082316400,  -1364982364,  -1364982364,    858240904,    858240904
    .long   1806278032,   1806278032,    222489248,    222489248,   -346752664,   -346752664,    684667771,    684667771
    .long   1654287830,   1654287830,   -878576921,   -878576921,  -1257667337,  -1257667337,   -748618600,   -748618600
    .long    329347125,    329347125,   1837364258,   1837364258,  -1443016191,  -1443016191,  -1170414139,  -1170414139
    .long  -1846138265,  -1631226336,  -1404529459,   1838055109,   1594295555,  -1076973524,  -1898723372,   -594436433
    .long   -202001019,   -475984260,   -561427818,   1797021249,  -1061813248,   2059733581,  -1661512036,  -1104976547
    .long  -1750224323,   -901666090,    418987550,   1831915353,  -1925356481,    992097815,    879957084,   2024403852
    .long   1484874664,  -1636082790,   -285388938,  -1983539117,  -1495136972,   -950076368,  -1714807468,   -952438995
    .long  -1574918427,   1350681039,  -2143979939,   1599739335,  -1285853323,   -993005454,  -1440787840,    568627424
    .long   -783134478,   -588790216,    289871779,  -1262003603,   2135294594,  -1018755525,   -889861155,   1665705315
    .long   1321868265,   1225434135,  -1784632064,    666258756,    675310538,  -1555941048,  -1999506068,  -1499481951
    .long   -695180180,  -1375177022,   1777179795,    334803717,   -178766299,   -518252220,   1957047970,   1146323031
    .long   -654783359,  -1974159335,   1651689966,    140455867,  -1039411342,   1955560694,   1529189038,  -2131021878
    .long   -247357819,   1518161567,    -86965173,   1708872713,   1787797779,   1638590967,   -120646188,  -1669960606
    .long   -916321552,   1155548552,   2143745726,   1210558298,  -1261461890,   -318346816,    628664287,  -1729304568
    .long   1422575624,   1424130038,  -1185330464,    235321234,    168022240,   1206536194,    985155484,   -894060583
    .long      -898413,  -1363460238,   -605900043,   2027833504,     14253662,   1014493059,    863641633,   1819892093
    .long   2124962073,  -1223601433,  -1920467227,  -1637785316,  -1536588520,    694382729,    235104446,  -1045062172
    .long    831969619,   -300448763,    756955444,   -260312805,   1554794072,   1339088280,  -2040058690,   -853476187
    .long  -2047270596,  -1723816713,  -1591599803,   -440824168,   1119856484,   1544891539,    155290192,   -973777462
    .long    991903578,    912367099,    -44694137,   1176904444,   -421552614,   -818371958,   1747917558,   -325927722
    .long    908452108,   1851023419,  -1176751719,  -1354528380,    -72690498,   -314284737,    985022747,    963438279
    .long  -1078959975,    604552167,  -1021949428,    608791570,    173440395,  -2126092136,  -1316619236,  -1039370342
    .long      6087993,   -110126092,    565464272,  -1758099917,  -1600929361,    879867909,  -1809756372,    400711272
    .long   1363007700,     30313375,   -326425360,   1683520342,   -517299994,   2027935492,  -1372618620,    128353682
    .long  -1123881663,    137583815,   -635454918,   -642772911,     45766801,    671509323,  -2070602178,    419615363
    .long   1216882040,   -270590488,  -1276805128,    371462360,  -1357098057,   -384158533,    827959816,   -596344473
    .long    702390549,   -279505433,   -260424530,    -71875110,  -1208667171,  -1499603926,   2036925262,   -540420426
    .long    746144248,  -1420958686,   2032221021,   1904936414,   1257750362,   1926727420,   1931587462,   1258381762
    .long    885133339,   1629985060,   1967222129,      6363718,  -1287922800,   1136965286,   1779436847,   1116720494
    .long   1042326957,   1405999311,    713994583,    940195359,  -1542497137,   2061661095,   -883155599,   1726753853
    .long  -1547952704,    394851342,    283780712,    776003547,   1123958025,    201262505,   1934038751,    374860238

.p2align 5
s_fwd_zeta:
    .long     -3975713,        25847,     -2608894,      -518909,       237124,      -777960,      -876248,       466468
    .long      1826347,      1826347,      1826347,      1826347,      2353451,      2353451,      2353451,      2353451
    .long      -359251,      -359251,      -359251,      -359251,     -2091905,     -2091905,     -2091905,     -2091905
    .long      3119733,      3119733,      3119733,      3119733,     -2884855,     -2884855,     -2884855,     -2884855
    .long      3111497,      3111497,      3111497,      3111497,      2680103,      2680103,      2680103,      2680103
    .long      2725464,      2725464,      1024112,      1024112,     -1079900,     -1079900,      3585928,      3585928
    .long      -549488,      -549488,     -1119584,     -1119584,      2619752,      2619752,     -2108549,     -2108549
    .long     -2118186,     -2118186,     -3859737,     -3859737,     -1399561,     -1399561,     -3277672,     -3277672
    .long      1757237,      1757237,       -19422,       -19422,      4010497,      4010497,       280005,       280005
    .long      2706023,        95776,      3077325,      3530437,     -1661693,     -3592148,     -2537516,      3915439
    .long     -3861115,     -3043716,      3574422,     -2867647,      3539968,      -300467,      2348700,      -539299
    .long     -1699267,     -1643818,      3505694,     -3821735,      3507263,     -2140649,     -1600420,      3699596
    .long       811944,       531354,       954230,      3881043,      3900724,     -2556880,      2071892,     -2797779
    .long     -3930395,     -3677745,     -1452451,      2176455,     -1257611,     -4083598,     -3190144,     -3632928
    .long      3412210,      2147896,     -2967645,      -411027,      -671102,       -22981,      -381987,      1852771
    .long     -3343383,       508951,        44288,       904516,     -3724342,      1653064,      2389356,       759969
    .long       189548,      3159746,     -2409325,      1315589,      1285669,      -812732,     -3019102,     -3628969
    .long     -1528703,     -3041255,      3475950,     -1585221,      1939314,     -1000202,     -3157330,       126922
    .long      -983419,      2715295,     -3693493,     -2477047,     -1228525,     -1308169,      1349076,     -1430430
    .long       264944,      3097992,     -1100098,      3958618,        -8578,     -3249728,      -210977,     -1316856
    .long     -3553272,     -1851402,      -177440,      1341330,     -1584928,     -1439742,     -3881060,      3839961
    .long      2091667,     -3342478,       266997,     -3520352,       900702,       495491,      -655327,     -3556995
    .long       342297,      3437287,      2842341,      4055324,     -3767016,     -2994039,     -1333058,      -451100
    .long     -1279661,      1500165,      -542412,     -2584293,     -2013608,      1957272,     -3183426,       810149
    .long     -3038916,      2213111,      -426683,     -1667432,     -2939036,       183443,      -554416,      3937738
    .long      3407706,      2244091,      2434439,     -3759364,      1859098,     -1613174,     -3122442,      -525098
    .long       286988,     -3342277,      2691481,      1247620,      1250494,      1869119,      1237275,      1312455
    .long      1917081,       777191,     -2831860,     -3724270,      2432395,      3369112,       162844,      1652634
    .long      3523897,      -975884,      1723600,     -1104333,     -2235985,      -976891,      3919660,      1400424
    .long      2316500,     -2446433,     -1235728,     -1197226,       909542,       -43260,      2031748,      -768622
    .long     -2437823,      1735879,     -2590150,      2486353,      2635921,      1903435,     -3318210,      3306115
    .long     -2546312,      2235880,     -1671176,       594136,      2454455,       185531,      1616392,     -3694233
    .long      3866901,      1717735,     -1803090,      -260646,      -420899,      1612842,       -48306,      -846154
    .long      3817976,     -3562462,      3513181,     -3193378,       819034,      -522500,      3207046,     -3595838
    .long      4108315,       203044,      1265009,      1595974,     -3548272,     -1050970,     -1430225,     -1962642
    .long     -1374803,      3406031,     -1846953,     -3776993,      -164721,     -1207385,      3014001,     -1799107
    .long       269760,       472078,      1910376,     -3833893,     -2286327,     -3545687,     -1362209,      1976782
