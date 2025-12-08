/**
 * @file test_trans_helpers.h
 * @brief Common helper functions for trans integration tests
 * @date 2025-11-01
 * @copyright (c) 2025 Cellframe Network
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include "dap_common.h"
#include "dap_client.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream.h"
#include "dap_net_trans.h"
#include "dap_net_trans_server.h"
#include "dap_cert.h"  // For dap_stream_node_addr_from_cert
#include "dap_test.h"
#include "dap_test_async.h"

// Forward declaration of trans config structure
typedef struct trans_test_config {
    dap_net_trans_type_t trans_type;
    const char *name;
    uint16_t base_port;
    const char *address;
} trans_test_config_t;

// External references to trans configs (defined in test_trans_integration.c)
extern const trans_test_config_t g_trans_configs[];
// Count is defined as macro in test_trans_integration.c for compile-time use
// For runtime use, use g_trans_config_count
extern const size_t g_trans_config_count;

/**
 * @brief Create test data for stream testing
 * @param a_size Size of test data to create
 * @return Pointer to allocated test data (caller must free with DAP_DELETE)
 */
uint8_t *test_trans_create_test_data(size_t a_size);

/**
 * @brief Verify stream data integrity
 * @param a_sent Original sent data
 * @param a_received Received data
 * @param a_size Size of data to compare
 * @return true if data matches, false otherwise
 */
bool test_trans_verify_data(const void *a_sent, const void *a_received, size_t a_size);

/**
 * @brief Default test data size for stream tests
 */
#define TEST_TRANS_STREAM_DATA_SIZE 1024

/**
 * @brief Default test server address
 */
#define TEST_TRANS_SERVER_ADDR "127.0.0.1"

/**
 * @brief Default test server ports for different transs
 */
#define TEST_WEBSOCKET_SERVER_PORT 18100
#define TEST_HTTP_SERVER_PORT      18101
#define TEST_UDP_SERVER_PORT       18102
#define TEST_DNS_SERVER_PORT       18103

/**
 * @brief Default test timeout in seconds
 */
#define TEST_TRANS_TIMEOUT_SEC 60

// ============================================================================
// Stream Channel Test Helpers
// ============================================================================

/**
 * @brief Test ctx for stream channel data exchange
 */
typedef struct test_stream_ch_ctx {
    uint8_t *sent_data;
    size_t sent_data_size;
    uint8_t *received_data;
    size_t received_data_size;
    bool data_received;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char channel_id;
    uint8_t packet_type;
} test_stream_ch_ctx_t;

/**
 * @brief Initialize stream channel test ctx
 * @param a_ctx Context to initialize
 * @param a_channel_id Channel ID (e.g., 'A', 'B', 'C')
 * @param a_data_size Size of test data
 * @return 0 on success, -1 on failure
 */
int test_stream_ch_ctx_init(test_stream_ch_ctx_t *a_ctx, 
                                               char a_channel_id, 
                                 size_t a_data_size);

/**
 * @brief Cleanup stream channel test ctx
 * @param a_ctx Context to cleanup
 */
void test_stream_ch_ctx_cleanup(test_stream_ch_ctx_t *a_ctx);

/**
 * @brief Callback for receiving data through stream channel
 * @param a_ch Channel that received the packet
 * @param a_type Packet type
 * @param a_data Packet data
 * @param a_data_size Packet data size
 * @param a_arg Context pointer (test_stream_ch_ctx_t*)
 */
void test_stream_ch_receive_callback(dap_stream_ch_t *a_ch, 
                                                    uint8_t a_type,
                                                    const void *a_data, 
                                                    size_t a_data_size, 
                                      void *a_arg);

/**
 * @brief Wait for client to reach STAGE_STREAM_STREAMING
 * @param a_client Client to wait for
 * @param a_timeout_ms Timeout in milliseconds
 * @return true if streaming stage reached, false on timeout
 */
bool test_trans_wait_for_streaming(dap_client_t *a_client, uint32_t a_timeout_ms);

/**
 * @brief Send data through stream channel and wait for response
 * @param a_client Client to send data through
 * @param a_ctx Test ctx (contains sent/received data)
 * @param a_timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on failure
 */
int test_stream_ch_send_and_wait(dap_client_t *a_client,
                                                test_stream_ch_ctx_t *a_ctx,
                                  uint32_t a_timeout_ms);

/**
 * @brief Add notifier directly to channel (bypassing address lookup)
 */
int test_stream_ch_add_notifier_direct(dap_stream_ch_t *a_ch, 
                                        dap_stream_ch_notify_callback_t a_callback, 
                                        void *a_arg);

/**
 * @brief Register notifier for receiving data on a channel
 * @param a_client Client to register notifier for
 * @param a_channel_id Channel ID
 * @param a_callback_arg Context pointer (test_stream_ch_ctx_t*)
 * @return 0 on success, -1 on failure
 */
int test_stream_ch_register_receiver(dap_client_t *a_client,
                                                    char a_channel_id,
                                      void *a_callback_arg);

// ============================================================================
// Intelligent Waiting Functions (implemented in test_trans_helpers.c)
// ============================================================================

/**
 * @brief Wait for transs to be registered
 * @param a_timeout_ms Timeout in milliseconds
 * @return true if all transs registered, false on timeout
 */
bool test_wait_for_transs_registered(uint32_t a_timeout_ms);

/**
 * @brief Wait for server to be ready (listening)
 * @param a_server Server instance
 * @param a_timeout_ms Timeout in milliseconds
 * @return true if server is ready, false on timeout
 */
bool test_wait_for_server_ready(dap_net_trans_server_t *a_server, uint32_t a_timeout_ms);

/**
 * @brief Wait for stream channels to be created
 * @param a_client Client to check
 * @param a_expected_channels Expected channel IDs (e.g., "ABC")
 * @param a_timeout_ms Timeout in milliseconds
 * @return true if channels are ready, false on timeout
 */
bool test_wait_for_stream_channels_ready(dap_client_t *a_client, const char *a_expected_channels, uint32_t a_timeout_ms);

/**
 * @brief Wait for client to be deleted
 * @param a_client_ptr Pointer to client pointer (will be set to NULL after deletion)
 * @param a_timeout_ms Timeout in milliseconds
 * @return true if client deleted, false on timeout
 */
bool test_wait_for_client_deleted(dap_client_t **a_client_ptr, uint32_t a_timeout_ms);

/**
 * @brief Wait for all streams to be closed
 * @param a_timeout_ms Timeout in milliseconds
 * @return true if all streams closed, false on timeout
 */
bool test_wait_for_all_streams_closed(uint32_t a_timeout_ms);
