/*
 * CR-D10 regression: canonical wire codec for chipmunk_multi_signature_t
 * and the dap_sign aggregate verifier that consumes it.
 *
 * Covers:
 *   - round-trip: deserialize(serialize(x)) must reconstruct x
 *   - dap_sign bridge: from_multi_sig -> dap_sign_verify_aggregated == 0
 *   - wrong message (hash mismatch, divergent messages) rejected
 *   - malformed blobs rejected:
 *       * bad magic
 *       * bad version
 *       * non-zero reserved byte
 *       * signer_count = 0
 *       * payload_length lie
 *       * truncated buffer
 *       * trailing garbage
 *       * flipped coefficient (verify fails, codec passes — that's the
 *         whole point: the codec is a pure transport)
 *       * legacy raw-struct blob (flat memcpy of chipmunk_multi_signature_t)
 *         MUST be rejected before any pointer dereference.
 *
 *  Schema-driven decoder regression (since CR-D10 stage-3):
 *       * per-signer path_length tampered (header common count diverges
 *         from inner path_length[i]) — must be rejected by the post-
 *         schema cross-check in chipmunk_multi_signature_deserialize.
 *       * is_randomized byte set to a value outside {0,1}
 *       * 3-byte reserved block in the body has a non-zero byte
 *       * 8-signer fixture round-trip via the dap_sign bridge to
 *         exercise the array-of-structs schema walk at scale.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "dap_common.h"
#include "dap_sign.h"
#include "dap_sign_chipmunk.h"
#include "dap_enc.h"
#include "dap_test.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_hots.h"
#include "chipmunk/chipmunk_tree.h"
#include "chipmunk/chipmunk_aggregation.h"
#include "chipmunk/chipmunk_multi_signature_codec.h"

#define LOG_TAG "test_chipmunk_multi_sig_codec"

#define TEST_ASSERT(cond, msg)                                           \
    do {                                                                 \
        if (!(cond)) {                                                   \
            log_it(L_ERROR, "[FAIL] %s:%d: %s  (expr: %s)",              \
                   __FILE__, __LINE__, (msg), #cond);                    \
            return false;                                                \
        }                                                                \
    } while (0)

/* ---------------------------------------------------------------------- *
 *  Fixture: build a real N-signer multi-signature                         *
 * ---------------------------------------------------------------------- */

#define FIXTURE_MAX_SIGNERS 16

typedef struct {
    chipmunk_tree_t            tree;
    chipmunk_hvc_hasher_t      hasher;
    chipmunk_individual_sig_t  individual_sigs[FIXTURE_MAX_SIGNERS];
    chipmunk_multi_signature_t multi_sig;
    size_t                     num_signers;
    bool tree_ready;
    bool sigs_ready;
    bool multi_ready;
} fixture_t;

static void s_fixture_clear(fixture_t *f)
{
    if (!f) return;
    if (f->multi_ready) {
        /* chipmunk_aggregate_signatures_with_tree allocates the five
         * parallel arrays; free them via the non-deep helper — the
         * proofs[].nodes arrays are separately freed via individual_sigs.*/
        chipmunk_multi_signature_free(&f->multi_sig);
        f->multi_ready = false;
    }
    if (f->sigs_ready) {
        for (size_t i = 0; i < f->num_signers; ++i) {
            chipmunk_individual_signature_free(&f->individual_sigs[i]);
        }
        f->sigs_ready = false;
    }
    if (f->tree_ready) {
        chipmunk_tree_clear(&f->tree);
        f->tree_ready = false;
    }
    f->num_signers = 0;
}

static const char s_fixture_message[] = "CR-D10 wire codec regression message";

static bool s_fixture_build_n(fixture_t *f, size_t num_signers)
{
    memset(f, 0, sizeof(*f));

    if (num_signers == 0 || num_signers > FIXTURE_MAX_SIGNERS) {
        return false;
    }
    f->num_signers = num_signers;

    chipmunk_private_key_t  priv_keys[FIXTURE_MAX_SIGNERS];
    chipmunk_public_key_t   pub_keys[FIXTURE_MAX_SIGNERS];
    chipmunk_hots_pk_t      hots_pks[FIXTURE_MAX_SIGNERS];
    chipmunk_hots_sk_t      hots_sks[FIXTURE_MAX_SIGNERS];

    for (size_t i = 0; i < num_signers; ++i) {
        int rc = chipmunk_keypair((uint8_t *)&pub_keys[i],  sizeof(pub_keys[i]),
                                  (uint8_t *)&priv_keys[i], sizeof(priv_keys[i]));
        TEST_ASSERT(rc == 0, "chipmunk_keypair failed");

        hots_pks[i].v0 = priv_keys[i].pk.v0;
        hots_pks[i].v1 = priv_keys[i].pk.v1;

        chipmunk_hots_params_t params;
        TEST_ASSERT(chipmunk_hots_setup(&params) == 0, "hots_setup");

        uint8_t seed[32];
        memcpy(seed, priv_keys[i].key_seed, 32);
        TEST_ASSERT(chipmunk_hots_keygen(seed, (uint32_t)i, &params,
                                         &hots_pks[i], &hots_sks[i]) == 0,
                    "hots_keygen");
    }

    uint8_t hasher_seed[32];
    for (int i = 0; i < 32; ++i) hasher_seed[i] = (uint8_t)(i + 1);
    TEST_ASSERT(chipmunk_hvc_hasher_init(&f->hasher, hasher_seed) == 0,
                "hvc_hasher_init");

    chipmunk_hvc_poly_t leaf_nodes[CHIPMUNK_TREE_LEAF_COUNT_DEFAULT];
    memset(leaf_nodes, 0, sizeof(leaf_nodes));
    for (size_t i = 0; i < num_signers; ++i) {
        chipmunk_public_key_t wrap;
        memset(&wrap, 0, sizeof(wrap));
        memcpy(&wrap.v0, &hots_pks[i].v0, sizeof(chipmunk_poly_t));
        memcpy(&wrap.v1, &hots_pks[i].v1, sizeof(chipmunk_poly_t));
        TEST_ASSERT(chipmunk_hots_pk_to_hvc_poly(&wrap, &leaf_nodes[i]) == 0,
                    "pk_to_hvc_poly");
    }

    TEST_ASSERT(chipmunk_tree_new_with_leaf_nodes(&f->tree, leaf_nodes,
                                                   num_signers, &f->hasher) == 0,
                "tree_new_with_leaf_nodes");
    f->tree_ready = true;

    for (size_t i = 0; i < num_signers; ++i) {
        int rc = chipmunk_create_individual_signature(
                     (const uint8_t *)s_fixture_message,
                     sizeof(s_fixture_message) - 1,
                     &hots_sks[i], &hots_pks[i],
                     pub_keys[i].rho_seed,
                     &f->tree, i,
                     &f->individual_sigs[i]);
        TEST_ASSERT(rc == 0, "create_individual_signature");
    }
    f->sigs_ready = true;

    TEST_ASSERT(chipmunk_aggregate_signatures_with_tree(
                    f->individual_sigs, num_signers,
                    (const uint8_t *)s_fixture_message,
                    sizeof(s_fixture_message) - 1,
                    &f->tree, &f->multi_sig) == 0,
                "aggregate_signatures_with_tree");
    f->multi_ready = true;

    TEST_ASSERT(chipmunk_verify_multi_signature(
                    &f->multi_sig,
                    (const uint8_t *)s_fixture_message,
                    sizeof(s_fixture_message) - 1) == 1,
                "sanity: multi_sig verifies in-memory");

    return true;
}

static bool s_fixture_build(fixture_t *f)
{
    return s_fixture_build_n(f, 3);
}

/* ---------------------------------------------------------------------- *
 *  Helpers                                                                *
 * ---------------------------------------------------------------------- */

static bool s_byte_equal_polys(const int32_t *a, const int32_t *b)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

/* ---------------------------------------------------------------------- *
 *  Test cases                                                             *
 * ---------------------------------------------------------------------- */

static bool test_round_trip(void)
{
    fixture_t f;
    TEST_ASSERT(s_fixture_build(&f), "fixture build");

    size_t expected = 0;
    TEST_ASSERT(chipmunk_multi_signature_serialized_size(&f.multi_sig, &expected)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK,
                "serialized_size");

    uint8_t *buf = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, expected);
    TEST_ASSERT(buf, "alloc buf");

    size_t written = 0;
    TEST_ASSERT(chipmunk_multi_signature_serialize(&f.multi_sig, buf, expected,
                                                   &written)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK,
                "serialize");
    TEST_ASSERT(written == expected, "written == expected");

    chipmunk_multi_signature_t copy;
    memset(&copy, 0, sizeof(copy));
    TEST_ASSERT(chipmunk_multi_signature_deserialize(buf, expected, &copy)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK,
                "deserialize");

    /* Structural equality: all parallel arrays must match byte-for-byte.*/
    TEST_ASSERT(copy.signer_count == f.multi_sig.signer_count, "signer_count");
    TEST_ASSERT(memcmp(copy.message_hash, f.multi_sig.message_hash,
                       sizeof(copy.message_hash)) == 0,
                "message_hash");
    TEST_ASSERT(memcmp(copy.hvc_hasher_seed, f.multi_sig.hvc_hasher_seed,
                       sizeof(copy.hvc_hasher_seed)) == 0,
                "hvc_hasher_seed");
    TEST_ASSERT(copy.aggregated_hots.is_randomized
                    == f.multi_sig.aggregated_hots.is_randomized,
                "is_randomized");
    TEST_ASSERT(s_byte_equal_polys(copy.tree_root.coeffs,
                                   f.multi_sig.tree_root.coeffs),
                "tree_root");
    for (int k = 0; k < CHIPMUNK_GAMMA; ++k) {
        TEST_ASSERT(s_byte_equal_polys(copy.aggregated_hots.sigma[k].coeffs,
                                        f.multi_sig.aggregated_hots.sigma[k].coeffs),
                    "aggregated sigma");
    }
    for (size_t i = 0; i < copy.signer_count; ++i) {
        TEST_ASSERT(s_byte_equal_polys(copy.public_key_roots[i].coeffs,
                                        f.multi_sig.public_key_roots[i].coeffs),
                    "public_key_roots[i]");
        TEST_ASSERT(s_byte_equal_polys(copy.hots_pks[i].v0.coeffs,
                                        f.multi_sig.hots_pks[i].v0.coeffs),
                    "hots_pks[i].v0");
        TEST_ASSERT(s_byte_equal_polys(copy.hots_pks[i].v1.coeffs,
                                        f.multi_sig.hots_pks[i].v1.coeffs),
                    "hots_pks[i].v1");
        TEST_ASSERT(memcmp(copy.rho_seeds[i], f.multi_sig.rho_seeds[i], 32) == 0,
                    "rho_seeds[i]");
        TEST_ASSERT(copy.leaf_indices[i] == f.multi_sig.leaf_indices[i],
                    "leaf_indices[i]");
        TEST_ASSERT(copy.proofs[i].index == f.multi_sig.proofs[i].index,
                    "proof.index");
        TEST_ASSERT(copy.proofs[i].path_length == f.multi_sig.proofs[i].path_length,
                    "proof.path_length");
        for (size_t j = 0; j < copy.proofs[i].path_length; ++j) {
            TEST_ASSERT(s_byte_equal_polys(
                            copy.proofs[i].nodes[j].left.coeffs,
                            f.multi_sig.proofs[i].nodes[j].left.coeffs),
                        "proof node left");
            TEST_ASSERT(s_byte_equal_polys(
                            copy.proofs[i].nodes[j].right.coeffs,
                            f.multi_sig.proofs[i].nodes[j].right.coeffs),
                        "proof node right");
        }
    }

    /* Re-serialising the copy must produce byte-identical wire bytes —  */
    /* the codec is canonical.                                            */
    uint8_t *buf2 = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, expected);
    TEST_ASSERT(buf2, "alloc buf2");
    size_t written2 = 0;
    TEST_ASSERT(chipmunk_multi_signature_serialize(&copy, buf2, expected, &written2)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK,
                "serialize copy");
    TEST_ASSERT(written2 == expected, "written2 == expected");
    TEST_ASSERT(memcmp(buf, buf2, expected) == 0, "canonical serialisation");

    /* Deserialised copy must still verify as a valid Chipmunk multi-sig.*/
    TEST_ASSERT(chipmunk_verify_multi_signature(
                    &copy,
                    (const uint8_t *)s_fixture_message,
                    sizeof(s_fixture_message) - 1) == 1,
                "deserialised copy verifies");

    DAP_DELETE(buf);
    DAP_DELETE(buf2);
    chipmunk_multi_signature_deep_free(&copy);
    s_fixture_clear(&f);
    return true;
}

static bool test_dap_sign_bridge_happy(void)
{
    fixture_t f;
    TEST_ASSERT(s_fixture_build(&f), "fixture build");

    dap_sign_t *sign = dap_sign_from_chipmunk_multi_signature(&f.multi_sig);
    TEST_ASSERT(sign != NULL, "dap_sign bridge returns non-NULL");
    TEST_ASSERT(sign->header.type.type == SIG_TYPE_CHIPMUNK, "SIG_TYPE_CHIPMUNK");
    TEST_ASSERT(sign->header.sign_pkey_size == 0, "no pkey in aggregate");
    TEST_ASSERT(sign->header.sign_size > 0, "non-empty sig payload");

    const void *messages[3]       = { s_fixture_message, s_fixture_message, s_fixture_message };
    const size_t message_sizes[3] = { sizeof(s_fixture_message) - 1,
                                      sizeof(s_fixture_message) - 1,
                                      sizeof(s_fixture_message) - 1 };
    dap_pkey_t *pubkeys[3] = { NULL, NULL, NULL };

    int rc = dap_sign_verify_aggregated(sign, messages, message_sizes, pubkeys, 3);
    TEST_ASSERT(rc == 0, "dap_sign_verify_aggregated == 0 on happy path");

    /* Divergent message lengths must be rejected with -4. */
    const size_t bad_sizes[3] = { sizeof(s_fixture_message) - 1,
                                  sizeof(s_fixture_message) - 2,
                                  sizeof(s_fixture_message) - 1 };
    rc = dap_sign_verify_aggregated(sign, messages, bad_sizes, pubkeys, 3);
    TEST_ASSERT(rc == -4, "divergent message sizes rejected");

    /* Divergent message bytes rejected with -4 (same length, different payload). */
    const char alt_message[] = "CR-D10 wire codec regression messaGe";
    const void *mixed_msgs[3] = { s_fixture_message, alt_message, s_fixture_message };
    const size_t mixed_sizes[3] = { sizeof(s_fixture_message) - 1,
                                    sizeof(alt_message) - 1,
                                    sizeof(s_fixture_message) - 1 };
    rc = dap_sign_verify_aggregated(sign, mixed_msgs, mixed_sizes, pubkeys, 3);
    TEST_ASSERT(rc == -4, "divergent message bytes rejected");

    /* Wrong message entirely — hash mismatch with wire commitment. */
    const char wrong[] = "wrong message entirely";
    const void *wrong_msgs[3] = { wrong, wrong, wrong };
    const size_t wrong_sizes[3] = { sizeof(wrong) - 1,
                                     sizeof(wrong) - 1,
                                     sizeof(wrong) - 1 };
    rc = dap_sign_verify_aggregated(sign, wrong_msgs, wrong_sizes, pubkeys, 3);
    TEST_ASSERT(rc == -4, "wrong message rejected");

    /* Signer count mismatch. */
    rc = dap_sign_verify_aggregated(sign, messages, message_sizes, pubkeys, 2);
    TEST_ASSERT(rc == -3, "signer count mismatch rejected");

    DAP_DELETE(sign);
    s_fixture_clear(&f);
    return true;
}

static bool test_malformed_blobs(void)
{
    fixture_t f;
    TEST_ASSERT(s_fixture_build(&f), "fixture build");

    size_t blob_size = 0;
    TEST_ASSERT(chipmunk_multi_signature_serialized_size(&f.multi_sig, &blob_size)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK,
                "size");
    uint8_t *blob = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, blob_size);
    TEST_ASSERT(blob, "alloc blob");
    TEST_ASSERT(chipmunk_multi_signature_serialize(&f.multi_sig, blob, blob_size, NULL)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK,
                "serialize");

    chipmunk_multi_signature_t out;

    /* Bad magic */
    {
        uint8_t *clone = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, blob_size);
        memcpy(clone, blob, blob_size);
        clone[0] = 'X';
        memset(&out, 0, sizeof(out));
        int rc = chipmunk_multi_signature_deserialize(clone, blob_size, &out);
        TEST_ASSERT(rc == CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_MAGIC, "bad magic");
        chipmunk_multi_signature_deep_free(&out);
        DAP_DELETE(clone);
    }

    /* Bad version */
    {
        uint8_t *clone = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, blob_size);
        memcpy(clone, blob, blob_size);
        clone[4] = 0x99; clone[5] = 0x00;  /* version = 0x0099 */
        memset(&out, 0, sizeof(out));
        int rc = chipmunk_multi_signature_deserialize(clone, blob_size, &out);
        TEST_ASSERT(rc == CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_VERSION, "bad version");
        chipmunk_multi_signature_deep_free(&out);
        DAP_DELETE(clone);
    }

    /* Non-zero reserved16 */
    {
        uint8_t *clone = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, blob_size);
        memcpy(clone, blob, blob_size);
        clone[6] = 0x01;  /* reserved16 lo byte */
        memset(&out, 0, sizeof(out));
        int rc = chipmunk_multi_signature_deserialize(clone, blob_size, &out);
        TEST_ASSERT(rc == CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_RESERVED,
                    "bad reserved16");
        chipmunk_multi_signature_deep_free(&out);
        DAP_DELETE(clone);
    }

    /* signer_count = 0 */
    {
        uint8_t *clone = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, blob_size);
        memcpy(clone, blob, blob_size);
        clone[8] = 0; clone[9] = 0; clone[10] = 0; clone[11] = 0;
        memset(&out, 0, sizeof(out));
        int rc = chipmunk_multi_signature_deserialize(clone, blob_size, &out);
        TEST_ASSERT(rc == CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_SIGNER_COUNT,
                    "zero signer_count");
        chipmunk_multi_signature_deep_free(&out);
        DAP_DELETE(clone);
    }

    /* payload_length lie (header says blob is 1 byte shorter than it is) */
    {
        uint8_t *clone = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, blob_size);
        memcpy(clone, blob, blob_size);
        uint64_t fake = (uint64_t)(blob_size - 1);
        for (int i = 0; i < 8; ++i) {
            clone[16 + i] = (uint8_t)((fake >> (8 * i)) & 0xFF);
        }
        memset(&out, 0, sizeof(out));
        int rc = chipmunk_multi_signature_deserialize(clone, blob_size, &out);
        TEST_ASSERT(rc == CHIPMUNK_MULTI_SIG_CODEC_ERR_SIZE_MISMATCH,
                    "payload_length lie");
        chipmunk_multi_signature_deep_free(&out);
        DAP_DELETE(clone);
    }

    /* Truncated buffer */
    {
        memset(&out, 0, sizeof(out));
        int rc = chipmunk_multi_signature_deserialize(blob, blob_size - 1, &out);
        TEST_ASSERT(rc != CHIPMUNK_MULTI_SIG_CODEC_OK, "truncated rejected");
        chipmunk_multi_signature_deep_free(&out);
    }

    /* Trailing garbage */
    {
        uint8_t *clone = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, blob_size + 4);
        memcpy(clone, blob, blob_size);
        clone[blob_size]     = 0xDE;
        clone[blob_size + 1] = 0xAD;
        clone[blob_size + 2] = 0xBE;
        clone[blob_size + 3] = 0xEF;
        memset(&out, 0, sizeof(out));
        int rc = chipmunk_multi_signature_deserialize(clone, blob_size + 4, &out);
        TEST_ASSERT(rc == CHIPMUNK_MULTI_SIG_CODEC_ERR_SIZE_MISMATCH,
                    "trailing garbage rejected");
        chipmunk_multi_signature_deep_free(&out);
        DAP_DELETE(clone);
    }

    /* Legacy raw-struct blob (memcpy of the struct) — the old CR-D10
     * regression path.  The raw struct starts with arbitrary pointer
     * bytes / flags, not the "CHMA" magic, so deserialisation must
     * fail on BAD_MAGIC before any pointer is dereferenced.            */
    {
        size_t raw_size = sizeof(chipmunk_multi_signature_t);
        uint8_t *raw = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, raw_size);
        memcpy(raw, &f.multi_sig, raw_size);
        memset(&out, 0, sizeof(out));
        int rc = chipmunk_multi_signature_deserialize(raw, raw_size, &out);
        TEST_ASSERT(rc != CHIPMUNK_MULTI_SIG_CODEC_OK,
                    "legacy raw struct rejected");
        chipmunk_multi_signature_deep_free(&out);
        DAP_DELETE(raw);
    }

    /* Coefficient flip — codec still passes (pure transport), but the */
    /* cryptographic verifier must reject.                              */
    {
        uint8_t *clone = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, blob_size);
        memcpy(clone, blob, blob_size);
        /* Flip one byte inside the aggregated_hots payload — the        */
        /* fixed-body offset is: header(24) + message_hash(32)           */
        /* + hasher_seed(32) + flag+rsvd(4) + tree_root(N*4).            */
        size_t off = 24u + 32u + 32u + 4u + (size_t)CHIPMUNK_N * 4u;
        clone[off] ^= 0x01;
        memset(&out, 0, sizeof(out));
        int rc = chipmunk_multi_signature_deserialize(clone, blob_size, &out);
        TEST_ASSERT(rc == CHIPMUNK_MULTI_SIG_CODEC_OK,
                    "coefficient flip accepted by codec");
        int vrc = chipmunk_verify_multi_signature(
                      &out,
                      (const uint8_t *)s_fixture_message,
                      sizeof(s_fixture_message) - 1);
        TEST_ASSERT(vrc != 1, "coefficient flip rejected by verifier");
        chipmunk_multi_signature_deep_free(&out);
        DAP_DELETE(clone);
    }

    /* Same test but through the dap_sign aggregate verifier: wrap a   */
    /* tampered blob in a dap_sign_t manually and confirm the verifier */
    /* rejects it with -5 (verifier NACK) or -2 (parse reject).        */
    {
        uint8_t *clone = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, blob_size);
        memcpy(clone, blob, blob_size);
        size_t off = 24u + 32u + 32u + 4u + (size_t)CHIPMUNK_N * 4u;
        clone[off] ^= 0x01;

        dap_sign_t *sign = DAP_NEW_Z_SIZE(dap_sign_t,
                                          sizeof(dap_sign_t) + blob_size);
        sign->header.type.type      = SIG_TYPE_CHIPMUNK;
        sign->header.hash_type      = DAP_SIGN_HASH_TYPE_SHA3;
        sign->header.sign_size      = (uint32_t)blob_size;
        sign->header.sign_pkey_size = 0;
        memcpy(sign->pkey_n_sign, clone, blob_size);

        const void *messages[3]       = { s_fixture_message, s_fixture_message, s_fixture_message };
        const size_t message_sizes[3] = { sizeof(s_fixture_message) - 1,
                                          sizeof(s_fixture_message) - 1,
                                          sizeof(s_fixture_message) - 1 };
        dap_pkey_t *pubkeys[3] = { NULL, NULL, NULL };
        int rc = dap_sign_verify_aggregated(sign, messages, message_sizes, pubkeys, 3);
        TEST_ASSERT(rc != 0, "dap_sign_verify_aggregated rejects tampered blob");

        DAP_DELETE(sign);
        DAP_DELETE(clone);
    }

    DAP_DELETE(blob);
    s_fixture_clear(&f);
    return true;
}

/* ---------------------------------------------------------------------- *
 *  Schema-driven decoder regression cases                                 *
 *                                                                         *
 *  These attack the post-decode validation layer in                       *
 *  chipmunk_multi_signature_deserialize().  After the schema engine has   *
 *  successfully consumed the wire bytes, the codec performs three        *
 *  protocol-level cross-checks (each per-signer path_length matches the  *
 *  header value, is_randomized is in {0,1}, reserved-3 is all zero).     *
 *  Bypassing the schema reader and emitting a structurally valid blob    *
 *  with these fields tampered must produce a deterministic rejection.   *
 * ---------------------------------------------------------------------- */

/* Body-relative offsets inside the schema-driven layout (after the
 * 24-byte CHMA header).  Mirrors `chipmunk_multi_signature_wire_schema`
 * exactly — keep in sync if the schema field order ever changes. */
#define BODY_OFF_MSG_HASH        0u
#define BODY_OFF_HASHER_SEED     32u
#define BODY_OFF_IS_RANDOMIZED   64u
#define BODY_OFF_RESERVED        65u
#define BODY_OFF_TREE_ROOT       68u
#define BODY_OFF_SIGMA           (BODY_OFF_TREE_ROOT + (size_t)CHIPMUNK_N * 4u)
#define BODY_OFF_SIGNERS         (BODY_OFF_SIGMA + (size_t)CHIPMUNK_GAMMA * (size_t)CHIPMUNK_N * 4u)

#define HDR_SIZE                 24u
#define POLY_BYTES               ((size_t)CHIPMUNK_N * 4u)

static bool test_is_randomized_invalid_value(void)
{
    fixture_t f;
    TEST_ASSERT(s_fixture_build(&f), "fixture build");

    size_t blob_size = 0;
    TEST_ASSERT(chipmunk_multi_signature_serialized_size(&f.multi_sig, &blob_size)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK, "size");
    uint8_t *blob = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, blob_size);
    TEST_ASSERT(blob, "alloc blob");
    TEST_ASSERT(chipmunk_multi_signature_serialize(&f.multi_sig, blob, blob_size, NULL)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK, "serialize");

    chipmunk_multi_signature_t out;

    /* Replace the 1-byte is_randomized flag with values outside {0,1}.
     * Schema decode succeeds — uint8 has no codec-level validation — but
     * the codec's post-decode check rejects it.                         */
    static const uint8_t s_bad_values[] = { 0x02, 0x42, 0xFE, 0xFF };
    uint8_t saved = blob[HDR_SIZE + BODY_OFF_IS_RANDOMIZED];
    for (size_t i = 0; i < sizeof(s_bad_values) / sizeof(s_bad_values[0]); ++i) {
        blob[HDR_SIZE + BODY_OFF_IS_RANDOMIZED] = s_bad_values[i];
        memset(&out, 0, sizeof(out));
        int rc = chipmunk_multi_signature_deserialize(blob, blob_size, &out);
        TEST_ASSERT(rc == CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_FLAG,
                    "is_randomized != {0,1} rejected with BAD_FLAG");
        chipmunk_multi_signature_deep_free(&out);
    }
    blob[HDR_SIZE + BODY_OFF_IS_RANDOMIZED] = saved;

    /* Sanity: untampered blob still round-trips. */
    memset(&out, 0, sizeof(out));
    TEST_ASSERT(chipmunk_multi_signature_deserialize(blob, blob_size, &out)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK,
                "restored blob deserialises cleanly");
    chipmunk_multi_signature_deep_free(&out);

    DAP_DELETE(blob);
    s_fixture_clear(&f);
    return true;
}

static bool test_body_reserved_nonzero(void)
{
    fixture_t f;
    TEST_ASSERT(s_fixture_build(&f), "fixture build");

    size_t blob_size = 0;
    TEST_ASSERT(chipmunk_multi_signature_serialized_size(&f.multi_sig, &blob_size)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK, "size");
    uint8_t *blob = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, blob_size);
    TEST_ASSERT(blob, "alloc blob");
    TEST_ASSERT(chipmunk_multi_signature_serialize(&f.multi_sig, blob, blob_size, NULL)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK, "serialize");

    chipmunk_multi_signature_t out;

    /* Each of the three reserved bytes in the body must be zero on the
     * wire.  Set each one independently and confirm BAD_RESERVED. */
    for (size_t i = 0; i < 3; ++i) {
        uint8_t saved = blob[HDR_SIZE + BODY_OFF_RESERVED + i];
        blob[HDR_SIZE + BODY_OFF_RESERVED + i] = 0x01;
        memset(&out, 0, sizeof(out));
        int rc = chipmunk_multi_signature_deserialize(blob, blob_size, &out);
        TEST_ASSERT(rc == CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_RESERVED,
                    "reserved byte != 0 rejected with BAD_RESERVED");
        chipmunk_multi_signature_deep_free(&out);
        blob[HDR_SIZE + BODY_OFF_RESERVED + i] = saved;
    }

    DAP_DELETE(blob);
    s_fixture_clear(&f);
    return true;
}

static bool test_inner_path_length_mismatch(void)
{
    fixture_t f;
    TEST_ASSERT(s_fixture_build(&f), "fixture build");
    TEST_ASSERT(f.multi_sig.signer_count >= 2,
                "fixture must have >= 2 signers for this test");

    size_t blob_size = 0;
    TEST_ASSERT(chipmunk_multi_signature_serialized_size(&f.multi_sig, &blob_size)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK, "size");
    uint8_t *blob = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, blob_size);
    TEST_ASSERT(blob, "alloc blob");
    TEST_ASSERT(chipmunk_multi_signature_serialize(&f.multi_sig, blob, blob_size, NULL)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK, "serialize");

    /* All proofs share the same path length by construction.  Compute
     * the size of one signer record and locate signer[1]'s path_length
     * field.                                                            */
    const size_t l_common_path_length = f.multi_sig.proofs[0].path_length;
    const size_t l_record_size = 3u * POLY_BYTES + 32u + 4u + 8u + 4u
                                + l_common_path_length * 2u * POLY_BYTES;

    /* Within a record, path_length sits at offset 3*N*4 + 32 + 4 + 8 = 3*N*4 + 44. */
    const size_t l_path_len_in_record = 3u * POLY_BYTES + 32u + 4u + 8u;
    const size_t l_signer1_record_off = BODY_OFF_SIGNERS + l_record_size;
    const size_t l_path_len_off       = HDR_SIZE + l_signer1_record_off + l_path_len_in_record;

    chipmunk_multi_signature_t out;

    /* Replace path_length[1] with l_common_path_length+1.  This causes
     * the schema reader to either read past the end of the buffer (size
     * mismatch) or — if the buffer is large enough — finish the body
     * but leave bytes_read != buffer_size.  Either way the codec must
     * reject before the post-decode cross-check ever runs.            */
    uint8_t saved[4];
    memcpy(saved, blob + l_path_len_off, 4);

    uint32_t bumped = (uint32_t)(l_common_path_length + 1);
    blob[l_path_len_off + 0] = (uint8_t)(bumped & 0xFF);
    blob[l_path_len_off + 1] = (uint8_t)((bumped >> 8) & 0xFF);
    blob[l_path_len_off + 2] = (uint8_t)((bumped >> 16) & 0xFF);
    blob[l_path_len_off + 3] = (uint8_t)((bumped >> 24) & 0xFF);

    memset(&out, 0, sizeof(out));
    int rc = chipmunk_multi_signature_deserialize(blob, blob_size, &out);
    TEST_ASSERT(rc != CHIPMUNK_MULTI_SIG_CODEC_OK,
                "path_length+1 rejected by codec");
    chipmunk_multi_signature_deep_free(&out);

    /* Now zero-out path_length[1] (still valid uint32, but mismatches the
     * header value).  The schema reader will simply allocate zero nodes,
     * succeed cleanly, and the codec's post-decode cross-check must fire
     * with PATH_LENGTH_MISMATCH.  This requires growing the buffer to
     * absorb the bytes the reader saved.                                */
    memcpy(blob + l_path_len_off, saved, 4);

    /* Build a synthetic blob where signer[1]'s path_length=0 by stripping
     * its node bytes from the buffer and adjusting the header total.    */
    {
        const size_t l_strip = l_common_path_length * 2u * POLY_BYTES;
        const size_t l_signer1_nodes_start =
                HDR_SIZE + l_signer1_record_off + l_path_len_in_record + 4u;
        size_t l_new_size = blob_size - l_strip;
        uint8_t *clone = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, l_new_size);
        TEST_ASSERT(clone, "alloc clone");
        /* Copy bytes up to and including signer[1].path_length. */
        memcpy(clone, blob, l_signer1_nodes_start);
        /* Skip the signer[1] nodes block; copy the rest. */
        memcpy(clone + l_signer1_nodes_start,
               blob + l_signer1_nodes_start + l_strip,
               blob_size - (l_signer1_nodes_start + l_strip));
        /* Patch signer[1].path_length to 0. */
        clone[l_path_len_off + 0] = 0;
        clone[l_path_len_off + 1] = 0;
        clone[l_path_len_off + 2] = 0;
        clone[l_path_len_off + 3] = 0;
        /* Patch payload_length in the header to the new size. */
        uint64_t l_total = (uint64_t)l_new_size;
        for (int i = 0; i < 8; ++i) {
            clone[16 + i] = (uint8_t)((l_total >> (8 * i)) & 0xFF);
        }

        memset(&out, 0, sizeof(out));
        rc = chipmunk_multi_signature_deserialize(clone, l_new_size, &out);
        TEST_ASSERT(rc == CHIPMUNK_MULTI_SIG_CODEC_ERR_PATH_LENGTH_MISMATCH,
                    "inner path_length=0 detected as PATH_LENGTH_MISMATCH");
        chipmunk_multi_signature_deep_free(&out);
        DAP_DELETE(clone);
    }

    DAP_DELETE(blob);
    s_fixture_clear(&f);
    return true;
}

static bool test_8_signers_dap_sign_round_trip(void)
{
    fixture_t f;
    TEST_ASSERT(s_fixture_build_n(&f, 8), "fixture build (8 signers)");

    dap_sign_t *sign = dap_sign_from_chipmunk_multi_signature(&f.multi_sig);
    TEST_ASSERT(sign != NULL, "dap_sign bridge returns non-NULL (8 signers)");
    TEST_ASSERT(sign->header.type.type == SIG_TYPE_CHIPMUNK, "SIG_TYPE_CHIPMUNK");
    TEST_ASSERT(sign->header.sign_size > 0, "non-empty sig payload");

    const void *messages[8];
    size_t       message_sizes[8];
    dap_pkey_t  *pubkeys[8];
    for (size_t i = 0; i < 8; ++i) {
        messages[i]      = s_fixture_message;
        message_sizes[i] = sizeof(s_fixture_message) - 1;
        pubkeys[i]       = NULL;
    }

    int rc = dap_sign_verify_aggregated(sign, messages, message_sizes, pubkeys, 8);
    TEST_ASSERT(rc == 0,
                "dap_sign_verify_aggregated == 0 on 8-signer happy path");

    /* Round-trip via the codec — re-serialise the deserialised copy and
     * confirm canonical (byte-identical) output, exercising the array-of-
     * structs schema walk twice over a non-trivial signer count.         */
    size_t expected = 0;
    TEST_ASSERT(chipmunk_multi_signature_serialized_size(&f.multi_sig, &expected)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK, "size");
    uint8_t *buf1 = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, expected);
    TEST_ASSERT(buf1, "alloc buf1");
    TEST_ASSERT(chipmunk_multi_signature_serialize(&f.multi_sig, buf1, expected, NULL)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK, "serialize");

    chipmunk_multi_signature_t copy;
    memset(&copy, 0, sizeof(copy));
    TEST_ASSERT(chipmunk_multi_signature_deserialize(buf1, expected, &copy)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK, "deserialize");
    TEST_ASSERT(copy.signer_count == 8, "signer_count == 8");

    uint8_t *buf2 = (uint8_t *)DAP_NEW_Z_SIZE(uint8_t, expected);
    TEST_ASSERT(buf2, "alloc buf2");
    TEST_ASSERT(chipmunk_multi_signature_serialize(&copy, buf2, expected, NULL)
                    == CHIPMUNK_MULTI_SIG_CODEC_OK, "serialize copy");
    TEST_ASSERT(memcmp(buf1, buf2, expected) == 0, "canonical 8-signer wire");

    DAP_DELETE(buf1);
    DAP_DELETE(buf2);
    chipmunk_multi_signature_deep_free(&copy);
    DAP_DELETE(sign);
    s_fixture_clear(&f);
    return true;
}

/* ---------------------------------------------------------------------- *
 *  Driver                                                                 *
 * ---------------------------------------------------------------------- */

int main(void)
{
    dap_common_init("test_chipmunk_multi_sig_codec", NULL);
    dap_log_level_set(L_ERROR);

    struct {
        const char *name;
        bool (*fn)(void);
    } tests[] = {
        { "round_trip",                       test_round_trip },
        { "dap_sign_bridge_happy",            test_dap_sign_bridge_happy },
        { "malformed_blobs",                  test_malformed_blobs },
        { "is_randomized_invalid_value",      test_is_randomized_invalid_value },
        { "body_reserved_nonzero",            test_body_reserved_nonzero },
        { "inner_path_length_mismatch",       test_inner_path_length_mismatch },
        { "8_signers_dap_sign_round_trip",    test_8_signers_dap_sign_round_trip },
    };

    int fail = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        bool ok = tests[i].fn();
        printf("[%s] %s\n", ok ? " OK " : "FAIL", tests[i].name);
        if (!ok) ++fail;
    }

    dap_common_deinit();
    return fail == 0 ? 0 : 1;
}
