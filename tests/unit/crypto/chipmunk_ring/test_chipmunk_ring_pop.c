/*
 * test_chipmunk_ring_pop.c — CR-9.5 / CR-11.E acceptance tests
 *
 * Locks in the Proof-of-Possession primitive
 * (dap_chipmunk_ring_threshold.h / pop_create / pop_verify /
 * pop_verify_bytes).  Every row of
 * SLC `documentation_c17bd6bdc72b4e00` (CR-9.5 design) §5 has a
 * dedicated check below.  CR-11.E intentionally removes v1 wire
 * compatibility because ChipmunkRing has not shipped in production.
 */

#include <dap_common.h>
#include <dap_test.h>
#include <dap_rand.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_chipmunk_ring_threshold.h"
#include "chipmunk/chipmunk.h"            /* CHIPMUNK_ERROR_VERIFY_FAILED */
#include "chipmunk/chipmunk_hypertree.h"  /* ht_keypair_from_seed/ht_sign/clear */

#define LOG_TAG "test_chipmunk_ring_pop"

/* PoP wire size for v1 — header + ht-sig. */
#define POP_BYTES                                                               \
    (CHIPMUNK_RING_POP_HEADER_BYTES + (size_t)CHIPMUNK_HT_SIGNATURE_SIZE)

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Build a freshly-materialised keypair from a deterministic seed. */
static void s_keypair_from_seed_byte(uint8_t a_seed_byte,
                                     chipmunk_ht_public_key_t  *a_pk,
                                     chipmunk_ht_private_key_t *a_sk)
{
    uint8_t l_seed[32];
    memset(l_seed, a_seed_byte, sizeof(l_seed));
    memset(a_pk, 0, sizeof(*a_pk));
    memset(a_sk, 0, sizeof(*a_sk));
    int rc = chipmunk_ht_keypair_from_seed(l_seed, a_pk, a_sk);
    dap_assert(rc == 0, "ht_keypair_from_seed OK");
}

/* ------------------------------------------------------------------ */
/* Tests (matching CR-9.5 + CR-11.E SLC contracts)                     */
/* ------------------------------------------------------------------ */

static bool s_test_pop_roundtrip_deterministic(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    s_keypair_from_seed_byte(0x42, &l_pk, &l_sk);

    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, POP_BYTES);
    dap_assert(l_pop != NULL, "PoP buf alloc");

    int rc = chipmunk_ring_pop_create(&l_sk, l_pop, POP_BYTES);
    dap_assert(rc == 0, "pop_create OK");

    rc = chipmunk_ring_pop_verify(&l_pk, l_pop, POP_BYTES);
    dap_assert(rc == 0, "pop_verify OK on matching pk");

    DAP_DELETE(l_pop);
    chipmunk_ht_private_key_clear(&l_sk);
    return true;
}

static bool s_test_pop_roundtrip_random(void)
{
    for (int iter = 0; iter < 3; ++iter) {
        uint8_t l_seed[32];
        dap_random_bytes(l_seed, sizeof(l_seed));

        chipmunk_ht_public_key_t  l_pk;
        chipmunk_ht_private_key_t l_sk;
        memset(&l_pk, 0, sizeof(l_pk));
        memset(&l_sk, 0, sizeof(l_sk));
        int rc = chipmunk_ht_keypair_from_seed(l_seed, &l_pk, &l_sk);
        dap_assert(rc == 0, "random keypair_from_seed OK");

        uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, POP_BYTES);
        rc = chipmunk_ring_pop_create(&l_sk, l_pop, POP_BYTES);
        dap_assert(rc == 0, "random pop_create OK");
        rc = chipmunk_ring_pop_verify(&l_pk, l_pop, POP_BYTES);
        dap_assert(rc == 0, "random pop_verify OK");

        DAP_DELETE(l_pop);
        chipmunk_ht_private_key_clear(&l_sk);
    }
    return true;
}

static bool s_test_pop_create_accepts_used_sk_and_preserves_counter(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    s_keypair_from_seed_byte(0x11, &l_pk, &l_sk);

    /* Burn leaf_index 0 with a production sig. */
    chipmunk_ht_signature_t l_dummy_sig;
    memset(&l_dummy_sig, 0, sizeof(l_dummy_sig));
    int rc = chipmunk_ht_sign(&l_sk, (const uint8_t *)"burn", 4, &l_dummy_sig);
    dap_assert(rc == 0, "ht_sign burns leaf 0");
    chipmunk_ht_signature_clear(&l_dummy_sig);
    dap_assert(l_sk.leaf_index == 1u, "leaf_index advanced to 1");

    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, POP_BYTES);
    rc = chipmunk_ring_pop_create(&l_sk, l_pop, POP_BYTES);
    dap_assert(rc == 0, "CR-11.E pop_create accepts already-used production sk");
    dap_assert(l_sk.leaf_index == 1u, "CR-11.E pop_create preserves production counter");
    dap_assert(chipmunk_ring_pop_verify(&l_pk, l_pop, POP_BYTES) == 0,
               "CR-11.E PoP verifies under matching pk");

    DAP_DELETE(l_pop);
    chipmunk_ht_private_key_clear(&l_sk);
    return true;
}

static bool s_test_pop_preserves_full_production_budget(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    s_keypair_from_seed_byte(0x12, &l_pk, &l_sk);

    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, POP_BYTES);
    int rc = chipmunk_ring_pop_create(&l_sk, l_pop, POP_BYTES);
    dap_assert(rc == 0, "PoP before production signing succeeds");
    dap_assert(l_sk.leaf_index == 0u, "PoP does not consume production leaf 0");

    for (uint32_t i = 0; i < CHIPMUNK_HT_MAX_SIGNATURES; ++i) {
        char l_msg[32];
        snprintf(l_msg, sizeof(l_msg), "budget-%u", (unsigned)i);
        chipmunk_ht_signature_t l_sig;
        memset(&l_sig, 0, sizeof(l_sig));
        rc = chipmunk_ht_sign(&l_sk, (const uint8_t *)l_msg, strlen(l_msg), &l_sig);
        dap_assert(rc == 0, "production sign succeeds after PoP");
        dap_assert(l_sig.leaf_index == i, "production leaf index remains monotonic");
        rc = chipmunk_ht_verify(&l_pk, (const uint8_t *)l_msg, strlen(l_msg), &l_sig);
        dap_assert(rc == 0, "production signature verifies after PoP");
        chipmunk_ht_signature_clear(&l_sig);
    }
    dap_assert(l_sk.leaf_index == CHIPMUNK_HT_MAX_SIGNATURES,
               "all production leaves remain available after PoP");

    DAP_DELETE(l_pop);
    chipmunk_ht_private_key_clear(&l_sk);
    return true;
}

static bool s_test_pop_create_rejects_null(void)
{
    uint8_t l_pop[64];
    int rc = chipmunk_ring_pop_create(NULL, l_pop, sizeof(l_pop));
    dap_assert(rc == -EINVAL, "pop_create(NULL sk) -> -EINVAL");

    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    s_keypair_from_seed_byte(0x22, &l_pk, &l_sk);

    rc = chipmunk_ring_pop_create(&l_sk, NULL, POP_BYTES);
    dap_assert(rc == -EINVAL, "pop_create(NULL out) -> -EINVAL");

    /* Too-small buffer also rejected. */
    rc = chipmunk_ring_pop_create(&l_sk, l_pop, 8);
    dap_assert(rc == -EINVAL, "pop_create(too-small out) -> -EINVAL");

    chipmunk_ht_private_key_clear(&l_sk);
    return true;
}

static bool s_test_pop_verify_rejects_wrong_pk(void)
{
    chipmunk_ht_public_key_t  l_pk_a, l_pk_b;
    chipmunk_ht_private_key_t l_sk_a, l_sk_b;
    s_keypair_from_seed_byte(0x33, &l_pk_a, &l_sk_a);
    s_keypair_from_seed_byte(0x44, &l_pk_b, &l_sk_b);

    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, POP_BYTES);
    int rc = chipmunk_ring_pop_create(&l_sk_a, l_pop, POP_BYTES);
    dap_assert(rc == 0, "pop_create under sk_A");

    /* Verify under the WRONG pk_B → signature mismatch. */
    rc = chipmunk_ring_pop_verify(&l_pk_b, l_pop, POP_BYTES);
    dap_assert(rc == CHIPMUNK_ERROR_VERIFY_FAILED,
               "pop_verify under wrong pk -> VERIFY_FAILED");

    /* And verify under the right pk_A still works. */
    rc = chipmunk_ring_pop_verify(&l_pk_a, l_pop, POP_BYTES);
    dap_assert(rc == 0, "pop_verify under correct pk OK");

    DAP_DELETE(l_pop);
    chipmunk_ht_private_key_clear(&l_sk_a);
    chipmunk_ht_private_key_clear(&l_sk_b);
    return true;
}

static bool s_test_pop_verify_rejects_bad_magic(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    s_keypair_from_seed_byte(0x55, &l_pk, &l_sk);

    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, POP_BYTES);
    int rc = chipmunk_ring_pop_create(&l_sk, l_pop, POP_BYTES);
    dap_assert(rc == 0, "pop_create OK");

    l_pop[0] ^= 0xFFu;   /* tamper magic LSB */
    rc = chipmunk_ring_pop_verify(&l_pk, l_pop, POP_BYTES);
    dap_assert(rc == -EINVAL, "tampered magic -> -EINVAL");

    DAP_DELETE(l_pop);
    chipmunk_ht_private_key_clear(&l_sk);
    return true;
}

static bool s_test_pop_verify_rejects_bad_version(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    s_keypair_from_seed_byte(0x66, &l_pk, &l_sk);

    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, POP_BYTES);
    dap_assert(chipmunk_ring_pop_create(&l_sk, l_pop, POP_BYTES) == 0, "create");

    l_pop[4] = 0x99u;   /* unknown version */
    int rc = chipmunk_ring_pop_verify(&l_pk, l_pop, POP_BYTES);
    dap_assert(rc == -EINVAL, "unknown version -> -EINVAL");

    l_pop[4] = 0x01u;   /* old CR-9.5 v1, intentionally unsupported */
    rc = chipmunk_ring_pop_verify(&l_pk, l_pop, POP_BYTES);
    dap_assert(rc == -EINVAL, "legacy v1 version -> -EINVAL");

    DAP_DELETE(l_pop);
    chipmunk_ht_private_key_clear(&l_sk);
    return true;
}

static bool s_test_pop_verify_rejects_nonzero_reserved(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    s_keypair_from_seed_byte(0x77, &l_pk, &l_sk);

    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, POP_BYTES);
    dap_assert(chipmunk_ring_pop_create(&l_sk, l_pop, POP_BYTES) == 0, "create");

    /* Test all three reserved bytes individually. */
    for (int b = 5; b <= 7; ++b) {
        uint8_t l_orig = l_pop[b];
        l_pop[b] = 0x01u;
        int rc = chipmunk_ring_pop_verify(&l_pk, l_pop, POP_BYTES);
        dap_assert(rc == -EINVAL, "non-zero reserved byte -> -EINVAL");
        l_pop[b] = l_orig;
    }
    /* Sanity: with reserved bytes restored, verify passes again. */
    dap_assert(chipmunk_ring_pop_verify(&l_pk, l_pop, POP_BYTES) == 0,
               "restored reserved bytes -> verify OK");

    DAP_DELETE(l_pop);
    chipmunk_ht_private_key_clear(&l_sk);
    return true;
}

static bool s_test_pop_verify_rejects_tampered_signature(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    s_keypair_from_seed_byte(0x88, &l_pk, &l_sk);

    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, POP_BYTES);
    dap_assert(chipmunk_ring_pop_create(&l_sk, l_pop, POP_BYTES) == 0, "create");

    /* Tamper a byte inside the signature body (offset 8 + 16 = near
     * the start of sigma[0]).  Envelope still valid, only the
     * crypto check should fail. */
    l_pop[CHIPMUNK_RING_POP_HEADER_BYTES + 16] ^= 0x01u;
    int rc = chipmunk_ring_pop_verify(&l_pk, l_pop, POP_BYTES);
    /* Acceptable: either signature-from-bytes detects malformedness
     * (negative) or ht_verify rejects (CHIPMUNK_ERROR_VERIFY_FAILED).
     * In either case the verifier MUST NOT return 0. */
    dap_assert(rc != 0, "tampered signature body -> verify fails");

    DAP_DELETE(l_pop);
    chipmunk_ht_private_key_clear(&l_sk);
    return true;
}

static bool s_test_pop_verify_bytes_equivalence(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    s_keypair_from_seed_byte(0x99, &l_pk, &l_sk);

    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, POP_BYTES);
    dap_assert(chipmunk_ring_pop_create(&l_sk, l_pop, POP_BYTES) == 0, "create");

    uint8_t l_pk_bytes[CHIPMUNK_HT_PUBLIC_KEY_SIZE];
    dap_assert(chipmunk_ht_public_key_to_bytes(l_pk_bytes, &l_pk) == 0, "pk->bytes");

    int rc_struct = chipmunk_ring_pop_verify(&l_pk, l_pop, POP_BYTES);
    int rc_bytes  = chipmunk_ring_pop_verify_bytes(l_pk_bytes, sizeof(l_pk_bytes),
                                                   l_pop, POP_BYTES);
    dap_assert(rc_struct == 0 && rc_bytes == 0,
               "struct and bytes verify both succeed on valid PoP");

    /* And both fail on tamper. */
    l_pop[0] ^= 0xFFu;
    rc_struct = chipmunk_ring_pop_verify(&l_pk, l_pop, POP_BYTES);
    rc_bytes  = chipmunk_ring_pop_verify_bytes(l_pk_bytes, sizeof(l_pk_bytes),
                                               l_pop, POP_BYTES);
    dap_assert(rc_struct == rc_bytes,
               "struct and bytes verify agree on tampered envelope");

    DAP_DELETE(l_pop);
    chipmunk_ht_private_key_clear(&l_sk);
    return true;
}

/* THE rogue-key regression test.
 *
 *   Scenario: target generates a real keypair (pk_target, sk_target)
 *   and publishes a valid PoP over pk_target.  Attacker observes
 *   pk_target's PoP blob on the wire.  Attacker then publishes a
 *   forged "rogue" pk_rogue (constructed *algebraically* from
 *   pk_target — here we use a simple XOR tweak — without ever
 *   knowing sk_rogue) and tries to pass off the captured PoP as
 *   proof of possession for pk_rogue.
 *
 *   The PoP message is pk-bound (SHA3-256(domain || LE32(len) ||
 *   pk_bytes)), so the captured PoP fails verification under
 *   pk_rogue: pop_message recomputed under pk_rogue ≠
 *   pop_message at sign time under pk_target ⇒
 *   ht_verify returns CHIPMUNK_ERROR_VERIFY_FAILED.
 *
 *   This is the central security claim of CR-9.5 — locking it in
 *   here prevents any future regression that would, say, drop the
 *   pk binding from pop_message or admit a fixed message instead. */
static bool s_test_pop_rogue_key_attack_rejected(void)
{
    chipmunk_ht_public_key_t  l_pk_target, l_pk_rogue;
    chipmunk_ht_private_key_t l_sk_target;
    s_keypair_from_seed_byte(0xA0, &l_pk_target, &l_sk_target);

    /* Algebraic forgery: pk_rogue = pk_target with rho_seed[0] XOR'd.
     * The attacker NEVER materialises sk_rogue. */
    l_pk_rogue = l_pk_target;
    l_pk_rogue.rho_seed[0] ^= 0xFFu;

    /* Honest PoP under sk_target. */
    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, POP_BYTES);
    dap_assert(chipmunk_ring_pop_create(&l_sk_target, l_pop, POP_BYTES) == 0,
               "honest PoP for pk_target");

    /* Verify under the honest pk → must succeed (baseline). */
    int rc_honest = chipmunk_ring_pop_verify(&l_pk_target, l_pop, POP_BYTES);
    dap_assert(rc_honest == 0, "honest PoP verifies under pk_target");

    /* Replay the SAME PoP blob claiming pk_rogue → must fail. */
    int rc_rogue = chipmunk_ring_pop_verify(&l_pk_rogue, l_pop, POP_BYTES);
    dap_assert(rc_rogue == CHIPMUNK_ERROR_VERIFY_FAILED,
               "rogue-key attack: replayed PoP does NOT verify under forged pk");

    DAP_DELETE(l_pop);
    chipmunk_ht_private_key_clear(&l_sk_target);
    return true;
}

static bool s_test_pop_zeroisation_on_error(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    s_keypair_from_seed_byte(0xB0, &l_pk, &l_sk);

    /* Pre-fill the output with non-zero, hit -EINVAL (too-small
     * buffer), and assert every byte got zeroised. */
    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, POP_BYTES);
    memset(l_pop, 0xCC, POP_BYTES);
    int rc = chipmunk_ring_pop_create(&l_sk, l_pop, POP_BYTES - 1);
    dap_assert(rc == -EINVAL, "too-small buffer -> -EINVAL");
    for (size_t i = 0; i < POP_BYTES - 1; ++i) {
        /* Only the size we passed is contractually zeroised. */
        dap_assert(l_pop[i] == 0u, "output zeroised on -EINVAL");
    }

    DAP_DELETE(l_pop);
    chipmunk_ht_private_key_clear(&l_sk);
    return true;
}

/* ------------------------------------------------------------------ */
/* Test runner                                                        */
/* ------------------------------------------------------------------ */

int main(void)
{
    dap_set_appname("test_chipmunk_ring_pop");
    dap_common_init("test_chipmunk_ring_pop", NULL);

    int l_rc = 0;
    if (!s_test_pop_roundtrip_deterministic())               l_rc = 1;
    if (!s_test_pop_roundtrip_random())                      l_rc = 1;
    if (!s_test_pop_create_accepts_used_sk_and_preserves_counter()) l_rc = 1;
    if (!s_test_pop_preserves_full_production_budget())       l_rc = 1;
    if (!s_test_pop_create_rejects_null())                   l_rc = 1;
    if (!s_test_pop_verify_rejects_wrong_pk())               l_rc = 1;
    if (!s_test_pop_verify_rejects_bad_magic())              l_rc = 1;
    if (!s_test_pop_verify_rejects_bad_version())            l_rc = 1;
    if (!s_test_pop_verify_rejects_nonzero_reserved())       l_rc = 1;
    if (!s_test_pop_verify_rejects_tampered_signature())     l_rc = 1;
    if (!s_test_pop_verify_bytes_equivalence())              l_rc = 1;
    if (!s_test_pop_rogue_key_attack_rejected())             l_rc = 1;
    if (!s_test_pop_zeroisation_on_error())                  l_rc = 1;

    if (l_rc == 0) {
        log_it(L_INFO, "ALL CR-9.5 PoP tests PASSED");
    } else {
        log_it(L_ERROR, "Some CR-9.5 PoP tests FAILED");
    }
    dap_common_deinit();
    return l_rc;
}
