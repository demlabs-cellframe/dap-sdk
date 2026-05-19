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

static int32_t s_center_q(int32_t a_v)
{
    if (a_v > CHIPMUNK_Q / 2) {
        return a_v - CHIPMUNK_Q;
    }
    return a_v;
}

static int32_t s_mod_q_i64(int64_t a_v)
{
    int64_t l_r = a_v % CHIPMUNK_Q;
    if (l_r < 0) {
        l_r += CHIPMUNK_Q;
    }
    return (int32_t)l_r;
}

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

static uint16_t s_xof_u16(lrs_xof_reader_t *a_reader)
{
    uint16_t l_lo = s_xof_u8(a_reader);
    uint16_t l_hi = s_xof_u8(a_reader);
    return (uint16_t)(l_lo | (uint16_t)(l_hi << 8));
}

int chipmunk_lrs_poly_qpack(uint8_t a_out[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                            const chipmunk_poly_t *a_poly)
{
    if (!a_out || !a_poly) {
        return -EINVAL;
    }

    memset(a_out, 0, CHIPMUNK_LRS_POLY_QPACK_BYTES);
    uint64_t l_acc = 0;
    uint32_t l_bits = 0;
    size_t l_pos = 0;

    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        int32_t l_coeff = a_poly->coeffs[i];
        if (l_coeff < 0) {
            if (l_coeff < -CHIPMUNK_Q / 2) {
                return -EINVAL;
            }
            l_coeff += CHIPMUNK_Q;
        }
        if (l_coeff < 0 || l_coeff >= CHIPMUNK_Q) {
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
                              const uint8_t a_in[CHIPMUNK_LRS_POLY_QPACK_BYTES])
{
    if (!a_poly || !a_in) {
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
        if (l_coeff >= CHIPMUNK_Q) {
            return -EINVAL;
        }
        a_poly->coeffs[i] = (int32_t)l_coeff;
    }

    return l_pos == CHIPMUNK_LRS_POLY_QPACK_BYTES && l_bits == 0 ? 0 : -EINVAL;
}

int chipmunk_lrs_poly_chknorm_centered(const chipmunk_poly_t *a_poly,
                                       int32_t a_bound)
{
    if (!a_poly || a_bound < 0) {
        return -EINVAL;
    }
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        int32_t l_c = a_poly->coeffs[i];
        if (l_c < 0) {
            if (l_c < -CHIPMUNK_Q / 2) {
                return -EINVAL;
            }
        } else if (l_c >= CHIPMUNK_Q) {
            return -EINVAL;
        }
        l_c = s_center_q(l_c);
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
                             uint32_t a_index)
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

    const uint32_t l_threshold = UINT32_MAX - (UINT32_MAX % CHIPMUNK_Q);
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        uint32_t l_v;
        do {
            l_v = s_xof_u32(&l_reader);
        } while (l_v >= l_threshold);
        a_poly->coeffs[i] = (int32_t)(l_v % CHIPMUNK_Q);
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
                             const uint8_t a_pk_seed[CHIPMUNK_LRS_SEED_BYTES])
{
    if (!a_A_pk || !a_pk_seed) {
        return -EINVAL;
    }
    for (uint32_t i = 0; i < CHIPMUNK_LRS_K; ++i) {
        int l_rc = chipmunk_lrs_h_to_poly_q(&a_A_pk[i], "chipmunk-lrs-pk-matrix",
                                            CHIPMUNK_LRS_PARAMS_C0,
                                            a_pk_seed, CHIPMUNK_LRS_SEED_BYTES, i);
        if (l_rc != 0) {
            return l_rc;
        }
    }
    return 0;
}

int chipmunk_lrs_derive_A_I(chipmunk_poly_t a_A_I[CHIPMUNK_LRS_K],
                            const uint8_t a_pk_seed[CHIPMUNK_LRS_SEED_BYTES],
                            const uint8_t a_P[CHIPMUNK_LRS_POLY_QPACK_BYTES])
{
    if (!a_A_I || !a_pk_seed || !a_P) {
        return -EINVAL;
    }

    uint8_t l_seed[CHIPMUNK_LRS_SEED_BYTES + CHIPMUNK_LRS_POLY_QPACK_BYTES];
    memcpy(l_seed, a_pk_seed, CHIPMUNK_LRS_SEED_BYTES);
    memcpy(l_seed + CHIPMUNK_LRS_SEED_BYTES, a_P, CHIPMUNK_LRS_POLY_QPACK_BYTES);

    for (uint32_t i = 0; i < CHIPMUNK_LRS_K; ++i) {
        int l_rc = chipmunk_lrs_h_to_poly_q(&a_A_I[i], "chipmunk-lrs-image-matrix",
                                            CHIPMUNK_LRS_PARAMS_C0,
                                            l_seed, sizeof(l_seed), i);
        if (l_rc != 0) {
            dap_memwipe(l_seed, sizeof(l_seed));
            return l_rc;
        }
    }

    dap_memwipe(l_seed, sizeof(l_seed));
    return 0;
}

int chipmunk_lrs_relation_eval(chipmunk_poly_t *a_out,
                               const chipmunk_poly_t a_A[CHIPMUNK_LRS_K],
                               const chipmunk_poly_t a_x[CHIPMUNK_LRS_K])
{
    if (!a_out || !a_A || !a_x) {
        return -EINVAL;
    }

    memset(a_out, 0, sizeof(*a_out));

    for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
        chipmunk_poly_t l_A = a_A[j];
        chipmunk_poly_t l_x = a_x[j];
        chipmunk_poly_t l_prod;

        int l_rc = chipmunk_poly_ntt(&l_A);
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
            return l_rc;
        }
        l_rc = chipmunk_poly_ntt(&l_x);
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
            return l_rc;
        }
        chipmunk_poly_mul_ntt(&l_prod, &l_A, &l_x);
        l_rc = chipmunk_poly_invntt(&l_prod);
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
            return l_rc;
        }

        for (size_t i = 0; i < CHIPMUNK_N; ++i) {
            a_out->coeffs[i] = s_mod_q_i64((int64_t)a_out->coeffs[i] + l_prod.coeffs[i]);
        }
    }

    return 0;
}

int chipmunk_lrs_keypair_from_seeds(chipmunk_lrs_public_key_t *a_pk,
                                    chipmunk_lrs_secret_key_t *a_sk,
                                    const uint8_t a_x_seed[CHIPMUNK_LRS_SEED_BYTES],
                                    const uint8_t a_pk_seed[CHIPMUNK_LRS_SEED_BYTES])
{
    if (!a_pk || !a_sk || !a_x_seed || !a_pk_seed) {
        return -EINVAL;
    }

    chipmunk_poly_t l_x[CHIPMUNK_LRS_K];
    chipmunk_poly_t l_A[CHIPMUNK_LRS_K];
    chipmunk_poly_t l_P;

    int l_rc = chipmunk_lrs_derive_witness(l_x, a_x_seed);
    if (l_rc != 0) {
        return l_rc;
    }
    l_rc = chipmunk_lrs_derive_A_pk(l_A, a_pk_seed);
    if (l_rc != 0) {
        dap_memwipe(l_x, sizeof(l_x));
        return l_rc;
    }
    l_rc = chipmunk_lrs_relation_eval(&l_P, l_A, l_x);
    if (l_rc != 0) {
        dap_memwipe(l_x, sizeof(l_x));
        dap_memwipe(l_A, sizeof(l_A));
        return l_rc;
    }

    memset(a_pk, 0, sizeof(*a_pk));
    a_pk->magic = CHIPMUNK_LRS_MAGIC_CLPK;
    a_pk->params_id = CHIPMUNK_LRS_PARAMS_C0;
    memcpy(a_pk->pk_seed, a_pk_seed, CHIPMUNK_LRS_SEED_BYTES);
    l_rc = chipmunk_lrs_poly_qpack(a_pk->P, &l_P);
    if (l_rc == 0) {
        memset(a_sk, 0, sizeof(*a_sk));
        a_sk->magic = CHIPMUNK_LRS_MAGIC_CLSK;
        a_sk->params_id = CHIPMUNK_LRS_PARAMS_C0;
        memcpy(a_sk->x_seed, a_x_seed, CHIPMUNK_LRS_SEED_BYTES);
        memcpy(a_sk->pk_seed, a_pk_seed, CHIPMUNK_LRS_SEED_BYTES);
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
    l_rc = chipmunk_lrs_derive_A_I(l_A_I, a_sk->pk_seed, a_sk->P);
    if (l_rc == 0) {
        l_rc = chipmunk_lrs_relation_eval(&l_I, l_A_I, l_x);
    }
    if (l_rc == 0) {
        l_rc = chipmunk_lrs_poly_qpack(a_key_image, &l_I);
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
    int l_rc = chipmunk_lrs_poly_qunpack(&l_P, a_pk->P);
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
    int l_rc = chipmunk_lrs_keypair_from_seeds(&l_pk, &l_sk, a_sk->x_seed, a_sk->pk_seed);
    if (l_rc != 0) {
        return l_rc;
    }
    l_rc = memcmp(l_sk.P, a_sk->P, CHIPMUNK_LRS_POLY_QPACK_BYTES) == 0 ? 0 : -EINVAL;
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
