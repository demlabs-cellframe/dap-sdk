/**
 * @file dap_kem.c
 * @brief Generic KEM dispatch — routes to algorithm-specific implementations.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdlib.h>
#include <string.h>
#include "dap_kem.h"

/* ML-KEM raw API (from per-K libraries) */
#define MLKEM_DECL(k, pk, sk, ct) \
    int dap_mlkem##k##_kem_keypair(uint8_t *, uint8_t *); \
    int dap_mlkem##k##_kem_enc(uint8_t *, uint8_t *, const uint8_t *); \
    int dap_mlkem##k##_kem_dec(uint8_t *, const uint8_t *, const uint8_t *); \
    int dap_mlkem##k##_ctx_init(void *, const uint8_t *, const uint8_t *); \
    int dap_mlkem##k##_ctx_init_enc(void *, const uint8_t *); \
    int dap_mlkem##k##_kem_enc_ctx(uint8_t *, uint8_t *, const void *); \
    int dap_mlkem##k##_kem_dec_ctx(uint8_t *, const uint8_t *, const void *);

MLKEM_DECL(512,  800, 1632, 768)
MLKEM_DECL(768, 1184, 2400, 1088)
MLKEM_DECL(1024, 1568, 3168, 1568)

typedef struct {
    const char *name;
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

/*
 * ctx_state_size: conservative upper bound for the opaque ctx struct.
 * polyvec = K * 256 * 2 bytes; mat_t = K * polyvec; ctx ≈ mat_t + 3*polyvec + mulcache + scalars.
 */
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
};

static inline const s_kem_vtable_t *s_get(dap_kem_alg_t a_alg)
{
    return (unsigned)a_alg < DAP_KEM_ALG_COUNT ? &s_vtable[a_alg] : NULL;
}

const char *dap_kem_alg_name(dap_kem_alg_t a_alg)
{
    const s_kem_vtable_t *v = s_get(a_alg);
    return v ? v->name : "unknown";
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
