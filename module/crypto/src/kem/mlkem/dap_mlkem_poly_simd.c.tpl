/**
 * @file dap_mlkem_poly_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} SIMD-optimized ML-KEM polynomial helpers.
 * @details Generated from dap_mlkem_poly_simd.c.tpl by dap_tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
{{ARCH_INCLUDES}}

{{#include PRIMITIVES_FILE}}

#include "dap_mlkem_poly_simd.h"

#define MLKEM_Q     3329
#define MLKEM_QINV  62209
#define MLKEM_N     256

{{TARGET_ATTR}}
static inline VEC_T
s_fqmul_vec(VEC_T a_a, VEC_T a_b, VEC_T a_qinv, VEC_T a_q)
{
    VEC_T l_lo = VEC_MULLO16(a_a, a_b);
    VEC_T l_hi = VEC_MULHI16(a_a, a_b);
    VEC_T l_u  = VEC_MULLO16(l_lo, a_qinv);
    VEC_T l_uq = VEC_MULHI16(l_u, a_q);
    return VEC_SUB16(l_hi, l_uq);
}

/* ============================================================================
 * poly_csubq
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_csubq_{{ARCH_LOWER}}(int16_t *a_coeffs)
{
    const VEC_T l_q = VEC_SET1_16(MLKEM_Q);
    for (unsigned i = 0; i < MLKEM_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        v = VEC_SUB16(v, l_q);
        VEC_T mask = VEC_SRAI16(v, 15);
        v = VEC_ADD16(v, VEC_AND(mask, l_q));
        VEC_STORE(a_coeffs + i, v);
    }
}

/* ============================================================================
 * poly_reduce: Barrett reduction
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_reduce_{{ARCH_LOWER}}(int16_t *a_coeffs)
{
    const VEC_T l_v = VEC_SET1_16(20159);
    const VEC_T l_q = VEC_SET1_16(MLKEM_Q);
    for (unsigned i = 0; i < MLKEM_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_T t = VEC_MULHI16(v, l_v);
        t = VEC_SRAI16(t, 10);
        v = VEC_SUB16(v, VEC_MULLO16(t, l_q));
        VEC_STORE(a_coeffs + i, v);
    }
}

/* ============================================================================
 * poly_tomont
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_tomont_{{ARCH_LOWER}}(int16_t *a_coeffs)
{
    const VEC_T l_f    = VEC_SET1_16((int16_t)((1ULL << 32) % MLKEM_Q));
    const VEC_T l_qinv = VEC_SET1_16((int16_t)MLKEM_QINV);
    const VEC_T l_q    = VEC_SET1_16(MLKEM_Q);
    for (unsigned i = 0; i < MLKEM_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        v = s_fqmul_vec(v, l_f, l_qinv, l_q);
        VEC_STORE(a_coeffs + i, v);
    }
}

/* ============================================================================
 * poly_add / poly_sub
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_add_{{ARCH_LOWER}}(int16_t * restrict a_r,
                                         const int16_t * restrict a_a,
                                         const int16_t * restrict a_b)
{
    for (unsigned i = 0; i < MLKEM_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T vb = VEC_LOAD(a_b + i);
        VEC_STORE(a_r + i, VEC_ADD16(va, vb));
    }
}

{{TARGET_ATTR}}
void dap_mlkem_poly_sub_{{ARCH_LOWER}}(int16_t * restrict a_r,
                                         const int16_t * restrict a_a,
                                         const int16_t * restrict a_b)
{
    for (unsigned i = 0; i < MLKEM_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T vb = VEC_LOAD(a_b + i);
        VEC_STORE(a_r + i, VEC_SUB16(va, vb));
    }
}

/* ============================================================================
 * basemul_montgomery: NTT-domain polynomial multiply
 *
 * Pre-computed zeta table eliminates per-iteration stack buffer.
 * Layout: [+z, +z, -z, -z] repeated for each zeta, 64 zetas × 4 = 256 entries.
 * ============================================================================ */

static const int16_t s_basemul_zetas_expanded[256] = {
    /* zetas[64..127] expanded to [+z, +z, -z, -z] pattern */
     2226,  2226, -2226, -2226,   430,   430,  -430,  -430,
      555,   555,  -555,  -555,   843,   843,  -843,  -843,
     2078,  2078, -2078, -2078,   871,   871,  -871,  -871,
     1550,  1550, -1550, -1550,   105,   105,  -105,  -105,
      422,   422,  -422,  -422,   587,   587,  -587,  -587,
      177,   177,  -177,  -177,  3094,  3094, -3094, -3094,
     3038,  3038, -3038, -3038,  2869,  2869, -2869, -2869,
     1574,  1574, -1574, -1574,  1653,  1653, -1653, -1653,
     3083,  3083, -3083, -3083,   778,   778,  -778,  -778,
     1159,  1159, -1159, -1159,  3182,  3182, -3182, -3182,
     2552,  2552, -2552, -2552,  1483,  1483, -1483, -1483,
     2727,  2727, -2727, -2727,  1119,  1119, -1119, -1119,
     1739,  1739, -1739, -1739,   644,   644,  -644,  -644,
     2457,  2457, -2457, -2457,   349,   349,  -349,  -349,
      418,   418,  -418,  -418,   329,   329,  -329,  -329,
     3173,  3173, -3173, -3173,  3254,  3254, -3254, -3254,
      817,   817,  -817,  -817,  1097,  1097, -1097, -1097,
      603,   603,  -603,  -603,   610,   610,  -610,  -610,
     1322,  1322, -1322, -1322,  2044,  2044, -2044, -2044,
     1864,  1864, -1864, -1864,   384,   384,  -384,  -384,
     2114,  2114, -2114, -2114,  3193,  3193, -3193, -3193,
     1218,  1218, -1218, -1218,  1994,  1994, -1994, -1994,
     2455,  2455, -2455, -2455,   220,   220,  -220,  -220,
     2142,  2142, -2142, -2142,  1670,  1670, -1670, -1670,
     2144,  2144, -2144, -2144,  1799,  1799, -1799, -1799,
     2051,  2051, -2051, -2051,   794,   794,  -794,  -794,
     1819,  1819, -1819, -1819,  2475,  2475, -2475, -2475,
     2459,  2459, -2459, -2459,   478,   478,  -478,  -478,
     3221,  3221, -3221, -3221,  3021,  3021, -3021, -3021,
      996,   996,  -996,  -996,   991,   991,  -991,  -991,
      958,   958,  -958,  -958,  1869,  1869, -1869, -1869,
     1522,  1522, -1522, -1522,  1628,  1628, -1628, -1628,
};

{{TARGET_ATTR}} __attribute__((optimize("Os"), noinline))
void dap_mlkem_poly_basemul_montgomery_{{ARCH_LOWER}}(
    int16_t *a_r, const int16_t *a_a, const int16_t *a_b, const int16_t *a_zetas)
{
    (void)a_zetas;
    const VEC_T l_qinv = VEC_SET1_16((int16_t)MLKEM_QINV);
    const VEC_T l_q    = VEC_SET1_16(MLKEM_Q);

    for (unsigned i = 0; i < MLKEM_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T vb = VEC_LOAD(a_b + i);
        VEC_T vz = VEC_LOAD(s_basemul_zetas_expanded + i);

        VEC_T vb_swap = VEC_SWAP_ADJACENT16(vb);
        VEC_T diag    = s_fqmul_vec(va, vb, l_qinv, l_q);
        VEC_T cross   = s_fqmul_vec(va, vb_swap, l_qinv, l_q);

        VEC_T diag_z  = s_fqmul_vec(diag, vz, l_qinv, l_q);
        VEC_T diag_zs = VEC_SWAP_ADJACENT16(diag_z);

        VEC_T r_diag  = VEC_ADD16(diag, diag_zs);
        VEC_T r_cross = VEC_ADD16(cross, VEC_SWAP_ADJACENT16(cross));

        VEC_STORE(a_r + i, VEC_BLEND_ODD(r_diag, r_cross));
    }
}

/* ============================================================================
 * basemul_acc_montgomery: fused K basemul + accumulate + Barrett reduce
 *
 * Computes r = sum(basemul(a[k], b[k])) for k=0..count-1 with inline reduce.
 * Eliminates K-1 intermediate load/store passes vs separate basemul + add.
 * ============================================================================ */

{{TARGET_ATTR}} __attribute__((optimize("Os"), noinline))
void dap_mlkem_poly_basemul_acc_montgomery_{{ARCH_LOWER}}(
    int16_t *a_r,
    const int16_t * const *a_polys_a,
    const int16_t * const *a_polys_b,
    unsigned a_count)
{
    const VEC_T l_qinv = VEC_SET1_16((int16_t)MLKEM_QINV);
    const VEC_T l_q    = VEC_SET1_16(MLKEM_Q);
    const VEC_T l_bv   = VEC_SET1_16(20159);

    for (unsigned i = 0; i < MLKEM_N; i += VEC_LANES) {
        VEC_T l_acc = VEC_ZERO();
        VEC_T vz = VEC_LOAD(s_basemul_zetas_expanded + i);

        for (unsigned k = 0; k < a_count; k++) {
            VEC_T va = VEC_LOAD(a_polys_a[k] + i);
            VEC_T vb = VEC_LOAD(a_polys_b[k] + i);

            VEC_T vb_swap = VEC_SWAP_ADJACENT16(vb);
            VEC_T diag    = s_fqmul_vec(va, vb, l_qinv, l_q);
            VEC_T cross   = s_fqmul_vec(va, vb_swap, l_qinv, l_q);

            VEC_T diag_z  = s_fqmul_vec(diag, vz, l_qinv, l_q);
            VEC_T diag_zs = VEC_SWAP_ADJACENT16(diag_z);

            VEC_T r_diag  = VEC_ADD16(diag, diag_zs);
            VEC_T r_cross = VEC_ADD16(cross, VEC_SWAP_ADJACENT16(cross));

            l_acc = VEC_ADD16(l_acc, VEC_BLEND_ODD(r_diag, r_cross));
        }

        VEC_T l_bt = VEC_SRAI16(VEC_MULHI16(l_acc, l_bv), 10);
        VEC_STORE(a_r + i, VEC_SUB16(l_acc, VEC_MULLO16(l_bt, l_q)));
    }
}

/* ============================================================================
 * compress_coeffs: round(x * 2^d / q) via mulhrs approximation
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_compress_coeffs_{{ARCH_LOWER}}(int16_t *a_coeffs,
                                                     int16_t a_magic,
                                                     int16_t a_mask)
{
    const VEC_T l_c = VEC_SET1_16(a_magic);
    const VEC_T l_m = VEC_SET1_16(a_mask);
    for (unsigned i = 0; i < MLKEM_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        v = VEC_AND(VEC_MULHRS16(v, l_c), l_m);
        VEC_STORE(a_coeffs + i, v);
    }
}

