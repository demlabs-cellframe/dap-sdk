#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_chipmunk_ring.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#define LOG_TAG "test_chipmunk_ring_security_levels"

typedef struct security_level_case {
    chipmunk_ring_security_level_t level;
    const char *name;
    uint32_t ring_lwe_n;
    uint32_t ntru_n;
    uint32_t code_n;
} security_level_case_t;

static const security_level_case_t s_level_cases[] = {
    {CHIPMUNK_RING_SECURITY_LEVEL_I,      "NIST I", 512, 512, 1536},
    {CHIPMUNK_RING_SECURITY_LEVEL_III,    "NIST III", 768, 768, 2048},
    {CHIPMUNK_RING_SECURITY_LEVEL_V,      "NIST V", 1024, 1024, 2560},
    {CHIPMUNK_RING_SECURITY_LEVEL_V_PLUS, "V+", 1024, 1024, 3072},
};

/**
 * @brief Verify security level initialization and reported info
 */
static bool s_test_security_level_init_and_info(void) {
    log_it(L_INFO, "Testing security level initialization and info reporting...");

    dap_assert(dap_enc_chipmunk_ring_init_with_security_level(0) == -EINVAL,
               "Invalid security level 0 should be rejected");
    dap_assert(dap_enc_chipmunk_ring_init_with_security_level(999) == -EINVAL,
               "Invalid security level 999 should be rejected");

    for (size_t i = 0; i < sizeof(s_level_cases) / sizeof(s_level_cases[0]); i++) {
        chipmunk_ring_security_level_t level = s_level_cases[i].level;
        int init_res = dap_enc_chipmunk_ring_init_with_security_level(level);
        dap_assert(init_res == 0, "Security level init should succeed for known level");

        chipmunk_ring_security_info_t info = {0};
        dap_assert(dap_enc_chipmunk_ring_get_security_info(&info) == 0,
                   "Security info must be available after level init");
        dap_assert(info.level == level, "Reported security level should match requested level");
        dap_assert(info.classical_bits > 0, "Classical security bits should be positive");
        dap_assert(info.quantum_bits > 0, "Quantum security bits should be positive");
        dap_assert(info.logical_qubits_required > 0, "Logical qubits estimate should be positive");
        dap_assert(info.description != NULL && strlen(info.description) > 0,
                   "Security description should be set");

        log_it(L_INFO, "Security level %s verified (classical=%u, quantum=%u, qubits=%" PRIu64 ")",
               s_level_cases[i].name, info.classical_bits, info.quantum_bits, info.logical_qubits_required);
    }

    return true;
}

/**
 * @brief Verify parameter retrieval and validation by level
 */
static bool s_test_security_level_params_api(void) {
    log_it(L_INFO, "Testing security parameter APIs by level...");

    chipmunk_ring_security_info_t baseline = {0};
    dap_assert(dap_enc_chipmunk_ring_get_security_info(&baseline) == 0,
               "Must return security info before invalid parameter API checks");

    for (size_t i = 0; i < sizeof(s_level_cases) / sizeof(s_level_cases[0]); i++) {
        chipmunk_ring_pq_params_t params = {0};
        int params_res = dap_enc_chipmunk_ring_get_params_for_level(s_level_cases[i].level, &params);
        dap_assert(params_res == 0, "get_params_for_level should succeed for known level");
        dap_assert(params.ring_lwe_n == s_level_cases[i].ring_lwe_n,
                   "ring_lwe_n should match preset for level");
        dap_assert(params.ntru_n == s_level_cases[i].ntru_n,
                   "ntru_n should match preset for level");
        dap_assert(params.code_n == s_level_cases[i].code_n,
                   "code_n should match preset for level");
        dap_assert(params.ring_lwe_q > 0, "ring_lwe_q should be > 0");
        dap_assert(params.ntru_q > 0, "ntru_q should be > 0");
        dap_assert(params.code_k > 0, "code_k should be > 0");
        dap_assert(params.code_t > 0, "code_t should be > 0");
    }

    dap_assert(dap_enc_chipmunk_ring_get_params_for_level(0, NULL) == -EINVAL,
               "get_params_for_level should fail for invalid level");
    dap_assert(dap_enc_chipmunk_ring_get_params_for_level(999, NULL) == -EINVAL,
               "get_params_for_level should fail for NULL output pointer");
    dap_assert(dap_enc_chipmunk_ring_get_security_info(NULL) == -EINVAL,
               "get_security_info should fail for NULL output pointer");
    dap_assert(dap_enc_chipmunk_ring_validate_security_level(0) == -EINVAL,
               "validate_security_level should fail for invalid level");
    dap_assert(dap_enc_chipmunk_ring_get_security_info(&baseline) == 0,
               "security info should be unchanged after invalid API calls");

    chipmunk_ring_security_info_t after_invalid = {0};
    dap_assert(dap_enc_chipmunk_ring_get_security_info(&after_invalid) == 0,
               "Should be able to read security info after invalid API calls");
    dap_assert(memcmp(&baseline, &after_invalid, sizeof(chipmunk_ring_security_info_t)) == 0,
               "Invalid get_params_for_level calls should not mutate security state");

    return true;
}

/**
 * @brief Verify security level comparisons and downgrade-like transitions
 */
static bool s_test_validate_security_level_matrix(void) {
    log_it(L_INFO, "Testing security level validation matrix...");

    dap_assert(dap_enc_chipmunk_ring_init_with_security_level(CHIPMUNK_RING_SECURITY_LEVEL_III) == 0,
               "Should initialize at NIST III");

    dap_assert(dap_enc_chipmunk_ring_validate_security_level(CHIPMUNK_RING_SECURITY_LEVEL_I) == 0,
               "III config should pass at least level I");
    dap_assert(dap_enc_chipmunk_ring_validate_security_level(CHIPMUNK_RING_SECURITY_LEVEL_III) == 0,
               "III config should pass required level III");
    dap_assert(dap_enc_chipmunk_ring_validate_security_level(CHIPMUNK_RING_SECURITY_LEVEL_V) != 0,
               "III config should fail required level V");

    dap_assert(dap_enc_chipmunk_ring_init_with_security_level(CHIPMUNK_RING_SECURITY_LEVEL_V_PLUS) == 0,
               "Should initialize at level V+");
    dap_assert(dap_enc_chipmunk_ring_validate_security_level(CHIPMUNK_RING_SECURITY_LEVEL_V) == 0,
               "V+ config should pass required level V");
    dap_assert(dap_enc_chipmunk_ring_validate_security_level(CHIPMUNK_RING_SECURITY_LEVEL_V_PLUS) == 0,
               "V+ config should pass required level V+");

    return true;
}

/**
 * @brief Verify explicit downgrade behavior through parameter mutation
 */
static bool s_test_downgrade_behavior_detection(void) {
    log_it(L_INFO, "Testing explicit downgrade behavior via parameter mutation...");

    dap_assert(dap_enc_chipmunk_ring_init_with_security_level(CHIPMUNK_RING_SECURITY_LEVEL_V_PLUS) == 0,
               "Start from V+ baseline for safe reset path");

    chipmunk_ring_pq_params_t original_params = {0};
    dap_assert(dap_enc_chipmunk_ring_get_params(&original_params) == 0,
               "Must be able to retrieve current parameters");

    chipmunk_ring_pq_params_t downgraded = original_params;
    downgraded.ring_lwe_n = 520; // between I and III minima according to current formula
    downgraded.ring_lwe_q = original_params.ring_lwe_q;
    dap_assert(dap_enc_chipmunk_ring_set_params(&downgraded) == 0,
               "Setting downgraded parameters should be accepted");

    chipmunk_ring_security_info_t downgraded_info = {0};
    dap_assert(dap_enc_chipmunk_ring_get_security_info(&downgraded_info) == 0,
               "Must return downgraded security info");
    dap_assert(downgraded_info.level <= CHIPMUNK_RING_SECURITY_LEVEL_III,
               "Downgraded parameters should not keep V+ level");

    // V must fail after downgrade
    dap_assert(dap_enc_chipmunk_ring_validate_security_level(CHIPMUNK_RING_SECURITY_LEVEL_V) != 0,
               "Downgraded config should fail V validation");
    dap_assert(dap_enc_chipmunk_ring_validate_security_level(CHIPMUNK_RING_SECURITY_LEVEL_I) == 0,
               "Downgraded config should still satisfy I");
    dap_assert(dap_enc_chipmunk_ring_set_params(NULL) == -EINVAL,
               "set_params should reject NULL pointer");
    chipmunk_ring_pq_params_t zeroed = {0};
    dap_assert(dap_enc_chipmunk_ring_set_params(&zeroed) == -EINVAL,
               "set_params should reject zeroed parameters");

    // Restore baseline and re-check
    dap_assert(dap_enc_chipmunk_ring_reset_params() == 0, "Reset params should succeed");
    chipmunk_ring_security_info_t restored_info = {0};
    dap_assert(dap_enc_chipmunk_ring_get_security_info(&restored_info) == 0,
               "Security info should be available after reset");
    dap_assert(restored_info.logical_qubits_required > 0, "Logical qubits should remain positive after reset");
    dap_assert(restored_info.level >= CHIPMUNK_RING_SECURITY_LEVEL_I,
               "Reset should restore a valid security level");

    return true;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    log_it(L_NOTICE, "Starting ChipmunkRing security level tests...");

    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk Ring module");
        return -1;
    }

    bool all_passed = true;
    all_passed &= s_test_security_level_init_and_info();
    all_passed &= s_test_security_level_params_api();
    all_passed &= s_test_validate_security_level_matrix();
    all_passed &= s_test_downgrade_behavior_detection();

    log_it(L_NOTICE, "ChipmunkRing security level tests completed");
    if (all_passed) {
        log_it(L_NOTICE, "All security level tests PASSED");
        return 0;
    }

    log_it(L_ERROR, "Some security level tests FAILED");
    return -1;
}
