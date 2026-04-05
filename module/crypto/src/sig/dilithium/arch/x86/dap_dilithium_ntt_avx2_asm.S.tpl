/*
 * Dilithium NTT — AVX2 hand-tuned assembly with pre-computed zeta*QINV.
 * Generated from dap_dilithium_ntt_avx2_asm.S.tpl by dap_tpl.
 *
 * Key optimization: Montgomery butterfly uses pre-computed zeta*QINV table,
 * eliminating one vpmulld from the critical dependency chain (14→9 cycles).
 *
 * void dap_dilithium_ntt_forward_{{ARCH_LOWER}}_asm(int32_t coeffs[256]);
 * void dap_dilithium_ntt_inverse_{{ARCH_LOWER}}_asm(int32_t coeffs[256]);
 * void dap_dilithium_pointwise_mont_{{ARCH_LOWER}}_asm(int32_t *c, const int32_t *a, const int32_t *b);
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#define DIL_N    256
#define DIL_Q    8380417

/* ============================================================================
 * Montgomery reduce for YMM: t = (zeta * data) * R^{-1} mod Q
 *
 * Uses pre-computed zeta_qinv = zeta * QINV mod 2^32
 *
 * Inputs:  data_ymm, z_ymm (zeta), zq_ymm (zeta_qinv), q_ymm (Q broadcast)
 * Output:  result in res_ymm (first scratch register)
 * Clobbers: scratch registers s1..s5
 * ============================================================================ */
#define MONT_REDUCE_YMM(data, z, zq, q, res, s1, s2, s3, s4, s5) \
    vpmulld  zq, data, res;            /* u = zeta_qinv * data (low 32) */    \
    vpmuldq  z, data, s1;              /* ab_even = zeta * data[even] (64-bit) */ \
    vpmuludq res, q, s2;               /* uq_even = u * Q (unsigned 64-bit) */ \
    vpaddq   s2, s1, s1;               /* ab_even + uq_even */                \
    vpsrlq   $32, s1, s1;              /* result_even (high 32 bits) */        \
    vpsrlq   $32, data, s3;            /* data_odd */                          \
    vpsrlq   $32, res, s4;             /* u_odd */                             \
    vpmuldq  z, s3, s5;                /* ab_odd */                            \
    vpmuludq s4, q, s4;                /* uq_odd */                            \
    vpaddq   s4, s5, s5;               /* ab_odd + uq_odd (result in hi32) */ \
    vpblendd $0xAA, s5, s1, res        /* merge even/odd */

/* Same for XMM (128-bit, used in inner len=4 layer) */
#define MONT_REDUCE_XMM(data, z, zq, q, res, s1, s2, s3, s4, s5) \
    vpmulld  zq, data, res;            \
    vpmuldq  z, data, s1;              \
    vpmuludq res, q, s2;               \
    vpaddq   s2, s1, s1;               \
    vpsrlq   $32, s1, s1;              \
    vpsrlq   $32, data, s3;            \
    vpsrlq   $32, res, s4;             \
    vpmuldq  z, s3, s5;                \
    vpmuludq s4, q, s4;                \
    vpaddq   s4, s5, s5;               \
    vpblendd $0x0A, s5, s1, res

/* Butterfly: load a,b; compute t=mont(zeta*b); store a+t, a-t */
#define BF(a_off, b_off) \
    vmovdqu  a_off(%rdi), %ymm0;       \
    vmovdqu  b_off(%rdi), %ymm1;       \
    MONT_REDUCE_YMM(%ymm1, %ymm14, %ymm13, %ymm15, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7); \
    vpaddd   %ymm2, %ymm0, %ymm3;     \
    vpsubd   %ymm2, %ymm0, %ymm4;     \
    vmovdqu  %ymm3, a_off(%rdi);       \
    vmovdqu  %ymm4, b_off(%rdi)

/* Load zeta[k] and zeta_qinv[k] as broadcasts */
#define LOAD_ZETA(k) \
    vpbroadcastd s_zetas+4*(k)(%rip), %ymm14; \
    vpbroadcastd s_zetas_qinv+4*(k)(%rip), %ymm13

/* Inverse butterfly (Gentleman-Sande): a'=a+b, b'=mont(zeta_inv*(a-b)) */
#define IBF(a_off, b_off) \
    vmovdqu a_off(%rdi), %ymm0;       \
    vmovdqu b_off(%rdi), %ymm1;       \
    vpaddd  %ymm1, %ymm0, %ymm2;     \
    vpsubd  %ymm1, %ymm0, %ymm3;     \
    MONT_REDUCE_YMM(%ymm3, %ymm14, %ymm13, %ymm15, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8, %ymm9); \
    vmovdqu %ymm2, a_off(%rdi);       \
    vmovdqu %ymm4, b_off(%rdi)

#define LOAD_ZETA_INV(k) \
    vpbroadcastd s_zetas_inv+4*(k)(%rip), %ymm14; \
    vpbroadcastd s_zetas_inv_qinv+4*(k)(%rip), %ymm13

.section .rodata
.p2align 5

s_zetas:
    .long 0, 25847, 5771523, 7861508, 237124, 7602457, 7504169, 466468
    .long 1826347, 2353451, 8021166, 6288512, 3119733, 5495562, 3111497, 2680103
    .long 2725464, 1024112, 7300517, 3585928, 7830929, 7260833, 2619752, 6271868
    .long 6262231, 4520680, 6980856, 5102745, 1757237, 8360995, 4010497, 280005
    .long 2706023, 95776, 3077325, 3530437, 6718724, 4788269, 5842901, 3915439
    .long 4519302, 5336701, 3574422, 5512770, 3539968, 8079950, 2348700, 7841118
    .long 6681150, 6736599, 3505694, 4558682, 3507263, 6239768, 6779997, 3699596
    .long 811944, 531354, 954230, 3881043, 3900724, 5823537, 2071892, 5582638
    .long 4450022, 6851714, 4702672, 5339162, 6927966, 3475950, 2176455, 6795196
    .long 7122806, 1939314, 4296819, 7380215, 5190273, 5223087, 4747489, 126922
    .long 3412210, 7396998, 2147896, 2715295, 5412772, 4686924, 7969390, 5903370
    .long 7709315, 7151892, 8357436, 7072248, 7998430, 1349076, 1852771, 6949987
    .long 5037034, 264944, 508951, 3097992, 44288, 7280319, 904516, 3958618
    .long 4656075, 8371839, 1653064, 5130689, 2389356, 8169440, 759969, 7063561
    .long 189548, 4827145, 3159746, 6529015, 5971092, 8202977, 1315589, 1341330
    .long 1285669, 6795489, 7567685, 6940675, 5361315, 4499357, 4751448, 3839961
    .long 2091667, 3407706, 2316500, 3817976, 5037939, 2244091, 5933984, 4817955
    .long 266997, 2434439, 7144689, 3513181, 4860065, 4621053, 7183191, 5187039
    .long 900702, 1859098, 909542, 819034, 495491, 6767243, 8337157, 7857917
    .long 7725090, 5257975, 2031748, 3207046, 4823422, 7855319, 7611795, 4784579
    .long 342297, 286988, 5942594, 4108315, 3437287, 5038140, 1735879, 203044
    .long 2842341, 2691481, 5790267, 1265009, 4055324, 1247620, 2486353, 1595974
    .long 4613401, 1250494, 2635921, 4832145, 5386378, 1869119, 1903435, 7329447
    .long 7047359, 1237275, 5062207, 6950192, 7929317, 1312455, 3306115, 6417775
    .long 7100756, 1917081, 5834105, 7005614, 1500165, 777191, 2235880, 3406031
    .long 7838005, 5548557, 6709241, 6533464, 5796124, 4656147, 594136, 4603424
    .long 6366809, 2432395, 2454455, 8215696, 1957272, 3369112, 185531, 7173032
    .long 5196991, 162844, 1616392, 3014001, 810149, 1652634, 4686184, 6581310
    .long 5341501, 3523897, 3866901, 269760, 2213111, 7404533, 1717735, 472078
    .long 7953734, 1723600, 6577327, 1910376, 6712985, 7276084, 8119771, 4546524
    .long 5441381, 6144432, 7959518, 6094090, 183443, 7403526, 1612842, 4834730
    .long 7826001, 3919660, 8332111, 7018208, 3937738, 1400424, 7534263, 1976782

s_zetas_qinv:
    .long 0, -1830765815, 1929875197, 1927777020, -1640767044, -1477910809, -1612161321, -1640734244
    .long -308362795, 1815525077, 1374673746, 1091570560, 1929495947, -515185418, 285697463, -625853735
    .long -1727305304, -2082316400, 1364982363, -858240904, -1806278033, -222489249, 346752664, -684667772
    .long -1654287831, 878576920, 1257667336, 748618599, -329347125, -1837364259, 1443016191, 1170414139
    .long 1846138265, 1631226336, 1404529459, -1838055109, -1594295556, 1076973523, 1898723371, 594436433
    .long 202001018, 475984259, 561427818, -1797021250, 1061813248, -2059733582, 1661512036, 1104976546
    .long 1750224322, 901666089, -418987550, -1831915354, 1925356481, -992097816, -879957085, -2024403852
    .long -1484874664, 1636082790, 285388938, 1983539117, 1495136972, 950076367, 1714807468, 952438994
    .long 1574918426, 654783358, -1350681040, 1974159334, 2143979938, -1651689966, -1599739335, -140455868
    .long 1285853322, 1039411342, 993005453, -1955560695, 1440787839, -1529189039, -568627425, 2131021878
    .long 783134478, 247357818, 588790216, -1518161567, -289871780, 86965172, 1262003602, -1708872714
    .long -2135294595, -1787797780, 1018755524, -1638590968, 889861154, 120646188, -1665705315, 1669960605
    .long -1321868266, 916321552, -1225434135, -1155548552, 1784632064, -2143745727, -666258756, -1210558298
    .long -675310539, 1261461889, 1555941048, 318346815, 1999506068, -628664288, 1499481951, 1729304567
    .long 695180180, -1422575625, 1375177022, -1424130039, -1777179796, 1185330463, -334803717, -235321234
    .long 178766299, -168022241, 518252219, -1206536195, -1957047971, -985155485, -1146323032, 894060583
    .long 898413, -991903578, -1363007700, -746144248, 1363460237, -912367099, -30313376, 1420958685
    .long 605900043, 44694137, 326425359, -2032221021, -2027833505, -1176904445, -1683520343, -1904936415
    .long -14253662, 421552614, 517299994, -1257750362, -1014493059, 818371957, -2027935493, -1926727421
    .long -863641634, -1747917559, 1372618620, -1931587462, -1819892094, 325927721, -128353683, -1258381763
    .long -2124962073, -908452108, 1123881662, -885133339, 1223601433, -1851023420, -137583815, -1629985060
    .long 1920467227, 1176751719, 635454917, -1967222129, 1637785316, 1354528380, 642772911, -6363718
    .long 1536588519, 72690498, -45766801, 1287922799, -694382730, 314284737, -671509323, -1136965287
    .long -235104447, -985022747, 2070602177, -1779436848, 1045062171, -963438279, -419615363, -1116720495
    .long -831969620, 1078959975, -1216882041, -1042326958, 300448763, -604552167, 270590488, -1405999311
    .long -756955445, 1021949427, 1276805127, -713994584, 260312804, -608791571, -371462360, -940195360
    .long -1554794073, -173440395, 1357098057, 1542497136, -1339088280, 2126092136, 384158533, -2061661096
    .long 2040058689, 1316619236, -827959816, 883155599, 853476187, 1039370342, 596344472, -1726753854
    .long 2047270595, -6087993, -702390549, 1547952704, 1723816713, 110126091, 279505433, -394851342
    .long 1591599802, -565464272, 260424529, -283780712, 440824167, 1758099916, 71875109, -776003548
    .long -1119856485, 1600929360, 1208667170, -1123958026, -1544891539, -879867910, 1499603926, -201262506
    .long -155290193, 1809756372, -2036925263, -1934038752, 973777462, -400711272, 540420425, -374860238

s_zetas_inv:
    .long 6403635, 846154, 6979993, 4442679, 1362209, 48306, 4460757, 554416
    .long 3545687, 6767575, 976891, 8196974, 2286327, 420899, 2235985, 2939036
    .long 3833893, 260646, 1104333, 1667432, 6470041, 1803090, 6656817, 426683
    .long 7908339, 6662682, 975884, 6167306, 8110657, 4513516, 4856520, 3038916
    .long 1799107, 3694233, 6727783, 7570268, 5366416, 6764025, 8217573, 3183426
    .long 1207385, 8194886, 5011305, 6423145, 164721, 5925962, 5948022, 2013608
    .long 3776993, 7786281, 3724270, 2584293, 1846953, 1671176, 2831860, 542412
    .long 4974386, 6144537, 7603226, 6880252, 1374803, 2546312, 6463336, 1279661
    .long 1962642, 5074302, 7067962, 451100, 1430225, 3318210, 7143142, 1333058
    .long 1050970, 6476982, 6511298, 2994039, 3548272, 5744496, 7129923, 3767016
    .long 6784443, 5894064, 7132797, 4325093, 7115408, 2590150, 5688936, 5538076
    .long 8177373, 6644538, 3342277, 4943130, 4272102, 2437823, 8093429, 8038120
    .long 3595838, 768622, 525098, 3556995, 5173371, 6348669, 3122442, 655327
    .long 522500, 43260, 1613174, 7884926, 7561383, 7470875, 6521319, 7479715
    .long 3193378, 1197226, 3759364, 3520352, 4867236, 1235728, 5945978, 8113420
    .long 3562462, 2446433, 6136326, 3342478, 4562441, 6063917, 4972711, 6288750
    .long 4540456, 3628969, 3881060, 3019102, 1439742, 812732, 1584928, 7094748
    .long 7039087, 7064828, 177440, 2409325, 1851402, 5220671, 3553272, 8190869
    .long 1316856, 7620448, 210977, 5991061, 3249728, 6727353, 8578, 3724342
    .long 4421799, 7475901, 1100098, 8336129, 5282425, 7871466, 8115473, 3343383
    .long 1430430, 6527646, 7031341, 381987, 1308169, 22981, 1228525, 671102
    .long 2477047, 411027, 3693493, 2967645, 5665122, 6232521, 983419, 4968207
    .long 8253495, 3632928, 3157330, 3190144, 1000202, 4083598, 6441103, 1257611
    .long 1585221, 6203962, 4904467, 1452451, 3041255, 3677745, 1528703, 3930395
    .long 2797779, 6308525, 2556880, 4479693, 4499374, 7426187, 7849063, 7568473
    .long 4680821, 1600420, 2140649, 4873154, 3821735, 4874723, 1643818, 1699267
    .long 539299, 6031717, 300467, 4840449, 2867647, 4805995, 3043716, 3861115
    .long 4464978, 2537516, 3592148, 1661693, 4849980, 5303092, 8284641, 5674394
    .long 8100412, 4369920, 19422, 6623180, 3277672, 1399561, 3859737, 2118186
    .long 2108549, 5760665, 1119584, 549488, 4794489, 1079900, 7356305, 5654953
    .long 5700314, 5268920, 2884855, 5260684, 2091905, 359251, 6026966, 6554070
    .long 7913949, 876248, 777960, 8143293, 518909, 2608894, 8354570

s_zetas_inv_qinv:
    .long 374860237, -540420426, 400711271, -973777463, 1934038751, 2036925262, -1809756373, 155290192
    .long 201262505, -1499603927, 879867909, 1544891538, 1123958025, -1208667171, -1600929361, 1119856484
    .long 776003547, -71875110, -1758099917, -440824168, 283780711, -260424530, 565464271, -1591599803
    .long 394851341, -279505434, -110126092, -1723816714, -1547952705, 702390548, 6087992, -2047270596
    .long 1726753853, -596344473, -1039370343, -853476188, -883155600, 827959815, -1316619237, -2040058690
    .long 2061661095, -384158534, -2126092137, 1339088279, -1542497137, -1357098058, 173440394, 1554794072
    .long 940195359, 371462359, 608791570, -260312805, 713994583, -1276805128, -1021949428, 756955444
    .long 1405999310, -270590489, 604552166, -300448764, 1042326957, 1216882040, -1078959976, 831969619
    .long 1116720494, 419615362, 963438278, -1045062172, 1779436847, -2070602178, 985022746, 235104446
    .long 1136965286, 671509322, -314284738, 694382729, -1287922800, 45766800, -72690499, -1536588520
    .long 6363717, -642772912, -1354528381, -1637785317, 1967222128, -635454918, -1176751720, -1920467228
    .long 1629985059, 137583814, 1851023419, -1223601434, 885133338, -1123881663, 908452107, 2124962072
    .long 1258381762, 128353682, -325927722, 1819892093, 1931587461, -1372618621, 1747917558, 863641633
    .long 1926727420, 2027935492, -818371958, 1014493058, 1257750361, -517299995, -421552615, 14253661
    .long 1904936414, 1683520342, 1176904444, 2027833504, 2032221020, -326425360, -44694138, -605900044
    .long -1420958686, 30313375, 912367098, -1363460238, 746144247, 1363007699, 991903577, -898414
    .long -894060584, 1146323031, 985155484, 1957047970, 1206536194, -518252220, 168022240, -178766300
    .long 235321233, 334803716, -1185330464, 1777179795, 1424130038, -1375177023, 1422575624, -695180181
    .long -1729304568, -1499481952, 628664287, -1999506069, -318346816, -1555941049, -1261461890, 675310538
    .long 1210558297, 666258755, 2143745726, -1784632065, 1155548551, 1225434134, -916321553, 1321868265
    .long -1669960606, 1665705314, -120646189, -889861155, 1638590967, -1018755525, 1787797779, 2135294594
    .long 1708872713, -1262003603, -86965173, 289871779, 1518161566, -588790217, -247357819, -783134479
    .long -2131021879, 568627424, 1529189038, -1440787840, 1955560694, -993005454, -1039411343, -1285853323
    .long 140455867, 1599739334, 1651689965, -2143979939, -1974159335, 1350681039, -654783359, -1574918427
    .long -952438995, -1714807469, -950076368, -1495136973, -1983539118, -285388939, -1636082791, 1484874663
    .long 2024403851, 879957084, 992097815, -1925356482, 1831915353, 418987549, -901666090, -1750224323
    .long -1104976547, -1661512037, 2059733581, -1061813249, 1797021249, -561427819, -475984260, -202001019
    .long -594436434, -1898723372, -1076973524, 1594295555, 1838055108, -1404529460, -1631226337, -1846138266
    .long -1170414140, -1443016192, 1837364258, 329347124, -748618600, -1257667337, -878576921, 1654287830
    .long 684667771, -346752665, 222489248, 1806278032, 858240903, -1364982364, 2082316399, 1727305303
    .long 625853734, -285697464, 515185417, -1929495948, -1091570561, -1374673747, -1815525078, 308362794
    .long 1640734243, 1612161320, 1477910808, 1640767043, -1927777021, -1929875198, 1830765814

s_intt_f:      .long 41978
s_intt_f_qinv: .long 8395782

.p2align 5
.L_fwd_z1_blk0:
    .long 2091667, 2091667, 3407706, 3407706, 2316500, 2316500, 3817976, 3817976
.L_fwd_zq1_blk0:
    .long 898413, 898413, -991903578, -991903578, -1363007700, -1363007700, -746144248, -746144248
.p2align 5
.L_fwd_z1_blk1:
    .long 5037939, 5037939, 2244091, 2244091, 5933984, 5933984, 4817955, 4817955
.L_fwd_zq1_blk1:
    .long 1363460237, 1363460237, -912367099, -912367099, -30313376, -30313376, 1420958685, 1420958685
.p2align 5
.L_fwd_z1_blk2:
    .long 266997, 266997, 2434439, 2434439, 7144689, 7144689, 3513181, 3513181
.L_fwd_zq1_blk2:
    .long 605900043, 605900043, 44694137, 44694137, 326425359, 326425359, -2032221021, -2032221021
.p2align 5
.L_fwd_z1_blk3:
    .long 4860065, 4860065, 4621053, 4621053, 7183191, 7183191, 5187039, 5187039
.L_fwd_zq1_blk3:
    .long -2027833505, -2027833505, -1176904445, -1176904445, -1683520343, -1683520343, -1904936415, -1904936415
.p2align 5
.L_fwd_z1_blk4:
    .long 900702, 900702, 1859098, 1859098, 909542, 909542, 819034, 819034
.L_fwd_zq1_blk4:
    .long -14253662, -14253662, 421552614, 421552614, 517299994, 517299994, -1257750362, -1257750362
.p2align 5
.L_fwd_z1_blk5:
    .long 495491, 495491, 6767243, 6767243, 8337157, 8337157, 7857917, 7857917
.L_fwd_zq1_blk5:
    .long -1014493059, -1014493059, 818371957, 818371957, -2027935493, -2027935493, -1926727421, -1926727421
.p2align 5
.L_fwd_z1_blk6:
    .long 7725090, 7725090, 5257975, 5257975, 2031748, 2031748, 3207046, 3207046
.L_fwd_zq1_blk6:
    .long -863641634, -863641634, -1747917559, -1747917559, 1372618620, 1372618620, -1931587462, -1931587462
.p2align 5
.L_fwd_z1_blk7:
    .long 4823422, 4823422, 7855319, 7855319, 7611795, 7611795, 4784579, 4784579
.L_fwd_zq1_blk7:
    .long -1819892094, -1819892094, 325927721, 325927721, -128353683, -128353683, -1258381763, -1258381763
.p2align 5
.L_fwd_z1_blk8:
    .long 342297, 342297, 286988, 286988, 5942594, 5942594, 4108315, 4108315
.L_fwd_zq1_blk8:
    .long -2124962073, -2124962073, -908452108, -908452108, 1123881662, 1123881662, -885133339, -885133339
.p2align 5
.L_fwd_z1_blk9:
    .long 3437287, 3437287, 5038140, 5038140, 1735879, 1735879, 203044, 203044
.L_fwd_zq1_blk9:
    .long 1223601433, 1223601433, -1851023420, -1851023420, -137583815, -137583815, -1629985060, -1629985060
.p2align 5
.L_fwd_z1_blk10:
    .long 2842341, 2842341, 2691481, 2691481, 5790267, 5790267, 1265009, 1265009
.L_fwd_zq1_blk10:
    .long 1920467227, 1920467227, 1176751719, 1176751719, 635454917, 635454917, -1967222129, -1967222129
.p2align 5
.L_fwd_z1_blk11:
    .long 4055324, 4055324, 1247620, 1247620, 2486353, 2486353, 1595974, 1595974
.L_fwd_zq1_blk11:
    .long 1637785316, 1637785316, 1354528380, 1354528380, 642772911, 642772911, -6363718, -6363718
.p2align 5
.L_fwd_z1_blk12:
    .long 4613401, 4613401, 1250494, 1250494, 2635921, 2635921, 4832145, 4832145
.L_fwd_zq1_blk12:
    .long 1536588519, 1536588519, 72690498, 72690498, -45766801, -45766801, 1287922799, 1287922799
.p2align 5
.L_fwd_z1_blk13:
    .long 5386378, 5386378, 1869119, 1869119, 1903435, 1903435, 7329447, 7329447
.L_fwd_zq1_blk13:
    .long -694382730, -694382730, 314284737, 314284737, -671509323, -671509323, -1136965287, -1136965287
.p2align 5
.L_fwd_z1_blk14:
    .long 7047359, 7047359, 1237275, 1237275, 5062207, 5062207, 6950192, 6950192
.L_fwd_zq1_blk14:
    .long -235104447, -235104447, -985022747, -985022747, 2070602177, 2070602177, -1779436848, -1779436848
.p2align 5
.L_fwd_z1_blk15:
    .long 7929317, 7929317, 1312455, 1312455, 3306115, 3306115, 6417775, 6417775
.L_fwd_zq1_blk15:
    .long 1045062171, 1045062171, -963438279, -963438279, -419615363, -419615363, -1116720495, -1116720495
.p2align 5
.L_fwd_z1_blk16:
    .long 7100756, 7100756, 1917081, 1917081, 5834105, 5834105, 7005614, 7005614
.L_fwd_zq1_blk16:
    .long -831969620, -831969620, 1078959975, 1078959975, -1216882041, -1216882041, -1042326958, -1042326958
.p2align 5
.L_fwd_z1_blk17:
    .long 1500165, 1500165, 777191, 777191, 2235880, 2235880, 3406031, 3406031
.L_fwd_zq1_blk17:
    .long 300448763, 300448763, -604552167, -604552167, 270590488, 270590488, -1405999311, -1405999311
.p2align 5
.L_fwd_z1_blk18:
    .long 7838005, 7838005, 5548557, 5548557, 6709241, 6709241, 6533464, 6533464
.L_fwd_zq1_blk18:
    .long -756955445, -756955445, 1021949427, 1021949427, 1276805127, 1276805127, -713994584, -713994584
.p2align 5
.L_fwd_z1_blk19:
    .long 5796124, 5796124, 4656147, 4656147, 594136, 594136, 4603424, 4603424
.L_fwd_zq1_blk19:
    .long 260312804, 260312804, -608791571, -608791571, -371462360, -371462360, -940195360, -940195360
.p2align 5
.L_fwd_z1_blk20:
    .long 6366809, 6366809, 2432395, 2432395, 2454455, 2454455, 8215696, 8215696
.L_fwd_zq1_blk20:
    .long -1554794073, -1554794073, -173440395, -173440395, 1357098057, 1357098057, 1542497136, 1542497136
.p2align 5
.L_fwd_z1_blk21:
    .long 1957272, 1957272, 3369112, 3369112, 185531, 185531, 7173032, 7173032
.L_fwd_zq1_blk21:
    .long -1339088280, -1339088280, 2126092136, 2126092136, 384158533, 384158533, -2061661096, -2061661096
.p2align 5
.L_fwd_z1_blk22:
    .long 5196991, 5196991, 162844, 162844, 1616392, 1616392, 3014001, 3014001
.L_fwd_zq1_blk22:
    .long 2040058689, 2040058689, 1316619236, 1316619236, -827959816, -827959816, 883155599, 883155599
.p2align 5
.L_fwd_z1_blk23:
    .long 810149, 810149, 1652634, 1652634, 4686184, 4686184, 6581310, 6581310
.L_fwd_zq1_blk23:
    .long 853476187, 853476187, 1039370342, 1039370342, 596344472, 596344472, -1726753854, -1726753854
.p2align 5
.L_fwd_z1_blk24:
    .long 5341501, 5341501, 3523897, 3523897, 3866901, 3866901, 269760, 269760
.L_fwd_zq1_blk24:
    .long 2047270595, 2047270595, -6087993, -6087993, -702390549, -702390549, 1547952704, 1547952704
.p2align 5
.L_fwd_z1_blk25:
    .long 2213111, 2213111, 7404533, 7404533, 1717735, 1717735, 472078, 472078
.L_fwd_zq1_blk25:
    .long 1723816713, 1723816713, 110126091, 110126091, 279505433, 279505433, -394851342, -394851342
.p2align 5
.L_fwd_z1_blk26:
    .long 7953734, 7953734, 1723600, 1723600, 6577327, 6577327, 1910376, 1910376
.L_fwd_zq1_blk26:
    .long 1591599802, 1591599802, -565464272, -565464272, 260424529, 260424529, -283780712, -283780712
.p2align 5
.L_fwd_z1_blk27:
    .long 6712985, 6712985, 7276084, 7276084, 8119771, 8119771, 4546524, 4546524
.L_fwd_zq1_blk27:
    .long 440824167, 440824167, 1758099916, 1758099916, 71875109, 71875109, -776003548, -776003548
.p2align 5
.L_fwd_z1_blk28:
    .long 5441381, 5441381, 6144432, 6144432, 7959518, 7959518, 6094090, 6094090
.L_fwd_zq1_blk28:
    .long -1119856485, -1119856485, 1600929360, 1600929360, 1208667170, 1208667170, -1123958026, -1123958026
.p2align 5
.L_fwd_z1_blk29:
    .long 183443, 183443, 7403526, 7403526, 1612842, 1612842, 4834730, 4834730
.L_fwd_zq1_blk29:
    .long -1544891539, -1544891539, -879867910, -879867910, 1499603926, 1499603926, -201262506, -201262506
.p2align 5
.L_fwd_z1_blk30:
    .long 7826001, 7826001, 3919660, 3919660, 8332111, 8332111, 7018208, 7018208
.L_fwd_zq1_blk30:
    .long -155290193, -155290193, 1809756372, 1809756372, -2036925263, -2036925263, -1934038752, -1934038752
.p2align 5
.L_fwd_z1_blk31:
    .long 3937738, 3937738, 1400424, 1400424, 7534263, 7534263, 1976782, 1976782
.L_fwd_zq1_blk31:
    .long 973777462, 973777462, -400711272, -400711272, 540420425, 540420425, -374860238, -374860238


{{#include ASM_MACROS}}

.text

/* ============================================================================
 * Forward NTT (Cooley-Tukey) with pre-computed Montgomery reduction
 * void dap_dilithium_ntt_forward_{{ARCH_LOWER}}_asm(int32_t coeffs[256]);
 * ============================================================================ */
.globl dap_dilithium_ntt_forward_{{ARCH_LOWER}}_asm
FUNC_TYPE(dap_dilithium_ntt_forward_{{ARCH_LOWER}}_asm)
.p2align 4
dap_dilithium_ntt_forward_{{ARCH_LOWER}}_asm:
    /* Q broadcast constant */
    movl    $DIL_Q, %eax
    vmovd   %eax, %xmm15
    vpbroadcastd %xmm15, %ymm15

    /* Layer len=128: 1 groups x 16 pairs, k=1..1 */
    LOAD_ZETA(1)
    BF(0, 512)
    BF(32, 544)
    BF(64, 576)
    BF(96, 608)
    BF(128, 640)
    BF(160, 672)
    BF(192, 704)
    BF(224, 736)
    BF(256, 768)
    BF(288, 800)
    BF(320, 832)
    BF(352, 864)
    BF(384, 896)
    BF(416, 928)
    BF(448, 960)
    BF(480, 992)
    /* Layer len=64: 2 groups x 8 pairs, k=2..3 */
    LOAD_ZETA(2)
    BF(0, 256)
    BF(32, 288)
    BF(64, 320)
    BF(96, 352)
    BF(128, 384)
    BF(160, 416)
    BF(192, 448)
    BF(224, 480)
    LOAD_ZETA(3)
    BF(512, 768)
    BF(544, 800)
    BF(576, 832)
    BF(608, 864)
    BF(640, 896)
    BF(672, 928)
    BF(704, 960)
    BF(736, 992)
    /* Layer len=32: 4 groups x 4 pairs, k=4..7 */
    LOAD_ZETA(4)
    BF(0, 128)
    BF(32, 160)
    BF(64, 192)
    BF(96, 224)
    LOAD_ZETA(5)
    BF(256, 384)
    BF(288, 416)
    BF(320, 448)
    BF(352, 480)
    LOAD_ZETA(6)
    BF(512, 640)
    BF(544, 672)
    BF(576, 704)
    BF(608, 736)
    LOAD_ZETA(7)
    BF(768, 896)
    BF(800, 928)
    BF(832, 960)
    BF(864, 992)
    /* Layer len=16: 8 groups x 2 pairs, k=8..15 */
    LOAD_ZETA(8)
    BF(0, 64)
    BF(32, 96)
    LOAD_ZETA(9)
    BF(128, 192)
    BF(160, 224)
    LOAD_ZETA(10)
    BF(256, 320)
    BF(288, 352)
    LOAD_ZETA(11)
    BF(384, 448)
    BF(416, 480)
    LOAD_ZETA(12)
    BF(512, 576)
    BF(544, 608)
    LOAD_ZETA(13)
    BF(640, 704)
    BF(672, 736)
    LOAD_ZETA(14)
    BF(768, 832)
    BF(800, 864)
    LOAD_ZETA(15)
    BF(896, 960)
    BF(928, 992)
    /* Layer len=8: 16 groups x 1 pairs, k=16..31 */
    LOAD_ZETA(16)
    BF(0, 32)
    LOAD_ZETA(17)
    BF(64, 96)
    LOAD_ZETA(18)
    BF(128, 160)
    LOAD_ZETA(19)
    BF(192, 224)
    LOAD_ZETA(20)
    BF(256, 288)
    LOAD_ZETA(21)
    BF(320, 352)
    LOAD_ZETA(22)
    BF(384, 416)
    LOAD_ZETA(23)
    BF(448, 480)
    LOAD_ZETA(24)
    BF(512, 544)
    LOAD_ZETA(25)
    BF(576, 608)
    LOAD_ZETA(26)
    BF(640, 672)
    LOAD_ZETA(27)
    BF(704, 736)
    LOAD_ZETA(28)
    BF(768, 800)
    LOAD_ZETA(29)
    BF(832, 864)
    LOAD_ZETA(30)
    BF(896, 928)
    LOAD_ZETA(31)
    BF(960, 992)

    /* Inner layers (len=4,2,1): 32 blocks of 8 elements */
    /* Block 0 at offset 0 */
    vmovdqu 0(%rdi), %ymm0

    /* len=4: zeta[32] = 2706023 */
    movl $2706023, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1846138265, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[64]=4450022, zeta[65]=6851714 */
    movl $4450022, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $6851714, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $1574918426, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $654783358, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[128..131] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk0(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk0(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 0(%rdi)

    /* Block 1 at offset 32 */
    vmovdqu 32(%rdi), %ymm0

    /* len=4: zeta[33] = 95776 */
    movl $95776, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1631226336, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[66]=4702672, zeta[67]=5339162 */
    movl $4702672, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $5339162, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-1350681040, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $1974159334, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[132..135] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk1(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk1(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 32(%rdi)

    /* Block 2 at offset 64 */
    vmovdqu 64(%rdi), %ymm0

    /* len=4: zeta[34] = 3077325 */
    movl $3077325, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1404529459, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[68]=6927966, zeta[69]=3475950 */
    movl $6927966, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $3475950, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $2143979938, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-1651689966, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[136..139] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk2(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk2(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 64(%rdi)

    /* Block 3 at offset 96 */
    vmovdqu 96(%rdi), %ymm0

    /* len=4: zeta[35] = 3530437 */
    movl $3530437, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $-1838055109, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[70]=2176455, zeta[71]=6795196 */
    movl $2176455, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $6795196, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-1599739335, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-140455868, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[140..143] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk3(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk3(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 96(%rdi)

    /* Block 4 at offset 128 */
    vmovdqu 128(%rdi), %ymm0

    /* len=4: zeta[36] = 6718724 */
    movl $6718724, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $-1594295556, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[72]=7122806, zeta[73]=1939314 */
    movl $7122806, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $1939314, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $1285853322, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $1039411342, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[144..147] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk4(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk4(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 128(%rdi)

    /* Block 5 at offset 160 */
    vmovdqu 160(%rdi), %ymm0

    /* len=4: zeta[37] = 4788269 */
    movl $4788269, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1076973523, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[74]=4296819, zeta[75]=7380215 */
    movl $4296819, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $7380215, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $993005453, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-1955560695, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[148..151] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk5(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk5(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 160(%rdi)

    /* Block 6 at offset 192 */
    vmovdqu 192(%rdi), %ymm0

    /* len=4: zeta[38] = 5842901 */
    movl $5842901, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1898723371, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[76]=5190273, zeta[77]=5223087 */
    movl $5190273, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $5223087, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $1440787839, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-1529189039, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[152..155] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk6(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk6(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 192(%rdi)

    /* Block 7 at offset 224 */
    vmovdqu 224(%rdi), %ymm0

    /* len=4: zeta[39] = 3915439 */
    movl $3915439, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $594436433, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[78]=4747489, zeta[79]=126922 */
    movl $4747489, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $126922, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-568627425, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $2131021878, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[156..159] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk7(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk7(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 224(%rdi)

    /* Block 8 at offset 256 */
    vmovdqu 256(%rdi), %ymm0

    /* len=4: zeta[40] = 4519302 */
    movl $4519302, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $202001018, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[80]=3412210, zeta[81]=7396998 */
    movl $3412210, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $7396998, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $783134478, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $247357818, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[160..163] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk8(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk8(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 256(%rdi)

    /* Block 9 at offset 288 */
    vmovdqu 288(%rdi), %ymm0

    /* len=4: zeta[41] = 5336701 */
    movl $5336701, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $475984259, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[82]=2147896, zeta[83]=2715295 */
    movl $2147896, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $2715295, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $588790216, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-1518161567, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[164..167] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk9(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk9(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 288(%rdi)

    /* Block 10 at offset 320 */
    vmovdqu 320(%rdi), %ymm0

    /* len=4: zeta[42] = 3574422 */
    movl $3574422, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $561427818, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[84]=5412772, zeta[85]=4686924 */
    movl $5412772, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $4686924, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-289871780, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $86965172, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[168..171] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk10(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk10(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 320(%rdi)

    /* Block 11 at offset 352 */
    vmovdqu 352(%rdi), %ymm0

    /* len=4: zeta[43] = 5512770 */
    movl $5512770, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $-1797021250, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[86]=7969390, zeta[87]=5903370 */
    movl $7969390, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $5903370, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $1262003602, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-1708872714, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[172..175] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk11(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk11(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 352(%rdi)

    /* Block 12 at offset 384 */
    vmovdqu 384(%rdi), %ymm0

    /* len=4: zeta[44] = 3539968 */
    movl $3539968, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1061813248, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[88]=7709315, zeta[89]=7151892 */
    movl $7709315, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $7151892, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-2135294595, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-1787797780, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[176..179] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk12(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk12(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 384(%rdi)

    /* Block 13 at offset 416 */
    vmovdqu 416(%rdi), %ymm0

    /* len=4: zeta[45] = 8079950 */
    movl $8079950, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $-2059733582, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[90]=8357436, zeta[91]=7072248 */
    movl $8357436, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $7072248, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $1018755524, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-1638590968, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[180..183] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk13(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk13(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 416(%rdi)

    /* Block 14 at offset 448 */
    vmovdqu 448(%rdi), %ymm0

    /* len=4: zeta[46] = 2348700 */
    movl $2348700, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1661512036, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[92]=7998430, zeta[93]=1349076 */
    movl $7998430, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $1349076, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $889861154, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $120646188, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[184..187] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk14(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk14(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 448(%rdi)

    /* Block 15 at offset 480 */
    vmovdqu 480(%rdi), %ymm0

    /* len=4: zeta[47] = 7841118 */
    movl $7841118, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1104976546, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[94]=1852771, zeta[95]=6949987 */
    movl $1852771, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $6949987, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-1665705315, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $1669960605, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[188..191] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk15(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk15(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 480(%rdi)

    /* Block 16 at offset 512 */
    vmovdqu 512(%rdi), %ymm0

    /* len=4: zeta[48] = 6681150 */
    movl $6681150, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1750224322, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[96]=5037034, zeta[97]=264944 */
    movl $5037034, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $264944, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-1321868266, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $916321552, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[192..195] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk16(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk16(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 512(%rdi)

    /* Block 17 at offset 544 */
    vmovdqu 544(%rdi), %ymm0

    /* len=4: zeta[49] = 6736599 */
    movl $6736599, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $901666089, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[98]=508951, zeta[99]=3097992 */
    movl $508951, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $3097992, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-1225434135, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-1155548552, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[196..199] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk17(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk17(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 544(%rdi)

    /* Block 18 at offset 576 */
    vmovdqu 576(%rdi), %ymm0

    /* len=4: zeta[50] = 3505694 */
    movl $3505694, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $-418987550, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[100]=44288, zeta[101]=7280319 */
    movl $44288, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $7280319, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $1784632064, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-2143745727, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[200..203] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk18(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk18(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 576(%rdi)

    /* Block 19 at offset 608 */
    vmovdqu 608(%rdi), %ymm0

    /* len=4: zeta[51] = 4558682 */
    movl $4558682, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $-1831915354, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[102]=904516, zeta[103]=3958618 */
    movl $904516, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $3958618, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-666258756, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-1210558298, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[204..207] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk19(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk19(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 608(%rdi)

    /* Block 20 at offset 640 */
    vmovdqu 640(%rdi), %ymm0

    /* len=4: zeta[52] = 3507263 */
    movl $3507263, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1925356481, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[104]=4656075, zeta[105]=8371839 */
    movl $4656075, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $8371839, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-675310539, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $1261461889, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[208..211] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk20(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk20(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 640(%rdi)

    /* Block 21 at offset 672 */
    vmovdqu 672(%rdi), %ymm0

    /* len=4: zeta[53] = 6239768 */
    movl $6239768, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $-992097816, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[106]=1653064, zeta[107]=5130689 */
    movl $1653064, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $5130689, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $1555941048, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $318346815, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[212..215] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk21(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk21(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 672(%rdi)

    /* Block 22 at offset 704 */
    vmovdqu 704(%rdi), %ymm0

    /* len=4: zeta[54] = 6779997 */
    movl $6779997, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $-879957085, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[108]=2389356, zeta[109]=8169440 */
    movl $2389356, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $8169440, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $1999506068, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-628664288, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[216..219] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk22(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk22(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 704(%rdi)

    /* Block 23 at offset 736 */
    vmovdqu 736(%rdi), %ymm0

    /* len=4: zeta[55] = 3699596 */
    movl $3699596, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $-2024403852, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[110]=759969, zeta[111]=7063561 */
    movl $759969, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $7063561, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $1499481951, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $1729304567, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[220..223] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk23(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk23(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 736(%rdi)

    /* Block 24 at offset 768 */
    vmovdqu 768(%rdi), %ymm0

    /* len=4: zeta[56] = 811944 */
    movl $811944, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $-1484874664, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[112]=189548, zeta[113]=4827145 */
    movl $189548, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $4827145, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $695180180, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-1422575625, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[224..227] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk24(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk24(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 768(%rdi)

    /* Block 25 at offset 800 */
    vmovdqu 800(%rdi), %ymm0

    /* len=4: zeta[57] = 531354 */
    movl $531354, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1636082790, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[114]=3159746, zeta[115]=6529015 */
    movl $3159746, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $6529015, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $1375177022, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-1424130039, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[228..231] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk25(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk25(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 800(%rdi)

    /* Block 26 at offset 832 */
    vmovdqu 832(%rdi), %ymm0

    /* len=4: zeta[58] = 954230 */
    movl $954230, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $285388938, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[116]=5971092, zeta[117]=8202977 */
    movl $5971092, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $8202977, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-1777179796, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $1185330463, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[232..235] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk26(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk26(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 832(%rdi)

    /* Block 27 at offset 864 */
    vmovdqu 864(%rdi), %ymm0

    /* len=4: zeta[59] = 3881043 */
    movl $3881043, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1983539117, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[118]=1315589, zeta[119]=1341330 */
    movl $1315589, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $1341330, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-334803717, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-235321234, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[236..239] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk27(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk27(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 864(%rdi)

    /* Block 28 at offset 896 */
    vmovdqu 896(%rdi), %ymm0

    /* len=4: zeta[60] = 3900724 */
    movl $3900724, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1495136972, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[120]=1285669, zeta[121]=6795489 */
    movl $1285669, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $6795489, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $178766299, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-168022241, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[240..243] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk28(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk28(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 896(%rdi)

    /* Block 29 at offset 928 */
    vmovdqu 928(%rdi), %ymm0

    /* len=4: zeta[61] = 5823537 */
    movl $5823537, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $950076367, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[122]=7567685, zeta[123]=6940675 */
    movl $7567685, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $6940675, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $518252219, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-1206536195, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[244..247] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk29(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk29(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 928(%rdi)

    /* Block 30 at offset 960 */
    vmovdqu 960(%rdi), %ymm0

    /* len=4: zeta[62] = 2071892 */
    movl $2071892, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $1714807468, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[124]=5361315, zeta[125]=4499357 */
    movl $5361315, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $4499357, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-1957047971, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $-985155485, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[248..251] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk30(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk30(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 960(%rdi)

    /* Block 31 at offset 992 */
    vmovdqu 992(%rdi), %ymm0

    /* len=4: zeta[63] = 5582638 */
    movl $5582638, %eax
    vmovd %eax, %xmm14
    vpbroadcastd %xmm14, %xmm14  /* 128-bit broadcast */
    movl $952438994, %eax
    vmovd %eax, %xmm13
    vpbroadcastd %xmm13, %xmm13
    vextracti128 $1, %ymm0, %xmm1   /* hi half */
    /* xmm0 = lo half (implicit) */
    MONT_REDUCE_XMM(%xmm1, %xmm14, %xmm13, %xmm15, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7)
    vpaddd %xmm2, %xmm0, %xmm3    /* lo + t */
    vpsubd %xmm2, %xmm0, %xmm4    /* lo - t */
    vinserti128 $1, %xmm4, %ymm3, %ymm0

    /* len=2: zeta[126]=4751448, zeta[127]=3839961 */
    movl $4751448, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8     /* z0 in low 128 */
    movl $3839961, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9     /* z1 in high 128 */
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    movl $-1146323032, %eax
    vmovd %eax, %xmm8
    vpbroadcastd %xmm8, %xmm8
    movl $894060583, %eax
    vmovd %eax, %xmm9
    vpbroadcastd %xmm9, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13
    vpshufd $0x44, %ymm0, %ymm1    /* lo: [0,1,0,1] per 128 */
    vpshufd $0xEE, %ymm0, %ymm2    /* hi: [2,3,2,3] per 128 */
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xCC, %ymm5, %ymm4, %ymm0  /* merge: [a+t, a-t] interleaved */

    /* len=1: zeta[252..255] */
    vpshufd $0xA0, %ymm0, %ymm1    /* lo: [0,0,2,2] per 128 */
    vpshufd $0xF5, %ymm0, %ymm2    /* hi: [1,1,3,3] per 128 */
    vmovdqu .L_fwd_z1_blk31(%rip), %ymm14
    vmovdqu .L_fwd_zq1_blk31(%rip), %ymm13
    MONT_REDUCE_YMM(%ymm2, %ymm14, %ymm13, %ymm15, %ymm3, %ymm4, %ymm5, %ymm6, %ymm7, %ymm8)
    vpaddd %ymm3, %ymm1, %ymm4
    vpsubd %ymm3, %ymm1, %ymm5
    vpblendd $0xAA, %ymm5, %ymm4, %ymm0

    vmovdqu %ymm0, 992(%rdi)


    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_ntt_forward_{{ARCH_LOWER}}_asm)


/* ============================================================================
 * Inverse NTT (Gentleman-Sande) with pre-computed zeta_inv*QINV.
 * Inner layers (len=1,2,4) are looped over 32 blocks.
 * Outer layers (len=8..128) are fully unrolled.
 * Final scaling by f = 41978 (Montgomery representation of 1/256 mod Q).
 *
 * void dap_dilithium_ntt_inverse_{{ARCH_LOWER}}_asm(int32_t coeffs[256]);
 * ============================================================================ */
.globl dap_dilithium_ntt_inverse_{{ARCH_LOWER}}_asm
FUNC_TYPE(dap_dilithium_ntt_inverse_{{ARCH_LOWER}}_asm)
.p2align 4
dap_dilithium_ntt_inverse_{{ARCH_LOWER}}_asm:
    movl    $DIL_Q, %eax
    vmovd   %eax, %xmm15
    vpbroadcastd %xmm15, %ymm15

    leaq    s_zetas_inv(%rip), %rsi
    leaq    s_zetas_inv_qinv(%rip), %rdx

    xorl    %ecx, %ecx

    /* Inner layers (len=1, 2, 4): 32 blocks of 8 coefficients */
    .p2align 4
.L_inv_inner:
    vmovdqu (%rdi,%rcx), %ymm0

    /* len=1: GS butterfly on adjacent pairs.
       zetas_inv[4*b..4*b+3], byte offset = rcx/2 */
    movq    %rcx, %rax
    shrq    $1, %rax
    vmovdqu (%rsi,%rax), %xmm10
    vmovdqu (%rdx,%rax), %xmm11
    vpunpckldq %xmm10, %xmm10, %xmm8
    vpunpckhdq %xmm10, %xmm10, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    vpunpckldq %xmm11, %xmm11, %xmm8
    vpunpckhdq %xmm11, %xmm11, %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13

    vpshufd $0xA0, %ymm0, %ymm1
    vpshufd $0xF5, %ymm0, %ymm2
    vpaddd  %ymm2, %ymm1, %ymm3
    vpsubd  %ymm2, %ymm1, %ymm4
    MONT_REDUCE_YMM(%ymm4, %ymm14, %ymm13, %ymm15, %ymm5, %ymm6, %ymm7, %ymm8, %ymm9, %ymm10)
    vpblendd $0xAA, %ymm5, %ymm3, %ymm0

    /* len=2: GS butterfly on pairs of 2.
       zetas_inv[128+2*b], byte offset = 512 + rcx/4 */
    movq    %rcx, %rax
    shrq    $2, %rax
    addq    $512, %rax
    vpbroadcastd (%rsi,%rax), %xmm8
    vpbroadcastd 4(%rsi,%rax), %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm14
    vpbroadcastd (%rdx,%rax), %xmm8
    vpbroadcastd 4(%rdx,%rax), %xmm9
    vinserti128 $1, %xmm9, %ymm8, %ymm13

    vpshufd $0x44, %ymm0, %ymm1
    vpshufd $0xEE, %ymm0, %ymm2
    vpaddd  %ymm2, %ymm1, %ymm3
    vpsubd  %ymm2, %ymm1, %ymm4
    MONT_REDUCE_YMM(%ymm4, %ymm14, %ymm13, %ymm15, %ymm5, %ymm6, %ymm7, %ymm8, %ymm9, %ymm10)
    vpblendd $0xCC, %ymm5, %ymm3, %ymm0

    /* len=4: GS butterfly on 128-bit halves.
       zetas_inv[192+b], byte offset = 768 + rcx/8 */
    movq    %rcx, %rax
    shrq    $3, %rax
    addq    $768, %rax
    vpbroadcastd (%rsi,%rax), %xmm14
    vpbroadcastd (%rdx,%rax), %xmm13

    vextracti128 $1, %ymm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm2
    vpsubd  %xmm1, %xmm0, %xmm3
    MONT_REDUCE_XMM(%xmm3, %xmm14, %xmm13, %xmm15, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm9)
    vinserti128 $1, %xmm4, %ymm2, %ymm0

    vmovdqu %ymm0, (%rdi,%rcx)

    addq    $32, %rcx
    cmpq    $1024, %rcx
    jne     .L_inv_inner

    /* Outer layers (len=8 through 128) */

    /* len=8: 16 groups x 1 pair, k=224..239 */
    LOAD_ZETA_INV(224)
    IBF(0, 32)
    LOAD_ZETA_INV(225)
    IBF(64, 96)
    LOAD_ZETA_INV(226)
    IBF(128, 160)
    LOAD_ZETA_INV(227)
    IBF(192, 224)
    LOAD_ZETA_INV(228)
    IBF(256, 288)
    LOAD_ZETA_INV(229)
    IBF(320, 352)
    LOAD_ZETA_INV(230)
    IBF(384, 416)
    LOAD_ZETA_INV(231)
    IBF(448, 480)
    LOAD_ZETA_INV(232)
    IBF(512, 544)
    LOAD_ZETA_INV(233)
    IBF(576, 608)
    LOAD_ZETA_INV(234)
    IBF(640, 672)
    LOAD_ZETA_INV(235)
    IBF(704, 736)
    LOAD_ZETA_INV(236)
    IBF(768, 800)
    LOAD_ZETA_INV(237)
    IBF(832, 864)
    LOAD_ZETA_INV(238)
    IBF(896, 928)
    LOAD_ZETA_INV(239)
    IBF(960, 992)
    /* len=16: 8 groups x 2 pairs, k=240..247 */
    LOAD_ZETA_INV(240)
    IBF(0, 64)
    IBF(32, 96)
    LOAD_ZETA_INV(241)
    IBF(128, 192)
    IBF(160, 224)
    LOAD_ZETA_INV(242)
    IBF(256, 320)
    IBF(288, 352)
    LOAD_ZETA_INV(243)
    IBF(384, 448)
    IBF(416, 480)
    LOAD_ZETA_INV(244)
    IBF(512, 576)
    IBF(544, 608)
    LOAD_ZETA_INV(245)
    IBF(640, 704)
    IBF(672, 736)
    LOAD_ZETA_INV(246)
    IBF(768, 832)
    IBF(800, 864)
    LOAD_ZETA_INV(247)
    IBF(896, 960)
    IBF(928, 992)
    /* len=32: 4 groups x 4 pairs, k=248..251 */
    LOAD_ZETA_INV(248)
    IBF(0, 128)
    IBF(32, 160)
    IBF(64, 192)
    IBF(96, 224)
    LOAD_ZETA_INV(249)
    IBF(256, 384)
    IBF(288, 416)
    IBF(320, 448)
    IBF(352, 480)
    LOAD_ZETA_INV(250)
    IBF(512, 640)
    IBF(544, 672)
    IBF(576, 704)
    IBF(608, 736)
    LOAD_ZETA_INV(251)
    IBF(768, 896)
    IBF(800, 928)
    IBF(832, 960)
    IBF(864, 992)
    /* len=64: 2 groups x 8 pairs, k=252..253 */
    LOAD_ZETA_INV(252)
    IBF(0, 256)
    IBF(32, 288)
    IBF(64, 320)
    IBF(96, 352)
    IBF(128, 384)
    IBF(160, 416)
    IBF(192, 448)
    IBF(224, 480)
    LOAD_ZETA_INV(253)
    IBF(512, 768)
    IBF(544, 800)
    IBF(576, 832)
    IBF(608, 864)
    IBF(640, 896)
    IBF(672, 928)
    IBF(704, 960)
    IBF(736, 992)
    /* len=128: 1 group x 16 pairs, k=254 */
    LOAD_ZETA_INV(254)
    IBF(0, 512)
    IBF(32, 544)
    IBF(64, 576)
    IBF(96, 608)
    IBF(128, 640)
    IBF(160, 672)
    IBF(192, 704)
    IBF(224, 736)
    IBF(256, 768)
    IBF(288, 800)
    IBF(320, 832)
    IBF(352, 864)
    IBF(384, 896)
    IBF(416, 928)
    IBF(448, 960)
    IBF(480, 992)

    /* Final scaling: coeffs[i] = mont(f * coeffs[i]) for all i */
    vpbroadcastd s_intt_f(%rip), %ymm14
    vpbroadcastd s_intt_f_qinv(%rip), %ymm13
    xorl    %ecx, %ecx
    .p2align 4
.L_inv_scale:
    vmovdqu (%rdi,%rcx), %ymm0
    MONT_REDUCE_YMM(%ymm0, %ymm14, %ymm13, %ymm15, %ymm1, %ymm2, %ymm3, %ymm4, %ymm5, %ymm6)
    vmovdqu %ymm1, (%rdi,%rcx)
    addq    $32, %rcx
    cmpq    $1024, %rcx
    jne     .L_inv_scale

    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_ntt_inverse_{{ARCH_LOWER}}_asm)


/* ============================================================================
 * Pointwise Montgomery multiply: c[i] = (a[i] * b[i]) * R^{-1} mod Q
 * void dap_dilithium_pointwise_mont_{{ARCH_LOWER}}_asm(
 *     int32_t *c, const int32_t *a, const int32_t *b);
 * ============================================================================ */
.globl dap_dilithium_pointwise_mont_{{ARCH_LOWER}}_asm
FUNC_TYPE(dap_dilithium_pointwise_mont_{{ARCH_LOWER}}_asm)
.p2align 4
dap_dilithium_pointwise_mont_{{ARCH_LOWER}}_asm:
    movl    $DIL_Q, %eax
    vmovd   %eax, %xmm15
    vpbroadcastd %xmm15, %ymm15

    movl    $0xFC7FDFFF, %eax
    vmovd   %eax, %xmm12
    vpbroadcastd %xmm12, %ymm12     /* QINV broadcast */

    xorl    %ecx, %ecx
    .p2align 4
.L_pw_loop:
    vmovdqu (%rsi,%rcx), %ymm0       /* a[i] */
    vmovdqu (%rdx,%rcx), %ymm1       /* b[i] */

    /* u = (a * b) * QINV mod 2^32 = a * (b * QINV) ... no, for pointwise
     * we don't have pre-computed. Use standard approach with QINV broadcast. */
    vpmulld  %ymm0, %ymm1, %ymm2     /* ab_lo = a * b (low 32) */
    vpmulld  %ymm2, %ymm12, %ymm3    /* u = ab_lo * QINV */
    vpmuldq  %ymm0, %ymm1, %ymm4     /* ab_even (signed 64) */
    vpmuludq %ymm3, %ymm15, %ymm5    /* uq_even */
    vpaddq   %ymm5, %ymm4, %ymm4
    vpsrlq   $32, %ymm4, %ymm4       /* result_even */
    vpsrlq   $32, %ymm0, %ymm6       /* a_odd */
    vpsrlq   $32, %ymm1, %ymm7       /* b_odd */
    vpsrlq   $32, %ymm3, %ymm8       /* u_odd */
    vpmuldq  %ymm6, %ymm7, %ymm9     /* ab_odd */
    vpmuludq %ymm8, %ymm15, %ymm8    /* uq_odd */
    vpaddq   %ymm8, %ymm9, %ymm9
    vpblendd $0xAA, %ymm9, %ymm4, %ymm4

    vmovdqu  %ymm4, (%rdi,%rcx)
    addq     $32, %rcx
    cmpq     $1024, %rcx
    jne      .L_pw_loop
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_pointwise_mont_{{ARCH_LOWER}}_asm)

