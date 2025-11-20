/*
 * Authors:
 * Dmitry Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dap_common.h"
#include "dap_enc_base58.h"
#include "../../../fixtures/utilities/test_helpers.h"
#include "../../../fixtures/json_samples.h"
#include <inttypes.h>
#include <string.h>

#define LOG_TAG "test_base58"

// Test cases based on base58_encode_decode.json structure, adapted for base32
// Format: [hex_string, base32_string]
typedef struct {
    const char* hex_input;
    const char* base32_expected;
} base58_test_case_t;

// Test cases 
static const base32_test_case_t s_base32_test_cases[] = {
    {"", ""},
    {"61", "MF"},
    {"626262", "MFRGG"},
    {"636363", "MFRGG"},
    {"73696d706c792061206c6f6e6720737472696e67", "ONXW2ZJAMRQXIYJAO5UXI2BAAAQGC3TEEDX3XPY"},
    {"00eb15231dfceb60925886b67d065299925915aeb172c06647", "AHM6A83HENMP6QS0"},
    {"516b6fcd0f", "ABNR2XO34EX"},
    {"bf4f89001e670274dd", "X5YRBMDPK3J7"},
    {"572e4794", "K5SWYY3PNVSSA"},
    {"ecac89cad93923c02321", "7HIK76GYB7W6UJ"},
    {"10c8511e", "CPM5AG4"},
    {"00000000000000000000", "AAAAAAAAAA"},
    {"00000000000000000000000000000000000000000000000000000000000000000000000000000000", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"},
    {"00000000000000000000000000000000000000000000000000000000000000000000000000000001", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB"},
};


#define BASE58_TEST_CASES_COUNT (sizeof(s_base32_test_cases) / sizeof(s_base32_test_cases[0]))



/**
 * @brief Parse hex string to binary data
 * @param hex_str Hex string to parse
 * @param out Output buffer
 * @param out_size Output buffer size
 * @return Number of bytes parsed, or 0 on error
 */
static size_t s_parse_hex(const char* hex_str, uint8_t* out, size_t out_size) {
    if (!hex_str || !out) {
        return 0;
    }
    
    size_t hex_len = strlen(hex_str);
    if (hex_len == 0) {
        return 0;
    }
    
    // Hex string must have even length
    if (hex_len % 2 != 0) {
        return 0;
    }
    
    size_t bytes_needed = hex_len / 2;
    if (bytes_needed > out_size) {
        return 0;
    }
    
    return dap_hex2bin(out, hex_str, hex_len);
}







/**
 * @brief Main test function for Base58
 */
int main(void) {
    log_it(L_INFO, "Starting Base58 unit tests");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    l_all_passed &= s_test_base58_basic();
    l_all_passed &= s_test_base58_consistency();
    l_all_passed &= s_test_base58_empty();
    l_all_passed &= s_test_base58_performance();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All Base58 tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some Base58 tests failed!");
        return -1;
    }
}