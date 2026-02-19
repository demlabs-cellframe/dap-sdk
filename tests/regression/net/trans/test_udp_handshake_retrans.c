/**
 * @file test_udp_handshake_retrans.c
 * @brief Regression test for UDP handshake retransmission
 * 
 * This test verifies that the DAP SDK UDP client correctly retransmits
 * HANDSHAKE packets when no response is received from the server.
 * 
 * Test approach:
 * 1. Create a "fake server" using raw UDP sockets that:
 *    - Receives handshake packets
 *    - Counts them (does NOT respond - to trigger client retransmission)
 * 2. Initialize DAP SDK with real reactor and create a real UDP client
 * 3. Client tries to connect to our fake server
 * 4. Verify that multiple HANDSHAKE packets were received
 *    (proving retransmission mechanism works)
 * 
 * Expected result: The client should send multiple HANDSHAKE packets
 * before eventually timing out.
 * 
 * @date 2026-02-05
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dap_common.h"
#include "dap_time.h"
#include "dap_test.h"
#include "dap_test_helpers.h"
#include "dap_config.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_proc_thread.h"
#include "dap_enc.h"
#include "dap_client.h"
#include "dap_client_pvt.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_net_trans.h"
#include "dap_io_flow.h"
#include "dap_stream_ctl.h"
#include "dap_link_manager.h"
#include "dap_global_db.h"
#include "dap_module.h"
#include "dap_cert.h"
#include "dap_cert_file.h"
#include "dap_io_flow_socket.h"
#include "dap_client_test_fixtures.h"
#include "test_trans_helpers.h"

#define LOG_TAG "test_udp_hs_retrans"

// Required by test_trans_helpers.c but not used in this test
const trans_test_config_t g_trans_configs[] = {{0}};
const size_t g_trans_config_count = 0;

//===================================================================
// CONFIGURATION
//===================================================================

#define TEST_PORT           19394
#define TEST_ADDR           "127.0.0.1"
#define HANDSHAKE_PKT_MIN   600     // Min size for handshake (obfuscated KEM pub key)
#define HANDSHAKE_PKT_MAX   1500    // Max size for handshake (with obfuscation padding)
#define WAIT_FOR_RETRANS_MS 8000    // Wait 8 seconds for retransmissions (timer is 500ms)
#define MIN_EXPECTED_PKTS   2       // Expect at least 2 packets (original + 1 retransmit)

//===================================================================
// FAKE SERVER STATE
//===================================================================

typedef struct {
    int fd;
    uint16_t port;
    _Atomic uint32_t handshake_count;
    _Atomic bool stop_flag;
    _Atomic bool ready_flag;  // Server thread is ready to receive
    pthread_t thread;
} fake_server_t;

static fake_server_t s_fake_server = {0};

//===================================================================
// FAKE SERVER THREAD
//===================================================================

static void *s_fake_server_thread(void *a_arg)
{
    (void)a_arg;
    
    uint8_t l_buf[2048];
    struct sockaddr_in l_client_addr;
    socklen_t l_addr_len;
    
    log_it(L_INFO, "Fake server thread started, listening for handshake packets...");
    
    // Set receive timeout
    struct timeval l_tv = { .tv_sec = 0, .tv_usec = 100000 };  // 100ms
    setsockopt(s_fake_server.fd, SOL_SOCKET, SO_RCVTIMEO, &l_tv, sizeof(l_tv));
    
    // Signal that server is ready
    atomic_store(&s_fake_server.ready_flag, true);
    
    while (!atomic_load(&s_fake_server.stop_flag)) {
        l_addr_len = sizeof(l_client_addr);
        
        ssize_t l_recv = recvfrom(s_fake_server.fd, l_buf, sizeof(l_buf), 0,
                                   (struct sockaddr*)&l_client_addr, &l_addr_len);
        
        if (l_recv < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;  // Timeout, check stop_flag and retry
            }
            if (errno != EINTR) {
                log_it(L_ERROR, "recvfrom failed: %s", strerror(errno));
            }
            break;
        }
        
        // Check if this looks like a handshake packet (by size range)
        if (l_recv >= HANDSHAKE_PKT_MIN && l_recv <= HANDSHAKE_PKT_MAX) {
            uint32_t l_count = atomic_fetch_add(&s_fake_server.handshake_count, 1) + 1;
            log_it(L_NOTICE, "*** HANDSHAKE packet #%u received (%zd bytes) ***", l_count, l_recv);
            
            // DO NOT RESPOND - we want to trigger client retransmission
        } else {
            log_it(L_DEBUG, "Received non-handshake packet (%zd bytes)", l_recv);
        }
    }
    
    log_it(L_INFO, "Fake server thread exiting (total handshakes: %u)", 
           atomic_load(&s_fake_server.handshake_count));
    return NULL;
}

//===================================================================
// FAKE SERVER CONTROL
//===================================================================

static int s_fake_server_start(void)
{
    memset(&s_fake_server, 0, sizeof(s_fake_server));
    
    // Create socket
    s_fake_server.fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_fake_server.fd < 0) {
        log_it(L_ERROR, "Failed to create fake server socket: %s", strerror(errno));
        return -1;
    }
    
    // Allow reuse
    int l_reuse = 1;
    setsockopt(s_fake_server.fd, SOL_SOCKET, SO_REUSEADDR, &l_reuse, sizeof(l_reuse));
    
    // Bind
    struct sockaddr_in l_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(TEST_PORT),
        .sin_addr.s_addr = inet_addr(TEST_ADDR)
    };
    
    if (bind(s_fake_server.fd, (struct sockaddr*)&l_addr, sizeof(l_addr)) < 0) {
        log_it(L_ERROR, "Failed to bind fake server: %s", strerror(errno));
        close(s_fake_server.fd);
        return -2;
    }
    
    s_fake_server.port = TEST_PORT;
    
    // Start thread
    if (pthread_create(&s_fake_server.thread, NULL, s_fake_server_thread, NULL) != 0) {
        log_it(L_ERROR, "Failed to start fake server thread");
        close(s_fake_server.fd);
        return -3;
    }
    
    log_it(L_NOTICE, "Fake server started on %s:%u", TEST_ADDR, TEST_PORT);
    return 0;
}

static void s_fake_server_stop(void)
{
    atomic_store(&s_fake_server.stop_flag, true);
    
    if (s_fake_server.thread) {
        pthread_join(s_fake_server.thread, NULL);
        s_fake_server.thread = 0;
    }
    
    if (s_fake_server.fd > 0) {
        close(s_fake_server.fd);
        s_fake_server.fd = -1;
    }
}

//===================================================================
// DAP SDK INITIALIZATION
//===================================================================

static bool s_sdk_initialized = false;

static int s_init_sdk(void)
{
    log_it(L_INFO, "Initializing DAP SDK...");
    
    // Force CBPF tier for consistent testing
    dap_io_flow_set_forced_tier(DAP_IO_FLOW_LB_TIER_CLASSIC_BPF);
    
    int ret = dap_events_init(0, 0);
    if (ret != 0) {
        log_it(L_ERROR, "Events init failed: %d", ret);
        return -1;
    }
    
    ret = dap_events_start();
    if (ret != 0) {
        log_it(L_ERROR, "Events start failed: %d", ret);
        return -2;
    }
    
    dap_enc_init();
    
    ret = dap_cert_init(NULL);
    if (ret != 0) {
        log_it(L_ERROR, "Cert init failed: %d", ret);
        return -3;
    }
    
    ret = dap_test_setup_certificates(".");
    if (ret != 0) {
        log_it(L_ERROR, "Certificate setup failed: %d", ret);
        return -4;
    }
    
    ret = dap_stream_init(NULL);
    if (ret != 0) {
        log_it(L_ERROR, "Stream init failed: %d", ret);
        return -5;
    }
    
    ret = dap_stream_ctl_init();
    if (ret != 0) {
        log_it(L_ERROR, "Stream ctl init failed: %d", ret);
        return -6;
    }
    
    dap_link_manager_init(NULL);
    dap_global_db_init(NULL);
    dap_client_init();
    dap_module_init_all();
    
    s_sdk_initialized = true;
    log_it(L_NOTICE, "DAP SDK initialized");
    return 0;
}

static void s_deinit_sdk(void)
{
    if (!s_sdk_initialized) return;
    
    log_it(L_INFO, "Deinitializing DAP SDK...");
    dap_client_deinit();
    dap_events_deinit();
    s_sdk_initialized = false;
}

//===================================================================
// MAIN TEST
//===================================================================

static void test_handshake_retransmission(void)
{
    dap_test_msg("Test: UDP handshake retransmission with real DAP SDK client");
    
    // Start fake server that won't respond (to trigger retransmission)
    int ret = s_fake_server_start();
    dap_assert(ret == 0, "Fake server started");
    
    // Wait for server thread to be ready (intelligent polling)
    bool l_server_ready = false;
    for (int i = 0; i < 50 && !l_server_ready; i++) {
        if (atomic_load(&s_fake_server.ready_flag)) {
            l_server_ready = true;
        } else {
            usleep(10000);  // 10ms poll
        }
    }
    dap_assert(l_server_ready, "Fake server ready");
    
    // Create DAP client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    dap_assert(l_client != NULL, "Client created");
    
    dap_client_set_trans_type(l_client, DAP_NET_TRANS_UDP_BASIC);
    
    // Wait for client to initialize
    bool l_ready = false;
    for (int i = 0; i < 20 && !l_ready; i++) {
        dap_client_esocket_t *esocket = DAP_CLIENT_ESOCKET(l_client);
        if (esocket && esocket->worker) l_ready = true;
        else usleep(100000);
    }
    dap_assert(l_ready, "Client initialized with worker");
    
    // Set uplink to our fake server
    dap_stream_node_addr_t l_fake_addr = {.uint64 = 0x1234567890ABCDEF};
    dap_client_set_uplink_unsafe(l_client, &l_fake_addr, TEST_ADDR, TEST_PORT);
    
    log_it(L_NOTICE, "Starting handshake to fake server at %s:%d (will not respond)...", TEST_ADDR, TEST_PORT);
    
    // Start handshake - this will fail eventually, but should trigger retransmissions
    dap_client_go_stage(l_client, STAGE_STREAM_STREAMING, NULL);
    
    // Intelligent wait for first handshake packet (with polling)
    dap_test_msg("Waiting for first handshake packet (client stage: %s)...", 
                 dap_client_get_stage_str(l_client));
    uint64_t l_first_pkt_start = dap_nanotime_now();
    uint64_t l_first_pkt_timeout = 3000ULL * 1000000ULL;  // 3 seconds max
    const char *l_last_stage = "";
    while ((dap_nanotime_now() - l_first_pkt_start) < l_first_pkt_timeout) {
        const char *l_cur_stage = dap_client_get_stage_str(l_client);
        if (strcmp(l_cur_stage, l_last_stage) != 0) {
            dap_test_msg("  Client stage changed: %s", l_cur_stage);
            l_last_stage = l_cur_stage;
        }
        if (atomic_load(&s_fake_server.handshake_count) > 0) {
            dap_test_msg("  First handshake packet received!");
            break;
        }
        usleep(50000);  // 50ms poll
    }
    
    if (atomic_load(&s_fake_server.handshake_count) == 0) {
        dap_test_msg("  No handshake packet received within 3 seconds!");
        dap_test_msg("  Final client stage: %s", dap_client_get_stage_str(l_client));
    }
    
    // Wait for retransmissions to occur
    dap_test_msg("Waiting %d ms for retransmissions...", WAIT_FOR_RETRANS_MS);
    
    uint64_t l_start = dap_nanotime_now();
    uint64_t l_timeout_ns = (uint64_t)WAIT_FOR_RETRANS_MS * 1000000ULL;
    
    while ((dap_nanotime_now() - l_start) < l_timeout_ns) {
        uint32_t l_count = atomic_load(&s_fake_server.handshake_count);
        if (l_count >= MIN_EXPECTED_PKTS) {
            log_it(L_NOTICE, "Got %u handshake packets, retransmission confirmed!", l_count);
            break;
        }
        usleep(100000);  // 100ms poll interval
    }
    
    // Get final count
    uint32_t l_final_count = atomic_load(&s_fake_server.handshake_count);
    
    dap_test_msg("Results:");
    dap_test_msg("  - Handshake packets received: %u", l_final_count);
    dap_test_msg("  - Expected minimum: %d", MIN_EXPECTED_PKTS);
    
    // Cleanup client
    log_it(L_INFO, "Cleaning up client...");
    dap_client_go_stage(l_client, STAGE_BEGIN, NULL);
    usleep(200000);
    dap_client_delete_unsafe(l_client);
    
    // Stop fake server
    s_fake_server_stop();
    
    // Verify retransmission worked
    if (l_final_count >= MIN_EXPECTED_PKTS) {
        dap_test_msg("Handshake retransmission WORKS! (received %u packets)", l_final_count);
        dap_pass_msg("Handshake retransmission works");
    } else {
        dap_test_msg("\n*** BUG: No handshake retransmission! ***");
        dap_test_msg("The client sent only %u handshake packet(s).", l_final_count);
        dap_test_msg("Expected: at least %d packets (original + retransmits)", MIN_EXPECTED_PKTS);
    }
    
    dap_assert(l_final_count >= MIN_EXPECTED_PKTS,
               "Handshake retransmission must work (at least 2 packets expected)");
}

//===================================================================
// MAIN
//===================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    dap_set_appname(LOG_TAG);

    // Initialize with log file for debugging
    if (dap_common_init(LOG_TAG, "/tmp/test_hs_retrans.log") != 0) {
        fprintf(stderr, "Failed to init dap_common\n");
        return 1;
    }
    
    // Enable debug logging AFTER dap_common_init
    dap_log_level_set(L_INFO);
    
    printf("\n");
    printf("========================================\n");
    printf("UDP Handshake Retransmission Test\n");
    printf("Using REAL DAP SDK client + fake server\n");
    printf("========================================\n\n");
    
    dap_print_module_name("udp_handshake_retransmission");
    
    // Initialize SDK
    if (s_init_sdk() != 0) {
        log_it(L_CRITICAL, "SDK initialization failed");
        dap_common_deinit();
        return 1;
    }
    
    // Run test
    test_handshake_retransmission();
    
    // Cleanup
    s_deinit_sdk();
    dap_common_deinit();
    
    printf("\n=== Test completed ===\n");
    return 0;
}
