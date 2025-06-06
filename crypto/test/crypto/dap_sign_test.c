#include <dap_test.h>
#include <dap_common.h>
#include <dap_sign.h>
#include <dap_enc_key.h>

#define LOG_TAG "dap_sign_test"

static void test_aggregation_support(void)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    log_it(L_INFO, "Testing signature aggregation support detection");
    
    // Test Chipmunk signature type support
    dap_sign_type_t chipmunk_type = {.type = SIG_TYPE_CHIPMUNK};
    bool supports_agg = dap_sign_type_supports_aggregation(chipmunk_type);
    dap_assert(supports_agg, "Chipmunk should support aggregation");
    
    bool supports_batch = dap_sign_type_supports_batch_verification(chipmunk_type);
    dap_assert(supports_batch, "Chipmunk should support batch verification");
    
    // Test other signature types don't support aggregation
    dap_sign_type_t bliss_type = {.type = SIG_TYPE_BLISS};
    bool bliss_agg = dap_sign_type_supports_aggregation(bliss_type);
    dap_assert(!bliss_agg, "Bliss should not support aggregation");
    
    log_it(L_INFO, "Aggregation support detection tests passed");
}

static void test_aggregation_types_query(void)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    log_it(L_INFO, "Testing aggregation types query");
    
    dap_sign_type_t chipmunk_type = {.type = SIG_TYPE_CHIPMUNK};
    dap_sign_aggregation_type_t agg_types[5];
    
    uint32_t count = dap_sign_get_supported_aggregation_types(chipmunk_type, agg_types, 5);
    dap_assert(count > 0, "Chipmunk should support at least one aggregation type");
    dap_assert(agg_types[0] == DAP_SIGN_AGGREGATION_TYPE_TREE_BASED, 
               "First aggregation type should be tree-based");
    
    log_it(L_INFO, "Found %u supported aggregation types for Chipmunk", count);
    log_it(L_INFO, "Aggregation types query tests passed");
}

static void test_signature_info_functions(void)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    log_it(L_INFO, "Testing signature info functions");
    
    // Create a dummy signature for testing
    size_t sign_size = sizeof(dap_sign_t) + 64; // Small dummy signature
    dap_sign_t *test_sign = DAP_NEW_Z_SIZE(dap_sign_t, sign_size);
    test_sign->header.type.type = SIG_TYPE_CHIPMUNK;
    test_sign->header.sign_size = 32;
    test_sign->header.sign_pkey_size = 32;
    
    // Test aggregated check (should return false for regular signature)
    bool is_agg = dap_sign_is_aggregated(test_sign);
    dap_assert(!is_agg, "Regular signature should not be aggregated");
    
    // Test signer count (should return 1 for regular signature) 
    uint32_t signers = dap_sign_get_signers_count(test_sign);
    dap_assert(signers == 1, "Regular signature should have 1 signer");
    
    DAP_DELETE(test_sign);
    log_it(L_INFO, "Signature info function tests passed");
}

static void test_batch_verification_context(void)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    log_it(L_INFO, "Testing batch verification context management");
    
    dap_sign_type_t chipmunk_type = {.type = SIG_TYPE_CHIPMUNK};
    
    // Test context creation
    dap_sign_batch_verify_ctx_t *ctx = dap_sign_batch_verify_ctx_new(chipmunk_type, 10);
    dap_assert(ctx != NULL, "Batch verify context should be created");
    dap_assert(ctx->signature_type.raw == chipmunk_type.raw, "Context should store correct signature type");
    dap_assert(ctx->max_signatures == 10, "Context should store correct max signatures");
    dap_assert(ctx->signatures_count == 0, "Context should start with 0 signatures");
    
    // Test context cleanup
    dap_sign_batch_verify_ctx_free(ctx);
    
    // Test invalid signature type
    dap_sign_type_t unsupported_type = {.type = SIG_TYPE_BLISS};
    dap_sign_batch_verify_ctx_t *invalid_ctx = dap_sign_batch_verify_ctx_new(unsupported_type, 10);
    dap_assert(invalid_ctx == NULL, "Context creation should fail for unsupported signature type");
    
    log_it(L_INFO, "Batch verification context tests passed");
}

static void test_performance_benchmarking(void)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    log_it(L_INFO, "Testing performance benchmarking functions");
    
    dap_sign_type_t chipmunk_type = {.type = SIG_TYPE_CHIPMUNK};
    dap_sign_performance_stats_t stats;
    
    // Test aggregation benchmarking
    int result = dap_sign_benchmark_aggregation(
        chipmunk_type, 
        DAP_SIGN_AGGREGATION_TYPE_TREE_BASED, 
        10, 
        &stats
    );
    dap_assert(result == 0, "Aggregation benchmark should succeed");
    dap_assert(stats.signatures_processed == 10, "Should process correct number of signatures");
    dap_assert(stats.aggregation_time_ms >= 0, "Aggregation time should be non-negative");
    
    // Test batch verification benchmarking
    result = dap_sign_benchmark_batch_verification(chipmunk_type, 10, &stats);
    dap_assert(result == 0, "Batch verification benchmark should succeed");
    dap_assert(stats.signatures_processed == 10, "Should process correct number of signatures");
    dap_assert(stats.batch_verification_time_ms >= 0, "Batch verification time should be non-negative");
    
    log_it(L_INFO, "Aggregation benchmark: %.2f ms, %.2f sigs/sec", 
           stats.aggregation_time_ms, stats.throughput_sigs_per_sec);
    
    // Test unsupported signature type
    dap_sign_type_t unsupported_type = {.type = SIG_TYPE_BLISS};
    result = dap_sign_benchmark_aggregation(
        unsupported_type, 
        DAP_SIGN_AGGREGATION_TYPE_TREE_BASED, 
        10, 
        &stats
    );
    dap_assert(result < 0, "Benchmark should fail for unsupported signature type");
    
    log_it(L_INFO, "Performance benchmarking tests passed");
}

void dap_sign_test_run(void)
{
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    log_it(L_INFO, "=== Running Universal Signature API Tests ===");
    
    test_aggregation_support();
    test_aggregation_types_query(); 
    test_signature_info_functions();
    test_batch_verification_context();
    test_performance_benchmarking();
    
    log_it(L_INFO, "=== All Universal Signature API Tests Passed ===");
} 