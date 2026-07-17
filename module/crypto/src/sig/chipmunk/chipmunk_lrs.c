/*
 * Chipmunk LRS canonical C0/RB2 primitives.
 */

#define LOG_TAG "chipmunk_lrs"

#include "chipmunk_lrs.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake256.h"
#include "dap_memwipe.h"

#include "chipmunk_poly.h"

static void s_le32_store(uint8_t a_out[4], uint32_t a_v)
{
    a_out[0] = (uint8_t)(a_v);
    a_out[1] = (uint8_t)(a_v >> 8);
    a_out[2] = (uint8_t)(a_v >> 16);
    a_out[3] = (uint8_t)(a_v >> 24);
}

static uint32_t s_le32_load(const uint8_t a_in[4])
{
    return ((uint32_t)a_in[0])
         | ((uint32_t)a_in[1] << 8)
         | ((uint32_t)a_in[2] << 16)
         | ((uint32_t)a_in[3] << 24);
}

static int s_memcmp_ct(const void *a, const void *b, size_t a_len)
{
    const uint8_t *l_a = (const uint8_t *)a;
    const uint8_t *l_b = (const uint8_t *)b;
    uint8_t l_diff = 0u;
    for (size_t i = 0u; i < a_len; ++i) {
        l_diff |= l_a[i] ^ l_b[i];
    }
    return (int)l_diff;
}

static void s_hash_update_bytes(uint8_t **a_pos, const void *a_data, size_t a_size)
{
    memcpy(*a_pos, a_data, a_size);
    *a_pos += a_size;
}

static int s_hash_len_prefixed(uint8_t a_out[32],
                               const char *a_domain,
                               const void *a_data,
                               size_t a_data_size)
{
    if (!a_out || !a_domain || (!a_data && a_data_size != 0) ||
        strlen(a_domain) > UINT32_MAX || a_data_size > UINT32_MAX) {
        return -EINVAL;
    }

    size_t l_domain_len = strlen(a_domain);
    size_t l_total = 4u + l_domain_len + 4u + a_data_size;
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_total);
    if (!l_buf) {
        return -ENOMEM;
    }

    uint8_t *p = l_buf;
    uint8_t le[4];
    s_le32_store(le, (uint32_t)l_domain_len);
    s_hash_update_bytes(&p, le, sizeof(le));
    s_hash_update_bytes(&p, a_domain, l_domain_len);
    s_le32_store(le, (uint32_t)a_data_size);
    s_hash_update_bytes(&p, le, sizeof(le));
    if (a_data_size) {
        s_hash_update_bytes(&p, a_data, a_data_size);
    }

    dap_hash_sha3_256_t h;
    bool ok = dap_hash_sha3_256(l_buf, l_total, &h);
    memcpy(a_out, &h, 32);
    dap_memwipe(&h, sizeof(h));
    dap_memwipe(l_buf, l_total);
    DAP_DELETE(l_buf);
    return ok ? 0 : -EIO;
}

static int32_t s_center_q(int32_t a_v, uint64_t q)
{
    if (a_v > (int32_t)(q / 2)) {
        return a_v - (int32_t)q;
    }
    return a_v;
}

/* chipmunk_mod_q is now in chipmunk_poly.h — unified across all modules */

static int s_build_xof_input(uint8_t **a_out, size_t *a_out_size,
                             const char *a_domain,
                             uint32_t a_params_id,
                             const uint8_t *a_seed_material,
                             size_t a_seed_material_size,
                             uint32_t a_index)
{
    if (!a_out || !a_out_size || !a_domain ||
        (!a_seed_material && a_seed_material_size != 0)) {
        return -EINVAL;
    }

    size_t l_domain_len = strlen(a_domain);
    if (l_domain_len == 0 || l_domain_len > UINT32_MAX ||
        a_seed_material_size > UINT32_MAX) {
        return -EINVAL;
    }

    const size_t l_size = 4u + l_domain_len + 4u + 4u + a_seed_material_size + 4u;
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_size);
    if (!l_buf) {
        return -ENOMEM;
    }

    uint8_t *p = l_buf;
    s_le32_store(p, (uint32_t)l_domain_len); p += 4u;
    memcpy(p, a_domain, l_domain_len); p += l_domain_len;
    s_le32_store(p, a_params_id); p += 4u;
    s_le32_store(p, (uint32_t)a_seed_material_size); p += 4u;
    if (a_seed_material_size) {
        memcpy(p, a_seed_material, a_seed_material_size);
        p += a_seed_material_size;
    }
    s_le32_store(p, a_index);

    *a_out = l_buf;
    *a_out_size = l_size;
    return 0;
}

typedef struct lrs_xof_reader {
    uint64_t st[25];
    uint8_t block[DAP_SHAKE256_RATE];
    size_t pos;
    size_t avail;
} lrs_xof_reader_t;

static void s_xof_reader_init(lrs_xof_reader_t *a_reader,
                              const uint8_t *a_input, size_t a_input_size)
{
    memset(a_reader, 0, sizeof(*a_reader));
    dap_hash_shake256_absorb(a_reader->st, a_input, a_input_size);
}

static uint8_t s_xof_u8(lrs_xof_reader_t *a_reader)
{
    if (a_reader->pos == a_reader->avail) {
        dap_hash_shake256_squeezeblocks(a_reader->block, 1u, a_reader->st);
        a_reader->pos = 0;
        a_reader->avail = sizeof(a_reader->block);
    }
    return a_reader->block[a_reader->pos++];
}

static uint32_t s_xof_u32(lrs_xof_reader_t *a_reader)
{
    uint8_t b[4];
    b[0] = s_xof_u8(a_reader);
    b[1] = s_xof_u8(a_reader);
    b[2] = s_xof_u8(a_reader);
    b[3] = s_xof_u8(a_reader);
    return s_le32_load(b);
}

static uint64_t s_xof_u64(lrs_xof_reader_t *a_reader)
{
    uint64_t l_v = 0;
    for (int l_i = 0; l_i < 8; ++l_i) {
        l_v |= ((uint64_t)s_xof_u8(a_reader)) << (8 * l_i);
    }
    return l_v;
}

static uint16_t s_xof_u16(lrs_xof_reader_t *a_reader)
{
    uint16_t l_lo = s_xof_u8(a_reader);
    uint16_t l_hi = s_xof_u8(a_reader);
    return (uint16_t)(l_lo | (uint16_t)(l_hi << 8));
}

int chipmunk_lrs_poly_qpack(uint8_t a_out[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                            const chipmunk_poly_t *a_poly, uint64_t q)
{
    if (!a_out || !a_poly) {
        return -EINVAL;
    }
    /* Guard: coefficients are packed CHIPMUNK_LRS_Q_BITS wide. If q exceeds
     * 2^CHIPMUNK_LRS_Q_BITS, coefficients in [2^Q_BITS, q) would silently
     * overflow into the next coefficient's bit field. */
    if (q > ((uint64_t)1u << CHIPMUNK_LRS_Q_BITS)) {
        return -EINVAL;
    }

    memset(a_out, 0, CHIPMUNK_LRS_POLY_QPACK_BYTES);
    uint64_t l_acc = 0;
    uint32_t l_bits = 0;
    size_t l_pos = 0;

    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        int32_t l_coeff = a_poly->coeffs[i];
        if (l_coeff < 0) {
            if (l_coeff < -(int32_t)(q / 2)) {
                return -EINVAL;
            }
            l_coeff += (int32_t)q;
        }
        if (l_coeff < 0 || (uint32_t)l_coeff >= (uint32_t)q) {
            return -EINVAL;
        }

        l_acc |= ((uint64_t)(uint32_t)l_coeff) << l_bits;
        l_bits += CHIPMUNK_LRS_Q_BITS;
        while (l_bits >= 8u) {
            if (l_pos >= CHIPMUNK_LRS_POLY_QPACK_BYTES) {
                return -EOVERFLOW;
            }
            a_out[l_pos++] = (uint8_t)(l_acc & 0xffu);
            l_acc >>= 8u;
            l_bits -= 8u;
        }
    }

    return l_pos == CHIPMUNK_LRS_POLY_QPACK_BYTES && l_bits == 0 ? 0 : -EOVERFLOW;
}

int chipmunk_lrs_poly_qunpack(chipmunk_poly_t *a_poly,
                              const uint8_t a_in[CHIPMUNK_LRS_POLY_QPACK_BYTES], uint64_t q)
{
    if (!a_poly || !a_in) {
        return -EINVAL;
    }
    if (q > ((uint64_t)1u << CHIPMUNK_LRS_Q_BITS)) {
        return -EINVAL;
    }

    uint64_t l_acc = 0;
    uint32_t l_bits = 0;
    size_t l_pos = 0;

    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        while (l_bits < CHIPMUNK_LRS_Q_BITS) {
            if (l_pos >= CHIPMUNK_LRS_POLY_QPACK_BYTES) {
                return -EINVAL;
            }
            l_acc |= ((uint64_t)a_in[l_pos++]) << l_bits;
            l_bits += 8u;
        }

        uint32_t l_coeff = (uint32_t)(l_acc & ((1u << CHIPMUNK_LRS_Q_BITS) - 1u));
        l_acc >>= CHIPMUNK_LRS_Q_BITS;
        l_bits -= CHIPMUNK_LRS_Q_BITS;
        if ((uint32_t)l_coeff >= (uint32_t)q) {
            return -EINVAL;
        }
        a_poly->coeffs[i] = (int32_t)l_coeff;
    }

    return l_pos == CHIPMUNK_LRS_POLY_QPACK_BYTES && l_bits == 0 ? 0 : -EINVAL;
}

int chipmunk_lrs_poly_zpack(uint8_t a_out[CHIPMUNK_LRS_POLY_ZPACK_BYTES],
                             const chipmunk_poly_t *a_poly)
{
    if (!a_out || !a_poly) {
        return -EINVAL;
    }

    memset(a_out, 0, CHIPMUNK_LRS_POLY_ZPACK_BYTES);
    uint64_t l_acc  = 0;
    uint32_t l_bits = 0;
    size_t   l_pos  = 0;

    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        int32_t  l_c = a_poly->coeffs[i];
        if (l_c < -CHIPMUNK_LRS_Z_BIAS ||
            l_c >= CHIPMUNK_LRS_Z_BIAS) {     /* half-open [−B, B) */
            return -EINVAL;
        }
        uint32_t l_v = (uint32_t)(l_c + CHIPMUNK_LRS_Z_BIAS);  /* [0, 2B-1] */

        l_acc  |= ((uint64_t)l_v) << l_bits;
        l_bits += CHIPMUNK_LRS_Z_BITS;
        while (l_bits >= 8u) {
            if (l_pos >= CHIPMUNK_LRS_POLY_ZPACK_BYTES) {
                return -EOVERFLOW;
            }
            a_out[l_pos++] = (uint8_t)(l_acc & 0xffu);
            l_acc  >>= 8u;
            l_bits  -= 8u;
        }
    }

    return (l_pos == CHIPMUNK_LRS_POLY_ZPACK_BYTES && l_bits == 0) ? 0 : -EOVERFLOW;
}

int chipmunk_lrs_poly_zunpack(chipmunk_poly_t *a_poly,
                               const uint8_t a_in[CHIPMUNK_LRS_POLY_ZPACK_BYTES])
{
    if (!a_poly || !a_in) {
        return -EINVAL;
    }

    uint64_t l_acc  = 0;
    uint32_t l_bits = 0;
    size_t   l_pos  = 0;

    const uint32_t l_mask = (1u << CHIPMUNK_LRS_Z_BITS) - 1u;  /* 0xFFFFF */
    const uint32_t l_max  = 2u * (uint32_t)CHIPMUNK_LRS_Z_BIAS - 1u; /* 2B-1 */

    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        while (l_bits < CHIPMUNK_LRS_Z_BITS) {
            if (l_pos >= CHIPMUNK_LRS_POLY_ZPACK_BYTES) {
                return -EINVAL;
            }
            l_acc  |= ((uint64_t)a_in[l_pos++]) << l_bits;
            l_bits += 8u;
        }
        uint32_t l_v = (uint32_t)(l_acc & l_mask);
        l_acc  >>= CHIPMUNK_LRS_Z_BITS;
        l_bits  -= CHIPMUNK_LRS_Z_BITS;

        if (l_v > l_max) {
            return -EINVAL;
        }
        a_poly->coeffs[i] = (int32_t)l_v - CHIPMUNK_LRS_Z_BIAS;
    }

    return (l_pos == CHIPMUNK_LRS_POLY_ZPACK_BYTES && l_bits == 0) ? 0 : -EINVAL;
}

int chipmunk_lrs_poly_chknorm_centered(const chipmunk_poly_t *a_poly,
                                       int32_t a_bound, uint64_t q)
{
    if (!a_poly || a_bound < 0) {
        return -EINVAL;
    }
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        int32_t l_c = a_poly->coeffs[i];
        if (l_c < 0) {
            if (l_c < -(int32_t)(q / 2)) {
                return -EINVAL;
            }
        } else if (l_c >= (int32_t)q) {
            return -EINVAL;
        }
        l_c = s_center_q(l_c, q);
        /* Closed interval [-bound, bound] to match h_to_short_poly which
         * generates coefficients in [-bound, +bound] inclusive (range = 2*bound+1).
         * The previous half-open [-bound, bound) incorrectly rejected the
         * maximum value +bound, causing "witness within B_x" test failures. */
        if (l_c < -a_bound || l_c > a_bound) {
            return 1;
        }
    }
    return 0;
}

int chipmunk_lrs_h_to_poly_q(chipmunk_poly_t *a_poly,
                             const char *a_domain,
                             uint32_t a_params_id,
                             const uint8_t *a_seed_material,
                             size_t a_seed_material_size,
                             uint32_t a_index, uint64_t q)
{
    if (!a_poly || a_params_id != CHIPMUNK_LRS_PARAMS_C0) {
        return -EINVAL;
    }

    uint8_t *l_input = NULL;
    size_t l_input_size = 0;
    int l_rc = s_build_xof_input(&l_input, &l_input_size, a_domain, a_params_id,
                                 a_seed_material, a_seed_material_size, a_index);
    if (l_rc != 0) {
        return l_rc;
    }

    lrs_xof_reader_t l_reader;
    s_xof_reader_init(&l_reader, l_input, l_input_size);

    const uint64_t l_threshold = UINT64_MAX - (UINT64_MAX % q);
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        uint64_t l_v;
        do {
            l_v = s_xof_u64(&l_reader);
        } while (l_v >= l_threshold);
        a_poly->coeffs[i] = (int32_t)(l_v % q);
    }

    dap_memwipe(&l_reader, sizeof(l_reader));
    dap_memwipe(l_input, l_input_size);
    DAP_DELETE(l_input);
    return 0;
}

int chipmunk_lrs_h_to_short_poly(chipmunk_poly_t *a_poly,
                                 const char *a_domain,
                                 uint32_t a_params_id,
                                 const uint8_t a_seed[CHIPMUNK_LRS_SEED_BYTES],
                                 uint32_t a_index,
                                 int32_t a_bound)
{
    if (!a_poly || !a_seed || a_params_id != CHIPMUNK_LRS_PARAMS_C0 ||
        a_bound <= 0 || a_bound > 127) {
        return -EINVAL;
    }

    uint8_t *l_input = NULL;
    size_t l_input_size = 0;
    int l_rc = s_build_xof_input(&l_input, &l_input_size, a_domain, a_params_id,
                                 a_seed, CHIPMUNK_LRS_SEED_BYTES, a_index);
    if (l_rc != 0) {
        return l_rc;
    }

    lrs_xof_reader_t l_reader;
    s_xof_reader_init(&l_reader, l_input, l_input_size);

    const uint32_t l_range = (uint32_t)(2 * a_bound + 1);
    const uint32_t l_threshold = 256u - (256u % l_range);
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        uint8_t l_v;
        do {
            l_v = s_xof_u8(&l_reader);
        } while ((uint32_t)l_v >= l_threshold);
        a_poly->coeffs[i] = (int32_t)((uint32_t)l_v % l_range) - a_bound;
    }

    dap_memwipe(&l_reader, sizeof(l_reader));
    dap_memwipe(l_input, l_input_size);
    DAP_DELETE(l_input);
    return 0;
}

int chipmunk_lrs_h_to_sparse_ternary(chipmunk_poly_t *a_challenge,
                                     const char *a_domain,
                                     uint32_t a_params_id,
                                     const uint8_t a_seed[CHIPMUNK_LRS_CHALLENGE_SEED_BYTES])
{
    if (!a_challenge || !a_domain || !a_seed || a_params_id != CHIPMUNK_LRS_PARAMS_C0) {
        return -EINVAL;
    }

    uint8_t *l_input = NULL;
    size_t l_input_size = 0;
    int l_rc = s_build_xof_input(&l_input, &l_input_size, a_domain, a_params_id,
                                 a_seed, CHIPMUNK_LRS_CHALLENGE_SEED_BYTES, 0);
    if (l_rc != 0) {
        return l_rc;
    }

    memset(a_challenge, 0, sizeof(*a_challenge));
    bool l_used[CHIPMUNK_N];
    memset(l_used, 0, sizeof(l_used));

    lrs_xof_reader_t l_reader;
    s_xof_reader_init(&l_reader, l_input, l_input_size);
    uint32_t l_filled = 0;
    while (l_filled < CHIPMUNK_LRS_CHALLENGE_WEIGHT) {
        uint16_t l_pos = (uint16_t)(s_xof_u16(&l_reader) & (CHIPMUNK_N - 1u));
        if (l_used[l_pos]) {
            continue;
        }
        l_used[l_pos] = true;
        a_challenge->coeffs[l_pos] = (s_xof_u8(&l_reader) & 1u) ? 1 : -1;
        ++l_filled;
    }

    dap_memwipe(&l_reader, sizeof(l_reader));
    dap_memwipe(l_input, l_input_size);
    DAP_DELETE(l_input);
    return 0;
}

int chipmunk_lrs_derive_witness(chipmunk_poly_t a_x[CHIPMUNK_LRS_K],
                                const uint8_t a_x_seed[CHIPMUNK_LRS_SEED_BYTES])
{
    if (!a_x || !a_x_seed) {
        return -EINVAL;
    }
    for (uint32_t i = 0; i < CHIPMUNK_LRS_K; ++i) {
        int l_rc = chipmunk_lrs_h_to_short_poly(&a_x[i], "chipmunk-lrs-keygen",
                                                CHIPMUNK_LRS_PARAMS_C0,
                                                a_x_seed, i,
                                                CHIPMUNK_LRS_WITNESS_BOUND);
        if (l_rc != 0) {
            return l_rc;
        }
    }
    return 0;
}

int chipmunk_lrs_derive_A_pk(chipmunk_poly_t a_A_pk[CHIPMUNK_LRS_K],
                             uint32_t a_params_id)
{
    if (!a_A_pk || a_params_id != CHIPMUNK_LRS_PARAMS_C0) {
        return -EINVAL;
    }

    uint8_t l_params_le[4];
    s_le32_store(l_params_le, a_params_id);

    for (uint32_t i = 0; i < CHIPMUNK_LRS_K; ++i) {
        int l_rc = chipmunk_lrs_h_to_poly_q(&a_A_pk[i], "chipmunk-ring-system-A-pk",
                                            a_params_id,
                                            l_params_le, sizeof(l_params_le), i,
                                            (uint64_t)CHIPMUNK_Q);
        if (l_rc != 0) {
            return l_rc;
        }
    }
    return 0;
}

int chipmunk_lrs_derive_A_I(chipmunk_poly_t a_A_I[CHIPMUNK_LRS_K],
                            uint32_t a_params_id,
                            const uint8_t a_P[CHIPMUNK_LRS_POLY_QPACK_BYTES])
{
    if (!a_A_I || !a_P || a_params_id != CHIPMUNK_LRS_PARAMS_C0) {
        return -EINVAL;
    }

    for (uint32_t i = 0; i < CHIPMUNK_LRS_K; ++i) {
        int l_rc = chipmunk_lrs_h_to_poly_q(&a_A_I[i], "chipmunk-lrs-image-matrix",
                                            a_params_id,
                                            a_P, CHIPMUNK_LRS_POLY_QPACK_BYTES, i,
                                            (uint64_t)CHIPMUNK_Q);
        if (l_rc != 0) {
            return l_rc;
        }
    }
    return 0;
}

int chipmunk_lrs_relation_eval(chipmunk_poly_t *a_out,
                               const chipmunk_poly_t a_A[CHIPMUNK_LRS_K],
                               const chipmunk_poly_t a_x[CHIPMUNK_LRS_K], uint64_t q)
{
    if (!a_out || !a_A || !a_x) {
        return -EINVAL;
    }

    /* NTT-native: accumulate Σ A[j]*x[j] pointwise in NTT domain.
     * Eliminates K-1 redundant invNTTs (was K round-trips, now 1 invNTT). */
    chipmunk_poly_t l_acc_ntt;
    memset(&l_acc_ntt, 0, sizeof(l_acc_ntt));

    for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
        chipmunk_poly_t l_A = a_A[j];
        chipmunk_poly_t l_x = a_x[j];

        int l_rc = chipmunk_poly_ntt(&l_A);
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) return l_rc;
        l_rc = chipmunk_poly_ntt(&l_x);
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) return l_rc;

        chipmunk_poly_t l_prod;
        chipmunk_poly_mul_ntt_q(&l_prod, &l_A, &l_x, q);
        for (size_t i = 0; i < CHIPMUNK_N; ++i) {
            int64_t l_s = (int64_t)l_acc_ntt.coeffs[i] + l_prod.coeffs[i];
            int32_t l_r = (int32_t)(l_s % (int64_t)q);
            if (l_r < 0) l_r += (int32_t)q;
            l_acc_ntt.coeffs[i] = l_r;
        }
    }

    /* Single invNTT → time-domain result */
    int l_rc = chipmunk_poly_invntt(&l_acc_ntt);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) return l_rc;

    /* Canonicalize to [0,q) */
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        if (l_acc_ntt.coeffs[i] < 0) l_acc_ntt.coeffs[i] += (int32_t)q;
        a_out->coeffs[i] = l_acc_ntt.coeffs[i];
    }
    return 0;
}

int chipmunk_lrs_keypair_from_seeds(chipmunk_lrs_public_key_t *a_pk,
                                    chipmunk_lrs_secret_key_t *a_sk,
                                    const uint8_t a_x_seed[CHIPMUNK_LRS_SEED_BYTES])
{
    if (!a_pk || !a_sk || !a_x_seed) {
        return -EINVAL;
    }

    chipmunk_poly_t l_x[CHIPMUNK_LRS_K];
    chipmunk_poly_t l_A[CHIPMUNK_LRS_K];
    chipmunk_poly_t l_P;

    int l_rc = chipmunk_lrs_derive_witness(l_x, a_x_seed);
    if (l_rc != 0) {
        return l_rc;
    }
    l_rc = chipmunk_lrs_derive_A_pk(l_A, CHIPMUNK_LRS_PARAMS_C0);
    if (l_rc != 0) {
        dap_memwipe(l_x, sizeof(l_x));
        return l_rc;
    }
    l_rc = chipmunk_lrs_relation_eval(&l_P, l_A, l_x, (uint64_t)CHIPMUNK_Q);
    if (l_rc != 0) {
        dap_memwipe(l_x, sizeof(l_x));
        dap_memwipe(l_A, sizeof(l_A));
        return l_rc;
    }

    memset(a_pk, 0, sizeof(*a_pk));
    a_pk->magic = CHIPMUNK_LRS_MAGIC_CLPK;
    a_pk->params_id = CHIPMUNK_LRS_PARAMS_C0;
    l_rc = chipmunk_lrs_poly_qpack(a_pk->P, &l_P, (uint64_t)CHIPMUNK_Q);
    if (l_rc == 0) {
        memset(a_sk, 0, sizeof(*a_sk));
        a_sk->magic = CHIPMUNK_LRS_MAGIC_CLSK;
        a_sk->params_id = CHIPMUNK_LRS_PARAMS_C0;
        memcpy(a_sk->x_seed, a_x_seed, CHIPMUNK_LRS_SEED_BYTES);
        memcpy(a_sk->P, a_pk->P, CHIPMUNK_LRS_POLY_QPACK_BYTES);
    }

    dap_memwipe(l_x, sizeof(l_x));
    dap_memwipe(l_A, sizeof(l_A));
    dap_memwipe(&l_P, sizeof(l_P));
    return l_rc;
}

int chipmunk_lrs_key_image(uint8_t a_key_image[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                           const chipmunk_lrs_secret_key_t *a_sk)
{
    if (!a_key_image || !a_sk || a_sk->magic != CHIPMUNK_LRS_MAGIC_CLSK ||
        a_sk->params_id != CHIPMUNK_LRS_PARAMS_C0 ||
        a_sk->reserved0 != 0 || a_sk->reserved1 != 0) {
        return -EINVAL;
    }

    chipmunk_poly_t l_x[CHIPMUNK_LRS_K];
    chipmunk_poly_t l_A_I[CHIPMUNK_LRS_K];
    chipmunk_poly_t l_I;

    int l_rc = chipmunk_lrs_derive_witness(l_x, a_sk->x_seed);
    if (l_rc != 0) {
        return l_rc;
    }
    l_rc = chipmunk_lrs_derive_A_I(l_A_I, CHIPMUNK_LRS_PARAMS_C0, a_sk->P);
    if (l_rc == 0) {
        l_rc = chipmunk_lrs_relation_eval(&l_I, l_A_I, l_x, (uint64_t)CHIPMUNK_Q);
    }
    if (l_rc == 0) {
        l_rc = chipmunk_lrs_poly_qpack(a_key_image, &l_I, (uint64_t)CHIPMUNK_Q);
    }

    dap_memwipe(l_x, sizeof(l_x));
    dap_memwipe(l_A_I, sizeof(l_A_I));
    dap_memwipe(&l_I, sizeof(l_I));
    return l_rc;
}

int chipmunk_lrs_public_key_validate(const chipmunk_lrs_public_key_t *a_pk)
{
    if (!a_pk || a_pk->magic != CHIPMUNK_LRS_MAGIC_CLPK ||
        a_pk->params_id != CHIPMUNK_LRS_PARAMS_C0 ||
        a_pk->reserved0 != 0 || a_pk->reserved1 != 0) {
        return -EINVAL;
    }

    chipmunk_poly_t l_P;
    int l_rc = chipmunk_lrs_poly_qunpack(&l_P, a_pk->P, (uint64_t)CHIPMUNK_Q);
    dap_memwipe(&l_P, sizeof(l_P));
    return l_rc;
}

int chipmunk_lrs_secret_key_validate(const chipmunk_lrs_secret_key_t *a_sk)
{
    if (!a_sk || a_sk->magic != CHIPMUNK_LRS_MAGIC_CLSK ||
        a_sk->params_id != CHIPMUNK_LRS_PARAMS_C0 ||
        a_sk->reserved0 != 0 || a_sk->reserved1 != 0) {
        return -EINVAL;
    }

    chipmunk_lrs_public_key_t l_pk;
    chipmunk_lrs_secret_key_t l_sk;
    int l_rc = chipmunk_lrs_keypair_from_seeds(&l_pk, &l_sk, a_sk->x_seed);
    if (l_rc != 0) {
        return l_rc;
    }
    l_rc = s_memcmp_ct(l_sk.P, a_sk->P, CHIPMUNK_LRS_POLY_QPACK_BYTES) == 0 ? 0 : -EINVAL;
    dap_memwipe(&l_pk, sizeof(l_pk));
    dap_memwipe(&l_sk, sizeof(l_sk));
    return l_rc;
}

int chipmunk_lrs_public_key_hash(uint8_t a_out[32],
                                 const chipmunk_lrs_public_key_t *a_pk)
{
    int l_rc = chipmunk_lrs_public_key_validate(a_pk);
    if (l_rc != 0) {
        return l_rc;
    }
    return s_hash_len_prefixed(a_out, "chipmunk-lrs-public-key-hash",
                               a_pk, sizeof(*a_pk));
}

static int s_pk_cmp(const void *a_a, const void *a_b)
{
    return memcmp(a_a, a_b, sizeof(chipmunk_lrs_public_key_t));
}

static int s_h_to_wide_poly(chipmunk_poly_t *a_poly,
                            const char *a_domain,
                            uint32_t a_params_id,
                            const uint8_t *a_seed_material,
                            size_t a_seed_material_size,
                            uint32_t a_index,
                            int32_t a_bound, uint64_t q)
{
    if (!a_poly || a_bound <= 0) {
        return -EINVAL;
    }
    if (a_bound >= (int32_t)(q / 2)) {
        return -EINVAL;
    }

    uint8_t *l_input = NULL;
    size_t l_input_size = 0;
    int l_rc = s_build_xof_input(&l_input, &l_input_size, a_domain, a_params_id,
                                 a_seed_material, a_seed_material_size, a_index);
    if (l_rc != 0) {
        return l_rc;
    }

    lrs_xof_reader_t l_reader;
    s_xof_reader_init(&l_reader, l_input, l_input_size);

    const uint32_t l_range = (uint32_t)(2 * a_bound + 1);
    const uint32_t l_threshold = UINT32_MAX - (UINT32_MAX % l_range);
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        uint32_t l_v;
        do {
            l_v = s_xof_u32(&l_reader);
        } while (l_v >= l_threshold);
        a_poly->coeffs[i] = (int32_t)(l_v % l_range) - a_bound;
    }

    dap_memwipe(&l_reader, sizeof(l_reader));
    dap_memwipe(l_input, l_input_size);
    DAP_DELETE(l_input);
    return 0;
}

int chipmunk_lrs_h_to_bounded_poly(chipmunk_poly_t *a_poly,
                                   const char *a_domain,
                                   uint32_t a_params_id,
                                   const uint8_t *a_seed_material,
                                   size_t a_seed_material_size,
                                   uint32_t a_index,
                                   int32_t a_bound)
{
    return s_h_to_wide_poly(a_poly, a_domain, a_params_id,
                            a_seed_material, a_seed_material_size,
                            a_index, a_bound, (uint64_t)CHIPMUNK_Q);
}

int chipmunk_lrs_ring_hash(uint8_t a_out[32],
                           const chipmunk_lrs_public_key_t *a_public_keys,
                           size_t a_ring_size)
{
    if (!a_out || !a_public_keys || a_ring_size < 2u || a_ring_size > 64u) {
        return -EINVAL;
    }

    chipmunk_lrs_public_key_t *l_sorted =
        DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, a_ring_size);
    if (!l_sorted) {
        return -ENOMEM;
    }

    for (size_t i = 0; i < a_ring_size; ++i) {
        int l_rc = chipmunk_lrs_public_key_validate(&a_public_keys[i]);
        if (l_rc != 0) {
            dap_memwipe(l_sorted, sizeof(*l_sorted) * a_ring_size);
            DAP_DELETE(l_sorted);
            return l_rc;
        }
        l_sorted[i] = a_public_keys[i];
    }

    qsort(l_sorted, a_ring_size, sizeof(*l_sorted), s_pk_cmp);
    for (size_t i = 1; i < a_ring_size; ++i) {
        if (memcmp(&l_sorted[i - 1u], &l_sorted[i], sizeof(*l_sorted)) == 0) {
            dap_memwipe(l_sorted, sizeof(*l_sorted) * a_ring_size);
            DAP_DELETE(l_sorted);
            return -EINVAL;
        }
    }

    const size_t l_key_size = sizeof(chipmunk_lrs_public_key_t);
    const size_t l_payload_size = 4u + 4u + a_ring_size * (4u + l_key_size);
    uint8_t *l_payload = DAP_NEW_Z_SIZE(uint8_t, l_payload_size);
    if (!l_payload) {
        dap_memwipe(l_sorted, sizeof(*l_sorted) * a_ring_size);
        DAP_DELETE(l_sorted);
        return -ENOMEM;
    }

    uint8_t *p = l_payload;
    uint8_t le[4];
    s_le32_store(le, CHIPMUNK_LRS_PARAMS_C0);
    s_hash_update_bytes(&p, le, sizeof(le));
    s_le32_store(le, (uint32_t)a_ring_size);
    s_hash_update_bytes(&p, le, sizeof(le));
    for (size_t i = 0; i < a_ring_size; ++i) {
        s_le32_store(le, (uint32_t)l_key_size);
        s_hash_update_bytes(&p, le, sizeof(le));
        s_hash_update_bytes(&p, &l_sorted[i], l_key_size);
    }

    int l_rc = s_hash_len_prefixed(a_out, "chipmunk-lrs-ring-hash",
                                   l_payload, l_payload_size);
    dap_memwipe(l_payload, l_payload_size);
    DAP_DELETE(l_payload);
    dap_memwipe(l_sorted, sizeof(*l_sorted) * a_ring_size);
    DAP_DELETE(l_sorted);
    return l_rc;
}

/*
 * PoP transcript helper: c_seed = H("chipmunk-lrs-pop-step" ||
 *   LE32(params_id) || pk_hash || LE32(K) || qpack(T[0]) || ... || qpack(T[K-1]))
 *
 * The transcript binds every coefficient of T through canonical q-pack
 * encoding so a verifier reconstructing T' from (c, z, P) recovers exactly
 * the same hash input.
 */
static int s_pop_challenge_seed(uint8_t a_out[32],
                                uint32_t a_params_id,
                                const uint8_t a_pk_hash[32],
                                const uint8_t a_T_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES])
{
    const size_t l_payload_size = 4u + 32u + 4u + CHIPMUNK_LRS_POLY_QPACK_BYTES;
    uint8_t *l_payload = DAP_NEW_Z_SIZE(uint8_t, l_payload_size);
    if (!l_payload) {
        return -ENOMEM;
    }

    uint8_t *p = l_payload;
    uint8_t le[4];
    s_le32_store(le, a_params_id);
    s_hash_update_bytes(&p, le, sizeof(le));
    s_hash_update_bytes(&p, a_pk_hash, 32u);
    s_le32_store(le, 1u); /* number of T polynomials packed below */
    s_hash_update_bytes(&p, le, sizeof(le));
    s_hash_update_bytes(&p, a_T_packed, CHIPMUNK_LRS_POLY_QPACK_BYTES);

    int l_rc = s_hash_len_prefixed(a_out, "chipmunk-lrs-pop-step",
                                   l_payload, l_payload_size);
    dap_memwipe(l_payload, l_payload_size);
    DAP_DELETE(l_payload);
    return l_rc;
}

/*
 * Compute T = Sum A[j] * y[j] mod q.
 * y[j] is in unreduced integer space (centered values up to ~gamma),
 * caller is responsible for reducing into [0,q) representatives before
 * NTT.  Helper below performs the reduction on a working copy so the
 * caller's y stays untouched.
 */
static int s_relation_eval_centered(chipmunk_poly_t *a_out,
                                    const chipmunk_poly_t a_A[CHIPMUNK_LRS_K],
                                    const chipmunk_poly_t a_y[CHIPMUNK_LRS_K], uint64_t q)
{
    chipmunk_poly_t l_y_red[CHIPMUNK_LRS_K];
    for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
        for (size_t i = 0; i < CHIPMUNK_N; ++i) {
            l_y_red[j].coeffs[i] = chipmunk_mod_q_q((int64_t)a_y[j].coeffs[i], q);
        }
    }
    int l_rc = chipmunk_lrs_relation_eval(a_out, a_A, l_y_red, q);
    dap_memwipe(l_y_red, sizeof(l_y_red));
    return l_rc;
}

/*
 * Compute cx = c * x mod q and return centered representatives in [-q/2, q/2].
 * Both inputs are read-only.
 */
static int s_mul_challenge_witness_centered(chipmunk_poly_t a_cx[CHIPMUNK_LRS_K],
                                            const chipmunk_poly_t *a_c,
                                            const chipmunk_poly_t a_x[CHIPMUNK_LRS_K], uint64_t q)
{
    chipmunk_poly_t l_c_red = *a_c;
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        l_c_red.coeffs[i] = chipmunk_mod_q_q((int64_t)l_c_red.coeffs[i], q);
    }
    int l_rc = chipmunk_poly_ntt(&l_c_red);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
        dap_memwipe(&l_c_red, sizeof(l_c_red));
        return l_rc;
    }

    for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
        chipmunk_poly_t l_x_red = a_x[j];
        for (size_t i = 0; i < CHIPMUNK_N; ++i) {
            l_x_red.coeffs[i] = chipmunk_mod_q_q((int64_t)l_x_red.coeffs[i], q);
        }
        l_rc = chipmunk_poly_ntt(&l_x_red);
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
            dap_memwipe(&l_x_red, sizeof(l_x_red));
            dap_memwipe(&l_c_red, sizeof(l_c_red));
            return l_rc;
        }
        chipmunk_poly_mul_ntt_q(&a_cx[j], &l_c_red, &l_x_red, q);
        l_rc = chipmunk_poly_invntt(&a_cx[j]);
        dap_memwipe(&l_x_red, sizeof(l_x_red));
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
            dap_memwipe(&l_c_red, sizeof(l_c_red));
            return l_rc;
        }
        for (size_t i = 0; i < CHIPMUNK_N; ++i) {
            int32_t l_c = a_cx[j].coeffs[i];
            if (l_c < 0 || l_c >= (int32_t)q) {
                l_c = chipmunk_mod_q_q((int64_t)l_c, q);
            }
            a_cx[j].coeffs[i] = s_center_q(l_c, q);
        }
    }
    dap_memwipe(&l_c_red, sizeof(l_c_red));
    return 0;
}

int chipmunk_lrs_pop_create(uint8_t *a_pop,
                            size_t a_pop_size,
                            const chipmunk_lrs_secret_key_t *a_sk,
                            const uint8_t a_randomness_seed[CHIPMUNK_LRS_SEED_BYTES], uint64_t q)
{
    if (!a_pop || a_pop_size != CHIPMUNK_LRS_POP_BYTES ||
        !a_sk || !a_randomness_seed) {
        return -EINVAL;
    }
    int l_rc = chipmunk_lrs_secret_key_validate(a_sk);
    if (l_rc != 0) {
        return l_rc;
    }

    chipmunk_lrs_public_key_t l_pk;
    memset(&l_pk, 0, sizeof(l_pk));
    l_pk.magic = CHIPMUNK_LRS_MAGIC_CLPK;
    l_pk.params_id = CHIPMUNK_LRS_PARAMS_C0;
    memcpy(l_pk.P, a_sk->P, CHIPMUNK_LRS_POLY_QPACK_BYTES);

    uint8_t l_pk_hash[32];
    l_rc = chipmunk_lrs_public_key_hash(l_pk_hash, &l_pk);
    if (l_rc != 0) {
        dap_memwipe(&l_pk, sizeof(l_pk));
        return l_rc;
    }

    chipmunk_poly_t l_x[CHIPMUNK_LRS_K];
    chipmunk_poly_t l_A_pk[CHIPMUNK_LRS_K];
    l_rc = chipmunk_lrs_derive_witness(l_x, a_sk->x_seed);
    if (l_rc == 0) {
        l_rc = chipmunk_lrs_derive_A_pk(l_A_pk, CHIPMUNK_LRS_PARAMS_C0);
    }
    if (l_rc != 0) {
        dap_memwipe(l_x, sizeof(l_x));
        dap_memwipe(l_A_pk, sizeof(l_A_pk));
        dap_memwipe(&l_pk, sizeof(l_pk));
        return l_rc;
    }

    /*
     * Mask sampler seed binds the caller-provided randomness to the
     * concrete LRS public key so the same randomness reused against a
     * different key cannot collapse to the same y.
     */
    const size_t l_mask_seed_size =
        CHIPMUNK_LRS_SEED_BYTES + CHIPMUNK_LRS_POLY_QPACK_BYTES;
    uint8_t *l_mask_seed = DAP_NEW_Z_SIZE(uint8_t, l_mask_seed_size);
    if (!l_mask_seed) {
        dap_memwipe(l_x, sizeof(l_x));
        dap_memwipe(l_A_pk, sizeof(l_A_pk));
        dap_memwipe(&l_pk, sizeof(l_pk));
        return -ENOMEM;
    }
    memcpy(l_mask_seed, a_randomness_seed, CHIPMUNK_LRS_SEED_BYTES);
    memcpy(l_mask_seed + CHIPMUNK_LRS_SEED_BYTES, a_sk->P,
           CHIPMUNK_LRS_POLY_QPACK_BYTES);

    chipmunk_poly_t l_y[CHIPMUNK_LRS_K];
    chipmunk_poly_t l_cx[CHIPMUNK_LRS_K];
    chipmunk_poly_t l_T;
    chipmunk_poly_t l_challenge;
    uint8_t l_T_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES];
    uint8_t l_challenge_seed[32];
    uint8_t l_z_packed[CHIPMUNK_LRS_K][CHIPMUNK_LRS_POLY_QPACK_BYTES];

    int l_done = 0;
    for (uint32_t l_attempt = 0; l_attempt < CHIPMUNK_LRS_MAX_ATTEMPTS && !l_done;
         ++l_attempt) {
        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            l_rc = s_h_to_wide_poly(&l_y[j], "chipmunk-lrs-pop-mask",
                                    CHIPMUNK_LRS_PARAMS_C0, l_mask_seed,
                                    l_mask_seed_size,
                                    l_attempt * CHIPMUNK_LRS_K + j,
                                    CHIPMUNK_LRS_MASK_BOUND, q);
            if (l_rc != 0) {
                goto cleanup;
            }
        }

        l_rc = s_relation_eval_centered(&l_T, l_A_pk, l_y, q);
        if (l_rc != 0) {
            goto cleanup;
        }
        l_rc = chipmunk_lrs_poly_qpack(l_T_packed, &l_T, q);
        if (l_rc != 0) {
            goto cleanup;
        }

        l_rc = s_pop_challenge_seed(l_challenge_seed,
                                    CHIPMUNK_LRS_PARAMS_C0, l_pk_hash, l_T_packed);
        if (l_rc != 0) {
            goto cleanup;
        }
        l_rc = chipmunk_lrs_h_to_sparse_ternary(&l_challenge,
                                                "chipmunk-lrs-challenge",
                                                CHIPMUNK_LRS_PARAMS_C0,
                                                l_challenge_seed);
        if (l_rc != 0) {
            goto cleanup;
        }

        l_rc = s_mul_challenge_witness_centered(l_cx, &l_challenge, l_x, q);
        if (l_rc != 0) {
            goto cleanup;
        }

        bool l_reject = false;
        for (uint32_t j = 0; j < CHIPMUNK_LRS_K && !l_reject; ++j) {
            for (size_t i = 0; i < CHIPMUNK_N; ++i) {
                /*
                 * y[j][i] is centered in [-gamma, gamma], cx[j][i] is centered
                 * in [-beta, beta], so the integer sum fits well within int32_t
                 * and is its own centered representative modulo q (no wrap).
                 */
                int32_t l_zi = l_y[j].coeffs[i] + l_cx[j].coeffs[i];
                if (l_zi < -CHIPMUNK_LRS_RESPONSE_BOUND ||
                    l_zi > CHIPMUNK_LRS_RESPONSE_BOUND) {
                    l_reject = true;
                    break;
                }
            }
        }
        if (l_reject) {
            continue;
        }

        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            chipmunk_poly_t l_z_poly;
            for (size_t i = 0; i < CHIPMUNK_N; ++i) {
                int32_t l_zi = l_y[j].coeffs[i] + l_cx[j].coeffs[i];
                l_z_poly.coeffs[i] = chipmunk_mod_q_q((int64_t)l_zi, q);
            }
            l_rc = chipmunk_lrs_poly_qpack(l_z_packed[j], &l_z_poly, q);
            dap_memwipe(&l_z_poly, sizeof(l_z_poly));
            if (l_rc != 0) {
                goto cleanup;
            }
        }
        l_done = 1;
    }

    if (!l_done) {
        l_rc = -EAGAIN;
        goto cleanup;
    }

    memset(a_pop, 0, a_pop_size);
    uint8_t *p = a_pop;
    s_le32_store(p, CHIPMUNK_LRS_MAGIC_CLRP); p += 4u;
    s_le32_store(p, CHIPMUNK_LRS_PARAMS_C0); p += 4u;
    s_le32_store(p, 0u); p += 4u; /* flags */
    s_le32_store(p, (uint32_t)CHIPMUNK_LRS_POP_HEADER_BYTES); p += 4u;
    s_le32_store(p, (uint32_t)CHIPMUNK_LRS_POP_RESPONSE_BYTES); p += 4u;
    s_le32_store(p, 0u); p += 4u; /* reserved0 */
    s_le32_store(p, 0u); p += 4u; /* reserved1 */
    s_le32_store(p, 0u); p += 4u; /* reserved2 */
    memcpy(p, l_pk_hash, 32u); p += 32u;
    memcpy(p, l_challenge_seed, 32u); p += 32u;
    for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
        memcpy(p, l_z_packed[j], CHIPMUNK_LRS_POLY_QPACK_BYTES);
        p += CHIPMUNK_LRS_POLY_QPACK_BYTES;
    }
    l_rc = 0;

cleanup:
    dap_memwipe(l_x, sizeof(l_x));
    dap_memwipe(l_A_pk, sizeof(l_A_pk));
    dap_memwipe(l_y, sizeof(l_y));
    dap_memwipe(l_cx, sizeof(l_cx));
    dap_memwipe(&l_T, sizeof(l_T));
    dap_memwipe(&l_challenge, sizeof(l_challenge));
    dap_memwipe(l_T_packed, sizeof(l_T_packed));
    dap_memwipe(l_challenge_seed, sizeof(l_challenge_seed));
    dap_memwipe(l_z_packed, sizeof(l_z_packed));
    dap_memwipe(l_pk_hash, sizeof(l_pk_hash));
    dap_memwipe(&l_pk, sizeof(l_pk));
    dap_memwipe(l_mask_seed, l_mask_seed_size);
    DAP_DELETE(l_mask_seed);
    return l_rc;
}

int chipmunk_lrs_pop_verify(const uint8_t *a_pop,
                            size_t a_pop_size,
                            const chipmunk_lrs_public_key_t *a_pk, uint64_t q)
{
    if (!a_pop || a_pop_size != CHIPMUNK_LRS_POP_BYTES || !a_pk) {
        return -EINVAL;
    }
    int l_rc = chipmunk_lrs_public_key_validate(a_pk);
    if (l_rc != 0) {
        return l_rc;
    }

    /* Header gates first — fail closed without touching algebra. */
    const uint8_t *p = a_pop;
    uint32_t l_magic = s_le32_load(p); p += 4u;
    uint32_t l_params = s_le32_load(p); p += 4u;
    uint32_t l_flags = s_le32_load(p); p += 4u;
    uint32_t l_hdr_bytes = s_le32_load(p); p += 4u;
    uint32_t l_resp_bytes = s_le32_load(p); p += 4u;
    uint32_t l_r0 = s_le32_load(p); p += 4u;
    uint32_t l_r1 = s_le32_load(p); p += 4u;
    uint32_t l_r2 = s_le32_load(p); p += 4u;
    if (l_magic != CHIPMUNK_LRS_MAGIC_CLRP ||
        l_params != CHIPMUNK_LRS_PARAMS_C0 ||
        l_flags != 0u ||
        l_hdr_bytes != CHIPMUNK_LRS_POP_HEADER_BYTES ||
        l_resp_bytes != CHIPMUNK_LRS_POP_RESPONSE_BYTES ||
        l_r0 != 0u || l_r1 != 0u || l_r2 != 0u) {
        return -EINVAL;
    }

    uint8_t l_pk_hash[32];
    l_rc = chipmunk_lrs_public_key_hash(l_pk_hash, a_pk);
    if (l_rc != 0) {
        return l_rc;
    }
    if (s_memcmp_ct(p, l_pk_hash, 32u) != 0) {
        dap_memwipe(l_pk_hash, sizeof(l_pk_hash));
        return -EINVAL;
    }
    p += 32u;

    uint8_t l_challenge_seed[32];
    memcpy(l_challenge_seed, p, 32u);
    p += 32u;

    chipmunk_poly_t l_z[CHIPMUNK_LRS_K];
    for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
        l_rc = chipmunk_lrs_poly_qunpack(&l_z[j], p, q);
        if (l_rc != 0) {
            goto vfail;
        }
        l_rc = chipmunk_lrs_poly_chknorm_centered(&l_z[j], CHIPMUNK_LRS_RESPONSE_BOUND, q);
        if (l_rc != 0) {
            l_rc = (l_rc == 1) ? -EINVAL : l_rc;
            goto vfail;
        }
        p += CHIPMUNK_LRS_POLY_QPACK_BYTES;
    }

    chipmunk_poly_t l_challenge;
    l_rc = chipmunk_lrs_h_to_sparse_ternary(&l_challenge, "chipmunk-lrs-challenge",
                                            CHIPMUNK_LRS_PARAMS_C0, l_challenge_seed);
    if (l_rc != 0) {
        goto vfail;
    }

    chipmunk_poly_t l_A_pk[CHIPMUNK_LRS_K];
    l_rc = chipmunk_lrs_derive_A_pk(l_A_pk, CHIPMUNK_LRS_PARAMS_C0);
    if (l_rc != 0) {
        goto vfail;
    }

    chipmunk_poly_t l_P;
    l_rc = chipmunk_lrs_poly_qunpack(&l_P, a_pk->P, q);
    if (l_rc != 0) {
        goto vfail;
    }

    /*
     * T' = Sum A_pk[j]*z[j] - c*P, all mod q.  We compute it as a single
     * accumulator to minimise temporaries and rely on NTT mul for both
     * legs.
     */
    chipmunk_poly_t l_sum_Az;
    l_rc = chipmunk_lrs_relation_eval(&l_sum_Az, l_A_pk, l_z, q);
    if (l_rc != 0) {
        goto vfail;
    }

    chipmunk_poly_t l_cP;
    {
        chipmunk_poly_t l_c_red = l_challenge;
        chipmunk_poly_t l_P_red = l_P;
        for (size_t i = 0; i < CHIPMUNK_N; ++i) {
            l_c_red.coeffs[i] = chipmunk_mod_q_q((int64_t)l_c_red.coeffs[i], q);
        }
        l_rc = chipmunk_poly_ntt(&l_c_red);
        if (l_rc == CHIPMUNK_ERROR_SUCCESS) {
            l_rc = chipmunk_poly_ntt(&l_P_red);
        }
        if (l_rc == CHIPMUNK_ERROR_SUCCESS) {
            chipmunk_poly_mul_ntt_q(&l_cP, &l_c_red, &l_P_red, q);
            l_rc = chipmunk_poly_invntt(&l_cP);
        }
        dap_memwipe(&l_c_red, sizeof(l_c_red));
        dap_memwipe(&l_P_red, sizeof(l_P_red));
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
            goto vfail;
        }
    }

    chipmunk_poly_t l_T_prime;
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        int64_t l_v = (int64_t)l_sum_Az.coeffs[i] - (int64_t)l_cP.coeffs[i];
        l_T_prime.coeffs[i] = chipmunk_mod_q_q(l_v, q);
    }

    uint8_t l_T_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES];
    l_rc = chipmunk_lrs_poly_qpack(l_T_packed, &l_T_prime, q);
    if (l_rc != 0) {
        goto vfail;
    }

    uint8_t l_recovered_seed[32];
    l_rc = s_pop_challenge_seed(l_recovered_seed, CHIPMUNK_LRS_PARAMS_C0,
                                l_pk_hash, l_T_packed);
    if (l_rc != 0) {
        goto vfail;
    }

    l_rc = (s_memcmp_ct(l_recovered_seed, l_challenge_seed, 32u) == 0) ? 0 : -EINVAL;

    dap_memwipe(&l_T_prime, sizeof(l_T_prime));
    dap_memwipe(&l_sum_Az, sizeof(l_sum_Az));
    dap_memwipe(&l_cP, sizeof(l_cP));
    dap_memwipe(l_A_pk, sizeof(l_A_pk));
    dap_memwipe(&l_P, sizeof(l_P));
    dap_memwipe(&l_challenge, sizeof(l_challenge));
    dap_memwipe(l_T_packed, sizeof(l_T_packed));
    dap_memwipe(l_recovered_seed, sizeof(l_recovered_seed));
    dap_memwipe(l_z, sizeof(l_z));
    dap_memwipe(l_challenge_seed, sizeof(l_challenge_seed));
    dap_memwipe(l_pk_hash, sizeof(l_pk_hash));
    return l_rc;

vfail:
    dap_memwipe(l_z, sizeof(l_z));
    dap_memwipe(l_challenge_seed, sizeof(l_challenge_seed));
    dap_memwipe(l_pk_hash, sizeof(l_pk_hash));
    return l_rc != 0 ? l_rc : -EINVAL;
}

/* ------------------------------------------------------------------ */
/*  CLRS — canonical hash-chain OR linkable ring signature             */
/* ------------------------------------------------------------------ */

size_t chipmunk_lrs_signature_size(uint32_t a_ring_size)
{
    if (a_ring_size < CHIPMUNK_LRS_RING_MIN || a_ring_size > CHIPMUNK_LRS_RING_MAX) {
        return 0;
    }
    return (size_t)CHIPMUNK_LRS_SIG_HEADER_BYTES +
           (size_t)a_ring_size * CHIPMUNK_LRS_SIG_PER_MEMBER_BYTES;
}

typedef struct lrs_ring_member_ctx {
    chipmunk_poly_t A_pk[CHIPMUNK_LRS_K];
    chipmunk_poly_t A_I[CHIPMUNK_LRS_K];
    chipmunk_poly_t P_poly;
} lrs_ring_member_ctx_t;

static int s_derive_ring_member_ctx(lrs_ring_member_ctx_t *a_ctx,
                                    const chipmunk_lrs_public_key_t *a_pk)
{
    int l_rc = chipmunk_lrs_public_key_validate(a_pk);
    if (l_rc != 0) {
        return l_rc;
    }
    l_rc = chipmunk_lrs_derive_A_pk(a_ctx->A_pk, a_pk->params_id);
    if (l_rc != 0) {
        return l_rc;
    }
    l_rc = chipmunk_lrs_derive_A_I(a_ctx->A_I, a_pk->params_id, a_pk->P);
    if (l_rc != 0) {
        return l_rc;
    }
    return chipmunk_lrs_poly_qunpack(&a_ctx->P_poly, a_pk->P, (uint64_t)CHIPMUNK_Q);
}

/*
 * Sort the candidate ring canonically (memcmp over CLPK bytes), reject
 * duplicates, and locate the signer's CLPK inside the sorted ring.  The
 * caller-provided sk is paired against a freshly recomputed CLPK for the
 * signer, so a stale or mismatched sk fails closed.
 */
static int s_canonicalise_ring(chipmunk_lrs_public_key_t *a_sorted,
                               size_t a_ring_size,
                               const chipmunk_lrs_public_key_t *a_in,
                               const chipmunk_lrs_public_key_t *a_signer_pk_or_null,
                               size_t *a_signer_index_out)
{
    if (!a_sorted || !a_in || a_ring_size < CHIPMUNK_LRS_RING_MIN ||
        a_ring_size > CHIPMUNK_LRS_RING_MAX) {
        return -EINVAL;
    }
    for (size_t i = 0; i < a_ring_size; ++i) {
        int l_rc = chipmunk_lrs_public_key_validate(&a_in[i]);
        if (l_rc != 0) {
            return l_rc;
        }
        a_sorted[i] = a_in[i];
    }
    qsort(a_sorted, a_ring_size, sizeof(*a_sorted), s_pk_cmp);
    for (size_t i = 1; i < a_ring_size; ++i) {
        if (memcmp(&a_sorted[i - 1u], &a_sorted[i], sizeof(*a_sorted)) == 0) {
            return -EINVAL;
        }
    }
    if (a_signer_pk_or_null) {
        size_t l_idx = SIZE_MAX;
        for (size_t i = 0u; i < a_ring_size; ++i) {
            /* CT-safe: always scan full ring, no early break. */
            if (s_memcmp_ct(&a_sorted[i], a_signer_pk_or_null,
                            sizeof(*a_sorted)) == 0) {
                l_idx = i;
            }
        }
        if (l_idx == SIZE_MAX) {
            return -EINVAL;
        }
        if (a_signer_index_out) {
            *a_signer_index_out = l_idx;
        }
    }
    return 0;
}

static int s_message_hash(uint8_t a_out[32],
                          const uint8_t *a_message, size_t a_message_size)
{
    if (!a_out || (!a_message && a_message_size != 0) ||
        a_message_size > UINT32_MAX) {
        return -EINVAL;
    }
    return s_hash_len_prefixed(a_out, "chipmunk-lrs-message",
                               a_message, a_message_size);
}

static int s_signature_prefix(uint8_t a_out[32],
                              uint32_t a_params_id,
                              uint32_t a_ring_size,
                              const uint8_t a_ring_hash[32],
                              const uint8_t a_key_image_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                              const uint8_t a_msg_hash[32])
{
    const size_t l_payload_size =
        4u + 4u + 32u + CHIPMUNK_LRS_POLY_QPACK_BYTES + 32u;
    uint8_t *l_payload = DAP_NEW_Z_SIZE(uint8_t, l_payload_size);
    if (!l_payload) {
        return -ENOMEM;
    }
    uint8_t *p = l_payload;
    uint8_t le[4];
    s_le32_store(le, a_params_id); s_hash_update_bytes(&p, le, sizeof(le));
    s_le32_store(le, a_ring_size); s_hash_update_bytes(&p, le, sizeof(le));
    s_hash_update_bytes(&p, a_ring_hash, 32u);
    s_hash_update_bytes(&p, a_key_image_packed, CHIPMUNK_LRS_POLY_QPACK_BYTES);
    s_hash_update_bytes(&p, a_msg_hash, 32u);
    int l_rc = s_hash_len_prefixed(a_out, "chipmunk-lrs-prefix",
                                   l_payload, l_payload_size);
    dap_memwipe(l_payload, l_payload_size);
    DAP_DELETE(l_payload);
    return l_rc;
}

/*
 * Step seed: H("chipmunk-lrs-challenge-step" || prefix || LE32(next_idx) ||
 *   LE32(K_T) || qpack(T_pk) || qpack(T_I)).  K_T = 2 binds the two
 *   commitment polys carried per branch.
 */
static int s_step_seed(uint8_t a_out[32],
                       const uint8_t a_prefix[32],
                       uint32_t a_next_idx,
                       const uint8_t a_T_pk_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                       const uint8_t a_T_I_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES])
{
    const size_t l_payload_size =
        32u + 4u + 4u + 2u * CHIPMUNK_LRS_POLY_QPACK_BYTES;
    uint8_t *l_payload = DAP_NEW_Z_SIZE(uint8_t, l_payload_size);
    if (!l_payload) {
        return -ENOMEM;
    }
    uint8_t *p = l_payload;
    uint8_t le[4];
    s_hash_update_bytes(&p, a_prefix, 32u);
    s_le32_store(le, a_next_idx); s_hash_update_bytes(&p, le, sizeof(le));
    s_le32_store(le, 2u); s_hash_update_bytes(&p, le, sizeof(le));
    s_hash_update_bytes(&p, a_T_pk_packed, CHIPMUNK_LRS_POLY_QPACK_BYTES);
    s_hash_update_bytes(&p, a_T_I_packed, CHIPMUNK_LRS_POLY_QPACK_BYTES);
    int l_rc = s_hash_len_prefixed(a_out, "chipmunk-lrs-challenge-step",
                                   l_payload, l_payload_size);
    dap_memwipe(l_payload, l_payload_size);
    DAP_DELETE(l_payload);
    return l_rc;
}

/*
 * Simulator: given (A_pk_i, A_I_i, P_i, I, c_i, z_i), compute the two
 * commitments T_pk_i' = sum A_pk[j]*z[j] - c*P, T_I_i' = sum A_I[j]*z[j] - c*I,
 * and q-pack them.  z_i is provided in centered domain [-B_z, B_z].
 */
static int s_simulate_branch(uint8_t a_T_pk_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                             uint8_t a_T_I_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                             const lrs_ring_member_ctx_t *a_ctx,
                             const chipmunk_poly_t *a_I_poly,
                             const chipmunk_poly_t *a_c_poly,
                             const chipmunk_poly_t a_z[CHIPMUNK_LRS_K], uint64_t q)
{
    chipmunk_poly_t l_sum_A_pk_z, l_sum_A_I_z;
    int l_rc = chipmunk_lrs_relation_eval(&l_sum_A_pk_z, a_ctx->A_pk, a_z, q);
    if (l_rc == 0) {
        l_rc = chipmunk_lrs_relation_eval(&l_sum_A_I_z, a_ctx->A_I, a_z, q);
    }
    if (l_rc != 0) {
        return l_rc;
    }

    chipmunk_poly_t l_cP, l_cI;
    {
        chipmunk_poly_t l_c_red = *a_c_poly;
        chipmunk_poly_t l_P_red = a_ctx->P_poly;
        chipmunk_poly_t l_I_red = *a_I_poly;
        for (size_t i = 0; i < CHIPMUNK_N; ++i) {
            l_c_red.coeffs[i] = chipmunk_mod_q_q((int64_t)l_c_red.coeffs[i], q);
        }
        l_rc = chipmunk_poly_ntt(&l_c_red);
        if (l_rc == CHIPMUNK_ERROR_SUCCESS) l_rc = chipmunk_poly_ntt(&l_P_red);
        if (l_rc == CHIPMUNK_ERROR_SUCCESS) l_rc = chipmunk_poly_ntt(&l_I_red);
        if (l_rc == CHIPMUNK_ERROR_SUCCESS) {
            chipmunk_poly_mul_ntt_q(&l_cP, &l_c_red, &l_P_red, q);
            chipmunk_poly_mul_ntt_q(&l_cI, &l_c_red, &l_I_red, q);
            l_rc = chipmunk_poly_invntt(&l_cP);
            if (l_rc == CHIPMUNK_ERROR_SUCCESS) l_rc = chipmunk_poly_invntt(&l_cI);
        }
        dap_memwipe(&l_c_red, sizeof(l_c_red));
        dap_memwipe(&l_P_red, sizeof(l_P_red));
        dap_memwipe(&l_I_red, sizeof(l_I_red));
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
            return l_rc;
        }
    }

    chipmunk_poly_t l_T_pk, l_T_I;
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        l_T_pk.coeffs[i] = chipmunk_mod_q_q((int64_t)l_sum_A_pk_z.coeffs[i] -
                                       (int64_t)l_cP.coeffs[i], q);
        l_T_I.coeffs[i]  = chipmunk_mod_q_q((int64_t)l_sum_A_I_z.coeffs[i] -
                                       (int64_t)l_cI.coeffs[i], q);
    }
    l_rc = chipmunk_lrs_poly_qpack(a_T_pk_packed, &l_T_pk, q);
    if (l_rc == 0) {
        l_rc = chipmunk_lrs_poly_qpack(a_T_I_packed, &l_T_I, q);
    }
    dap_memwipe(&l_sum_A_pk_z, sizeof(l_sum_A_pk_z));
    dap_memwipe(&l_sum_A_I_z, sizeof(l_sum_A_I_z));
    dap_memwipe(&l_cP, sizeof(l_cP));
    dap_memwipe(&l_cI, sizeof(l_cI));
    dap_memwipe(&l_T_pk, sizeof(l_T_pk));
    dap_memwipe(&l_T_I, sizeof(l_T_I));
    return l_rc;
}

/*
 * Real commitments for the signer's own branch: T_pk = sum A_pk*y,
 * T_I = sum A_I*y, q-packed.  y carries unreduced centered coefficients.
 */
static int s_real_commitments(uint8_t a_T_pk_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                              uint8_t a_T_I_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                              const chipmunk_poly_t a_A_pk[CHIPMUNK_LRS_K],
                              const chipmunk_poly_t a_A_I[CHIPMUNK_LRS_K],
                              const chipmunk_poly_t a_y[CHIPMUNK_LRS_K])
{
    chipmunk_poly_t l_T_pk, l_T_I;
    int l_rc = s_relation_eval_centered(&l_T_pk, a_A_pk, a_y, (uint64_t)CHIPMUNK_Q);
    if (l_rc == 0) {
        l_rc = s_relation_eval_centered(&l_T_I, a_A_I, a_y, (uint64_t)CHIPMUNK_Q);
    }
    if (l_rc == 0) {
        l_rc = chipmunk_lrs_poly_qpack(a_T_pk_packed, &l_T_pk, (uint64_t)CHIPMUNK_Q);
    }
    if (l_rc == 0) {
        l_rc = chipmunk_lrs_poly_qpack(a_T_I_packed, &l_T_I, (uint64_t)CHIPMUNK_Q);
    }
    dap_memwipe(&l_T_pk, sizeof(l_T_pk));
    dap_memwipe(&l_T_I, sizeof(l_T_I));
    return l_rc;
}

int chipmunk_lrs_sign(uint8_t *a_sig,
                      size_t a_sig_size,
                      const chipmunk_lrs_secret_key_t *a_sk,
                      const chipmunk_lrs_public_key_t *a_ring,
                      size_t a_ring_size,
                      const uint8_t *a_message,
                      size_t a_message_size,
                      const uint8_t a_randomness_seed[CHIPMUNK_LRS_SEED_BYTES], uint64_t q)
{
    if (!a_sig || !a_sk || !a_ring || !a_randomness_seed ||
        (!a_message && a_message_size != 0) ||
        a_message_size > UINT32_MAX ||
        a_ring_size < CHIPMUNK_LRS_RING_MIN ||
        a_ring_size > CHIPMUNK_LRS_RING_MAX) {
        return -EINVAL;
    }
    const size_t l_expected_size = chipmunk_lrs_signature_size((uint32_t)a_ring_size);
    if (l_expected_size == 0 || a_sig_size != l_expected_size) {
        return -EINVAL;
    }
    int l_rc = chipmunk_lrs_secret_key_validate(a_sk);
    if (l_rc != 0) {
        return l_rc;
    }

    /* Reconstruct signer's CLPK and find s in the canonical sorted ring. */
    chipmunk_lrs_public_key_t l_signer_pk;
    memset(&l_signer_pk, 0, sizeof(l_signer_pk));
    l_signer_pk.magic = CHIPMUNK_LRS_MAGIC_CLPK;
    l_signer_pk.params_id = CHIPMUNK_LRS_PARAMS_C0;
    memcpy(l_signer_pk.P, a_sk->P, CHIPMUNK_LRS_POLY_QPACK_BYTES);

    chipmunk_lrs_public_key_t *l_sorted_ring =
        DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, a_ring_size);
    lrs_ring_member_ctx_t *l_ring_ctx =
        DAP_NEW_Z_COUNT(lrs_ring_member_ctx_t, a_ring_size);
    if (!l_sorted_ring || !l_ring_ctx) {
        l_rc = -ENOMEM;
        goto out;
    }

    size_t l_signer_idx = 0;
    l_rc = s_canonicalise_ring(l_sorted_ring, a_ring_size, a_ring,
                               &l_signer_pk, &l_signer_idx);
    if (l_rc != 0) {
        goto out;
    }
    for (size_t i = 0; i < a_ring_size; ++i) {
        l_rc = s_derive_ring_member_ctx(&l_ring_ctx[i], &l_sorted_ring[i]);
        if (l_rc != 0) {
            goto out;
        }
    }

    /* Witness / key-image polynomial state. */
    chipmunk_poly_t l_x[CHIPMUNK_LRS_K];
    l_rc = chipmunk_lrs_derive_witness(l_x, a_sk->x_seed);
    if (l_rc != 0) {
        goto out;
    }
    chipmunk_poly_t l_I_poly;
    l_rc = chipmunk_lrs_relation_eval(&l_I_poly, l_ring_ctx[l_signer_idx].A_I, l_x, q);
    if (l_rc != 0) {
        goto out;
    }
    uint8_t l_I_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES];
    l_rc = chipmunk_lrs_poly_qpack(l_I_packed, &l_I_poly, q);
    if (l_rc != 0) {
        goto out;
    }

    /* Transcript prefix. */
    uint8_t l_ring_hash[32];
    l_rc = chipmunk_lrs_ring_hash(l_ring_hash, l_sorted_ring, a_ring_size);
    if (l_rc != 0) {
        goto out;
    }
    uint8_t l_msg_hash[32];
    l_rc = s_message_hash(l_msg_hash, a_message, a_message_size);
    if (l_rc != 0) {
        goto out;
    }
    uint8_t l_prefix[32];
    l_rc = s_signature_prefix(l_prefix, CHIPMUNK_LRS_PARAMS_C0,
                              (uint32_t)a_ring_size, l_ring_hash,
                              l_I_packed, l_msg_hash);
    if (l_rc != 0) {
        goto out;
    }

    /*
     * Per-call mask/simulation seed material binds the caller-provided
     * randomness, the signer's pk, the message, the ring, and the key
     * image.  Index space below is unique per (attempt, ring_member, j).
     */
    const size_t l_rnd_seed_size =
        CHIPMUNK_LRS_SEED_BYTES + CHIPMUNK_LRS_POLY_QPACK_BYTES +
        32u + 32u + CHIPMUNK_LRS_POLY_QPACK_BYTES;
    uint8_t *l_rnd_seed = DAP_NEW_Z_SIZE(uint8_t, l_rnd_seed_size);
    if (!l_rnd_seed) {
        l_rc = -ENOMEM;
        goto out;
    }
    {
        uint8_t *p = l_rnd_seed;
        memcpy(p, a_randomness_seed, CHIPMUNK_LRS_SEED_BYTES);
        p += CHIPMUNK_LRS_SEED_BYTES;
        memcpy(p, a_sk->P, CHIPMUNK_LRS_POLY_QPACK_BYTES);
        p += CHIPMUNK_LRS_POLY_QPACK_BYTES;
        memcpy(p, l_ring_hash, 32u); p += 32u;
        memcpy(p, l_msg_hash, 32u); p += 32u;
        memcpy(p, l_I_packed, CHIPMUNK_LRS_POLY_QPACK_BYTES);
    }

    /* Per-attempt working buffers. */
    chipmunk_poly_t l_y[CHIPMUNK_LRS_K];
    chipmunk_poly_t *l_z_branches =
        DAP_NEW_Z_COUNT(chipmunk_poly_t, a_ring_size * CHIPMUNK_LRS_K);
    uint8_t l_T_pk_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES];
    uint8_t l_T_I_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES];
    uint8_t l_seed_at_next[32];
    uint8_t l_seed_at_zero[32];
    chipmunk_poly_t l_cx[CHIPMUNK_LRS_K];
    chipmunk_poly_t l_c_poly;
    if (!l_z_branches) {
        l_rc = -ENOMEM;
        dap_memwipe(l_rnd_seed, l_rnd_seed_size);
        DAP_DELETE(l_rnd_seed);
        goto out;
    }

    int l_done = 0;
    for (uint32_t l_attempt = 0;
         l_attempt < CHIPMUNK_LRS_MAX_ATTEMPTS && !l_done; ++l_attempt) {

        /* Sample y for the signer's real branch. */
        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            l_rc = s_h_to_wide_poly(&l_y[j], "chipmunk-lrs-sig-mask",
                                    CHIPMUNK_LRS_PARAMS_C0, l_rnd_seed,
                                    l_rnd_seed_size,
                                    l_attempt * CHIPMUNK_LRS_K + j,
                                    CHIPMUNK_LRS_MASK_BOUND, q);
            if (l_rc != 0) {
                goto loop_out;
            }
        }

        /* Sample simulator z for ALL positions (CT-safe: no branch on signer_idx). */
        for (size_t i = 0u; i < a_ring_size; ++i) {
            for (uint32_t j = 0u; j < CHIPMUNK_LRS_K; ++j) {
                l_rc = s_h_to_wide_poly(
                    &l_z_branches[i * CHIPMUNK_LRS_K + j],
                    "chipmunk-lrs-sig-sim-z",
                    CHIPMUNK_LRS_PARAMS_C0, l_rnd_seed, l_rnd_seed_size,
                    l_attempt * (uint32_t)(a_ring_size * CHIPMUNK_LRS_K) +
                        (uint32_t)(i * CHIPMUNK_LRS_K) + j,
                    CHIPMUNK_LRS_RESPONSE_BOUND, q);
                if (l_rc != 0) {
                    goto loop_out;
                }
            }
        }

        /* Compute signer's real commitments, then seed for the (s+1)%n step. */
        l_rc = s_real_commitments(l_T_pk_packed, l_T_I_packed,
                                  l_ring_ctx[l_signer_idx].A_pk,
                                  l_ring_ctx[l_signer_idx].A_I, l_y);
        if (l_rc != 0) {
            goto loop_out;
        }

        size_t l_next_idx = (l_signer_idx + 1u) % a_ring_size;
        l_rc = s_step_seed(l_seed_at_next, l_prefix, (uint32_t)l_next_idx,
                           l_T_pk_packed, l_T_I_packed);
        if (l_rc != 0) {
            goto loop_out;
        }
        if (l_next_idx == 0u) {
            memcpy(l_seed_at_zero, l_seed_at_next, 32u);
        }

        /* Walk the chain through all non-signer indices. */
        for (size_t step = 1; step < a_ring_size; ++step) {
            size_t cur = (l_signer_idx + step) % a_ring_size;
            l_rc = chipmunk_lrs_h_to_sparse_ternary(&l_c_poly,
                                                    "chipmunk-lrs-challenge",
                                                    CHIPMUNK_LRS_PARAMS_C0,
                                                    l_seed_at_next);
            if (l_rc != 0) {
                goto loop_out;
            }
            l_rc = s_simulate_branch(l_T_pk_packed, l_T_I_packed,
                                     &l_ring_ctx[cur], &l_I_poly,
                                     &l_c_poly,
                                     &l_z_branches[cur * CHIPMUNK_LRS_K], q);
            if (l_rc != 0) {
                goto loop_out;
            }
            size_t after = (cur + 1u) % a_ring_size;
            l_rc = s_step_seed(l_seed_at_next, l_prefix, (uint32_t)after,
                               l_T_pk_packed, l_T_I_packed);
            if (l_rc != 0) {
                goto loop_out;
            }
            if (after == 0u) {
                memcpy(l_seed_at_zero, l_seed_at_next, 32u);
            }
        }

        /* At this point l_seed_at_next is the seed AT the signer's index. */
        l_rc = chipmunk_lrs_h_to_sparse_ternary(&l_c_poly,
                                                "chipmunk-lrs-challenge",
                                                CHIPMUNK_LRS_PARAMS_C0,
                                                l_seed_at_next);
        if (l_rc != 0) {
            goto loop_out;
        }
        l_rc = s_mul_challenge_witness_centered(l_cx, &l_c_poly, l_x, q);
        if (l_rc != 0) {
            goto loop_out;
        }

        bool l_reject = false;
        for (uint32_t j = 0; j < CHIPMUNK_LRS_K && !l_reject; ++j) {
            for (size_t i = 0; i < CHIPMUNK_N; ++i) {
                int32_t l_zi = l_y[j].coeffs[i] + l_cx[j].coeffs[i];
                if (l_zi < -CHIPMUNK_LRS_RESPONSE_BOUND ||
                    l_zi > CHIPMUNK_LRS_RESPONSE_BOUND) {
                    l_reject = true;
                    break;
                }
            }
        }
        if (l_reject) {
            continue;
        }

        /* Materialise z_s polynomials into l_z_branches and write the wire. */
        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            chipmunk_poly_t *l_zs = &l_z_branches[l_signer_idx * CHIPMUNK_LRS_K + j];
            for (size_t i = 0; i < CHIPMUNK_N; ++i) {
                int32_t l_zi = l_y[j].coeffs[i] + l_cx[j].coeffs[i];
                l_zs->coeffs[i] = s_center_q(chipmunk_mod_q_q((int64_t)l_zi, q), q);
            }
        }
        l_done = 1;
    }

    if (!l_done) {
        l_rc = -EAGAIN;
        goto loop_out;
    }

    /* Serialise the canonical wire. */
    memset(a_sig, 0, a_sig_size);
    uint8_t *p = a_sig;
    s_le32_store(p, CHIPMUNK_LRS_MAGIC_CLRS); p += 4u;
    s_le32_store(p, CHIPMUNK_LRS_PARAMS_C0); p += 4u;
    s_le32_store(p, (uint32_t)a_ring_size); p += 4u;
    s_le32_store(p, 0u); p += 4u; /* flags */
    s_le32_store(p, CHIPMUNK_LRS_SIG_HEADER_BYTES); p += 4u;
    s_le32_store(p, (uint32_t)(a_ring_size * CHIPMUNK_LRS_SIG_PER_MEMBER_BYTES)); p += 4u;
    s_le32_store(p, 0u); p += 4u; /* reserved0 */
    s_le32_store(p, 0u); p += 4u; /* reserved1 */
    memcpy(p, l_ring_hash, 32u); p += 32u;
    memcpy(p, l_I_packed, CHIPMUNK_LRS_POLY_QPACK_BYTES);
    p += CHIPMUNK_LRS_POLY_QPACK_BYTES;
    memcpy(p, l_seed_at_zero, 32u); p += 32u;
    for (size_t i = 0; i < a_ring_size; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            chipmunk_poly_t l_z = l_z_branches[i * CHIPMUNK_LRS_K + j];
            /* Convert centered to canonical [0,q) for qpack. */
            for (size_t k = 0; k < CHIPMUNK_N; ++k) {
                if (l_z.coeffs[k] < 0) {
                    l_z.coeffs[k] += q;
                }
            }
            l_rc = chipmunk_lrs_poly_qpack(p, &l_z, q);
            if (l_rc != 0) {
                dap_memwipe(&l_z, sizeof(l_z));
                goto loop_out;
            }
            p += CHIPMUNK_LRS_POLY_QPACK_BYTES;
            dap_memwipe(&l_z, sizeof(l_z));
        }
    }
    l_rc = 0;

loop_out:
    dap_memwipe(l_x, sizeof(l_x));
    dap_memwipe(l_y, sizeof(l_y));
    dap_memwipe(l_cx, sizeof(l_cx));
    dap_memwipe(&l_c_poly, sizeof(l_c_poly));
    dap_memwipe(&l_I_poly, sizeof(l_I_poly));
    dap_memwipe(l_I_packed, sizeof(l_I_packed));
    dap_memwipe(l_T_pk_packed, sizeof(l_T_pk_packed));
    dap_memwipe(l_T_I_packed, sizeof(l_T_I_packed));
    dap_memwipe(l_seed_at_next, sizeof(l_seed_at_next));
    dap_memwipe(l_seed_at_zero, sizeof(l_seed_at_zero));
    dap_memwipe(l_prefix, sizeof(l_prefix));
    dap_memwipe(l_ring_hash, sizeof(l_ring_hash));
    dap_memwipe(l_msg_hash, sizeof(l_msg_hash));
    dap_memwipe(l_z_branches,
                sizeof(chipmunk_poly_t) * a_ring_size * CHIPMUNK_LRS_K);
    DAP_DELETE(l_z_branches);
    dap_memwipe(l_rnd_seed, l_rnd_seed_size);
    DAP_DELETE(l_rnd_seed);
out:
    if (l_ring_ctx) {
        dap_memwipe(l_ring_ctx, sizeof(*l_ring_ctx) * a_ring_size);
        DAP_DELETE(l_ring_ctx);
    }
    if (l_sorted_ring) {
        dap_memwipe(l_sorted_ring, sizeof(*l_sorted_ring) * a_ring_size);
        DAP_DELETE(l_sorted_ring);
    }
    dap_memwipe(&l_signer_pk, sizeof(l_signer_pk));
    return l_rc;
}

int chipmunk_lrs_verify(const uint8_t *a_sig,
                        size_t a_sig_size,
                        const chipmunk_lrs_public_key_t *a_ring,
                        size_t a_ring_size,
                        const uint8_t *a_message,
                        size_t a_message_size, uint64_t q)
{
    if (!a_sig || !a_ring || (!a_message && a_message_size != 0) ||
        a_message_size > UINT32_MAX ||
        a_ring_size < CHIPMUNK_LRS_RING_MIN ||
        a_ring_size > CHIPMUNK_LRS_RING_MAX) {
        return -EINVAL;
    }
    const size_t l_expected_size = chipmunk_lrs_signature_size((uint32_t)a_ring_size);
    if (l_expected_size == 0 || a_sig_size != l_expected_size) {
        return -EINVAL;
    }

    const uint8_t *p = a_sig;
    uint32_t l_magic = s_le32_load(p); p += 4u;
    uint32_t l_params = s_le32_load(p); p += 4u;
    uint32_t l_ring_size = s_le32_load(p); p += 4u;
    uint32_t l_flags = s_le32_load(p); p += 4u;
    uint32_t l_hdr_bytes = s_le32_load(p); p += 4u;
    uint32_t l_resp_bytes = s_le32_load(p); p += 4u;
    uint32_t l_r0 = s_le32_load(p); p += 4u;
    uint32_t l_r1 = s_le32_load(p); p += 4u;
    if (l_magic != CHIPMUNK_LRS_MAGIC_CLRS ||
        l_params != CHIPMUNK_LRS_PARAMS_C0 ||
        l_ring_size != (uint32_t)a_ring_size ||
        l_flags != 0u || l_r0 != 0u || l_r1 != 0u ||
        l_hdr_bytes != CHIPMUNK_LRS_SIG_HEADER_BYTES ||
        l_resp_bytes !=
            (uint32_t)(a_ring_size * CHIPMUNK_LRS_SIG_PER_MEMBER_BYTES)) {
        return -EINVAL;
    }

    chipmunk_lrs_public_key_t *l_sorted_ring =
        DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, a_ring_size);
    lrs_ring_member_ctx_t *l_ring_ctx =
        DAP_NEW_Z_COUNT(lrs_ring_member_ctx_t, a_ring_size);
    chipmunk_poly_t *l_z_branches =
        DAP_NEW_Z_COUNT(chipmunk_poly_t, a_ring_size * CHIPMUNK_LRS_K);
    if (!l_sorted_ring || !l_ring_ctx || !l_z_branches) {
        if (l_z_branches) DAP_DELETE(l_z_branches);
        if (l_ring_ctx) DAP_DELETE(l_ring_ctx);
        if (l_sorted_ring) DAP_DELETE(l_sorted_ring);
        return -ENOMEM;
    }

    int l_rc = s_canonicalise_ring(l_sorted_ring, a_ring_size, a_ring,
                                   NULL, NULL);
    if (l_rc != 0) {
        goto vfail;
    }
    for (size_t i = 0; i < a_ring_size; ++i) {
        l_rc = s_derive_ring_member_ctx(&l_ring_ctx[i], &l_sorted_ring[i]);
        if (l_rc != 0) {
            goto vfail;
        }
    }

    uint8_t l_ring_hash[32];
    l_rc = chipmunk_lrs_ring_hash(l_ring_hash, l_sorted_ring, a_ring_size);
    if (l_rc != 0) {
        goto vfail;
    }
    if (s_memcmp_ct(p, l_ring_hash, 32u) != 0) {
        l_rc = -EINVAL;
        goto vfail;
    }
    p += 32u;

    chipmunk_poly_t l_I_poly;
    l_rc = chipmunk_lrs_poly_qunpack(&l_I_poly, p, q);
    if (l_rc != 0) {
        goto vfail;
    }
    bool l_image_nonzero = false;
    for (size_t i = 0; i < CHIPMUNK_N && !l_image_nonzero; ++i) {
        l_image_nonzero |= (l_I_poly.coeffs[i] != 0);
    }
    if (!l_image_nonzero) {
        l_rc = -EINVAL;
        goto vfail;
    }
    uint8_t l_I_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES];
    memcpy(l_I_packed, p, sizeof(l_I_packed));
    p += CHIPMUNK_LRS_POLY_QPACK_BYTES;

    uint8_t l_c0_seed[32];
    memcpy(l_c0_seed, p, 32u);
    p += 32u;

    /* Decode all responses with RB2 norm gate. */
    for (size_t i = 0; i < a_ring_size; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            chipmunk_poly_t *l_z = &l_z_branches[i * CHIPMUNK_LRS_K + j];
            l_rc = chipmunk_lrs_poly_qunpack(l_z, p, q);
            if (l_rc != 0) {
                goto vfail;
            }
            l_rc = chipmunk_lrs_poly_chknorm_centered(l_z,
                                                      CHIPMUNK_LRS_RESPONSE_BOUND, q);
            if (l_rc != 0) {
                l_rc = (l_rc == 1) ? -EINVAL : l_rc;
                goto vfail;
            }
            for (size_t k = 0; k < CHIPMUNK_N; ++k) {
                int32_t v = l_z->coeffs[k];
                if (v > (int32_t)(q / 2)) {
                    l_z->coeffs[k] = v - (int32_t)q;
                }
            }
            p += CHIPMUNK_LRS_POLY_QPACK_BYTES;
        }
    }

    uint8_t l_msg_hash[32];
    l_rc = s_message_hash(l_msg_hash, a_message, a_message_size);
    if (l_rc != 0) {
        goto vfail;
    }
    uint8_t l_prefix[32];
    l_rc = s_signature_prefix(l_prefix, CHIPMUNK_LRS_PARAMS_C0,
                              (uint32_t)a_ring_size, l_ring_hash,
                              l_I_packed, l_msg_hash);
    if (l_rc != 0) {
        goto vfail;
    }

    uint8_t l_seed[32];
    memcpy(l_seed, l_c0_seed, 32u);
    chipmunk_poly_t l_c_poly;
    uint8_t l_T_pk_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES];
    uint8_t l_T_I_packed[CHIPMUNK_LRS_POLY_QPACK_BYTES];

    for (size_t i = 0; i < a_ring_size; ++i) {
        l_rc = chipmunk_lrs_h_to_sparse_ternary(&l_c_poly,
                                                "chipmunk-lrs-challenge",
                                                CHIPMUNK_LRS_PARAMS_C0,
                                                l_seed);
        if (l_rc != 0) {
            goto vfail;
        }
        l_rc = s_simulate_branch(l_T_pk_packed, l_T_I_packed,
                                 &l_ring_ctx[i], &l_I_poly,
                                 &l_c_poly,
                                 &l_z_branches[i * CHIPMUNK_LRS_K], q);
        if (l_rc != 0) {
            goto vfail;
        }
        size_t after = (i + 1u) % a_ring_size;
        l_rc = s_step_seed(l_seed, l_prefix, (uint32_t)after,
                           l_T_pk_packed, l_T_I_packed);
        if (l_rc != 0) {
            goto vfail;
        }
    }

    l_rc = (s_memcmp_ct(l_seed, l_c0_seed, 32u) == 0) ? 0 : -EINVAL;

    dap_memwipe(&l_c_poly, sizeof(l_c_poly));
    dap_memwipe(&l_I_poly, sizeof(l_I_poly));
    dap_memwipe(l_T_pk_packed, sizeof(l_T_pk_packed));
    dap_memwipe(l_T_I_packed, sizeof(l_T_I_packed));
    dap_memwipe(l_prefix, sizeof(l_prefix));
    dap_memwipe(l_seed, sizeof(l_seed));
    dap_memwipe(l_msg_hash, sizeof(l_msg_hash));
    dap_memwipe(l_c0_seed, sizeof(l_c0_seed));
    dap_memwipe(l_I_packed, sizeof(l_I_packed));
    dap_memwipe(l_ring_hash, sizeof(l_ring_hash));
    dap_memwipe(l_z_branches,
                sizeof(chipmunk_poly_t) * a_ring_size * CHIPMUNK_LRS_K);
    DAP_DELETE(l_z_branches);
    dap_memwipe(l_ring_ctx, sizeof(*l_ring_ctx) * a_ring_size);
    DAP_DELETE(l_ring_ctx);
    dap_memwipe(l_sorted_ring, sizeof(*l_sorted_ring) * a_ring_size);
    DAP_DELETE(l_sorted_ring);
    return l_rc;

vfail:
    dap_memwipe(l_z_branches,
                sizeof(chipmunk_poly_t) * a_ring_size * CHIPMUNK_LRS_K);
    DAP_DELETE(l_z_branches);
    dap_memwipe(l_ring_ctx, sizeof(*l_ring_ctx) * a_ring_size);
    DAP_DELETE(l_ring_ctx);
    dap_memwipe(l_sorted_ring, sizeof(*l_sorted_ring) * a_ring_size);
    DAP_DELETE(l_sorted_ring);
    return l_rc != 0 ? l_rc : -EINVAL;
}
