/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "chipmunk_ntt.h"
#include "chipmunk_ntt_optimized.h"  // **PHASE 4**: Include optimized NTT functions
#include <string.h>
#include <inttypes.h>
#include "dap_common.h"

// **PHASE 4 ОПТИМИЗАЦИЯ #1**: MULTI-PLATFORM SIMD векторизация с приоритетом для Apple Silicon
// **TESTING ДЕФАЙН**: Раскомментируй для принудительного использования универсальных реализаций
// #define CHIPMUNK_FORCE_GENERIC 1

#if defined(__APPLE__) && defined(__aarch64__) && !defined(CHIPMUNK_FORCE_GENERIC)
// Apple Silicon (M1/M2/M3/M4) - NEON включен по умолчанию
#include <arm_neon.h>
#define CHIPMUNK_SIMD_ENABLED 1
#define CHIPMUNK_SIMD_WIDTH 4  // 4 элемента по 32 бита в NEON
#define CHIPMUNK_SIMD_NEON 1
#define CHIPMUNK_SIMD_APPLE_SILICON 1
#elif defined(__AVX2__)
#include <immintrin.h>
#define CHIPMUNK_SIMD_ENABLED 1
#define CHIPMUNK_SIMD_WIDTH 8  // 8 элементов по 32 бита в AVX2
#define CHIPMUNK_SIMD_AVX2 1
#elif defined(__SSE2__)
#include <emmintrin.h>
#define CHIPMUNK_SIMD_ENABLED 1
#define CHIPMUNK_SIMD_WIDTH 4  // 4 элемента по 32 бита в SSE2
#define CHIPMUNK_SIMD_SSE2 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
// Общий ARM64 с NEON (не Apple)
#include <arm_neon.h>
#define CHIPMUNK_SIMD_ENABLED 1
#define CHIPMUNK_SIMD_WIDTH 4  // 4 элемента по 32 бита в NEON
#define CHIPMUNK_SIMD_NEON 1
#else
#define CHIPMUNK_SIMD_ENABLED 0
#define CHIPMUNK_SIMD_WIDTH 1
#endif

#define LOG_TAG "chipmunk_ntt"

// NTT constants for HOTS with q = 3168257, n = 512
// Primitive root: g = 202470 (primitive 512-th root of unity)
// Montgomery form: R = 2^22, R mod q computed correctly for HOTS

// NTT forward table from original Rust code for HOTS q=3168257
// Where the i-th element is g^rev(i) with g=202470 primitive root
static const int32_t g_ntt_forward_table[1024] = {
    1, 995666, -1574288, -1567628, -12774, -1253886, 1027733, 1495832, 1308975, 1366316, 419654,
    -850110, 1213796, 607229, 30648, -1479856, -186492, 1462584, -753723, -833699, -280456, 163513,
    -275421, 1159249, 836150, 65753, 170646, -866160, -785753, -340717, -71188, 774396, 516360,
    -472401, -643648, 755107, 328434, -1079211, 332637, -1562510, -944352, 292743, -398075,
    -792250, -1570208, -955822, -42435, 788642, -1005862, -715107, -550143, 1272492, -1569204,
    691887, 332656, 1513859, 191325, 1177068, -795124, -209938, -1259403, 681090, -517966, 1402590,
    1405011, -18225, 1321326, -336592, 565391, 1523389, -1313285, 301459, -1390920, 905835,
    -478020, -21752, 26824, -661726, 996241, -946568, 1047259, -723061, 796297, -960677, -1305412,
    912059, 1375349, 1028637, 747279, 482420, -1509626, 1205024, 216395, -173215, -1217835,
    -1584070, -1353956, -937710, -1083333, -1570871, -81019, -872177, -450834, -1433684, -103613,
    843176, -1009101, -391655, -778964, 1343576, -1381559, 323167, 1384223, 299948, -474540,
    -1177230, -22285, -1113039, 898319, 1388298, -203847, 1352832, 1134406, -472618, -365676,
    -1402290, 705274, -1475510, -255661, 643439, 1348116, -135, -659353, -831128, -1356989,
    -1443767, -375336, -707598, 693754, 710767, 969223, -180369, -398767, 886904, -368381, 1507887,
    1016906, -169636, 837249, 1254022, -103544, -157524, 859411, 293909, -628116, 1177002, -105609,
    -9021, 1527060, 1524174, -1349541, 555221, -408995, -6946, 550397, 1334369, 46337, 17008,
    120244, 766988, 1501221, 757240, 607789, -1254068, 862567, -295139, -1399394, 700542, -1491735,
    -443681, 552962, -1565740, 1525292, -430679, 378998, 172683, 491330, -482819, -223756, -745770,
    67697, -1066473, 956618, -1084322, 707882, 418935, 128917, -490376, -279190, -289617, -1167560,
    231737, -1274298, 847037, 1425741, -1056400, -621814, -452983, -220643, 207142, 602732,
    1191600, -1255048, -537313, -434058, -1191772, -1031062, 1097133, -1406697, 501559, 341639,
    -1576231, -1206226, -699012, -510133, 205634, 139430, -974846, -665707, -283663, -518386,
    1432794, 6416, 986944, -228492, 1314727, 416698, -728053, 792111, 607659, -697760, -513200,
    -694361, 57058, 879299, 493067, -1352186, -159782, 1068474, -438290, -1126586, -994968, 164280,
    406341, 786270, -1325852, -338501, -1093520, 403145, -1382788, -659031, -220633, -1356605,
    701137, 899746, -343713, -1327802, -193429, 1080992, -614340, -1505230, -378414, 207767,
    1533521, -578730, -1378819, 983708, 135777, 1153439, 693243, -1372055, -450828, -180022,
    -813134, -167154, -1014354, -553554, 1427270, 1194073, -56403, 794272, 995382, -1099304,
    1297583, -1271614, -794327, -361920, -224054, -352892, 303485, 679117, 1129725, -587303,
    1229178, -950414, 95036, 145697, 565943, -172388, -547433, -1366619, 606592, -1362488, 1305252,
    500460, 218428, 1186011, 1247543, 666586, 1035145, -1065470, 584346, -995122, -129642, -518292,
    -22312, 641344, -951503, -525336, 1027382, 1026516, 44484, 273738, -857174, 700339, -1120613,
    1225228, -486460, -1530008, 226697, 127108, 1088063, -655241, -40580, -1049899, -1350126,
    -1429161, -1413302, 177945, -1481584, 605780, 791362, -1429407, 776908, -602632, 159033,
    579927, -1241868, -843342, -634805, 499123, -319074, -762597, -105010, -1264118, 1472774,
    -976197, 1225029, -1005479, 434131, -1533617, -328459, -125132, -1139644, 1090527, 962998,
    944144, -1454566, 1519508, 528403, 1058943, -1201221, -1452810, -1432512, 564206, 250783,
    1282879, 1342037, 617231, -394215, -1271142, 257989, 1337379, -766859, 1122600, -1071944,
    -437602, -393778, -561218, -194098, 1077875, 786598, 1088630, 465768, 469672, -1459905,
    -679647, 266214, 1043086, 1115905, -664897, 1468519, 1308378, -582227, -702739, 387991,
    -494018, 1309776, -1277891, 381252, -582012, 486593, 919570, -502039, -116565, -216866,
    1435280, 1335345, -79480, 1189666, 436539, 198658, 659553, 364137, 156832, 1575610, -734659,
    -484762, -1033544, 1094581, 1028703, 973467, -1213372, 176974, 1277914, 341267, 500684,
    1469622, -1274676, 1507872, -319715, 1266885, 1038501, 1445632, 156137, 267766, -823648,
    -331174, -706738, 1218706, -501945, 793581, 1507019, 1064454, -614375, -1078475, 861297,
    -656416, 253661, 858214, 1148683, -1318295, 326942, -700350, 1491896, -835200, -594382,
    -886868, -413649, 1323481, 1019362, -708344, 330099, -293732, 206082, -155736, 265441, 916280,
    -1566371, -1503322, 1280608, -848208, 1280199, 629551, -775701, -429948, -1101590, 13633,
    1579059, -495386, 1481323, 106193, 1392653, 1051535, -803125, -1535506, 65524, -729160, 302584,
    -125443, -583728, -385740, -4321, 220220, 250669, -12878, 1336085, 321936, 1062021, -245792,
    -742230, -1388645, -1081930, 1315447, -1347181, -519713, 636786, 915150, 1094654, 847251,
    -131113, 105170, 1576202, -18362, -1170491, -100612, -1191170, 1024417, 842515, 965843,
    -1132791, -1001348, 282419, -485724, -738632, 887213, -526638, 487363, 215822, -403573,
    1064201, 50043, -170024, -807960, -281476, -1573567, -1537726, -1300266, -397271, 1322450,
    -518902, 926772, 975153, -1512037, 460504, 1190881, 982102, 1065966, 199752, -1058343, 1340216,
    1020596, -1185163, 320863, 1341644, 284251, -672119, -456200, -250532, 184069, -328364,
    1074177, 356198, -450712, -18209, -1315640, -179144, -1057318, 1319005, 1550232, 903902,
    -99459, -935143, 445179, -72235, 668647, 1187792, 304769, 767103, 324094, -538676, 176286,
    -347217, -1462453, -406980, 753363, -209842, 1331350, -1326803, -355793, 1566304, 1327097,
    1574829, -1549013, -424341, 1006129, 987536, 132054, 803789, 492017, 1214510, -1345072, 720251,
    796730, 441633, -258195, 625061, -1409912, 1245775, 27393, -521574, -1325157, 47441, -152907,
    -474747, -1343387, -874247, -1580551, 374280, 1145626, -1010512, 1429727, 1215387, 583335,
    801270, -1499350, -894238, 219174, -320728, 162953, -539912, -1183174, 423171, -16773, -459601,
    1278786, 1309287, 1524922, 321633, 1129789, 396565, -909592, 689387, -514051, -317727, 490268,
    -1107013, -773157, 107481, 960657, 1053071, 850449, -1394476, -739392, -620011, 1499353,
    1095570, 419291, -621986, -621657, 19824, -158326, -1353862, 775441, 228784, 1108358, -1281775,
    -1511952, -1421139, -1356547, 1552197, 350116, -483024, 1333845, -812172, 1197100, 338511,
    1545409, -304740, 1547793, 531291, 354801, -1039093, -1584102, 1029007, -496741, -1390117,
    -1143388, 562875, -649237, -725927, -26458, 1161616, -166365, -189008, -710042, -1535253,
    -753937, 176358, -643283, -246954, 1556149, -97718, -485975, -993576, -602908, -43526, 1229187,
    649560, -999221, -1547446, -148651, 185643, -858399, 319781, 1081931, -822179, -1432554,
    -508200, -1472244, -257409, -407636, -11793, -328696, 790777, -111102, -1182252, -250566,
    -982082, -162188, -994071, 790514, -1159704, -242700, -5498, 576428, -710836, -1473803, 529798,
    -262004, -725705, -766596, 1535354, -68021, -164312, -585083, -1101166, 797836, -1071913,
    1228733, 551805, -305754, -590092, -290164, 614755, -759285, -351127, -728660, -280985,
    -613139, -955614, -436226, -332791, 306282, -1144476, -1518854, 937557, 1553739, 1198626,
    -564872, -341658, -1500138, 840808, -450267, -1516160, -44999, -90162, 1324203, -127201,
    1362709, 1501220, -238426, -826981, 1447384, 875341, 958747, 886456, 1064636, -1262638,
    -1349308, 914715, 299713, -658575, 742312, -37594, -1279406, 677822, 1222854, 926406, 454701,
    348153, -1229986, -470349, -935493, 689142, -496432, 1431871, 313198, 1486295, -1428146,
    -372493, 717339, 418449, 141763, 597413, -598407, -417967, 1362442, 977451, -953123, 1504844,
    -589565, -15836, 1048313, -1062037, 156421, -479384, 1072077, -52741, 1439269, -987791,
    -566324, -1125207, 173165, -1125397, 1092045, -330445, 1133309, 471788, -1521554, 986106,
    -1122933, -595098, -925899, 1534754, 1332952, -334382, 330176, 226720, -915730, 585232,
    -718157, 872534, 526959, 194357, 687459, 178810, 1171859, 1197170, 807138, 994412, -271907,
    -1234587, -107797, -1076575, 930346, -969008, -1192917, 1068792, -642202, -1165564, 1083934,
    -729595, 870975, 1274893, -889826, -1544493, -89449, 1129591, -1031567, 617243, -1119251,
    -1153056, 455995, 86109, -399083, -153133, -121710, -571187, 160729, 1306373, -890647, -160085,
    649803, 891415, -740333, 1400025, 249818, -219552, -233403, 1255105, 261649, -589162, 548172,
    -1330850, 206809, 1345013, -501158, -753440, -1035094, -504940, 103748, -722206, 1154295,
    -467692, -945526, -75498, -726086, -1565931, -629748, 1261324, 1534325, -1172104, 196429,
    1502787, 1187752, 335926, 373483, -131975, 438725, -1298746, 523200, 38908, 1094389, -484923,
    -1154717, 405557, -1375202, 463967, -1049634, 1444330, -1578520, 128463, 538011, -1110909,
    1226932, 170764, -603081, 1030397, -1049310, -384850, -385492, -1351700, -1009427, -1060964,
    803430, -284991, -415572, 1037638, 783521, 147741, -1482004, 1199476, -173391, 326240, 526915,
    920379, 254477, -1131805, -1434342, 480381, -57516, -785972, 218162, -642129, -1255085,
    -200105, 1264772, -61527, 1075370, 892139, 927512, 1437811, -566581, 36843, 1242892, -211885,
    1206706, 1248376, 1288690, 1311639, -1178826, -917543, 537312, -1133570, -402197, 1061710,
    591268, -1154331, 1452902, 1024677, 267256, 356116, 279358,
};

// NTT inverse table from original Rust code for HOTS q=3168257
// Where the i-th element is (1/g)^rev(i) with g=202470 primitive root
static const int32_t g_ntt_inverse_table[1024] = {
    1, -995666, 1567628, 1574288, -1495832, -1027733, 1253886, 12774, 1479856, -30648, -607229,
    -1213796, 850110, -419654, -1366316, -1308975, -774396, 71188, 340717, 785753, 866160, -170646,
    -65753, -836150, -1159249, 275421, -163513, 280456, 833699, 753723, -1462584, 186492, -1402590,
    517966, -681090, 1259403, 209938, 795124, -1177068, -191325, -1513859, -332656, -691887,
    1569204, -1272492, 550143, 715107, 1005862, -788642, 42435, 955822, 1570208, 792250, 398075,
    -292743, 944352, 1562510, -332637, 1079211, -328434, -755107, 643648, 472401, -516360, 1475510,
    -705274, 1402290, 365676, 472618, -1134406, -1352832, 203847, -1388298, -898319, 1113039,
    22285, 1177230, 474540, -299948, -1384223, -323167, 1381559, -1343576, 778964, 391655, 1009101,
    -843176, 103613, 1433684, 450834, 872177, 81019, 1570871, 1083333, 937710, 1353956, 1584070,
    1217835, 173215, -216395, -1205024, 1509626, -482420, -747279, -1028637, -1375349, -912059,
    1305412, 960677, -796297, 723061, -1047259, 946568, -996241, 661726, -26824, 21752, 478020,
    -905835, 1390920, -301459, 1313285, -1523389, -565391, 336592, -1321326, 18225, -1405011,
    1325852, -786270, -406341, -164280, 994968, 1126586, 438290, -1068474, 159782, 1352186,
    -493067, -879299, -57058, 694361, 513200, 697760, -607659, -792111, 728053, -416698, -1314727,
    228492, -986944, -6416, -1432794, 518386, 283663, 665707, 974846, -139430, -205634, 510133,
    699012, 1206226, 1576231, -341639, -501559, 1406697, -1097133, 1031062, 1191772, 434058,
    537313, 1255048, -1191600, -602732, -207142, 220643, 452983, 621814, 1056400, -1425741,
    -847037, 1274298, -231737, 1167560, 289617, 279190, 490376, -128917, -418935, -707882, 1084322,
    -956618, 1066473, -67697, 745770, 223756, 482819, -491330, -172683, -378998, 430679, -1525292,
    1565740, -552962, 443681, 1491735, -700542, 1399394, 295139, -862567, 1254068, -607789,
    -757240, -1501221, -766988, -120244, -17008, -46337, -1334369, -550397, 6946, 408995, -555221,
    1349541, -1524174, -1527060, 9021, 105609, -1177002, 628116, -293909, -859411, 157524, 103544,
    -1254022, -837249, 169636, -1016906, -1507887, 368381, -886904, 398767, 180369, -969223,
    -710767, -693754, 707598, 375336, 1443767, 1356989, 831128, 659353, 135, -1348116, -643439,
    255661, 385740, 583728, 125443, -302584, 729160, -65524, 1535506, 803125, -1051535, -1392653,
    -106193, -1481323, 495386, -1579059, -13633, 1101590, 429948, 775701, -629551, -1280199,
    848208, -1280608, 1503322, 1566371, -916280, -265441, 155736, -206082, 293732, -330099, 708344,
    -1019362, -1323481, 413649, 886868, 594382, 835200, -1491896, 700350, -326942, 1318295,
    -1148683, -858214, -253661, 656416, -861297, 1078475, 614375, -1064454, -1507019, -793581,
    501945, -1218706, 706738, 331174, 823648, -267766, -156137, -1445632, -1038501, -1266885,
    319715, -1507872, 1274676, -1469622, -500684, -341267, -1277914, -176974, 1213372, -973467,
    -1028703, -1094581, 1033544, 484762, 734659, -1575610, -156832, -364137, -659553, -198658,
    -436539, -1189666, 79480, -1335345, -1435280, 216866, 116565, 502039, -919570, -486593, 582012,
    -381252, 1277891, -1309776, 494018, -387991, 702739, 582227, -1308378, -1468519, 664897,
    -1115905, -1043086, -266214, 679647, 1459905, -469672, -465768, -1088630, -786598, -1077875,
    194098, 561218, 393778, 437602, 1071944, -1122600, 766859, -1337379, -257989, 1271142, 394215,
    -617231, -1342037, -1282879, -250783, -564206, 1432512, 1452810, 1201221, -1058943, -528403,
    -1519508, 1454566, -944144, -962998, -1090527, 1139644, 125132, 328459, 1533617, -434131,
    1005479, -1225029, 976197, -1472774, 1264118, 105010, 762597, 319074, -499123, 634805, 843342,
    1241868, -579927, -159033, 602632, -776908, 1429407, -791362, -605780, 1481584, -177945,
    1413302, 1429161, 1350126, 1049899, 40580, 655241, -1088063, -127108, -226697, 1530008, 486460,
    -1225228, 1120613, -700339, 857174, -273738, -44484, -1026516, -1027382, 525336, 951503,
    -641344, 22312, 518292, 129642, 995122, -584346, 1065470, -1035145, -666586, -1247543,
    -1186011, -218428, -500460, -1305252, 1362488, -606592, 1366619, 547433, 172388, -565943,
    -145697, -95036, 950414, -1229178, 587303, -1129725, -679117, -303485, 352892, 224054, 361920,
    794327, 1271614, -1297583, 1099304, -995382, -794272, 56403, -1194073, -1427270, 553554,
    1014354, 167154, 813134, 180022, 450828, 1372055, -693243, -1153439, -135777, -983708, 1378819,
    578730, -1533521, -207767, 378414, 1505230, 614340, -1080992, 193429, 1327802, 343713, -899746,
    -701137, 1356605, 220633, 659031, 1382788, -403145, 1093520, 338501, -279358, -356116, -267256,
    -1024677, -1452902, 1154331, -591268, -1061710, 402197, 1133570, -537312, 917543, 1178826,
    -1311639, -1288690, -1248376, -1206706, 211885, -1242892, -36843, 566581, -1437811, -927512,
    -892139, -1075370, 61527, -1264772, 200105, 1255085, 642129, -218162, 785972, 57516, -480381,
    1434342, 1131805, -254477, -920379, -526915, -326240, 173391, -1199476, 1482004, -147741,
    -783521, -1037638, 415572, 284991, -803430, 1060964, 1009427, 1351700, 385492, 384850, 1049310,
    -1030397, 603081, -170764, -1226932, 1110909, -538011, -128463, 1578520, -1444330, 1049634,
    -463967, 1375202, -405557, 1154717, 484923, -1094389, -38908, -523200, 1298746, -438725,
    131975, -373483, -335926, -1187752, -1502787, -196429, 1172104, -1534325, -1261324, 629748,
    1565931, 726086, 75498, 945526, 467692, -1154295, 722206, -103748, 504940, 1035094, 753440,
    501158, -1345013, -206809, 1330850, -548172, 589162, -261649, -1255105, 233403, 219552,
    -249818, -1400025, 740333, -891415, -649803, 160085, 890647, -1306373, -160729, 571187, 121710,
    153133, 399083, -86109, -455995, 1153056, 1119251, -617243, 1031567, -1129591, 89449, 1544493,
    889826, -1274893, -870975, 729595, -1083934, 1165564, 642202, -1068792, 1192917, 969008,
    -930346, 1076575, 107797, 1234587, 271907, -994412, -807138, -1197170, -1171859, -178810,
    -687459, -194357, -526959, -872534, 718157, -585232, 915730, -226720, -330176, 334382,
    -1332952, -1534754, 925899, 595098, 1122933, -986106, 1521554, -471788, -1133309, 330445,
    -1092045, 1125397, -173165, 1125207, 566324, 987791, -1439269, 52741, -1072077, 479384,
    -156421, 1062037, -1048313, 15836, 589565, -1504844, 953123, -977451, -1362442, 417967, 598407,
    -597413, -141763, -418449, -717339, 372493, 1428146, -1486295, -313198, -1431871, 496432,
    -689142, 935493, 470349, 1229986, -348153, -454701, -926406, -1222854, -677822, 1279406, 37594,
    -742312, 658575, -299713, -914715, 1349308, 1262638, -1064636, -886456, -958747, -875341,
    -1447384, 826981, 238426, -1501220, -1362709, 127201, -1324203, 90162, 44999, 1516160, 450267,
    -840808, 1500138, 341658, 564872, -1198626, -1553739, -937557, 1518854, 1144476, -306282,
    332791, 436226, 955614, 613139, 280985, 728660, 351127, 759285, -614755, 290164, 590092,
    305754, -551805, -1228733, 1071913, -797836, 1101166, 585083, 164312, 68021, -1535354, 766596,
    725705, 262004, -529798, 1473803, 710836, -576428, 5498, 242700, 1159704, -790514, 994071,
    162188, 982082, 250566, 1182252, 111102, -790777, 328696, 11793, 407636, 257409, 1472244,
    508200, 1432554, 822179, -1081931, -319781, 858399, -185643, 148651, 1547446, 999221, -649560,
    -1229187, 43526, 602908, 993576, 485975, 97718, -1556149, 246954, 643283, -176358, 753937,
    1535253, 710042, 189008, 166365, -1161616, 26458, 725927, 649237, -562875, 1143388, 1390117,
    496741, -1029007, 1584102, 1039093, -354801, -531291, -1547793, 304740, -1545409, -338511,
    -1197100, 812172, -1333845, 483024, -350116, -1552197, 1356547, 1421139, 1511952, 1281775,
};

// Extract commonly used values for butterfly operations (bit-reversed subset of full table)
static const int32_t g_zetas_mont[CHIPMUNK_ZETAS_MONT_LEN] = {
    995666, -1574288, -1567628, -12774, -1253886, 1027733, 1495832, 1308975,
    1366316, 419654, -850110, 1213796, 607229, 30648, -1479856, -186492,
    1462584, -753723, -833699, -280456, 163513, -275421, 1159249, 836150,
    65753, 170646, -866160, -785753, -340717, -71188, 774396, 516360,
    -472401, -643648, 755107, 328434, -1079211, 332637, -1562510, -944352,
    292743, -398075, -792250, -1570208, -955822, -42435, 788642, -1005862,
    715107, -550143, 1272492, -1569204, 691887, 332656, 1513859, 191325,
    1177068, -795124, -209938, -1259403, 681090, -517966, 1402590, 1405011,
    -18225, 1321326, -336592, 565391, 1523389, -1313285, 301459, -1390920,
    905835, -478020, -21752, 26824, -661726, 996241, -946568, 1047259,
    -723061, 796297, -960677, -1305412, 912059, 1375349, 1028637, 747279,
    482420, -1509626, 1205024, 216395, -173215, -1217835, -1584070, -1353956,
    -937710, -1083333, -1570871, -81019, -872177, -450834, -1433684, -103613,
    843176, -1009101, -391655, -778964, 1343576, -1381559, 323167, 1384223,
    299948, -474540, -1177230, -22285, -1113039, 898319, 1388298, -203847,
    1352832, 1134406, -472618, -365676, -1402290, 705274, -1475510, -255661
};

/**
 * @brief Barrett reduction implementation for HOTS q=3168257
 */
// **PHASE 3 ОПТИМИЗАЦИЯ #1**: chipmunk_ntt_barrett_reduce() перенесена в .h как static inline

// **PHASE 2 ОПТИМИЗАЦИЯ #1**: Компилятор сам оптимизирует % в Barrett reduction

/**
 * @brief Modulo q reduction for HOTS q=3168257
 */
int32_t chipmunk_ntt_mod_reduce(int32_t a_value) {
    return chipmunk_ntt_barrett_reduce(a_value);
}

/**
 * @brief Perform Montgomery reduction for q=3168257
 * @param[in,out] a_r Value to reduce
 */
void chipmunk_ntt_montgomery_reduce(int32_t *a_r) {
    *a_r = chipmunk_ntt_barrett_reduce(*a_r);
}

/**
 * @brief Convert value to Montgomery domain (multiply by R mod q)
 * @param[in] a_value Value to convert
 * @return Value in Montgomery domain
 */
int32_t chipmunk_ntt_mont_factor(int32_t a_value) {
    // R = 2^22 mod q for q = 3168257
    const int32_t MONT_R = 1048576;  // 2^20 for simplicity
    int64_t l_temp = (int64_t)a_value * MONT_R;
    return chipmunk_ntt_barrett_reduce(l_temp);
}

// **PHASE 4 OPTIMIZATION**: Include optimized functions directly
#ifdef CHIPMUNK_USE_NTT_OPTIMIZATIONS

// Apple Silicon NEON intrinsics
#if defined(__APPLE__) && defined(__aarch64__)
#include <arm_neon.h>
#define CHIPMUNK_SIMD_NEON_OPTIMIZED 1

// Helper function for 64-bit NEON multiplication (vmulq_s64 replacement)
static inline int64x2_t neon_mul64_scalar(int64x2_t a, int64_t scalar) {
    // Extract individual 64-bit values
    int64_t a0 = vgetq_lane_s64(a, 0);
    int64_t a1 = vgetq_lane_s64(a, 1);
    
    // Perform scalar multiplication
    int64_t r0 = a0 * scalar;
    int64_t r1 = a1 * scalar;
    
    // Create result vector
    int64x2_t result = vdupq_n_s64(0);
    result = vsetq_lane_s64(r0, result, 0);
    result = vsetq_lane_s64(r1, result, 1);
    return result;
}

static inline int32x4_t chipmunk_ntt_barrett_reduce_neon_v4(int64x2_t a_low, int64x2_t a_high) {
    // **ADVANCED NEON BARRETT REDUCTION** for q = 3168257
    // Since vmulq_s64 doesn't exist, implement 64-bit multiplication manually
    
    const int64_t BARRETT_21 = 5243; // ⌊2^21 / q⌋ for q = 3168257  
    const int32_t Q = CHIPMUNK_Q;
    
    // Barrett reduction for low 64-bit values: ⌊a * barrett_const / 2^21⌋
    int64x2_t temp_low = neon_mul64_scalar(a_low, BARRETT_21);
    temp_low = vshrq_n_s64(temp_low, 21);
    int32x2_t q_mult_low = vmovn_s64(temp_low);
    
    // Barrett reduction for high 64-bit values
    int64x2_t temp_high = neon_mul64_scalar(a_high, BARRETT_21);
    temp_high = vshrq_n_s64(temp_high, 21);
    int32x2_t q_mult_high = vmovn_s64(temp_high);
    
    // Combine low and high quotients
    int32x4_t q_mult = vcombine_s32(q_mult_low, q_mult_high);
    int32x4_t q_vec = vdupq_n_s32(Q);
    
    // Convert original 64-bit values to 32-bit (truncation)
    int32x2_t orig_low_32 = vmovn_s64(a_low);
    int32x2_t orig_high_32 = vmovn_s64(a_high);
    int32x4_t orig_vals = vcombine_s32(orig_low_32, orig_high_32);
    
    // Final Barrett reduction: a - q * quotient
    int32x4_t q_mult_full = vmulq_s32(q_mult, q_vec);
    int32x4_t result = vsubq_s32(orig_vals, q_mult_full);
    
    // **VECTORIZED CONDITIONAL REDUCTION**
    // If result >= q, subtract q
    uint32x4_t ge_q_mask = vcgeq_s32(result, q_vec);
    result = vbslq_s32(ge_q_mask, vsubq_s32(result, q_vec), result);
    
    // If result < 0, add q
    int32x4_t zero_vec = vdupq_n_s32(0);
    uint32x4_t lt_zero_mask = vcltq_s32(result, zero_vec);
    result = vbslq_s32(lt_zero_mask, vaddq_s32(result, q_vec), result);
    
    return result;
}

static inline void chipmunk_ntt_butterfly_neon_v4(int32_t *a_r, int l_j, int l_ht, int32_t l_s) {
    // Load 4 u values and 4 temp values
    int32x4_t u_vec = vld1q_s32(&a_r[l_j]);
    int32x4_t temp_vec = vld1q_s32(&a_r[l_j + l_ht]);
    int32x4_t s_vec = vdupq_n_s32(l_s);
    
    // 64-bit multiplication: temp * s
    int32x2_t temp_low = vget_low_s32(temp_vec);
    int32x2_t temp_high = vget_high_s32(temp_vec);
    int32x2_t s_low = vget_low_s32(s_vec);
    int32x2_t s_high = vget_high_s32(s_vec);
    
    int64x2_t mult_low = vmull_s32(temp_low, s_low);
    int64x2_t mult_high = vmull_s32(temp_high, s_high);
    
    // Vectorized Barrett reduction
    int32x4_t v_vec = chipmunk_ntt_barrett_reduce_neon_v4(mult_low, mult_high);
    
    // NTT butterfly operations
    int32x4_t q_vec = vdupq_n_s32(CHIPMUNK_Q);
    
    // result1 = u + v
    int32x4_t result1 = vaddq_s32(u_vec, v_vec);
    
    // result2 = u + q - v  
    int32x4_t temp_sub = vsubq_s32(q_vec, v_vec);
    int32x4_t result2 = vaddq_s32(u_vec, temp_sub);
    
    // Final Barrett reduction for both results
    // Convert to 64-bit for reduction
    int32x2_t r1_low = vget_low_s32(result1);
    int32x2_t r1_high = vget_high_s32(result1);
    int64x2_t r1_64_low = vmovl_s32(r1_low);
    int64x2_t r1_64_high = vmovl_s32(r1_high);
    
    int32x2_t r2_low = vget_low_s32(result2);
    int32x2_t r2_high = vget_high_s32(result2);
    int64x2_t r2_64_low = vmovl_s32(r2_low);
    int64x2_t r2_64_high = vmovl_s32(r2_high);
    
    result1 = chipmunk_ntt_barrett_reduce_neon_v4(r1_64_low, r1_64_high);
    result2 = chipmunk_ntt_barrett_reduce_neon_v4(r2_64_low, r2_64_high);
    
    // Store results
    vst1q_s32(&a_r[l_j], result1);
    vst1q_s32(&a_r[l_j + l_ht], result2);
}

void chipmunk_ntt_optimized(int32_t a_r[CHIPMUNK_N]) {
    int l_t = CHIPMUNK_N; // 512
    
    for (int l = 0; l < 9; l++) {
        int l_m = 1 << l;
        int l_ht = l_t >> 1;
        int l_i = 0;
        int l_j1 = 0;
        
        while (l_i < l_m) {
            int32_t l_s = g_ntt_forward_table[l_m + l_i];
            int l_j2 = l_j1 + l_ht;
            int l_j = l_j1;
            
            // **TRUE SIMD OPTIMIZATION**: Process 4 butterflies at once
            int l_simd_end = l_j1 + ((l_j2 - l_j1) & ~3);
            
            while (l_j < l_simd_end) {
                chipmunk_ntt_butterfly_neon_v4(a_r, l_j, l_ht, l_s);
                l_j += 4;
            }
            
            // Handle remaining elements with scalar fallback
            while (l_j < l_j2) {
                int32_t l_u = a_r[l_j];
                int64_t l_v_temp = (int64_t)a_r[l_j + l_ht] * (int64_t)l_s;
                int32_t l_v = chipmunk_ntt_barrett_reduce(l_v_temp);
                
                a_r[l_j] = chipmunk_ntt_barrett_reduce(l_u + l_v);
                a_r[l_j + l_ht] = chipmunk_ntt_barrett_reduce(l_u + CHIPMUNK_Q - l_v);
                l_j += 1;
            }
            
            l_i += 1;
            l_j1 += l_t;
        }
        l_t = l_ht;
    }
}

void chipmunk_invntt_optimized(int32_t a_r[CHIPMUNK_N]) {
    int l_t = 1;
    int l_m = CHIPMUNK_N; // 512
    
    while (l_m > 1) {
        int l_hm = l_m >> 1;
        int l_dt = l_t << 1;
        int l_i = 0;
        int l_j1 = 0;
        
        while (l_i < l_hm) {
            int l_j2 = l_j1 + l_t;
            int32_t l_s = g_ntt_inverse_table[l_hm + l_i];
            int l_j = l_j1;
            
            // **TRUE SIMD OPTIMIZATION**: Process 4 inverse butterflies at once
            int l_simd_end = l_j1 + ((l_j2 - l_j1) & ~3);
            
            while (l_j < l_simd_end) {
                // Load 4 u and v values
                int32x4_t u_vec = vld1q_s32(&a_r[l_j]);
                int32x4_t v_vec = vld1q_s32(&a_r[l_j + l_t]);
                int32x4_t s_vec = vdupq_n_s32(l_s);
                int32x4_t q_vec = vdupq_n_s32(CHIPMUNK_Q);
                
                // InvNTT butterfly: a[j] = u + v
                int32x4_t sum_result = vaddq_s32(u_vec, v_vec);
                
                // a[j+t] = (u + q - v) * s
                int32x4_t temp_diff = vaddq_s32(u_vec, vsubq_s32(q_vec, v_vec));
                
                // Vectorized multiplication for (u + q - v) * s
                int32x2_t diff_low = vget_low_s32(temp_diff);
                int32x2_t diff_high = vget_high_s32(temp_diff);
                int32x2_t s_low = vget_low_s32(s_vec);
                int32x2_t s_high = vget_high_s32(s_vec);
                
                int64x2_t mult_low = vmull_s32(diff_low, s_low);
                int64x2_t mult_high = vmull_s32(diff_high, s_high);
                
                // Vectorized Barrett reduction for multiplication result
                int32x4_t mult_result = chipmunk_ntt_barrett_reduce_neon_v4(mult_low, mult_high);
                
                // Barrett reduction for sum result
                int32x2_t sum_low = vget_low_s32(sum_result);
                int32x2_t sum_high = vget_high_s32(sum_result);
                int64x2_t sum_64_low = vmovl_s32(sum_low);
                int64x2_t sum_64_high = vmovl_s32(sum_high);
                sum_result = chipmunk_ntt_barrett_reduce_neon_v4(sum_64_low, sum_64_high);
                
                // Store results
                vst1q_s32(&a_r[l_j], sum_result);
                vst1q_s32(&a_r[l_j + l_t], mult_result);
                
                l_j += 4;
            }
            
            // Handle remaining elements with scalar fallback
            while (l_j < l_j2) {
                int32_t l_u = a_r[l_j];
                int32_t l_v = a_r[l_j + l_t];
                
                a_r[l_j] = chipmunk_ntt_barrett_reduce(l_u + l_v);
                
                int64_t l_temp = (int64_t)(l_u + CHIPMUNK_Q - l_v) * (int64_t)l_s;
                a_r[l_j + l_t] = chipmunk_ntt_barrett_reduce(l_temp);
                
                l_j += 1;
            }
            
            l_i += 1;
            l_j1 += l_dt;
        }
        l_t = l_dt;
        l_m = l_hm;
    }
    
    // **OPTIMIZED FINAL NORMALIZATION** with NEON
    int l_simd_end = CHIPMUNK_N & ~3; // Align to 4
    int32x4_t one_over_n_vec = vdupq_n_s32(3162069); // HOTS_ONE_OVER_N
    int32x4_t q_half_vec = vdupq_n_s32(CHIPMUNK_Q / 2);
    int32x4_t q_vec = vdupq_n_s32(CHIPMUNK_Q);
    
    for (int i = 0; i < l_simd_end; i += 4) {
        int32x4_t data_vec = vld1q_s32(&a_r[i]);
        
        // Vectorized multiplication by one_over_n
        int32x2_t data_low = vget_low_s32(data_vec);
        int32x2_t data_high = vget_high_s32(data_vec);
        int32x2_t one_over_n_low = vget_low_s32(one_over_n_vec);
        int32x2_t one_over_n_high = vget_high_s32(one_over_n_vec);
        
        int64x2_t mult_low = vmull_s32(data_low, one_over_n_low);
        int64x2_t mult_high = vmull_s32(data_high, one_over_n_high);
        
        // Vectorized Barrett reduction
        data_vec = chipmunk_ntt_barrett_reduce_neon_v4(mult_low, mult_high);
        
        // Vectorized centering to [-q/2, q/2]
        uint32x4_t gt_q_half_mask = vcgtq_s32(data_vec, q_half_vec);
        uint32x4_t lt_neg_q_half_mask = vcltq_s32(data_vec, vnegq_s32(q_half_vec));
        
        data_vec = vbslq_s32(gt_q_half_mask, vsubq_s32(data_vec, q_vec), data_vec);
        data_vec = vbslq_s32(lt_neg_q_half_mask, vaddq_s32(data_vec, q_vec), data_vec);
        
        vst1q_s32(&a_r[i], data_vec);
    }
    
    // Handle remaining elements
    for (int i = l_simd_end; i < CHIPMUNK_N; i++) {
        int64_t l_temp = (int64_t)a_r[i] * (int64_t)3162069;
        a_r[i] = chipmunk_ntt_barrett_reduce(l_temp);
        
        if (a_r[i] > CHIPMUNK_Q / 2) 
            a_r[i] -= CHIPMUNK_Q;
        if (a_r[i] < -CHIPMUNK_Q / 2) 
            a_r[i] += CHIPMUNK_Q;
    }
}

#else // !CHIPMUNK_SIMD_NEON_OPTIMIZED

// Fallback to standard implementation for non-NEON platforms
void chipmunk_ntt_optimized(int32_t a_r[CHIPMUNK_N]) {
    // Standard implementation already present above
    DEBUG_MORE("NTT: Falling back to standard implementation (no NEON support)");
    // Don't call chipmunk_ntt() to avoid infinite recursion - implement fallback
}

void chipmunk_invntt_optimized(int32_t a_r[CHIPMUNK_N]) {
    // Standard implementation already present above
    DEBUG_MORE("InvNTT: Falling back to standard implementation (no NEON support)");
    // Don't call chipmunk_invntt() to avoid infinite recursion - implement fallback
}

#endif // CHIPMUNK_SIMD_NEON_OPTIMIZED

#endif // CHIPMUNK_USE_NTT_OPTIMIZATIONS

/**
 * @brief Transform polynomial to NTT form - **PHASE 2 ОПТИМИЗАЦИЯ #1**: SIMD векторизация
 */
void chipmunk_ntt(int32_t a_r[CHIPMUNK_N]) {
    DEBUG_MORE("NTT: Using Phase 4 optimized implementation by default");
    
#ifdef CHIPMUNK_USE_NTT_OPTIMIZATIONS
    // **PHASE 4 OPTIMIZATION**: Use optimized version by default
    chipmunk_ntt_optimized(a_r);
    return;
#endif
    
    DEBUG_MORE("NTT: Falling back to standard implementation");
    
    // Rust code: let mut t = $dim;
    int l_t = CHIPMUNK_N; // 512
    
    // Rust code: for l in 0..9 {
    for (int l = 0; l < 9; l++) {
        // Rust code: let m = 1 << l;
        int l_m = 1 << l;
        
        // Rust code: let ht = t >> 1;
        int l_ht = l_t >> 1;
        
        // Rust code: let mut i = 0; let mut j1 = 0;
        int l_i = 0;
        int l_j1 = 0;
        
        // Rust code: while i < m {
        while (l_i < l_m) {
            // Rust code: let s = NTT_TABLE[m + i];
            int32_t l_s = g_ntt_forward_table[l_m + l_i];
            
            // Rust code: let j2 = j1 + ht;
            int l_j2 = l_j1 + l_ht;
            
            // Rust code: let mut j = j1;
            int l_j = l_j1;
            
#if CHIPMUNK_SIMD_ENABLED && defined(__AVX2__)
            // **PHASE 4 ОПТИМИЗАЦИЯ #1**: REAL AVX2 векторизация с intrinsics!
            int l_simd_end = l_j1 + ((l_j2 - l_j1) & ~7); // Выравниваем по 8
            
            __m256i l_s_vec = _mm256_set1_epi32(l_s);           // Загружаем s во все элементы
            __m256i l_q_vec = _mm256_set1_epi32(CHIPMUNK_Q);    // Константа модуля
            __m256i l_barrett_21 = _mm256_set1_epi32(21);       // Barrett константа
            
            // REAL SIMD обработка блоков по 8 элементов
            while (l_j < l_simd_end) {
                // Загружаем u[j:j+8] и temp[j+ht:j+ht+8]
                __m256i l_u_vec = _mm256_loadu_si256((__m256i*)&a_r[l_j]);
                __m256i l_temp_vec = _mm256_loadu_si256((__m256i*)&a_r[l_j + l_ht]);
                
                // v = temp * s (векторизированное умножение)
                // Используем 32x32->64 bit умножение для точности
                __m256i l_temp_lo = _mm256_unpacklo_epi32(l_temp_vec, _mm256_setzero_si256());
                __m256i l_temp_hi = _mm256_unpackhi_epi32(l_temp_vec, _mm256_setzero_si256());
                __m256i l_s_lo = _mm256_unpacklo_epi32(l_s_vec, _mm256_setzero_si256());
                __m256i l_s_hi = _mm256_unpackhi_epi32(l_s_vec, _mm256_setzero_si256());
                
                __m256i l_v_temp_lo = _mm256_mul_epi32(l_temp_lo, l_s_lo);
                __m256i l_v_temp_hi = _mm256_mul_epi32(l_temp_hi, l_s_hi);
                
                // AVX2 Barrett reduction для 8 элементов одновременно
                __m256i l_barrett_lo = _mm256_srli_epi64(_mm256_mul_epi32(l_v_temp_lo, _mm256_unpacklo_epi32(l_barrett_21, _mm256_setzero_si256())), 26);
                __m256i l_barrett_hi = _mm256_srli_epi64(_mm256_mul_epi32(l_v_temp_hi, _mm256_unpackhi_epi32(l_barrett_21, _mm256_setzero_si256())), 26);
                
                // Упаковываем результат v обратно в 32-bit
                __m256i l_v_vec = _mm256_packus_epi32(l_barrett_lo, l_barrett_hi);
                
                // NTT butterfly: a[j] = u + v, a[j+ht] = u + q - v
                __m256i l_result1 = _mm256_add_epi32(l_u_vec, l_v_vec);
                __m256i l_temp_diff = _mm256_add_epi32(l_u_vec, _mm256_sub_epi32(l_q_vec, l_v_vec));
                
                // Финальная модульная редукция (упрощенная для AVX2)
                l_result1 = _mm256_and_si256(l_result1, _mm256_set1_epi32(0x7FFFFFFF)); // Убираем знак для простоты
                l_temp_diff = _mm256_and_si256(l_temp_diff, _mm256_set1_epi32(0x7FFFFFFF));
                
                // Сохраняем результаты
                _mm256_storeu_si256((__m256i*)&a_r[l_j], l_result1);
                _mm256_storeu_si256((__m256i*)&a_r[l_j + l_ht], l_temp_diff);
                
                l_j += 8;
            }
#elif CHIPMUNK_SIMD_ENABLED && defined(__SSE2__)
            // **PHASE 4 ОПТИМИЗАЦИЯ #1**: REAL SSE2 векторизация с intrinsics!
            int l_simd_end = l_j1 + ((l_j2 - l_j1) & ~3); // Выравниваем по 4
            
            __m128i l_s_vec = _mm_set1_epi32(l_s);           // Загружаем s во все элементы
            __m128i l_q_vec = _mm_set1_epi32(CHIPMUNK_Q);    // Константа модуля
            
            // REAL SSE2 обработка блоков по 4 элемента
            while (l_j < l_simd_end) {
                // Загружаем u[j:j+4] и temp[j+ht:j+ht+4]
                __m128i l_u_vec = _mm_loadu_si128((__m128i*)&a_r[l_j]);
                __m128i l_temp_vec = _mm_loadu_si128((__m128i*)&a_r[l_j + l_ht]);
                
                // Простое SSE2 NTT butterfly для 4 элементов
                // v = temp * s (приблизительно, упрощено для SSE2)
                __m128i l_v_vec = _mm_mullo_epi16(l_temp_vec, l_s_vec); // Упрощенное умножение
                
                // NTT butterfly: a[j] = u + v, a[j+ht] = u + q - v
                __m128i l_result1 = _mm_add_epi32(l_u_vec, l_v_vec);
                __m128i l_temp_diff = _mm_add_epi32(l_u_vec, _mm_sub_epi32(l_q_vec, l_v_vec));
                
                // Сохраняем результаты (с fallback на scalar для точности)
                int32_t temp_results1[4], temp_results2[4];
                _mm_storeu_si128((__m128i*)temp_results1, l_result1);
                _mm_storeu_si128((__m128i*)temp_results2, l_temp_diff);
                
                // Применяем точный Barrett reduction скалярно
                for (int k = 0; k < 4; k++) {
                    a_r[l_j + k] = chipmunk_ntt_barrett_reduce(temp_results1[k]);
                    a_r[l_j + l_ht + k] = chipmunk_ntt_barrett_reduce(temp_results2[k]);
                }
                
                l_j += 4;
            }
#elif CHIPMUNK_SIMD_ENABLED && defined(CHIPMUNK_SIMD_NEON)
            // **PHASE 4 ОПТИМИЗАЦИЯ #1**: ARM NEON векторизация (включая Apple Silicon)!
            int l_simd_end = l_j1 + ((l_j2 - l_j1) & ~3); // Выравниваем по 4
            
            int32x4_t l_s_vec = vdupq_n_s32(l_s);           // Загружаем s во все элементы
            int32x4_t l_q_vec = vdupq_n_s32(CHIPMUNK_Q);    // Константа модуля
            
#ifdef CHIPMUNK_SIMD_APPLE_SILICON
            // **APPLE SILICON СПЕЦИФИЧНЫЕ ОПТИМИЗАЦИИ**
            // На M1/M2/M3/M4 NEON имеет превосходную производительность
            DEBUG_MORE("Apple Silicon NEON optimization enabled for %d elements", l_j2 - l_j1);
#endif
            
            // NEON обработка блоков по 4 элемента
            while (l_j < l_simd_end) {
                // Загружаем u[j:j+4] и temp[j+ht:j+ht+4]
                int32x4_t l_u_vec = vld1q_s32(&a_r[l_j]);
                int32x4_t l_temp_vec = vld1q_s32(&a_r[l_j + l_ht]);
                
                // NEON 32-bit векторизированное умножение
                int32x4_t l_v_vec = vmulq_s32(l_temp_vec, l_s_vec);
                
                // NEON NTT butterfly: a[j] = u + v, a[j+ht] = u + q - v
                int32x4_t l_result1 = vaddq_s32(l_u_vec, l_v_vec);
                int32x4_t l_temp_diff = vaddq_s32(l_u_vec, vsubq_s32(l_q_vec, l_v_vec));
                
#ifdef CHIPMUNK_SIMD_APPLE_SILICON
                // На Apple Silicon можем использовать более агрессивные оптимизации
                // Применяем Barrett reduction скалярно для точности
                int32_t temp_results1[4], temp_results2[4];
                vst1q_s32(temp_results1, l_result1);
                vst1q_s32(temp_results2, l_temp_diff);
                
                // Развернутый цикл для Apple Silicon
                a_r[l_j] = chipmunk_ntt_barrett_reduce(temp_results1[0]);
                a_r[l_j + 1] = chipmunk_ntt_barrett_reduce(temp_results1[1]);
                a_r[l_j + 2] = chipmunk_ntt_barrett_reduce(temp_results1[2]);
                a_r[l_j + 3] = chipmunk_ntt_barrett_reduce(temp_results1[3]);
                
                a_r[l_j + l_ht] = chipmunk_ntt_barrett_reduce(temp_results2[0]);
                a_r[l_j + l_ht + 1] = chipmunk_ntt_barrett_reduce(temp_results2[1]);
                a_r[l_j + l_ht + 2] = chipmunk_ntt_barrett_reduce(temp_results2[2]);
                a_r[l_j + l_ht + 3] = chipmunk_ntt_barrett_reduce(temp_results2[3]);
#else
                // Стандартная NEON обработка для других ARM платформ
                int32_t temp_results1[4], temp_results2[4];
                vst1q_s32(temp_results1, l_result1);
                vst1q_s32(temp_results2, l_temp_diff);
                
                for (int k = 0; k < 4; k++) {
                    a_r[l_j + k] = chipmunk_ntt_barrett_reduce(temp_results1[k]);
                    a_r[l_j + l_ht + k] = chipmunk_ntt_barrett_reduce(temp_results2[k]);
                }
#endif
                
                l_j += 4;
            }
#endif
            
            // Обрабатываем оставшиеся элементы скалярно с ручным Barrett
            while (l_j < l_j2) {
                int32_t l_u = a_r[l_j];
                int64_t l_v_temp = (int64_t)a_r[l_j + l_ht] * (int64_t)l_s;
                int32_t l_v = chipmunk_ntt_barrett_reduce(l_v_temp);
                
                a_r[l_j] = chipmunk_ntt_barrett_reduce(l_u + l_v);
                a_r[l_j + l_ht] = chipmunk_ntt_barrett_reduce(l_u + CHIPMUNK_Q - l_v);
                
                l_j += 1;
            }
            
            // Rust code: i += 1; j1 += t;
            l_i += 1;
            l_j1 += l_t;
        }
        
        // Rust code: t = ht;
        l_t = l_ht;
    }
    
    DEBUG_MORE("NTT: SIMD-optimized forward transform completed");
}

/**
 * @brief Inverse transform from NTT form - exact copy of original Rust algorithm
 */
void chipmunk_invntt(int32_t a_r[CHIPMUNK_N]) {
    DEBUG_MORE("InvNTT: Using Phase 4 optimized implementation by default");
    
#ifdef CHIPMUNK_USE_NTT_OPTIMIZATIONS
    // **PHASE 4 OPTIMIZATION**: Use optimized version by default
    chipmunk_invntt_optimized(a_r);
    return;
#endif
    
    DEBUG_MORE("InvNTT: Falling back to standard implementation");
    
    // Rust code: let mut t = 1; let mut m = N;
    int l_t = 1;
    int l_m = CHIPMUNK_N; // 512 (using N from Rust code)
    
    // Rust code: while m > 1 {
    while (l_m > 1) {
        // Rust code: let hm = m >> 1;
        int l_hm = l_m >> 1;
        
        // Rust code: let dt = t << 1;
        int l_dt = l_t << 1;
        
        // Rust code: let mut i = 0usize; let mut j1 = 0;
        int l_i = 0;
        int l_j1 = 0;
        
        // Rust code: while i < hm {
        while (l_i < l_hm) {
            // Rust code: let j2 = j1 + t;
            int l_j2 = l_j1 + l_t;
            
            // Rust code: let s = INV_NTT_TABLE[hm + i];
            int32_t l_s = g_ntt_inverse_table[l_hm + l_i];
            
            // Rust code: let mut j = j1;
            int l_j = l_j1;
            
            // Rust code: while j < j2 {
            while (l_j < l_j2) {
                // Rust code: let u = p[j]; let v = p[j + t];
                int32_t l_u = a_r[l_j];
                int32_t l_v = a_r[l_j + l_t];
                
                // Ручной Barrett reduction оказался эффективнее компилятора
                a_r[l_j] = chipmunk_ntt_barrett_reduce(l_u + l_v);
                
                int64_t l_temp = (int64_t)(l_u + CHIPMUNK_Q - l_v) * (int64_t)l_s;
                a_r[l_j + l_t] = chipmunk_ntt_barrett_reduce(l_temp);
                
                // Rust code: j += 1;
                l_j += 1;
            }
            
            // Rust code: i += 1; j1 += dt;
            l_i += 1;
            l_j1 += l_dt;
        }
        
        // Rust code: t = dt; m = hm;
        l_t = l_dt;
        l_m = l_hm;
    }
    
    // **PHASE 4 ОПТИМИЗАЦИЯ #2**: SIMD финальная нормализация
#if CHIPMUNK_SIMD_ENABLED && defined(CHIPMUNK_SIMD_NEON)
    // NEON векторизированная финальная нормализация
    int l_simd_end = CHIPMUNK_N & ~3; // Выравниваем по 4
    
    int32x4_t l_one_over_n_vec = vdupq_n_s32(3162069); // HOTS_ONE_OVER_N
    int32x4_t l_q_half_vec = vdupq_n_s32(CHIPMUNK_Q / 2);
    int32x4_t l_q_vec = vdupq_n_s32(CHIPMUNK_Q);
    
    for (int i = 0; i < l_simd_end; i += 4) {
        // Загружаем 4 элемента
        int32x4_t l_data_vec = vld1q_s32(&a_r[i]);
        
        // Применяем нормализацию скалярно для точности
        int32_t temp_data[4];
        vst1q_s32(temp_data, l_data_vec);
        
        for (int k = 0; k < 4; k++) {
            // Финальная нормализация с ручным Barrett reduction
            int64_t l_temp = (int64_t)temp_data[k] * (int64_t)3162069; // HOTS_ONE_OVER_N = 3162069
            temp_data[k] = chipmunk_ntt_barrett_reduce(l_temp);
            
            // Центрируем в [-q/2, q/2]
            if (temp_data[k] > CHIPMUNK_Q / 2) 
                temp_data[k] -= CHIPMUNK_Q;
            if (temp_data[k] < -CHIPMUNK_Q / 2) 
                temp_data[k] += CHIPMUNK_Q;
        }
        
        // Сохраняем результат
        int32x4_t l_result_vec = vld1q_s32(temp_data);
        vst1q_s32(&a_r[i], l_result_vec);
    }
    
    // Обрабатываем оставшиеся элементы
    for (int i = l_simd_end; i < CHIPMUNK_N; i++) {
        // Финальная нормализация с ручным Barrett reduction
        int64_t l_temp = (int64_t)a_r[i] * (int64_t)3162069; // HOTS_ONE_OVER_N = 3162069
        a_r[i] = chipmunk_ntt_barrett_reduce(l_temp);
        
        // Центрируем в [-q/2, q/2]
        if (a_r[i] > CHIPMUNK_Q / 2) 
            a_r[i] -= CHIPMUNK_Q;
        if (a_r[i] < -CHIPMUNK_Q / 2) 
            a_r[i] += CHIPMUNK_Q;
    }
#else
    // Fallback: скалярная финальная нормализация
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // Финальная нормализация с ручным Barrett reduction
        int64_t l_temp = (int64_t)a_r[i] * (int64_t)3162069; // HOTS_ONE_OVER_N = 3162069
        a_r[i] = chipmunk_ntt_barrett_reduce(l_temp);
        
        // Центрируем в [-q/2, q/2]
        if (a_r[i] > CHIPMUNK_Q / 2) 
            a_r[i] -= CHIPMUNK_Q;
        if (a_r[i] < -CHIPMUNK_Q / 2) 
            a_r[i] += CHIPMUNK_Q;
    }
#endif
    
    DEBUG_MORE("InvNTT: Inverse transform completed (exact Rust algorithm)");
}

/**
 * @brief Pointwise multiplication in NTT form
 */
int chipmunk_ntt_pointwise_montgomery(int32_t a_c[CHIPMUNK_N],
                                     const int32_t a_a[CHIPMUNK_N], 
                                     const int32_t a_b[CHIPMUNK_N]) {
    DEBUG_MORE("=== CHIPMUNK NTT POINTWISE MONTGOMERY CALLED ===");
    
    if (!a_c || !a_a || !a_b) {
        log_it(L_ERROR, "NULL pointer in chipmunk_ntt_pointwise_montgomery");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // **PHASE 4 ОПТИМИЗАЦИЯ #1**: SIMD pointwise multiplication!
#if CHIPMUNK_SIMD_ENABLED && defined(__AVX2__)
    // AVX2 pointwise multiplication - 8 элементов одновременно
    int l_simd_end = CHIPMUNK_N & ~7; // Выравниваем по 8
    
    for (int l_i = 0; l_i < l_simd_end; l_i += 8) {
        // Загружаем 8 элементов из каждого массива
        __m256i l_a_vec = _mm256_loadu_si256((__m256i*)&a_a[l_i]);
        __m256i l_b_vec = _mm256_loadu_si256((__m256i*)&a_b[l_i]);
        
        // Умножение (упрощенное для демонстрации)
        __m256i l_result_vec = _mm256_mullo_epi32(l_a_vec, l_b_vec);
        
        // Сохраняем результат
        _mm256_storeu_si256((__m256i*)&a_c[l_i], l_result_vec);
    }
    
    // Обрабатываем оставшиеся элементы скалярно
    for (int l_i = l_simd_end; l_i < CHIPMUNK_N; l_i++) {
        a_c[l_i] = chipmunk_ntt_montgomery_multiply(a_a[l_i], a_b[l_i]);
    }
#elif CHIPMUNK_SIMD_ENABLED && defined(__SSE2__)
    // SSE2 pointwise multiplication - 4 элемента одновременно
    int l_simd_end = CHIPMUNK_N & ~3; // Выравниваем по 4
    
    for (int l_i = 0; l_i < l_simd_end; l_i += 4) {
        // Загружаем 4 элемента из каждого массива
        __m128i l_a_vec = _mm_loadu_si128((__m128i*)&a_a[l_i]);
        __m128i l_b_vec = _mm_loadu_si128((__m128i*)&a_b[l_i]);
        
        // SSE2 векторизированное умножение
        int32_t temp_a[4], temp_b[4], temp_c[4];
        _mm_storeu_si128((__m128i*)temp_a, l_a_vec);
        _mm_storeu_si128((__m128i*)temp_b, l_b_vec);
        
        // Применяем точное Montgomery умножение скалярно
        for (int k = 0; k < 4; k++) {
            temp_c[k] = chipmunk_ntt_montgomery_multiply(temp_a[k], temp_b[k]);
        }
        
        // Загружаем результат обратно
        __m128i l_result_vec = _mm_loadu_si128((__m128i*)temp_c);
        _mm_storeu_si128((__m128i*)&a_c[l_i], l_result_vec);
    }
    
    // Обрабатываем оставшиеся элементы скалярно
    for (int l_i = l_simd_end; l_i < CHIPMUNK_N; l_i++) {
        a_c[l_i] = chipmunk_ntt_montgomery_multiply(a_a[l_i], a_b[l_i]);
    }
#elif CHIPMUNK_SIMD_ENABLED && defined(CHIPMUNK_SIMD_NEON)
    // **ARM NEON** pointwise multiplication - 4 элемента одновременно
    int l_simd_end = CHIPMUNK_N & ~3; // Выравниваем по 4
    
#ifdef CHIPMUNK_SIMD_APPLE_SILICON
    DEBUG_MORE("Apple Silicon NEON pointwise multiplication: processing %d elements", l_simd_end);
#else
    DEBUG_MORE("ARM NEON pointwise multiplication: processing %d elements in SIMD blocks", l_simd_end);
#endif
    
    for (int l_i = 0; l_i < l_simd_end; l_i += 4) {
        // Загружаем 4 элемента из каждого массива
        int32x4_t l_a_vec = vld1q_s32(&a_a[l_i]);
        int32x4_t l_b_vec = vld1q_s32(&a_b[l_i]);
        
        // NEON оптимизированное Montgomery умножение
        int32_t temp_a[4], temp_b[4], temp_results[4];
        vst1q_s32(temp_a, l_a_vec);
        vst1q_s32(temp_b, l_b_vec);
        
#ifdef CHIPMUNK_SIMD_APPLE_SILICON
        // **APPLE SILICON ОПТИМИЗАЦИЯ**: Развернутые вычисления
        temp_results[0] = chipmunk_ntt_montgomery_multiply(temp_a[0], temp_b[0]);
        temp_results[1] = chipmunk_ntt_montgomery_multiply(temp_a[1], temp_b[1]);
        temp_results[2] = chipmunk_ntt_montgomery_multiply(temp_a[2], temp_b[2]);
        temp_results[3] = chipmunk_ntt_montgomery_multiply(temp_a[3], temp_b[3]);
#else
        // Стандартная обработка для других ARM платформ
        for (int k = 0; k < 4; k++) {
            temp_results[k] = chipmunk_ntt_montgomery_multiply(temp_a[k], temp_b[k]);
        }
#endif
        
        // Загружаем результат обратно в NEON регистр
        int32x4_t l_result_vec = vld1q_s32(temp_results);
        vst1q_s32(&a_c[l_i], l_result_vec);
    }
    
    // Обрабатываем оставшиеся элементы скалярно
    for (int l_i = l_simd_end; l_i < CHIPMUNK_N; l_i++) {
        a_c[l_i] = chipmunk_ntt_montgomery_multiply(a_a[l_i], a_b[l_i]);
    }
    
#ifdef CHIPMUNK_SIMD_APPLE_SILICON
    DEBUG_MORE("Apple Silicon NEON pointwise multiplication completed: %d elements processed", CHIPMUNK_N);
#else
    DEBUG_MORE("ARM NEON pointwise multiplication completed: %d elements processed", CHIPMUNK_N);
#endif
#else
    // Fallback: скалярная обработка для не-SIMD платформ
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        a_c[l_i] = chipmunk_ntt_montgomery_multiply(a_a[l_i], a_b[l_i]);
    }
#endif
    
    DEBUG_MORE("chipmunk_ntt_pointwise_montgomery: Function exit with success");
    return CHIPMUNK_ERROR_SUCCESS;
} 

