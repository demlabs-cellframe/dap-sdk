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
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

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
#include "dap_stream_worker.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream_ctl.h"
#include "dap_enc.h"
#include "dap_net_trans.h"
#include "dap_net_trans_server.h"
#include "dap_net_trans_udp_server.h"
#include "dap_io_flow.h"
#include "dap_io_flow_socket.h"
#include "dap_link_manager.h"
#include "dap_global_db.h"
#include "dap_module.h"
#include "dap_cert.h"
#include "dap_cert_file.h"
#include "dap_client_test_fixtures.h"

#define LOG_TAG "test_fc_multiclient"

//===================================================================
// CONFIGURATION
//===================================================================

// Will be set dynamically to first non-loopback interface IP
static char s_test_server_addr[INET_ADDRSTRLEN] = "127.0.0.1";
#define TEST_SERVER_PORT    18200

/**
 * @brief Find first non-loopback IPv4 interface address
 * 
 * For CBPF SO_REUSEPORT to work properly, we need a real interface
 * because loopback doesn't compute rxhash (skb->hash = 0).
 * 
 * @return true if found, false if falling back to localhost
 */
static bool s_find_real_interface_ip(void) {
    struct ifaddrs *ifaddr, *ifa;
    
    if (getifaddrs(&ifaddr) == -1) {
        log_it(L_WARNING, "getifaddrs() failed, using localhost");
        return false;
    }
    
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        
        // Only IPv4
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;
        
        // Skip loopback
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;
        
        // Skip down interfaces
        if (!(ifa->ifa_flags & IFF_UP))
            continue;
        
        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &addr->sin_addr, s_test_server_addr, sizeof(s_test_server_addr));
        
        log_it(L_NOTICE, "Using interface '%s' with IP %s for CBPF test (rxhash will work!)",
               ifa->ifa_name, s_test_server_addr);
        
        freeifaddrs(ifaddr);
        return true;
    }
    
    freeifaddrs(ifaddr);
    log_it(L_WARNING, "No non-loopback interface found, using localhost (rxhash will be 0!)");
    return false;
}
#define TEST_CH_ID          'T'
#define NUM_CLIENTS         100    // 100 clients to stress test
#define DATA_SIZE           (256 * 1024)  // 256KB per client (25.6MB total)
#define HANDSHAKE_TIMEOUT   30000
#define DATA_TIMEOUT        120000

// Packet tracking statistics
static _Atomic uint64_t s_packets_sent = 0;
static _Atomic uint64_t s_packets_received = 0;
static _Atomic uint64_t s_packets_wrong_worker = 0;
static _Atomic uint64_t s_packets_no_session = 0;
static _Atomic uint64_t s_acks_sent = 0;
static _Atomic uint64_t s_acks_received = 0;

// Server node address
static dap_stream_node_addr_t s_server_node_addr = {0};

// Print packet statistics
static void s_print_stats(void) {
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║        PACKET TRACKING STATISTICS        ║\n");
    printf("╠══════════════════════════════════════════╣\n");
    printf("║ Packets sent:         %10lu        ║\n", atomic_load(&s_packets_sent));
    printf("║ Packets received:     %10lu        ║\n", atomic_load(&s_packets_received));
    printf("║ Wrong worker:         %10lu        ║\n", atomic_load(&s_packets_wrong_worker));
    printf("║ No session:           %10lu        ║\n", atomic_load(&s_packets_no_session));
    printf("║ ACKs sent:            %10lu        ║\n", atomic_load(&s_acks_sent));
    printf("║ ACKs received:        %10lu        ║\n", atomic_load(&s_acks_received));
    printf("╚══════════════════════════════════════════╝\n\n");
}

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
// SERVER CHANNEL - Echo back data (uses new API signature)
//===================================================================

static bool s_ch_pkt_in(dap_stream_ch_t *a_ch, void *a_data)
{
    // New API: data is dap_stream_ch_pkt_t*, size in pkt->hdr.data_size
    dap_stream_ch_pkt_t *l_pkt = (dap_stream_ch_pkt_t *)a_data;
    size_t l_size = l_pkt->hdr.data_size;
    log_it(L_DEBUG, "Server: echo %zu bytes", l_size);
    dap_stream_ch_pkt_write_unsafe(a_ch, 0, l_pkt->data, l_size);
    return true;
}

//===================================================================
// CLIENT CHANNEL NOTIFIER
//===================================================================

// Use DAP SDK's notifier type directly
// dap_stream_ch_notifier_t is defined in dap_stream_ch.h

static void s_client_data_in(dap_stream_ch_t *a_ch, uint8_t a_type, const void *a_data, size_t a_data_size, void *a_arg)
{
    (void)a_type;
    client_ctx_t *ctx = (client_ctx_t*)a_arg;
    
    if (!ctx || !a_data || a_data_size == 0) return;
    
    // Track packet received
    atomic_fetch_add(&s_packets_received, 1);
    
    // Log worker info for debugging
    dap_worker_t *l_worker = a_ch->stream_worker ? a_ch->stream_worker->worker : NULL;
    uint32_t l_worker_id = l_worker ? l_worker->id : 999;
    
    pthread_mutex_lock(&ctx->mutex);
    
    size_t new_size = ctx->recv_size + a_data_size;
    ctx->recv_data = DAP_REALLOC(ctx->recv_data, new_size);
    memcpy(ctx->recv_data + ctx->recv_size, a_data, a_data_size);
    ctx->recv_size = new_size;
    
    // Log every 100th packet or when data is complete
    uint64_t total_recv = atomic_load(&s_packets_received);
    if (total_recv % 100 == 0 || ctx->recv_size >= ctx->send_size) {
        log_it(L_INFO, "Client %d @ worker %u: recv %zu (total %zu/%zu) [pkt #%lu]", 
               ctx->id, l_worker_id, a_data_size, ctx->recv_size, ctx->send_size, total_recv);
    }
    
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
    s_server_node_addr = dap_stream_node_addr_from_cert(cert);
    dap_cert_delete(cert);
    
    // Register echo channel (new API: id, new_cb, delete_cb, pkt_in_cb, pkt_out_cb)
    dap_stream_ch_proc_add(TEST_CH_ID, NULL, NULL, s_ch_pkt_in, NULL);
    
    // FORCE EBPF TIER for multi-worker distribution testing
    // eBPF reads source port from UDP header - works on localhost!
    dap_io_flow_set_forced_tier(DAP_IO_FLOW_LB_TIER_EBPF);
    log_it(L_NOTICE, "FORCED EBPF tier for stress testing");
    
    // Create UDP server
    s_server = dap_net_trans_server_new(DAP_NET_TRANS_UDP_BASIC, "test_server");
    if (!s_server) {
        log_it(L_ERROR, "Failed to create server");
        return -2;
    }
    
    // New API: dap_net_trans_server_start(server, config, addrs[], ports[], count)
    const char *l_addr = s_test_server_addr;
    uint16_t l_port = TEST_SERVER_PORT;
    int ret = dap_net_trans_server_start(s_server, NULL, &l_addr, &l_port, 1);
    if (ret != 0) {
        log_it(L_ERROR, "Server start failed: %d", ret);
        dap_net_trans_server_delete(s_server);
        s_server = NULL;
        return -3;
    }
    
    log_it(L_NOTICE, "Server started on %s:%d with EBPF tier", s_test_server_addr, TEST_SERVER_PORT);
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
    dap_client_set_uplink_unsafe(ctx->client, &s_server_node_addr, s_test_server_addr, TEST_SERVER_PORT);
    
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
    
    // PARALLEL data exchange - this is the key to reproducing the bug!
    // Sequential sends don't stress the routing enough
    printf("\n--- PARALLEL data exchange (stress test) ---\n");
    printf("Sending %d KB x %d clients = %.1f MB simultaneously\n", 
           DATA_SIZE / 1024, NUM_CLIENTS, (double)(DATA_SIZE * NUM_CLIENTS) / (1024.0 * 1024.0));
    
    int data_ok = 0;
    int data_fail = 0;
    
    // Reset all clients first
    for (int i = 0; i < NUM_CLIENTS; i++) {
        client_ctx_t *ctx = &s_clients[i];
        pthread_mutex_lock(&ctx->mutex);
        ctx->data_received = false;
        ctx->recv_size = 0;
        DAP_DEL_Z(ctx->recv_data);
        pthread_mutex_unlock(&ctx->mutex);
    }
    
    // Send from ALL clients simultaneously (this triggers the bug!)
    printf("Sending from all %d clients...\n", NUM_CLIENTS);
    int send_failed = 0;
    for (int i = 0; i < NUM_CLIENTS; i++) {
        client_ctx_t *ctx = &s_clients[i];
        int sent = dap_client_write_mt(ctx->client, TEST_CH_ID, 0, ctx->send_data, ctx->send_size);
        if (sent < 0) {
            log_it(L_ERROR, "Client %d: send failed", i);
            send_failed++;
        }
        atomic_fetch_add(&s_packets_sent, 1);
    }
    if (send_failed > 0) {
        printf("WARNING: %d clients failed to send\n", send_failed);
    }
    printf("All sends initiated, waiting for responses...\n");
    
    // Now wait for ALL clients to receive data (parallel wait)
    for (int i = 0; i < NUM_CLIENTS; i++) {
        client_ctx_t *ctx = &s_clients[i];
        
        if (s_wait_data(i, DATA_TIMEOUT)) {
            if (ctx->recv_size == ctx->send_size &&
                memcmp(ctx->recv_data, ctx->send_data, ctx->send_size) == 0) {
                data_ok++;
                if (data_ok % 10 == 0) {
                    printf("Progress: %d/%d clients OK\n", data_ok, NUM_CLIENTS);
                }
            } else {
                data_fail++;
                printf("Client %d: DATA MISMATCH (got %zu, expected %zu)\n", 
                       i, ctx->recv_size, ctx->send_size);
            }
        } else {
            data_fail++;
            printf("Client %d: TIMEOUT (got %zu/%zu = %.1f%%)\n", 
                   i, ctx->recv_size, ctx->send_size,
                   100.0 * ctx->recv_size / ctx->send_size);
        }
    }
    
    printf("\n========================================\n");
    printf("Results: %d OK, %d FAILED\n", data_ok, data_fail);
    printf("========================================\n\n");
    
    // Print detailed statistics
    s_print_stats();
    
    if (data_fail > 0) {
        printf("*** BUG REPRODUCED: %d clients failed! ***\n\n", data_fail);
    }
    
    // Check for routing problems
    uint64_t wrong_worker = atomic_load(&s_packets_wrong_worker);
    uint64_t no_session = atomic_load(&s_packets_no_session);
    if (wrong_worker > 0 || no_session > 0) {
        printf("*** ROUTING PROBLEM DETECTED! ***\n");
        printf("    Packets to wrong worker: %lu\n", wrong_worker);
        printf("    Packets with no session: %lu\n\n", no_session);
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
    
    // Reset tier forcing
    dap_io_flow_set_forced_tier(-1);
}

//===================================================================
// MAIN
//===================================================================

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    
    // 0. Find real network interface for CBPF test (loopback has rxhash=0)
    s_find_real_interface_ip();
    
    // 1. Create test config file
    const char *config_content = 
        "[general]\n"
        "debug_mode=true\n"
        "[dap_io_flow_socket]\n"
        "debug_more=false\n"
        "[dap_stream]\n"
        "debug_more=false\n"
        "[dap_client]\n"
        "debug_more=false\n";
    FILE *f = fopen("test_fc_multiclient.cfg", "w");
    if (f) {
        fwrite(config_content, 1, strlen(config_content), f);
        fclose(f);
    }
    
    // 2. Set logging to stdout BEFORE dap_common_init
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    dap_log_level_set(L_DEBUG);
    
    // 3. Initialize config system
    dap_config_init(".");
    
    // 4. Open config and set as global
    extern dap_config_t *g_config;
    g_config = dap_config_open("test_fc_multiclient");
    if (!g_config) {
        printf("WARNING: Failed to open config (continuing anyway)\n");
    }
    
    // 5. Initialize DAP common
    dap_common_init("test_fc_multiclient", NULL);
    
    printf("\n============================\n");
    printf("Flow Ctrl Regression Test\n");
    printf("(Should FAIL to show bug)\n");
    printf("============================\n\n");
    
    dap_print_module_name("io_flow_ctrl_multiclient");
    
    int ret;
    
    // 6. Events system
    log_it(L_NOTICE, "Init: events_init...");
    ret = dap_events_init(0, 0);
    if (ret != 0) {
        log_it(L_CRITICAL, "Events init FAILED: %d", ret);
        return 1;
    }
    
    log_it(L_NOTICE, "Init: events_start...");
    ret = dap_events_start();
    if (ret != 0) {
        log_it(L_CRITICAL, "Events start FAILED: %d", ret);
        return 1;
    }
    
    // 7. Encryption and certificates
    log_it(L_NOTICE, "Init: enc_init...");
    dap_enc_init();
    
    log_it(L_NOTICE, "Init: cert_init...");
    ret = dap_cert_init(NULL);
    if (ret != 0) {
        log_it(L_CRITICAL, "Cert init FAILED: %d", ret);
        return 1;
    }
    
    // Setup test certificates (REQUIRED for dap_stream_init)
    log_it(L_NOTICE, "Init: setup_certificates...");
    ret = dap_test_setup_certificates(".");
    if (ret != 0) {
        log_it(L_CRITICAL, "Setup certificates FAILED: %d", ret);
        return 1;
    }
    
    // 8. Stream system (CRITICAL - initializes stream workers!)
    log_it(L_NOTICE, "Init: stream_init...");
    ret = dap_stream_init(NULL);
    if (ret != 0) {
        log_it(L_CRITICAL, "Stream init FAILED: %d", ret);
        return 1;
    }
    
    log_it(L_NOTICE, "Init: stream_ctl_init...");
    ret = dap_stream_ctl_init();
    if (ret != 0) {
        log_it(L_CRITICAL, "Stream ctl init FAILED: %d", ret);
        return 1;
    }
    
    // 9. Other components
    log_it(L_NOTICE, "Init: link_manager_init...");
    ret = dap_link_manager_init(NULL);
    if (ret != 0) log_it(L_WARNING, "Link manager: %d (may be OK)", ret);
    
    log_it(L_NOTICE, "Init: global_db_init...");
    ret = dap_global_db_init(NULL);
    if (ret != 0) log_it(L_WARNING, "Global DB: %d (may be OK)", ret);
    
    log_it(L_NOTICE, "Init: client_init...");
    ret = dap_client_init();
    if (ret != 0) log_it(L_WARNING, "Client init: %d", ret);
    
    // 10. Module system
    log_it(L_NOTICE, "Init: module_init_all...");
    ret = dap_module_init_all();
    if (ret != 0) {
        log_it(L_CRITICAL, "Module init FAILED: %d", ret);
        return 1;
    }
    
    // Verify stream workers are initialized
    uint32_t l_worker_count = dap_events_thread_get_count();
    log_it(L_NOTICE, "Checking %u workers for stream_worker...", l_worker_count);
    for (uint32_t i = 0; i < l_worker_count; i++) {
        dap_worker_t *l_worker = dap_events_worker_get(i);
        if (l_worker) {
            log_it(L_NOTICE, "  Worker %u: _inheritor=%p", i, (void*)l_worker->_inheritor);
        } else {
            log_it(L_ERROR, "  Worker %u: NULL!", i);
        }
    }
    
    log_it(L_NOTICE, "Init: DONE");
    
    // Run test
    test_multiclient_udp();
    
    // Cleanup
    dap_client_deinit();
    dap_events_deinit();
    if (g_config) {
        dap_config_close(g_config);
        g_config = NULL;
    }
    dap_config_deinit();
    dap_common_deinit();
    
    // Remove temp config
    remove("test_fc_multiclient.cfg");
    
    return 0;
}
