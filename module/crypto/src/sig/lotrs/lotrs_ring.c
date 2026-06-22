/*
 * LoTRS — polynomial ring R_q = Z_q[X]/(X^d + 1) arithmetic.
 *
 * Schoolbook for d=32; NTT fast path for d=128 (future).
 */

#include "lotrs_ring.h"

#include <errno.h>
#include <string.h>

#define LOG_TAG "lotrs_ring"
#include "dap_common.h"
#include "dap_memwipe.h"
#include "dap_serialize.h"
#include "lotrs_codec.h"

/* --- Polynomial schema (dap_serialize) --- */

static const dap_serialize_field_t s_lotrs_poly_fields[] = {
    {
        .name = "coeffs",
        .type = DAP_SERIALIZE_TYPE_ARRAY_FIXED,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(lotrs_poly_t, coeffs),
        .size = sizeof(uint64_t),
        .fixed_count = LOTRS_D_MAX,
        .element_type = DAP_SERIALIZE_TYPE_UINT64,
    },
};

DAP_SERIALIZE_SCHEMA_DEFINE(lotrs_poly_schema,
                            lotrs_poly_t,
                            s_lotrs_poly_fields);

/* --- Allocation --- */

lotrs_poly_t *lotrs_poly_alloc(const lotrs_params_t *a_par)
{
    (void)a_par;
    return DAP_NEW_Z(lotrs_poly_t);
}

void lotrs_poly_free(lotrs_poly_t *a_p)
{
    if (a_p) {
        DAP_DELETE(a_p);
    }
}

lotrs_polyvec_t lotrs_polyvec_alloc(const lotrs_params_t *a_par, uint32_t a_n)
{
    lotrs_polyvec_t l_v = { .polys = NULL, .n = a_n };
    l_v.polys = DAP_NEW_Z_COUNT(lotrs_poly_t *, a_n);
    if (!l_v.polys) {
        l_v.n = 0;
        return l_v;
    }
    for (uint32_t i = 0u; i < a_n; ++i) {
        l_v.polys[i] = lotrs_poly_alloc(a_par);
        if (!l_v.polys[i]) {
            for (uint32_t j = 0u; j < i; ++j) DAP_DELETE(l_v.polys[j]);
            DAP_DELETE(l_v.polys);
            l_v.polys = NULL;
            l_v.n = 0;
            break;
        }
    }
    return l_v;
}

lotrs_polymat_t lotrs_polymat_alloc(const lotrs_params_t *a_par,
                                    uint32_t a_nrows, uint32_t a_ncols)
{
    lotrs_polymat_t l_m = { .rows = NULL, .nrows = a_nrows, .ncols = a_ncols };
    l_m.rows = DAP_NEW_Z_COUNT(lotrs_polyvec_t, a_nrows);
    if (!l_m.rows) {
        l_m.nrows = 0;
        return l_m;
    }
    for (uint32_t i = 0u; i < a_nrows; ++i) {
        l_m.rows[i] = lotrs_polyvec_alloc(a_par, a_ncols);
        if (!l_m.rows[i].polys) {
            for (uint32_t j = 0u; j < i; ++j) lotrs_polyvec_free(&l_m.rows[j]);
            DAP_DELETE(l_m.rows);
            l_m.rows = NULL;
            l_m.nrows = 0;
            break;
        }
    }
    return l_m;
}

void lotrs_polyvec_free(lotrs_polyvec_t *a_v)
{
    if (a_v && a_v->polys) {
        for (uint32_t i = 0u; i < a_v->n; ++i) {
            lotrs_poly_free(a_v->polys[i]);
        }
        DAP_DELETE(a_v->polys);
        a_v->polys = NULL;
        a_v->n = 0;
    }
}

void lotrs_polymat_free(lotrs_polymat_t *a_m)
{
    if (a_m && a_m->rows) {
        for (uint32_t i = 0u; i < a_m->nrows; ++i) {
            lotrs_polyvec_free(&a_m->rows[i]);
        }
        DAP_DELETE(a_m->rows);
        a_m->rows = NULL;
        a_m->nrows = 0;
    }
}

/* --- Ring operations --- */

void lotrs_poly_zero(lotrs_poly_t *a_p, const lotrs_params_t *a_par)
{
    memset(a_p->coeffs, 0, a_par->d * sizeof(uint64_t));
}

void lotrs_poly_copy(lotrs_poly_t *a_dst, const lotrs_poly_t *a_src,
                     const lotrs_params_t *a_par)
{
    memcpy(a_dst->coeffs, a_src->coeffs, a_par->d * sizeof(uint64_t));
}

static uint64_t s_mod_add(uint64_t a, uint64_t b, uint64_t q)
{
    uint64_t l_s = a + b;
    return l_s >= q ? l_s - q : l_s;
}

static uint64_t s_mod_sub(uint64_t a, uint64_t b, uint64_t q)
{
    return a >= b ? a - b : a + q - b;
}

void lotrs_poly_add(lotrs_poly_t *a_out, const lotrs_poly_t *a_a,
                    const lotrs_poly_t *a_b, const lotrs_params_t *a_par)
{
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        a_out->coeffs[i] = s_mod_add(a_a->coeffs[i], a_b->coeffs[i], a_par->q);
    }
}

void lotrs_poly_sub(lotrs_poly_t *a_out, const lotrs_poly_t *a_a,
                    const lotrs_poly_t *a_b, const lotrs_params_t *a_par)
{
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        a_out->coeffs[i] = s_mod_sub(a_a->coeffs[i], a_b->coeffs[i], a_par->q);
    }
}

void lotrs_poly_neg(lotrs_poly_t *a_out, const lotrs_poly_t *a_a,
                    const lotrs_params_t *a_par)
{
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        a_out->coeffs[i] = a_a->coeffs[i] == 0 ? 0 : a_par->q - a_a->coeffs[i];
    }
}

void lotrs_poly_mul(lotrs_poly_t *a_out, const lotrs_poly_t *a_a,
                    const lotrs_poly_t *a_b, const lotrs_params_t *a_par)
{
    const uint32_t l_d = a_par->d;
    const int64_t l_q = (int64_t)a_par->q;

    /* Use __int128 accumulator to avoid precision loss. */
    __int128_t *l_tmp = DAP_NEW_Z_SIZE(__int128_t, l_d * sizeof(__int128_t));
    if (!l_tmp) return;

    for (uint32_t i = 0u; i < l_d; ++i) {
        for (uint32_t j = 0u; j < l_d; ++j) {
            uint32_t k = i + j;
            __int128_t l_prod = (__int128_t)(int64_t)a_a->coeffs[i]
                              * (__int128_t)(int64_t)a_b->coeffs[j];

            if (k < l_d) {
                l_tmp[k] += l_prod;
            } else {
                l_tmp[k - l_d] -= l_prod;
            }
        }
    }

    for (uint32_t i = 0u; i < l_d; ++i) {
        int64_t l_r = (int64_t)(l_tmp[i] % l_q);
        if (l_r < 0) l_r += l_q;
        a_out->coeffs[i] = (uint64_t)l_r;
    }

    DAP_DELETE(l_tmp);
}

void lotrs_poly_scalar_mul(lotrs_poly_t *a_out, uint64_t a_scalar,
                           const lotrs_poly_t *a_a, const lotrs_params_t *a_par)
{
    const uint64_t l_q = a_par->q;
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        __uint128_t l_prod = (__uint128_t)a_a->coeffs[i] * a_scalar;
        a_out->coeffs[i] = (uint64_t)(l_prod % l_q);
    }
}

/* --- Vector operations --- */

void lotrs_polyvec_zero(lotrs_polyvec_t *a_v, const lotrs_params_t *a_par)
{
    for (uint32_t i = 0u; i < a_v->n; ++i) {
        lotrs_poly_zero(a_v->polys[i], a_par);
    }
}

void lotrs_polyvec_add(lotrs_polyvec_t *a_out, const lotrs_polyvec_t *a_a,
                       const lotrs_polyvec_t *a_b, const lotrs_params_t *a_par)
{
    for (uint32_t i = 0u; i < a_out->n; ++i) {
        lotrs_poly_add(a_out->polys[i], a_a->polys[i], a_b->polys[i], a_par);
    }
}

void lotrs_polyvec_sub(lotrs_polyvec_t *a_out, const lotrs_polyvec_t *a_a,
                       const lotrs_polyvec_t *a_b, const lotrs_params_t *a_par)
{
    for (uint32_t i = 0u; i < a_out->n; ++i) {
        lotrs_poly_sub(a_out->polys[i], a_a->polys[i], a_b->polys[i], a_par);
    }
}

void lotrs_polymat_vecmul(lotrs_polyvec_t *a_out, const lotrs_polymat_t *a_A,
                          const lotrs_polyvec_t *a_x, const lotrs_params_t *a_par)
{
    const uint32_t l_d = a_par->d;
    const int64_t l_q = (int64_t)a_par->q;

    for (uint32_t i = 0u; i < a_A->nrows; ++i) {
        /* Use __int128 accumulator for exact computation. */
        __int128_t *l_acc = DAP_NEW_Z_SIZE(__int128_t, l_d * sizeof(__int128_t));
        if (!l_acc) return;

        for (uint32_t j = 0u; j < a_A->ncols; ++j) {
            lotrs_poly_t *l_tmp = lotrs_poly_alloc(a_par);
            if (!l_tmp) { DAP_DELETE(l_acc); return; }
            lotrs_poly_mul(l_tmp, a_A->rows[i].polys[j], a_x->polys[j], a_par);
            for (uint32_t k = 0u; k < l_d; ++k) {
                l_acc[k] += (int64_t)l_tmp->coeffs[k];
            }
            lotrs_poly_free(l_tmp);
        }

        /* Reduce mod q. */
        for (uint32_t k = 0u; k < l_d; ++k) {
            int64_t l_r = (int64_t)(l_acc[k] % l_q);
            if (l_r < 0) l_r += l_q;
            a_out->polys[i]->coeffs[k] = (uint64_t)l_r;
        }
        DAP_DELETE(l_acc);
    }
}

/* --- Centered representation --- */

int64_t lotrs_center(uint64_t a_a, uint64_t a_q)
{
    /* Branchless: subtract q if a > q/2. */
    int64_t l_v = (int64_t)a_a;
    int64_t l_half = (int64_t)(a_q / 2u);
    /* mask = all-ones if l_v > l_half, 0 otherwise. */
    int64_t l_diff = l_half - l_v;
    int64_t l_mask = l_diff >> 63; /* -1 if l_diff < 0 (i.e., l_v > l_half) */
    l_v -= l_mask & (int64_t)a_q;
    return l_v;
}

/* --- Serialization (dap_serialize) --- */

/* Bytes per coefficient: ceil(ceil(log2(q)) / 8). */
static inline uint32_t s_coeff_bytes(uint64_t a_q)
{
    if (a_q <= (1ULL << 8))  return 1u;
    if (a_q <= (1ULL << 16)) return 2u;
    if (a_q <= (1ULL << 24)) return 3u;
    if (a_q <= (1ULL << 32)) return 4u;
    if (a_q <= (1ULL << 40)) return 5u;
    if (a_q <= (1ULL << 48)) return 6u;
    if (a_q <= (1ULL << 56)) return 7u;
    return 8u;
}

size_t lotrs_poly_bytes(const lotrs_params_t *a_par)
{
    return (size_t)a_par->d * s_coeff_bytes(a_par->q);
}

int lotrs_poly_pack(uint8_t *a_out, size_t a_out_len,
                    const lotrs_poly_t *a_p, const lotrs_params_t *a_par)
{
    const uint32_t l_cb = s_coeff_bytes(a_par->q);
    const size_t l_total = (size_t)a_par->d * l_cb;
    if (a_out_len < l_total) return -EINVAL;

    if (l_cb == 8u) {
        /* Fast path: raw 8-byte LE copy. */
        return dap_serialize_ptr_to_buffer(a_p->coeffs, l_total, a_out, a_out_len);
    }

    /* Compact path: encode each coefficient as l_cb bytes LE. */
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        uint64_t v = a_p->coeffs[i];
        for (uint32_t b = 0u; b < l_cb; ++b) {
            a_out[i * l_cb + b] = (uint8_t)(v >> (8u * b));
        }
    }
    return 0;
}

int lotrs_poly_unpack(lotrs_poly_t *a_p, const uint8_t *a_in, size_t a_in_len,
                      const lotrs_params_t *a_par)
{
    const uint32_t l_cb = s_coeff_bytes(a_par->q);
    const size_t l_total = (size_t)a_par->d * l_cb;
    if (a_in_len < l_total) return -EINVAL;

    if (l_cb == 8u) {
        /* Fast path: raw 8-byte LE copy. */
        int l_rc = dap_serialize_ptr_from_buffer(a_in, a_in_len, a_p->coeffs, l_total);
        if (l_rc != 0) return l_rc;
    } else {
        /* Compact path: decode l_cb bytes LE per coefficient. */
        for (uint32_t i = 0u; i < a_par->d; ++i) {
            uint64_t v = 0u;
            for (uint32_t b = 0u; b < l_cb; ++b) {
                v |= (uint64_t)a_in[i * l_cb + b] << (8u * b);
            }
            a_p->coeffs[i] = v;
        }
    }

    /* Reduce mod q. */
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        a_p->coeffs[i] %= a_par->q;
    }
    return 0;
}

/* --- Golomb-Rice compact serialization --- */

int lotrs_poly_pack_rice(uint8_t *a_out, size_t a_out_cap,
                         const lotrs_poly_t *a_p, const lotrs_params_t *a_par,
                         uint32_t a_rice_k, int64_t a_bound,
                         size_t *a_bytes_written)
{
    /* Convert uint64 coefficients to signed centered form for encoding. */
    int64_t *l_centered = DAP_NEW_Z_SIZE(int64_t, a_par->d * sizeof(int64_t));
    if (!l_centered) return -ENOMEM;
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        l_centered[i] = lotrs_center(a_p->coeffs[i], a_par->q);
    }
    int l_rc = lotrs_rice_pack(a_out, a_out_cap, l_centered, a_par->d,
                               a_rice_k, a_bound, a_bytes_written);
    DAP_DELETE(l_centered);
    return l_rc;
}

int lotrs_poly_unpack_rice(lotrs_poly_t *a_p,
                           const uint8_t *a_in, size_t a_in_len,
                           const lotrs_params_t *a_par,
                           uint32_t a_rice_k, int64_t a_bound,
                           size_t *a_bytes_consumed)
{
    int64_t *l_centered = DAP_NEW_Z_SIZE(int64_t, a_par->d * sizeof(int64_t));
    if (!l_centered) return -ENOMEM;
    int l_rc = lotrs_rice_unpack(l_centered, a_par->d, a_in, a_in_len,
                                 a_rice_k, a_bound, a_bytes_consumed);
    if (l_rc != 0) {
        DAP_DELETE(l_centered);
        return l_rc;
    }
    /* Convert signed centered to [0, q). */
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        int64_t v = l_centered[i] % (int64_t)a_par->q;
        if (v < 0) v += (int64_t)a_par->q;
        a_p->coeffs[i] = (uint64_t)v;
    }
    DAP_DELETE(l_centered);
    return 0;
}

size_t lotrs_polyvec_bytes(const lotrs_params_t *a_par, uint32_t a_n)
{
    return (size_t)a_n * lotrs_poly_bytes(a_par);
}

int lotrs_polyvec_pack(uint8_t *a_out, size_t a_out_len,
                       const lotrs_polyvec_t *a_v, const lotrs_params_t *a_par)
{
    const size_t l_per = lotrs_poly_bytes(a_par);
    if (a_out_len < (size_t)a_v->n * l_per) return -EINVAL;
    for (uint32_t i = 0u; i < a_v->n; ++i) {
        int l_rc = lotrs_poly_pack(a_out + i * l_per, l_per, a_v->polys[i], a_par);
        if (l_rc != 0) return l_rc;
    }
    return 0;
}

int lotrs_polyvec_unpack(lotrs_polyvec_t *a_v, const uint8_t *a_in, size_t a_in_len,
                         const lotrs_params_t *a_par)
{
    const size_t l_per = lotrs_poly_bytes(a_par);
    if (a_in_len < (size_t)a_v->n * l_per) return -EINVAL;
    for (uint32_t i = 0u; i < a_v->n; ++i) {
        int l_rc = lotrs_poly_unpack(a_v->polys[i], a_in + i * l_per, l_per, a_par);
        if (l_rc != 0) return l_rc;
    }
    return 0;
}
