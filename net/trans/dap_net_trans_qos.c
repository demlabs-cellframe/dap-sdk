/*
 * Transport-independent QoS measurement via probe/echo.
 *
 * Server-side:
 *   UDP/TLS/DNS — detects probe magic in deobfuscated handshake payload, builds echo.
 *   HTTP/WS (legacy) — /qos_probe HTTP handler, simple request/response.
 *
 * Client-side (latency):
 *   UDP/TLS/DNS — dap_client_t FSM → go_stage(STAGE_QOS_PROBE), dedicated FSM branch.
 *   HTTP/WS — dap_client_request() to /qos_probe endpoint.
 *
 * Client-side (bandwidth):
 *   HTTP/WS — POST BW_REQUEST to /qos_probe, server responds with bw_bytes of data.
 *   UDP/TLS/DNS — burst of N probe/echo round-trips, aggregate throughput estimation.
 */

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_net_trans_qos.h"
#include "dap_net_trans.h"
#include "dap_client.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_http_simple.h"
#include "http_status_code.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_net_trans_qos"

/* ========================================================================== */
/*  Helpers                                                                   */
/* ========================================================================== */

static uint64_t s_mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ========================================================================== */
/*  Server-side: probe detection and echo building                            */
/* ========================================================================== */

bool dap_qos_is_probe(const void *a_payload, size_t a_size)
{
    if (!a_payload || a_size < sizeof(dap_qos_probe_pkt_t))
        return false;
    const dap_qos_probe_pkt_t *p = (const dap_qos_probe_pkt_t *)a_payload;
    return p->magic == DAP_QOS_PROBE_MAGIC
        && (p->type == DAP_QOS_TYPE_PROBE || p->type == DAP_QOS_TYPE_BW_REQUEST);
}

#define BW_MAX_BYTES  (4 * 1024 * 1024)

int dap_qos_build_echo(const void *a_probe, size_t a_probe_size,
                        void **a_echo_out, size_t *a_echo_size)
{
    if (!a_probe || a_probe_size < sizeof(dap_qos_probe_pkt_t)
        || !a_echo_out || !a_echo_size)
        return -1;

    const dap_qos_probe_pkt_t *p = (const dap_qos_probe_pkt_t *)a_probe;
    bool l_is_bw = (p->type == DAP_QOS_TYPE_BW_REQUEST);

    size_t l_out_size;
    if (l_is_bw) {
        uint32_t l_bw = p->bw_bytes;
        if (l_bw == 0 || l_bw > BW_MAX_BYTES) l_bw = BW_MAX_BYTES;
        l_out_size = sizeof(dap_qos_echo_pkt_t) + l_bw;
    } else {
        l_out_size = a_probe_size;
    }

    uint8_t *l_out = DAP_NEW_Z_SIZE(uint8_t, l_out_size);
    if (!l_out) return -1;

    dap_qos_echo_pkt_t *e = (dap_qos_echo_pkt_t *)l_out;
    e->magic     = DAP_QOS_ECHO_MAGIC;
    e->type      = l_is_bw ? DAP_QOS_TYPE_BW_DATA : DAP_QOS_TYPE_ECHO;
    e->probe_id  = p->probe_id;
    e->client_ts = p->client_ts;
    e->server_ts = s_mono_ns();

    if (l_out_size > sizeof(dap_qos_echo_pkt_t)) {
        size_t l_fill = l_out_size - sizeof(dap_qos_echo_pkt_t);
        memset(l_out + sizeof(dap_qos_echo_pkt_t), 0xAA, l_fill);
    }

    *a_echo_out  = l_out;
    *a_echo_size = l_out_size;
    return 0;
}

/* ========================================================================== */
/*  Server-side: HTTP /qos_probe handler (legacy HTTP transport)              */
/* ========================================================================== */

static void s_qos_probe_http_proc(dap_http_simple_t *a_http_simple, void *a_arg)
{
    http_status_code_t *l_rc = (http_status_code_t *)a_arg;

    if (!a_http_simple->request || a_http_simple->request_size < sizeof(dap_qos_probe_pkt_t)) {
        *l_rc = Http_Status_BadRequest;
        return;
    }

    const dap_qos_probe_pkt_t *p = (const dap_qos_probe_pkt_t *)a_http_simple->request;
    if (p->magic != DAP_QOS_PROBE_MAGIC) {
        *l_rc = Http_Status_BadRequest;
        return;
    }

    void  *l_echo = NULL;
    size_t l_echo_size = 0;
    if (dap_qos_build_echo(a_http_simple->request, a_http_simple->request_size,
                           &l_echo, &l_echo_size) != 0) {
        *l_rc = Http_Status_InternalServerError;
        return;
    }

    dap_http_simple_reply(a_http_simple, l_echo, l_echo_size);
    DAP_DELETE(l_echo);
    *l_rc = Http_Status_OK;
}

void dap_net_trans_qos_add_proc(dap_http_server_t *a_http)
{
    if (!a_http) return;
    dap_http_simple_proc_add(a_http, "/qos_probe", BW_MAX_BYTES + 1024, s_qos_probe_http_proc);
    log_it(L_INFO, "Registered QoS probe handler: /qos_probe");
}

/* ========================================================================== */
/*  Client-side: blocking probe via condvar                                   */
/* ========================================================================== */

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    bool            completed;
    int             result_ms;
    uint64_t        start_ns;
    uint64_t        probe_id;
    dap_client_t   *client;
    dap_net_trans_type_t trans_type;
    bool            bw_mode;
    size_t          bw_received;
} s_qos_ctx_t;

static s_qos_ctx_t *s_ctx_new(void)
{
    s_qos_ctx_t *c = DAP_NEW_Z(s_qos_ctx_t);
    if (!c) return NULL;
    pthread_mutex_init(&c->mutex, NULL);
    pthread_cond_init(&c->cond, NULL);
    c->result_ms = -1;
    return c;
}

static void s_ctx_signal(s_qos_ctx_t *c, int a_ms)
{
    pthread_mutex_lock(&c->mutex);
    if (!c->completed) {
        c->completed = true;
        c->result_ms = a_ms;
    }
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->mutex);
}

static int s_ctx_wait(s_qos_ctx_t *c, uint32_t a_timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += a_timeout_ms / 1000;
    ts.tv_nsec += (long)(a_timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }

    pthread_mutex_lock(&c->mutex);
    while (!c->completed) {
        if (pthread_cond_timedwait(&c->cond, &c->mutex, &ts) != 0) {
            c->completed = true;
            break;
        }
    }
    int r = c->result_ms;
    pthread_mutex_unlock(&c->mutex);
    return r;
}

static void s_ctx_free(s_qos_ctx_t *c)
{
    if (!c) return;
    pthread_mutex_destroy(&c->mutex);
    pthread_cond_destroy(&c->cond);
    DAP_DELETE(c);
}

/* ---- FSM callbacks (UDP and other modern transports) ---- */

static void s_probe_stage_done_cb(dap_client_t *a_client, void *a_arg)
{
    (void)a_arg;
    s_qos_ctx_t *ctx = (s_qos_ctx_t *)a_client->_inheritor;
    if (!ctx) return;

    uint64_t now = s_mono_ns();
    int ms = (int)((now - ctx->start_ns) / 1000000ULL);
    s_ctx_signal(ctx, ms);
}

static void s_probe_stage_error_cb(dap_client_t *a_client, void *a_arg)
{
    (void)a_arg;
    s_qos_ctx_t *ctx = (s_qos_ctx_t *)a_client->_inheritor;
    if (!ctx) return;
    s_ctx_signal(ctx, -1);
}

/* ---- HTTP legacy callbacks (dap_client_request → /qos_probe) ---- */

static void s_http_probe_response_cb(dap_client_t *a_client, void *a_data, size_t a_data_size)
{
    s_qos_ctx_t *ctx = (s_qos_ctx_t *)a_client->_inheritor;
    if (!ctx) return;

    const dap_qos_echo_pkt_t *e = (const dap_qos_echo_pkt_t *)a_data;
    if (a_data_size >= sizeof(dap_qos_echo_pkt_t)
        && e->magic == DAP_QOS_ECHO_MAGIC
        && e->probe_id == ctx->probe_id) {
        uint64_t now = s_mono_ns();
        int ms = (int)((now - ctx->start_ns) / 1000000ULL);
        if (ctx->bw_mode) {
            ctx->bw_received = a_data_size;
        }
        s_ctx_signal(ctx, ms);
    } else {
        s_ctx_signal(ctx, -1);
    }
}

static void s_http_probe_error_cb(dap_client_t *a_client, void *a_arg, int a_error)
{
    (void)a_arg;
    (void)a_error;
    s_qos_ctx_t *ctx = (s_qos_ctx_t *)a_client->_inheritor;
    if (!ctx) return;
    s_ctx_signal(ctx, -1);
}

/**
 * HTTP legacy probe: POST probe payload to /qos_probe, measure round-trip.
 */
static int s_probe_latency_http(const char *a_host, uint16_t a_port, uint32_t a_timeout_ms)
{
    s_qos_ctx_t *ctx = s_ctx_new();
    if (!ctx) return -1;

    dap_client_t *l_client = dap_client_new(s_probe_stage_error_cb, NULL);
    if (!l_client) {
        s_ctx_free(ctx);
        return -1;
    }

    l_client->_inheritor = ctx;
    ctx->client = l_client;

    dap_stream_node_addr_t l_node_addr = {0};
    dap_client_set_uplink_unsafe(l_client, &l_node_addr, a_host, a_port);

    dap_qos_probe_pkt_t l_probe = {0};
    l_probe.magic = DAP_QOS_PROBE_MAGIC;
    l_probe.type  = DAP_QOS_TYPE_PROBE;
    randombytes((uint8_t *)&l_probe.probe_id, sizeof(l_probe.probe_id));
    l_probe.client_ts = s_mono_ns();

    ctx->start_ns  = l_probe.client_ts;
    ctx->probe_id  = l_probe.probe_id;

    dap_client_request(l_client, "/qos_probe",
                       &l_probe, sizeof(l_probe),
                       s_http_probe_response_cb,
                       s_http_probe_error_cb,
                       NULL);

    int result = s_ctx_wait(ctx, a_timeout_ms);

    dap_client_delete_mt(l_client);
    s_ctx_free(ctx);
    return result;
}

/**
 * UDP/TLS/DNS probe: open session via FSM → STAGE_QOS_PROBE (dedicated branch).
 * FSM flow: STAGE_BEGIN → STAGE_QOS_PROBE (probe/echo via handshake path).
 */
static int s_probe_latency_udp(dap_net_trans_t *a_trans,
                                const char *a_host, uint16_t a_port,
                                uint32_t a_timeout_ms)
{
    s_qos_ctx_t *ctx = s_ctx_new();
    if (!ctx) return -1;
    ctx->trans_type = a_trans->type;

    dap_client_t *l_client = dap_client_new(s_probe_stage_error_cb, NULL);
    if (!l_client) {
        s_ctx_free(ctx);
        return -1;
    }

    l_client->_inheritor = ctx;
    ctx->client = l_client;

    dap_stream_node_addr_t l_node_addr = {0};
    dap_client_set_uplink_unsafe(l_client, &l_node_addr, a_host, a_port);
    dap_client_set_trans_type(l_client, a_trans->type);

    ctx->start_ns = s_mono_ns();
    dap_client_go_stage(l_client, STAGE_QOS_PROBE, s_probe_stage_done_cb);

    int result = s_ctx_wait(ctx, a_timeout_ms);

    dap_client_delete_mt(l_client);
    s_ctx_free(ctx);
    return result;
}

/* ---- HTTP bandwidth test: POST BW_REQUEST to /qos_probe ---- */

static int s_bw_test_http(const char *a_host, uint16_t a_port,
                           uint32_t a_timeout_ms, uint32_t a_bw_bytes,
                           float *a_out_down_mbps)
{
    s_qos_ctx_t *ctx = s_ctx_new();
    if (!ctx) return -1;
    ctx->bw_mode = true;

    dap_client_t *l_client = dap_client_new(s_probe_stage_error_cb, NULL);
    if (!l_client) { s_ctx_free(ctx); return -1; }

    l_client->_inheritor = ctx;
    ctx->client = l_client;

    dap_stream_node_addr_t l_node_addr = {0};
    dap_client_set_uplink_unsafe(l_client, &l_node_addr, a_host, a_port);

    dap_qos_probe_pkt_t l_probe = {0};
    l_probe.magic    = DAP_QOS_PROBE_MAGIC;
    l_probe.type     = DAP_QOS_TYPE_BW_REQUEST;
    l_probe.bw_bytes = a_bw_bytes;
    randombytes((uint8_t *)&l_probe.probe_id, sizeof(l_probe.probe_id));
    l_probe.client_ts = s_mono_ns();
    ctx->start_ns     = l_probe.client_ts;
    ctx->probe_id     = l_probe.probe_id;

    dap_client_request(l_client, "/qos_probe",
                       &l_probe, sizeof(l_probe),
                       s_http_probe_response_cb, s_http_probe_error_cb, NULL);

    int result_ms = s_ctx_wait(ctx, a_timeout_ms);

    if (result_ms > 0 && ctx->bw_received > sizeof(dap_qos_echo_pkt_t)) {
        size_t payload = ctx->bw_received - sizeof(dap_qos_echo_pkt_t);
        float secs = (float)result_ms / 1000.0f;
        if (a_out_down_mbps)
            *a_out_down_mbps = ((float)payload * 8.0f) / (secs * 1e6f);
    }

    dap_client_delete_mt(l_client);
    s_ctx_free(ctx);
    return result_ms >= 0 ? 0 : -1;
}

/* ========================================================================== */
/*  Public API                                                                */
/* ========================================================================== */

int dap_net_trans_qos_probe_latency(dap_net_trans_t *a_trans,
                                     const char *a_host, uint16_t a_port,
                                     uint32_t a_timeout_ms)
{
    if (!a_trans || !a_host || !a_port) return -1;

    switch (a_trans->type) {
    case DAP_NET_TRANS_HTTP:
    case DAP_NET_TRANS_WEBSOCKET:
        return s_probe_latency_http(a_host, a_port, a_timeout_ms);

    case DAP_NET_TRANS_UDP_BASIC:
    case DAP_NET_TRANS_UDP_RELIABLE:
    case DAP_NET_TRANS_UDP_QUIC_LIKE:
    case DAP_NET_TRANS_TLS_DIRECT:
    case DAP_NET_TRANS_DNS_TUNNEL:
    default:
        return s_probe_latency_udp(a_trans, a_host, a_port, a_timeout_ms);
    }
}

int dap_net_trans_qos_measure_rtt(dap_net_trans_t *a_trans,
                                   const char *a_host, uint16_t a_port,
                                   uint32_t a_count, uint32_t a_timeout_ms,
                                   uint32_t *a_out_rtt, uint32_t *a_out_ok)
{
    if (!a_out_rtt || !a_out_ok || a_count == 0) return -1;
    *a_out_ok = 0;

    for (uint32_t i = 0; i < a_count; i++) {
        int lat = dap_net_trans_qos_probe_latency(a_trans, a_host, a_port, a_timeout_ms);
        if (lat >= 0) {
            a_out_rtt[*a_out_ok] = (uint32_t)lat;
            (*a_out_ok)++;
        }
    }
    return *a_out_ok > 0 ? 0 : -1;
}

#define BW_TEST_BYTES_DEFAULT  (1 * 1024 * 1024)
#define BW_PROBE_BURST_COUNT   32
#define BW_PROBE_PAYLOAD_SIZE  800

/**
 * UDP/TLS/DNS bandwidth estimation via probe burst:
 * sends N probe packets in rapid succession, each echoed by server (~800 bytes),
 * measures aggregate throughput from total bytes / elapsed time.
 */
static int s_bw_test_burst(dap_net_trans_t *a_trans, const char *a_host,
                            uint16_t a_port, uint32_t a_timeout_ms,
                            float *a_out_down_mbps)
{
    uint32_t n_ok = 0;
    uint64_t t_start = s_mono_ns();

    for (int i = 0; i < BW_PROBE_BURST_COUNT; i++) {
        int lat = dap_net_trans_qos_probe_latency(a_trans, a_host, a_port, a_timeout_ms);
        if (lat >= 0)
            n_ok++;
    }

    uint64_t t_end = s_mono_ns();
    if (n_ok == 0) return -1;

    float elapsed_s = (float)(t_end - t_start) / 1e9f;
    if (elapsed_s < 0.001f) elapsed_s = 0.001f;

    size_t total_bytes = (size_t)n_ok * BW_PROBE_PAYLOAD_SIZE;
    if (a_out_down_mbps)
        *a_out_down_mbps = ((float)total_bytes * 8.0f) / (elapsed_s * 1e6f);

    return 0;
}

int dap_net_trans_qos_measure_throughput(dap_net_trans_t *a_trans,
                                         const char *a_host, uint16_t a_port,
                                         uint32_t a_timeout_ms,
                                         float *a_out_down_mbps,
                                         float *a_out_up_mbps)
{
    if (!a_trans || !a_host || !a_port) return -1;

    if (a_out_down_mbps) *a_out_down_mbps = 0.0f;
    if (a_out_up_mbps)   *a_out_up_mbps   = 0.0f;

    switch (a_trans->type) {
    case DAP_NET_TRANS_HTTP:
    case DAP_NET_TRANS_WEBSOCKET:
        return s_bw_test_http(a_host, a_port, a_timeout_ms,
                              BW_TEST_BYTES_DEFAULT, a_out_down_mbps);

    default:
        return s_bw_test_burst(a_trans, a_host, a_port, a_timeout_ms, a_out_down_mbps);
    }
}
