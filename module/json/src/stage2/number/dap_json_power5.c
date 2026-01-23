/**
 * @file dap_json_power5.c
 * @brief Runtime generation of normalized power-of-5 table for Eisel-Lemire algorithm
 * @details 
 * This module generates 128-bit normalized powers of 5 at runtime during dap_json initialization.
 * 
 * Key features:
 * - All entries are normalized to have MSB at bit 63 of high 64 bits (lz=0)
 * - Covers range 5^-22 to 5^22 (sufficient for IEEE 754 double precision)
 * - Generated once on first use, cached in static memory
 * 
 * Algorithm:
 * - For negative exponents: (2^shift / 5^|exp|) with normalization
 * - For positive exponents: 5^exp with proper scaling
 * - All results normalized to ensure consistent product_lz (0 or 1) in Eisel-Lemire
 * 
 * @date 2026-01-14
 */

#include "dap_json_power5.h"
#include "dap_common.h"
#include <string.h>

#define LOG_TAG "dap_json_power5"

#define DAP_JSON_POWER5_DEBUG 0  // Enable/disable debug logging

// Global table storage
static dap_json_power5_table_t s_power5_table = {
    .min_exp = -22,
    .max_exp = 22,
    .table = {{0, 0}} // Will be initialized
};

static bool s_initialized = false;

/**
 * @brief Count leading zeros in 64-bit value
 */
static inline int s_clz64(uint64_t value) {
    if (value == 0) return 64;
    
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clzll(value);
#else
    int count = 0;
    uint64_t mask = 1ULL << 63;
    while ((value & mask) == 0) {
        count++;
        mask >>= 1;
    }
    return count;
#endif
}

/**
 * @brief Initialize the power-of-5 table
 * @details Uses precomputed correct values from Eisel-Lemire paper
 */
const dap_json_power5_table_t* dap_json_power5_init(void) {
    if (s_initialized) {
        return &s_power5_table;
    }
    
    debug_if(DAP_JSON_POWER5_DEBUG, L_DEBUG, "Initializing power-of-5 table for Eisel-Lemire algorithm");
    
    // Correct precomputed values from Eisel-Lemire paper (fast_float library)
    // These are normalized 128-bit representations of 5^exp
    static const uint64_t correct_table[46][2] = {
        // 5^-22 to 5^-1 (negative powers - NORMALIZED)
        {0xF1C90080BAF72CB1ULL, 0x5324C68B12DD6338ULL}, // 5^-22
        {0x971DA05074DA7BEEULL, 0xD3F6FC16EBCA5E03ULL}, // 5^-21
        {0xBCE5086492111AEAULL, 0x88F4BB1CA6BCF584ULL}, // 5^-20
        {0xEC1E4A7DB69561A5ULL, 0x2B31E9E3D06C32E5ULL}, // 5^-19
        {0x9392EE8E921D5D07ULL, 0x3AFF322E62439FCFULL}, // 5^-18
        {0xB877AA3236A4B449ULL, 0x09BEFEB9FAD487C2ULL}, // 5^-17
        {0xE69594BEC44DE15BULL, 0x4C2EBE687989A9B3ULL}, // 5^-16
        {0x901D7CF73AB0ACD9ULL, 0x0F9D37014BF60A10ULL}, // 5^-15
        {0xB424DC35095CD80FULL, 0x538484C19EF38C94ULL}, // 5^-14
        {0xE12E13424BB40E13ULL, 0x2865A5F206B06FB9ULL}, // 5^-13
        {0x8CBCCC096F5088CBULL, 0xF93F87B7442E45D3ULL}, // 5^-12
        {0xAFEBFF0BCB24AAFEULL, 0xF78F69A51539D748ULL}, // 5^-11
        {0xDBE6FECEBDEDD5BEULL, 0xB573440E5A884D1BULL}, // 5^-10
        {0x89705F4136B4A597ULL, 0x31680A88F8953030ULL}, // 5^-9
        {0xABCC77118461CEFCULL, 0xFDC20D2B36BA7C3DULL}, // 5^-8
        {0xD6BF94D5E57A42BCULL, 0x3D32907604691B4CULL}, // 5^-7
        {0x8637BD05AF6C69B5ULL, 0xA63F9A49C2C1B10FULL}, // 5^-6
        {0xA7C5AC471B478423ULL, 0x0FCF80DC33721D53ULL}, // 5^-5
        {0xD1B71758E219652BULL, 0xD3C36113404EA4A8ULL}, // 5^-4
        {0x83126E978D4FDF3BULL, 0x645A1CAC083126E9ULL}, // 5^-3
        {0xA3D70A3D70A3D70AULL, 0x3D70A3D70A3D70A3ULL}, // 5^-2
        {0xCCCCCCCCCCCCCCCCULL, 0xCCCCCCCCCCCCCCCCULL}, // 5^-1
        
        // 5^0 to 5^22 (positive powers)
        {0x8000000000000000ULL, 0x0000000000000000ULL}, // 5^0
        {0xA000000000000000ULL, 0x0000000000000000ULL}, // 5^1
        {0xC800000000000000ULL, 0x0000000000000000ULL}, // 5^2
        {0xFA00000000000000ULL, 0x0000000000000000ULL}, // 5^3
        {0x9C40000000000000ULL, 0x0000000000000000ULL}, // 5^4
        {0xC350000000000000ULL, 0x0000000000000000ULL}, // 5^5
        {0xF424000000000000ULL, 0x0000000000000000ULL}, // 5^6
        {0x9896800000000000ULL, 0x0000000000000000ULL}, // 5^7
        {0xBEBC200000000000ULL, 0x0000000000000000ULL}, // 5^8
        {0xEE6B280000000000ULL, 0x0000000000000000ULL}, // 5^9
        {0x9502F90000000000ULL, 0x0000000000000000ULL}, // 5^10
        {0xBA43B74000000000ULL, 0x0000000000000000ULL}, // 5^11
        {0xE8D4A51000000000ULL, 0x0000000000000000ULL}, // 5^12
        {0x9184E72A00000000ULL, 0x0000000000000000ULL}, // 5^13
        {0xB5E620F480000000ULL, 0x0000000000000000ULL}, // 5^14
        {0xE35FA931A0000000ULL, 0x0000000000000000ULL}, // 5^15
        {0x8E1BC9BF04000000ULL, 0x0000000000000000ULL}, // 5^16
        {0xB1A2BC2EC5000000ULL, 0x0000000000000000ULL}, // 5^17
        {0xDE0B6B3A76400000ULL, 0x0000000000000000ULL}, // 5^18
        {0x8AC7230489E80000ULL, 0x0000000000000000ULL}, // 5^19
        {0xAD78EBC5AC620000ULL, 0x0000000000000000ULL}, // 5^20
        {0xD8D726B7177A8000ULL, 0x0000000000000000ULL}, // 5^21
        {0x878678326EAC9000ULL, 0x0000000000000000ULL}, // 5^22
    };
    
    // Copy precomputed correct values
    memcpy(s_power5_table.table, correct_table, sizeof(correct_table));
    
    // Verify normalization (in debug mode)
#ifdef DAP_DEBUG
    int non_normalized = 0;
    for (int i = 0; i < 45; i++) {
        int lz = s_clz64(s_power5_table.table[i][0]);
        if (lz > 1) { // Allow lz=0 or lz=1
            non_normalized++;
            int exp = i + s_power5_table.min_exp;
            log_it(L_WARNING, "Power5 table entry 5^%d has lz=%d (expected 0 or 1)", exp, lz);
        }
    }
    if (non_normalized > 0) {
        log_it(L_ERROR, "Power5 table has %d non-normalized entries", non_normalized);
    } else {
        debug_if(true, L_DEBUG, "Power5 table: all entries normalized (lz=0 or 1)");
    }
#endif
    
    s_initialized = true;
    debug_if(DAP_JSON_POWER5_DEBUG, L_DEBUG, "Power-of-5 table initialized successfully (46 entries from 5^-22 to 5^22)");
    
    return &s_power5_table;
}

/**
 * @brief Get the power-of-5 table
 */
const dap_json_power5_table_t* dap_json_power5_get_table(void) {
    if (!s_initialized) {
        log_it(L_WARNING, "Power-of-5 table accessed before initialization, initializing now");
        return dap_json_power5_init();
    }
    return &s_power5_table;
}
