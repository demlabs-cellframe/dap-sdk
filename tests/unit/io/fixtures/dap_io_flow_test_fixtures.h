/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief IO Flow test tier enumeration
 */
typedef enum {
    IO_FLOW_TIER_APPLICATION = 0,   // Pure application-level flow control
    IO_FLOW_TIER_CBPF,              // Classic BPF (socket filters)
    IO_FLOW_TIER_EBPF,              // Extended BPF (full eBPF)
    IO_FLOW_TIER_COUNT
} dap_io_flow_test_tier_t;

/**
 * @brief Test context for IO flow tier tests
 */
typedef struct dap_io_flow_test_context {
    dap_io_flow_test_tier_t tier;
    bool initialized;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t packets_sent;
    uint64_t packets_received;
    uint64_t errors;
    void *user_data;
} dap_io_flow_test_context_t;

/**
 * @brief Initialize test fixtures
 * @return 0 on success, negative on error
 */
int dap_io_flow_test_fixtures_init(void);

/**
 * @brief Cleanup test fixtures
 */
void dap_io_flow_test_fixtures_deinit(void);

/**
 * @brief Create a test context for specific tier
 * @param a_tier Tier to test
 * @return Pointer to context or NULL on error
 */
dap_io_flow_test_context_t *dap_io_flow_test_context_create(dap_io_flow_test_tier_t a_tier);

/**
 * @brief Delete test context
 * @param a_ctx Context to delete
 */
void dap_io_flow_test_context_delete(dap_io_flow_test_context_t *a_ctx);

/**
 * @brief Check if tier is available on current platform
 * @param a_tier Tier to check
 * @return true if available
 */
bool dap_io_flow_test_tier_available(dap_io_flow_test_tier_t a_tier);

/**
 * @brief Get tier name as string
 * @param a_tier Tier
 * @return String name
 */
const char *dap_io_flow_test_tier_name(dap_io_flow_test_tier_t a_tier);

/**
 * @brief Run basic send/receive test for tier
 * @param a_ctx Test context
 * @param a_data_size Size of data to transfer
 * @return 0 on success
 */
int dap_io_flow_test_basic_transfer(dap_io_flow_test_context_t *a_ctx, size_t a_data_size);

/**
 * @brief Run throughput test for tier
 * @param a_ctx Test context
 * @param a_duration_ms Test duration in milliseconds
 * @param a_throughput_out Output: bytes per second
 * @return 0 on success
 */
int dap_io_flow_test_throughput(dap_io_flow_test_context_t *a_ctx, 
                                 uint32_t a_duration_ms,
                                 uint64_t *a_throughput_out);

/**
 * @brief Check if eBPF capabilities are available
 * @return true if CAP_BPF or CAP_SYS_ADMIN is available
 */
bool dap_io_flow_test_ebpf_capable(void);
