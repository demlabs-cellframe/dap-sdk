/**
 * @file test_io_flow_concurrent.c
 * @brief Unit test for dap_io_flow concurrent sessions handling
 * @details Tests that dap_io_flow correctly isolates multiple concurrent sessions:
 *          - Each session has independent state (keys, sequences, flow data)
 *          - No cross-contamination between sessions
 *          - Correct packet routing to session's native worker
 * 
 * This test was created to debug issue #19980 where multiple UDP clients
 * experienced "Invalid handshake size" errors due to key mixing.
 * 
 * @date 2026-01-11
 * @copyright (c) 2026 Cellframe Network
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "dap_common.h"
#include "dap_config.h"
#include "dap_events.h"
#include "dap_io_flow.h"
#include "dap_io_flow_udp.h"
#include "dap_worker.h"
#include "dap_test.h"
#include "dap_test_helpers.h"

#define LOG_TAG "test_io_flow_concurrent"

// Test configuration
#define TEST_PORT_BASE 19000
#define TEST_NUM_SESSIONS 10  // 10 concurrent sessions

// Session tracking
typedef struct test_session {
    uint32_t session_id;
    struct sockaddr_storage remote_addr;
    uint8_t session_key[32];  // Mock session key
    uint32_t packet_count;
    bool key_contaminated;    // Flag if wrong key was used
} test_session_t;

static test_session_t *g_sessions[TEST_NUM_SESSIONS] = {0};
static _Atomic uint32_t g_packets_received = 0;
static _Atomic uint32_t g_key_errors = 0;

/**
 * @brief Mock flow creation - stores session-specific data
 */
static void* test_flow_create(dap_io_flow_server_t *a_server,
                               const struct sockaddr_storage *a_remote_addr,
                               void *a_flow_data)
{
    // Allocate flow-specific session data
    test_session_t *l_session = DAP_NEW_Z(test_session_t);
    if (!l_session) {
        return NULL;
    }
    
    // Initialize session with unique ID based on remote port
    struct sockaddr_in *l_addr = (struct sockaddr_in*)a_remote_addr;
    l_session->session_id = ntohs(l_addr->sin_port);
    memcpy(&l_session->remote_addr, a_remote_addr, sizeof(*a_remote_addr));
    
    // Generate unique session key based on session_id
    for (int i = 0; i < 32; i++) {
        l_session->session_key[i] = (l_session->session_id + i) & 0xFF;
    }
    
    log_it(L_INFO, "Flow created for session %u (port %u)",
           l_session->session_id, ntohs(l_addr->sin_port));
    
    return l_session;
}

/**
 * @brief Mock flow destruction
 */
static void test_flow_destroy(dap_io_flow_t *a_flow)
{
    if (a_flow && a_flow->flow_data) {
        test_session_t *l_session = (test_session_t*)a_flow->flow_data;
        log_it(L_INFO, "Flow destroyed for session %u (packets: %u)",
               l_session->session_id, l_session->packet_count);
        DAP_DELETE(l_session);
    }
}

/**
 * @brief Mock packet processing - verifies session isolation
 */
static int test_flow_packet(dap_io_flow_t *a_flow,
                             const uint8_t *a_data,
                             size_t a_data_size)
{
    if (!a_flow || !a_flow->flow_data || !a_data || a_data_size < 4) {
        return -1;
    }
    
    test_session_t *l_session = (test_session_t*)a_flow->flow_data;
    
    // Packet format: [session_id:4][key_byte:1][data...]
    uint32_t l_pkt_session_id;
    memcpy(&l_pkt_session_id, a_data, sizeof(l_pkt_session_id));
    l_pkt_session_id = ntohl(l_pkt_session_id);
    
    uint8_t l_pkt_key_byte = a_data[4];
    
    // VERIFY: session_id matches
    if (l_pkt_session_id != l_session->session_id) {
        log_it(L_ERROR, "SESSION MISMATCH! Flow has session %u, packet has %u",
               l_session->session_id, l_pkt_session_id);
        atomic_fetch_add(&g_key_errors, 1);
        return -2;
    }
    
    // VERIFY: key matches (first byte of session key)
    uint8_t l_expected_key_byte = l_session->session_key[0];
    if (l_pkt_key_byte != l_expected_key_byte) {
        log_it(L_ERROR, "KEY MISMATCH! Session %u expects key_byte=0x%02x, got 0x%02x",
               l_session->session_id, l_expected_key_byte, l_pkt_key_byte);
        l_session->key_contaminated = true;
        atomic_fetch_add(&g_key_errors, 1);
        return -3;
    }
    
    l_session->packet_count++;
    atomic_fetch_add(&g_packets_received, 1);
    
    debug_if(true, L_DEBUG, "Session %u: packet %u received correctly (key=0x%02x)",
             l_session->session_id, l_session->packet_count, l_pkt_key_byte);
    
    return 0;
}

static dap_io_flow_ops_t g_test_ops = {
    .flow_create = test_flow_create,
    .flow_destroy = test_flow_destroy,
    .flow_packet_in = test_flow_packet,
};

/**
 * @brief Test: Multiple concurrent sessions with unique keys
 */
static void test_01_concurrent_sessions_isolation(void)
{
    TEST_INFO("Testing session isolation with %d concurrent sessions", TEST_NUM_SESSIONS);
    
    // Create flow server
    dap_io_flow_server_t *l_server = dap_io_flow_server_new_udp(
        "test_concurrent",
        &g_test_ops,
        NULL  // No stream ops for this test
    );
    TEST_ASSERT_NOT_NULL(l_server, "Flow server created");
    
    // Start listener
    int l_ret = dap_io_flow_server_listen(l_server, "127.0.0.1", TEST_PORT_BASE);
    TEST_ASSERT_EQUAL(l_ret, 0, "Server started on port %d", TEST_PORT_BASE);
    
    // Create client sockets for each session
    int l_client_socks[TEST_NUM_SESSIONS];
    struct sockaddr_in l_server_addr;
    memset(&l_server_addr, 0, sizeof(l_server_addr));
    l_server_addr.sin_family = AF_INET;
    l_server_addr.sin_port = htons(TEST_PORT_BASE);
    inet_pton(AF_INET, "127.0.0.1", &l_server_addr.sin_addr);
    
    // Create sessions
    for (int i = 0; i < TEST_NUM_SESSIONS; i++) {
        l_client_socks[i] = socket(AF_INET, SOCK_DGRAM, 0);
        TEST_ASSERT(l_client_socks[i] >= 0, "Client socket %d created", i);
        
        // Bind to specific port to ensure unique session_id
        struct sockaddr_in l_client_addr;
        memset(&l_client_addr, 0, sizeof(l_client_addr));
        l_client_addr.sin_family = AF_INET;
        l_client_addr.sin_addr.s_addr = INADDR_ANY;
        l_client_addr.sin_port = htons(TEST_PORT_BASE + 1000 + i);
        
        l_ret = bind(l_client_socks[i], (struct sockaddr*)&l_client_addr, sizeof(l_client_addr));
        TEST_ASSERT_EQUAL(l_ret, 0, "Client %d bound to port %d", i, TEST_PORT_BASE + 1000 + i);
    }
    
    // Send packets from each session with UNIQUE keys
    const int PACKETS_PER_SESSION = 5;
    for (int round = 0; round < PACKETS_PER_SESSION; round++) {
        for (int sess = 0; sess < TEST_NUM_SESSIONS; sess++) {
            uint32_t l_session_id = TEST_PORT_BASE + 1000 + sess;
            uint8_t l_key_byte = (l_session_id + 0) & 0xFF;  // First byte of session key
            
            uint8_t l_packet[64];
            uint32_t l_sid_be = htonl(l_session_id);
            memcpy(l_packet, &l_sid_be, sizeof(l_sid_be));
            l_packet[4] = l_key_byte;
            snprintf((char*)l_packet + 5, sizeof(l_packet) - 5, 
                     "Session %u packet %d", l_session_id, round);
            
            ssize_t l_sent = sendto(l_client_socks[sess], l_packet, sizeof(l_packet), 0,
                                   (struct sockaddr*)&l_server_addr, sizeof(l_server_addr));
            TEST_ASSERT(l_sent == sizeof(l_packet), "Session %u sent packet %d", sess, round);
        }
        
        // Small delay to allow processing
        usleep(10000);  // 10ms
    }
    
    // Wait for all packets to be processed
    int l_wait_iterations = 0;
    const int MAX_WAIT = 100;  // 10 seconds max
    while (atomic_load(&g_packets_received) < (TEST_NUM_SESSIONS * PACKETS_PER_SESSION) && 
           l_wait_iterations < MAX_WAIT) {
        usleep(100000);  // 100ms
        l_wait_iterations++;
    }
    
    // Check results
    uint32_t l_received = atomic_load(&g_packets_received);
    uint32_t l_errors = atomic_load(&g_key_errors);
    
    TEST_INFO("Results: %u/%u packets received, %u key errors",
              l_received, TEST_NUM_SESSIONS * PACKETS_PER_SESSION, l_errors);
    
    TEST_ASSERT_EQUAL(l_errors, 0, "No key contamination errors");
    TEST_ASSERT_EQUAL(l_received, TEST_NUM_SESSIONS * PACKETS_PER_SESSION, 
                     "All packets received");
    
    // Cleanup
    for (int i = 0; i < TEST_NUM_SESSIONS; i++) {
        close(l_client_socks[i]);
    }
    
    dap_io_flow_server_delete(l_server);
    
    TEST_SUCCESS("Session isolation test passed");
}

int main(void)
{
    // Setup
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    dap_log_level_set(L_DEBUG);
    
    dap_common_init("test_io_flow_concurrent", NULL);
    dap_config_init(".");
    
    // Initialize events (required for workers)
    int l_ret = dap_events_init(0, 60000);
    if (l_ret != 0) {
        printf("Failed to init events: %d\n", l_ret);
        return -1;
    }
    dap_events_start();
    
    TEST_SUITE_START("dap_io_flow Concurrent Sessions");
    
    TEST_RUN(test_01_concurrent_sessions_isolation);
    
    TEST_SUITE_END();
    
    return 0;
}

