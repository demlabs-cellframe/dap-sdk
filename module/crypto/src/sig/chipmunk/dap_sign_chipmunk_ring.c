/*
 * Chipmunk Ring DAP signature bridge.
 *
 * CR-11.G Phase 7.0: thin wrapper over chipmunk_ring wire API.
 * No magic dispatcher, no legacy CLTS path.
 *
 * Production wire: magic CRNG / R_CRNG k-of-N (chipmunk_ring_crng).
 */

#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_rand.h"
#include "dap_memwipe.h"
#include "dap_enc_key.h"
#include "dap_sign.h"
#include "dap_enc_chipmunk_ring.h"
#include "chipmunk_lrs.h"
#include "chipmunk_ring.h"

#define LOG_TAG "dap_sign_chipmunk_ring"

/* Outer seed redraw when CRNG returns NORM_BOUND (inner loop is 2048 attempts). */
#define RING_SEED_RETRIES 16u

static dap_sign_t *s_chipmunk_ring_create(
    dap_enc_key_t **a_signer_keys, size_t a_signers_count,
    uint32_t a_required_signers,
    const void *a_data, size_t a_data_size,
    dap_enc_key_t **a_ring_keys, size_t a_ring_size)
{
    if (!a_signer_keys || !a_ring_keys ||
        a_signers_count == 0u ||
        a_ring_size < CHIPMUNK_RING_RING_MIN ||
        a_ring_size > CHIPMUNK_RING_RING_MAX ||
        a_required_signers < CHIPMUNK_RING_THRESHOLD_MIN ||
        a_required_signers > a_ring_size ||
        (size_t)a_required_signers != a_signers_count) {
        log_it(L_ERROR, "chipmunk_ring sign: invalid parameters "
               "(signers=%zu, ring=%zu, threshold=%u)",
               a_signers_count, a_ring_size, a_required_signers);
        return NULL;
    }

    if (a_ring_size < CHIPMUNK_RING_RING_MINIMUM_ANON) {
        log_it(L_WARNING, "chipmunk_ring sign: ring size %zu below recommended "
               "anonymity minimum %u", a_ring_size,
               CHIPMUNK_RING_RING_MINIMUM_ANON);
    }

    for (size_t i = 0; i < a_signers_count; ++i) {
        if (!a_signer_keys[i] ||
            !a_signer_keys[i]->priv_key_data ||
            a_signer_keys[i]->priv_key_data_size != DAP_ENC_CHIPMUNK_RING_PRIV_KEY_SIZE) {
            log_it(L_ERROR, "chipmunk_ring sign: signer key[%zu] invalid", i);
            return NULL;
        }
    }
    for (size_t i = 0; i < a_ring_size; ++i) {
        if (!a_ring_keys[i] ||
            !a_ring_keys[i]->pub_key_data ||
            a_ring_keys[i]->pub_key_data_size != DAP_ENC_CHIPMUNK_RING_PUB_KEY_SIZE) {
            log_it(L_ERROR, "chipmunk_ring sign: ring key[%zu] invalid", i);
            return NULL;
        }
    }

    const uint32_t l_N = (uint32_t)a_ring_size;
    const uint32_t l_t = a_required_signers;

    const chipmunk_lrs_secret_key_t **l_sks =
        DAP_NEW_Z_COUNT(const chipmunk_lrs_secret_key_t *, l_t);
    chipmunk_lrs_public_key_t *l_ring =
        DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, l_N);
    if (!l_sks || !l_ring) {
        DAP_DELETE(l_sks);
        DAP_DELETE(l_ring);
        return NULL;
    }

    for (uint32_t k = 0; k < l_t; ++k) {
        l_sks[k] = (const chipmunk_lrs_secret_key_t *)
                   a_signer_keys[k]->priv_key_data;
    }
    for (uint32_t i = 0; i < l_N; ++i) {
        memcpy(&l_ring[i], a_ring_keys[i]->pub_key_data,
               sizeof(chipmunk_lrs_public_key_t));
    }

    const size_t l_seed_sz = (size_t)l_t * CHIPMUNK_LRS_SEED_BYTES;
    uint8_t *l_seeds = DAP_NEW_Z_SIZE(uint8_t, l_seed_sz);
    if (!l_seeds) {
        DAP_DELETE(l_sks);
        DAP_DELETE(l_ring);
        return NULL;
    }

    uint8_t *l_blob = NULL;
    size_t   l_blob_sz = 0;
    chipmunk_ring_error_t l_rc = CHIPMUNK_RING_ERR_NORM_BOUND;

    for (uint32_t l_attempt = 0;
         l_attempt < RING_SEED_RETRIES && l_rc != CHIPMUNK_RING_OK;
         ++l_attempt) {
        dap_random_bytes(l_seeds, l_seed_sz);
        l_rc = chipmunk_ring_sign_to_bytes(
            &l_blob, &l_blob_sz, l_sks, l_t,
            l_ring, l_N, l_t,
            (const uint8_t *)a_data, a_data_size,
            NULL, 0, l_seeds);
        if (l_rc == CHIPMUNK_RING_ERR_NORM_BOUND) {
            continue;
        }
        break;
    }

    dap_memwipe(l_seeds, l_seed_sz);
    DAP_DELETE(l_seeds);
    DAP_DELETE(l_sks);
    DAP_DELETE(l_ring);

    if (l_rc != CHIPMUNK_RING_OK || !l_blob) {
        log_it(L_ERROR, "chipmunk_ring sign failed (N=%u, t=%u, rc=%d %s)",
               l_N, l_t, (int)l_rc, chipmunk_ring_strerror(l_rc));
        return NULL;
    }

    dap_sign_t *l_sign = DAP_NEW_Z_SIZE(dap_sign_t,
                                        sizeof(dap_sign_hdr_t) + l_blob_sz);
    if (!l_sign) {
        dap_memwipe(l_blob, l_blob_sz);
        DAP_DELETE(l_blob);
        return NULL;
    }
    memcpy(l_sign->pkey_n_sign, l_blob, l_blob_sz);
    dap_memwipe(l_blob, l_blob_sz);
    DAP_DELETE(l_blob);

    l_sign->header.type.type      = SIG_TYPE_CHIPMUNK_MRING;
    l_sign->header.sign_pkey_size = 0u;
    l_sign->header.sign_size      = (uint32_t)l_blob_sz;
    return l_sign;
}

static int s_chipmunk_ring_verify(dap_sign_t *a_sign,
                                  const void *a_data, size_t a_data_size,
                                  dap_enc_key_t **a_ring_keys, size_t a_ring_size)
{
    if (!a_sign || !a_ring_keys ||
        a_ring_size < CHIPMUNK_RING_RING_MIN ||
        a_ring_size > CHIPMUNK_RING_RING_MAX) {
        return -EINVAL;
    }
    for (size_t i = 0; i < a_ring_size; ++i) {
        if (!a_ring_keys[i] ||
            !a_ring_keys[i]->pub_key_data ||
            a_ring_keys[i]->pub_key_data_size != DAP_ENC_CHIPMUNK_RING_PUB_KEY_SIZE) {
            log_it(L_ERROR, "chipmunk_ring verify: ring key[%zu] invalid", i);
            return -EINVAL;
        }
    }

    if (a_sign->header.sign_pkey_size != 0u) {
        return -EINVAL;
    }

    const size_t l_wire_sz = a_sign->header.sign_size;
    const uint8_t *l_wire = a_sign->pkey_n_sign;
    if (l_wire_sz < 4u) {
        return -EINVAL;
    }

    const uint32_t l_N = (uint32_t)a_ring_size;
    chipmunk_lrs_public_key_t *l_ring =
        DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, l_N);
    if (!l_ring) {
        return -ENOMEM;
    }
    for (uint32_t i = 0; i < l_N; ++i) {
        memcpy(&l_ring[i], a_ring_keys[i]->pub_key_data,
               sizeof(chipmunk_lrs_public_key_t));
    }

    chipmunk_ring_error_t l_e = chipmunk_ring_verify_from_bytes(
        l_wire, l_wire_sz, l_ring, l_N,
        (const uint8_t *)a_data, a_data_size, NULL, 0);

    DAP_DELETE(l_ring);

    if (l_e != CHIPMUNK_RING_OK) {
        log_it(L_INFO, "chipmunk_ring verify failed: %s",
               chipmunk_ring_strerror(l_e));
        return -EINVAL;
    }
    return 0;
}

int dap_sign_chipmunk_ring_register_callbacks(void)
{
    /* Register MRNG (log-N threshold ring) under both old and new type. */
    dap_sign_type_t l_mring_type = { .type = SIG_TYPE_CHIPMUNK_MRING };
    int rc = dap_sign_register_ring_callbacks(l_mring_type,
                                              s_chipmunk_ring_create,
                                              s_chipmunk_ring_verify);
    if (rc != 0) {
        return rc;
    }

    /* Register LRS (1-of-N linkable ring) under separate type. */
    dap_sign_type_t l_lrs_type = { .type = SIG_TYPE_CHIPMUNK_LRS };
    return dap_sign_register_ring_callbacks(l_lrs_type,
                                            s_chipmunk_ring_create,
                                            s_chipmunk_ring_verify);
}
