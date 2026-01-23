/**
 * @file dap_json_power5.h
 * @brief Runtime generation of normalized power-of-5 table for Eisel-Lemire algorithm
 * @details Generates 128-bit normalized powers of 5 for fast double parsing
 * @date 2026-01-14
 */

#ifndef DAP_JSON_POWER5_H
#define DAP_JSON_POWER5_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Power of 5 table structure
 */
typedef struct {
    int16_t min_exp;        ///< Minimum exponent (-22)
    int16_t max_exp;        ///< Maximum exponent (22)
    uint64_t table[46][2];  ///< [high, low] pairs for 5^-22 to 5^22
} dap_json_power5_table_t;

/**
 * @brief Initialize and generate the power-of-5 table
 * @details Generates normalized 128-bit representations of 5^exp
 *          All entries have MSB of high 64 bits at bit 63
 * @return Pointer to the global power-of-5 table
 */
const dap_json_power5_table_t* dap_json_power5_init(void);

/**
 * @brief Get the power-of-5 table (must be initialized first)
 * @return Pointer to the global power-of-5 table, or NULL if not initialized
 */
const dap_json_power5_table_t* dap_json_power5_get_table(void);

#ifdef __cplusplus
}
#endif

#endif // DAP_JSON_POWER5_H
