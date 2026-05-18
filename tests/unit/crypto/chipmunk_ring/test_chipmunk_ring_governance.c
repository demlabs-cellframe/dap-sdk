/*
 * test_chipmunk_ring_governance.c — CR-9.6 acceptance tests
 *
 * Exercises the Cellframe governance integration surface
 * (dap_enc_chipmunk_ring_governance.h).  See
 * SLC `documentation_831b3c2fd035cada` (CR-9.6 design) §5.
 */

#include <dap_common.h>
#include <dap_test.h>
#include <dap_rand.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>   /* strlen */

#include "dap_enc_chipmunk_ring_governance.h"
#include "dap_enc_chipmunk_ring.h"
#include "dap_enc_chipmunk_ring_params.h"
#include "dap_enc_key.h"
#include "dap_chipmunk_ring_threshold.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_hypertree.h"
#include "chipmunk/chipmunk_ring.h"

#define LOG_TAG "test_chipmunk_ring_gov"

static struct dap_enc_key *s_member_key_from_seed(uint8_t a_byte)
{
    uint8_t l_seed[32];
    memset(l_seed, a_byte, sizeof(l_seed));
    return dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING,
                                    NULL, 0, l_seed, sizeof(l_seed), 0);
}

static bool s_test_governance_deal_combine_roundtrip(void)
{
    uint8_t l_master[32];
    for (int i = 0; i < 32; ++i) l_master[i] = (uint8_t)(0xA0 + i);

    chipmunk_ring_threshold_share_t l_shares[5];
    dap_assert(dap_enc_chipmunk_ring_governance_deal(l_master, 5, 3, l_shares) == 0,
               "governance_deal OK");

    struct dap_enc_key *l_key = NULL;
    dap_assert(dap_enc_chipmunk_ring_governance_combine_to_key(l_shares, 3, &l_key) == 0,
               "governance_combine_to_key OK");
    dap_assert(l_key != NULL, "combined key allocated");
    dap_assert(l_key->pub_key_data_size == CHIPMUNK_RING_PUBLIC_KEY_SIZE,
               "combined pub size");
    dap_assert(l_key->priv_key_data_size == CHIPMUNK_RING_PRIVATE_KEY_SIZE,
               "combined priv size");

    dap_enc_key_delete(l_key);
    return true;
}

static bool s_test_governance_combine_signs(void)
{
    uint8_t l_master[32];
    dap_random_bytes(l_master, sizeof(l_master));

    chipmunk_ring_threshold_share_t l_shares[3];
    dap_assert(dap_enc_chipmunk_ring_governance_deal(l_master, 3, 2, l_shares) == 0,
               "deal 2-of-3");

    struct dap_enc_key *l_signer = NULL;
    dap_assert(dap_enc_chipmunk_ring_governance_combine_to_key(l_shares, 2, &l_signer) == 0,
               "combine 2 shares");

    /* Prove the combined dap_enc_key is a valid hypertree signing key
     * (Cellframe uses it for governance votes before ring-layer wrap). */
    chipmunk_ht_private_key_t l_sk;
    chipmunk_ht_public_key_t  l_pk;
    memset(&l_sk, 0, sizeof(l_sk));
    memset(&l_pk, 0, sizeof(l_pk));
    dap_assert(chipmunk_ht_private_key_from_bytes(&l_sk, l_signer->priv_key_data) == 0,
               "priv deser");
    dap_assert(chipmunk_ht_public_key_from_bytes(&l_pk, l_signer->pub_key_data) == 0,
               "pub deser");

    const char *l_msg = "CR-9.6 governance combine sign";
    chipmunk_ht_signature_t l_sig;
    memset(&l_sig, 0, sizeof(l_sig));
    dap_assert(chipmunk_ht_sign(&l_sk, (const uint8_t *)l_msg, strlen(l_msg), &l_sig) == 0,
               "ht_sign after governance combine");
    dap_assert(chipmunk_ht_verify(&l_pk, (const uint8_t *)l_msg, strlen(l_msg), &l_sig) == 0,
               "ht_verify after governance combine");

    chipmunk_ht_signature_clear(&l_sig);
    chipmunk_ht_private_key_clear(&l_sk);
    dap_enc_key_delete(l_signer);
    return true;
}

static bool s_test_member_pop_dap_enc_roundtrip(void)
{
    struct dap_enc_key *l_key = s_member_key_from_seed(0x55);
    dap_assert(l_key != NULL, "member key");

    const size_t l_pop_sz = dap_enc_chipmunk_ring_pop_wire_size();
    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, l_pop_sz);
    dap_assert(l_pop != NULL, "pop alloc");

    dap_assert(dap_enc_chipmunk_ring_member_pop_create(l_key, l_pop, l_pop_sz) == 0,
               "member_pop_create OK");
    dap_assert(dap_enc_chipmunk_ring_member_pop_verify(l_key, l_pop, l_pop_sz) == 0,
               "member_pop_verify OK");

    DAP_DELETE(l_pop);
    dap_enc_key_delete(l_key);
    return true;
}

static bool s_test_container_with_pop_accepts_valid(void)
{
    const size_t l_n = 3;
    struct dap_enc_key *l_keys[3];
    const uint8_t *l_pops[3];
    const size_t l_pop_sz = dap_enc_chipmunk_ring_pop_wire_size();

    for (size_t i = 0; i < l_n; ++i) {
        l_keys[i] = s_member_key_from_seed((uint8_t)(0x10 + i));
        dap_assert(l_keys[i] != NULL, "member key alloc");
        uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, l_pop_sz);
        dap_assert(dap_enc_chipmunk_ring_member_pop_create(l_keys[i], l_pop, l_pop_sz) == 0,
                   "pop for member");
        l_pops[i] = l_pop;
    }

    chipmunk_ring_container_t l_ring;
    memset(&l_ring, 0, sizeof(l_ring));
    dap_assert(dap_enc_chipmunk_ring_container_create_with_pop(
                   l_keys, l_pops, l_n, &l_ring) == 0,
               "container_create_with_pop accepts valid set");
    dap_assert(l_ring.size == l_n, "ring size");

    chipmunk_ring_container_free(&l_ring);
    for (size_t i = 0; i < l_n; ++i) {
        DAP_DELETE((void *)l_pops[i]);
        dap_enc_key_delete(l_keys[i]);
    }
    return true;
}

static bool s_test_container_with_pop_rejects_rogue(void)
{
    struct dap_enc_key *l_honest = s_member_key_from_seed(0xAA);
    struct dap_enc_key *l_rogue  = s_member_key_from_seed(0xBB);
    dap_assert(l_honest != NULL && l_rogue != NULL, "keys");

    const size_t l_pop_sz = dap_enc_chipmunk_ring_pop_wire_size();
    uint8_t *l_pop_honest = DAP_NEW_Z_SIZE(uint8_t, l_pop_sz);
    dap_assert(dap_enc_chipmunk_ring_member_pop_create(l_honest, l_pop_honest, l_pop_sz) == 0,
               "honest pop");

    struct dap_enc_key *l_keys[2] = { l_rogue, l_honest };
    const uint8_t *l_pops[2] = { l_pop_honest, l_pop_honest };

    chipmunk_ring_container_t l_ring;
    memset(&l_ring, 0, sizeof(l_ring));
    int rc = dap_enc_chipmunk_ring_container_create_with_pop(
        l_keys, l_pops, 2, &l_ring);
    dap_assert(rc == CHIPMUNK_ERROR_VERIFY_FAILED,
               "rogue pk with honest pop rejected");

    DAP_DELETE(l_pop_honest);
    dap_enc_key_delete(l_honest);
    dap_enc_key_delete(l_rogue);
    return true;
}

int main(void)
{
    dap_set_appname("test_chipmunk_ring_governance");
    dap_common_init("test_chipmunk_ring_governance", NULL);
    dap_enc_chipmunk_ring_init();

    int l_rc = 0;
    if (!s_test_governance_deal_combine_roundtrip())  l_rc = 1;
    if (!s_test_governance_combine_signs())         l_rc = 1;
    if (!s_test_member_pop_dap_enc_roundtrip())     l_rc = 1;
    if (!s_test_container_with_pop_accepts_valid()) l_rc = 1;
    if (!s_test_container_with_pop_rejects_rogue()) l_rc = 1;

    if (l_rc == 0) {
        log_it(L_INFO, "ALL CR-9.6 governance tests PASSED");
    } else {
        log_it(L_ERROR, "Some CR-9.6 governance tests FAILED");
    }
    dap_common_deinit();
    return l_rc;
}
