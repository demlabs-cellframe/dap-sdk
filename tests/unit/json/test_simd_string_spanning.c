/**
 * @file test_simd_string_spanning.c
 * @brief UNIVERSAL REGRESSION TEST: Strings spanning chunk boundaries for ALL architectures
 * 
 * This test targets the case where JSON strings span across SIMD chunk boundaries.
 * Bug discovered during Phase 1.4.4 - affects all SIMD implementations.
 * 
 * DO NOT MODIFY - Keep as permanent regression test!
 * 
 * Tested architectures:
 * - Reference (C implementation - baseline)
 * - SSE2 (x86/x64: 16-byte chunks)
 * - AVX2 (x86/x64: 32-byte chunks)
 * - AVX-512 (x86/x64: 64-byte chunks)
 * - ARM NEON (ARM: 16-byte chunks)
 */

#define LOG_TAG "test_simd_string_span"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "dap_common.h"
#include "dap_json.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

// Note: Architecture-specific SIMD headers are now included via dap_json_stage1.h

extern void dap_json_init(void);

typedef int (*stage1_impl_fn_t)(dap_json_stage1_t *);

typedef struct {
    const char *name;
    stage1_impl_fn_t impl;
    void (*set_debug)(bool);
} arch_impl_t;

static bool s_compare_tokens(dap_json_stage1_t *ref, dap_json_stage1_t *impl, const char *test_name, const char *arch_name) {
    if (ref->indices_count != impl->indices_count) {
        log_it(L_ERROR, "[%s/%s] Token count mismatch: ref=%zu, impl=%zu", 
               arch_name, test_name, ref->indices_count, impl->indices_count);
        return false;
    }
    
    for (size_t i = 0; i < ref->indices_count; i++) {
        if (ref->indices[i].position != impl->indices[i].position ||
            ref->indices[i].length != impl->indices[i].length ||
            ref->indices[i].type != impl->indices[i].type) {
            log_it(L_ERROR, "[%s/%s] Token %zu mismatch:", arch_name, test_name, i);
            log_it(L_ERROR, "  REF:  pos=%u len=%u type=%d", 
                   ref->indices[i].position, ref->indices[i].length, ref->indices[i].type);
            log_it(L_ERROR, "  IMPL: pos=%u len=%u type=%d", 
                   impl->indices[i].position, impl->indices[i].length, impl->indices[i].type);
            return false;
        }
    }
    
    return true;
}

static bool s_test_string_spanning_case(const char *input, const char *test_name, arch_impl_t *arch) {
    log_it(L_DEBUG, "  [%s] %s", arch->name, test_name);
    
    // Reference
    dap_json_stage1_t *ref = dap_json_stage1_create((const uint8_t*)input, strlen(input));
    int ref_result = dap_json_stage1_run_ref(ref);
    if (ref_result != 0) {
        log_it(L_ERROR, "  REF failed: %d", ref_result);
        dap_json_stage1_free(ref);
        return false;
    }
    
    // Implementation
    dap_json_stage1_t *impl = dap_json_stage1_create((const uint8_t*)input, strlen(input));
    int impl_result = arch->impl(impl);
    if (impl_result != 0) {
        log_it(L_ERROR, "  %s failed: %d", arch->name, impl_result);
        dap_json_stage1_free(ref);
        dap_json_stage1_free(impl);
        return false;
    }
    
    // Compare
    bool result = s_compare_tokens(ref, impl, test_name, arch->name);
    
    dap_json_stage1_free(ref);
    dap_json_stage1_free(impl);
    
    return result;
}

static int s_test_architecture(arch_impl_t *arch) {
    log_it(L_INFO, "===========================================");
    log_it(L_INFO, "Testing %s String Spanning", arch->name);
    log_it(L_INFO, "===========================================");
    
    int passed = 0;
    int failed = 0;
    
    // Test 1: String crosses chunk boundary
    if (s_test_string_spanning_case(
        "{\"key1234567890\":1}",
        "String crosses chunk boundary", arch)) {
        passed++;
    } else {
        failed++;
    }
    
    // Test 2: Multiple strings, multi-chunk
    if (s_test_string_spanning_case(
        "{\"k1\":1,\"k2\":2,\"k3\":3,\"k4\":4,\"k5\":5,\"k6\":6,\"k7\":7,\"k8\":8}",
        "Multiple strings (57 bytes)", arch)) {
        passed++;
    } else {
        failed++;
    }
    
    // Test 3: Complex object
    if (s_test_string_spanning_case(
        "{\"arr\":[1,2,3],\"str\":\"test\",\"num\":42,\"bool\":true,\"null\":null}",
        "Complex object (61 bytes)", arch)) {
        passed++;
    } else {
        failed++;
    }
    
    // Test 4: String exactly at boundary
    if (s_test_string_spanning_case(
        "{\"012345678901\":\"x\"}",
        "String at chunk boundary", arch)) {
        passed++;
    } else {
        failed++;
    }
    
    // Test 5: Long string spans multiple chunks
    if (s_test_string_spanning_case(
        "{\"key\":\"0123456789012345678901234567890123456789\"}",
        "Long string (3+ chunks)", arch)) {
        passed++;
    } else {
        failed++;
    }
    
    log_it(L_INFO, "[%s] Results: %d/%d tests passed", arch->name, passed, passed + failed);
    
    return failed;
}

int main() {
    dap_log_level_set(L_INFO);
    dap_json_init();
    
    log_it(L_INFO, "===========================================");
    log_it(L_INFO, "UNIVERSAL String Spanning Regression Test");
    log_it(L_INFO, "Phase 1.4.4 - All SIMD Architectures");
    log_it(L_INFO, "===========================================");
    
    int total_failed = 0;
    
    // Test Reference (baseline)
    arch_impl_t ref_arch = {"Reference", dap_json_stage1_run_ref, NULL};
    total_failed += s_test_architecture(&ref_arch);
    
#ifdef __SSE2__
    // Test SSE2
    extern int dap_json_stage1_run_sse2(dap_json_stage1_t *a_stage1);
    arch_impl_t sse2_arch = {"SSE2", dap_json_stage1_run_sse2, dap_json_stage1_sse2_set_debug};
    total_failed += s_test_architecture(&sse2_arch);
#else
    log_it(L_INFO, "SSE2 not available, skipping");
#endif
    
#ifdef __AVX2__
    // Test AVX2
    extern int dap_json_stage1_run_avx2(dap_json_stage1_t *a_stage1);
    arch_impl_t avx2_arch = {"AVX2", dap_json_stage1_run_avx2, NULL};
    total_failed += s_test_architecture(&avx2_arch);
#else
    log_it(L_INFO, "AVX2 not available, skipping");
#endif
    
#ifdef __AVX512F__
    // Test AVX-512
    extern int dap_json_stage1_run_avx512(dap_json_stage1_t *a_stage1);
    arch_impl_t avx512_arch = {"AVX-512", dap_json_stage1_run_avx512, NULL};
    total_failed += s_test_architecture(&avx512_arch);
#else
    log_it(L_INFO, "AVX-512 not available, skipping");
#endif
    
#ifdef __ARM_NEON
    // Test NEON
    extern int dap_json_stage1_run_neon(dap_json_stage1_t *a_stage1);
    arch_impl_t neon_arch = {"NEON", dap_json_stage1_run_neon, NULL};
    total_failed += s_test_architecture(&neon_arch);
#else
    log_it(L_INFO, "NEON not available, skipping");
#endif
    
    log_it(L_INFO, "===========================================");
    if (total_failed > 0) {
        log_it(L_ERROR, "REGRESSION TEST FAILED! (%d architectures with failures)", total_failed);
        return 1;
    }
    
    log_it(L_INFO, "All string spanning tests PASSED for all architectures ✓");
    return 0;
}

