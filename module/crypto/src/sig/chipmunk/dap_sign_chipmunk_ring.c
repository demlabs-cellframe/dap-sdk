/*
 * dap_sign_chipmunk_ring.c — dap_sign bridge for non-interactive lattice ring.
 *
 * Registers SIG_TYPE_CHIPMUNK_RING callbacks.
 * MRNG and LRS have their own bridge files.
 */

#include <errno.h>
#include <string.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_rand.h"
#include "dap_memwipe.h"
#include "dap_enc_key.h"
#include "dap_sign.h"
#include "chipmunk_ring.h"
#include "lotrs_params.h"

#define LOG_TAG "dap_sign_chipmunk_ring"

static dap_sign_t *s_chipmunk_ring_create(
    dap_enc_key_t **a_signer_keys, size_t a_signers_count,
    uint32_t a_required_signers,
    const void *a_data, size_t a_data_size,
    dap_enc_key_t **a_ring_keys, size_t a_ring_size)
{
    (void)a_required_signers;

    if (!a_signer_keys || !a_ring_keys || a_signers_count != 1u
        || a_ring_size < 2u || a_ring_size > 256u) {
        return NULL;
    }

    const lotrs_params_t *l_par = &LOTRS_PARAMS_RING_OPT;
    const uint32_t l_N = (uint32_t)a_ring_size;

    chipmunk_ring_table_t l_ring = {0};
    l_ring.N = l_N;
    l_ring.pks = DAP_NEW_Z_COUNT(chipmunk_ring_pk_t, l_N);
    if (!l_ring.pks) return NULL;

    for (uint32_t i = 0u; i < l_N; ++i) {
        if (!a_ring_keys[i] || !a_ring_keys[i]->pub_key_data) {
            chipmunk_ring_table_free(&l_ring);
            return NULL;
        }
        l_ring.pks[i].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
        if (!l_ring.pks[i].a_hat.polys) {
            chipmunk_ring_table_free(&l_ring);
            return NULL;
        }
        lotrs_polyvec_unpack(&l_ring.pks[i].a_hat,
                             a_ring_keys[i]->pub_key_data,
                             a_ring_keys[i]->pub_key_data_size, l_par);
    }

    if (!a_signer_keys[0] || !a_signer_keys[0]->priv_key_data) {
        chipmunk_ring_table_free(&l_ring);
        return NULL;
    }
    chipmunk_ring_sk_t l_sk = {0};
    l_sk.s = lotrs_polyvec_alloc(l_par, l_par->l + l_par->k);
    if (!l_sk.s.polys) {
        chipmunk_ring_table_free(&l_ring);
        return NULL;
    }
    lotrs_polyvec_unpack(&l_sk.s, a_signer_keys[0]->priv_key_data,
                         a_signer_keys[0]->priv_key_data_size, l_par);

    /* Find signer index in ring by matching public key. */
    uint32_t l_signer_idx = 0u;
    for (uint32_t i = 0u; i < l_N; ++i) {
        if (a_ring_keys[i] == a_signer_keys[0]) {
            l_signer_idx = i;
            break;
        }
    }

    chipmunk_ring_sig_t l_sig = {0};
    uint8_t l_seed[32];
    dap_random_bytes(l_seed, sizeof(l_seed));

    int l_rc = chipmunk_ring_sign(&l_sig, l_par, &l_ring, &l_sk, l_signer_idx,
                                  (const uint8_t *)a_data, a_data_size,
                                  l_seed);
    lotrs_polyvec_free(&l_sk.s);
    chipmunk_ring_table_free(&l_ring);

    if (l_rc != 0 || !l_sig.data) {
        log_it(L_ERROR, "chipmunk_ring sign failed (rc=%d)", l_rc);
        return NULL;
    }

    size_t l_sig_len = l_sig.len;  /* save before free */
    dap_sign_t *l_sign = DAP_NEW_Z_SIZE(dap_sign_t,
                                        sizeof(dap_sign_hdr_t) + l_sig_len);
    if (!l_sign) {
        chipmunk_ring_sig_free(&l_sig);
        return NULL;
    }
    memcpy(l_sign->pkey_n_sign, l_sig.data, l_sig_len);
    chipmunk_ring_sig_free(&l_sig);

    l_sign->header.type.type = SIG_TYPE_CHIPMUNK_RING;
    l_sign->header.sign_pkey_size = 0u;
    l_sign->header.sign_size = (uint32_t)l_sig_len;
    return l_sign;
}

static int s_chipmunk_ring_verify(dap_sign_t *a_sign,
                                  const void *a_data, size_t a_data_size,
                                  dap_enc_key_t **a_ring_keys, size_t a_ring_size)
{
    if (!a_sign || !a_ring_keys || a_ring_size < 2u || a_ring_size > 256u) {
        return -EINVAL;
    }

    const lotrs_params_t *l_par = &LOTRS_PARAMS_RING_OPT;
    const uint32_t l_N = (uint32_t)a_ring_size;

    chipmunk_ring_table_t l_ring = {0};
    l_ring.N = l_N;
    l_ring.pks = DAP_NEW_Z_COUNT(chipmunk_ring_pk_t, l_N);
    if (!l_ring.pks) return -ENOMEM;

    for (uint32_t i = 0u; i < l_N; ++i) {
        if (!a_ring_keys[i] || !a_ring_keys[i]->pub_key_data) {
            chipmunk_ring_table_free(&l_ring);
            return -EINVAL;
        }
        l_ring.pks[i].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
        if (!l_ring.pks[i].a_hat.polys) {
            chipmunk_ring_table_free(&l_ring);
            return -ENOMEM;
        }
        lotrs_polyvec_unpack(&l_ring.pks[i].a_hat,
                             a_ring_keys[i]->pub_key_data,
                             a_ring_keys[i]->pub_key_data_size, l_par);
    }

    chipmunk_ring_sig_t l_sig = {
        .data = a_sign->pkey_n_sign,
        .len = a_sign->header.sign_size
    };

    int l_rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring,
                                    (const uint8_t *)a_data, a_data_size);
    chipmunk_ring_table_free(&l_ring);
    return l_rc;
}

int dap_sign_chipmunk_ring_register_callbacks(void)
{
    dap_sign_type_t l_type = { .type = SIG_TYPE_CHIPMUNK_RING };
    return dap_sign_register_ring_callbacks(l_type,
                                            s_chipmunk_ring_create,
                                            s_chipmunk_ring_verify);
}
