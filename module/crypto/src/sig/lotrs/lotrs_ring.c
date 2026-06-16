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

/* --- Allocation --- */

lotrs_poly_t *lotrs_poly_alloc(const lotrs_params_t *a_par)
{
    lotrs_poly_t *l_p = DAP_NEW_Z(lotrs_poly_t);
    if (!l_p) return NULL;
    l_p->coeffs = DAP_NEW_Z_SIZE(uint64_t, a_par->d * sizeof(uint64_t));
    if (!l_p->coeffs) {
        DAP_DELETE(l_p);
        return NULL;
    }
    return l_p;
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

void lotrs_poly_free(lotrs_poly_t *a_p)
{
    if (a_p) {
        if (a_p->coeffs) {
            /* Wipe before freeing — coefficients may be secret. */
            DAP_DELETE(a_p->coeffs);
        }
        DAP_DELETE(a_p);
    }
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

/*
 * Negacyclic schoolbook multiplication: out = a * b mod (X^d + 1, q).
 * O(d^2) — fine for d=32.
 */
void lotrs_poly_mul(lotrs_poly_t *a_out, const lotrs_poly_t *a_a,
                    const lotrs_poly_t *a_b, const lotrs_params_t *a_par)
{
    const uint32_t l_d = a_par->d;
    const uint64_t l_q = a_par->q;

    uint64_t *l_tmp = DAP_NEW_Z_SIZE(uint64_t, l_d * sizeof(uint64_t));
    if (!l_tmp) return;

    for (uint32_t i = 0u; i < l_d; ++i) {
        for (uint32_t j = 0u; j < l_d; ++j) {
            uint32_t k = i + j;
            __uint128_t l_prod = (__uint128_t)a_a->coeffs[i] * a_b->coeffs[j];
            uint64_t l_lo = (uint64_t)l_prod;
            uint64_t l_hi = (uint64_t)(l_prod >> 64);

            if (k < l_d) {
                l_tmp[k] += l_lo;
                if (l_tmp[k] < l_lo) l_hi++;
                if (k + 1 < l_d) l_tmp[k + 1] += l_hi;
            } else {
                uint32_t l_kk = k - l_d;
                l_tmp[l_kk] -= l_lo;
                if (l_tmp[l_kk] > UINT64_MAX - l_lo) l_hi++;
                if (l_kk > 0) l_tmp[l_kk - 1] -= l_hi;
            }
        }
    }

    for (uint32_t i = 0u; i < l_d; ++i) {
        int64_t l_v = (int64_t)l_tmp[i];
        int64_t l_r = l_v % (int64_t)l_q;
        if (l_r < 0) l_r += (int64_t)l_q;
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

/* out = A * x (matrix-vector). */
void lotrs_polymat_vecmul(lotrs_polyvec_t *a_out, const lotrs_polymat_t *a_A,
                          const lotrs_polyvec_t *a_x, const lotrs_params_t *a_par)
{
    for (uint32_t i = 0u; i < a_A->nrows; ++i) {
        lotrs_poly_zero(a_out->polys[i], a_par);
        for (uint32_t j = 0u; j < a_A->ncols; ++j) {
            lotrs_poly_t *l_tmp = lotrs_poly_alloc(a_par);
            if (!l_tmp) return;
            lotrs_poly_mul(l_tmp, a_A->rows[i].polys[j], a_x->polys[j], a_par);
            for (uint32_t k = 0u; k < a_par->d; ++k) {
                a_out->polys[i]->coeffs[k] =
                    s_mod_add(a_out->polys[i]->coeffs[k],
                              l_tmp->coeffs[k], a_par->q);
            }
            lotrs_poly_free(l_tmp);
        }
    }
}

/* --- Centered representation --- */

int64_t lotrs_center(uint64_t a_a, uint64_t a_q)
{
    int64_t l_v = (int64_t)a_a;
    int64_t l_half = (int64_t)(a_q / 2u);
    if (l_v > l_half) l_v -= (int64_t)a_q;
    return l_v;
}

/* --- Serialization --- */

size_t lotrs_poly_bytes(const lotrs_params_t *a_par)
{
    return (size_t)a_par->d * 8u;
}

int lotrs_poly_pack(uint8_t *a_out, size_t a_out_len,
                    const lotrs_poly_t *a_p, const lotrs_params_t *a_par)
{
    const size_t l_total = lotrs_poly_bytes(a_par);
    return dap_serialize_ptr_to_buffer(a_p->coeffs, l_total, a_out, a_out_len);
}

int lotrs_poly_unpack(lotrs_poly_t *a_p, const uint8_t *a_in, size_t a_in_len,
                      const lotrs_params_t *a_par)
{
    const size_t l_total = lotrs_poly_bytes(a_par);
    if (a_in_len < l_total) return -EINVAL;
    int l_rc = dap_serialize_ptr_from_buffer(a_in, a_in_len, a_p->coeffs, l_total);
    if (l_rc != 0) return l_rc;
    /* Reduce mod q. */
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        a_p->coeffs[i] %= a_par->q;
    }
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
