/**
 * @file test_bridge.c
 * @brief Standalone test of the thread+socketpair bridge against a live node.
 *        Mirrors exactly how the SDK adapter will drive the transport:
 *        poll(ctrl) for 'R', write request to data fd, read response.
 *        Env: same as test_interop.c (REALITY_NODE/PORT/SNI/PUBKEY/SHORTID/UUID/IFACE).
 */

#include "reality_bridge.h"
#include "reality_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>

static int wait_ctrl(int fd, char want, int timeout_ms)
{
    struct pollfd p = { .fd = fd, .events = POLLIN };
    int pr = poll(&p, 1, timeout_ms);
    if (pr <= 0) return -1;
    uint8_t ev[2] = {0, 0};
    ssize_t n = recv(fd, ev, sizeof(ev), MSG_PEEK);
    if (n < 1) return -1;
    if (ev[0] == want) {
        n = recv(fd, ev, 1, 0);
        if (ev[0] == 'E') { ssize_t m = recv(fd, ev + 1, 1, 0); (void)m; }
        return (ev[0] == 'E') ? -(int)ev[1] : 0;
    }
    /* consume unexpected event and report */
    n = recv(fd, ev, ev[0] == 'E' ? 2 : 1, 0);
    return ev[0] == 'E' ? -(int)ev[1] : -1000;
}

int main(void)
{
    const char *node = getenv("REALITY_NODE");
    const char *port_s = getenv("REALITY_PORT");
    const char *sni = getenv("REALITY_SNI");
    const char *pubkey = getenv("REALITY_PUBKEY");
    const char *shortid = getenv("REALITY_SHORTID");
    const char *uuid = getenv("REALITY_UUID");
    const char *dhost = getenv("REALITY_DEST_HOST");
    const char *dport_s = getenv("REALITY_DEST_PORT");
    const char *iface = getenv("REALITY_IFACE");

    if (!node || !port_s || !sni || !pubkey || !uuid) {
        fprintf(stderr, "set REALITY_NODE/PORT/SNI/PUBKEY/UUID env\n");
        return 2;
    }
    if (!dhost) dhost = "ifconfig.me";
    if (!dport_s) dport_s = "80";

    reality_bridge_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.rcfg.sni, sizeof(cfg.rcfg.sni), "%s", sni);
    snprintf(cfg.rcfg.public_key_b64url, sizeof(cfg.rcfg.public_key_b64url), "%s", pubkey);
    if (shortid) snprintf(cfg.rcfg.short_id_hex, sizeof(cfg.rcfg.short_id_hex), "%s", shortid);
    snprintf(cfg.rcfg.uuid, sizeof(cfg.rcfg.uuid), "%s", uuid);
    cfg.rcfg.rate_limit_bytes_per_sec = 0;
    snprintf(cfg.host, sizeof(cfg.host), "%s", node);
    cfg.port = (uint16_t)atoi(port_s);
    snprintf(cfg.dest_host, sizeof(cfg.dest_host), "%s", dhost);
    cfg.dest_port = (uint16_t)atoi(dport_s);
    cfg.timeout_sec = 15;
    if (iface) snprintf(cfg.iface, sizeof(cfg.iface), "%s", iface);

    reality_bridge_t *b = reality_bridge_start(&cfg);
    if (!b) { fprintf(stderr, "bridge start failed\n"); return 1; }

    int ctrl = reality_bridge_ctrl_fd(b);
    int data = reality_bridge_data_fd(b);

    int rc = wait_ctrl(ctrl, 'R', 20000);
    if (rc != 0) {
        fprintf(stderr, "bridge not ready: rc=%d state=%d\n", rc, reality_bridge_state(b));
        reality_bridge_stop(b);
        close(data); close(ctrl);
        return 1;
    }
    printf("bridge READY (state=%d)\n", reality_bridge_state(b));

    const char *req =
        "GET /ip HTTP/1.1\r\nHost: ifconfig.me\r\nUser-Agent: curl/8.4.0\r\n"
        "Accept: */*\r\nConnection: close\r\n\r\n";
    ssize_t w = send(data, req, strlen(req), 0);
    if (w != (ssize_t)strlen(req)) {
        fprintf(stderr, "pipe write failed\n");
        reality_bridge_stop(b);
        close(data); close(ctrl);
        return 1;
    }

    char buf[8192];
    size_t total = 0;
    for (;;) {
        struct pollfd p = { .fd = data, .events = POLLIN };
        int pr = poll(&p, 1, 15000);
        if (pr <= 0) break;
        ssize_t n = recv(data, buf + total, sizeof(buf) - total - 1, 0);
        if (n <= 0) break;
        total += (size_t)n;
        if (total >= sizeof(buf) - 1) break;
    }
    buf[total] = 0;
    printf("--- response via bridge (%zu bytes) ---\n%s---\n", total, buf);

    reality_bridge_stop(b);
    close(data); close(ctrl);

    int ok = total > 0 && strstr(buf, "200") != NULL;
    printf(ok ? "BRIDGE PASS\n" : "BRIDGE FAIL\n");
    return ok ? 0 : 1;
}
