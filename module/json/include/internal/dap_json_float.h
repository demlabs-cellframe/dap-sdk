/*
 * Authors:
 * Daniel Lemire et al. (original C++ fast_float library)
 * DAP SDK Team (C port)
 * Copyright (c) 2026
 * All rights reserved.
 */

/**
 * @file dap_json_float.h
 * @brief Lemire's algorithm - Fast double parsing
 * @details Eisel-Lemire algorithm for high-performance double parsing
 * @date 2026-01-13
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse double using Lemire's algorithm
 * @details High-performance double parsing with exact rounding
 * 
 * Performance: ~20-40ns per number (vs ~100-200ns strtod) → 5-10x faster!
 * 
 * @param[in] a_str Input string (NOT null-terminated)
 * @param[in] a_len String length
 * @param[out] a_out_value Parsed double value
 * @return true if successful, false on error
 */
bool dap_json_float_parse(const char *a_str, size_t a_len, double *a_out_value);

#ifdef __cplusplus
}
#endif
