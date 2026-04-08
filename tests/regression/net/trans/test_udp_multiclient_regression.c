/**
 * @file test_udp_multiclient_regression.c
 * @brief UDP Multi-client regression test
 * 
 * Minimal test to reproduce multi-client UDP bug.
 * This test should FAIL to demonstrate the problem.
 * 
 * @date 2026-02-03
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>

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
#include "dap_client_fsm.h"
#include "dap_client_trans_ctx.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_pkt.h"
#include "dap_net_trans.h"
#include "dap_net_trans_server.h"
#include "dap_net_trans_udp_server.h"
#include "dap_io_flow.h"
#include "dap_io_flow_socket.h"
#include "dap_stream_ctl.h"
#include "dap_link_manager.h"
#include "dap_global_db.h"
#include "dap_module.h"
#include "dap_cert.h"
#include "dap_cert_file.h"
#include "test_trans_helpers.h"
#include "dap_client_test_fixtures.h"

#define LOG_TAG "test_udp_multiclient"

// Required by test_trans_helpers.c but not used in this test
const trans_test_config_t g_trans_configs[] = {{0}};
const size_t g_trans_config_count = 0;

//===================================================================
// CONFIGURATION - Same as failing test_trans_integration
//===================================================================

#define TEST_CH_ID          'T'
#define NUM_CLIENTS         100     // 100 clients to stress CBPF routing
#define DATA_SIZE           (64 * 1024)    // 64KB per client (6.4MB total)
#define HANDSHAKE_TIMEOUT   60000   // 60s for 100 clients
#define DATA_TIMEOUT        120000  // 120 sec for parallel data exchange

//===================================================================
// TEST STATE
//===================================================================

static dap_net_trans_server_t *s_server = NULL;
static dap_client_t *s_clients[NUM_CLIENTS] = {NULL};
static test_stream_ch_ctx_t s_stream_ctxs[NUM_CLIENTS];

static dap_stream_node_addr_t s_server_node_addr = {0};

// UDP trans config
static const trans_test_config_t s_udp_config = {
    .trans_type = DAP_NET_TRANS_UDP_BASIC,
    .name = "UDP",
    .base_port = 18102,
    .address = TEST_TRANS_SERVER_ADDR
};

//===================================================================
// INITIALIZATION
//===================================================================

static int s_init_all(void)
{
    // Force a non-application tier for reliable testing
    // Application tier has race conditions with multiple clients
#if defined(__linux__) || defined(ANDROID)
    dap_io_flow_set_forced_tier(DAP_IO_FLOW_LB_TIER_CLASSIC_BPF);
#elif defined(__APPLE__) && defined(__MACH__)
    dap_io_flow_set_forced_tier(DAP_IO_FLOW_LB_TIER_DARWIN_GCD);
#endif
    
    log_it(L_INFO, "Init: events_init...");
    int ret = dap_events_init(0, 0);
    if (ret != 0) {
        log_it(L_CRITICAL, "Events init failed: %d", ret);
        return -1;
    }
    
    log_it(L_INFO, "Init: events_start...");
    ret = dap_events_start();
    if (ret != 0) {
        log_it(L_CRITICAL, "Events start failed: %d", ret);
        return -2;
    }
    // dap_events_start() uses DAP_CONTEXT_FLAG_WAIT_FOR_STARTED - workers ready immediately
    
    log_it(L_INFO, "Init: enc_init...");
    dap_enc_init();
    
    log_it(L_INFO, "Init: cert_init...");
    ret = dap_cert_init(NULL);
    if (ret != 0) {
        log_it(L_CRITICAL, "Cert init failed: %d", ret);
        return -3;
    }
    
    log_it(L_INFO, "Init: setup_certificates...");
    ret = dap_test_setup_certificates(".");
    if (ret != 0) {
        log_it(L_CRITICAL, "Setup certificates failed: %d", ret);
        return -4;
    }
    
    log_it(L_INFO, "Init: stream_init...");
    ret = dap_stream_init(NULL);
    if (ret != 0) {
        log_it(L_CRITICAL, "Stream init failed: %d", ret);
        return -5;
    }
    
    log_it(L_INFO, "Init: stream_ctl_init...");
    ret = dap_stream_ctl_init();
    if (ret != 0) {
        log_it(L_CRITICAL, "Stream ctl init failed: %d", ret);
        return -6;
    }
    
    log_it(L_INFO, "Init: link_manager_init...");
    ret = dap_link_manager_init(NULL);
    if (ret != 0) {
        log_it(L_WARNING, "Link manager init: %d (may be OK)", ret);
    }
    
    log_it(L_INFO, "Init: global_db_init...");
    ret = dap_global_db_init(NULL);
    if (ret != 0) {
        log_it(L_WARNING, "Global DB init: %d (may be OK)", ret);
    }
    
    log_it(L_INFO, "Init: client_init...");
    ret = dap_client_init();
    if (ret != 0) {
        log_it(L_WARNING, "Client init: %d", ret);
    }
    
    log_it(L_INFO, "Init: register channel '%c'...", TEST_CH_ID);
    dap_stream_ch_proc_add(TEST_CH_ID, NULL, NULL, test_server_channel_echo_callback, NULL);
    
    log_it(L_INFO, "Init: module_init_all...");
    ret = dap_module_init_all();
    if (ret != 0) {
        log_it(L_CRITICAL, "Module init failed: %d", ret);
        return -7;
    }
    
    s_server_node_addr = g_node_addr;
    log_it(L_NOTICE, "Init: DONE");
    return 0;
}

//===================================================================
// SERVER SETUP
//===================================================================

static int s_setup_server(void)
{
    s_server = dap_net_trans_server_new(DAP_NET_TRANS_UDP_BASIC, "udp_regression");
    if (!s_server) {
        TEST_ERROR("Failed to create server");
        return -1;
    }
    
    const char *addr = s_udp_config.address;
    uint16_t port = s_udp_config.base_port;
    
    int ret = dap_net_trans_server_start(s_server, NULL, &addr, &port, 1);
    if (ret != 0) {
        TEST_ERROR("Server start failed: %d", ret);
        dap_net_trans_server_delete(s_server);
        s_server = NULL;
        return -2;
    }
    
    TEST_SUCCESS("Server started on %s:%d", addr, port);
    return 0;
}

static void s_cleanup_server(void)
{
    if (s_server) {
        dap_net_trans_server_stop(s_server);
        dap_net_trans_server_delete(s_server);
        s_server = NULL;
    }
}

//===================================================================
// CLIENT SETUP
//===================================================================

static int s_setup_client(int id)
{
    if (test_stream_ch_ctx_init(&s_stream_ctxs[id], TEST_CH_ID, DATA_SIZE) != 0) {
        log_it(L_ERROR, "Client %d: ctx init failed", id);
        return -1;
    }
    
    s_clients[id] = dap_client_new(NULL, NULL);
    if (!s_clients[id]) {
        log_it(L_ERROR, "Client %d: create failed", id);
        return -2;
    }
    
    dap_client_set_trans_type(s_clients[id], DAP_NET_TRANS_UDP_BASIC);
    
    // Wait for client init with polling
    bool ready = false;
    for (int i = 0; i < 20 && !ready; i++) {
        dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(s_clients[id]);
        if (l_fsm && l_fsm->worker) ready = true;
        else usleep(100000);
    }
    
    if (!ready) {
        log_it(L_ERROR, "Client %d: init timeout", id);
        return -3;
    }
    
    dap_client_set_uplink_unsafe(s_clients[id], &s_server_node_addr, 
                                  s_udp_config.address, s_udp_config.base_port);
    
    char ch[2] = {TEST_CH_ID, 0};
    dap_client_set_active_channels_unsafe(s_clients[id], ch);
    
    return 0;
}

static void s_cleanup_client(int id)
{
    if (s_clients[id]) {
        dap_client_delete_unsafe(s_clients[id]);
        s_clients[id] = NULL;
    }
    test_stream_ch_ctx_cleanup(&s_stream_ctxs[id]);
}

//===================================================================
// MAIN TEST
//===================================================================

static void test_multiclient_udp(void)
{
    TEST_INFO("UDP Multi-Client Regression Test");
    TEST_INFO("Clients: %d, Data: %d MB each", NUM_CLIENTS, DATA_SIZE / (1024 * 1024));
    
    // Server
    if (s_setup_server() != 0) {
        dap_fail("Server setup failed");
        return;
    }
    
    // Create clients
    TEST_INFO("Creating %d clients...", NUM_CLIENTS);
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (s_setup_client(i) != 0) {
            dap_fail("Client setup failed");
            goto cleanup;
        }
    }
    
    // Start handshakes
    TEST_INFO("Starting handshakes...");
    for (int i = 0; i < NUM_CLIENTS; i++) {
        dap_client_go_stage(s_clients[i], STAGE_STREAM_STREAMING, NULL);
    }
    
    // Wait handshakes
    int hs_ok = 0;
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (test_trans_wait_for_streaming(s_clients[i], HANDSHAKE_TIMEOUT)) {
            hs_ok++;
        } else {
            TEST_ERROR("Client %d: handshake TIMEOUT (stage=%s)", 
                       i, dap_client_get_stage_str(s_clients[i]));
        }
    }
    TEST_INFO("Handshakes: %d/%d", hs_ok, NUM_CLIENTS);
    
    if (hs_ok != NUM_CLIENTS) {
        TEST_WARN("BUG REPRODUCED: Only %d/%d handshakes completed!", hs_ok, NUM_CLIENTS);
        dap_assert(hs_ok == NUM_CLIENTS, "REGRESSION: All handshakes should complete");
        goto cleanup;
    }
    
    // Register receivers
    TEST_INFO("Registering receivers...");
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (test_stream_ch_register_receiver(s_clients[i], TEST_CH_ID, &s_stream_ctxs[i]) != 0) {
            TEST_ERROR("Client %d: register failed", i);
        }
    }
    
    // PARALLEL data exchange - THIS IS KEY TO REPRODUCING THE BUG!
    // Sequential sends don't stress CBPF routing enough
    TEST_INFO("PARALLEL data exchange (%d clients x %d KB = %.1f MB)...", 
              NUM_CLIENTS, DATA_SIZE / 1024, (double)(NUM_CLIENTS * DATA_SIZE) / (1024.0 * 1024.0));
    
    int data_ok = 0;
    int data_fail = 0;
    
    // Step 1: Reset all contexts
    for (int i = 0; i < NUM_CLIENTS; i++) {
        pthread_mutex_lock(&s_stream_ctxs[i].mutex);
        s_stream_ctxs[i].data_received = false;
        s_stream_ctxs[i].received_data_size = 0;
        DAP_DEL_Z(s_stream_ctxs[i].received_data);
        pthread_mutex_unlock(&s_stream_ctxs[i].mutex);
    }
    
    // Step 2: Send from ALL clients simultaneously (triggers CBPF routing bug!)
    TEST_INFO("Sending from all %d clients simultaneously...", NUM_CLIENTS);
    int send_failed = 0;
    for (int i = 0; i < NUM_CLIENTS; i++) {
        int sent = dap_client_write_mt(s_clients[i], TEST_CH_ID, 0, 
                                        s_stream_ctxs[i].sent_data, 
                                        s_stream_ctxs[i].sent_data_size);
        if (sent < 0) {
            TEST_ERROR("Client %d: send failed (ret=%d)", i, sent);
            send_failed++;
        }
    }
    if (send_failed > 0) {
        TEST_WARN("%d clients failed to send", send_failed);
    }
    TEST_INFO("All sends initiated, waiting for responses...");
    
    // Step 3: Wait for ALL clients to receive data
    for (int i = 0; i < NUM_CLIENTS; i++) {
        bool success = false;
        
        // Wait for this client's data
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += DATA_TIMEOUT / 1000;
        ts.tv_nsec += (DATA_TIMEOUT % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        
        pthread_mutex_lock(&s_stream_ctxs[i].mutex);
        while (!s_stream_ctxs[i].data_received) {
            if (pthread_cond_timedwait(&s_stream_ctxs[i].cond, &s_stream_ctxs[i].mutex, &ts) == ETIMEDOUT) {
                break;
            }
        }
        bool received = s_stream_ctxs[i].data_received;
        pthread_mutex_unlock(&s_stream_ctxs[i].mutex);
        
        if (received && s_stream_ctxs[i].received_data) {
            bool valid = test_trans_verify_data(s_stream_ctxs[i].sent_data,
                                                 s_stream_ctxs[i].received_data,
                                                 s_stream_ctxs[i].received_data_size);
            if (valid) {
                data_ok++;
                success = true;
            } else {
                TEST_ERROR("Client %d: DATA MISMATCH (got %zu, expected %zu)", 
                           i, s_stream_ctxs[i].received_data_size, s_stream_ctxs[i].sent_data_size);
            }
        } else {
            TEST_ERROR("Client %d: TIMEOUT (got %zu/%zu = %.1f%%)", 
                       i, s_stream_ctxs[i].received_data_size, s_stream_ctxs[i].sent_data_size,
                       100.0 * s_stream_ctxs[i].received_data_size / s_stream_ctxs[i].sent_data_size);
        }
        
        if (!success) data_fail++;
        
        // Progress every 10 clients
        if ((data_ok + data_fail) % 10 == 0) {
            TEST_INFO("Progress: %d/%d clients processed (%d OK, %d FAIL)", 
                      data_ok + data_fail, NUM_CLIENTS, data_ok, data_fail);
        }
    }
    
    TEST_INFO("Results: %d OK, %d FAILED out of %d", data_ok, data_fail, NUM_CLIENTS);
    
    if (data_fail > 0) {
        TEST_WARN("BUG REPRODUCED: %d clients failed!", data_fail);
    }
    
    // This assertion should FAIL if bug is present
    dap_assert(data_fail == 0, "REGRESSION: All clients should complete data exchange");
    
cleanup:
    TEST_INFO("Cleanup: stopping clients...");
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (s_clients[i]) {
            dap_client_go_stage(s_clients[i], STAGE_BEGIN, NULL);
        }
    }
    
    // Wait for disconnects and async operations
    usleep(1000000);
    
    TEST_INFO("Cleanup: stopping server...");
    s_cleanup_server();
    
    TEST_INFO("Cleanup: done");
}

//===================================================================
// MAIN
//===================================================================

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    // Create config before init so dap_client_fsm_init() picks up our settings.
    // timeout_active_after_connect: 100 clients can take ~70s to all complete
    // handshakes; default 15s would kill early clients before data exchange.
    const char *l_cfg =
        "[general]\ndebug_mode=true\n"
        "[dap_client]\n"
        "timeout_active_after_connect=300\n"
        "debug_more=false\n"
        "[dap_net_trans_udp]\ndebug_more=false\n"
        "[dap_io_flow_socket]\ndebug_more=false\n";
    FILE *l_f = fopen("test_udp_multiclient.cfg", "w");
    if (l_f) { fwrite(l_cfg, 1, strlen(l_cfg), l_f); fclose(l_f); }
    dap_config_init(".");
    extern dap_config_t *g_config;
    g_config = dap_config_open("test_udp_multiclient");

    dap_common_init(LOG_TAG, NULL);
    dap_log_level_set(L_DEBUG);
    
    dap_print_module_name("udp_multiclient_regression");
    
    if (s_init_all() != 0) {
        log_it(L_CRITICAL, "Initialization failed");
        return 1;
    }
    
    TEST_RUN(test_multiclient_udp);
    
    // Stop event loop first (join worker threads) before client cleanup
    dap_events_deinit();
    
    // Now safe to clean up clients — no worker threads running
    for (int i = 0; i < NUM_CLIENTS; i++) {
        s_cleanup_client(i);
    }
    dap_client_deinit();
    dap_common_deinit();
    
    return 0;
}
