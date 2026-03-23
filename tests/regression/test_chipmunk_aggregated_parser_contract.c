#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "dap_common.h"
#include "dap_mock.h"
#include "dap_sign.h"
#include "chipmunk_aggregation.h"
#include "../fixtures/utilities/test_helpers.h"

#define LOG_TAG "test_regression_chipmunk_aggregated_parser_contract"

static int s_mock_verify_ret = 1;
static size_t s_mock_last_message_len = 0;

DAP_MOCK_CUSTOM(int, chipmunk_verify_multi_signature,
    (const chipmunk_multi_signature_t *multi_sig, const uint8_t *message, size_t message_len))
    void *l_args[] = {
        (void *)multi_sig,
        (void *)message,
        (void *)(uintptr_t)message_len
    };
    if (!G_MOCK || !dap_mock_prepare_call(G_MOCK, l_args, 3)) {
        return DAP_MOCK_CALL_ORIGINAL(chipmunk_verify_multi_signature, multi_sig, message, message_len);
    }

    s_mock_last_message_len = message_len;
    return s_mock_verify_ret;
}

static dap_sign_t *s_make_chipmunk_aggregated_sign(uint32_t a_signers_count, uint32_t a_sign_pkey_size,
                                                   bool a_truncate_payload)
{
    size_t l_leaf_indices_size = (size_t)a_signers_count * sizeof(uint32_t);
    size_t l_roots_size = (size_t)a_signers_count * sizeof(chipmunk_hvc_poly_t);
    size_t l_proofs_size = (size_t)a_signers_count * sizeof(chipmunk_path_t);
    size_t l_payload_full_size = sizeof(uint32_t) + l_leaf_indices_size +
                                 sizeof(chipmunk_multi_signature_t) + l_roots_size + l_proofs_size;
    size_t l_payload_reported_size = a_truncate_payload && l_payload_full_size > 0
                                   ? l_payload_full_size - 1
                                   : l_payload_full_size;

    size_t l_alloc_size = sizeof(dap_sign_t) + (size_t)a_sign_pkey_size + l_payload_full_size;
    dap_sign_t *l_sign = DAP_NEW_Z_SIZE(dap_sign_t, l_alloc_size);
    if (!l_sign) {
        return NULL;
    }

    l_sign->header.type = (dap_sign_type_t){ .type = SIG_TYPE_CHIPMUNK };
    l_sign->header.hash_type = DAP_SIGN_HASH_TYPE_NONE;
    l_sign->header.sign_pkey_size = a_sign_pkey_size;
    l_sign->header.sign_size = (uint32_t)l_payload_reported_size;

    uint8_t *l_sig_data = l_sign->pkey_n_sign + a_sign_pkey_size;
    memcpy(l_sig_data, &a_signers_count, sizeof(a_signers_count));
    l_sig_data += sizeof(uint32_t);
    l_sig_data += l_leaf_indices_size; // leave zeroed leaf indices

    chipmunk_multi_signature_t l_wire = {0};
    l_wire.signer_count = a_signers_count;
    // Make signature non-zero so parser can reach mock verifier in success path.
    l_wire.aggregated_hots.sigma[0].coeffs[0] = 1;
    memcpy(l_sig_data, &l_wire, sizeof(l_wire));

    return l_sign;
}

static bool s_test_reject_nonzero_sign_pkey_size(void)
{
    log_it(L_INFO, "Testing aggregated parser reject on non-zero sign_pkey_size");

    DAP_MOCK_RESET(chipmunk_verify_multi_signature);
    s_mock_verify_ret = 1;
    s_mock_last_message_len = 0;

    dap_sign_t *l_sign = s_make_chipmunk_aggregated_sign(2, 16, false);
    DAP_TEST_ASSERT_NOT_NULL(l_sign, "Aggregated sign allocation");

    const uint8_t l_msg[] = "chipmunk-contract";
    const void *l_msgs[2] = { l_msg, l_msg };
    const size_t l_sizes[2] = { sizeof(l_msg) - 1, sizeof(l_msg) - 1 };

    int l_rc = dap_sign_verify_aggregated(l_sign, l_msgs, l_sizes, NULL, 2);
    DAP_TEST_ASSERT(l_rc != 0, "Verification must fail for non-zero sign_pkey_size");
    DAP_TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(chipmunk_verify_multi_signature) == 0,
                    "Verifier mock must not be called on sign_pkey_size contract failure");

    DAP_DELETE(l_sign);
    return true;
}

static bool s_test_reject_mixed_messages_for_multisigner(void)
{
    log_it(L_INFO, "Testing aggregated verify rejects mixed messages for multi-signer input");

    DAP_MOCK_RESET(chipmunk_verify_multi_signature);
    s_mock_verify_ret = 1;
    s_mock_last_message_len = 0;

    dap_sign_t *l_sign = s_make_chipmunk_aggregated_sign(2, 0, false);
    DAP_TEST_ASSERT_NOT_NULL(l_sign, "Aggregated sign allocation");

    const uint8_t l_msg_a[] = "message-A";
    const uint8_t l_msg_b[] = "message-B";
    const void *l_msgs[2] = { l_msg_a, l_msg_b };
    const size_t l_sizes[2] = { sizeof(l_msg_a) - 1, sizeof(l_msg_b) - 1 };

    int l_rc = dap_sign_verify_aggregated(l_sign, l_msgs, l_sizes, NULL, 2);
    DAP_TEST_ASSERT(l_rc != 0, "Verification must fail for mixed multi-signer messages");
    DAP_TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(chipmunk_verify_multi_signature) == 0,
                    "Verifier mock must not be called for mixed-message contract failure");

    DAP_DELETE(l_sign);
    return true;
}

static bool s_test_accept_identical_messages_for_multisigner(void)
{
    log_it(L_INFO, "Testing aggregated verify accepts identical messages for all signers");

    DAP_MOCK_RESET(chipmunk_verify_multi_signature);
    s_mock_verify_ret = 1;
    s_mock_last_message_len = 0;

    dap_sign_t *l_sign = s_make_chipmunk_aggregated_sign(2, 0, false);
    DAP_TEST_ASSERT_NOT_NULL(l_sign, "Aggregated sign allocation");

    const uint8_t l_msg[] = "same-message";
    const void *l_msgs[2] = { l_msg, l_msg };
    const size_t l_sizes[2] = { sizeof(l_msg) - 1, sizeof(l_msg) - 1 };

    int l_rc = dap_sign_verify_aggregated(l_sign, l_msgs, l_sizes, NULL, 2);
    DAP_TEST_ASSERT(l_rc == 0, "Verification should pass for identical messages");
    DAP_TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(chipmunk_verify_multi_signature) == 1,
                    "Verifier mock should be called once on success path");
    DAP_TEST_ASSERT(s_mock_last_message_len == sizeof(l_msg) - 1,
                    "Verifier should receive expected message length");

    DAP_DELETE(l_sign);
    return true;
}

static bool s_test_reject_truncated_payload(void)
{
    log_it(L_INFO, "Testing aggregated parser rejects truncated payload");

    DAP_MOCK_RESET(chipmunk_verify_multi_signature);
    s_mock_verify_ret = 1;
    s_mock_last_message_len = 0;

    dap_sign_t *l_sign = s_make_chipmunk_aggregated_sign(2, 0, true);
    DAP_TEST_ASSERT_NOT_NULL(l_sign, "Aggregated sign allocation");

    const uint8_t l_msg[] = "same-message";
    const void *l_msgs[2] = { l_msg, l_msg };
    const size_t l_sizes[2] = { sizeof(l_msg) - 1, sizeof(l_msg) - 1 };

    int l_rc = dap_sign_verify_aggregated(l_sign, l_msgs, l_sizes, NULL, 2);
    DAP_TEST_ASSERT(l_rc != 0, "Verification must fail for truncated payload");
    DAP_TEST_ASSERT(DAP_MOCK_GET_CALL_COUNT(chipmunk_verify_multi_signature) == 0,
                    "Verifier mock must not be called for truncated payload");

    DAP_DELETE(l_sign);
    return true;
}

int main(void)
{
    if (dap_mock_init() != 0) {
        return 1;
    }

    bool l_ok = true;
    l_ok &= s_test_reject_nonzero_sign_pkey_size();
    l_ok &= s_test_reject_mixed_messages_for_multisigner();
    l_ok &= s_test_accept_identical_messages_for_multisigner();
    l_ok &= s_test_reject_truncated_payload();

    dap_mock_deinit();

    return l_ok ? 0 : 1;
}
