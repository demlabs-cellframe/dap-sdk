/**
 * @file dap_math_str.h
 * @brief String conversion functions for 128-bit integers
 *
 * Copyright (c) 2024-2026 Demlabs
 * License: GNU GPL v3
 */

#pragma once

#include "dap_math_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DAP_GLOBAL_IS_INT128

/**
 * @brief Convert string to unsigned 128-bit integer
 * @param p Input string
 * @param endp Pointer to store end position (can be NULL)
 * @param base Number base (0 for auto-detect, 2-36)
 * @return Converted value
 */
uint128_t dap_strtou128(const char *p, char **endp, int base);

/**
 * @brief Convert string to signed 128-bit integer
 * @param p Input string
 * @param endp Pointer to store end position (can be NULL)
 * @param base Number base (0 for auto-detect, 2-36)
 * @return Converted value
 */
int128_t dap_strtoi128(const char *p, char **endp, int base);

/**
 * @brief Convert string to unsigned 128-bit integer (base 10)
 * @param p Input string
 * @return Converted value
 */
static inline uint128_t dap_atou128(const char *p) {
    return dap_strtou128(p, (char**)NULL, 10);
}

/**
 * @brief Convert string to signed 128-bit integer (base 10)
 * @param p Input string
 * @return Converted value
 */
static inline int128_t dap_atoi128(const char *p) {
    return dap_strtoi128(p, (char**)NULL, 10);
}

/**
 * @brief Convert unsigned 128-bit integer to string
 * @param dest Output buffer (must be at least 129 bytes for base 2)
 * @param v Value to convert
 * @param base Number base (2-36)
 * @return Pointer to dest
 */
char *dap_utoa128(char *dest, uint128_t v, int base);

/**
 * @brief Convert signed 128-bit integer to string
 * @param a_str Output buffer
 * @param a_value Value to convert
 * @param a_base Number base (2-36)
 * @return Pointer to a_str
 */
char *dap_itoa128(char *a_str, int128_t a_value, int a_base);

#endif // DAP_GLOBAL_IS_INT128

#ifdef __cplusplus
}
#endif
