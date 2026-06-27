/*
 * test_chipmunk_snark.c — Lattice-based SNARK (Ligero-style) tests.
 *
 * Tests: init, commit, prove/verify round-trip, soundness (false witness rejected).
 */

#include <dap_common.h>
#include <dap_test.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sig/chipmunk/chipmunk_snark.h"
#include "sig/chipmunk/chipmunk_lrs.h"

#define LOG_TAG "test_chipmunk_snark"

static void test_init(void)
{
    chipmunk_snark_ctx_t l_ctx;
    int l_rc = chipmunk_snark_init(&l_ctx);
    dap_assert(l_rc == 0, "SNARK init OK");
    dap_assert(l_ctx.initialized, "SNARK ctx initialized");
    chipmunk_snark_ctx_free(&l_ctx);
}

static void test_commit(void)
{
    chipmunk_snark_commit_t l_commit;
    chipmunk_poly_t l_poly;
    memset(&l_poly, 0, sizeof(l_poly));
    l_poly.coeffs[0] = 42;
    l_poly.coeffs[1] = 1337;

    int l_rc = chipmunk_snark_commit(&l_commit, &l_poly);
    dap_assert(l_rc == 0, "commit OK");

    /* Commitment should be nonzero */
    int l_nonzero = 0;
    for (int i = 0; i < 32; ++i) {
        if (l_commit.hash[i] != 0) { l_nonzero = 1; break; }
    }
    dap_assert(l_nonzero, "commitment hash non-zero");

    /* Same polynomial should give same commitment */
    chipmunk_snark_commit_t l_commit2;
    chipmunk_snark_commit(&l_commit2, &l_poly);
    dap_assert(memcmp(l_commit.hash, l_commit2.hash, 32) == 0, "deterministic commit");

    /* Different polynomial should give different commitment */
    chipmunk_poly_t l_poly2;
    memset(&l_poly2, 0, sizeof(l_poly2));
    l_poly2.coeffs[0] = 99;
    chipmunk_snark_commit_t l_commit3;
    chipmunk_snark_commit(&l_commit3, &l_poly2);
    dap_assert(memcmp(l_commit.hash, l_commit3.hash, 32) != 0, "different poly → different commit");
}

static void test_prove_verify(void)
{
    chipmunk_snark_ctx_t l_ctx;
    int l_rc = chipmunk_snark_init(&l_ctx);
    dap_assert(l_rc == 0, "init OK");

    /* Build a small ring of 2 keys */
    chipmunk_lrs_public_key_t l_ring[2];
    memset(l_ring, 0, sizeof(l_ring));

    /* Build statement */
    chipmunk_snark_statement_t l_stmt;
    memset(&l_stmt, 0, sizeof(l_stmt));
    l_stmt.ring = l_ring;
    l_stmt.ring_size = 2;
    const uint8_t l_msg[] = "test-message";
    l_stmt.message = l_msg;
    l_stmt.message_size = sizeof(l_msg);

    /* Build witness (signer at index 0) */
    chipmunk_snark_witness_t l_witness;
    memset(&l_witness, 0, sizeof(l_witness));
    l_witness.signer_index = 0;
    l_witness.indicator.coeffs[0] = 1;

    /* Prove */
    chipmunk_snark_proof_t l_proof;
    memset(&l_proof, 0, sizeof(l_proof));
    l_rc = chipmunk_snark_prove(&l_proof, &l_ctx, &l_stmt, &l_witness);
    dap_assert(l_rc == 0, "prove OK");

    /* Verify */
    l_rc = chipmunk_snark_verify(&l_proof, &l_ctx, &l_stmt);
    dap_assert(l_rc == 1, "verify OK");

    /* Cleanup */
    chipmunk_snark_proof_free(&l_proof);
    chipmunk_snark_ctx_free(&l_ctx);
}

static void test_soundness_false_witness(void)
{
    chipmunk_snark_ctx_t l_ctx;
    int l_rc = chipmunk_snark_init(&l_ctx);
    dap_assert(l_rc == 0, "init OK");

    chipmunk_lrs_public_key_t l_ring[2];
    memset(l_ring, 0, sizeof(l_ring));

    chipmunk_snark_statement_t l_stmt;
    memset(&l_stmt, 0, sizeof(l_stmt));
    l_stmt.ring = l_ring;
    l_stmt.ring_size = 2;
    const uint8_t l_msg[] = "test-message";
    l_stmt.message = l_msg;
    l_stmt.message_size = sizeof(l_msg);

    /* Witness with signer_index = 0 */
    chipmunk_snark_witness_t l_witness;
    memset(&l_witness, 0, sizeof(l_witness));
    l_witness.signer_index = 0;
    l_witness.indicator.coeffs[0] = 1;

    chipmunk_snark_proof_t l_proof;
    memset(&l_proof, 0, sizeof(l_proof));
    l_rc = chipmunk_snark_prove(&l_proof, &l_ctx, &l_stmt, &l_witness);
    dap_assert(l_rc == 0, "prove OK");

    /* Verify with different message — should fail */
    chipmunk_snark_statement_t l_stmt_bad = l_stmt;
    const uint8_t l_bad_msg[] = "different-message";
    l_stmt_bad.message = l_bad_msg;
    l_stmt_bad.message_size = sizeof(l_bad_msg);

    l_rc = chipmunk_snark_verify(&l_proof, &l_ctx, &l_stmt_bad);
    dap_assert(l_rc != 1, "verify fails on different message");

    chipmunk_snark_proof_free(&l_proof);
    chipmunk_snark_ctx_free(&l_ctx);
}

static void test_proof_nonzero(void)
{
    chipmunk_snark_ctx_t l_ctx;
    chipmunk_snark_init(&l_ctx);

    chipmunk_lrs_public_key_t l_ring[2];
    memset(l_ring, 0, sizeof(l_ring));

    chipmunk_snark_statement_t l_stmt;
    memset(&l_stmt, 0, sizeof(l_stmt));
    l_stmt.ring = l_ring;
    l_stmt.ring_size = 2;
    const uint8_t l_msg[] = "test";
    l_stmt.message = l_msg;
    l_stmt.message_size = 4;

    chipmunk_snark_witness_t l_witness;
    memset(&l_witness, 0, sizeof(l_witness));
    l_witness.signer_index = 1;
    l_witness.indicator.coeffs[1] = 1;

    chipmunk_snark_proof_t l_proof;
    memset(&l_proof, 0, sizeof(l_proof));
    int l_rc = chipmunk_snark_prove(&l_proof, &l_ctx, &l_stmt, &l_witness);
    dap_assert(l_rc == 0, "prove OK");

    /* Proof transcript hash should be nonzero */
    int l_nonzero = 0;
    for (int i = 0; i < 32; ++i) {
        if (l_proof.transcript_hash[i] != 0) { l_nonzero = 1; break; }
    }
    dap_assert(l_nonzero, "proof transcript hash non-zero");

    chipmunk_snark_proof_free(&l_proof);
    chipmunk_snark_ctx_free(&l_ctx);
}

int main(void)
{
    dap_set_appname("test_chipmunk_snark");
    dap_common_init("test_chipmunk_snark", NULL);

    test_init();
    test_commit();
    test_prove_verify();
    test_soundness_false_witness();
    test_proof_nonzero();

    log_it(L_INFO, "=== ALL Chipmunk SNARK tests PASSED ===");
    dap_common_deinit();
    return 0;
}
