/**
 * @file dap_kem.c
 * @brief Generic KEM dispatch — routes to ALL KEM algorithm implementations.
 *
 * Covers: ML-KEM (512/768/1024), NTRU Prime (sntrup761), NewHope CPA-KEM.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <string.h>
#include "dap_kem.h"

/* ===== ML-KEM raw API (from per-K libraries) ===== */

#define MLKEM_DECL(k) \
    int dap_mlkem##k##_kem_keypair(uint8_t *, uint8_t *); \
    int dap_mlkem##k##_kem_enc(uint8_t *, uint8_t *, const uint8_t *); \
    int dap_mlkem##k##_kem_dec(uint8_t *, const uint8_t *, const uint8_t *); \
    int dap_mlkem##k##_ctx_init(void *, const uint8_t *, const uint8_t *); \
    int dap_mlkem##k##_ctx_init_enc(void *, const uint8_t *); \
    int dap_mlkem##k##_kem_enc_ctx(uint8_t *, uint8_t *, const void *); \
    int dap_mlkem##k##_kem_dec_ctx(uint8_t *, const uint8_t *, const void *);

MLKEM_DECL(512)
MLKEM_DECL(768)
MLKEM_DECL(1024)

/* ===== NTRU Prime (sntrup761) raw API ===== */

int sntrup761_keypair(uint8_t *pk, uint8_t *sk);
int sntrup761_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int sntrup761_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

/* ===== NewHope CPA-KEM raw API ===== */

int crypto_kem_keypair(unsigned char *pk, unsigned char *sk);
int crypto_kem_enc(unsigned char *ct, unsigned char *ss, const unsigned char *pk);
int crypto_kem_dec(unsigned char *ss, const unsigned char *ct, const unsigned char *sk);

/* ===== Internal vtable ===== */

typedef struct {
    const char *name;
    bool deprecated;
    size_t pk_size, sk_size, ct_size, ss_size;
    size_t ctx_state_size;
    int (*keypair)(uint8_t *, uint8_t *);
    int (*encaps)(uint8_t *, uint8_t *, const uint8_t *);
    int (*decaps)(uint8_t *, const uint8_t *, const uint8_t *);
    int (*ctx_init)(void *, const uint8_t *, const uint8_t *);
    int (*ctx_init_enc)(void *, const uint8_t *);
    int (*ctx_encaps)(uint8_t *, uint8_t *, const void *);
    int (*ctx_decaps)(uint8_t *, const uint8_t *, const void *);
} s_kem_vtable_t;

static const s_kem_vtable_t s_vtable[DAP_KEM_ALG_COUNT] = {
    [DAP_KEM_ALG_ML_KEM_512] = {
        .name = "ML-KEM-512", .pk_size = 800, .sk_size = 1632, .ct_size = 768, .ss_size = 32,
        .ctx_state_size = 8192,
        .keypair = dap_mlkem512_kem_keypair,
        .encaps  = dap_mlkem512_kem_enc,
        .decaps  = dap_mlkem512_kem_dec,
        .ctx_init     = dap_mlkem512_ctx_init,
        .ctx_init_enc = dap_mlkem512_ctx_init_enc,
        .ctx_encaps   = dap_mlkem512_kem_enc_ctx,
        .ctx_decaps   = dap_mlkem512_kem_dec_ctx,
    },
    [DAP_KEM_ALG_ML_KEM_768] = {
        .name = "ML-KEM-768", .pk_size = 1184, .sk_size = 2400, .ct_size = 1088, .ss_size = 32,
        .ctx_state_size = 12288,
        .keypair = dap_mlkem768_kem_keypair,
        .encaps  = dap_mlkem768_kem_enc,
        .decaps  = dap_mlkem768_kem_dec,
        .ctx_init     = dap_mlkem768_ctx_init,
        .ctx_init_enc = dap_mlkem768_ctx_init_enc,
        .ctx_encaps   = dap_mlkem768_kem_enc_ctx,
        .ctx_decaps   = dap_mlkem768_kem_dec_ctx,
    },
    [DAP_KEM_ALG_ML_KEM_1024] = {
        .name = "ML-KEM-1024", .pk_size = 1568, .sk_size = 3168, .ct_size = 1568, .ss_size = 32,
        .ctx_state_size = 16384,
        .keypair = dap_mlkem1024_kem_keypair,
        .encaps  = dap_mlkem1024_kem_enc,
        .decaps  = dap_mlkem1024_kem_dec,
        .ctx_init     = dap_mlkem1024_ctx_init,
        .ctx_init_enc = dap_mlkem1024_ctx_init_enc,
        .ctx_encaps   = dap_mlkem1024_kem_enc_ctx,
        .ctx_decaps   = dap_mlkem1024_kem_dec_ctx,
    },
    [DAP_KEM_ALG_NTRU_PRIME] = {
        .name = "NTRU-Prime-761", .pk_size = 1522, .sk_size = 1936, .ct_size = 1522, .ss_size = 32,
        .keypair = sntrup761_keypair,
        .encaps  = sntrup761_enc,
        .decaps  = sntrup761_dec,
    },
    [DAP_KEM_ALG_NEWHOPE] = {
        .name = "NewHope-1024-CPA", .deprecated = true,
        .pk_size = 1824, .sk_size = 1792, .ct_size = 2176, .ss_size = 32,
        .keypair = (int (*)(uint8_t *, uint8_t *))crypto_kem_keypair,
        .encaps  = (int (*)(uint8_t *, uint8_t *, const uint8_t *))crypto_kem_enc,
        .decaps  = (int (*)(uint8_t *, const uint8_t *, const uint8_t *))crypto_kem_dec,
    },
};

static inline const s_kem_vtable_t *s_get(dap_kem_alg_t a_alg)
{
    return (unsigned)a_alg < DAP_KEM_ALG_COUNT ? &s_vtable[a_alg] : NULL;
}

/* ===== Algorithm metadata ===== */

const char *dap_kem_alg_name(dap_kem_alg_t a_alg)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    return v ? v->name : "unknown";
}

bool dap_kem_alg_is_deprecated(dap_kem_alg_t a_alg)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    return v ? v->deprecated : true;
}

bool dap_kem_alg_has_ctx(dap_kem_alg_t a_alg)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    return v && v->ctx_init;
}

size_t dap_kem_publickey_size(dap_kem_alg_t a_alg)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    return v ? v->pk_size : 0;
}

size_t dap_kem_secretkey_size(dap_kem_alg_t a_alg)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    return v ? v->sk_size : 0;
}

size_t dap_kem_ciphertext_size(dap_kem_alg_t a_alg)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    return v ? v->ct_size : 0;
}

size_t dap_kem_sharedsecret_size(dap_kem_alg_t a_alg)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    return v ? v->ss_size : 0;
}

/* ===== Type conversions ===== */

dap_kem_alg_t dap_kem_alg_from_enc_key_type(dap_enc_key_type_t a_key_type)
{
    switch (a_key_type) {
    case DAP_ENC_KEY_TYPE_ML_KEM:
    case DAP_ENC_KEY_TYPE_KEM_KYBER512:
        return DAP_KEM_ALG_ML_KEM_512;
    case DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME:
        return DAP_KEM_ALG_NTRU_PRIME;
    case DAP_ENC_KEY_TYPE_RLWE_NEWHOPE_CPA_KEM:
        return DAP_KEM_ALG_NEWHOPE;
    default:
        return DAP_KEM_ALG_COUNT;
    }
}

dap_enc_key_type_t dap_kem_alg_to_enc_key_type(dap_kem_alg_t a_alg)
{
    switch (a_alg) {
    case DAP_KEM_ALG_ML_KEM_512:  return DAP_ENC_KEY_TYPE_ML_KEM;
    case DAP_KEM_ALG_ML_KEM_768:  return DAP_ENC_KEY_TYPE_ML_KEM;
    case DAP_KEM_ALG_ML_KEM_1024: return DAP_ENC_KEY_TYPE_ML_KEM;
    case DAP_KEM_ALG_NTRU_PRIME:  return DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME;
    case DAP_KEM_ALG_NEWHOPE:     return DAP_ENC_KEY_TYPE_RLWE_NEWHOPE_CPA_KEM;
    default:                      return DAP_ENC_KEY_TYPE_INVALID;
    }
}

dap_kem_alg_t dap_kem_alg_from_str(const char *a_str)
{
    if (!a_str)
        return DAP_KEM_ALG_COUNT;
    for (unsigned i = 0; i < DAP_KEM_ALG_COUNT; i++) {
        if (s_vtable[i].name && !strcmp(s_vtable[i].name, a_str))
            return (dap_kem_alg_t)i;
    }
    return DAP_KEM_ALG_COUNT;
}

const char *dap_kem_alg_to_str(dap_kem_alg_t a_alg)
{
    return dap_kem_alg_name(a_alg);
}

/* ===== Stateless operations ===== */

int dap_kem_keypair(dap_kem_alg_t a_alg, uint8_t *a_pk, uint8_t *a_sk)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    return v && v->keypair ? v->keypair(a_pk, a_sk) : -1;
}

int dap_kem_encaps(dap_kem_alg_t a_alg, uint8_t *a_ct, uint8_t *a_ss,
                   const uint8_t *a_pk)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    return v && v->encaps ? v->encaps(a_ct, a_ss, a_pk) : -1;
}

int dap_kem_decaps(dap_kem_alg_t a_alg, uint8_t *a_ss, const uint8_t *a_ct,
                   const uint8_t *a_sk)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    return v && v->decaps ? v->decaps(a_ss, a_ct, a_sk) : -1;
}

/* ===== Persistent context (ML-KEM only) ===== */

int dap_kem_ctx_create(dap_kem_ctx_t *a_ctx, dap_kem_alg_t a_alg,
                       const uint8_t *a_pk, const uint8_t *a_sk)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    if (!v || !v->ctx_init)
        return -1;
    void *state = calloc(1, v->ctx_state_size);
    if (!state)
        return -1;
    a_ctx->alg = a_alg;
    a_ctx->state = state;
    return v->ctx_init(state, a_pk, a_sk);
}

int dap_kem_ctx_create_enc(dap_kem_ctx_t *a_ctx, dap_kem_alg_t a_alg,
                           const uint8_t *a_pk)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    if (!v || !v->ctx_init_enc)
        return -1;
    void *state = calloc(1, v->ctx_state_size);
    if (!state)
        return -1;
    a_ctx->alg = a_alg;
    a_ctx->state = state;
    return v->ctx_init_enc(state, a_pk);
}

int dap_kem_ctx_encaps(uint8_t *a_ct, uint8_t *a_ss,
                       const dap_kem_ctx_t *a_ctx)
{
    const s_kem_vtable_t *v = s_get(a_ctx->alg);
    return v && v->ctx_encaps ? v->ctx_encaps(a_ct, a_ss, a_ctx->state) : -1;
}

int dap_kem_ctx_decaps(uint8_t *a_ss, const uint8_t *a_ct,
                       const dap_kem_ctx_t *a_ctx)
{
    const s_kem_vtable_t *v = s_get(a_ctx->alg);
    return v && v->ctx_decaps ? v->ctx_decaps(a_ss, a_ct, a_ctx->state) : -1;
}

void dap_kem_ctx_destroy(dap_kem_ctx_t *a_ctx)
{
    if (a_ctx && a_ctx->state) {
        const s_kem_vtable_t *v = s_get(a_ctx->alg);
        if (v)
            memset(a_ctx->state, 0, v->ctx_state_size);
        free(a_ctx->state);
        a_ctx->state = NULL;
    }
}
