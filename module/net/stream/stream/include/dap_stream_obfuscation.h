/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Contributors:
 * Copyright (c) 2017-2025 Demlabs Ltd <https://demlabs.net>
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

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file dap_stream_obfuscation.h
 * @brief DAP Stream Obfuscation Engine - Core Interface
 * 
 * This module provides the core obfuscation engine for DAP Stream protocol,
 * designed to bypass Deep Packet Inspection (DPI) and traffic analysis.
 * 
 * **Obfuscation Techniques:**
 * 1. **Traffic Padding** - Adding random padding to obscure real data patterns
 * 2. **Protocol Mimicry** - Making traffic look like legitimate protocols (HTTPS, HTTP/2)
 * 3. **Timing Obfuscation** - Randomizing packet timing to break flow patterns
 * 4. **Magic Number Polymorphism** - Dynamic protocol identifiers per session
 * 5. **Traffic Mixing** - Injecting artificial traffic to mask real communication
 * 
 * **Architecture:**
 * ```
 * +---------------------------+
 * | Application Data          |
 * +---------------------------+
 *            ↓
 * +---------------------------+
 * | DAP Stream                |
 * +---------------------------+
 *            ↓
 * +---------------------------+
 * | Obfuscation Engine        | ← This module
 * | - Padding                 |
 * | - Mimicry                 |
 * | - Timing                  |
 * +---------------------------+
 *            ↓
 * +---------------------------+
 * | Transport Layer           |
 * | (HTTP/UDP/WebSocket)      |
 * +---------------------------+
 *            ↓
 * +---------------------------+
 * | Network                   |
 * +---------------------------+
 * ```
 * 
 * **Security Considerations:**
 * - All obfuscation must be cryptographically secure
 * - Random number generation must use CSPRNG
 * - Timing patterns must not leak information
 * - Padding must be indistinguishable from real data
 * 
 * @see dap_stream_obfuscation_padding.h
 * @see dap_stream_obfuscation_mimicry.h
 * @see dap_stream_obfuscation_timing.h
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Obfuscation technique types
 */
typedef enum dap_stream_obfuscation_type {
    DAP_STREAM_OBFS_NONE        = 0x00,  ///< No obfuscation
    DAP_STREAM_OBFS_PADDING     = 0x01,  ///< Traffic padding
    DAP_STREAM_OBFS_MIMICRY     = 0x02,  ///< Protocol mimicry
    DAP_STREAM_OBFS_TIMING      = 0x04,  ///< Timing obfuscation
    DAP_STREAM_OBFS_POLYMORPHIC = 0x08,  ///< Polymorphic magic numbers
    DAP_STREAM_OBFS_MIXING      = 0x10,  ///< Traffic mixing
    DAP_STREAM_OBFS_ALL         = 0x1F   ///< All techniques
} dap_stream_obfuscation_type_t;

/**
 * @brief Obfuscation level (intensity)
 */
typedef enum dap_stream_obfuscation_level {
    DAP_STREAM_OBFS_LEVEL_NONE      = 0,  ///< No obfuscation
    DAP_STREAM_OBFS_LEVEL_LOW       = 1,  ///< Light obfuscation (low overhead)
    DAP_STREAM_OBFS_LEVEL_MEDIUM    = 2,  ///< Moderate obfuscation (balanced)
    DAP_STREAM_OBFS_LEVEL_HIGH      = 3,  ///< Strong obfuscation (high overhead)
    DAP_STREAM_OBFS_LEVEL_PARANOID  = 4   ///< Maximum obfuscation (very high overhead)
} dap_stream_obfuscation_level_t;

/**
 * @brief Obfuscation configuration
 */
typedef struct dap_stream_obfuscation_config {
    uint32_t enabled_techniques;        ///< Bitmask of enabled techniques
    dap_stream_obfuscation_level_t level;  ///< Obfuscation intensity level
    
    // Padding configuration
    struct {
        size_t min_padding;             ///< Minimum padding size (bytes)
        size_t max_padding;             ///< Maximum padding size (bytes)
        float padding_probability;      ///< Probability of adding padding (0.0-1.0)
    } padding;
    
    // Timing configuration
    struct {
        uint32_t min_delay_ms;          ///< Minimum delay between packets (ms)
        uint32_t max_delay_ms;          ///< Maximum delay between packets (ms)
        bool randomize_burst_size;      ///< Randomize burst sizes
    } timing;
    
    // Mixing configuration
    struct {
        uint32_t artificial_traffic_rate;  ///< Fake traffic rate (bytes/sec)
        uint32_t min_packet_size;       ///< Min fake packet size
        uint32_t max_packet_size;       ///< Max fake packet size
    } mixing;
    
    // Mimicry configuration
    struct {
        const char *target_protocol;    ///< Protocol to mimic ("https", "http2", etc.)
        bool emulate_browser;           ///< Emulate browser fingerprint
    } mimicry;
    
} dap_stream_obfuscation_config_t;

/**
 * @brief Obfuscation engine instance
 */
typedef struct dap_stream_obfuscation dap_stream_obfuscation_t;

/**
 * @brief Obfuscation operation callbacks
 */
typedef struct dap_stream_obfuscation_ops {
    /**
     * @brief Apply obfuscation to outgoing data
     * 
     * @param a_obfs Obfuscation instance
     * @param a_data Input data
     * @param a_data_size Input data size
     * @param[out] a_out_data Obfuscated output data (caller must free)
     * @param[out] a_out_size Obfuscated output size
     * @return 0 on success, negative error code on failure
     */
    int (*obfuscate)(dap_stream_obfuscation_t *a_obfs,
                     const void *a_data, size_t a_data_size,
                     void **a_out_data, size_t *a_out_size);
    
    /**
     * @brief Remove obfuscation from incoming data
     * 
     * @param a_obfs Obfuscation instance
     * @param a_data Obfuscated input data
     * @param a_data_size Obfuscated input size
     * @param[out] a_out_data Deobfuscated output data (caller must free)
     * @param[out] a_out_size Deobfuscated output size
     * @return 0 on success, negative error code on failure
     */
    int (*deobfuscate)(dap_stream_obfuscation_t *a_obfs,
                       const void *a_data, size_t a_data_size,
                       void **a_out_data, size_t *a_out_size);
    
    /**
     * @brief Generate artificial traffic
     * 
     * @param a_obfs Obfuscation instance
     * @param[out] a_fake_data Generated fake data (caller must free)
     * @param[out] a_fake_size Generated fake data size
     * @return 0 on success, negative error code on failure
     */
    int (*generate_fake_traffic)(dap_stream_obfuscation_t *a_obfs,
                                 void **a_fake_data, size_t *a_fake_size);
    
    /**
     * @brief Calculate optimal delay for next packet
     * 
     * @param a_obfs Obfuscation instance
     * @return Delay in milliseconds
     */
    uint32_t (*calc_delay)(dap_stream_obfuscation_t *a_obfs);
    
} dap_stream_obfuscation_ops_t;

/**
 * @brief Obfuscation engine instance structure
 */
struct dap_stream_obfuscation {
    dap_stream_obfuscation_config_t config;  ///< Configuration
    const dap_stream_obfuscation_ops_t *ops; ///< Operations
    void *internal;                          ///< Internal state
    
    // Statistics
    struct {
        uint64_t packets_obfuscated;
        uint64_t packets_deobfuscated;
        uint64_t fake_packets_generated;
        uint64_t bytes_padding_added;
        uint64_t bytes_fake_traffic;
    } stats;
};

//=============================================================================
// Public API Functions
//=============================================================================

/**
 * @brief Create obfuscation engine with default configuration
 * 
 * Creates a new obfuscation engine instance with sensible defaults:
 * - Level: MEDIUM
 * - Techniques: PADDING | TIMING | MIXING
 * - Padding: 16-256 bytes, 70% probability
 * - Timing: 10-50ms delay
 * - Mixing: 1KB/sec fake traffic
 * 
 * @return New obfuscation engine instance or NULL on failure
 */
dap_stream_obfuscation_t *dap_stream_obfuscation_create(void);

/**
 * @brief Create obfuscation engine with custom configuration
 * 
 * @param a_config Custom configuration
 * @return New obfuscation engine instance or NULL on failure
 */
dap_stream_obfuscation_t *dap_stream_obfuscation_create_with_config(
    const dap_stream_obfuscation_config_t *a_config);

/**
 * @brief Destroy obfuscation engine
 * 
 * @param a_obfs Obfuscation engine instance
 */
void dap_stream_obfuscation_destroy(dap_stream_obfuscation_t *a_obfs);

/**
 * @brief Apply obfuscation to data
 * 
 * Applies all enabled obfuscation techniques to outgoing data.
 * The output buffer is allocated and must be freed by caller.
 * 
 * @param a_obfs Obfuscation engine instance
 * @param a_data Input data
 * @param a_data_size Input data size
 * @param[out] a_out_data Obfuscated output (caller must free with DAP_DELETE)
 * @param[out] a_out_size Obfuscated output size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_obfuscation_apply(dap_stream_obfuscation_t *a_obfs,
                                  const void *a_data, size_t a_data_size,
                                  void **a_out_data, size_t *a_out_size);

/**
 * @brief Remove obfuscation from data
 * 
 * Removes obfuscation from incoming data to recover original payload.
 * The output buffer is allocated and must be freed by caller.
 * 
 * @param a_obfs Obfuscation engine instance
 * @param a_data Obfuscated input data
 * @param a_data_size Obfuscated input size
 * @param[out] a_out_data Deobfuscated output (caller must free with DAP_DELETE)
 * @param[out] a_out_size Deobfuscated output size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_obfuscation_remove(dap_stream_obfuscation_t *a_obfs,
                                   const void *a_data, size_t a_data_size,
                                   void **a_out_data, size_t *a_out_size);

/**
 * @brief Generate artificial traffic packet
 * 
 * Generates a fake traffic packet to mix with real communication.
 * Used to obfuscate traffic patterns and prevent flow analysis.
 * 
 * @param a_obfs Obfuscation engine instance
 * @param[out] a_fake_data Generated fake data (caller must free)
 * @param[out] a_fake_size Generated fake data size
 * @return 0 on success, negative error code on failure
 */
int dap_stream_obfuscation_generate_fake_traffic(dap_stream_obfuscation_t *a_obfs,
                                                  void **a_fake_data,
                                                  size_t *a_fake_size);

/**
 * @brief Calculate optimal packet delay
 * 
 * Calculates the optimal delay before sending next packet
 * based on timing obfuscation settings.
 * 
 * @param a_obfs Obfuscation engine instance
 * @return Delay in milliseconds
 */
uint32_t dap_stream_obfuscation_calc_delay(dap_stream_obfuscation_t *a_obfs);

/**
 * @brief Enable/disable specific obfuscation technique
 * 
 * @param a_obfs Obfuscation engine instance
 * @param a_technique Technique to enable/disable
 * @param a_enable true to enable, false to disable
 * @return 0 on success, negative error code on failure
 */
int dap_stream_obfuscation_set_technique(dap_stream_obfuscation_t *a_obfs,
                                          dap_stream_obfuscation_type_t a_technique,
                                          bool a_enable);

/**
 * @brief Set obfuscation level
 * 
 * @param a_obfs Obfuscation engine instance
 * @param a_level New obfuscation level
 * @return 0 on success, negative error code on failure
 */
int dap_stream_obfuscation_set_level(dap_stream_obfuscation_t *a_obfs,
                                      dap_stream_obfuscation_level_t a_level);

/**
 * @brief Get current configuration
 * 
 * @param a_obfs Obfuscation engine instance
 * @param[out] a_config Configuration structure to fill
 * @return 0 on success, negative error code on failure
 */
int dap_stream_obfuscation_get_config(dap_stream_obfuscation_t *a_obfs,
                                       dap_stream_obfuscation_config_t *a_config);

/**
 * @brief Update configuration
 * 
 * @param a_obfs Obfuscation engine instance
 * @param a_config New configuration
 * @return 0 on success, negative error code on failure
 */
int dap_stream_obfuscation_set_config(dap_stream_obfuscation_t *a_obfs,
                                       const dap_stream_obfuscation_config_t *a_config);

/**
 * @brief Get statistics
 * 
 * @param a_obfs Obfuscation engine instance
 * @return Pointer to statistics structure (read-only)
 */
const void *dap_stream_obfuscation_get_stats(dap_stream_obfuscation_t *a_obfs);

/**
 * @brief Reset statistics
 * 
 * @param a_obfs Obfuscation engine instance
 * @return 0 on success, negative error code on failure
 */
int dap_stream_obfuscation_reset_stats(dap_stream_obfuscation_t *a_obfs);

/**
 * @brief Create default configuration for given level
 * 
 * Helper function to create a configuration with sensible defaults
 * for a specific obfuscation level.
 * 
 * @param a_level Obfuscation level
 * @return Configuration structure
 */
dap_stream_obfuscation_config_t dap_stream_obfuscation_config_for_level(
    dap_stream_obfuscation_level_t a_level);

#ifdef __cplusplus
}
#endif

