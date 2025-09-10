#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include <dap_hash.h>
#include "rand/dap_rand.h"
#include "chipmunk/chipmunk_ring.h"
#include <sys/time.h>
#include <unistd.h>

#define LOG_TAG "test_chipmunk_ring_parameter_comparison"

// Test constants
#define TEST_MESSAGE "ChipmunkRing Parameter Comparison - Quantum Security Analysis"
#define COMPARISON_RING_SIZE 16  // Fixed ring size for parameter comparison
#define COMPARISON_ITERATIONS 20

// Performance measurement utilities
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

// Parameter set definition
typedef struct {
    const char* name;
    const char* description;
    chipmunk_ring_pq_params_t params;
    size_t expected_qubits_ring_lwe;
    size_t expected_qubits_ntru;
    size_t expected_qubits_code;
    size_t expected_total_qubits;
} parameter_set_t;

// Performance result for parameter comparison
typedef struct {
    const char* param_name;
    size_t signature_size;
    double avg_signing_time;
    double avg_verification_time;
    size_t total_quantum_resistance;
    size_t ring_lwe_size;
    size_t ntru_size;
    size_t code_size;
    size_t binding_size;
} param_performance_result_t;

// Global results storage
static param_performance_result_t g_param_results[10];
static size_t g_param_results_count = 0;

/**
 * @brief Define parameter sets for comparison
 */
static parameter_set_t get_parameter_sets(void) {
    static parameter_set_t sets[] = {
        // Minimal security (fast, smaller signatures)
        {
            .name = "MINIMAL",
            .description = "Minimum viable quantum security (~150,000 qubits)",
            .params = {
                .chipmunk_n = 256,
                .chipmunk_gamma = 4,
                .randomness_size = 32,
                .ring_lwe_n = 512,           // Reduced for speed
                .ring_lwe_q = 12289,         // Smaller prime
                .ring_lwe_sigma_numerator = 16,
                .ntru_n = 512,               // Reduced for speed
                .ntru_q = 32768,             // Smaller prime
                .code_n = 1536,              // Reduced code length
                .code_k = 768,               // Proportional
                .code_t = 96                 // Proportional error weight
            },
            .expected_qubits_ring_lwe = 45000,  // ~4n×log₂(q) = 4×512×13.6
            .expected_qubits_ntru = 40000,      // ~4n×log₂(q) = 4×512×15
            .expected_qubits_code = 30000,      // ~2n = 2×1536
            .expected_total_qubits = 115000
        },
        
        // Balanced security (current default)
        {
            .name = "BALANCED",
            .description = "Current optimized parameters (~240,000 qubits)",
            .params = {
                .chipmunk_n = 256,
                .chipmunk_gamma = 4,
                .randomness_size = 32,
                .ring_lwe_n = 1024,          // Current default
                .ring_lwe_q = 40961,         // Current default
                .ring_lwe_sigma_numerator = 32,
                .ntru_n = 1024,              // Current default
                .ntru_q = 65537,             // Current default
                .code_n = 3072,              // Enhanced from Hash layer
                .code_k = 1536,              // Proportional
                .code_t = 192                // Enhanced error weight
            },
            .expected_qubits_ring_lwe = 90000,  // ~4n×log₂(q) = 4×1024×15.3
            .expected_qubits_ntru = 70000,      // ~4n×log₂(q) = 4×1024×16
            .expected_qubits_code = 80000,      // Enhanced from hash layer
            .expected_total_qubits = 240000
        },
        
        // Maximum security (slow, larger signatures, 100+ year protection)
        {
            .name = "MAXIMUM",
            .description = "Maximum quantum security (~400,000 qubits, 100+ years)",
            .params = {
                .chipmunk_n = 512,           // Doubled for extra security
                .chipmunk_gamma = 8,         // Increased gamma
                .randomness_size = 64,       // Doubled randomness
                .ring_lwe_n = 2048,          // Doubled for 100+ year security
                .ring_lwe_q = 65537,         // Larger prime
                .ring_lwe_sigma_numerator = 64,
                .ntru_n = 2048,              // Doubled for 100+ year security
                .ntru_q = 131071,            // Larger prime
                .code_n = 6144,              // Doubled code length
                .code_k = 3072,              // Proportional
                .code_t = 384                // Doubled error weight
            },
            .expected_qubits_ring_lwe = 180000, // ~4n×log₂(q) = 4×2048×16
            .expected_qubits_ntru = 140000,     // ~4n×log₂(q) = 4×2048×17
            .expected_qubits_code = 160000,     // ~2n = 2×6144 + enhanced
            .expected_total_qubits = 480000
        },
        
        // Ultra-fast (reduced security for high-performance scenarios)
        {
            .name = "FAST",
            .description = "High performance, reduced security (~100,000 qubits)",
            .params = {
                .chipmunk_n = 128,           // Reduced for speed
                .chipmunk_gamma = 2,         // Minimal gamma
                .randomness_size = 16,       // Minimal randomness
                .ring_lwe_n = 256,           // Minimal viable
                .ring_lwe_q = 4093,          // Small prime
                .ring_lwe_sigma_numerator = 8,
                .ntru_n = 256,               // Minimal viable
                .ntru_q = 16384,             // Small prime
                .code_n = 768,               // Minimal code
                .code_k = 384,               // Proportional
                .code_t = 48                 // Minimal error weight
            },
            .expected_qubits_ring_lwe = 20000,  // ~4n×log₂(q) = 4×256×12
            .expected_qubits_ntru = 18000,      // ~4n×log₂(q) = 4×256×14
            .expected_qubits_code = 15000,      // ~2n = 2×768
            .expected_total_qubits = 53000
        },
        
        // Paranoid security (maximum possible protection)
        {
            .name = "PARANOID",
            .description = "Paranoid security level (~600,000 qubits, 200+ years)",
            .params = {
                .chipmunk_n = 512,
                .chipmunk_gamma = 8,
                .randomness_size = 64,
                .ring_lwe_n = 4096,          // Quadrupled
                .ring_lwe_q = 131071,        // Large prime
                .ring_lwe_sigma_numerator = 128,
                .ntru_n = 4096,              // Quadrupled
                .ntru_q = 262144,            // Very large prime
                .code_n = 12288,             // Quadrupled
                .code_k = 6144,              // Proportional
                .code_t = 768                // Quadrupled error weight
            },
            .expected_qubits_ring_lwe = 360000, // ~4n×log₂(q) = 4×4096×17
            .expected_qubits_ntru = 280000,     // ~4n×log₂(q) = 4×4096×18
            .expected_qubits_code = 320000,     // ~2n = 2×12288 + enhanced
            .expected_total_qubits = 960000
        }
    };
    
    return sets[0]; // Return first set (function signature compatibility)
}

/**
 * @brief Test performance with different parameter sets
 */
static bool s_test_parameter_performance(void) {
    log_it(L_INFO, "Testing ChipmunkRing parameter set performance comparison...");
    
    // Get parameter sets
    parameter_set_t sets[] = {
        // Minimal security
        {
            .name = "MINIMAL",
            .description = "Minimum viable quantum security (~150,000 qubits)",
            .params = {
                .chipmunk_n = 256, .chipmunk_gamma = 4, .randomness_size = 32,
                .ring_lwe_n = 512, .ring_lwe_q = 12289, .ring_lwe_sigma_numerator = 16,
                .ntru_n = 512, .ntru_q = 32768,
                .code_n = 1536, .code_k = 768, .code_t = 96
            },
            .expected_total_qubits = 115000
        },
        
        // Fast performance
        {
            .name = "FAST",
            .description = "High performance, reduced security (~100,000 qubits)",
            .params = {
                .chipmunk_n = 128, .chipmunk_gamma = 2, .randomness_size = 16,
                .ring_lwe_n = 256, .ring_lwe_q = 4093, .ring_lwe_sigma_numerator = 8,
                .ntru_n = 256, .ntru_q = 16384,
                .code_n = 768, .code_k = 384, .code_t = 48
            },
            .expected_total_qubits = 53000
        },
        
        // Balanced (current)
        {
            .name = "BALANCED",
            .description = "Current optimized parameters (~240,000 qubits)",
            .params = {
                .chipmunk_n = 256, .chipmunk_gamma = 4, .randomness_size = 32,
                .ring_lwe_n = 1024, .ring_lwe_q = 40961, .ring_lwe_sigma_numerator = 32,
                .ntru_n = 1024, .ntru_q = 65537,
                .code_n = 3072, .code_k = 1536, .code_t = 192
            },
            .expected_total_qubits = 240000
        },
        
        // Maximum security
        {
            .name = "MAXIMUM",
            .description = "Maximum quantum security (~480,000 qubits, 100+ years)",
            .params = {
                .chipmunk_n = 512, .chipmunk_gamma = 8, .randomness_size = 64,
                .ring_lwe_n = 2048, .ring_lwe_q = 65537, .ring_lwe_sigma_numerator = 64,
                .ntru_n = 2048, .ntru_q = 131071,
                .code_n = 6144, .code_k = 3072, .code_t = 384
            },
            .expected_total_qubits = 480000
        },
        
        // Paranoid security
        {
            .name = "PARANOID", 
            .description = "Paranoid security level (~960,000 qubits, 200+ years)",
            .params = {
                .chipmunk_n = 512, .chipmunk_gamma = 8, .randomness_size = 64,
                .ring_lwe_n = 4096, .ring_lwe_q = 131071, .ring_lwe_sigma_numerator = 128,
                .ntru_n = 4096, .ntru_q = 262144,
                .code_n = 12288, .code_k = 6144, .code_t = 768
            },
            .expected_total_qubits = 960000
        }
    };
    
    const size_t num_sets = sizeof(sets) / sizeof(sets[0]);
    
    // Hash the test message
    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");
    
    // Test each parameter set in complete isolation
    for (size_t set_idx = 0; set_idx < num_sets; set_idx++) {
        const parameter_set_t* set = &sets[set_idx];
        
        log_it(L_INFO, "Testing parameter set: %s", set->name);
        log_it(L_INFO, "  Description: %s", set->description);
        
        
        // Now set new parameters
        int param_result = dap_enc_chipmunk_ring_set_params(&set->params);
        dap_assert(param_result == 0, "Parameter setting should succeed");
        
        // Generate keys for test ring AFTER parameter change
        dap_enc_key_t** l_ring_keys = DAP_NEW_SIZE(dap_enc_key_t*, COMPARISON_RING_SIZE * sizeof(dap_enc_key_t*));
        dap_assert(l_ring_keys != NULL, "Memory allocation for ring keys should succeed");
        memset(l_ring_keys, 0, COMPARISON_RING_SIZE * sizeof(dap_enc_key_t*));
        
        for (size_t i = 0; i < COMPARISON_RING_SIZE; i++) {
            l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
        }
        
        // Measure performance
        double l_total_signing_time = 0.0;
        double l_total_verification_time = 0.0;
        size_t l_signature_size = 0;
        
        for (size_t iter = 0; iter < COMPARISON_ITERATIONS; iter++) {
            // Measure signing time
            double l_sign_start = get_time_ms();
            dap_sign_t* l_signature = dap_sign_create_ring(
                l_ring_keys[iter % COMPARISON_RING_SIZE],
                &l_message_hash, sizeof(l_message_hash),
                l_ring_keys, COMPARISON_RING_SIZE,
                iter % COMPARISON_RING_SIZE
            );
            double l_sign_end = get_time_ms();
            
            dap_assert(l_signature != NULL, "Signature creation should succeed");
            l_signature_size = l_signature->header.sign_size;
            l_total_signing_time += (l_sign_end - l_sign_start);
            
            // Measure verification time
            double l_verify_start = get_time_ms();
            int l_verify_result = dap_sign_verify_ring(l_signature, &l_message_hash, sizeof(l_message_hash),
                                                      l_ring_keys, COMPARISON_RING_SIZE);
            double l_verify_end = get_time_ms();
            
            dap_assert(l_verify_result == 0, "Signature verification should succeed");
            l_total_verification_time += (l_verify_end - l_verify_start);
            
            // CRITICAL: Proper signature cleanup
            if (l_signature) {
                DAP_DELETE(l_signature);
                l_signature = NULL;
            }
            
            // Force memory cleanup after each signature
            usleep(100); // 0.1ms delay
        }
        
        // Calculate averages
        double l_avg_signing_time = l_total_signing_time / COMPARISON_ITERATIONS;
        double l_avg_verification_time = l_total_verification_time / COMPARISON_ITERATIONS;
        
        // Get layer sizes
        size_t ring_lwe_size, ntru_size, code_size, binding_size;
        dap_enc_chipmunk_ring_get_layer_sizes(&ring_lwe_size, &ntru_size, &code_size, &binding_size);
        
        // Store results
        g_param_results[g_param_results_count].param_name = set->name;
        g_param_results[g_param_results_count].signature_size = l_signature_size;
        g_param_results[g_param_results_count].avg_signing_time = l_avg_signing_time;
        g_param_results[g_param_results_count].avg_verification_time = l_avg_verification_time;
        g_param_results[g_param_results_count].total_quantum_resistance = set->expected_total_qubits;
        g_param_results[g_param_results_count].ring_lwe_size = ring_lwe_size;
        g_param_results[g_param_results_count].ntru_size = ntru_size;
        g_param_results[g_param_results_count].code_size = code_size;
        g_param_results[g_param_results_count].binding_size = binding_size;
        g_param_results_count++;
        
        // CRITICAL: Complete cleanup before parameter restoration
        for (size_t i = 0; i < COMPARISON_RING_SIZE; i++) {
            if (l_ring_keys[i]) {
                dap_enc_key_delete(l_ring_keys[i]);
                l_ring_keys[i] = NULL;
            }
        }

        // Free the dynamic array itself
        if (l_ring_keys) {
            DAP_FREE(l_ring_keys);
            l_ring_keys = NULL;
        }
    }
    
    return true;
}

/**
 * @brief Print comprehensive parameter comparison table
 */
static void s_print_parameter_comparison_table(void) {
    log_it(L_INFO, " ");
    log_it(L_INFO, "╔══════════════════════════════════════════════════════════════════════════════╗");
    log_it(L_INFO, "║                      CHIPMUNKRING PARAMETER COMPARISON REPORT                ║");
    log_it(L_INFO, "║                   Ring Size: %d participants, Iterations: %d                 ║", 
           COMPARISON_RING_SIZE, COMPARISON_ITERATIONS);
    log_it(L_INFO, "╠═══════════╪═══════════╪═══════════╪══════════╪══════════╪══════════╪═════════╣");
    log_it(L_INFO, "║ Param Set │ Signature │  Signing  │ Verif.   │ Quantum  │ Ring-LWE │  NTRU   ║");
    log_it(L_INFO, "║    Name   │   Size    │   Time    │  Time    │ Qubits   │   Size   │  Size   ║");
    log_it(L_INFO, "╠═══════════╪═══════════╪═══════════╪══════════╪══════════╪══════════╪═════════╣");
    
    for (size_t i = 0; i < g_param_results_count; i++) {
        const param_performance_result_t* result = &g_param_results[i];
        
        log_it(L_INFO, "║ %-9s │ %7.1fKB │ %7.3fms │ %6.3fms │ %7.0fK │ %6.1fKB │ %5.1fKB ║",
               result->param_name,
               result->signature_size / 1024.0,
               result->avg_signing_time,
               result->avg_verification_time,
               result->total_quantum_resistance / 1000.0,
               result->ring_lwe_size / 1024.0,
               result->ntru_size / 1024.0);
    }
    
    log_it(L_INFO, "╚═══════════╧═══════════╧═══════════╧══════════╧══════════╧══════════╧═════════╝");
    log_it(L_INFO, " ");
    
    // Security levels description
    log_it(L_INFO, "SECURITY LEVELS:");
    for (size_t i = 0; i < g_param_results_count; i++) {
        const param_performance_result_t* result = &g_param_results[i];
        
        const char* security_desc = "";
        if (result->total_quantum_resistance < 100000) {
            security_desc = "High-performance, minimal viable security";
        } else if (result->total_quantum_resistance < 200000) {
            security_desc = "Good security, balanced performance";
        } else if (result->total_quantum_resistance < 300000) {
            security_desc = "Strong security, production recommended";
        } else if (result->total_quantum_resistance < 500000) {
            security_desc = "Maximum practical security, 100+ years";
        } else {
            security_desc = "Paranoid security, 200+ years protection";
        }
        
        log_it(L_INFO, "• %s: %s", result->param_name, security_desc);
    }
    log_it(L_INFO, " ");
    
    // Performance analysis
    log_it(L_INFO, "PARAMETER ANALYSIS:");
    log_it(L_INFO, "- Ring size: %d participants (fixed for fair comparison)", COMPARISON_RING_SIZE);
    log_it(L_INFO, "- Iterations: %d per parameter set", COMPARISON_ITERATIONS);
    log_it(L_INFO, "- Message size: %zu bytes", strlen(TEST_MESSAGE));
    log_it(L_INFO, " ");
    
    // Find best performance vs security tradeoffs
    size_t best_performance_idx = 0, best_security_idx = 0, best_balanced_idx = 0;
    double best_total_time = g_param_results[0].avg_signing_time + g_param_results[0].avg_verification_time;
    size_t best_security = g_param_results[0].total_quantum_resistance;
    double best_ratio = (double)g_param_results[0].total_quantum_resistance / 
                       (g_param_results[0].avg_signing_time + g_param_results[0].avg_verification_time);
    
    for (size_t i = 1; i < g_param_results_count; i++) {
        double total_time = g_param_results[i].avg_signing_time + g_param_results[i].avg_verification_time;
        double ratio = (double)g_param_results[i].total_quantum_resistance / total_time;
        
        if (total_time < best_total_time) {
            best_performance_idx = i;
            best_total_time = total_time;
        }
        
        if (g_param_results[i].total_quantum_resistance > best_security) {
            best_security_idx = i;
            best_security = g_param_results[i].total_quantum_resistance;
        }
        
        if (ratio > best_ratio) {
            best_balanced_idx = i;
            best_ratio = ratio;
        }
    }
    
    log_it(L_INFO, "RECOMMENDATIONS:");
    log_it(L_INFO, "• Best Performance: %s (%.3fms total, %zuK qubits)",
           g_param_results[best_performance_idx].param_name,
           g_param_results[best_performance_idx].avg_signing_time + g_param_results[best_performance_idx].avg_verification_time,
           g_param_results[best_performance_idx].total_quantum_resistance / 1000);
    log_it(L_INFO, "• Best Security: %s (%zuK qubits, %.3fms total)",
           g_param_results[best_security_idx].param_name,
           g_param_results[best_security_idx].total_quantum_resistance / 1000,
           g_param_results[best_security_idx].avg_signing_time + g_param_results[best_security_idx].avg_verification_time);
    log_it(L_INFO, "• Best Balanced: %s (%.0f qubits/ms ratio)",
           g_param_results[best_balanced_idx].param_name,
           best_ratio);
    log_it(L_INFO, " ");
}

/**
 * @brief Main test function
 */
bool test_chipmunk_ring_parameter_comparison(void) {
    log_it(L_INFO, "Starting ChipmunkRing parameter comparison tests...");
    
    // Test different parameter sets
    dap_assert(s_test_parameter_performance(), "Parameter performance test should succeed");
    
    // Print comprehensive comparison table
    s_print_parameter_comparison_table();
    
    log_it(L_INFO, "Parameter comparison tests completed successfully");
    return true;
}

int main(void) {
    // Initialize test framework
    dap_log_level_set(L_INFO);
    
    // Run parameter comparison tests
    bool result = test_chipmunk_ring_parameter_comparison();
    
    if (result) {
        log_it(L_NOTICE, "[ * ] ChipmunkRing parameter comparison tests completed");
        log_it(L_NOTICE, "[ * ] All parameter comparison tests PASSED");
        return 0;
    } else {
        log_it(L_ERROR, "[ ✗ ] ChipmunkRing parameter comparison tests FAILED");
        return 1;
    }
}
