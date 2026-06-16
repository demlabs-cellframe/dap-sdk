/*
 * LoTRS — XOF-based samplers.
 *
 * Uses SHA3-256 for deterministic challenge generation.
 */

#include "lotrs_sample.h"

#include <errno.h>
#include <string.h>

#define LOG_TAG "lotrs_sample"
#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_memwipe.h"

struct lotrs_xof {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
    uint64_t counter;
};

lotrs_xof_t *lotrs_xof_new(const uint8_t *a_seed, size_t a_seed_len)
{
    lotrs_xof_t *l_xof = DAP_NEW_Z(lotrs_xof_t);
    if (!l_xof) return NULL;
    l_xof->cap = a_seed_len + 256u;
    l_xof->buf = DAP_NEW_Z_SIZE(uint8_t, l_xof->cap);
    if (!l_xof->buf) { DAP_DELETE(l_xof); return NULL; }
    if (a_seed && a_seed_len > 0u) {
        memcpy(l_xof->buf, a_seed, a_seed_len);
        l_xof->len = a_seed_len;
    }
    return l_xof;
}

void lotrs_xof_free(lotrs_xof_t *a_xof)
{
    if (a_xof) {
        if (a_xof->buf) {
            dap_memwipe(a_xof->buf, a_xof->cap);
            DAP_DELETE(a_xof->buf);
        }
        DAP_DELETE(a_xof);
    }
}

void lotrs_xof_absorb(lotrs_xof_t *a_xof, const uint8_t *a_data, size_t a_len)
{
    while (a_xof->len + a_len > a_xof->cap) {
        a_xof->cap = a_xof->cap * 2u + a_len;
        uint8_t *l_new = DAP_NEW_Z_SIZE(uint8_t, a_xof->cap);
        if (l_new) {
            memcpy(l_new, a_xof->buf, a_xof->len);
            DAP_DELETE(a_xof->buf);
            a_xof->buf = l_new;
        }
    }
    memcpy(a_xof->buf + a_xof->len, a_data, a_len);
    a_xof->len += a_len;
    a_xof->counter = 0u;
}

void lotrs_xof_squeeze(lotrs_xof_t *a_xof, uint8_t *a_out, size_t a_out_len)
{
    size_t l_total = a_xof->len + 8u;
    uint8_t *l_input = DAP_NEW_Z_SIZE(uint8_t, l_total);
    if (!l_input) return;
    memcpy(l_input, a_xof->buf, a_xof->len);

    for (int i = 0; i < 8; ++i) {
        l_input[a_xof->len + i] = (uint8_t)(a_xof->counter >> (8u * i));
    }

    size_t l_done = 0u;
    while (l_done < a_out_len) {
        dap_hash_sha3_256_t l_h;
        dap_hash_sha3_256(l_input, l_total, &l_h);
        size_t l_chunk = (a_out_len - l_done < 32u) ? a_out_len - l_done : 32u;
        memcpy(a_out + l_done, l_h.raw, l_chunk);
        l_done += l_chunk;
        a_xof->counter++;
        for (int i = 0; i < 8; ++i) {
            l_input[a_xof->len + i] = (uint8_t)(a_xof->counter >> (8u * i));
        }
    }

    DAP_DELETE(l_input);
}

int lotrs_sample_uniform(lotrs_poly_t *a_out, lotrs_xof_t *a_xof,
                         const lotrs_params_t *a_par)
{
    const uint64_t l_q = a_par->q;
    const uint64_t l_threshold = UINT64_MAX - (UINT64_MAX % l_q);

    for (uint32_t i = 0u; i < a_par->d; ) {
        uint8_t l_buf[8];
        lotrs_xof_squeeze(a_xof, l_buf, 8u);
        uint64_t l_v = 0;
        for (int b = 0; b < 8; ++b) {
            l_v |= (uint64_t)l_buf[b] << (8u * b);
        }
        if (l_v < l_threshold) {
            a_out->coeffs[i] = l_v % l_q;
            ++i;
        }
    }
    return 0;
}

int lotrs_sample_short(lotrs_poly_t *a_out, lotrs_xof_t *a_xof,
                       const lotrs_params_t *a_par, uint32_t a_eta)
{
    const uint32_t l_range = 2u * a_eta + 1u;
    const uint64_t l_threshold = UINT64_MAX - (UINT64_MAX % l_range);

    for (uint32_t i = 0u; i < a_par->d; ) {
        uint8_t l_buf[8];
        lotrs_xof_squeeze(a_xof, l_buf, 8u);
        uint64_t l_v = 0;
        for (int b = 0; b < 8; ++b) {
            l_v |= (uint64_t)l_buf[b] << (8u * b);
        }
        if (l_v < l_threshold) {
            a_out->coeffs[i] = (l_v % l_range + a_par->q - a_eta) % a_par->q;
            ++i;
        }
    }
    return 0;
}

int lotrs_sample_ternary(lotrs_poly_t *a_out, lotrs_xof_t *a_xof,
                         const lotrs_params_t *a_par, uint32_t a_weight)
{
    lotrs_poly_zero(a_out, a_par);

    uint32_t *l_perm = DAP_NEW_Z_COUNT(uint32_t, a_par->d);
    if (!l_perm) return -ENOMEM;
    for (uint32_t i = 0u; i < a_par->d; ++i) l_perm[i] = i;

    for (uint32_t i = a_par->d - 1u; i > a_par->d - a_weight; --i) {
        uint8_t l_buf[4];
        lotrs_xof_squeeze(a_xof, l_buf, 4u);
        uint32_t l_r = 0;
        for (int b = 0; b < 4; ++b) l_r |= (uint32_t)l_buf[b] << (8u * b);
        uint32_t l_j = l_r % (i + 1u);
        uint32_t l_tmp = l_perm[i]; l_perm[i] = l_perm[l_j]; l_perm[l_j] = l_tmp;
    }

    for (uint32_t i = 0u; i < a_weight; ++i) {
        uint8_t l_buf[1];
        lotrs_xof_squeeze(a_xof, l_buf, 1u);
        uint64_t l_sign = (l_buf[0] & 1u) ? 1u : (a_par->q - 1u);
        a_out->coeffs[l_perm[a_par->d - 1u - i]] = l_sign;
    }

    DAP_DELETE(l_perm);
    return 0;
}

int lotrs_sample_short_vec(lotrs_polyvec_t *a_out, lotrs_xof_t *a_xof,
                           const lotrs_params_t *a_par, uint32_t a_eta)
{
    for (uint32_t i = 0u; i < a_out->n; ++i) {
        int l_rc = lotrs_sample_short(a_out->polys[i], a_xof, a_par, a_eta);
        if (l_rc != 0) return l_rc;
    }
    return 0;
}

int lotrs_reject_infinity_norm(const lotrs_poly_t *a_z, int64_t a_bound,
                               const lotrs_params_t *a_par)
{
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        int64_t l_c = lotrs_center(a_z->coeffs[i], a_par->q);
        if (l_c < -a_bound || l_c > a_bound) return 0;
    }
    return 1;
}

int lotrs_reject_l2_norm_sq(const lotrs_polyvec_t *a_z, int64_t a_bound_sq,
                            const lotrs_params_t *a_par)
{
    __int128_t l_sum = 0;
    for (uint32_t i = 0u; i < a_z->n; ++i) {
        for (uint32_t j = 0u; j < a_par->d; ++j) {
            int64_t l_c = lotrs_center(a_z->polys[i]->coeffs[j], a_par->q);
            l_sum += (__int128_t)l_c * l_c;
            if (l_sum > (__int128_t)a_bound_sq) return 0;
        }
    }
    return 1;
}
