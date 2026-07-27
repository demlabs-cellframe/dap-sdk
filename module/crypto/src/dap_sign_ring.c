/*
 * dap_sign_ring.c — Unified ring signature API.
 *
 * Dispatches to MRNG, LRS, or LoTRS based on algorithm type.
 * Non-interactive schemes use dap_sign_ring_create().
 * Interactive schemes use dap_sign_ring_session_*().
 */

#include "dap_sign_ring.h"
#include "dap_sign.h"
#include "dap_memwipe.h"
#include "chipmunk_lrs.h"
#include "chipmunk_mring.h"

#include <errno.h>
#include <string.h>

#define LOG_TAG "dap_sign_ring"
#include "dap_common.h"

/* Chipmunk LRS key sizes. */
#define RING_PUB_KEY_SIZE   1424u
#define RING_PRIV_KEY_SIZE  1456u
#define RING_SEED_BYTES     32u

/* --- Key generation --- */

int dap_sign_ring_keygen(dap_sign_ring_alg_t a_alg,
                         uint8_t **a_pk, size_t *a_pk_len,
                         uint8_t **a_sk, size_t *a_sk_len)
{
    if (!a_pk || !a_pk_len || !a_sk || !a_sk_len) return -EINVAL;

    switch (a_alg) {
    case DAP_SIGN_RING_MRNG:
    case DAP_SIGN_RING_LRS:
        *a_pk_len = RING_PUB_KEY_SIZE;
        *a_sk_len = RING_PRIV_KEY_SIZE;
        *a_pk = DAP_NEW_Z_SIZE(uint8_t, *a_pk_len);
        *a_sk = DAP_NEW_Z_SIZE(uint8_t, *a_sk_len);
        if (!*a_pk || !*a_sk) {
            DAP_DELETE(*a_pk); DAP_DELETE(*a_sk);
            *a_pk = NULL; *a_sk = NULL;
            return -ENOMEM;
        }
        return 0;
    case DAP_SIGN_RING_LOTRS:
        return -ENOSYS;
    default:
        return -EINVAL;
    }
}

/* --- Non-interactive signing --- */

int dap_sign_ring_create(dap_sign_t **a_out,
                         const dap_sign_ring_params_t *a_params,
                         const uint8_t **a_sks, const size_t *a_sks_lens,
                         const uint8_t **a_ring, const size_t *a_ring_lens,
                         const uint8_t *a_msg, size_t a_msg_len,
                         const void *a_ctx, size_t a_ctx_len,
                         const uint8_t *a_seed)
{
    if (!a_out || !a_params || !a_sks || !a_ring || !a_msg || !a_seed)
        return -EINVAL;

    /* Interactive schemes must use session API. */
    if (a_params->interactive) {
        log_it(L_ERROR, "dap_sign_ring: interactive schemes require session API");
        return -EINVAL;
    }

    const uint32_t l_N = a_params->ring_size;
    const uint32_t l_t = a_params->threshold;

    if (l_N < 2u || l_N > 256u) return -EINVAL;
    if (l_t < 1u || l_t > l_N) return -EINVAL;

    /* LRS is non-threshold: reject t > 1. */
    if (a_params->alg == DAP_SIGN_RING_LRS && l_t > 1u) {
        log_it(L_ERROR, "dap_sign_ring: LRS does not support threshold > 1");
        return -EINVAL;
    }

    *a_out = NULL;

    switch (a_params->alg) {
    case DAP_SIGN_RING_MRNG: {
        const chipmunk_lrs_secret_key_t **l_sks =
            DAP_NEW_Z_COUNT(const chipmunk_lrs_secret_key_t *, l_t);
        chipmunk_lrs_public_key_t *l_ring =
            DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, l_N);
        if (!l_sks || !l_ring) {
            DAP_DELETE(l_sks); DAP_DELETE(l_ring);
            return -ENOMEM;
        }
        for (uint32_t i = 0u; i < l_t; ++i)
            l_sks[i] = (const chipmunk_lrs_secret_key_t *)a_sks[i];
        for (uint32_t i = 0u; i < l_N; ++i)
            memcpy(&l_ring[i], a_ring[i], sizeof(chipmunk_lrs_public_key_t));

        uint8_t *l_sig = NULL;
        size_t l_sig_sz = 0u;
        chipmunk_ring_error_t l_rc = chipmunk_ring_sign_to_bytes(
            &l_sig, &l_sig_sz, l_sks, l_t,
            l_ring, l_N, l_t,
            a_msg, a_msg_len, a_ctx, a_ctx_len, a_seed);

        DAP_DELETE(l_sks);
        DAP_DELETE(l_ring);

        if (l_rc != CHIPMUNK_RING_OK) return -EINVAL;

        *a_out = DAP_NEW_Z_SIZE(dap_sign_t,
                                sizeof(dap_sign_hdr_t) + l_sig_sz);
        if (!*a_out) {
            dap_memwipe(l_sig, l_sig_sz);
            DAP_DELETE(l_sig);
            return -ENOMEM;
        }
        memcpy((*a_out)->pkey_n_sign, l_sig, l_sig_sz);
        dap_memwipe(l_sig, l_sig_sz);
        DAP_DELETE(l_sig);

        (*a_out)->header.type.type = SIG_TYPE_CHIPMUNK_MRING;
        (*a_out)->header.sign_pkey_size = 0u;
        (*a_out)->header.sign_size = (uint32_t)l_sig_sz;
        return 0;
    }
    case DAP_SIGN_RING_LRS: {
        const chipmunk_lrs_secret_key_t *l_sk =
            (const chipmunk_lrs_secret_key_t *)a_sks[0];
        chipmunk_lrs_public_key_t *l_ring =
            DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, l_N);
        if (!l_ring) return -ENOMEM;
        for (uint32_t i = 0u; i < l_N; ++i)
            memcpy(&l_ring[i], a_ring[i], sizeof(chipmunk_lrs_public_key_t));

        size_t l_sig_sz = chipmunk_lrs_signature_size(l_N);
        uint8_t *l_sig = DAP_NEW_Z_SIZE(uint8_t, l_sig_sz);
        if (!l_sig) { DAP_DELETE(l_ring); return -ENOMEM; }

        int l_rc = chipmunk_lrs_sign(l_sig, l_sig_sz, l_sk, l_ring, l_N,
                                     a_msg, a_msg_len, a_seed, (uint64_t)CHIPMUNK_Q);
        DAP_DELETE(l_ring);
        if (l_rc != 0) {
            dap_memwipe(l_sig, l_sig_sz);
            DAP_DELETE(l_sig);
            return -EINVAL;
        }

        *a_out = DAP_NEW_Z_SIZE(dap_sign_t,
                                sizeof(dap_sign_hdr_t) + l_sig_sz);
        if (!*a_out) {
            dap_memwipe(l_sig, l_sig_sz);
            DAP_DELETE(l_sig);
            return -ENOMEM;
        }
        memcpy((*a_out)->pkey_n_sign, l_sig, l_sig_sz);
        dap_memwipe(l_sig, l_sig_sz);
        DAP_DELETE(l_sig);

        (*a_out)->header.type.type = SIG_TYPE_CHIPMUNK_LRS;
        (*a_out)->header.sign_pkey_size = 0u;
        (*a_out)->header.sign_size = (uint32_t)l_sig_sz;
        return 0;
    }
    case DAP_SIGN_RING_LOTRS:
        return -EINVAL; /* Must use session API. */
    default:
        return -EINVAL;
    }
}

/* --- Verification --- */

int dap_sign_ring_verify(const dap_sign_t *a_sign,
                         const uint8_t **a_ring, const size_t *a_ring_lens,
                         size_t a_ring_size,
                         const uint8_t *a_msg, size_t a_msg_len,
                         const void *a_ctx, size_t a_ctx_len)
{
    if (!a_sign || !a_ring || !a_msg) return -EINVAL;
    if (a_ring_size < 2u || a_ring_size > 256u) return -EINVAL;

    /* Dispatch based on signature type. */
    uint32_t l_type = a_sign->header.type.type;

    if (l_type == SIG_TYPE_CHIPMUNK_MRING) {
        chipmunk_lrs_public_key_t *l_ring =
            DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, a_ring_size);
        if (!l_ring) return -ENOMEM;
        for (size_t i = 0u; i < a_ring_size; ++i)
            memcpy(&l_ring[i], a_ring[i], sizeof(chipmunk_lrs_public_key_t));

        chipmunk_ring_error_t l_rc = chipmunk_ring_verify_from_bytes(
            a_sign->pkey_n_sign, a_sign->header.sign_size,
            l_ring, a_ring_size,
            a_msg, a_msg_len, a_ctx, a_ctx_len);
        DAP_DELETE(l_ring);
        return (l_rc == CHIPMUNK_RING_OK) ? 0 : -EINVAL;
    }

    if (l_type == SIG_TYPE_CHIPMUNK_LRS) {
        chipmunk_lrs_public_key_t *l_ring =
            DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, a_ring_size);
        if (!l_ring) return -ENOMEM;
        for (size_t i = 0u; i < a_ring_size; ++i)
            memcpy(&l_ring[i], a_ring[i], sizeof(chipmunk_lrs_public_key_t));

        int l_rc = chipmunk_lrs_verify(
            a_sign->pkey_n_sign, a_sign->header.sign_size,
            l_ring, a_ring_size,
            a_msg, a_msg_len, (uint64_t)CHIPMUNK_Q);
        DAP_DELETE(l_ring);
        return l_rc;
    }

    if (l_type == SIG_TYPE_LOTRS) {
        /* TODO(M9.2): LoTRS verify integration. */
        return -ENOSYS;
    }

    return -EINVAL;
}

/* --- Interactive session (LoTRS) — stubs --- */

struct dap_sign_ring_session {
    dap_sign_ring_params_t params;
    uint8_t *ring;
    uint8_t *msg;
    size_t msg_len;
};

int dap_sign_ring_session_create(dap_sign_ring_session_t **a_sess,
                                 const dap_sign_ring_params_t *a_params,
                                 const uint8_t **a_ring, const size_t *a_ring_lens,
                                 const uint8_t *a_msg, size_t a_msg_len)
{
    if (!a_sess || !a_params || !a_ring || !a_msg) return -EINVAL;
    if (!a_params->interactive) return -EINVAL;

    *a_sess = DAP_NEW_Z(dap_sign_ring_session_t);
    if (!*a_sess) return -ENOMEM;

    (*a_sess)->params = *a_params;
    (*a_sess)->msg = DAP_NEW_Z_SIZE(uint8_t, a_msg_len);
    if (!(*a_sess)->msg) {
        DAP_DELETE(*a_sess);
        *a_sess = NULL;
        return -ENOMEM;
    }
    memcpy((*a_sess)->msg, a_msg, a_msg_len);
    (*a_sess)->msg_len = a_msg_len;

    /* TODO(M9.2): store ring keys for LoTRS. */
    return 0;
}

int dap_sign_ring_session_round(dap_sign_ring_session_t *a_sess,
                                const uint8_t *a_sk, size_t a_sk_len,
                                uint32_t a_signer_idx,
                                const uint8_t *a_in, size_t a_in_len,
                                uint8_t **a_out, size_t *a_out_len)
{
    if (!a_sess || !a_sk || !a_out || !a_out_len) return -EINVAL;

    /* TODO(M9.2): implement LoTRS round protocol. */
    return -ENOSYS;
}

int dap_sign_ring_session_finish(dap_sign_ring_session_t *a_sess,
                                 dap_sign_t **a_out)
{
    if (!a_sess || !a_out) return -EINVAL;

    /* TODO(M9.2): implement LoTRS session finalization. */
    return -ENOSYS;
}

void dap_sign_ring_session_free(dap_sign_ring_session_t *a_sess)
{
    if (a_sess) {
        if (a_sess->msg) {
            dap_memwipe(a_sess->msg, a_sess->msg_len);
            DAP_DELETE(a_sess->msg);
        }
        DAP_DELETE(a_sess);
    }
}
