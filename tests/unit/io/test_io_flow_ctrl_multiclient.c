/**
 * @file test_io_flow_ctrl_multiclient.c
 * @brief Regression test for UDP multi-client bug
 * 
 * Uses test_trans_helpers from integration tests to reproduce the bug.
 * Expected: Test should FAIL to demonstrate the problem.
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
#include "dap_stream_ch_pkt.h"
#include "dap_net_trans.h"
#include "dap_net_trans_server.h"
#include "dap_net_trans_udp_server.h"
#include "dap_io_flow.h"
#include "dap_link_manager.h"
#include "dap_global_db.h"
#include "dap_module.h"
#include "dap_cert.h"
#include "dap_cert_file.h"

#define LOG_TAG "test_fc_multiclient"

//===================================================================
// CONFIGURATION
//===================================================================

#define TEST_SERVER_ADDR    "127.0.0.1"
#define TEST_SERVER_PORT    18200
#define TEST_CH_ID          'T'
#define NUM_CLIENTS         10
#define DATA_SIZE           (10 * 1024 * 1024)  // 10MB like integration test
#define HANDSHAKE_TIMEOUT   15000
#define DATA_TIMEOUT        60000

// Server node address
static dap_stream_node_addr_t s_server_node_addr = {0};

//===================================================================
// TEST STATE
//===================================================================

typedef struct {
    dap_client_t *client;
    int id;
    uint8_t *send_data;
    size_t send_size;
    uint8_t *recv_data;
    size_t recv_size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool data_received;
} client_ctx_t;

static dap_net_trans_server_t *s_server = NULL;
static client_ctx_t s_clients[NUM_CLIENTS];

//===================================================================
// SERVER CHANNEL - Echo back data
//===================================================================

static void s_ch_pkt_in(dap_stream_ch_t *a_ch, void *a_data, size_t a_size)
{
    log_it(L_DEBUG, "Server: echo %zu bytes", a_size);
    dap_stream_ch_pkt_write_unsafe(a_ch, 0, a_data, a_size);
}

//===================================================================
// CLIENT CHANNEL NOTIFIER
//===================================================================

// Use DAP SDK's notifier type directly
// dap_stream_ch_notifier_t is defined in dap_stream_ch.h

static void s_client_data_in(dap_stream_ch_t *a_ch, uint8_t a_type, const void *a_data, size_t a_data_size, void *a_arg)
{
    (void)a_type;
    (void)a_ch;
    client_ctx_t *ctx = (client_ctx_t*)a_arg;
    
    if (!ctx || !a_data || a_data_size == 0) return;
    
    pthread_mutex_lock(&ctx->mutex);
    
    size_t new_size = ctx->recv_size + a_data_size;
    ctx->recv_data = DAP_REALLOC(ctx->recv_data, new_size);
    memcpy(ctx->recv_data + ctx->recv_size, a_data, a_data_size);
    ctx->recv_size = new_size;
    
    log_it(L_DEBUG, "Client %d: recv %zu (total %zu/%zu)", 
           ctx->id, a_data_size, ctx->recv_size, ctx->send_size);
    
    if (ctx->recv_size >= ctx->send_size) {
        ctx->data_received = true;
        pthread_cond_signal(&ctx->cond);
    }
    
    pthread_mutex_unlock(&ctx->mutex);
}

//===================================================================
// SERVER SETUP
//===================================================================

static int s_setup_server(void)
{
    // Generate server certificate
    dap_cert_t *cert = dap_cert_generate_mem_with_seed("test_server_cert", DAP_ENC_KEY_TYPE_SIG_BLISS, "seed123", 7);
    if (!cert) {
        log_it(L_ERROR, "Failed to generate server certificate");
        return -1;
    }
    dap_stream_node_addr_from_cert(cert, &s_server_node_addr);
    dap_cert_delete(cert);
    
    // Register echo channel
    dap_stream_ch_proc_add(TEST_CH_ID, s_ch_pkt_in, NULL, NULL, NULL, NULL);
    
    // Create UDP server
    s_server = dap_net_trans_server_new(DAP_NET_TRANS_UDP_BASIC, "test_server");
    if (!s_server) {
        log_it(L_ERROR, "Failed to create server");
        return -2;
    }
    
    int ret = dap_net_trans_server_start(s_server, TEST_SERVER_ADDR, TEST_SERVER_PORT);
    if (ret != 0) {
        log_it(L_ERROR, "Server start failed: %d", ret);
        dap_net_trans_server_delete(s_server);
        s_server = NULL;
        return -3;
    }
    
    log_it(L_NOTICE, "Server started on %s:%d", TEST_SERVER_ADDR, TEST_SERVER_PORT);
    return 0;
}

//===================================================================
// CLIENT SETUP
//===================================================================

static int s_setup_client(int id)
{
    client_ctx_t *ctx = &s_clients[id];
    memset(ctx, 0, sizeof(*ctx));
    ctx->id = id;
    pthread_mutex_init(&ctx->mutex, NULL);
    pthread_cond_init(&ctx->cond, NULL);
    
    // Generate unique test data
    ctx->send_size = DATA_SIZE;
    ctx->send_data = DAP_NEW_SIZE(uint8_t, ctx->send_size);
    for (size_t i = 0; i < ctx->send_size; i++) {
        ctx->send_data[i] = (uint8_t)((i + id * 17) & 0xFF);
    }
    
    // Create client
    ctx->client = dap_client_new(NULL, NULL);
    if (!ctx->client) {
        log_it(L_ERROR, "Client %d: create failed", id);
        return -1;
    }
    
    dap_client_set_trans_type(ctx->client, DAP_NET_TRANS_UDP_BASIC);
    
    // Wait for client init
    for (int i = 0; i < 20; i++) {
        dap_client_pvt_t *pvt = DAP_CLIENT_PVT(ctx->client);
        if (pvt && pvt->worker) break;
        usleep(100000);
    }
    
    dap_client_pvt_t *pvt = DAP_CLIENT_PVT(ctx->client);
    if (!pvt || !pvt->worker) {
        log_it(L_ERROR, "Client %d: init timeout", id);
        return -2;
    }
    
    // Set uplink
    dap_client_set_uplink_unsafe(ctx->client, &s_server_node_addr, TEST_SERVER_ADDR, TEST_SERVER_PORT);
    
    // Set channels
    char ch[2] = {TEST_CH_ID, 0};
    dap_client_set_active_channels_unsafe(ctx->client, ch);
    
    return 0;
}

static void s_cleanup_client(int id)
{
    client_ctx_t *ctx = &s_clients[id];
    if (ctx->client) {
        dap_client_delete_unsafe(ctx->client);
        ctx->client = NULL;
    }
    DAP_DEL_Z(ctx->send_data);
    DAP_DEL_Z(ctx->recv_data);
    pthread_mutex_destroy(&ctx->mutex);
    pthread_cond_destroy(&ctx->cond);
}

//===================================================================
// REGISTER NOTIFIER IN WORKER
//===================================================================

typedef struct {
    dap_stream_t *stream;
    char channel_id;
    client_ctx_t *ctx;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
    int *result;
} register_args_t;

static void s_register_notifier_callback(void *a_arg)
{
    register_args_t *args = (register_args_t*)a_arg;
    
    dap_stream_ch_t *ch = dap_stream_ch_by_id_unsafe(args->stream, args->channel_id);
    if (!ch) {
        pthread_mutex_lock(args->mutex);
        *args->result = -1;
        pthread_cond_signal(args->cond);
        pthread_mutex_unlock(args->mutex);
        DAP_DELETE(args);
        return;
    }
    
    // Add notifier using DAP SDK's type
    dap_stream_ch_notifier_t *notifier = DAP_NEW_Z(dap_stream_ch_notifier_t);
    notifier->callback = s_client_data_in;
    notifier->arg = args->ctx;
    
    ch->packet_in_notifiers = dap_list_append(ch->packet_in_notifiers, notifier);
    
    pthread_mutex_lock(args->mutex);
    *args->result = 0;
    pthread_cond_signal(args->cond);
    pthread_mutex_unlock(args->mutex);
    
    DAP_DELETE(args);
}

static int s_register_receiver(int id)
{
    client_ctx_t *ctx = &s_clients[id];
    
    dap_stream_t *stream = dap_client_get_stream(ctx->client);
    if (!stream) {
        log_it(L_ERROR, "Client %d: no stream", id);
        return -1;
    }
    
    dap_client_pvt_t *pvt = DAP_CLIENT_PVT(ctx->client);
    if (!pvt || !pvt->worker) {
        log_it(L_ERROR, "Client %d: no worker", id);
        return -2;
    }
    
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    int result = -1;
    
    register_args_t *args = DAP_NEW_Z(register_args_t);
    args->stream = stream;
    args->channel_id = TEST_CH_ID;
    args->ctx = ctx;
    args->mutex = &mutex;
    args->cond = &cond;
    args->result = &result;
    
    dap_worker_exec_callback_on(pvt->worker, s_register_notifier_callback, args);
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;
    
    pthread_mutex_lock(&mutex);
    while (result == -1) {
        if (pthread_cond_timedwait(&cond, &mutex, &ts) == ETIMEDOUT) {
            pthread_mutex_unlock(&mutex);
            pthread_mutex_destroy(&mutex);
            pthread_cond_destroy(&cond);
            return -3;
        }
    }
    pthread_mutex_unlock(&mutex);
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    
    return result;
}

//===================================================================
// WAIT HELPERS
//===================================================================

static bool s_wait_streaming(int id, uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        dap_client_stage_t stage = dap_client_get_stage(s_clients[id].client);
        if (stage == STAGE_STREAM_STREAMING) return true;
        usleep(50000);
        elapsed += 50;
    }
    return false;
}

static bool s_wait_data(int id, uint32_t timeout_ms)
{
    client_ctx_t *ctx = &s_clients[id];
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    
    pthread_mutex_lock(&ctx->mutex);
    while (!ctx->data_received) {
        if (pthread_cond_timedwait(&ctx->cond, &ctx->mutex, &ts) == ETIMEDOUT) {
            pthread_mutex_unlock(&ctx->mutex);
            return false;
        }
    }
    pthread_mutex_unlock(&ctx->mutex);
    return true;
}

//===================================================================
// MAIN TEST
//===================================================================

static void test_multiclient_udp(void)
{
    printf("\n========================================\n");
    printf("UDP Multi-Client Test\n");
    printf("Clients: %d, Data: %d MB each\n", NUM_CLIENTS, DATA_SIZE / (1024 * 1024));
    printf("========================================\n\n");
    
    // Server
    if (s_setup_server() != 0) {
        dap_fail("Server setup failed");
        return;
    }
    
    // Create clients
    printf("Creating %d clients...\n", NUM_CLIENTS);
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (s_setup_client(i) != 0) {
            dap_fail("Client setup failed");
            goto cleanup;
        }
    }
    
    // Start handshakes
    printf("Starting handshakes...\n");
    for (int i = 0; i < NUM_CLIENTS; i++) {
        dap_client_go_stage(s_clients[i].client, STAGE_STREAM_STREAMING, NULL);
    }
    
    // Wait handshakes
    int hs_ok = 0;
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (s_wait_streaming(i, HANDSHAKE_TIMEOUT)) {
            hs_ok++;
            log_it(L_NOTICE, "Client %d: handshake OK", i);
        } else {
            log_it(L_ERROR, "Client %d: handshake TIMEOUT", i);
        }
    }
    printf("Handshakes: %d/%d\n", hs_ok, NUM_CLIENTS);
    
    if (hs_ok != NUM_CLIENTS) {
        dap_fail("Handshakes incomplete");
        goto cleanup;
    }
    
    // Register receivers
    printf("Registering receivers...\n");
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (s_register_receiver(i) != 0) {
            log_it(L_ERROR, "Client %d: register failed", i);
        }
    }
    
    // Sequential data exchange
    printf("\n--- Sequential data exchange ---\n");
    
    int data_ok = 0;
    int data_fail = 0;
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        client_ctx_t *ctx = &s_clients[i];
        
        // Reset
        pthread_mutex_lock(&ctx->mutex);
        ctx->data_received = false;
        ctx->recv_size = 0;
        DAP_DEL_Z(ctx->recv_data);
        pthread_mutex_unlock(&ctx->mutex);
        
        // Send
        printf("Client %d: sending %zu bytes...\n", i, ctx->send_size);
        int sent = dap_client_write_mt(ctx->client, TEST_CH_ID, 0, ctx->send_data, ctx->send_size);
        if (sent < 0) {
            log_it(L_ERROR, "Client %d: send failed", i);
            data_fail++;
            continue;
        }
        
        // Wait
        if (s_wait_data(i, DATA_TIMEOUT)) {
            if (ctx->recv_size == ctx->send_size &&
                memcmp(ctx->recv_data, ctx->send_data, ctx->send_size) == 0) {
                data_ok++;
                printf("Client %d: OK (%zu bytes)\n", i, ctx->recv_size);
            } else {
                data_fail++;
                printf("Client %d: DATA MISMATCH\n", i);
            }
        } else {
            data_fail++;
            printf("Client %d: TIMEOUT (got %zu/%zu)\n", i, ctx->recv_size, ctx->send_size);
        }
    }
    
    printf("\n========================================\n");
    printf("Results: %d OK, %d FAILED\n", data_ok, data_fail);
    printf("========================================\n\n");
    
    if (data_fail > 0) {
        printf("*** BUG REPRODUCED: %d clients failed! ***\n\n", data_fail);
    }
    
    // This should FAIL if bug is present
    dap_assert(data_fail == 0, "All clients should complete data exchange");
    
cleanup:
    for (int i = 0; i < NUM_CLIENTS; i++) {
        s_cleanup_client(i);
    }
    if (s_server) {
        dap_net_trans_server_stop(s_server);
        dap_net_trans_server_delete(s_server);
        s_server = NULL;
    }
}

//===================================================================
// MAIN
//===================================================================

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    
    dap_common_init("test_fc_multiclient", NULL);
    dap_log_level_set(L_DEBUG);
    
    printf("\n============================\n");
    printf("Flow Ctrl Regression Test\n");
    printf("(Should FAIL to show bug)\n");
    printf("============================\n\n");
    
    dap_print_module_name("io_flow_ctrl_multiclient");
    
    // Init all
    int ret = dap_module_init_all();
    if (ret != 0) {
        log_it(L_CRITICAL, "Module init: %d", ret);
        return 1;
    }
    
    ret = dap_events_init(0, 0);
    if (ret != 0) {
        log_it(L_CRITICAL, "Events init: %d", ret);
        return 1;
    }
    
    ret = dap_events_start();
    if (ret != 0) {
        log_it(L_CRITICAL, "Events start: %d", ret);
        return 1;
    }
    
    usleep(500000);
    
    ret = dap_link_manager_init(NULL);
    if (ret != 0) log_it(L_WARNING, "Link manager: %d", ret);
    
    ret = dap_global_db_init(NULL);
    if (ret != 0) log_it(L_WARNING, "Global DB: %d", ret);
    
    ret = dap_client_init();
    if (ret != 0) log_it(L_WARNING, "Client init: %d", ret);
    
    // Run
    test_multiclient_udp();
    
    // Cleanup
    dap_client_deinit();
    dap_events_deinit();
    dap_common_deinit();
    
    return 0;
}
