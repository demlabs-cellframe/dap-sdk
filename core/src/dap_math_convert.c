#include "dap_math_convert.h"

#define LOG_TAG "dap_math_convert"

const union __c_pow10_double__ {
    uint64_t u64[4];
    uint32_t u32[8];
} DAP_ALIGN_PACKED c_pow10_double[DATOSHI_POW256] = {
#ifdef DAP_GLOBAL_IS_INT128
        {.u64 = {0, 0, 0, 1ULL}},                          // 0
        {.u64 = {0, 0, 0, 10ULL}},                         // 1
        {.u64 = {0, 0, 0, 100ULL}},                        // 2
        {.u64 = {0, 0, 0, 1000ULL}},                       // 3
        {.u64 = {0, 0, 0, 10000ULL}},                      // 4
        {.u64 = {0, 0, 0, 100000ULL}},                     // 5
        {.u64 = {0, 0, 0, 1000000ULL}},                    // 6
        {.u64 = {0, 0, 0, 10000000ULL}},                   // 7
        {.u64 = {0, 0, 0, 100000000ULL}},                  // 8
        {.u64 = {0, 0, 0, 1000000000ULL}},                 // 9
        {.u64 = {0, 0, 0, 10000000000ULL}},                // 10
        {.u64 = {0, 0, 0, 100000000000ULL}},               // 11
        {.u64 = {0, 0, 0, 1000000000000ULL}},              // 12
        {.u64 = {0, 0, 0, 10000000000000ULL}},             // 13
        {.u64 = {0, 0, 0, 100000000000000ULL}},            // 14
        {.u64 = {0, 0, 0, 1000000000000000ULL}},           // 15
        {.u64 = {0, 0, 0, 10000000000000000ULL}},          // 16
        {.u64 = {0, 0, 0, 100000000000000000ULL}},         // 17
        {.u64 = {0, 0, 0, 1000000000000000000ULL}},        // 18
        {.u64 = {0, 0, 0, 10000000000000000000ULL}},       // 19
        {.u64 = {0, 0, 5ULL, 7766279631452241920ULL}},        // 20
        {.u64 = {0, 0, 54ULL, 3875820019684212736ULL}},        // 21
        {.u64 = {0, 0, 542ULL, 1864712049423024128ULL}},        // 22
        {.u64 = {0, 0, 5421ULL, 200376420520689664ULL}},         // 23
        {.u64 = {0, 0, 54210ULL, 2003764205206896640ULL}},        // 24
        {.u64 = {0, 0, 542101ULL, 1590897978359414784ULL}},        // 25
        {.u64 = {0, 0, 5421010ULL, 15908979783594147840ULL}},       // 26
        {.u64 = {0, 0, 54210108ULL, 11515845246265065472ULL}},       // 27
        {.u64 = {0, 0, 542101086ULL, 4477988020393345024ULL}},        // 28
        {.u64 = {0, 0, 5421010862ULL, 7886392056514347008ULL}},        // 29
        {.u64 = {0, 0, 54210108624ULL, 5076944270305263616ULL}},        // 30
        {.u64 = {0, 0, 542101086242ULL, 13875954555633532928ULL}},       // 31
        {.u64 = {0, 0, 5421010862427ULL, 9632337040368467968ULL}},        // 32
        {.u64 = {0, 0, 54210108624275ULL, 4089650035136921600ULL}},        // 33
        {.u64 = {0, 0, 542101086242752ULL, 4003012203950112768ULL}},        // 34
        {.u64 = {0, 0, 5421010862427522ULL, 3136633892082024448ULL}},        // 35
        {.u64 = {0, 0, 54210108624275221ULL, 12919594847110692864ULL}},       // 36
        {.u64 = {0, 0, 542101086242752217ULL, 68739955140067328ULL}},          // 37
        {.u64 = {0, 0, 5421010862427522170ULL, 687399551400673280ULL}},         // 38
        {.u64 = {0, 2ULL, 17316620476856118468ULL, 6873995514006732800ULL}},        // 39
        {.u64 = {0, 29ULL, 7145508105175220139ULL, 13399722918938673152ULL}},       // 40
        {.u64 = {0, 293ULL, 16114848830623546549ULL, 4870020673419870208ULL}},        // 41
        {.u64 = {0, 2938ULL, 13574535716559052564ULL, 11806718586779598848ULL}},       // 42
        {.u64 = {0, 29387ULL, 6618148649623664334ULL, 7386721425538678784ULL}},        // 43
        {.u64 = {0, 293873ULL, 10841254275107988496ULL, 80237960548581376ULL}},          // 44
        {.u64 = {0, 2938735ULL, 16178822382532126880ULL, 802379605485813760ULL}},          // 45
        {.u64 = {0, 29387358ULL, 14214271235644855872ULL, 8023796054858137600ULL}},          // 46
        {.u64 = {0, 293873587ULL, 13015503840481697412ULL, 6450984253743169536ULL}},          // 47
        {.u64 = {0, 2938735877ULL, 1027829888850112811ULL, 9169610316303040512ULL}},          // 48
        {.u64 = {0, 29387358770ULL, 10278298888501128114ULL, 17909126868192198656ULL}},          // 49
        {.u64 = {0, 293873587705ULL, 10549268516463523069ULL, 13070572018536022016ULL}},          // 50
        {.u64 = {0, 2938735877055ULL, 13258964796087472617ULL, 1578511669393358848ULL}},          // 51
        {.u64 = {0, 29387358770557ULL, 3462439444907864858ULL, 15785116693933588480ULL}},          // 52
        {.u64 = {0, 293873587705571ULL, 16177650375369096972ULL, 10277214349659471872ULL}},          // 53
        {.u64 = {0, 2938735877055718ULL, 14202551164014556797ULL, 10538423128046960640ULL}},          // 54
        {.u64 = {0, 29387358770557187ULL, 12898303124178706663ULL, 13150510911921848320ULL}},          // 55
        {.u64 = {0, 293873587705571876ULL, 18302566799529756941ULL, 2377900603251621888ULL}},          // 56
        {.u64 = {0, 2938735877055718769ULL, 17004971331911604867ULL, 5332261958806667264ULL}},          // 57
        {.u64 = {1, 10940614696847636083ULL, 4029016655730084128ULL, 16429131440647569408ULL}},          // 58
        {.u64 = {15ULL, 17172426599928602752ULL, 3396678409881738056ULL, 16717361816799281152ULL}},          // 59
        {.u64 = {159ULL, 5703569335900062977ULL, 15520040025107828953ULL, 1152921504606846976ULL}},          // 60
        {.u64 = {1593ULL, 1695461137871974930ULL, 7626447661401876602ULL, 11529215046068469760ULL}},          // 61
        {.u64 = {15930ULL, 16954611378719749304ULL, 2477500319180559562ULL, 4611686018427387904ULL}},          // 62
        {.u64 = {159309ULL, 3525417123811528497ULL, 6328259118096044006ULL, 9223372036854775808ULL}},          // 63
        {.u64 = {1593091ULL, 16807427164405733357ULL, 7942358959831785217ULL, 0ULL}},                            // 64
        {.u64 = {15930919ULL, 2053574980671369030ULL, 5636613303479645706ULL, 0ULL}},                            // 65
        {.u64 = {159309191ULL, 2089005733004138687ULL, 1025900813667802212ULL, 0ULL}},                            // 66
        {.u64 = {1593091911ULL, 2443313256331835254ULL, 10259008136678022120ULL,
                 0ULL}},                            // 67
        {.u64 = {15930919111ULL, 5986388489608800929ULL, 10356360998232463120ULL,
                 0ULL}},                            // 68
        {.u64 = {159309191113ULL, 4523652674959354447ULL, 11329889613776873120ULL,
                 0ULL}},                            // 69
        {.u64 = {1593091911132ULL, 8343038602174441244ULL, 2618431695511421504ULL,
                 0ULL}},                            // 70
        {.u64 = {15930919111324ULL, 9643409726906205977ULL, 7737572881404663424ULL,
                 0ULL}},                            // 71
        {.u64 = {159309191113245ULL, 4200376900514301694ULL, 3588752519208427776ULL,
                 0ULL}},                            // 72
        {.u64 = {1593091911132452ULL, 5110280857723913709ULL, 17440781118374726144ULL,
                 0ULL}},                            // 73
        {.u64 = {15930919111324522ULL, 14209320429820033867ULL, 8387114520361296896ULL,
                 0ULL}},                            // 74
        {.u64 = {159309191113245227ULL, 12965995782233477362ULL, 10084168908774762496ULL,
                 0ULL}},                            // 75
        {.u64 = {1593091911132452277ULL, 532749306367912313ULL, 8607968719199866880ULL,
                 0ULL}},                            // 76
        {.u64 = {15930919111324522770ULL, 5327493063679123134ULL, 12292710897160462336ULL,
                 0ULL}},                         // 77
#else
        { .u32 = {0, 0, 0, 0, 0, 0, 0, 1, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 0, 10, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 0, 100, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 0, 1000, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 0, 10000, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 0, 100000, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 0, 1000000, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 0, 10000000, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 0, 100000000, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 0, 1000000000, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 2, 1410065408, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 23, 1215752192, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 232, 3567587328, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 2328, 1316134912, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 23283, 276447232, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 232830, 2764472320, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 2328306, 1874919424, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 23283064, 1569325056, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 232830643, 2808348672, } },
        { .u32 = {0, 0, 0, 0, 0, 0, 2328306436, 2313682944, } },
        { .u32 = {0, 0, 0, 0, 0, 5, 1808227885, 1661992960, } },
        { .u32 = {0, 0, 0, 0, 0, 54, 902409669, 3735027712, } },
        { .u32 = {0, 0, 0, 0, 0, 542, 434162106, 2990538752, } },
        { .u32 = {0, 0, 0, 0, 0, 5421, 46653770, 4135583744, } },
        { .u32 = {0, 0, 0, 0, 0, 54210, 466537709, 2701131776, } },
        { .u32 = {0, 0, 0, 0, 0, 542101, 370409800, 1241513984, } },
        { .u32 = {0, 0, 0, 0, 0, 5421010, 3704098002, 3825205248, } },
        { .u32 = {0, 0, 0, 0, 0, 54210108, 2681241660, 3892314112, } },
        { .u32 = {0, 0, 0, 0, 0, 542101086, 1042612833, 268435456, } },
        { .u32 = {0, 0, 0, 0, 1, 1126043566, 1836193738, 2684354560, } },
        { .u32 = {0, 0, 0, 0, 12, 2670501072, 1182068202, 1073741824, } },
        { .u32 = {0, 0, 0, 0, 126, 935206946, 3230747430, 2147483648, } },
        { .u32 = {0, 0, 0, 0, 1262, 762134875, 2242703233, 0, } },
        { .u32 = {0, 0, 0, 0, 12621, 3326381459, 952195850, 0, } },
        { .u32 = {0, 0, 0, 0, 126217, 3199043520, 932023908, 0, } },
        { .u32 = {0, 0, 0, 0, 1262177, 1925664130, 730304488, 0, } },
        { .u32 = {0, 0, 0, 0, 12621774, 2076772117, 3008077584, 0, } },
        { .u32 = {0, 0, 0, 0, 126217744, 3587851993, 16004768, 0, } },
        { .u32 = {0, 0, 0, 0, 1262177448, 1518781562, 160047680, 0, } },
        { .u32 = {0, 0, 0, 2, 4031839891, 2302913732, 1600476800, 0, } },
        { .u32 = {0, 0, 0, 29, 1663693251, 1554300843, 3119866112, 0, } },
        { .u32 = {0, 0, 0, 293, 3752030625, 2658106549, 1133890048, 0, } },
        { .u32 = {0, 0, 0, 2938, 3160567888, 811261716, 2748965888, 0, } },
        { .u32 = {0, 0, 0, 29387, 1540907809, 3817649870, 1719855104, 0, } },
        { .u32 = {0, 0, 0, 293873, 2524176210, 3816760336, 18681856, 0, } },
        { .u32 = {0, 0, 0, 2938735, 3766925628, 3807864992, 186818560, 0, } },
        { .u32 = {0, 0, 0, 29387358, 3309517920, 3718911552, 1868185600, 0, } },
        { .u32 = {0, 0, 0, 293873587, 3030408136, 2829377156, 1501986816, 0, } },
        { .u32 = {0, 0, 0, 2938735877, 239310294, 2523967787, 2134966272, 0, } },
        { .u32 = {0, 0, 6, 3617554994, 2393102945, 3764841394, 4169793536, 0, } },
        { .u32 = {0, 0, 68, 1815811577, 2456192978, 3288675581, 3043229696, 0, } },
        { .u32 = {0, 0, 684, 978246591, 3087093307, 2821984745, 367525888, 0, } },
        { .u32 = {0, 0, 6842, 1192531325, 806162004, 2450043674, 3675258880, 0, } },
        { .u32 = {0, 0, 68422, 3335378659, 3766652749, 3025600268, 2392850432, 0, } },
        { .u32 = {0, 0, 684227, 3289015526, 3306789129, 191231613, 2453667840, 0, } },
        { .u32 = {0, 0, 6842277, 2825384195, 3003120218, 1912316135, 3061841920, 0, } },
        { .u32 = {0, 0, 68422776, 2484038180, 4261398408, 1943292173, 553648128, 0, } },
        { .u32 = {0, 0, 684227765, 3365545329, 3959278420, 2253052547, 1241513984, 0, } },
        { .u32 = {0, 1, 2547310361, 3590682227, 938078541, 1055688992, 3825205248, 0, } },
        { .u32 = {0, 15, 3998267138, 1547083904, 790850820, 1966955336, 3892314112, 0, } },
        { .u32 = {0, 159, 1327965719, 2585937153, 3613540908, 2489684185, 268435456, 0, } },
        { .u32 = {0, 1593, 394755308, 89567762, 1775670717, 3422005370, 2684354560, 0, } },
        { .u32 = {0, 15930, 3947553080, 895677624, 576837993, 4155282634, 1073741824, 0, } },
        { .u32 = {0, 159309, 820825138, 366841649, 1473412643, 2898120678, 2147483648, 0, } },
        { .u32 = {0, 1593091, 3913284084, 3668416493, 1849224548, 3211403009, 0, 0, } },
        { .u32 = {0, 15930919, 478135184, 2324426566, 1312376303, 2049259018, 0, 0, } },
        { .u32 = {0, 159309191, 486384549, 1769429183, 238861146, 3312720996, 0, 0, } },
        { .u32 = {0, 1593091911, 568878198, 514422646, 2388611467, 3062438888, 0, 0, } },
        { .u32 = {3, 3046017223, 1393814685, 849259169, 2411278197, 559617808, 0, 0, } },
        { .u32 = {37, 395401161, 1053244963, 4197624399, 2637945491, 1301210784, 0, 0, } },
        { .u32 = {370, 3954011612, 1942515047, 3321538332, 609651137, 127205952, 0, 0, } },
        { .u32 = {3709, 885410460, 2245281293, 3150612249, 1801544074, 1272059520, 0, 0, } },
        { .u32 = {37092, 264170013, 977976457, 1441351422, 835571558, 4130660608, 0, 0, } },
        { .u32 = {370920, 2641700132, 1189829981, 1528612333, 4060748293, 2651900416, 0, 0, } },
        { .u32 = {3709206, 647197546, 3308365221, 2401221451, 1952777272, 749200384, 0, 0, } },
        { .u32 = {37092061, 2177008171, 3018881143, 2537378034, 2347903537, 3197036544, 0, 0, } },
        { .u32 = {370920615, 295245237, 124040363, 3898943865, 2004198897, 1905594368, 0, 0, } },
        { .u32 = {3709206150, 2952452370, 1240403639, 334732990, 2862119790, 1876074496, 0, 0, } },
#endif
};

const union __c_pow10__ {
    uint64_t u64[2];
    uint32_t u32[4];
} DAP_ALIGN_PACKED c_pow10[DATOSHI_POW] = {
        { .u64 = {0,                         1ULL} },                          // 0
        { .u64 = {0,                         10ULL} },                         // 1
        { .u64 = {0,                         100ULL} },                        // 2
        { .u64 = {0,                         1000ULL} },                       // 3
        { .u64 = {0,                         10000ULL} },                      // 4
        { .u64 = {0,                         100000ULL} },                     // 5
        { .u64 = {0,                         1000000ULL} },                    // 6
        { .u64 = {0,                         10000000ULL} },                   // 7
        { .u64 = {0,                         100000000ULL} },                  // 8
        { .u64 = {0,                         1000000000ULL} },                 // 9
        { .u64 = {0,                         10000000000ULL} },                // 10
        { .u64 = {0,                         100000000000ULL} },               // 11
        { .u64 = {0,                         1000000000000ULL} },              // 12
        { .u64 = {0,                         10000000000000ULL} },             // 13
        { .u64 = {0,                         100000000000000ULL} },            // 14
        { .u64 = {0,                         1000000000000000ULL} },           // 15
        { .u64 = {0,                         10000000000000000ULL} },          // 16
        { .u64 = {0,                         100000000000000000ULL} },         // 17
        { .u64 = {0,                         1000000000000000000ULL} },        // 18
        { .u64 = {0,                         10000000000000000000ULL} },       // 19
        { .u64 = {5ULL,                      7766279631452241920ULL} },        // 20
        { .u64 = {54ULL,                     3875820019684212736ULL} },        // 21
        { .u64 = {542ULL,                    1864712049423024128ULL} },        // 22
        { .u64 = {5421ULL,                   200376420520689664ULL} },         // 23
        { .u64 = {54210ULL,                  2003764205206896640ULL} },        // 24
        { .u64 = {542101ULL,                 1590897978359414784ULL} },        // 25
        { .u64 = {5421010ULL,                15908979783594147840ULL} },       // 26
        { .u64 = {54210108ULL,               11515845246265065472ULL} },       // 27
        { .u64 = {542101086ULL,              4477988020393345024ULL} },        // 28
        { .u64 = {5421010862ULL,             7886392056514347008ULL} },        // 29
        { .u64 = {54210108624ULL,            5076944270305263616ULL} },        // 30
        { .u64 = {542101086242ULL,           13875954555633532928ULL} },       // 31
        { .u64 = {5421010862427ULL,          9632337040368467968ULL} },        // 32
        { .u64 = {54210108624275ULL,         4089650035136921600ULL} },        // 33
        { .u64 = {542101086242752ULL,        4003012203950112768ULL} },        // 34
        { .u64 = {5421010862427522ULL,       3136633892082024448ULL} },        // 35
        { .u64 = {54210108624275221ULL,      12919594847110692864ULL} },       // 36
        { .u64 = {542101086242752217ULL,     68739955140067328ULL} },          // 37
        { .u64 = {5421010862427522170ULL,    687399551400673280ULL} }          // 38
};

uint256_t dap_uint256_decimal_from_uint64(uint64_t a_uninteger)
{
    uint256_t ret;
    return MULT_256_256(GET_256_FROM_64(a_uninteger), GET_256_FROM_64(DATOSHI_MULT), &ret) ?
                uint256_0 : ret;
}

uint256_t dap_uint256_scan_uninteger(const char *a_str_uninteger)
{
    uint256_t l_ret = uint256_0, l_nul = uint256_0;
    int  l_strlen;
    char l_256bit_num[DAP_CHAIN$SZ_MAX256DEC + 1];
    int overflow_flag = 0;

    if (!a_str_uninteger) {
        return log_it(L_ERROR, "NULL as an argument"), l_nul;
    }

    /* Convert number from xxx.yyyyE+zz to xxxyyyy0000... */
    char *l_eptr = strchr(a_str_uninteger, 'e');
    if (!l_eptr)
        l_eptr = strchr(a_str_uninteger, 'E');
    if (l_eptr) {
        /* Compute & check length */
        if ( (l_strlen = strnlen(a_str_uninteger, DAP_SZ_MAX256SCINOT + 1) ) > DAP_SZ_MAX256SCINOT)
            return  log_it(L_ERROR, "Too many digits in `%s` (%d > %d)", a_str_uninteger, l_strlen, DAP_SZ_MAX256SCINOT), l_nul;

        char *l_exp_ptr = l_eptr + 1;
        if (*l_exp_ptr == '+')
            l_exp_ptr++;
        int l_exp = atoi(l_exp_ptr);
        if (!l_exp)
            return  log_it(L_ERROR, "Invalid exponent %s", l_eptr), uint256_0;
        char *l_dot_ptr = strchr(a_str_uninteger, '.');
        if (!l_dot_ptr || l_dot_ptr > l_eptr)
            return  log_it(L_ERROR, "Invalid number format with exponent %d", l_exp), uint256_0;
        int l_dot_len = l_dot_ptr - a_str_uninteger;
        if (l_dot_len >= DATOSHI_POW256)
            return log_it(L_ERROR, "Too many digits in '%s'", a_str_uninteger), uint256_0;
        int l_exp_len = l_eptr - a_str_uninteger - l_dot_len - 1;
        if (l_exp_len + l_dot_len + 1 >= DAP_SZ_MAX256SCINOT)
            return log_it(L_ERROR, "Too many digits in '%s'", a_str_uninteger), uint256_0;
        if (l_exp < l_exp_len) {
            //todo: we need to handle numbers like 1.23456789000000e9
            return log_it(L_ERROR, "Invalid number format with exponent %d and number count after dot %d", l_exp,
                          l_exp_len), uint256_0;
        }
        memcpy(l_256bit_num, a_str_uninteger, l_dot_len);
        memcpy(l_256bit_num + l_dot_len, a_str_uninteger + l_dot_len + 1, l_exp_len);
        int l_zero_cnt = l_exp - l_exp_len;
        if (l_zero_cnt > DATOSHI_POW256) {
            //todo: need to handle leading zeroes, like 0.000...123e100
            return log_it(L_ERROR, "Too long number for 256 bit: `%s` (%d > %d)", a_str_uninteger, l_strlen, DAP_CHAIN$SZ_MAX256DEC), l_nul;
        }
        size_t l_pos = l_dot_len + l_exp_len;
        for (int i = l_zero_cnt; i && l_pos < DATOSHI_POW256; i--)
            l_256bit_num[l_pos++] = '0';
        l_256bit_num[l_pos] = '\0';
        l_strlen = l_pos;

    } else {
        // We have a decimal string, not sci notation
        /* Compute & check length */
        if ( (l_strlen = strnlen(a_str_uninteger, DATOSHI_POW256 + 1) ) > DATOSHI_POW256)
            return  log_it(L_ERROR, "Too many digits in `%s` (%d > %d)", a_str_uninteger, l_strlen, DATOSHI_POW256), l_nul;
        memcpy(l_256bit_num, a_str_uninteger, l_strlen);
        l_256bit_num[l_strlen] = '\0';
    }

    for (int i = 0; i < l_strlen ; i++) {
        char c = l_256bit_num[l_strlen - i - 1];
        if (!isdigit(c)) {
            log_it(L_WARNING, "Incorrect input number");
            return l_nul;
        }
        uint8_t l_digit = c - '0';
        if (!l_digit)
            continue;
#ifdef DAP_GLOBAL_IS_INT128
        uint256_t l_tmp;
        l_tmp.hi = 0;
        l_tmp.lo = (uint128_t)c_pow10_double[i].u64[3] * (uint128_t) l_digit;
        overflow_flag = SUM_256_256(l_ret, l_tmp, &l_ret);
        if (overflow_flag) {
            //todo: change string to uint256_max after implementation
            return log_it(L_ERROR, "Too big number '%s', max number is '%s'", a_str_uninteger, "115792089237316195423570985008687907853269984665640564039457584007913129639935"), l_nul;
        }
        uint128_t l_mul = (uint128_t) c_pow10_double[i].u64[2] * (uint128_t) l_digit;
        l_tmp.lo = l_mul << 64;
        l_tmp.hi = l_mul >> 64;
        overflow_flag = SUM_256_256(l_ret, l_tmp, &l_ret);
        if (overflow_flag) {
            //todo: change string to uint256_max after implementation
            return log_it(L_ERROR, "Too big number '%s', max number is '%s'", a_str_uninteger, "115792089237316195423570985008687907853269984665640564039457584007913129639935"), l_nul;
        }

        if (l_ret.hi == 0 && l_ret.lo == 0) {
            return l_nul;
        }

        l_tmp.lo = 0;
        l_tmp.hi = (uint128_t) c_pow10_double[i].u64[1] * (uint128_t) l_digit;
        overflow_flag = SUM_256_256(l_ret, l_tmp, &l_ret);
        if (overflow_flag) {
            //todo: change string to uint256_max after implementation
            return log_it(L_ERROR, "Too big number '%s', max number is '%s'", a_str_uninteger, "115792089237316195423570985008687907853269984665640564039457584007913129639935"), l_nul;
        }
        if (l_ret.hi == 0 && l_ret.lo == 0) {
            return l_nul;
        }

        l_mul = (uint128_t) c_pow10_double[i].u64[0] * (uint128_t) l_digit;
        if (l_mul >> 64) {
            log_it(L_WARNING, "Input number is too big");
            return l_nul;
        }
        l_tmp.hi = l_mul << 64;
        overflow_flag = SUM_256_256(l_ret, l_tmp, &l_ret);
        if (overflow_flag) {
            //todo: change string to uint256_max after implementation
            return log_it(L_ERROR, "Too big number '%s', max number is '%s'", a_str_uninteger, "115792089237316195423570985008687907853269984665640564039457584007913129639935"), l_nul;
        }
        if (l_ret.hi == 0 && l_ret.lo == 0) {
            return l_nul;
        }
#else
        uint256_t l_tmp;
        for (int j = 7; j>=0; j--) {
            l_tmp = GET_256_FROM_64((uint64_t) c_pow10_double[i].u32[j]);
            if (IS_ZERO_256(l_tmp)) {
                if (j < 6) { // in table, we have only 7 and 6 position with 0-es but 5..0 non-zeroes, so if we have zero on 5 or less, there is no significant position anymore
                    break;
                }
                else {
                    continue;
                }
            }
            LEFT_SHIFT_256(l_tmp, &l_tmp, 32 * (7-j));
            overflow_flag = MULT_256_256(l_tmp, GET_256_FROM_64(l_digit), &l_tmp);
            if (overflow_flag) {
                //todo: change string to uint256_max after implementation
                return log_it(L_ERROR, "Too big number '%s', max number is '%s'", a_str_uninteger, "115792089237316195423570985008687907853269984665640564039457584007913129639935"), l_nul;
            }
            overflow_flag = SUM_256_256(l_ret, l_tmp, &l_ret);
            if (overflow_flag) {
                //todo: change string to uint256_max after implementation
                return log_it(L_ERROR, "Too big number '%s', max number is '%s'", a_str_uninteger, "115792089237316195423570985008687907853269984665640564039457584007913129639935"), l_nul;
            }
        }
#endif
    }
    return l_ret;
}

uint256_t dap_uint256_scan_decimal(const char *a_str_decimal)
{
    int l_len, l_pos;
    char    l_buf  [DAP_CHAIN$SZ_MAX256DEC + 8] = {0}, *l_point;

    /* "12300000000.0000456" */
    if ( (l_len = strnlen(a_str_decimal, DATOSHI_POW256 + 2)) > DATOSHI_POW256 + 1)/* Check for legal length */ /* 1 symbol for \0, one for '.', if more, there is an error */
        return  log_it(L_WARNING, "Incorrect balance format of '%s' - too long (%d > %d)", a_str_decimal,
                       l_len, DATOSHI_POW256 + 1), uint256_0;

    /* Find , check and remove 'precision' dot symbol */
    memcpy (l_buf, a_str_decimal, l_len);                                         /* Make local copy */
    if ( !(l_point = memchr(l_buf, '.', l_len)) )                           /* Is there 'dot' ? */
        return  log_it(L_WARNING, "Incorrect balance format of '%s' - no precision mark", a_str_decimal),
                uint256_0;

    l_pos = l_len - (l_point - l_buf) - 1;                                      /* Check number of decimals after dot */
    if ( l_pos > DATOSHI_DEGREE )
        return  log_it(L_WARNING, "Incorrect balance format of '%s' - too much precision", l_buf), uint256_0;

    /* "123.456" -> "123456" */
    memmove(l_point, l_point + 1, l_pos);                                   /* Shift left a right part of the decimal string
                                                                              to dot symbol place */
    *(l_point + l_pos) = '\0';

    /* Add trailer zeros:
     *                pos
     *                 |
     * 123456 -> 12345600...000
     *           ^            ^
     *           |            |
     *           +-18 digits--+
     */
    memset(l_point + l_pos, '0', DATOSHI_DEGREE - l_pos);

    return dap_uint256_scan_uninteger(l_buf);
}

const char *dap_uint256_to_char(uint256_t a_uint256, const char **a_frac)
{
    _Thread_local static char   s_buf       [DATOSHI_POW256 + 2],
                                s_buf_frac  [DATOSHI_POW256 + 2]; // Space for decimal dot and trailing zero
    char l_c, *l_c1 = s_buf, *l_c2 = s_buf;
    uint256_t l_value = a_uint256, uint256_ten = GET_256_FROM_64(10), rem;
    do {
        divmod_impl_256(l_value, uint256_ten, &l_value, &rem);
#ifdef DAP_GLOBAL_IS_INT128
        *l_c1++ = rem.lo + '0';
#else
        *l_c1++ = rem.lo.lo + (unsigned long long) '0';
#endif
    } while (!IS_ZERO_256(l_value));
    *l_c1 = '\0';
    int l_strlen = l_c1 - s_buf;
    --l_c1;

    do {
        l_c = *l_c2; *l_c2++ = *l_c1; *l_c1-- = l_c;
    } while (l_c2 < l_c1);
    if (!a_frac)
        return s_buf;

    int l_len;

    if ( 0 < (l_len = (l_strlen - DATOSHI_DEGREE)) ) {
        memcpy(s_buf_frac, s_buf, l_len);
        memcpy(s_buf_frac + l_len + 1, s_buf + l_len, DATOSHI_DEGREE);
        s_buf_frac[l_len] = '.';
        ++l_strlen;
    } else {
        memcpy(s_buf_frac, "0.", 2);
        if (l_len)
            memset(s_buf_frac + 2, '0', -l_len);
        memcpy(s_buf_frac - l_len + 2, s_buf, l_strlen);
        l_strlen += 2 - l_len;
    }
    l_c1 = s_buf_frac + l_strlen - 1;
    while (*l_c1-- == '0' && *l_c1 != '.')
        --l_strlen;
    s_buf_frac[l_strlen] = '\0';
    *a_frac = s_buf_frac;
    return s_buf;
}

char *dap_uint256_uninteger_to_char(uint256_t a_uninteger) {
    return strdup(dap_uint256_to_char(a_uninteger, NULL));
}

char *dap_uint256_decimal_to_char(uint256_t a_decimal){ //dap_chain_balance_to_coins256, dap_chain_balance_to_coins
    const char *l_frac = NULL;
    dap_uint256_to_char(a_decimal, &l_frac);
    return strdup(l_frac);
}

const char *dap_uint256_decimal_to_round_char(uint256_t a_uint256, uint8_t a_round_position, bool is_round)
{
    char *l_uint256_str = dap_uint256_decimal_to_char(a_uint256);
    const char *l_ret = dap_uint256_char_to_round_char(l_uint256_str, a_round_position, is_round);
    return DAP_DELETE(l_uint256_str), l_ret;
}

const char *dap_uint256_char_to_round_char(char* a_str_decimal, uint8_t a_round_pos, bool is_round)
{
    _Thread_local static char s_buf[DATOSHI_POW256 + 3];
    memset(s_buf, 0, sizeof(s_buf));
    char *l_dot_pos = strchr(a_str_decimal, '.'), *l_res = s_buf;
    int l_len = strlen(a_str_decimal);
    if (!l_dot_pos || a_round_pos >= DATOSHI_DEGREE || ( l_len - (l_dot_pos - a_str_decimal) <= a_round_pos ))
        return memcpy(l_res, a_str_decimal, l_len + 1);

    int l_new_len = (l_dot_pos - a_str_decimal) + a_round_pos + 1;
    *l_res = '0';
    char    *l_src_c = a_str_decimal + l_new_len,
            *l_dst_c = l_res + l_new_len,
            l_inc = *l_src_c >= '5';

    while ( l_src_c > a_str_decimal && (*l_src_c >= '5' || l_inc) ) {
        if (*--l_src_c == '9') {
            l_inc = 1;
            *l_dst_c = *l_dst_c == '.' ? '.' : '0';
            --l_dst_c;
        } else if (*l_src_c == '.') {
            *l_dst_c-- = '.';
        } else {
            *l_dst_c-- = *l_src_c + 1;
            l_inc = 0;
            break;
        }
    }
    if (l_src_c > a_str_decimal)
        memcpy(l_res + 1, a_str_decimal, l_src_c - a_str_decimal);
    if (!a_round_pos)
        --l_new_len;
    if (l_inc) {
        *l_res = '1';
        ++l_new_len;
    } else {
        ++l_res;
    }

    *(l_res + l_new_len) = '\0';
    return l_res;
}

int dap_id_uint64_parse(const char *a_id_str, uint64_t *a_id)
{
    if (!a_id_str || !a_id || (sscanf(a_id_str, "0x%16"DAP_UINT64_FORMAT_X, a_id) != 1 &&
            sscanf(a_id_str, "0x%16"DAP_UINT64_FORMAT_x, a_id) != 1 &&
            sscanf(a_id_str, "%"DAP_UINT64_FORMAT_U, a_id) != 1)) {
        log_it (L_ERROR, "Can't recognize '%s' string as 64-bit id, hex or dec", a_id_str);
        return -1;
    }
    return 0;
}

uint64_t dap_uint128_to_uint64(uint128_t a_from)
{
#ifdef DAP_GLOBAL_IS_INT128
    if (a_from > UINT64_MAX) {
        log_it(L_ERROR, "Can't convert balance to uint64_t. It's too big.");
    }
    return (uint64_t)a_from;
#else
    if (a_from.hi) {
        log_it(L_ERROR, "Can't convert balance to uint64_t. It's too big.");
    }
    return a_from.lo;
#endif
}

uint64_t dap_uint256_to_uint64(uint256_t a_from)
{
#ifdef DAP_GLOBAL_IS_INT128
    if (a_from.hi || a_from.lo > UINT64_MAX) {
        log_it(L_ERROR, "Can't convert balance to uint64_t. It's too big.");
    }
    return (uint64_t)a_from.lo;
#else
    if (!IS_ZERO_128(a_from.hi) || a_from.lo.hi) {
        log_it(L_ERROR, "Can't convert balance to uint64_t. It's too big.");
    }
    return a_from.lo.lo;
#endif
}

// 256
uint128_t dap_uint256_to_uint128(uint256_t a_from)
{
    if ( !( EQUAL_128(a_from.hi, uint128_0) ) ) {
        log_it(L_ERROR, "Can't convert to uint128_t. It's too big.");
    }
    return a_from.lo;
}

char *dap_uint128_uninteger_to_char(uint128_t a_uninteger)
{
    char *l_buf = DAP_NEW_Z_SIZE(char, DATOSHI_POW + 2);
    if (!l_buf) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    int l_pos = 0;
    uint128_t l_value = a_uninteger;
#ifdef DAP_GLOBAL_IS_INT128
    do {
        l_buf[l_pos++] = (l_value % 10) + '0';
        l_value /= 10;
    } while (l_value);
#else
    uint32_t l_tmp[4] = {l_value.u32.a, l_value.u32.b, l_value.u32.c, l_value.u32.d};
    uint64_t t, q;
    do {
        q = 0;
        // Byte order is 1, 0, 3, 2 for little endian
        for (int i = 1; i <= 3; ) {
            t = q << 32 | l_tmp[i];
            q = t % 10;
            l_tmp[i] = t / 10;
            if (i == 2) i = 4; // end of cycle
            if (i == 3) i = 2;
            if (i == 0) i = 3;
            if (i == 1) i = 0;
        }
        l_buf[l_pos++] = q + '0';
    } while (l_tmp[2]);
#endif
    int l_strlen = strlen(l_buf) - 1;
    for (int i = 0; i < (l_strlen + 1) / 2; i++) {
        char c = l_buf[i];
        l_buf[i] = l_buf[l_strlen - i];
        l_buf[l_strlen - i] = c;
    }
    return l_buf;
}

char *dap_uint128_decimal_to_char(uint128_t a_decimal)
{
    char *l_buf = dap_uint128_uninteger_to_char(a_decimal);
    int l_strlen = strlen(l_buf);
    int l_pos;
    if (l_strlen > DATOSHI_DEGREE) {
        for (l_pos = l_strlen; l_pos > l_strlen - DATOSHI_DEGREE; l_pos--) {
            l_buf[l_pos] = l_buf[l_pos - 1];
        }
        l_buf[l_pos] = '.';
    } else {
        int l_sub = DATOSHI_DEGREE - l_strlen + 2;
        for (l_pos = DATOSHI_DEGREE + 1; l_pos >= 0; l_pos--) {
            l_buf[l_pos] = (l_pos >= l_sub) ? l_buf[l_pos - l_sub] : '0';
        }
        l_buf[1] = '.';
    }
    return l_buf;
}

uint128_t dap_uint128_scan_uninteger(const char *a_str_uninteger)
{
    int l_strlen = strlen(a_str_uninteger);
    uint128_t l_ret = uint128_0, l_nul = uint128_0;
    if (l_strlen > DATOSHI_POW)
        return l_nul;
    for (int i = 0; i < l_strlen ; i++) {
        char c = a_str_uninteger[l_strlen - i - 1];
        if (!isdigit(c)) {
            log_it(L_WARNING, "Incorrect input number");
            return l_nul;
        }
        uint8_t l_digit = c - '0';
        if (!l_digit)
            continue;
#ifdef DAP_GLOBAL_IS_INT128
        uint128_t l_tmp = (uint128_t)c_pow10[i].u64[0] * l_digit;
        if (l_tmp >> 64) {
            log_it(L_WARNING, "Input number is too big");
            return l_nul;
        }
        l_tmp = (l_tmp << 64) + (uint128_t)c_pow10[i].u64[1] * l_digit;
        SUM_128_128(l_ret, l_tmp, &l_ret);
        if (l_ret == l_nul)
            return l_nul;
#else
        uint128_t l_tmp;
        l_tmp.hi = 0;
        l_tmp.lo = (uint64_t)c_pow10[i].u32[2] * (uint64_t)l_digit;
        SUM_128_128(l_ret, l_tmp, &l_ret);
        if (l_ret.hi == 0 && l_ret.lo == 0)
            return l_nul;
        uint64_t l_mul = (uint64_t)c_pow10[i].u32[3] * (uint64_t)l_digit;
        l_tmp.lo = l_mul << 32;
        l_tmp.hi = l_mul >> 32;
        SUM_128_128(l_ret, l_tmp, &l_ret);
        if (l_ret.hi == 0 && l_ret.lo == 0)
            return l_nul;
        l_tmp.lo = 0;
        l_tmp.hi = (uint64_t)c_pow10[i].u32[0] * (uint64_t)l_digit;
        SUM_128_128(l_ret, l_tmp, &l_ret);
        if (l_ret.hi == 0 && l_ret.lo == 0)
            return l_nul;
        l_mul = (uint64_t)c_pow10[i].u32[1] * (uint64_t)l_digit;
        if (l_mul >> 32) {
            log_it(L_WARNING, "Input number is too big");
            return l_nul;
        }
        l_tmp.hi = l_mul << 32;
        SUM_128_128(l_ret, l_tmp, &l_ret);
        if (l_ret.hi == 0 && l_ret.lo == 0)
            return l_nul;
#endif
    }
    return l_ret;
}

uint128_t dap_uint128_scan_decimal(const char *a_str_decimal)
{
    char l_buf [DATOSHI_POW + 2] = {0};
    uint128_t l_ret = uint128_0, l_nul = uint128_0;

    if (strlen(a_str_decimal) > DATOSHI_POW + 1) {
        log_it(L_WARNING, "Incorrect balance format - too long");
        return l_nul;
    }

    strcpy(l_buf, a_str_decimal);
    char *l_point = strchr(l_buf, '.');
    int l_tail = 0;
    int l_pos = strlen(l_buf);
    if (l_point) {
        l_tail = l_pos - 1 - (l_point - l_buf);
        l_pos = l_point - l_buf;
        if (l_tail > DATOSHI_DEGREE) {
            log_it(L_WARNING, "Incorrect balance format - too much precision");
            return l_nul;
        }
        while (l_buf[l_pos]) {
            l_buf[l_pos] = l_buf[l_pos + 1];
            l_pos++;
        }
        l_pos--;
    }
    if (l_pos + DATOSHI_DEGREE - l_tail > DATOSHI_POW) {
        log_it(L_WARNING, "Incorrect balance format - too long with point");
        return l_nul;
    }
    int i;
    for (i = 0; i < DATOSHI_DEGREE - l_tail; i++) {
        l_buf[l_pos + i] = '0';
    }
    l_buf[l_pos + i] = '\0';
    l_ret = dap_uint128_scan_uninteger(l_buf);

    return l_ret;
}

double dap_uint256_decimal_to_double(uint256_t a_decimal){
    const char *l_str = NULL;
    dap_uint256_to_char(a_decimal, &l_str);
    return strtod(l_str, NULL);
}
