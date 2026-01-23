/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file test_manual_arch_selection.c
 * @brief Unit tests for manual architecture selection API
 * @details Tests dap_cpu_arch_set/get API for manual SIMD architecture selection
 * @date 2026-01-11
 */

#define LOG_TAG "test_manual_arch_selection"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_cpu_arch.h"
#include "dap_test.h"
#include <string.h>

// Test JSON for correctness validation
static const char *s_test_json = 
    "{\"name\":\"Test\",\"value\":42,\"array\":[1,2,3],\"nested\":{\"key\":\"value\"}}";

/**
 * @brief Test architecture set/get cycle
 * @details AUTO → specific arch → AUTO
 */
static bool s_test_arch_set_get_cycle(void)
{
    log_it(L_INFO, "================================================");
    log_it(L_INFO, "Test: Architecture Set/Get Cycle");
    log_it(L_INFO, "================================================");
    
    // Get initial architecture (should be AUTO or auto-detected)
    dap_cpu_arch_t initial_arch = dap_cpu_arch_get();
    log_it(L_INFO, "Initial architecture: %s", dap_cpu_arch_get_name(initial_arch));
    
    // Try to set Reference C (always available)
    int ret = dap_cpu_arch_set(DAP_CPU_ARCH_REFERENCE);
    if (ret != 0) {
        log_it(L_ERROR, "Failed to set Reference C");
        return false;
    }
    
    dap_cpu_arch_t current = dap_cpu_arch_get();
    if (current != DAP_CPU_ARCH_REFERENCE) {
        log_it(L_ERROR, "Architecture should be Reference C, got %s", dap_cpu_arch_get_name(current));
        return false;
    }
    log_it(L_INFO, "✓ Set to Reference C: %s", dap_cpu_arch_get_name(current));
    
    // Try SSE2 if available
    if (dap_cpu_arch_is_available(DAP_CPU_ARCH_SSE2)) {
        ret = dap_cpu_arch_set(DAP_CPU_ARCH_SSE2);
        if (ret != 0) {
            log_it(L_ERROR, "Failed to set SSE2");
            return false;
        }
        
        current = dap_cpu_arch_get();
        if (current != DAP_CPU_ARCH_SSE2) {
            log_it(L_ERROR, "Architecture should be SSE2");
            return false;
        }
        log_it(L_INFO, "✓ Set to SSE2: %s", dap_cpu_arch_get_name(current));
    } else {
        log_it(L_INFO, "○ SSE2 not available, skipping");
    }
    
    // Try AVX2 if available
    if (dap_cpu_arch_is_available(DAP_CPU_ARCH_AVX2)) {
        ret = dap_cpu_arch_set(DAP_CPU_ARCH_AVX2);
        if (ret != 0) {
            log_it(L_ERROR, "Failed to set AVX2");
            return false;
        }
        
        current = dap_cpu_arch_get();
        if (current != DAP_CPU_ARCH_AVX2) {
            log_it(L_ERROR, "Architecture should be AVX2");
            return false;
        }
        log_it(L_INFO, "✓ Set to AVX2: %s", dap_cpu_arch_get_name(current));
    } else {
        log_it(L_INFO, "○ AVX2 not available, skipping");
    }
    
    // Try AVX-512 if available
    if (dap_cpu_arch_is_available(DAP_CPU_ARCH_AVX512)) {
        ret = dap_cpu_arch_set(DAP_CPU_ARCH_AVX512);
        if (ret != 0) {
            log_it(L_ERROR, "Failed to set AVX-512");
            return false;
        }
        
        current = dap_cpu_arch_get();
        if (current != DAP_CPU_ARCH_AVX512) {
            log_it(L_ERROR, "Architecture should be AVX-512");
            return false;
        }
        log_it(L_INFO, "✓ Set to AVX-512: %s", dap_cpu_arch_get_name(current));
    } else {
        log_it(L_INFO, "○ AVX-512 not available, skipping");
    }
    
    // Reset to AUTO
    ret = dap_cpu_arch_set(DAP_CPU_ARCH_AUTO);
    if (ret != 0) {
        log_it(L_ERROR, "Failed to reset to AUTO");
        return false;
    }
    
    current = dap_cpu_arch_get();
    log_it(L_INFO, "✓ Reset to AUTO, detected: %s", dap_cpu_arch_get_name(current));
    
    log_it(L_INFO, "✅ Architecture cycle test passed");
    return true;
}

/**
 * @brief Test invalid architecture rejection
 * @details Попытка установить недоступную архитектуру должна возвращать -1
 */
static bool s_test_invalid_arch_rejection(void)
{
    log_it(L_INFO, "================================================");
    log_it(L_INFO, "Test: Invalid Architecture Rejection");
    log_it(L_INFO, "================================================");
    
    // Save current architecture
    dap_cpu_arch_t saved_arch = dap_cpu_arch_get();
    
    // Try to set architectures that might not be available
    dap_cpu_arch_t test_archs[] = {
        DAP_CPU_ARCH_SSE2,
        DAP_CPU_ARCH_AVX2,
        DAP_CPU_ARCH_AVX512,
        DAP_CPU_ARCH_NEON,
        DAP_CPU_ARCH_SVE
    };
    
    bool all_passed = true;
    
    for (size_t i = 0; i < sizeof(test_archs) / sizeof(test_archs[0]); i++) {
        dap_cpu_arch_t arch = test_archs[i];
        bool available = dap_cpu_arch_is_available(arch);
        int ret = dap_cpu_arch_set(arch);
        
        if (available) {
            if (ret != 0) {
                log_it(L_ERROR, "Setting available %s should succeed", 
                       dap_cpu_arch_get_name(arch));
                all_passed = false;
            } else {
                log_it(L_INFO, "✓ %s is available and was set successfully", 
                       dap_cpu_arch_get_name(arch));
            }
        } else {
            if (ret == 0) {
                log_it(L_ERROR, "Setting unavailable %s should fail", 
                       dap_cpu_arch_get_name(arch));
                all_passed = false;
            } else {
                log_it(L_INFO, "✓ %s is unavailable and was correctly rejected", 
                       dap_cpu_arch_get_name(arch));
            }
        }
    }
    
    // Restore original architecture
    dap_cpu_arch_set(saved_arch);
    
    if (all_passed) {
        log_it(L_INFO, "✅ Invalid architecture rejection test passed");
    } else {
        log_it(L_ERROR, "❌ Invalid architecture rejection test failed");
    }
    
    return all_passed;
}

/**
 * @brief Test persistence of manual override
 * @details Manual override должен сохраняться между вызовами
 */
static bool s_test_override_persistence(void)
{
    log_it(L_INFO, "================================================");
    log_it(L_INFO, "Test: Override Persistence");
    log_it(L_INFO, "================================================");
    
    // Set to Reference C
    int ret = dap_cpu_arch_set(DAP_CPU_ARCH_REFERENCE);
    if (ret != 0) {
        log_it(L_ERROR, "Failed to set Reference C");
        return false;
    }
    
    // Check persistence across multiple get() calls
    for (int i = 0; i < 10; i++) {
        dap_cpu_arch_t arch = dap_cpu_arch_get();
        if (arch != DAP_CPU_ARCH_REFERENCE) {
            log_it(L_ERROR, "Architecture should stay Reference C (iteration %d)", i);
            return false;
        }
    }
    log_it(L_INFO, "✓ Override persisted across 10 get() calls");
    
    // Reset to AUTO and verify it changes
    ret = dap_cpu_arch_set(DAP_CPU_ARCH_AUTO);
    if (ret != 0) {
        log_it(L_ERROR, "Failed to set AUTO");
        return false;
    }
    
    dap_cpu_arch_t auto_arch = dap_cpu_arch_get();
    log_it(L_INFO, "✓ AUTO mode selected: %s", dap_cpu_arch_get_name(auto_arch));
    
    log_it(L_INFO, "✅ Override persistence test passed");
    return true;
}

/**
 * @brief Test correctness with each architecture
 * @details Парсинг должен работать корректно с каждой архитектурой
 */
static bool s_test_correctness_per_arch(void)
{
    log_it(L_INFO, "================================================");
    log_it(L_INFO, "Test: Correctness Per Architecture");
    log_it(L_INFO, "================================================");
    
    // Save current architecture
    dap_cpu_arch_t saved_arch = dap_cpu_arch_get();
    
    // Test all available architectures
    dap_cpu_arch_t test_archs[] = {
        DAP_CPU_ARCH_REFERENCE,
        DAP_CPU_ARCH_SSE2,
        DAP_CPU_ARCH_AVX2,
        DAP_CPU_ARCH_AVX512,
        DAP_CPU_ARCH_NEON
    };
    
    bool all_passed = true;
    
    for (size_t i = 0; i < sizeof(test_archs) / sizeof(test_archs[0]); i++) {
        dap_cpu_arch_t arch = test_archs[i];
        
        if (!dap_cpu_arch_is_available(arch)) {
            log_it(L_INFO, "○ %s not available, skipping", dap_cpu_arch_get_name(arch));
            continue;
        }
        
        int ret = dap_cpu_arch_set(arch);
        if (ret != 0) {
            log_it(L_ERROR, "Failed to set %s", dap_cpu_arch_get_name(arch));
            all_passed = false;
            continue;
        }
        
        // Parse test JSON
        dap_json_t *json = dap_json_parse_string(s_test_json);
        if (!json) {
            log_it(L_ERROR, "Failed to parse JSON with %s", dap_cpu_arch_get_name(arch));
            all_passed = false;
            continue;
        }
        
        // Verify content
        const char *name = dap_json_object_get_string(json, "name");
        if (!name || strcmp(name, "Test") != 0) {
            log_it(L_ERROR, "Incorrect 'name' field with %s", dap_cpu_arch_get_name(arch));
            dap_json_object_free(json);
            all_passed = false;
            continue;
        }
        
        int value = dap_json_object_get_int(json, "value");
        if (value != 42) {
            log_it(L_ERROR, "Incorrect 'value' field with %s", dap_cpu_arch_get_name(arch));
            dap_json_object_free(json);
            all_passed = false;
            continue;
        }
        
        dap_json_object_free(json);
        log_it(L_INFO, "✓ %s: parsing correct", dap_cpu_arch_get_name(arch));
    }
    
    // Restore original architecture
    dap_cpu_arch_set(saved_arch);
    
    if (all_passed) {
        log_it(L_INFO, "✅ Correctness per architecture test passed");
    } else {
        log_it(L_ERROR, "❌ Correctness per architecture test failed");
    }
    
    return all_passed;
}

/**
 * @brief Test JSON module wrappers
 * @details dap_json_set_arch/get_arch должны работать корректно
 */
static bool s_test_json_wrappers(void)
{
    log_it(L_INFO, "================================================");
    log_it(L_INFO, "Test: JSON Module Wrappers");
    log_it(L_INFO, "================================================");
    
    // Test dap_json_set_arch (wrapper for dap_cpu_arch_set)
    int ret = dap_json_set_arch(DAP_CPU_ARCH_REFERENCE);
    if (ret != 0) {
        log_it(L_ERROR, "dap_json_set_arch(REFERENCE) should succeed");
        return false;
    }
    
    // Test dap_json_get_arch (wrapper for dap_cpu_arch_get)
    dap_cpu_arch_t arch = dap_json_get_arch();
    if (arch != DAP_CPU_ARCH_REFERENCE) {
        log_it(L_ERROR, "dap_json_get_arch() should return REFERENCE");
        return false;
    }
    
    // Test dap_json_get_arch_name
    const char *name = dap_json_get_arch_name(arch);
    if (!name || strcmp(name, "Reference C") != 0) {
        log_it(L_ERROR, "dap_json_get_arch_name() should return 'Reference C', got '%s'", name ? name : "NULL");
        return false;
    }
    
    log_it(L_INFO, "✓ JSON wrappers work correctly");
    
    // Reset to AUTO
    dap_json_set_arch(DAP_CPU_ARCH_AUTO);
    
    log_it(L_INFO, "✅ JSON wrappers test passed");
    return true;
}

/**
 * @brief Main test runner
 */
int dap_json_manual_arch_selection_tests_run(void)
{
    log_it(L_INFO, "================================================");
    log_it(L_INFO, "STARTING: Manual Architecture Selection Tests");
    log_it(L_INFO, "================================================");
    
    int tests_passed = 0;
    int tests_total = 5;
    
    tests_passed += s_test_arch_set_get_cycle() ? 1 : 0;
    tests_passed += s_test_invalid_arch_rejection() ? 1 : 0;
    tests_passed += s_test_override_persistence() ? 1 : 0;
    tests_passed += s_test_correctness_per_arch() ? 1 : 0;
    tests_passed += s_test_json_wrappers() ? 1 : 0;
    
    log_it(L_INFO, "================================================");
    log_it(L_INFO, "SUMMARY: Manual Architecture Selection Tests");
    log_it(L_INFO, "================================================");
    log_it(L_INFO, "Tests: %d/%d passed (%d%%)", 
           tests_passed, tests_total, (tests_passed * 100) / tests_total);
    
    if (tests_passed == tests_total) {
        log_it(L_INFO, "✅ ALL TESTS PASSED");
    } else {
        log_it(L_ERROR, "❌ SOME TESTS FAILED");
    }
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void)
{
    dap_print_module_name("DAP JSON Manual Architecture Selection Tests");
    return dap_json_manual_arch_selection_tests_run();
}
