/* Standalone REALITY E2E probe: drives the real product client FSM (and thus
 * the refactored reality adapter + shared carrier_dap phase machine) against a
 * live Xray-fronted node. Localizes where the tunnel stops:
 * bridge ready -> ENC -> CTL -> STREAM -> data.
 * Usage: reality_e2e_probe <host> <port> <uuid> <pubkey_b64url> <short_id> [sni] */
#define LOG_TAG "reality_e2e"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include "dap_common.h"
#include "dap_enc.h"
#include "dap_events.h"
#include "dap_cert.h"
#include "dap_config.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_pkt.h"
#include "dap_client.h"
#include "dap_net_trans.h"

extern int dap_net_trans_http_stream_register(void);
extern int dap_net_trans_udp_stream_register_client(void);
extern int dap_net_trans_websocket_stream_register(void);
extern int dap_net_trans_dns_stream_register(void);
extern int dap_net_trans_reality_register(void);
extern int dap_stream_ctl_init(void);

static volatile int g_streaming = 0;
static volatile int g_failed = 0;
static volatile unsigned g_rx = 0;

static void s_ch_new(dap_stream_ch_t *ch, void *arg) { (void)ch;(void)arg; log_it(L_NOTICE, "CH 'S' new"); }
static void s_ch_del(dap_stream_ch_t *ch, void *arg) { (void)ch;(void)arg; log_it(L_NOTICE, "CH 'S' delete"); }
static bool s_ch_in(dap_stream_ch_t *ch, void *arg) {
    (void)ch;(void)arg;
    g_rx++;
    log_it(L_NOTICE, "CH 'S' RX callback (total %u)", g_rx);
    return true;
}
static bool s_ch_out(dap_stream_ch_t *ch, void *arg) { (void)ch;(void)arg; return true; }

static void s_stage_cb(dap_client_t *c, void *arg) {
    (void)arg;
    dap_client_stage_t st = dap_client_get_stage(c);
    dap_client_stage_status_t ss = dap_client_get_stage_status(c);
    log_it(L_NOTICE, "STAGE %-18s status=%-12s err=%s",
           dap_client_get_stage_str(c), dap_client_get_stage_status_str(c),
           dap_client_get_error_str(c));
    if (ss == STAGE_STATUS_ERROR) { g_failed = 1; }
    if (st == STAGE_STREAM_STREAMING && (ss == STAGE_STATUS_DONE || ss == STAGE_STATUS_COMPLETE)) {
        g_streaming = 1;
        log_it(L_NOTICE, "=== STREAMING reached: sending test packet on 'S' ===");
        const char msg[] = "reality-e2e-probe";
        dap_stream_ch_t *ch = dap_client_get_stream_ch_unsafe(c, 'S');
        if (ch) {
            size_t w = dap_stream_ch_pkt_write_unsafe(ch, 0x01, msg, sizeof(msg));
            log_it(L_NOTICE, "test packet write=%zu", w);
        } else log_it(L_ERROR, "no 'S' channel to write");
    }
}

static void *watchdog(void *arg) {
    int secs = (int)(intptr_t)arg;
    for (int i = 0; i < secs; i++) {
        if (g_streaming || g_failed) return NULL;
        sleep(1);
    }
    if (!g_streaming && !g_failed) {
        log_it(L_ERROR, "WATCHDOG: no STREAMING/ERROR after %ds — stuck", secs);
        _exit(3);
    }
    return NULL;
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "95.179.231.80";
    int port = argc > 2 ? atoi(argv[2]) : 443;
    const char *uuid = argc > 3 ? argv[3] : "";
    const char *pubkey = argc > 4 ? argv[4] : "";
    const char *short_id = argc > 5 ? argv[5] : "";
    const char *sni = argc > 6 ? argv[6] : "www.samsung.com";
    const char *iface = argc > 7 ? argv[7] : NULL;

    pthread_t wd; intptr_t to = 40;
    pthread_create(&wd, NULL, watchdog, (void*)to);

    dap_common_init("reality-e2e", "/tmp/reality-e2e.log");
    /* Optional outgoing iface bind (bypass a running VPN's TUN route): the
     * reality adapter reads iface from [reality:<host>:<port>] config. */
    if (iface && iface[0]) {
        mkdir("/tmp/reality-e2e-cfg", 0755);
        FILE *f = fopen("/tmp/reality-e2e-cfg/reality.cfg", "w");
        if (f) {
            fprintf(f, "[reality:%s:%d]\niface=%s\n", host, port, iface);
            fclose(f);
            g_config = dap_config_open("/tmp/reality-e2e-cfg/reality.cfg");
        }
    }
    dap_enc_init();
    dap_events_init(1, 0);
    dap_events_start();
    dap_cert_init();
    dap_cert_add_folder("/tmp/reality-e2e-certs");
    dap_net_trans_init();
    dap_net_trans_http_stream_register();
    dap_net_trans_udp_stream_register_client();
    dap_net_trans_websocket_stream_register();
    dap_net_trans_dns_stream_register();
    dap_net_trans_reality_register();
    dap_stream_init(NULL);
    dap_stream_ctl_init();
    dap_client_init();

    dap_stream_ch_proc_add('S', s_ch_new, s_ch_del, s_ch_in, s_ch_out);

    dap_client_t *c = dap_client_new(s_stage_cb, NULL);
    if (!c) { log_it(L_ERROR, "dap_client_new failed"); return 2; }
    c->no_transport_fallback = true;
    dap_client_set_trans_type(c, DAP_NET_TRANS_REALITY);
    strncpy(c->reality_uuid, uuid, sizeof(c->reality_uuid) - 1);
    strncpy(c->reality_public_key, pubkey, sizeof(c->reality_public_key) - 1);
    strncpy(c->reality_short_id, short_id, sizeof(c->reality_short_id) - 1);
    strncpy(c->reality_sni, sni, sizeof(c->reality_sni) - 1);
    dap_stream_node_addr_t node = {0};
    dap_client_set_uplink_unsafe(c, &node, host, (uint16_t)port);
    dap_client_set_active_channels_unsafe(c, "S");
    log_it(L_NOTICE, "=== connecting %s:%d transport=REALITY sni=%s uuid=%.8s... ===",
           host, port, sni, uuid);
    dap_client_go_stage(c, STAGE_STREAM_STREAMING, s_stage_cb);

    while (!g_streaming && !g_failed) usleep(100000);
    if (g_streaming) {
        log_it(L_NOTICE, "=== holding session 6s to verify downstream keepalive ===");
        for (int i = 0; i < 6 && !g_failed; i++) sleep(1);
    }
    log_it(L_NOTICE, "RESULT: streaming=%d failed=%d rx=%u final_stage=%s err=%s",
           g_streaming, g_failed, g_rx, dap_client_get_stage_str(c), dap_client_get_error_str(c));
    return (g_streaming && !g_failed) ? 0 : 1;
}