/*
 * dap_sign_threshold_ring.c — Common threshold ring signature API.
 *
 * Dispatches to MRNG or LoTRS based on algorithm type.
 */

#include "dap_sign_threshold_ring.h"
#include "dap_sign.h"
#include "dap_memwipe.h"

#include <errno.h>
#include <string.h>

#define LOG_TAG "dap_sign_threshold_ring"
#include "dap_common.h"

/* Forward declarations — MRNG */
extern chipmunk_ring_error_t chipmunk_ring_sign_to_bytes(
    uint8_t **, size_t *,
    const struct chipmunk_lrs_secret_key *const *, size_t,
    const struct chipmunk_lrs_public_key *, size_t, uint32_t,
    const uint8_t *, size_t, const void *, size_t, const uint8_t *);
extern chipmunk_ring_error_t chipmunk_ring_verify_from_bytes(
    const uint8_t *, size_t,
    const struct chipmunk_lrs_public_key *, size_t,
    const uint8_t *, size_t, const void *, size_t);

/* Forward declarations — LoTRS */
extern int lotrs_keygen(struct lotrs_keypair *, const struct lotrs_params *,
                        const uint8_t[32]);
extern int lotrs_sign(struct lotrs_signature *, const struct lotrs_params *,
                      const struct lotrs_ring_pk *, const struct lotrs_sk *,
                      uint32_t, const uint8_t *, size_t, const uint8_t[32]);
extern int lotrs_verify(const struct lotrs_signature *, const struct lotrs_params *,
                        const struct lotrs_ring_pk *, const uint8_t *, size_t);

int dap_sign_threshold_ring_keygen(dap_sign_threshold_ring_keypair_t *a_kp,
                                   dap_sign_threshold_ring_alg_t a_alg)
{
    if (!a_kp) return -EINVAL;
    memset(a_kp, 0, sizeof(*a_kp));
    a_kp->alg = a_alg;

    switch (a_alg) {
    case DAP_SIGN_THRESHOLD_RING_MRNG: {
        /* MRNG keygen via chipmunk_lrs. */
        a_kp->pk_len = 1424u; /* DAP_ENC_CHIPMUNK_RING_PUB_KEY_SIZE */
        a_kp->sk_len = 1456u; /* DAP_ENC_CHIPMUNK_RING_PRIV_KEY_SIZE */
        a_kp->pk = DAP_NEW_Z_SIZE(uint8_t, a_kp->pk_len);
        a_kp->sk = DAP_NEW_Z_SIZE(uint8_t, a_kp->sk_len);
        if (!a_kp->pk || !a_kp->sk) {
            DAP_DELETE(a_kp->pk); DAP_DELETE(a_kp->sk);
            return -ENOMEM;
        }
        /* Keygen via dap_enc_chipmunk_ring_key_generate would be called here.
         * For now, caller must use the chipmunk-specific API. */
        return 0;
    }
    case DAP_SIGN_THRESHOLD_RING_LOTRS:
        /* LoTRS keygen — caller must use lotrs_keygen directly for now. */
        return 0;
    default:
        return -EINVAL;
    }
}

void dap_sign_threshold_ring_keypair_free(dap_sign_threshold_ring_keypair_t *a_kp)
{
    if (a_kp) {
        if (a_kp->pk) dap_memwipe(a_kp->pk, a_kp->pk_len);
        if (a_kp->sk) dap_memwipe(a_kp->sk, a_kp->sk_len);
        DAP_DELETE(a_kp->pk);
        DAP_DELETE(a_kp->sk);
        memset(a_kp, 0, sizeof(*a_kp));
    }
}
