#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include <dap_hash.h>
#include <dap_hash_compat.h>
#include <string.h>
#include "chipmunk/chipmunk_ring.h"
#include "chipmunk/chipmunk_ring_serialize_schema.h"

#define LOG_TAG "test_chipmunk_ring_implementation_correctness"

#define TEST_RING_SIZE 4
#define TEST_MESSAGE "Chipmunk Ring implementation correctness test message"

/**
 * @brief Test null input handling in low-level serialization wrappers.
 */
static bool s_test_signature_serialization_null_inputs(void) {
    log_it(L_INFO, "Testing null input handling for signature serialization wrappers...");

    chipmunk_ring_signature_t l_signature = {0};
    uint8_t l_buffer[16] = {0};

    dap_assert(chipmunk_ring_signature_to_bytes(NULL, l_buffer, sizeof(l_buffer)) != 0,
               "to_bytes should fail on NULL signature");
    dap_assert(chipmunk_ring_signature_from_bytes(NULL, l_buffer, sizeof(l_buffer)) != 0,
               "from_bytes should fail on NULL output signature");
    dap_assert(chipmunk_ring_signature_to_bytes(&l_signature, NULL, sizeof(l_buffer)) != 0,
               "to_bytes should fail on NULL output buffer");
    dap_assert(chipmunk_ring_signature_from_bytes(&l_signature, NULL, sizeof(l_buffer)) != 0,
               "from_bytes should fail on NULL input buffer");
    dap_assert(chipmunk_ring_signature_to_bytes(&l_signature, l_buffer, 0) != 0,
               "to_bytes should fail on zero output size");
    dap_assert(chipmunk_ring_signature_from_bytes(&l_signature, l_buffer, 0) != 0,
               "from_bytes should fail on zero input size");

    return true;
}

/**
 * @brief Test signature serialization/deserialization round-trip invariants.
 */
static bool s_test_signature_serialization_roundtrip(void) {
    log_it(L_INFO, "Testing signature serialization roundtrip correctness...");

    dap_enc_key_t *l_ring_keys[TEST_RING_SIZE] = {0};
    for (size_t i = 0; i < TEST_RING_SIZE; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
    }

    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    dap_sign_t* l_signature = dap_sign_create_ring(
        l_ring_keys[0],
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, TEST_RING_SIZE,
        1
    );
    dap_assert(l_signature != NULL, "Signature creation should succeed");

    size_t l_wire_size = 0;
    uint8_t *l_wire_signature = dap_sign_get_sign(l_signature, &l_wire_size);
    dap_assert(l_wire_signature != NULL, "Serialized wire signature pointer should be available");
    dap_assert(l_wire_size > 0, "Serialized wire signature size should be positive");
    dap_assert(l_wire_size == l_signature->header.sign_size, "Wire size should match header sign_size");

    chipmunk_ring_signature_t l_parsed_signature = {0};
    int l_parse_result = chipmunk_ring_signature_from_bytes(&l_parsed_signature, l_wire_signature, l_wire_size);
    dap_assert(l_parse_result == 0, "Deserialization should parse wire signature");
    dap_assert(l_parsed_signature.ring_size == TEST_RING_SIZE, "Parsed ring size should match source ring size");
    dap_assert(l_parsed_signature.required_signers == 1, "Parsed required_signers should match source value");

    size_t l_serialized_size = chipmunk_ring_signature_calc_size(&l_parsed_signature);
    dap_assert(l_serialized_size > 0, "Serialized signature size from schema should be positive");
    dap_assert(l_serialized_size == l_wire_size, "Serialized schema size should match wire signature size");

    uint8_t *l_reserialized = DAP_NEW_SIZE(uint8_t, l_wire_size);
    dap_assert(l_reserialized != NULL, "Roundtrip buffer allocation should succeed");

    int l_roundtrip_result = chipmunk_ring_signature_to_bytes(&l_parsed_signature, l_reserialized, l_wire_size);
    dap_assert(l_roundtrip_result == 0, "Roundtrip serialization should succeed");
    dap_assert(memcmp(l_wire_signature, l_reserialized, l_wire_size) == 0,
               "Roundtrip bytes should equal original serialized bytes");

    if (l_serialized_size > 1) {
        int l_short_buffer_result = chipmunk_ring_signature_to_bytes(&l_parsed_signature, l_reserialized, l_serialized_size - 1);
        dap_assert(l_short_buffer_result != 0, "Serialization should fail when output buffer is undersized");
        
        chipmunk_ring_signature_t l_truncated_signature = {0};
        size_t l_truncated_size = l_serialized_size / 2;
        int l_truncated_result = chipmunk_ring_signature_from_bytes(&l_truncated_signature, l_wire_signature, l_truncated_size);
        dap_assert(l_truncated_result != 0, "Deserialization should reject truncated input");
        chipmunk_ring_signature_free(&l_truncated_signature);
    }

    int l_verify_result = dap_sign_verify_ring(l_signature, &l_message_hash, sizeof(l_message_hash), l_ring_keys, TEST_RING_SIZE);
    dap_assert(l_verify_result == 0, "Original signature should verify after serialization roundtrip");

    DAP_DELETE(l_reserialized);
    chipmunk_ring_signature_free(&l_parsed_signature);
    DAP_DELETE(l_signature);

    for (size_t i = 0; i < TEST_RING_SIZE; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    return true;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    log_it(L_NOTICE, "Starting Chipmunk Ring implementation correctness tests...");

    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk Ring module");
        return -1;
    }

    bool l_all_passed = true;
    l_all_passed &= s_test_signature_serialization_null_inputs();
    l_all_passed &= s_test_signature_serialization_roundtrip();

    if (l_all_passed) {
        log_it(L_NOTICE, "All implementation correctness tests PASSED");
        return 0;
    }

    log_it(L_ERROR, "Some implementation correctness tests FAILED");
    return -1;
}
