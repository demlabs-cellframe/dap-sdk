/*
 * Regression test for CR-D3: Chipmunk private keys are strictly one-time,
 * enforced by the leaf_index counter embedded in the serialized private-key
 * buffer.  Any second signing attempt MUST fail fast with
 * CHIPMUNK_ERROR_KEY_EXHAUSTED and leave the counter pinned, so that a
 * would-be attacker cannot exploit HOTS key recovery by observing two
 * signatures on different messages.
 *
 * This test pins down the following invariants:
 *
 *   1. Fresh keypair serializes with leaf_index = 0.
 *   2. First chipmunk_sign succeeds and advances leaf_index to 1.
 *   3. First signature verifies under the published public key.
 *   4. Second chipmunk_sign (any message) returns CHIPMUNK_ERROR_KEY_EXHAUSTED
 *      and does NOT overwrite the signature buffer with fresh bytes.
 *   5. After the refused second attempt the first signature still verifies —
 *      the failed call must not corrupt the private key.
 *   6. Two keypairs derived from the SAME seed produce IDENTICAL public
 *      keys and EQUAL initial leaf_index — a probe against the CR-D3
 *      derivation being seed-deterministic (needed for CR-D8 tests that
 *      simulate "same signer" via seed reuse).
 *   7. Two signatures produced with two independent seeded keypairs over
 *      the same message are BIT-IDENTICAL, because HOTS signing is a pure
 *      function of (key material, message).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "dap_common.h"
#include "chipmunk/chipmunk.h"

#define MSG1 "cr-d3 probe: first message"
#define MSG2 "cr-d3 probe: DIFFERENT second message"

static int s_expect(int a_cond, const char *a_label)
{
    if (!a_cond) {
        fprintf(stderr, "FAIL: %s\n", a_label);
        return 0;
    }
    return 1;
}

static uint32_t s_read_leaf_index_be32(const uint8_t *a_priv_buf)
{
    return ((uint32_t)a_priv_buf[0] << 24) |
           ((uint32_t)a_priv_buf[1] << 16) |
           ((uint32_t)a_priv_buf[2] << 8)  |
           ((uint32_t)a_priv_buf[3]);
}

int main(void)
{
    dap_common_init("chipmunk-one-time-exhaustion", NULL);

    uint8_t l_pub[CHIPMUNK_PUBLIC_KEY_SIZE];
    uint8_t l_priv[CHIPMUNK_PRIVATE_KEY_SIZE];
    uint8_t l_sig1[CHIPMUNK_SIGNATURE_SIZE];
    uint8_t l_sig2[CHIPMUNK_SIGNATURE_SIZE];

    memset(l_pub, 0, sizeof(l_pub));
    memset(l_priv, 0, sizeof(l_priv));
    memset(l_sig1, 0xAA, sizeof(l_sig1));
    memset(l_sig2, 0xBB, sizeof(l_sig2));

    if (!s_expect(chipmunk_keypair(l_pub, sizeof(l_pub), l_priv, sizeof(l_priv)) == CHIPMUNK_ERROR_SUCCESS,
                  "chipmunk_keypair succeeds"))
        return 1;

    /* (1) fresh private key serializes with leaf_index = 0 */
    if (!s_expect(s_read_leaf_index_be32(l_priv) == 0u,
                  "fresh keypair carries leaf_index == 0"))
        return 1;

    /* (2) first signing advances the counter */
    int l_rc1 = chipmunk_sign(l_priv, (const uint8_t *)MSG1, strlen(MSG1), l_sig1);
    if (!s_expect(l_rc1 == CHIPMUNK_ERROR_SUCCESS, "first chipmunk_sign succeeds"))
        return 1;
    if (!s_expect(s_read_leaf_index_be32(l_priv) == 1u,
                  "leaf_index advanced to 1 after first sign"))
        return 1;

    /* (3) that signature actually verifies */
    if (!s_expect(chipmunk_verify(l_pub, (const uint8_t *)MSG1, strlen(MSG1), l_sig1) == CHIPMUNK_ERROR_SUCCESS,
                  "first signature verifies under published public key"))
        return 1;

    /* (4) second signing is rejected fail-fast */
    uint8_t l_sig2_baseline[CHIPMUNK_SIGNATURE_SIZE];
    memcpy(l_sig2_baseline, l_sig2, sizeof(l_sig2_baseline));
    int l_rc2 = chipmunk_sign(l_priv, (const uint8_t *)MSG2, strlen(MSG2), l_sig2);
    if (!s_expect(l_rc2 == CHIPMUNK_ERROR_KEY_EXHAUSTED,
                  "second chipmunk_sign returns CHIPMUNK_ERROR_KEY_EXHAUSTED"))
        return 1;
    if (!s_expect(memcmp(l_sig2, l_sig2_baseline, sizeof(l_sig2)) == 0,
                  "refused second sign MUST NOT touch the signature buffer"))
        return 1;
    if (!s_expect(s_read_leaf_index_be32(l_priv) == 1u,
                  "refused second sign MUST NOT advance the counter further"))
        return 1;

    /* (5) first signature still verifies after the failed second attempt */
    if (!s_expect(chipmunk_verify(l_pub, (const uint8_t *)MSG1, strlen(MSG1), l_sig1) == CHIPMUNK_ERROR_SUCCESS,
                  "first signature remains valid after refused second attempt"))
        return 1;

    /* (6) seed-deterministic keypairs share the same public key and both
     * start at leaf_index = 0 */
    uint8_t l_seed[32] = {
        0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
        0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
        0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
        0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
    };
    uint8_t l_pubA[CHIPMUNK_PUBLIC_KEY_SIZE], l_pubB[CHIPMUNK_PUBLIC_KEY_SIZE];
    uint8_t l_privA[CHIPMUNK_PRIVATE_KEY_SIZE], l_privB[CHIPMUNK_PRIVATE_KEY_SIZE];

    if (!s_expect(chipmunk_keypair_from_seed(l_seed, l_pubA, sizeof(l_pubA), l_privA, sizeof(l_privA)) == 0,
                  "seeded keypair A"))
        return 1;
    if (!s_expect(chipmunk_keypair_from_seed(l_seed, l_pubB, sizeof(l_pubB), l_privB, sizeof(l_privB)) == 0,
                  "seeded keypair B"))
        return 1;
    if (!s_expect(memcmp(l_pubA, l_pubB, sizeof(l_pubA)) == 0,
                  "same seed -> identical public key"))
        return 1;
    if (!s_expect(s_read_leaf_index_be32(l_privA) == 0u && s_read_leaf_index_be32(l_privB) == 0u,
                  "both seeded keypairs start at leaf_index = 0"))
        return 1;

    /* (7) HOTS signing is deterministic in (key material, message) */
    uint8_t l_sigA[CHIPMUNK_SIGNATURE_SIZE];
    uint8_t l_sigB[CHIPMUNK_SIGNATURE_SIZE];
    if (!s_expect(chipmunk_sign(l_privA, (const uint8_t *)MSG1, strlen(MSG1), l_sigA) == 0,
                  "sign with seeded keypair A"))
        return 1;
    if (!s_expect(chipmunk_sign(l_privB, (const uint8_t *)MSG1, strlen(MSG1), l_sigB) == 0,
                  "sign with seeded keypair B"))
        return 1;
    if (!s_expect(memcmp(l_sigA, l_sigB, sizeof(l_sigA)) == 0,
                  "deterministic HOTS: same seed + same message -> same signature"))
        return 1;

    printf("PASS: CR-D3 one-time exhaustion invariants all hold\n");
    return 0;
}
