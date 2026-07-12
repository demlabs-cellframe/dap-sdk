/*
 * test_chipmunk_range_proof.c — Stern-like range proof tests.
 */

#include <dap_common.h>
#include <dap_test.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sig/chipmunk/chipmunk_range_proof.h"
#include "sig/chipmunk/chipmunk_pedersen.h"

#define LOG_TAG "test_chipmunk_range_proof"

static void s_u64_to_amount_bytes(uint64_t a_val, uint8_t a_out[CHIPMUNK_PEDERSEN_VALUE_BYTES])
{
    memset(a_out, 0, CHIPMUNK_PEDERSEN_VALUE_BYTES);
    memcpy(a_out, &a_val, sizeof(a_val));
}

static void test_prove_verify(void)
{
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    dap_assert(l_params != NULL, "params alloc OK");
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;
    int l_rc = chipmunk_pedersen_init(l_params, l_seed);
    dap_assert(l_rc == 0, "Pedersen init OK");

    chipmunk_pedersen_commit_t l_commit;
    uint8_t l_rand[32], l_value[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    for (int i = 0; i < 32; ++i) l_rand[i] = 0xAA;
    s_u64_to_amount_bytes(1000000, l_value);

    l_rc = chipmunk_pedersen_commit(&l_commit, l_params, l_value, l_rand);
    dap_assert(l_rc == 0, "Pedersen commit OK");

    chipmunk_range_proof_t l_proof;
    memset(&l_proof, 0, sizeof(l_proof));
    l_rc = chipmunk_range_proof_prove(&l_proof, l_params, &l_commit, l_value, l_rand);
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
    uint8_t l_rand[32], l_zero[CHIPMUNK_PEDERSEN_VALUE_BYTES] = {0};
    for (int i = 0; i < 32; ++i) l_rand[i] = 0xAA;
    chipmunk_pedersen_commit(&l_commit, l_params, l_zero, l_rand);

    chipmunk_range_proof_t l_proof;
    memset(&l_proof, 0, sizeof(l_proof));
    int l_rc = chipmunk_range_proof_prove(&l_proof, l_params, &l_commit, l_zero, l_rand);
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
    uint8_t l_rand[32], l_value[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    for (int i = 0; i < 32; ++i) l_rand[i] = 0xAA;
    s_u64_to_amount_bytes(100, l_value);
    chipmunk_pedersen_commit(&l_commit, l_params, l_value, l_rand);

    chipmunk_range_proof_t l_proof;
    memset(&l_proof, 0, sizeof(l_proof));
    chipmunk_range_proof_prove(&l_proof, l_params, &l_commit, l_value, l_rand);

    int l_nonzero = 0;
    for (int i = 0; i < 32; ++i) {
        if (l_proof.transcript_hash[i] != 0) { l_nonzero = 1; break; }
    }
    dap_assert(l_nonzero, "proof transcript hash non-zero");

    chipmunk_range_proof_free(&l_proof);
    DAP_DELETE(l_params);
}

static void test_uint256_large_amount(void)
{
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x11 + i;
    chipmunk_pedersen_init(l_params, l_seed);

    uint8_t l_value[32] = {
        0x00, 0x00, 0x50, 0xef, 0xe2, 0xd6, 0xe4, 0x1a, 0x1b,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    chipmunk_pedersen_commit_t l_commit;
    uint8_t l_rand[32];
    for (int i = 0; i < 32; ++i) l_rand[i] = 0x55 + i;
    int l_rc = chipmunk_pedersen_commit(&l_commit, l_params, l_value, l_rand);
    dap_assert(l_rc == 0, "uint256 Pedersen commit OK");

    chipmunk_range_proof_t l_proof;
    memset(&l_proof, 0, sizeof(l_proof));
    l_rc = chipmunk_range_proof_prove(&l_proof, l_params, &l_commit, l_value, l_rand);
    dap_assert(l_rc == 0, "uint256 range proof prove OK");

    l_rc = chipmunk_range_proof_verify(&l_proof, l_params, &l_commit);
    dap_assert(l_rc == 1, "uint256 range proof verify OK");

    chipmunk_range_proof_free(&l_proof);
    DAP_DELETE(l_params);
}

int main(void)
{
    dap_set_appname("test_chipmunk_range_proof");
    dap_common_init("test_chipmunk_range_proof", NULL);

    test_prove_verify();
    test_zero_value();
    test_proof_nonzero();
    test_uint256_large_amount();

    log_it(L_INFO, "=== ALL Chipmunk Range Proof tests PASSED ===");
    dap_common_deinit();
    return 0;
}
