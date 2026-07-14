/*
 * test_chipmunk_range_proof.c — Stern-like range proof tests.
 *
 * Tests: prove/verify round-trip, boundary cases.
 * All params heap-allocated to avoid stack overflow.
 */

#include <dap_common.h>
#include <dap_enc.h>
#include <dap_test.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sig/chipmunk/chipmunk_range_proof.h"
#include "sig/chipmunk/chipmunk_pedersen.h"

#define LOG_TAG "test_chipmunk_range_proof"

static void test_prove_verify(void)
{
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    dap_assert(l_params != NULL, "params alloc OK");
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;
    int l_rc = chipmunk_pedersen_init(l_params, l_seed);
    dap_assert(l_rc == 0, "Pedersen init OK");
    dap_assert(l_params->initialized, "params initialized");

    chipmunk_pedersen_commit_t l_commit;
    uint8_t l_rand[32];
    for (int i = 0; i < 32; ++i) l_rand[i] = 0xAA;

    int64_t l_value = 1000000;
    l_rc = chipmunk_pedersen_commit(&l_commit, l_params, l_value, l_rand);
    dap_assert(l_rc == 0, "Pedersen commit OK");

    chipmunk_range_proof_t l_proof;
    memset(&l_proof, 0, sizeof(l_proof));
    log_it(L_INFO, "About to call range proof prove with value=%ld, bits=64", (long)l_value);
    l_rc = chipmunk_range_proof_prove(&l_proof, l_params, &l_commit,
                                       l_value, l_rand, 64);
    log_it(L_INFO, "Range proof prove returned: %d", l_rc);
    dap_assert(l_rc == 0, "range proof prove OK");

    l_rc = chipmunk_range_proof_verify(&l_proof, l_params, &l_commit);
    dap_assert(l_rc == 1, "range proof verify OK");

    chipmunk_range_proof_free(&l_proof);
    DAP_DELETE(l_params);
}

static void test_zero_value(void)
{
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;
    chipmunk_pedersen_init(l_params, l_seed);

    chipmunk_pedersen_commit_t l_commit;
    uint8_t l_rand[32];
    for (int i = 0; i < 32; ++i) l_rand[i] = 0xAA;
    chipmunk_pedersen_commit(&l_commit, l_params, 0, l_rand);

    chipmunk_range_proof_t l_proof;
    memset(&l_proof, 0, sizeof(l_proof));
    int l_rc = chipmunk_range_proof_prove(&l_proof, l_params, &l_commit,
                                           0, l_rand, 64);
    dap_assert(l_rc == 0, "zero value prove OK");

    l_rc = chipmunk_range_proof_verify(&l_proof, l_params, &l_commit);
    dap_assert(l_rc == 1, "zero value verify OK");

    chipmunk_range_proof_free(&l_proof);
    DAP_DELETE(l_params);
}

static void test_proof_nonzero(void)
{
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;
    chipmunk_pedersen_init(l_params, l_seed);

    chipmunk_pedersen_commit_t l_commit;
    uint8_t l_rand[32];
    for (int i = 0; i < 32; ++i) l_rand[i] = 0xAA;
    chipmunk_pedersen_commit(&l_commit, l_params, 100, l_rand);

    chipmunk_range_proof_t l_proof;
    memset(&l_proof, 0, sizeof(l_proof));
    chipmunk_range_proof_prove(&l_proof, l_params, &l_commit, 100, l_rand, 64);

    int l_nonzero = 0;
    for (int i = 0; i < 32; ++i) {
        if (l_proof.transcript_hash[i] != 0) { l_nonzero = 1; break; }
    }
    dap_assert(l_nonzero, "proof transcript hash non-zero");

    chipmunk_range_proof_free(&l_proof);
    DAP_DELETE(l_params);
}

int main(void)
{
    dap_set_appname("test_chipmunk_range_proof");
    dap_common_init("test_chipmunk_range_proof", NULL);
    /* Initialise crypto subsystem (SIMD dispatch, chipmunk, etc.) */
    dap_enc_init();

    test_prove_verify();
    test_zero_value();
    test_proof_nonzero();

    log_it(L_INFO, "=== ALL Chipmunk Range Proof tests PASSED ===");
    dap_common_deinit();
    return 0;
}
