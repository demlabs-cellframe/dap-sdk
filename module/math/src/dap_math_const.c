/**
 * @file dap_math_const.c
 * @brief Constants for 128/256/512-bit integer types
 *
 * Copyright (c) 2017-2026 Demlabs
 * License: GNU GPL v3
 */

#include "dap_math_ops.h"

#ifndef DAP_GLOBAL_IS_INT128
const uint128_t uint128_0 = {};
const uint128_t uint128_1 = {.hi = 0, .lo = 1};
const uint128_t uint128_max = {.hi = UINT64_MAX, .lo = UINT64_MAX};

const uint256_t uint256_0 = {};
const uint256_t uint256_1 = {.hi = {.lo = 0, .hi = 0}, .lo = {.lo = 1, .hi = 0}};
const uint256_t uint256_max = {.hi = {.lo = UINT64_MAX, .hi = UINT64_MAX}, .lo = {.lo = UINT64_MAX, .hi = UINT64_MAX}};
#else // DAP_GLOBAL_IS_INT128
const uint128_t uint128_0 = 0;
const uint128_t uint128_1 = 1;
const uint128_t uint128_max = ((uint128_t)((int128_t)-1L));

const uint256_t uint256_0 = {};
const uint256_t uint256_1 = {.hi = uint128_0, .lo = uint128_1};
const uint256_t uint256_max = {.hi = uint128_max, .lo = uint128_max};
#endif // DAP_GLOBAL_IS_INT128

const uint512_t uint512_0 = {};
