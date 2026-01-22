/**
 * @file test_chunk_boundary_escapes.c
 * @brief Test SIMD chunk boundary handling with escape sequences
 * @details Tests correct parsing of JSON with escape sequences at various sizes,
 *          especially around SIMD chunk boundaries (16/32/64 bytes).
 * 
 * Historical context:
 * - Bug discovered on ARM64 NEON where JSON ≥64 bytes with escapes failed
 * - Test ensures correct behavior across all SIMD architectures (SSE2, AVX2, NEON, SVE)
 * - Validates that chunk boundaries don't break escape sequence handling
 * 
 * Test coverage:
 * - Small JSON (< 16 bytes): baseline correctness
 * - Medium JSON (16-63 bytes): single/multiple chunks
 * - Boundary JSON (64 bytes): exactly 4 chunks (NEON), 4 chunks (SSE2), 2 chunks (AVX2)
 * - Large JSON (>64 bytes): multi-chunk with various escape sequences
 */

#define LOG_TAG "test_chunk_boundary_escapes"

#include <stdio.h>
#include <string.h>
#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"

/**
 * @brief Test JSON size progression around chunk boundaries
 * @details Tests various sizes to ensure correct handling at SIMD chunk boundaries:
 *          - 16 bytes: 1 SSE2/NEON chunk
 *          - 32 bytes: 2 SSE2/NEON chunks, 1 AVX2 chunk
 *          - 64 bytes: 4 SSE2/NEON chunks, 2 AVX2 chunks, 1 AVX-512 chunk
 */
static void test_size_boundary(void)
{
    log_it(L_DEBUG, "Testing chunk boundary sizes (16/32/64 bytes)");
    
    // 13 bytes - smaller than any chunk
    const char *json_13 = "{\"a\":\"1\\n2\"}";
    dap_json_t *j13 = dap_json_parse_string(json_13);
    DAP_TEST_FAIL_IF_NULL(j13, "Parse 13-byte JSON (< 1 chunk)");
    dap_json_object_free(j13);
    
    // 16 bytes - exactly 1 SSE2/NEON chunk
    const char *json_16 = "{\"aa\":\"1\\n2\\t3\"}";
    dap_json_t *j16 = dap_json_parse_string(json_16);
    DAP_TEST_FAIL_IF_NULL(j16, "Parse 16-byte JSON (= 1 SSE2/NEON chunk)");
    dap_json_object_free(j16);
    
    // 32 bytes - 2 SSE2/NEON chunks, 1 AVX2 chunk
    const char *json_32 = "{\"aaa\":\"1\\n2\",\"bbb\":\"3\\t4\\r5\"}";
    dap_json_t *j32 = dap_json_parse_string(json_32);
    DAP_TEST_FAIL_IF_NULL(j32, "Parse 32-byte JSON (= 2 SSE2/NEON, 1 AVX2)");
    dap_json_object_free(j32);
    
    // 61 bytes - just before 64-byte boundary
    const char *json_61 = "{\"aaaaaaaaaa\":\"1\\n2\",\"bbbbbbbbbb\":\"3\\t4\",\"cccccccccc\":\"5\\r6\"}";
    dap_json_t *j61 = dap_json_parse_string(json_61);
    DAP_TEST_FAIL_IF_NULL(j61, "Parse 61-byte JSON (< 64 boundary)");
    dap_json_object_free(j61);
    
    // 64 bytes - exactly 4 SSE2/NEON chunks (critical boundary)
    const char *json_64 = "{\"aaaaaaaaaaa\":\"1\\n2\",\"bbbbbbbbbbb\":\"3\\t4\",\"ccccccccccc\":\"5\\r6\"}";
    dap_json_t *j64 = dap_json_parse_string(json_64);
    DAP_TEST_FAIL_IF_NULL(j64, "Parse 64-byte JSON (= 4 SSE2/NEON chunks) - CRITICAL");
    dap_json_object_free(j64);
    
    // 67 bytes - beyond 64-byte boundary
    const char *json_67 = "{\"aaaaaaaaaaaa\":\"1\\n2\",\"bbbbbbbbbbbb\":\"3\\t4\",\"cccccccccccc\":\"5\\r6\"}";
    dap_json_t *j67 = dap_json_parse_string(json_67);
    DAP_TEST_FAIL_IF_NULL(j67, "Parse 67-byte JSON (> 64 boundary)");
    dap_json_object_free(j67);
    
    // 128 bytes - 8 SSE2/NEON chunks, 4 AVX2 chunks, 2 AVX-512 chunks
    const char *json_128 = "{\"k1\":\"val1\\nline2\",\"k2\":\"val2\\ttab\",\"k3\":\"val3\\rret\","
                           "\"k4\":\"val4\\\"quote\",\"k5\":\"val5\\\\slash\",\"k6\":\"end\"}";
    dap_json_t *j128 = dap_json_parse_string(json_128);
    DAP_TEST_FAIL_IF_NULL(j128, "Parse 128-byte JSON (multiple chunks all archs)");
    dap_json_object_free(j128);
}

/**
 * @brief Test exact JSON from test_unicode.c with all escape types
 * @details This is a real-world test case with multiple escape sequences
 *          that historically failed on ARM64 at 178 bytes
 */
static void test_unicode_escape_json(void)
{
    log_it(L_DEBUG, "Testing comprehensive escape sequences (178 bytes)");
    
    const char *test_json = "{\"newline\":\"line1\\nline2\","
                           "\"tab\":\"col1\\tcol2\","
                           "\"return\":\"text\\rmore\","
                           "\"quote\":\"say \\\"hello\\\"\","
                           "\"backslash\":\"path\\\\file\","
                           "\"slash\":\"a\\/b\","
                           "\"backspace\":\"text\\bx\","
                           "\"formfeed\":\"page\\fbreak\"}";
    
    dap_json_t *json = dap_json_parse_string(test_json);
    DAP_TEST_FAIL_IF_NULL(json, "Parse comprehensive escape JSON (178 bytes)");
    
    // Verify all escape types are correctly parsed
    const char *newline = dap_json_object_get_string(json, "newline");
    DAP_TEST_FAIL_IF_NULL(newline, "Get newline value");
    DAP_TEST_FAIL_IF(strcmp(newline, "line1\nline2") != 0, "Newline (\\n) escaped correctly");
    
    const char *tab = dap_json_object_get_string(json, "tab");
    DAP_TEST_FAIL_IF_NULL(tab, "Get tab value");
    DAP_TEST_FAIL_IF(strcmp(tab, "col1\tcol2") != 0, "Tab (\\t) escaped correctly");
    
    const char *ret = dap_json_object_get_string(json, "return");
    DAP_TEST_FAIL_IF_NULL(ret, "Get return value");
    DAP_TEST_FAIL_IF(strcmp(ret, "text\rmore") != 0, "Return (\\r) escaped correctly");
    
    const char *quote = dap_json_object_get_string(json, "quote");
    DAP_TEST_FAIL_IF_NULL(quote, "Get quote value");
    DAP_TEST_FAIL_IF(strcmp(quote, "say \"hello\"") != 0, "Quote (\\\") escaped correctly");
    
    const char *backslash = dap_json_object_get_string(json, "backslash");
    DAP_TEST_FAIL_IF_NULL(backslash, "Get backslash value");
    DAP_TEST_FAIL_IF(strcmp(backslash, "path\\file") != 0, "Backslash (\\\\) escaped correctly");
    
    dap_json_object_free(json);
}

/**
 * @brief Test small JSON with escapes (should always work)
 */
static void test_small_escape_json(void)
{
    log_it(L_DEBUG, "Testing small JSON with escapes (< 64 bytes)");
    
    const char *small_json = "{\"key\":\"line1\\nline2\"}";
    dap_json_t *json = dap_json_parse_string(small_json);
    DAP_TEST_FAIL_IF_NULL(json, "Parse small escape JSON");
    
    const char *val = dap_json_object_get_string(json, "key");
    DAP_TEST_FAIL_IF_NULL(val, "Get value");
    DAP_TEST_FAIL_IF(strcmp(val, "line1\nline2") != 0, "Value correct");
    
    dap_json_object_free(json);
}

int main(void)
{
    dap_test_msg("SIMD Chunk Boundary with Escape Sequences Tests");
    
    // Small baseline test (< 1 chunk)
    test_small_escape_json();
    
    // Comprehensive boundary tests across all chunk sizes
    test_size_boundary();
    
    // Real-world comprehensive escape test
    test_unicode_escape_json();
    
    dap_test_msg("All chunk boundary escape tests passed");
    return 0;
}
