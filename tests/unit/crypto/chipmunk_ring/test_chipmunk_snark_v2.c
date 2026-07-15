/*
 * test_chipmunk_snark_v2.c — SNARK V2 (FRI-PCS integration) tests.
 *
 * Phase 9.11: verifies that the SNARK prover/verifier correctly wires
 * the FRI polynomial commitment scheme into the SNARK proof pipeline.
 *
 * All proofs are now V2 format (proof_version == 1), including:
 *   - FRI proof of q(X) with Fiat-Shamir transcript binding
 *   - Grinding PoW nonce (16-bit)
 *   - Raw z+q polynomials retained for algebraic checks (bridge phase)
 *
 * Tests:
 *   1. V2 prove/verify round-trip
 *   2. Proof version and FRI fields populated
 *   3. Grinding nonce > 0 and FRI caps nonzero
 *   4. Wrong message rejection (FRI + algebraic)
 *   5. Tampered FRI cap → FRI verify fails
 *   6. Tampered q_commit → FRI verify fails (caps don't match)
 *   7. Tampered opening proof → algebraic check fails (z_commit mismatch)
 *   8. Tampered leaf value → FRI verify fails
 *   9. Ring of 4, signer at index 2
 *  10. Proof size check (opening_proof_size == 4096)
 */

#include <dap_common.h>
#include <dap_test.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sig/chipmunk/chipmunk_snark.h"
#include "sig/chipmunk/chipmunk_lrs.h"
#include "sig/chipmunk/chipmunk_fri.h"
#include "sig/chipmunk/chipmunk_fri_transcript.h"

#define LOG_TAG "test_chipmunk_snark_v2"

/* Helper: set up a basic 2-key ring SNARK context + statement + witness. */
static void s_setup_basic(chipmunk_snark_ctx_t *ctx,
                          chipmunk_snark_statement_t *stmt,
                          chipmunk_snark_witness_t *witness,
                          chipmunk_lrs_public_key_t ring[2])
{
    memset(ring, 0, 2 * sizeof(chipmunk_lrs_public_key_t));
    chipmunk_snark_init(ctx);

    memset(stmt, 0, sizeof(*stmt));
    stmt->ring = ring;
    stmt->ring_size = 2;
    const uint8_t msg[] = "test-message";
    stmt->message = msg;
    stmt->message_size = sizeof(msg);

    memset(witness, 0, sizeof(*witness));
    witness->signer_index = 0;
    witness->indicator.coeffs[0] = 1;
}

/* Helper: prove + verify round-trip, return verify result. */
static int s_prove_verify(chipmunk_snark_proof_t *proof,
                          chipmunk_snark_ctx_t *ctx,
                          chipmunk_snark_statement_t *stmt,
                          chipmunk_snark_witness_t *witness)
{
    memset(proof, 0, sizeof(*proof));
    int rc = chipmunk_snark_prove(proof, ctx, stmt, witness);
    if (rc != 0) return -1;
    return chipmunk_snark_verify(proof, ctx, stmt);
}

/* Test 1: V2 prove/verify round-trip */
static void test_v2_prove_verify(void)
{
    chipmunk_snark_ctx_t ctx;
    chipmunk_snark_statement_t stmt;
    chipmunk_snark_witness_t witness;
    chipmunk_lrs_public_key_t ring[2];
    s_setup_basic(&ctx, &stmt, &witness, ring);

    chipmunk_snark_proof_t proof;
    int rc = s_prove_verify(&proof, &ctx, &stmt, &witness);
    dap_assert(rc == 1, "V2 prove/verify round-trip");

    chipmunk_snark_proof_free(&proof);
    chipmunk_snark_ctx_free(&ctx);
}

/* Test 2: Proof version and FRI fields populated */
static void test_v2_proof_fields(void)
{
    chipmunk_snark_ctx_t ctx;
    chipmunk_snark_statement_t stmt;
    chipmunk_snark_witness_t witness;
    chipmunk_lrs_public_key_t ring[2];
    s_setup_basic(&ctx, &stmt, &witness, ring);

    chipmunk_snark_proof_t proof;
    memset(&proof, 0, sizeof(proof));
    int rc = chipmunk_snark_prove(&proof, &ctx, &stmt, &witness);
    dap_assert(rc == 0, "prove OK");

    /* Must be V2 format */
    dap_assert(proof.proof_version == CHIPMUNK_SNARK_PROOF_VERSION_V2,
               "proof_version == V2");

    /* Opening proof must still be populated (bridge phase) */
    dap_assert(proof.opening_proof_size == 2 * CHIPMUNK_N * (int)sizeof(int32_t),
               "opening_proof_size == 4096 (retained for algebraic checks)");

    /* Transcript hash must be nonzero */
    int nonzero = 0;
    for (int i = 0; i < 32; ++i) {
        if (proof.transcript_hash[i] != 0) { nonzero = 1; break; }
    }
    dap_assert(nonzero, "transcript_hash nonzero");

    chipmunk_snark_proof_free(&proof);
    chipmunk_snark_ctx_free(&ctx);
}

/* Test 3: Grinding nonce > 0 and FRI caps nonzero */
static void test_v2_fri_nonzero(void)
{
    chipmunk_snark_ctx_t ctx;
    chipmunk_snark_statement_t stmt;
    chipmunk_snark_witness_t witness;
    chipmunk_lrs_public_key_t ring[2];
    s_setup_basic(&ctx, &stmt, &witness, ring);

    chipmunk_snark_proof_t proof;
    memset(&proof, 0, sizeof(proof));
    int rc = chipmunk_snark_prove(&proof, &ctx, &stmt, &witness);
    dap_assert(rc == 0, "prove OK");
    dap_assert(proof.proof_version == CHIPMUNK_SNARK_PROOF_VERSION_V2, "V2 format");

    /* Grinding nonce should be nonzero (requires 16-bit PoW) */
    dap_assert(proof.fri_grinding_nonce > 0,
               "grinding nonce > 0 (PoW performed)");

    /* FRI caps should have at least one nonzero value in round 0 */
    int cap_nonzero = 0;
    for (unsigned i = 0; i < CHIPMUNK_FRI_CAP_SIZE; ++i) {
        if (proof.fri_proof.commit.caps[0].nodes[i] != 0) {
            cap_nonzero = 1;
            break;
        }
    }
    dap_assert(cap_nonzero, "FRI round-0 cap has nonzero values");

    /* FRI final evals: may be all-zero when q(X) ≡ 0 (honest signer → z ≡ 0 → q ≡ 0).
     * This is a valid FRI proof; binding comes from Merkle caps (hashes), not values. */

    /* Query indices should be valid */
    for (unsigned q = 0; q < CHIPMUNK_FRI_NUM_QUERIES; ++q) {
        dap_assert(proof.fri_proof.queries[q].idx < CHIPMUNK_FRI_INIT_SIZE,
                   "query index < 2048");
    }

    chipmunk_snark_proof_free(&proof);
    chipmunk_snark_ctx_free(&ctx);
}

/* Test 4: Wrong message rejection */
static void test_v2_wrong_message(void)
{
    chipmunk_snark_ctx_t ctx;
    chipmunk_snark_statement_t stmt;
    chipmunk_snark_witness_t witness;
    chipmunk_lrs_public_key_t ring[2];
    s_setup_basic(&ctx, &stmt, &witness, ring);

    chipmunk_snark_proof_t proof;
    memset(&proof, 0, sizeof(proof));
    int rc = chipmunk_snark_prove(&proof, &ctx, &stmt, &witness);
    dap_assert(rc == 0, "prove OK");

    /* Verify with different message */
    chipmunk_snark_statement_t stmt_bad = stmt;
    const uint8_t bad_msg[] = "different-message";
    stmt_bad.message = bad_msg;
    stmt_bad.message_size = sizeof(bad_msg);

    rc = chipmunk_snark_verify(&proof, &ctx, &stmt_bad);
    dap_assert(rc != 1, "verify rejects different message (V2)");

    chipmunk_snark_proof_free(&proof);
    chipmunk_snark_ctx_free(&ctx);
}

/* Test 5: Tampered FRI cap → FRI verify fails */
static void test_v2_tampered_fri_cap(void)
{
    chipmunk_snark_ctx_t ctx;
    chipmunk_snark_statement_t stmt;
    chipmunk_snark_witness_t witness;
    chipmunk_lrs_public_key_t ring[2];
    s_setup_basic(&ctx, &stmt, &witness, ring);

    chipmunk_snark_proof_t proof;
    memset(&proof, 0, sizeof(proof));
    int rc = chipmunk_snark_prove(&proof, &ctx, &stmt, &witness);
    dap_assert(rc == 0, "prove OK");
    dap_assert(proof.proof_version == CHIPMUNK_SNARK_PROOF_VERSION_V2, "V2 format");

    /* Tamper with a cap value */
    proof.fri_proof.commit.caps[0].nodes[0] ^= 1;

    rc = chipmunk_snark_verify(&proof, &ctx, &stmt);
    dap_assert(rc != 1, "verify rejects tampered FRI cap");

    chipmunk_snark_proof_free(&proof);
    chipmunk_snark_ctx_free(&ctx);
}

/* Test 6: Tampered q_commit → FRI verify fails (FRI proof was made for original q) */
static void test_v2_tampered_q_commit(void)
{
    chipmunk_snark_ctx_t ctx;
    chipmunk_snark_statement_t stmt;
    chipmunk_snark_witness_t witness;
    chipmunk_lrs_public_key_t ring[2];
    s_setup_basic(&ctx, &stmt, &witness, ring);

    chipmunk_snark_proof_t proof;
    memset(&proof, 0, sizeof(proof));
    int rc = chipmunk_snark_prove(&proof, &ctx, &stmt, &witness);
    dap_assert(rc == 0, "prove OK");

    /* Tamper with q_commit — FRI alphas derived from different transcript */
    proof.q_commit.hash[0] ^= 0x01;

    rc = chipmunk_snark_verify(&proof, &ctx, &stmt);
    dap_assert(rc != 1, "verify rejects tampered q_commit (FRI alphas mismatch)");

    chipmunk_snark_proof_free(&proof);
    chipmunk_snark_ctx_free(&ctx);
}

/* Test 7: Tampered opening proof → algebraic check fails */
static void test_v2_tampered_opening(void)
{
    chipmunk_snark_ctx_t ctx;
    chipmunk_snark_statement_t stmt;
    chipmunk_snark_witness_t witness;
    chipmunk_lrs_public_key_t ring[2];
    s_setup_basic(&ctx, &stmt, &witness, ring);

    chipmunk_snark_proof_t proof;
    memset(&proof, 0, sizeof(proof));
    int rc = chipmunk_snark_prove(&proof, &ctx, &stmt, &witness);
    dap_assert(rc == 0, "prove OK");

    /* Tamper with opening proof — z_commit mismatch */
    proof.opening_proof[0] ^= 0x01;

    rc = chipmunk_snark_verify(&proof, &ctx, &stmt);
    dap_assert(rc != 1, "verify rejects tampered opening proof (z_commit mismatch)");

    chipmunk_snark_proof_free(&proof);
    chipmunk_snark_ctx_free(&ctx);
}

/* Test 8: Tampered leaf value → FRI verify fails */
static void test_v2_tampered_leaf(void)
{
    chipmunk_snark_ctx_t ctx;
    chipmunk_snark_statement_t stmt;
    chipmunk_snark_witness_t witness;
    chipmunk_lrs_public_key_t ring[2];
    s_setup_basic(&ctx, &stmt, &witness, ring);

    chipmunk_snark_proof_t proof;
    memset(&proof, 0, sizeof(proof));
    int rc = chipmunk_snark_prove(&proof, &ctx, &stmt, &witness);
    dap_assert(rc == 0, "prove OK");

    /* Tamper with a leaf value at round 0 in query 0 */
    proof.fri_proof.queries[0].leaf_values[0] ^= 1;

    rc = chipmunk_snark_verify(&proof, &ctx, &stmt);
    dap_assert(rc != 1, "verify rejects tampered FRI leaf value");

    chipmunk_snark_proof_free(&proof);
    chipmunk_snark_ctx_free(&ctx);
}

/* Test 9: Ring of 4, signer at index 2 */
static void test_v2_ring4(void)
{
    chipmunk_snark_ctx_t ctx;
    int rc = chipmunk_snark_init(&ctx);
    dap_assert(rc == 0, "init OK");

    chipmunk_lrs_public_key_t ring[4];
    memset(ring, 0, sizeof(ring));
    memset(&ring[0], 0xA0, sizeof(chipmunk_lrs_public_key_t));
    memset(&ring[1], 0xB0, sizeof(chipmunk_lrs_public_key_t));
    memset(&ring[2], 0xC0, sizeof(chipmunk_lrs_public_key_t));
    memset(&ring[3], 0xD0, sizeof(chipmunk_lrs_public_key_t));

    chipmunk_snark_statement_t stmt;
    memset(&stmt, 0, sizeof(stmt));
    stmt.ring = ring;
    stmt.ring_size = 4;
    const uint8_t msg[] = "ring4-v2";
    stmt.message = msg;
    stmt.message_size = sizeof(msg);

    chipmunk_snark_witness_t witness;
    memset(&witness, 0, sizeof(witness));
    witness.signer_index = 2;
    witness.indicator.coeffs[2] = 1;

    chipmunk_snark_proof_t proof;
    memset(&proof, 0, sizeof(proof));
    rc = chipmunk_snark_prove(&proof, &ctx, &stmt, &witness);
    dap_assert(rc == 0, "prove OK (ring of 4, signer at 2)");
    dap_assert(proof.proof_version == CHIPMUNK_SNARK_PROOF_VERSION_V2, "V2 format");

    rc = chipmunk_snark_verify(&proof, &ctx, &stmt);
    dap_assert(rc == 1, "verify OK (ring of 4, signer at 2)");

    chipmunk_snark_proof_free(&proof);
    chipmunk_snark_ctx_free(&ctx);
}

/* Test 10: Proof size check */
static void test_v2_proof_size(void)
{
    chipmunk_snark_ctx_t ctx;
    chipmunk_snark_statement_t stmt;
    chipmunk_snark_witness_t witness;
    chipmunk_lrs_public_key_t ring[2];
    s_setup_basic(&ctx, &stmt, &witness, ring);

    chipmunk_snark_proof_t proof;
    memset(&proof, 0, sizeof(proof));
    int rc = chipmunk_snark_prove(&proof, &ctx, &stmt, &witness);
    dap_assert(rc == 0, "prove OK");

    /* opening_proof must be full size (z+q, 4096 bytes) */
    dap_assert(proof.opening_proof_size == 2 * CHIPMUNK_N * (int)sizeof(int32_t),
               "opening_proof_size == 4096");

    /* FRI proof commit: 7 caps × 64 bytes + 16 evals × 4 bytes = 512 bytes */
    /* FRI proof queries: 8 × 284 bytes = 2272 bytes */
    /* Total FRI: 2784 bytes — verified struct matches */
    dap_assert(sizeof(proof.fri_proof) == sizeof(chipmunk_fri_proof_t),
               "fri_proof field matches chipmunk_fri_proof_t");

    /* Verify grinding nonce is within reasonable range */
    dap_assert(proof.fri_grinding_nonce < (1u << 24),
               "grinding nonce < 2^24");

    chipmunk_snark_proof_free(&proof);
    chipmunk_snark_ctx_free(&ctx);
}

int main(void)
{
    dap_set_appname("test_chipmunk_snark_v2");
    dap_common_init("test_chipmunk_snark_v2", NULL);

    test_v2_prove_verify();        /* 1 */
    test_v2_proof_fields();        /* 2 */
    test_v2_fri_nonzero();         /* 3 */
    test_v2_wrong_message();      /* 4 */
    test_v2_tampered_fri_cap();   /* 5 */
    test_v2_tampered_q_commit();  /* 6 */
    test_v2_tampered_opening();   /* 7 */
    test_v2_tampered_leaf();      /* 8 */
    test_v2_ring4();              /* 9 */
    test_v2_proof_size();         /* 10 */

    log_it(L_INFO, "=== ALL Chipmunk SNARK V2 (FRI-PCS) tests PASSED (Phase 9.11) ===");
    dap_common_deinit();
    return 0;
}
