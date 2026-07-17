/*
 * dap_sign_chipmunk_lrs.c — dap_sign bridge for LRS (1-of-N linkable ring).
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
#include "chipmunk_mring.h"

#define LOG_TAG "dap_sign_chipmunk_lrs"

static dap_sign_t *s_chipmunk_lrs_create(
    dap_enc_key_t **a_signer_keys, size_t a_signers_count,
    uint32_t a_required_signers,
    const void *a_data, size_t a_data_size,
    dap_enc_key_t **a_ring_keys, size_t a_ring_size)
{
    (void)a_required_signers;

    if (!a_signer_keys || !a_ring_keys || a_signers_count != 1u
        || a_ring_size < CHIPMUNK_RING_RING_MIN
        || a_ring_size > CHIPMUNK_RING_RING_MAX) {
        return NULL;
    }

    chipmunk_lrs_public_key_t *l_ring =
        DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, a_ring_size);
    if (!l_ring) return NULL;
    for (size_t i = 0u; i < a_ring_size; ++i) {
        memcpy(&l_ring[i], a_ring_keys[i]->pub_key_data,
               sizeof(chipmunk_lrs_public_key_t));
    }

    chipmunk_lrs_secret_key_t *l_sk =
        (chipmunk_lrs_secret_key_t *)a_signer_keys[0]->priv_key_data;

    size_t l_sig_sz = chipmunk_lrs_signature_size(a_ring_size);
    uint8_t *l_sig = DAP_NEW_Z_SIZE(uint8_t, l_sig_sz);
    if (!l_sig) { DAP_DELETE(l_ring); return NULL; }

    uint8_t l_seed[CHIPMUNK_LRS_SEED_BYTES];
    dap_random_bytes(l_seed, sizeof(l_seed));

    int l_rc = chipmunk_lrs_sign(l_sig, l_sig_sz, l_sk, l_ring, a_ring_size,
                                 a_data, a_data_size, l_seed, (uint64_t)CHIPMUNK_Q);
    DAP_DELETE(l_ring);
    if (l_rc != 0) {
        dap_memwipe(l_sig, l_sig_sz);
        DAP_DELETE(l_sig);
        return NULL;
    }

    dap_sign_t *l_sign = DAP_NEW_Z_SIZE(dap_sign_t,
                                        sizeof(dap_sign_hdr_t) + l_sig_sz);
    if (!l_sign) {
        dap_memwipe(l_sig, l_sig_sz);
        DAP_DELETE(l_sig);
        return NULL;
    }
    memcpy(l_sign->pkey_n_sign, l_sig, l_sig_sz);
    dap_memwipe(l_sig, l_sig_sz);
    DAP_DELETE(l_sig);

    l_sign->header.type.type = SIG_TYPE_CHIPMUNK_LRS;
    l_sign->header.sign_pkey_size = 0u;
    l_sign->header.sign_size = (uint32_t)l_sig_sz;
    return l_sign;
}

static int s_chipmunk_lrs_verify(dap_sign_t *a_sign,
                                 const void *a_data, size_t a_data_size,
                                 dap_enc_key_t **a_ring_keys, size_t a_ring_size)
{
    if (!a_sign || !a_ring_keys || a_ring_size < CHIPMUNK_RING_RING_MIN
        || a_ring_size > CHIPMUNK_RING_RING_MAX) {
        return -EINVAL;
    }

    chipmunk_lrs_public_key_t *l_ring =
        DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, a_ring_size);
    if (!l_ring) return -ENOMEM;
    for (size_t i = 0u; i < a_ring_size; ++i) {
        memcpy(&l_ring[i], a_ring_keys[i]->pub_key_data,
               sizeof(chipmunk_lrs_public_key_t));
    }

    int l_rc = chipmunk_lrs_verify(
        a_sign->pkey_n_sign, a_sign->header.sign_size,
        l_ring, a_ring_size, a_data, a_data_size, (uint64_t)CHIPMUNK_Q);
    DAP_DELETE(l_ring);
    return l_rc;
}

int dap_sign_chipmunk_lrs_register_callbacks(void)
{
    dap_sign_type_t l_type = { .type = SIG_TYPE_CHIPMUNK_LRS };
    return dap_sign_register_ring_callbacks(l_type,
                                            s_chipmunk_lrs_create,
                                            s_chipmunk_lrs_verify);
}
