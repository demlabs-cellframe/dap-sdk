/*
 * dap_sign_lotrs.c — dap_sign bridge for LoTRS threshold ring signatures.
 *
 * Registers SIG_TYPE_LOTRS callbacks with the dap_sign ring registry.
 */

#include <errno.h>
#include <string.h>

#include "dap_common.h"
#include "dap_sign.h"
#include "dap_memwipe.h"
#include "lotrs.h"
#include "lotrs_params.h"
#include "lotrs_wire.h"
#include "lotrs_ring.h"
#include "lotrs_sample.h"

#define LOG_TAG "dap_sign_lotrs"

/* Default parameter set for now. */
static const lotrs_params_t *s_lotrs_default_params = &LOTRS_PARAMS_TEST;

static dap_sign_t *s_lotrs_create(
    dap_enc_key_t **a_signer_keys, size_t a_signers_count,
    uint32_t a_required_signers,
    const void *a_data, size_t a_data_size,
    dap_enc_key_t **a_ring_keys, size_t a_ring_size)
{
    if (!a_signer_keys || !a_ring_keys || a_signers_count == 0u
        || a_ring_size < 2u || a_ring_size > 256u
        || a_required_signers < 1u || a_required_signers > a_ring_size
        || (size_t)a_required_signers != a_signers_count) {
        log_it(L_ERROR, "lotrs sign: invalid params (signers=%zu, ring=%zu, t=%u)",
               a_signers_count, a_ring_size, a_required_signers);
        return NULL;
    }

    const lotrs_params_t *l_par = s_lotrs_default_params;

    /* Build ring PK table. */
    lotrs_ring_pk_t l_ring = {0};
    l_ring.N = (uint32_t)a_ring_size;
    l_ring.T = a_required_signers;
    l_ring.pks = DAP_NEW_Z_COUNT(lotrs_pk_t, l_ring.N * l_ring.T);
    if (!l_ring.pks) return NULL;

    /* For now: replicate each signer's PK across all T slots.
     * Full threshold support (M9.4) will use distinct PKs per slot. */
    for (uint32_t i = 0u; i < l_ring.N; ++i) {
        if (!a_ring_keys[i] || !a_ring_keys[i]->pub_key_data) {
            lotrs_ring_pk_free(&l_ring);
            return NULL;
        }
        for (uint32_t t = 0u; t < l_ring.T; ++t) {
            memcpy(&l_ring.pks[i * l_ring.T + t].a_hat,
                   a_ring_keys[i]->pub_key_data,
                   sizeof(lotrs_polyvec_t)); /* placeholder */
        }
    }

    /* Sign with first signer (single-signer for M9.2). */
    lotrs_sk_t l_sk = {0};
    if (!a_signer_keys[0] || !a_signer_keys[0]->priv_key_data) {
        lotrs_ring_pk_free(&l_ring);
        return NULL;
    }

    /* For now, use a fixed seed. Full integration will use CSPRNG. */
    uint8_t l_seed[32];
    memset(l_seed, 0x42, sizeof(l_seed));

    lotrs_signature_t l_sig = {0};
    int l_rc = lotrs_sign(&l_sig, l_par, &l_ring, &l_sk, 0u,
                          (const uint8_t *)a_data, a_data_size, l_seed);
    lotrs_ring_pk_free(&l_ring);

    if (l_rc != 0 || !l_sig.data || l_sig.len == 0u) {
        log_it(L_ERROR, "lotrs sign failed (rc=%d)", l_rc);
        return NULL;
    }

    /* Build dap_sign_t. */
    dap_sign_t *l_sign = DAP_NEW_Z_SIZE(dap_sign_t,
                                        sizeof(dap_sign_hdr_t) + l_sig.len);
    if (!l_sign) {
        dap_memwipe(l_sig.data, l_sig.len);
        DAP_DELETE(l_sig.data);
        return NULL;
    }
    memcpy(l_sign->pkey_n_sign, l_sig.data, l_sig.len);
    dap_memwipe(l_sig.data, l_sig.len);
    DAP_DELETE(l_sig.data);

    l_sign->header.type.type = SIG_TYPE_LOTRS;
    l_sign->header.sign_pkey_size = 0u;
    l_sign->header.sign_size = (uint32_t)l_sig.len;
    return l_sign;
}

static int s_lotrs_verify(dap_sign_t *a_sign,
                          const void *a_data, size_t a_data_size,
                          dap_enc_key_t **a_ring_keys, size_t a_ring_size)
{
    if (!a_sign || !a_ring_keys || a_ring_size < 2u || a_ring_size > 256u) {
        return -EINVAL;
    }

    const lotrs_params_t *l_par = s_lotrs_default_params;

    lotrs_ring_pk_t l_ring = {0};
    l_ring.N = (uint32_t)a_ring_size;
    l_ring.T = 1u; /* single-signer for now */
    l_ring.pks = DAP_NEW_Z_COUNT(lotrs_pk_t, l_ring.N);
    if (!l_ring.pks) return -ENOMEM;

    for (uint32_t i = 0u; i < l_ring.N; ++i) {
        if (!a_ring_keys[i] || !a_ring_keys[i]->pub_key_data) {
            lotrs_ring_pk_free(&l_ring);
            return -EINVAL;
        }
        memcpy(&l_ring.pks[i].a_hat, a_ring_keys[i]->pub_key_data,
               sizeof(lotrs_polyvec_t));
    }

    /* Wrap signature bytes. */
    lotrs_signature_t l_sig = { .data = a_sign->pkey_n_sign, .len = a_sign->header.sign_size };

    int l_rc = lotrs_verify(&l_sig, l_par, &l_ring,
                            (const uint8_t *)a_data, a_data_size);
    lotrs_ring_pk_free(&l_ring);

    return l_rc;
}

int dap_sign_lotrs_register_callbacks(void)
{
    dap_sign_type_t l_type = { .type = SIG_TYPE_LOTRS };
    return dap_sign_register_ring_callbacks(l_type, s_lotrs_create, s_lotrs_verify);
}
