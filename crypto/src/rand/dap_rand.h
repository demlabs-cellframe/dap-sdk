#ifndef __DAP_RAND_H__
#define __DAP_RAND_H__
#include "inttypes.h"
#include "dap_math_ops.h"

// Generate random bytes and output the result to random_array
int randombytes(void* random_array, unsigned int nbytes);
int randombase64(void*random_array, unsigned int size);

// Helper macros for variadic argument counting (FIXED!)
#define _GET_RANDOM_MACRO(_1, _2, NAME, ...) NAME

// Generic variadic random generator - one function, multiple modes
#define DECLARE_VARIADIC_RANDOM(TYPE, SUFFIX) \
    static inline TYPE _random_##SUFFIX##_full(void) { \
        TYPE ret; \
        if (randombytes(&ret, sizeof(TYPE)) != 0) return 0; \
        return ret; \
    } \
    \
    static inline TYPE _random_##SUFFIX##_range(TYPE max_value) { \
        if (max_value <= 1) return 0; \
        \
        /* Check for power of 2 */ \
        if (!(max_value & (max_value - 1))) { \
            TYPE ret; \
            if (randombytes(&ret, sizeof(TYPE)) != 0) return 0; \
            return ret & (max_value - 1); \
        } \
        \
        /* General case: rejection sampling */ \
        const TYPE threshold = ((TYPE)~0 / max_value) * max_value; \
        TYPE ret; \
        int attempts = 0; \
        \
        do { \
            if (randombytes(&ret, sizeof(TYPE)) != 0) return 0; \
            attempts++; \
        } while (ret >= threshold && attempts < 100); \
        \
        if (attempts >= 100) return 0; \
        return ret % max_value; \
    }

// Generate all the variadic random functions
DECLARE_VARIADIC_RANDOM(uint8_t, u8)
DECLARE_VARIADIC_RANDOM(uint16_t, u16)  
DECLARE_VARIADIC_RANDOM(uint32_t, u32)
DECLARE_VARIADIC_RANDOM(uint64_t, u64)

// Variadic magic: m_dap_random_u8() or m_dap_random_u8(max) - CORRECTED ORDER!
#define m_dap_random_u8(...) _GET_RANDOM_MACRO(dummy, ##__VA_ARGS__, _random_u8_range, _random_u8_full)(__VA_ARGS__)
#define m_dap_random_u16(...) _GET_RANDOM_MACRO(dummy, ##__VA_ARGS__, _random_u16_range, _random_u16_full)(__VA_ARGS__)
#define m_dap_random_u32(...) _GET_RANDOM_MACRO(dummy, ##__VA_ARGS__, _random_u32_range, _random_u32_full)(__VA_ARGS__)
#define m_dap_random_u64(...) _GET_RANDOM_MACRO(dummy, ##__VA_ARGS__, _random_u64_range, _random_u64_full)(__VA_ARGS__)

// Backward compatibility wrappers (inline) - replacing old declarations
static inline uint8_t dap_random_byte(void) {
    return m_dap_random_u8();
}

static inline uint32_t random_uint32_t(const uint32_t MAX_NUMBER) {
    return m_dap_random_u32(MAX_NUMBER);
}

static inline uint16_t dap_random_uint16(void) {
    return m_dap_random_u16();
}

void dap_pseudo_random_seed(uint256_t a_seed);
uint256_t dap_pseudo_random_get(uint256_t a_rand_max, uint256_t *a_raw_result);

// Cleanup function for proper resource management
void dap_rand_cleanup(void);

#endif
