/**
 * @file dap_mlkem_poly_ops_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} poly I/O operations for ML-KEM (compress, serialize, mulcache).
 * @details Generated from dap_mlkem_poly_ops_simd.c.tpl by dap_tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
#include <string.h>
{{ARCH_INCLUDES}}

#define MLKEM_Q     3329
#define MLKEM_QINV  ((int16_t)-3327)
#define MLKEM_N     256

static inline int16_t s_fqmul_scalar(int16_t a, int16_t b)
{
    int32_t t = (int32_t)a * b;
    int16_t u = (int16_t)t * MLKEM_QINV;
    return (int16_t)((t - (int32_t)u * MLKEM_Q) >> 16);
}

static const int16_t s_basemul_zetas_nttpack[128] = {
     2226, -2226,   430,  -430,   555,  -555,   843,  -843,
     2078, -2078,   871,  -871,  1550, -1550,   105,  -105,
      422,  -422,   587,  -587,   177,  -177,  3094, -3094,
     3038, -3038,  2869, -2869,  1574, -1574,  1653, -1653,
     3083, -3083,   778,  -778,  1159, -1159,  3182, -3182,
     2552, -2552,  1483, -1483,  2727, -2727,  1119, -1119,
     1739, -1739,   644,  -644,  2457, -2457,   349,  -349,
      418,  -418,   329,  -329,  3173, -3173,  3254, -3254,
      817,  -817,  1097, -1097,   603,  -603,   610,  -610,
     1322, -1322,  2044, -2044,  1864, -1864,   384,  -384,
     2114, -2114,  3193, -3193,  1218, -1218,  1994, -1994,
     2455, -2455,   220,  -220,  2142, -2142,  1670, -1670,
     2144, -2144,  1799, -1799,  2051, -2051,   794,  -794,
     1819, -1819,  2475, -2475,  2459, -2459,   478,  -478,
     3221, -3221,  3021, -3021,   996,  -996,   991,  -991,
      958,  -958,  1869, -1869,  1522, -1522,  1628, -1628,
};

{{#include IMPL_FILE}}
