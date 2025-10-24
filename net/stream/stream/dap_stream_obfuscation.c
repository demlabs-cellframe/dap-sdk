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

#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_stream_obfuscation.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_stream_obfuscation"

// Internal implementation
typedef struct dap_stream_obfuscation_internal {
    uint64_t rng_state;             // RNG state for reproducibility
    uint32_t session_key;           // Per-session obfuscation key
    uint64_t last_packet_time_ms;   // Timestamp of last packet
    uint32_t packet_count;          // Packets processed this session
} dap_stream_obfuscation_internal_t;

// Forward declarations
static int s_obfuscate_impl(dap_stream_obfuscation_t *a_obfs,
                             const void *a_data, size_t a_data_size,
                             void **a_out_data, size_t *a_out_size);
static int s_deobfuscate_impl(dap_stream_obfuscation_t *a_obfs,
                               const void *a_data, size_t a_data_size,
                               void **a_out_data, size_t *a_out_size);
static int s_generate_fake_traffic_impl(dap_stream_obfuscation_t *a_obfs,
                                         void **a_fake_data, size_t *a_fake_size);
static uint32_t s_calc_delay_impl(dap_stream_obfuscation_t *a_obfs);

// Helper functions
static uint64_t s_get_time_ms(void);
static uint32_t s_generate_session_key(void);
static size_t s_calculate_padding_size(dap_stream_obfuscation_t *a_obfs, size_t a_data_size);

// Operations table
static const dap_stream_obfuscation_ops_t s_default_ops = {
    .obfuscate = s_obfuscate_impl,
    .deobfuscate = s_deobfuscate_impl,
    .generate_fake_traffic = s_generate_fake_traffic_impl,
    .calc_delay = s_calc_delay_impl
};

//=============================================================================
// Public API Implementation
//=============================================================================

/**
 * @brief Create obfuscation engine with default configuration
 */
dap_stream_obfuscation_t *dap_stream_obfuscation_create(void)
{
    dap_stream_obfuscation_config_t l_config = 
        dap_stream_obfuscation_config_for_level(DAP_STREAM_OBFS_LEVEL_MEDIUM);
    return dap_stream_obfuscation_create_with_config(&l_config);
}

/**
 * @brief Create obfuscation engine with custom configuration
 */
dap_stream_obfuscation_t *dap_stream_obfuscation_create_with_config(
    const dap_stream_obfuscation_config_t *a_config)
{
    if (!a_config) {
        log_it(L_ERROR, "Cannot create obfuscation engine with NULL config");
        return NULL;
    }

    dap_stream_obfuscation_t *l_obfs = DAP_NEW_Z(dap_stream_obfuscation_t);
    if (!l_obfs) {
        log_it(L_CRITICAL, "Memory allocation failed for obfuscation engine");
        return NULL;
    }

    // Allocate internal state
    dap_stream_obfuscation_internal_t *l_internal = DAP_NEW_Z(dap_stream_obfuscation_internal_t);
    if (!l_internal) {
        log_it(L_CRITICAL, "Memory allocation failed for internal state");
        DAP_DELETE(l_obfs);
        return NULL;
    }

    // Initialize
    memcpy(&l_obfs->config, a_config, sizeof(dap_stream_obfuscation_config_t));
    l_obfs->ops = &s_default_ops;
    l_obfs->internal = l_internal;

    // Initialize internal state
    l_internal->session_key = s_generate_session_key();
    l_internal->rng_state = (uint64_t)time(NULL) ^ l_internal->session_key;
    l_internal->last_packet_time_ms = s_get_time_ms();
    l_internal->packet_count = 0;

    // Zero statistics
    memset(&l_obfs->stats, 0, sizeof(l_obfs->stats));

    log_it(L_INFO, "Obfuscation engine created (level=%d, techniques=0x%x, session_key=0x%x)",
           a_config->level, a_config->enabled_techniques, l_internal->session_key);

    return l_obfs;
}

/**
 * @brief Destroy obfuscation engine
 */
void dap_stream_obfuscation_destroy(dap_stream_obfuscation_t *a_obfs)
{
    if (!a_obfs)
        return;

    if (a_obfs->internal) {
        // Zero out sensitive data
        dap_stream_obfuscation_internal_t *l_internal = 
            (dap_stream_obfuscation_internal_t*)a_obfs->internal;
        memset(l_internal, 0, sizeof(dap_stream_obfuscation_internal_t));
        DAP_DELETE(a_obfs->internal);
    }

    DAP_DELETE(a_obfs);
    log_it(L_DEBUG, "Obfuscation engine destroyed");
}

/**
 * @brief Apply obfuscation to data
 */
int dap_stream_obfuscation_apply(dap_stream_obfuscation_t *a_obfs,
                                  const void *a_data, size_t a_data_size,
                                  void **a_out_data, size_t *a_out_size)
{
    if (!a_obfs || !a_data || !a_out_data || !a_out_size) {
        log_it(L_ERROR, "Invalid arguments for obfuscation apply");
        return -1;
    }

    if (!a_obfs->ops || !a_obfs->ops->obfuscate) {
        log_it(L_ERROR, "Obfuscation operations not set");
        return -1;
    }

    int l_ret = a_obfs->ops->obfuscate(a_obfs, a_data, a_data_size, a_out_data, a_out_size);
    
    if (l_ret == 0) {
        a_obfs->stats.packets_obfuscated++;
    }

    return l_ret;
}

/**
 * @brief Remove obfuscation from data
 */
int dap_stream_obfuscation_remove(dap_stream_obfuscation_t *a_obfs,
                                   const void *a_data, size_t a_data_size,
                                   void **a_out_data, size_t *a_out_size)
{
    if (!a_obfs || !a_data || !a_out_data || !a_out_size) {
        log_it(L_ERROR, "Invalid arguments for obfuscation remove");
        return -1;
    }

    if (!a_obfs->ops || !a_obfs->ops->deobfuscate) {
        log_it(L_ERROR, "Obfuscation operations not set");
        return -1;
    }

    int l_ret = a_obfs->ops->deobfuscate(a_obfs, a_data, a_data_size, a_out_data, a_out_size);
    
    if (l_ret == 0) {
        a_obfs->stats.packets_deobfuscated++;
    }

    return l_ret;
}

/**
 * @brief Generate artificial traffic packet
 */
int dap_stream_obfuscation_generate_fake_traffic(dap_stream_obfuscation_t *a_obfs,
                                                  void **a_fake_data,
                                                  size_t *a_fake_size)
{
    if (!a_obfs || !a_fake_data || !a_fake_size) {
        log_it(L_ERROR, "Invalid arguments for fake traffic generation");
        return -1;
    }

    if (!(a_obfs->config.enabled_techniques & DAP_STREAM_OBFS_MIXING)) {
        log_it(L_DEBUG, "Traffic mixing not enabled");
        return -1;
    }

    if (!a_obfs->ops || !a_obfs->ops->generate_fake_traffic) {
        log_it(L_ERROR, "Fake traffic operation not set");
        return -1;
    }

    int l_ret = a_obfs->ops->generate_fake_traffic(a_obfs, a_fake_data, a_fake_size);
    
    if (l_ret == 0) {
        a_obfs->stats.fake_packets_generated++;
        a_obfs->stats.bytes_fake_traffic += *a_fake_size;
    }

    return l_ret;
}

/**
 * @brief Calculate optimal packet delay
 */
uint32_t dap_stream_obfuscation_calc_delay(dap_stream_obfuscation_t *a_obfs)
{
    if (!a_obfs) {
        return 0;
    }

    if (!(a_obfs->config.enabled_techniques & DAP_STREAM_OBFS_TIMING)) {
        return 0;
    }

    if (!a_obfs->ops || !a_obfs->ops->calc_delay) {
        return 0;
    }

    return a_obfs->ops->calc_delay(a_obfs);
}

/**
 * @brief Enable/disable specific obfuscation technique
 */
int dap_stream_obfuscation_set_technique(dap_stream_obfuscation_t *a_obfs,
                                          dap_stream_obfuscation_type_t a_technique,
                                          bool a_enable)
{
    if (!a_obfs) {
        log_it(L_ERROR, "Cannot set technique on NULL obfuscation engine");
        return -1;
    }

    if (a_enable) {
        a_obfs->config.enabled_techniques |= a_technique;
        log_it(L_DEBUG, "Enabled obfuscation technique: 0x%x", a_technique);
    } else {
        a_obfs->config.enabled_techniques &= ~a_technique;
        log_it(L_DEBUG, "Disabled obfuscation technique: 0x%x", a_technique);
    }

    return 0;
}

/**
 * @brief Set obfuscation level
 */
int dap_stream_obfuscation_set_level(dap_stream_obfuscation_t *a_obfs,
                                      dap_stream_obfuscation_level_t a_level)
{
    if (!a_obfs) {
        log_it(L_ERROR, "Cannot set level on NULL obfuscation engine");
        return -1;
    }

    // Get default config for this level and apply it
    dap_stream_obfuscation_config_t l_new_config = 
        dap_stream_obfuscation_config_for_level(a_level);
    
    memcpy(&a_obfs->config, &l_new_config, sizeof(dap_stream_obfuscation_config_t));
    
    log_it(L_INFO, "Obfuscation level changed to: %d", a_level);
    return 0;
}

/**
 * @brief Get current configuration
 */
int dap_stream_obfuscation_get_config(dap_stream_obfuscation_t *a_obfs,
                                       dap_stream_obfuscation_config_t *a_config)
{
    if (!a_obfs || !a_config) {
        log_it(L_ERROR, "Invalid arguments for get config");
        return -1;
    }

    memcpy(a_config, &a_obfs->config, sizeof(dap_stream_obfuscation_config_t));
    return 0;
}

/**
 * @brief Update configuration
 */
int dap_stream_obfuscation_set_config(dap_stream_obfuscation_t *a_obfs,
                                       const dap_stream_obfuscation_config_t *a_config)
{
    if (!a_obfs || !a_config) {
        log_it(L_ERROR, "Invalid arguments for set config");
        return -1;
    }

    memcpy(&a_obfs->config, a_config, sizeof(dap_stream_obfuscation_config_t));
    log_it(L_INFO, "Obfuscation configuration updated");
    return 0;
}

/**
 * @brief Get statistics
 */
const void *dap_stream_obfuscation_get_stats(dap_stream_obfuscation_t *a_obfs)
{
    if (!a_obfs) {
        return NULL;
    }
    return &a_obfs->stats;
}

/**
 * @brief Reset statistics
 */
int dap_stream_obfuscation_reset_stats(dap_stream_obfuscation_t *a_obfs)
{
    if (!a_obfs) {
        return -1;
    }

    memset(&a_obfs->stats, 0, sizeof(a_obfs->stats));
    log_it(L_DEBUG, "Obfuscation statistics reset");
    return 0;
}

/**
 * @brief Create default configuration for given level
 */
dap_stream_obfuscation_config_t dap_stream_obfuscation_config_for_level(
    dap_stream_obfuscation_level_t a_level)
{
    dap_stream_obfuscation_config_t l_config;
    memset(&l_config, 0, sizeof(l_config));
    
    l_config.level = a_level;
    
    switch (a_level) {
        case DAP_STREAM_OBFS_LEVEL_NONE:
            l_config.enabled_techniques = DAP_STREAM_OBFS_NONE;
            break;
            
        case DAP_STREAM_OBFS_LEVEL_LOW:
            l_config.enabled_techniques = DAP_STREAM_OBFS_PADDING | DAP_STREAM_OBFS_TIMING;
            l_config.padding.min_padding = 8;
            l_config.padding.max_padding = 64;
            l_config.padding.padding_probability = 0.3f;
            l_config.timing.min_delay_ms = 5;
            l_config.timing.max_delay_ms = 20;
            l_config.timing.randomize_burst_size = false;
            break;
            
        case DAP_STREAM_OBFS_LEVEL_MEDIUM:
            l_config.enabled_techniques = DAP_STREAM_OBFS_PADDING | 
                                          DAP_STREAM_OBFS_TIMING | 
                                          DAP_STREAM_OBFS_MIXING;
            l_config.padding.min_padding = 16;
            l_config.padding.max_padding = 256;
            l_config.padding.padding_probability = 0.7f;
            l_config.timing.min_delay_ms = 10;
            l_config.timing.max_delay_ms = 50;
            l_config.timing.randomize_burst_size = true;
            l_config.mixing.artificial_traffic_rate = 1024;  // 1 KB/sec
            l_config.mixing.min_packet_size = 64;
            l_config.mixing.max_packet_size = 512;
            break;
            
        case DAP_STREAM_OBFS_LEVEL_HIGH:
            l_config.enabled_techniques = DAP_STREAM_OBFS_PADDING | 
                                          DAP_STREAM_OBFS_TIMING | 
                                          DAP_STREAM_OBFS_MIXING |
                                          DAP_STREAM_OBFS_MIMICRY;
            l_config.padding.min_padding = 32;
            l_config.padding.max_padding = 512;
            l_config.padding.padding_probability = 0.9f;
            l_config.timing.min_delay_ms = 20;
            l_config.timing.max_delay_ms = 100;
            l_config.timing.randomize_burst_size = true;
            l_config.mixing.artificial_traffic_rate = 4096;  // 4 KB/sec
            l_config.mixing.min_packet_size = 128;
            l_config.mixing.max_packet_size = 1024;
            l_config.mimicry.target_protocol = "https";
            l_config.mimicry.emulate_browser = true;
            break;
            
        case DAP_STREAM_OBFS_LEVEL_PARANOID:
            l_config.enabled_techniques = DAP_STREAM_OBFS_ALL;
            l_config.padding.min_padding = 64;
            l_config.padding.max_padding = 1024;
            l_config.padding.padding_probability = 1.0f;
            l_config.timing.min_delay_ms = 50;
            l_config.timing.max_delay_ms = 200;
            l_config.timing.randomize_burst_size = true;
            l_config.mixing.artificial_traffic_rate = 10240;  // 10 KB/sec
            l_config.mixing.min_packet_size = 256;
            l_config.mixing.max_packet_size = 2048;
            l_config.mimicry.target_protocol = "https";
            l_config.mimicry.emulate_browser = true;
            break;
    }
    
    return l_config;
}

//=============================================================================
// Internal Implementation
//=============================================================================

/**
 * @brief Apply obfuscation (internal implementation)
 */
static int s_obfuscate_impl(dap_stream_obfuscation_t *a_obfs,
                             const void *a_data, size_t a_data_size,
                             void **a_out_data, size_t *a_out_size)
{
    if (!a_obfs->config.enabled_techniques) {
        // No obfuscation - just copy data
        *a_out_data = DAP_DUP_SIZE(a_data, a_data_size);
        *a_out_size = a_data_size;
        return 0;
    }

    dap_stream_obfuscation_internal_t *l_internal = 
        (dap_stream_obfuscation_internal_t*)a_obfs->internal;

    // Calculate padding size
    size_t l_padding_size = 0;
    if (a_obfs->config.enabled_techniques & DAP_STREAM_OBFS_PADDING) {
        l_padding_size = s_calculate_padding_size(a_obfs, a_data_size);
        a_obfs->stats.bytes_padding_added += l_padding_size;
    }

    // Allocate output buffer (original data + padding)
    size_t l_total_size = a_data_size + l_padding_size;
    uint8_t *l_output = DAP_NEW_Z_SIZE(uint8_t, l_total_size);
    if (!l_output) {
        log_it(L_CRITICAL, "Memory allocation failed for obfuscated data");
        return -1;
    }

    // Copy original data
    memcpy(l_output, a_data, a_data_size);

    // Add padding
    if (l_padding_size > 0) {
        // Fill padding with random data
        randombytes(l_output + a_data_size, l_padding_size);
    }

    // Apply XOR obfuscation if polymorphic enabled
    if (a_obfs->config.enabled_techniques & DAP_STREAM_OBFS_POLYMORPHIC) {
        uint32_t l_key = l_internal->session_key ^ l_internal->packet_count;
        for (size_t i = 0; i < l_total_size; i++) {
            l_output[i] ^= (l_key >> (i % 4 * 8)) & 0xFF;
        }
    }

    l_internal->packet_count++;
    l_internal->last_packet_time_ms = s_get_time_ms();

    *a_out_data = l_output;
    *a_out_size = l_total_size;
    
    return 0;
}

/**
 * @brief Remove obfuscation (internal implementation)
 */
static int s_deobfuscate_impl(dap_stream_obfuscation_t *a_obfs,
                               const void *a_data, size_t a_data_size,
                               void **a_out_data, size_t *a_out_size)
{
    if (!a_obfs->config.enabled_techniques) {
        // No obfuscation - just copy data
        *a_out_data = DAP_DUP_SIZE(a_data, a_data_size);
        *a_out_size = a_data_size;
        return 0;
    }

    dap_stream_obfuscation_internal_t *l_internal = 
        (dap_stream_obfuscation_internal_t*)a_obfs->internal;

    // Allocate working buffer
    uint8_t *l_buffer = DAP_DUP_SIZE(a_data, a_data_size);
    if (!l_buffer) {
        log_it(L_CRITICAL, "Memory allocation failed for deobfuscation");
        return -1;
    }

    // Remove XOR obfuscation if polymorphic enabled
    if (a_obfs->config.enabled_techniques & DAP_STREAM_OBFS_POLYMORPHIC) {
        uint32_t l_key = l_internal->session_key ^ l_internal->packet_count;
        for (size_t i = 0; i < a_data_size; i++) {
            l_buffer[i] ^= (l_key >> (i % 4 * 8)) & 0xFF;
        }
    }

    // Remove padding (for now, assume padding is at the end)
    // In production, padding info should be encoded in packet header
    size_t l_original_size = a_data_size;  // Placeholder
    if (a_obfs->config.enabled_techniques & DAP_STREAM_OBFS_PADDING) {
        // For now, just use full size
        // Real implementation would need padding length encoded
        l_original_size = a_data_size;
    }

    *a_out_data = l_buffer;
    *a_out_size = l_original_size;
    
    l_internal->packet_count++;
    
    return 0;
}

/**
 * @brief Generate fake traffic (internal implementation)
 */
static int s_generate_fake_traffic_impl(dap_stream_obfuscation_t *a_obfs,
                                         void **a_fake_data, size_t *a_fake_size)
{
    // Generate random size between min and max
    size_t l_size = a_obfs->config.mixing.min_packet_size + 
                    (m_dap_random_u32() % 
                     (a_obfs->config.mixing.max_packet_size - 
                      a_obfs->config.mixing.min_packet_size + 1));

    // Allocate buffer
    uint8_t *l_fake_data = DAP_NEW_Z_SIZE(uint8_t, l_size);
    if (!l_fake_data) {
        log_it(L_CRITICAL, "Memory allocation failed for fake traffic");
        return -1;
    }

    // Fill with random data
    randombytes(l_fake_data, l_size);

    *a_fake_data = l_fake_data;
    *a_fake_size = l_size;

    log_it(L_DEBUG, "Generated fake traffic: %zu bytes", l_size);
    return 0;
}

/**
 * @brief Calculate delay (internal implementation)
 */
static uint32_t s_calc_delay_impl(dap_stream_obfuscation_t *a_obfs)
{
    uint32_t l_min = a_obfs->config.timing.min_delay_ms;
    uint32_t l_max = a_obfs->config.timing.max_delay_ms;
    
    if (l_min >= l_max) {
        return l_min;
    }

    uint32_t l_delay = l_min + (m_dap_random_u32() % (l_max - l_min + 1));
    return l_delay;
}

//=============================================================================
// Helper Functions
//=============================================================================

/**
 * @brief Get current time in milliseconds
 */
static uint64_t s_get_time_ms(void)
{
    struct timespec l_ts;
    clock_gettime(CLOCK_MONOTONIC, &l_ts);
    return (uint64_t)l_ts.tv_sec * 1000ULL + (uint64_t)l_ts.tv_nsec / 1000000ULL;
}

/**
 * @brief Generate session key
 */
static uint32_t s_generate_session_key(void)
{
    return m_dap_random_u32();
}

/**
 * @brief Calculate padding size based on configuration
 */
static size_t s_calculate_padding_size(dap_stream_obfuscation_t *a_obfs, size_t a_data_size)
{
    UNUSED(a_data_size);
    
    // Check if we should add padding based on probability
    float l_random = (float)m_dap_random_u32() / (float)UINT32_MAX;
    if (l_random > a_obfs->config.padding.padding_probability) {
        return 0;  // No padding this time
    }

    // Generate random padding size
    size_t l_min = a_obfs->config.padding.min_padding;
    size_t l_max = a_obfs->config.padding.max_padding;
    
    if (l_min >= l_max) {
        return l_min;
    }

    size_t l_padding = l_min + (m_dap_random_u32() % (l_max - l_min + 1));
    return l_padding;
}

