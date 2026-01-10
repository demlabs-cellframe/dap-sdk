/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.

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
 * @file test_stage1_sse2_debug.c
 * @brief Debug test для SSE2 - детальная отладка tokenization
 */

#include "dap_common.h"
#include "dap_test.h"
#include "dap_json.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

#include <string.h>

#define LOG_TAG "test_sse2_debug"

// Forward declarations
extern int dap_json_stage1_run_sse2(dap_json_stage1_t *a_stage1);
extern void dap_json_stage1_sse2_set_debug(bool a_enable);
extern void dap_json_init(void);

/**
 * @brief Печать всех токенов
 */
static void s_print_tokens(const dap_json_stage1_t *a_stage1, const char *a_label)
{
    log_it(L_INFO, "=== %s: %zu tokens ===", a_label, a_stage1->indices_count);
    for (size_t i = 0; i < a_stage1->indices_count; i++) {
        const dap_json_struct_index_t *t = &a_stage1->indices[i];
        const char *type_name = "UNKNOWN";
        switch (t->type) {
            case TOKEN_TYPE_STRUCTURAL: type_name = "STRUCTURAL"; break;
            case TOKEN_TYPE_STRING: type_name = "STRING"; break;
            case TOKEN_TYPE_NUMBER: type_name = "NUMBER"; break;
            case TOKEN_TYPE_LITERAL: type_name = "LITERAL"; break;
        }
        
        if (t->type == TOKEN_TYPE_STRUCTURAL) {
            log_it(L_INFO, "  [%zu] pos=%u len=%u type=%s char='%c'",
                   i, t->position, t->length, type_name, t->character);
        } else {
            // Извлечь значение из input
            const uint8_t *value_start = a_stage1->input + t->position;
            size_t value_len = t->length;
            if (value_len > 20) value_len = 20; // ограничим для читаемости
            
            char value_buf[64] = {0};
            if (value_len > 0) {
                snprintf(value_buf, sizeof(value_buf), "%.*s", (int)value_len, value_start);
            }
            
            log_it(L_INFO, "  [%zu] pos=%u len=%u type=%s value='%s'",
                   i, t->position, t->length, type_name, value_buf);
        }
    }
}

/**
 * @brief Сравнить токены детально
 */
static bool s_compare_tokens_detailed(
    const dap_json_stage1_t *a_ref,
    const dap_json_stage1_t *a_simd,
    const char *a_input
)
{
    if (a_ref->indices_count != a_simd->indices_count) {
        log_it(L_ERROR, "Token count mismatch: ref=%zu, simd=%zu",
               a_ref->indices_count, a_simd->indices_count);
        s_print_tokens(a_ref, "Reference");
        s_print_tokens(a_simd, "SSE2");
        return false;
    }
    
    bool all_match = true;
    for (size_t i = 0; i < a_ref->indices_count; i++) {
        const dap_json_struct_index_t *r = &a_ref->indices[i];
        const dap_json_struct_index_t *s = &a_simd->indices[i];
        
        if (r->position != s->position || r->length != s->length ||
            r->type != s->type || r->character != s->character) {
            
            log_it(L_ERROR, "Token %zu mismatch:", i);
            log_it(L_ERROR, "  REF:  pos=%u len=%u type=%d char=%u",
                   r->position, r->length, r->type, r->character);
            log_it(L_ERROR, "  SSE2: pos=%u len=%u type=%d char=%u",
                   s->position, s->length, s->type, s->character);
            all_match = false;
        }
    }
    
    if (!all_match) {
        s_print_tokens(a_ref, "Reference");
        s_print_tokens(a_simd, "SSE2");
    }
    
    return all_match;
}

/**
 * @brief Тест простого массива чисел
 */
static bool s_test_simple_number_array(void)
{
    log_it(L_DEBUG, "Testing: simple number array");
    
    const char *input = "[1,2,3,4,5]";
    size_t len = strlen(input);
    
    log_it(L_INFO, "Input: '%s' (%zu bytes)", input, len);
    
    // Reference
    dap_json_stage1_t *ref = dap_json_stage1_create((const uint8_t*)input, len);
    if (!ref) {
        log_it(L_ERROR, "Reference init failed");
        return false;
    }
    
    int ref_err = dap_json_stage1_run_ref(ref);
    if (ref_err != STAGE1_SUCCESS) {
        log_it(L_ERROR, "Reference run failed: %d", ref_err);
        dap_json_stage1_free(ref);
        return false;
    }
    
    // SSE2
    dap_json_stage1_t *sse2 = dap_json_stage1_create((const uint8_t*)input, len);
    if (!sse2) {
        log_it(L_ERROR, "SSE2 init failed");
        dap_json_stage1_free(ref);
        return false;
    }
    
    int sse2_err = dap_json_stage1_run_sse2(sse2);
    if (sse2_err != STAGE1_SUCCESS) {
        log_it(L_ERROR, "SSE2 run failed: %d", sse2_err);
        dap_json_stage1_free(ref);
        dap_json_stage1_free(sse2);
        return false;
    }
    
    // Compare
    bool result = s_compare_tokens_detailed(ref, sse2, input);
    
    // Cleanup
    dap_json_stage1_free(ref);
    dap_json_stage1_free(sse2);
    
    return result;
}

/**
 * @brief Тест более длинного массива (больше 16 байт)
 */
static bool s_test_long_number_array(void)
{
    log_it(L_DEBUG, "Testing: long number array");
    
    const char *input = "[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20]";
    size_t len = strlen(input);
    
    log_it(L_INFO, "Input: '%s' (%zu bytes)", input, len);
    
    // Reference
    dap_json_stage1_t *ref = dap_json_stage1_create((const uint8_t*)input, len);
    if (!ref) return false;
    
    int ref_err = dap_json_stage1_run_ref(ref);
    if (ref_err != STAGE1_SUCCESS) {
        dap_json_stage1_free(ref);
        return false;
    }
    
    // SSE2
    dap_json_stage1_t *sse2 = dap_json_stage1_create((const uint8_t*)input, len);
    if (!sse2) {
        dap_json_stage1_free(ref);
        return false;
    }
    
    int sse2_err = dap_json_stage1_run_sse2(sse2);
    if (sse2_err != STAGE1_SUCCESS) {
        dap_json_stage1_free(ref);
        dap_json_stage1_free(sse2);
        return false;
    }
    
    // Compare
    bool result = s_compare_tokens_detailed(ref, sse2, input);
    
    // Cleanup
    dap_json_stage1_free(ref);
    dap_json_stage1_free(sse2);
    
    return result;
}

/**
 * @brief Тест объекта с числами
 */
static bool s_test_object_with_numbers(void)
{
    log_it(L_DEBUG, "Testing: object with numbers");
    
    const char *input = "{\"a\":1,\"b\":2}";
    size_t len = strlen(input);
    
    log_it(L_INFO, "Input: '%s' (%zu bytes)", input, len);
    
    // Reference
    dap_json_stage1_t *ref = dap_json_stage1_create((const uint8_t*)input, len);
    if (!ref) return false;
    
    int ref_err = dap_json_stage1_run_ref(ref);
    if (ref_err != STAGE1_SUCCESS) {
        dap_json_stage1_free(ref);
        return false;
    }
    
    // SSE2
    dap_json_stage1_t *sse2 = dap_json_stage1_create((const uint8_t*)input, len);
    if (!sse2) {
        dap_json_stage1_free(ref);
        return false;
    }
    
    int sse2_err = dap_json_stage1_run_sse2(sse2);
    if (sse2_err != STAGE1_SUCCESS) {
        dap_json_stage1_free(ref);
        dap_json_stage1_free(sse2);
        return false;
    }
    
    // Compare
    bool result = s_compare_tokens_detailed(ref, sse2, input);
    
    // Cleanup
    dap_json_stage1_free(ref);
    dap_json_stage1_free(sse2);
    
    return result;
}

int main(void)
{
    dap_json_init();
    
    // Enable detailed debug for SSE2 (configured via s_debug_more flag)
    dap_json_stage1_sse2_set_debug(true);
    
    dap_print_module_name("SSE2 Debug Tests");
    
    bool all_passed = true;
    
    log_it(L_INFO, "========================================");
    log_it(L_INFO, "Test 1: Simple number array [1,2,3,4,5]");
    log_it(L_INFO, "========================================");
    if (!s_test_simple_number_array()) {
        log_it(L_ERROR, "FAILED: Simple number array");
        all_passed = false;
    } else {
        log_it(L_INFO, "PASSED: Simple number array");
    }
    
    log_it(L_INFO, " ");
    log_it(L_INFO, "========================================");
    log_it(L_INFO, "Test 2: Long number array (52 bytes)");
    log_it(L_INFO, "========================================");
    if (!s_test_long_number_array()) {
        log_it(L_ERROR, "FAILED: Long number array");
        all_passed = false;
    } else {
        log_it(L_INFO, "PASSED: Long number array");
    }
    
    log_it(L_INFO, " ");
    log_it(L_INFO, "========================================");
    log_it(L_INFO, "Test 3: Object with numbers");
    log_it(L_INFO, "========================================");
    if (!s_test_object_with_numbers()) {
        log_it(L_ERROR, "FAILED: Object with numbers");
        all_passed = false;
    } else {
        log_it(L_INFO, "PASSED: Object with numbers");
    }
    
    log_it(L_INFO, " ");
    if (all_passed) {
        log_it(L_INFO, "=== All SSE2 Debug Tests Passed ===");
        return 0;
    } else {
        log_it(L_ERROR, "=== Some SSE2 Debug Tests Failed ===");
        return 1;
    }
}

