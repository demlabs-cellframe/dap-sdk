/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright  (c) 2025-2026
 * All rights reserved.
 *
 * Thin DAP-key adapter over the native Chipmunk Ring key material.
 * Stores chipmunk_lrs_secret_key_t / public_key_t byte-for-byte in
 * dap_enc_key_t buffers.  Ring sign/verify go through the generic
 * dap_sign_create_ring / dap_sign_verify_ring dispatcher because the
 * standard sign_get/sign_verify callbacks cannot carry ring and signer
 * subset context.
 */

#include <errno.h>
#include <string.h>

#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_enc_chipmunk_ring.h"
#include "dap_rand.h"
#include "dap_memwipe.h"
#include "sig/chipmunk/chipmunk_lrs.h"
#include "sig/chipmunk/chipmunk_hash.h"
#include "dap_hash_shake256.h"

#define LOG_TAG "dap_enc_chipmunk_ring"

_Static_assert(sizeof(chipmunk_lrs_public_key_t) == DAP_ENC_CHIPMUNK_RING_PUB_KEY_SIZE,
               "CLPK size pinned by adapter must match chipmunk_lrs");
_Static_assert(sizeof(chipmunk_lrs_secret_key_t) == DAP_ENC_CHIPMUNK_RING_PRIV_KEY_SIZE,
               "CLSK size pinned by adapter must match chipmunk_lrs");

int dap_enc_chipmunk_ring_init(void)
{
    int rc = dap_sign_chipmunk_mring_register_callbacks();
    if (rc != 0) return rc;
    rc = dap_sign_chipmunk_lrs_register_callbacks();
    if (rc != 0) return rc;
    return dap_sign_chipmunk_ring_register_callbacks();
}

static int s_expand_x_seed(uint8_t a_x_seed[CHIPMUNK_LRS_SEED_BYTES],
                           const void *a_material, size_t a_material_size)
{
    static const uint8_t k_domain[] = "chipmunk/lrs/keygen/v3";
    uint8_t l_concat[sizeof(k_domain) + 4096];
    if (a_material_size > sizeof(l_concat) - sizeof(k_domain)) {
        return -EINVAL;
    }
    size_t l_off = 0;
    memcpy(l_concat + l_off, k_domain, sizeof(k_domain));
    l_off += sizeof(k_domain);
    if (a_material_size > 0) {
        memcpy(l_concat + l_off, a_material, a_material_size);
        l_off += a_material_size;
    }

    dap_hash_shake256(a_x_seed, CHIPMUNK_LRS_SEED_BYTES, l_concat, l_off);
    dap_memwipe(l_concat, sizeof(l_concat));
    return 0;
}

static int s_derive_x_seed_from_anything(uint8_t a_x_seed[CHIPMUNK_LRS_SEED_BYTES],
                                         const void *a_kex_buf, size_t a_kex_size,
                                         const void *a_seed,    size_t a_seed_size,
                                         const void *a_personalisation, size_t a_personalisation_size)
{
    uint8_t l_buf[4096];
    size_t  l_off = 0;
    const struct { const void *p; size_t n; } l_chunks[] = {
        { a_seed,            a_seed_size            },
        { a_kex_buf,         a_kex_size             },
        { a_personalisation, a_personalisation_size },
    };
    for (size_t i = 0; i < sizeof(l_chunks) / sizeof(l_chunks[0]); ++i) {
        if (!l_chunks[i].p || l_chunks[i].n == 0) continue;
        if (l_chunks[i].n > sizeof(l_buf) - l_off) {
            dap_memwipe(l_buf, sizeof(l_buf));
            return -EINVAL;
        }
        memcpy(l_buf + l_off, l_chunks[i].p, l_chunks[i].n);
        l_off += l_chunks[i].n;
    }

    if (l_off == 0) {
        uint8_t l_rng[64];
        if (dap_random_bytes(l_rng, sizeof(l_rng)) != 0) {
            log_it(L_ERROR, "dap_random_bytes failed for CHIPMUNK_RING key seed");
            return -EIO;
        }
        int l_rc = s_expand_x_seed(a_x_seed, l_rng, sizeof(l_rng));
        dap_memwipe(l_rng, sizeof(l_rng));
        return l_rc;
    }

    int l_rc = s_expand_x_seed(a_x_seed, l_buf, l_off);
    dap_memwipe(l_buf, sizeof(l_buf));
    return l_rc;
}

dap_enc_key_t *dap_enc_chipmunk_ring_key_new(void)
{
    return dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING);
}

dap_enc_key_t *dap_enc_chipmunk_ring_key_generate(const void *a_kex_buf, size_t a_kex_size,
                                                  const void *a_seed,    size_t a_seed_size,
                                                  const void *a_personalisation, size_t a_personalisation_size)
{
    uint8_t l_x_seed[CHIPMUNK_LRS_SEED_BYTES];
    if (s_derive_x_seed_from_anything(l_x_seed,
                                      a_kex_buf, a_kex_size,
                                      a_seed, a_seed_size,
                                      a_personalisation, a_personalisation_size) != 0) {
        return NULL;
    }

    chipmunk_lrs_public_key_t *l_pk = DAP_NEW_Z(chipmunk_lrs_public_key_t);
    chipmunk_lrs_secret_key_t *l_sk = DAP_NEW_Z(chipmunk_lrs_secret_key_t);
    if (!l_pk || !l_sk) {
        DAP_DEL_Z(l_pk);
        DAP_DEL_Z(l_sk);
        dap_memwipe(l_x_seed, sizeof(l_x_seed));
        return NULL;
    }

    int l_rc = chipmunk_lrs_keypair_from_seeds(l_pk, l_sk, l_x_seed);
    dap_memwipe(l_x_seed, sizeof(l_x_seed));
    if (l_rc != 0) {
        DAP_DEL_Z(l_pk);
        dap_memwipe(l_sk, sizeof(*l_sk));
        DAP_DEL_Z(l_sk);
        return NULL;
    }

    dap_enc_key_t *l_key = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING);
    if (!l_key) {
        DAP_DEL_Z(l_pk);
        dap_memwipe(l_sk, sizeof(*l_sk));
        DAP_DEL_Z(l_sk);
        return NULL;
    }
    l_key->pub_key_data       = (uint8_t *)l_pk;
    l_key->pub_key_data_size  = sizeof(*l_pk);
    l_key->priv_key_data      = (uint8_t *)l_sk;
    l_key->priv_key_data_size = sizeof(*l_sk);
    return l_key;
}

void dap_enc_chipmunk_mring_key_new_callback(dap_enc_key_t *a_key)
{
    if (!a_key) return;
    a_key->type = DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_MRING;
    a_key->pub_key_data = NULL;
    a_key->pub_key_data_size = 0;
    a_key->priv_key_data = NULL;
    a_key->priv_key_data_size = 0;
}

void dap_enc_chipmunk_ring_key_new_callback(dap_enc_key_t *a_key)
{
    if (!a_key) return;
    a_key->type = DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING;
    a_key->pub_key_data = NULL;
    a_key->pub_key_data_size = 0;
    a_key->priv_key_data = NULL;
    a_key->priv_key_data_size = 0;
}

void dap_enc_chipmunk_ring_key_generate_callback(dap_enc_key_t *a_key,
                                                 const void *a_kex_buf, size_t a_kex_size,
                                                 const void *a_seed,    size_t a_seed_size,
                                                 size_t a_key_size)
{
    (void)a_key_size;
    if (!a_key) return;

    const dap_enc_key_type_t l_key_type = a_key->type;
    dap_enc_key_t *l_tmp = dap_enc_chipmunk_ring_key_generate(a_kex_buf, a_kex_size,
                                                              a_seed, a_seed_size,
                                                              NULL, 0);
    if (!l_tmp) {
        log_it(L_ERROR, "chipmunk_ring_key_generate_callback failed");
        return;
    }

    if (a_key->priv_key_data) {
        dap_memwipe(a_key->priv_key_data, a_key->priv_key_data_size);
        DAP_DELETE(a_key->priv_key_data);
    }
    if (a_key->pub_key_data) {
        DAP_DELETE(a_key->pub_key_data);
    }
    a_key->type               = l_key_type;
    a_key->pub_key_data       = l_tmp->pub_key_data;
    a_key->pub_key_data_size  = l_tmp->pub_key_data_size;
    a_key->priv_key_data      = l_tmp->priv_key_data;
    a_key->priv_key_data_size = l_tmp->priv_key_data_size;
    l_tmp->pub_key_data  = NULL;
    l_tmp->priv_key_data = NULL;
    DAP_DELETE(l_tmp);
}

void dap_enc_chipmunk_ring_key_delete(dap_enc_key_t *a_key)
{
    if (!a_key) return;
    if (a_key->priv_key_data) {
        dap_memwipe(a_key->priv_key_data, a_key->priv_key_data_size);
        DAP_DELETE(a_key->priv_key_data);
        a_key->priv_key_data = NULL;
        a_key->priv_key_data_size = 0;
    }
    if (a_key->pub_key_data) {
        DAP_DELETE(a_key->pub_key_data);
        a_key->pub_key_data = NULL;
        a_key->pub_key_data_size = 0;
    }
}

/* ----- public-key serialization ---------------------------------------- */

uint8_t *dap_enc_chipmunk_ring_write_public_key(const void *a_key, size_t *a_buflen_out)
{
    if (!a_key) return NULL;
    uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, sizeof(chipmunk_lrs_public_key_t));
    if (!l_buf) return NULL;
    memcpy(l_buf, a_key, sizeof(chipmunk_lrs_public_key_t));
    if (a_buflen_out) *a_buflen_out = sizeof(chipmunk_lrs_public_key_t);
    return l_buf;
}

void *dap_enc_chipmunk_ring_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
    if (!a_buf || a_buflen != sizeof(chipmunk_lrs_public_key_t)) return NULL;
    chipmunk_lrs_public_key_t *l_pk = DAP_NEW(chipmunk_lrs_public_key_t);
    if (!l_pk) return NULL;
    memcpy(l_pk, a_buf, sizeof(*l_pk));
    if (chipmunk_lrs_public_key_validate(l_pk) != 0) {
        DAP_DELETE(l_pk);
        return NULL;
    }
    return l_pk;
}

uint64_t dap_enc_chipmunk_ring_ser_public_key_size(const void *a_key)
{
    (void)a_key;
    return sizeof(chipmunk_lrs_public_key_t);
}

uint64_t dap_enc_chipmunk_ring_deser_public_key_size(const void *a_buf)
{
    (void)a_buf;
    return sizeof(chipmunk_lrs_public_key_t);
}

void dap_enc_chipmunk_ring_public_key_delete(void *a_pub_key)
{
    if (a_pub_key) DAP_DELETE(a_pub_key);
}

/* ----- private-key serialization --------------------------------------- */

uint8_t *dap_enc_chipmunk_ring_write_private_key(const void *a_key, size_t *a_buflen_out)
{
    if (!a_key) return NULL;
    uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, sizeof(chipmunk_lrs_secret_key_t));
    if (!l_buf) return NULL;
    memcpy(l_buf, a_key, sizeof(chipmunk_lrs_secret_key_t));
    if (a_buflen_out) *a_buflen_out = sizeof(chipmunk_lrs_secret_key_t);
    return l_buf;
}

void *dap_enc_chipmunk_ring_read_private_key(const uint8_t *a_buf, size_t a_buflen)
{
    if (!a_buf || a_buflen != sizeof(chipmunk_lrs_secret_key_t)) return NULL;
    chipmunk_lrs_secret_key_t *l_sk = DAP_NEW(chipmunk_lrs_secret_key_t);
    if (!l_sk) return NULL;
    memcpy(l_sk, a_buf, sizeof(*l_sk));
    if (chipmunk_lrs_secret_key_validate(l_sk) != 0) {
        dap_memwipe(l_sk, sizeof(*l_sk));
        DAP_DELETE(l_sk);
        return NULL;
    }
    return l_sk;
}

uint64_t dap_enc_chipmunk_ring_ser_private_key_size(const void *a_key)
{
    (void)a_key;
    return sizeof(chipmunk_lrs_secret_key_t);
}

uint64_t dap_enc_chipmunk_ring_deser_private_key_size(const void *a_buf)
{
    (void)a_buf;
    return sizeof(chipmunk_lrs_secret_key_t);
}

void dap_enc_chipmunk_ring_private_key_delete(void *a_priv_key)
{
    if (!a_priv_key) return;
    dap_memwipe(a_priv_key, sizeof(chipmunk_lrs_secret_key_t));
    DAP_DELETE(a_priv_key);
}
