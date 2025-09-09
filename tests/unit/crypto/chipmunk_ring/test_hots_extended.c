#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_hash.h>
#include <dap_math_mod.h>
#include "../../../fixtures/utilities/test_helpers.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_hots.h"
#include "chipmunk/chipmunk_poly.h"
#include "chipmunk/chipmunk_ntt.h"
#include "chipmunk/chipmunk_hash.h"

#define LOG_TAG "test_hots_extended"

// Test constants
#define TEST_MESSAGE "Test message for HOTS extended verification"

/**
 * @brief Test HOTS equation components separately
 */
static bool s_test_hots_equation_components(void) {
    log_it(L_INFO, "Testing HOTS equation components separately...");

    // Generate test key pair
    uint8_t l_private_key[CHIPMUNK_PRIVATE_KEY_SIZE];
    uint8_t l_public_key[CHIPMUNK_PUBLIC_KEY_SIZE];
    
    int l_keygen_result = chipmunk_keypair(l_public_key, CHIPMUNK_PUBLIC_KEY_SIZE,
                                          l_private_key, CHIPMUNK_PRIVATE_KEY_SIZE);
    dap_assert(l_keygen_result == CHIPMUNK_ERROR_SUCCESS, "Key generation should succeed");
    
    // Parse keys
    chipmunk_private_key_t l_sk = {0};
    chipmunk_public_key_t l_pk = {0};
    
    dap_assert(chipmunk_private_key_from_bytes(&l_sk, l_private_key) == 0, "Private key parsing should succeed");
    dap_assert(chipmunk_public_key_from_bytes(&l_pk, l_public_key) == 0, "Public key parsing should succeed");
    
    // Generate HOTS parameters - CRITICAL: same method as in verification
    chipmunk_hots_params_t l_params = {0};
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        dap_assert(dap_chipmunk_hash_sample_matrix(l_params.a[i].coeffs, l_pk.rho_seed, i) == 0, 
                  "Parameter generation should succeed");
        chipmunk_ntt(l_params.a[i].coeffs);
    }
    
    // Create signature
    uint8_t l_signature[CHIPMUNK_SIGNATURE_SIZE];
    int l_sign_result = chipmunk_sign(l_private_key, (uint8_t*)TEST_MESSAGE, strlen(TEST_MESSAGE), l_signature);
    dap_assert(l_sign_result == CHIPMUNK_ERROR_SUCCESS, "Signing should succeed");
    
    // Parse signature
    chipmunk_signature_t l_sig = {0};
    dap_assert(chipmunk_signature_from_bytes(&l_sig, l_signature) == 0, "Signature parsing should succeed");
    
    // Test equation components manually
    log_it(L_INFO, "=== MANUAL HOTS EQUATION VERIFICATION ===");
    
    // 1. Hash message to polynomial
    chipmunk_poly_t l_hm;
    dap_assert(chipmunk_poly_from_hash(&l_hm, (uint8_t*)TEST_MESSAGE, strlen(TEST_MESSAGE)) == 0,
              "Message hashing should succeed");
    
    log_it(L_INFO, "H(m) first coeffs: %d %d %d %d", 
           l_hm.coeffs[0], l_hm.coeffs[1], l_hm.coeffs[2], l_hm.coeffs[3]);
    
    // 2. Transform to NTT domain
    chipmunk_poly_t l_hm_ntt = l_hm;
    chipmunk_ntt(l_hm_ntt.coeffs);
    
    // 3. Compute left side: Σ(a_i * σ_i) in NTT domain
    chipmunk_poly_t l_left_ntt;
    memset(&l_left_ntt, 0, sizeof(l_left_ntt));
    
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        chipmunk_poly_t l_sigma_ntt = l_sig.sigma[i];
        chipmunk_ntt(l_sigma_ntt.coeffs);
        
        chipmunk_poly_t l_term;
        chipmunk_poly_mul_ntt(&l_term, &l_params.a[i], &l_sigma_ntt);
        
        for (int j = 0; j < CHIPMUNK_N; j++) {
            l_left_ntt.coeffs[j] = (l_left_ntt.coeffs[j] + l_term.coeffs[j]) % CHIPMUNK_Q;
        }
        
        log_it(L_INFO, "After a[%d] * σ[%d]: left_sum[0-3] = %d %d %d %d", i, i,
               l_left_ntt.coeffs[0], l_left_ntt.coeffs[1], l_left_ntt.coeffs[2], l_left_ntt.coeffs[3]);
    }
    
    // 4. Compute right side: H(m) * v0 + v1 in NTT domain
    chipmunk_poly_t l_v0_ntt = l_pk.v0;
    chipmunk_poly_t l_v1_ntt = l_pk.v1;
    chipmunk_ntt(l_v0_ntt.coeffs);
    chipmunk_ntt(l_v1_ntt.coeffs);
    
    chipmunk_poly_t l_hm_v0;
    chipmunk_poly_mul_ntt(&l_hm_v0, &l_hm_ntt, &l_v0_ntt);
    
    chipmunk_poly_t l_right_ntt;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        l_right_ntt.coeffs[i] = (l_hm_v0.coeffs[i] + l_v1_ntt.coeffs[i]) % CHIPMUNK_Q;
    }
    
    log_it(L_INFO, "Right side NTT: %d %d %d %d", 
           l_right_ntt.coeffs[0], l_right_ntt.coeffs[1], l_right_ntt.coeffs[2], l_right_ntt.coeffs[3]);
    
    // 5. Convert to time domain and compare
    chipmunk_poly_t l_left_time = l_left_ntt;
    chipmunk_poly_t l_right_time = l_right_ntt;
    chipmunk_invntt(l_left_time.coeffs);
    chipmunk_invntt(l_right_time.coeffs);
    
    log_it(L_INFO, "Left side time:  %d %d %d %d", 
           l_left_time.coeffs[0], l_left_time.coeffs[1], l_left_time.coeffs[2], l_left_time.coeffs[3]);
    log_it(L_INFO, "Right side time: %d %d %d %d", 
           l_right_time.coeffs[0], l_right_time.coeffs[1], l_right_time.coeffs[2], l_right_time.coeffs[3]);
    
    // Check if equations match
    bool l_equal = true;
    int l_diff_count = 0;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (l_left_time.coeffs[i] != l_right_time.coeffs[i]) {
            l_equal = false;
            l_diff_count++;
            if (l_diff_count <= 5) {
                log_it(L_INFO, "Diff[%d]: %d != %d (delta: %d)", i,
                       l_left_time.coeffs[i], l_right_time.coeffs[i],
                       l_left_time.coeffs[i] - l_right_time.coeffs[i]);
            }
        }
    }
    
    log_it(L_INFO, "Manual verification result: %s (%d/%d coeffs differ)", 
           l_equal ? "PASS" : "FAIL", l_diff_count, CHIPMUNK_N);
    
    // Now test with original chipmunk_verify function
    int l_verify_result = chipmunk_verify(l_public_key, (uint8_t*)TEST_MESSAGE, strlen(TEST_MESSAGE), l_signature);
    log_it(L_INFO, "Original chipmunk_verify result: %d", l_verify_result);
    
    dap_assert(l_equal == (l_verify_result == CHIPMUNK_ERROR_SUCCESS), 
              "Manual and original verification should match");
    
    return l_equal;
}

/**
 * @brief Test HOTS with ring signature context data
 */
static bool s_test_hots_with_ring_context(void) {
    log_it(L_INFO, "Testing HOTS with ring signature context data...");
    
    // Generate keys similar to ring signature context
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
    dap_assert(l_signer_key != NULL, "Signer key generation should succeed");
    
    // Create a challenge similar to ring signature challenge
    dap_hash_fast_t l_challenge_hash;
    dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_challenge_hash);
    
    uint8_t l_challenge[32];
    memcpy(l_challenge, &l_challenge_hash, 32);
    
    log_it(L_INFO, "Generated challenge: %02x%02x%02x%02x %02x%02x%02x%02x",
           l_challenge[0], l_challenge[1], l_challenge[2], l_challenge[3],
           l_challenge[4], l_challenge[5], l_challenge[6], l_challenge[7]);
    
    // Create signature with challenge (similar to ring signature)
    uint8_t l_signature[CHIPMUNK_SIGNATURE_SIZE];
    int l_sign_result = chipmunk_sign(l_signer_key->priv_key_data, l_challenge, 32, l_signature);
    dap_assert(l_sign_result == CHIPMUNK_ERROR_SUCCESS, "Challenge signing should succeed");
    
    // Verify signature with challenge
    int l_verify_result = chipmunk_verify(l_signer_key->pub_key_data, l_challenge, 32, l_signature);
    log_it(L_INFO, "Challenge verification result: %d", l_verify_result);
    
    dap_enc_key_delete(l_signer_key);
    
    dap_assert(l_verify_result == CHIPMUNK_ERROR_SUCCESS, "Challenge verification should succeed");
    return l_verify_result == CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Test parameter consistency between signing and verification
 */
static bool s_test_parameter_consistency(void) {
    log_it(L_INFO, "Testing parameter consistency between signing and verification...");
    
    // Generate test key
    uint8_t l_private_key[CHIPMUNK_PRIVATE_KEY_SIZE];
    uint8_t l_public_key[CHIPMUNK_PUBLIC_KEY_SIZE];
    
    int l_keygen_result = chipmunk_keypair(l_public_key, CHIPMUNK_PUBLIC_KEY_SIZE,
                                          l_private_key, CHIPMUNK_PRIVATE_KEY_SIZE);
    dap_assert(l_keygen_result == CHIPMUNK_ERROR_SUCCESS, "Key generation should succeed");
    
    // Parse keys
    chipmunk_private_key_t l_sk_sign = {0};
    chipmunk_public_key_t l_pk_verify = {0};
    
    dap_assert(chipmunk_private_key_from_bytes(&l_sk_sign, l_private_key) == 0, "Private key parsing should succeed");
    dap_assert(chipmunk_public_key_from_bytes(&l_pk_verify, l_public_key) == 0, "Public key parsing should succeed");
    
    // Check if public key from private key matches standalone public key
    log_it(L_INFO, "Comparing public keys from different sources...");
    
    // Public key from private key
    uint8_t l_pk_from_private[CHIPMUNK_PUBLIC_KEY_SIZE];
    chipmunk_public_key_to_bytes(l_pk_from_private, &l_sk_sign.pk);
    
    // Compare with original public key
    bool l_keys_match = (memcmp(l_pk_from_private, l_public_key, CHIPMUNK_PUBLIC_KEY_SIZE) == 0);
    log_it(L_INFO, "Public key consistency: %s", l_keys_match ? "MATCH" : "MISMATCH");
    
    // Detailed comparison analysis
    log_it(L_INFO, "=== DETAILED KEY COMPARISON ===");
    log_it(L_INFO, "Key sizes: from_private=%d, standalone=%d, expected=%d", 
           CHIPMUNK_PUBLIC_KEY_SIZE, CHIPMUNK_PUBLIC_KEY_SIZE, CHIPMUNK_PUBLIC_KEY_SIZE);
    
    // Find exact difference location
    int l_first_diff = -1;
    for (int i = 0; i < CHIPMUNK_PUBLIC_KEY_SIZE; i++) {
        if (l_pk_from_private[i] != l_public_key[i]) {
            l_first_diff = i;
            break;
        }
    }
    
    if (l_first_diff >= 0) {
        log_it(L_INFO, "First difference at byte %d: %02x != %02x", 
               l_first_diff, l_pk_from_private[l_first_diff], l_public_key[l_first_diff]);
        
        // Determine which section differs
        if (l_first_diff < 32) {
            log_it(L_INFO, "Difference in rho_seed section (bytes 0-31)");
        } else if (l_first_diff < 32 + CHIPMUNK_N * 4) {
            int coeff_index = (l_first_diff - 32) / 4;
            log_it(L_INFO, "Difference in v0 polynomial, coefficient %d", coeff_index);
        } else {
            int coeff_index = (l_first_diff - 32 - CHIPMUNK_N * 4) / 4;
            log_it(L_INFO, "Difference in v1 polynomial, coefficient %d", coeff_index);
        }
    } else {
        log_it(L_INFO, "All %d bytes are identical - this should not happen with MISMATCH!", CHIPMUNK_PUBLIC_KEY_SIZE);
    }
    
    log_it(L_INFO, "From private: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x", 
           l_pk_from_private[0], l_pk_from_private[1], l_pk_from_private[2], l_pk_from_private[3],
           l_pk_from_private[4], l_pk_from_private[5], l_pk_from_private[6], l_pk_from_private[7],
           l_pk_from_private[8], l_pk_from_private[9], l_pk_from_private[10], l_pk_from_private[11],
           l_pk_from_private[12], l_pk_from_private[13], l_pk_from_private[14], l_pk_from_private[15]);
    log_it(L_INFO, "Standalone:   %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x", 
           l_public_key[0], l_public_key[1], l_public_key[2], l_public_key[3],
           l_public_key[4], l_public_key[5], l_public_key[6], l_public_key[7],
           l_public_key[8], l_public_key[9], l_public_key[10], l_public_key[11],
           l_public_key[12], l_public_key[13], l_public_key[14], l_public_key[15]);
    
    dap_assert(l_keys_match, "Public keys should be consistent");
    return l_keys_match;
}

/**
 * @brief Main test function
 */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    // Initialize test environment
    if (dap_test_sdk_init() != 0) {
        log_it(L_CRITICAL, "Failed to initialize DAP SDK test environment");
        return -1;
    }
    
    log_it(L_INFO, "🧪 EXTENDED HOTS VERIFICATION TESTS");
    log_it(L_INFO, "Analyzing HOTS equation components for ring signature context");
    
    bool l_all_passed = true;
    
    // Test 1: Equation components
    if (!s_test_hots_equation_components()) {
        log_it(L_ERROR, "❌ HOTS equation components test FAILED");
        l_all_passed = false;
    } else {
        log_it(L_INFO, "✅ HOTS equation components test PASSED");
    }
    
    // Test 2: Ring context simulation
    if (!s_test_hots_with_ring_context()) {
        log_it(L_ERROR, "❌ HOTS ring context test FAILED");
        l_all_passed = false;
    } else {
        log_it(L_INFO, "✅ HOTS ring context test PASSED");
    }
    
    // Test 3: Parameter consistency
    if (!s_test_parameter_consistency()) {
        log_it(L_ERROR, "❌ Parameter consistency test FAILED");
        l_all_passed = false;
    } else {
        log_it(L_INFO, "✅ Parameter consistency test PASSED");
    }
    
    // Final result
    if (l_all_passed) {
        log_it(L_INFO, "🎉 ALL EXTENDED HOTS TESTS PASSED!");
    } else {
        log_it(L_ERROR, "❌ SOME EXTENDED HOTS TESTS FAILED!");
    }
    
    dap_test_sdk_cleanup();
    return l_all_passed ? 0 : -1;
}
