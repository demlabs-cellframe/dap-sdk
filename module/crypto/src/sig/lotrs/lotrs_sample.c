/*
 * LoTRS — XOF-based samplers.
 *
 * Uses SHAKE256 for deterministic challenge generation.
 */

#include "lotrs_sample.h"
#include "lotrs_ring.h"

#include <errno.h>
#include <string.h>

#define LOG_TAG "lotrs_sample"
#include "dap_common.h"
#include "dap_hash_shake256.h"
#include "dap_memwipe.h"

struct lotrs_xof {
    uint8_t  *absorb_buf;   /* accumulated absorb data */
    size_t    absorb_len;   /* valid bytes in absorb_buf */
    size_t    absorb_cap;   /* allocated capacity */
    uint64_t  state[25];    /* Keccak state (valid after first squeeze) */
    uint8_t   sq_buf[DAP_SHAKE256_RATE]; /* squeeze output buffer */
    size_t    sq_off;       /* current offset in sq_buf */
    size_t    sq_avail;     /* valid bytes in sq_buf */
    bool      squeezed;     /* true after first squeeze */
};

lotrs_xof_t *lotrs_xof_new(const uint8_t *a_seed, size_t a_seed_len)
{
    lotrs_xof_t *l_xof = DAP_NEW_Z(lotrs_xof_t);
    if (!l_xof) return NULL;
    l_xof->absorb_cap = (a_seed_len > 0u) ? a_seed_len + 256u : 256u;
    l_xof->absorb_buf = DAP_NEW_Z_SIZE(uint8_t, l_xof->absorb_cap);
    if (!l_xof->absorb_buf) { DAP_DELETE(l_xof); return NULL; }
    if (a_seed && a_seed_len > 0u) {
        memcpy(l_xof->absorb_buf, a_seed, a_seed_len);
        l_xof->absorb_len = a_seed_len;
    }
    return l_xof;
}

void lotrs_xof_free(lotrs_xof_t *a_xof)
{
    if (a_xof) {
        if (a_xof->absorb_buf) {
            dap_memwipe(a_xof->absorb_buf, a_xof->absorb_cap);
            DAP_DELETE(a_xof->absorb_buf);
        }
        dap_memwipe(a_xof->state, sizeof(a_xof->state));
        dap_memwipe(a_xof->sq_buf, sizeof(a_xof->sq_buf));
        DAP_DELETE(a_xof);
    }
}

int lotrs_xof_absorb(lotrs_xof_t *a_xof, const uint8_t *a_data, size_t a_len)
{
    if (a_xof->squeezed) return -EINVAL;
    while (a_xof->absorb_len + a_len > a_xof->absorb_cap) {
        size_t l_new_cap = a_xof->absorb_cap * 2u + a_len;
        uint8_t *l_new = DAP_NEW_Z_SIZE(uint8_t, l_new_cap);
        if (!l_new) return -ENOMEM;
        memcpy(l_new, a_xof->absorb_buf, a_xof->absorb_len);
        DAP_DELETE(a_xof->absorb_buf);
        a_xof->absorb_buf = l_new;
        a_xof->absorb_cap = l_new_cap;
    }
    memcpy(a_xof->absorb_buf + a_xof->absorb_len, a_data, a_len);
    a_xof->absorb_len += a_len;
    return 0;
}

static void s_xof_finalize(lotrs_xof_t *a_xof)
{
    /* Single-shot SHAKE256 absorb of all buffered data. */
    memset(a_xof->state, 0, sizeof(a_xof->state));
    dap_hash_shake256_absorb(a_xof->state, a_xof->absorb_buf, a_xof->absorb_len);
    /* Wipe and free absorb buffer. */
    dap_memwipe(a_xof->absorb_buf, a_xof->absorb_cap);
    DAP_DELETE(a_xof->absorb_buf);
    a_xof->absorb_buf = NULL;
    /* Squeeze first block. */
    dap_hash_shake256_squeezeblocks(a_xof->sq_buf, 1, a_xof->state);
    a_xof->sq_off = 0u;
    a_xof->sq_avail = DAP_SHAKE256_RATE;
    a_xof->squeezed = true;
}

void lotrs_xof_squeeze(lotrs_xof_t *a_xof, uint8_t *a_out, size_t a_out_len)
{
    if (!a_xof->squeezed) s_xof_finalize(a_xof);

    size_t l_done = 0u;
    while (l_done < a_out_len) {
        if (a_xof->sq_off >= a_xof->sq_avail) {
            /* Need more blocks from SHAKE256. */
            dap_hash_shake256_squeezeblocks(a_xof->sq_buf, 1, a_xof->state);
            a_xof->sq_off = 0u;
            a_xof->sq_avail = DAP_SHAKE256_RATE;
        }
        size_t l_avail = a_xof->sq_avail - a_xof->sq_off;
        size_t l_need = a_out_len - l_done;
        size_t l_copy = (l_need < l_avail) ? l_need : l_avail;
        memcpy(a_out + l_done, a_xof->sq_buf + a_xof->sq_off, l_copy);
        a_xof->sq_off += l_copy;
        l_done += l_copy;
    }
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
            uint64_t l_val = l_v % l_range;
            a_out->coeffs[i] = (uint64_t)lotrs_mod_reduce(
                (__int128_t)(int64_t)(l_val + a_par->q - a_eta), a_par->q);
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
        /* Rejection sampling to avoid modulo bias. */
        uint32_t l_j;
        do {
            uint8_t l_buf[4];
            lotrs_xof_squeeze(a_xof, l_buf, 4u);
            uint32_t l_r = 0;
            for (int b = 0; b < 4; ++b) l_r |= (uint32_t)l_buf[b] << (8u * b);
            l_j = l_r;
        } while (l_j >= (UINT32_MAX - (UINT32_MAX % (i + 1u))));
        l_j = l_j % (i + 1u);
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
    /* Constant-time: accumulate mask, no early exit.
     * Reject if |coeff| > bound (strict inequality matching original). */
    int64_t l_ok = -1; /* all bits set = accept */
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        int64_t l_c = lotrs_center(a_z->coeffs[i], a_par->q);
        int64_t l_abs = (l_c < 0) ? -l_c : l_c;
        /* Reject if l_abs > a_bound, i.e., a_bound - l_abs < 0. */
        int64_t l_diff = a_bound - l_abs;
        /* l_diff < 0 → sign bit = 1 → reject.
         * l_diff >= 0 → sign bit = 0 → accept. */
        l_ok &= ~(l_diff >> 63);
    }
    return (l_ok == -1) ? 1 : 0;
}

int lotrs_reject_l2_norm_sq(const lotrs_polyvec_t *a_z, int64_t a_bound_sq,
                            const lotrs_params_t *a_par)
{
    /* Constant-time: accumulate squared norm, compare at end. */
    __int128_t l_sum = 0;
    for (uint32_t i = 0u; i < a_z->n; ++i) {
        for (uint32_t j = 0u; j < a_par->d; ++j) {
            int64_t l_c = lotrs_center(a_z->polys[i]->coeffs[j], a_par->q);
            l_sum += (__int128_t)l_c * l_c;
        }
    }
    return (l_sum <= (__int128_t)a_bound_sq) ? 1 : 0;
}
