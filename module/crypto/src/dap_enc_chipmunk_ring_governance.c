/*
 * dap_enc_chipmunk_ring_governance.c — CR-9.6 Cellframe governance wrappers.
 */

#include "dap_enc_chipmunk_ring_governance.h"

#include <errno.h>
#include <string.h>

#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_enc_chipmunk_ring.h"
#include "dap_enc_chipmunk_ring_params.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_hypertree.h"
#include "chipmunk/chipmunk_ring.h"

#define LOG_TAG "dap_enc_chipmunk_ring_gov"

static int s_check_ring_member_key(const struct dap_enc_key *a_key, bool a_need_priv)
{
    if (a_key == NULL) return -EINVAL;
    if (a_key->type != DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING) return -EINVAL;
    if (!a_key->pub_key_data
        || a_key->pub_key_data_size != CHIPMUNK_RING_PUBLIC_KEY_SIZE) {
        return -EINVAL;
    }
    if (a_need_priv) {
        if (!a_key->priv_key_data
            || a_key->priv_key_data_size != CHIPMUNK_RING_PRIVATE_KEY_SIZE) {
            return -EINVAL;
        }
    }
    return 0;
}

size_t dap_enc_chipmunk_ring_pop_wire_size(void)
{
    return CHIPMUNK_RING_POP_HEADER_BYTES + (size_t)CHIPMUNK_HT_SIGNATURE_SIZE;
}

int dap_enc_chipmunk_ring_governance_deal(const uint8_t a_master_seed[32],
                                          uint32_t a_n,
                                          uint32_t a_t,
                                          chipmunk_ring_threshold_share_t *a_out_shares)
{
    return chipmunk_ring_threshold_deal(a_master_seed, a_n, a_t, a_out_shares);
}

int dap_enc_chipmunk_ring_governance_combine_to_key(
    const chipmunk_ring_threshold_share_t *a_shares,
    uint32_t a_t,
    struct dap_enc_key **a_out_key)
{
    if (a_out_key == NULL) return -EINVAL;
    *a_out_key = NULL;

    if (a_shares == NULL || a_t < 2u) return -EINVAL;

    uint8_t l_master[CHIPMUNK_RING_THRESHOLD_MASTER_SEED_BYTES];
    memset(l_master, 0, sizeof(l_master));

    int l_rc = chipmunk_ring_threshold_combine(a_shares, a_t, l_master);
    if (l_rc != 0) {
        memset(l_master, 0, sizeof(l_master));
        return l_rc;
    }

    struct dap_enc_key *l_key = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING,
        NULL, 0,
        l_master, sizeof(l_master),
        0);
    memset(l_master, 0, sizeof(l_master));
    if (l_key == NULL) {
        return -ENOMEM;
    }

    *a_out_key = l_key;
    return 0;
}

int dap_enc_chipmunk_ring_member_pop_create(struct dap_enc_key *a_member_key,
                                          uint8_t *a_out_pop,
                                          size_t a_out_pop_size)
{
    int l_rc = s_check_ring_member_key(a_member_key, true);
    if (l_rc != 0) return l_rc;
    if (a_out_pop == NULL) return -EINVAL;

    const size_t l_pop_sz = dap_enc_chipmunk_ring_pop_wire_size();
    if (a_out_pop_size < l_pop_sz) return -EINVAL;

    chipmunk_ht_private_key_t l_sk;
    memset(&l_sk, 0, sizeof(l_sk));
    l_rc = chipmunk_ht_private_key_from_bytes(&l_sk,
                                              a_member_key->priv_key_data);
    if (l_rc != 0) return l_rc;

    l_rc = chipmunk_ring_pop_create(&l_sk, a_out_pop, l_pop_sz);

    chipmunk_ht_private_key_clear(&l_sk);
    return l_rc;
}

int dap_enc_chipmunk_ring_member_pop_verify(struct dap_enc_key *a_member_key,
                                            const uint8_t *a_pop,
                                            size_t a_pop_size)
{
    int l_rc = s_check_ring_member_key(a_member_key, false);
    if (l_rc != 0) return l_rc;
    if (a_pop == NULL) return -EINVAL;

    const size_t l_pop_sz = dap_enc_chipmunk_ring_pop_wire_size();
    if (a_pop_size < l_pop_sz) return -EINVAL;

    return chipmunk_ring_pop_verify_bytes(a_member_key->pub_key_data,
                                         a_member_key->pub_key_data_size,
                                         a_pop, a_pop_size);
}

int dap_enc_chipmunk_ring_container_create_with_pop(
    struct dap_enc_key **a_member_keys,
    const uint8_t *const *a_member_pops,
    size_t a_ring_size,
    chipmunk_ring_container_t *a_out_ring)
{
    if (a_member_keys == NULL || a_member_pops == NULL || a_out_ring == NULL) {
        return -EINVAL;
    }
    if (a_ring_size < 2u || a_ring_size > (size_t)CHIPMUNK_RING_MAX_RING_SIZE) {
        return -EINVAL;
    }

    const size_t l_pop_sz = dap_enc_chipmunk_ring_pop_wire_size();

    for (size_t i = 0; i < a_ring_size; ++i) {
        if (a_member_keys[i] == NULL || a_member_pops[i] == NULL) {
            return -EINVAL;
        }
        int l_rc = dap_enc_chipmunk_ring_member_pop_verify(a_member_keys[i],
                                                           a_member_pops[i],
                                                           l_pop_sz);
        if (l_rc != 0) {
            log_it(L_WARNING,
                   "CR-9.6: ring member %zu PoP verify failed (rc=%d)", i, l_rc);
            return l_rc;
        }
    }

    chipmunk_ring_public_key_t *l_pks =
        DAP_NEW_Z_COUNT(chipmunk_ring_public_key_t, a_ring_size);
    if (l_pks == NULL) return -ENOMEM;

    for (size_t i = 0; i < a_ring_size; ++i) {
        memcpy(l_pks[i].data, a_member_keys[i]->pub_key_data,
               CHIPMUNK_RING_PUBLIC_KEY_SIZE);
    }

    memset(a_out_ring, 0, sizeof(*a_out_ring));
    int l_rc = chipmunk_ring_container_create(l_pks, a_ring_size, a_out_ring);
    DAP_DELETE(l_pks);
    return l_rc;
}
