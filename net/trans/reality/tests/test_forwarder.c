/**
 * @file test_forwarder.c
 * @brief Forwarder test against a live node: start the local forwarder, then
 *        speak plain HTTP to 127.0.0.1:<port> with a raw socket client --
 *        exactly how the SDK's HTTP machinery will ride the tunnel.
 *        Env: same as test_interop.c.
 */

#include "reality_forwarder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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

    reality_fwd_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.bridge.rcfg.sni, sizeof(cfg.bridge.rcfg.sni), "%s", sni);
    snprintf(cfg.bridge.rcfg.public_key_b64url, sizeof(cfg.bridge.rcfg.public_key_b64url), "%s", pubkey);
    if (shortid) snprintf(cfg.bridge.rcfg.short_id_hex, sizeof(cfg.bridge.rcfg.short_id_hex), "%s", shortid);
    snprintf(cfg.bridge.rcfg.uuid, sizeof(cfg.bridge.rcfg.uuid), "%s", uuid);
    snprintf(cfg.bridge.host, sizeof(cfg.bridge.host), "%s", node);
    cfg.bridge.port = (uint16_t)atoi(port_s);
    snprintf(cfg.bridge.dest_host, sizeof(cfg.bridge.dest_host), "%s", dhost);
    cfg.bridge.dest_port = (uint16_t)atoi(dport_s);
    cfg.bridge.timeout_sec = 15;
    if (iface) snprintf(cfg.bridge.iface, sizeof(cfg.bridge.iface), "%s", iface);
    cfg.listen_port = 0; /* ephemeral */

    reality_forwarder_t *f = reality_forwarder_start(&cfg);
    if (!f) { fprintf(stderr, "forwarder start failed\n"); return 1; }
    uint16_t lport = reality_forwarder_port(f);
    printf("forwarder listening on 127.0.0.1:%u\n", lport);

    int fails = 0;
    /* three sequential plain-HTTP sessions through the forwarder */
    for (int iter = 0; iter < 3; iter++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(lport);
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
            fprintf(stderr, "iter %d: connect to forwarder failed: %s\n", iter, strerror(errno));
            close(fd);
            fails++;
            continue;
        }
        const char *req =
            "GET /ip HTTP/1.1\r\nHost: ifconfig.me\r\nUser-Agent: curl/8.4.0\r\n"
            "Accept: */*\r\nConnection: close\r\n\r\n";
        if (send(fd, req, strlen(req), 0) != (ssize_t)strlen(req)) {
            fprintf(stderr, "iter %d: send failed\n", iter);
            close(fd);
            fails++;
            continue;
        }
        char buf[8192];
        size_t total = 0;
        for (;;) {
            ssize_t n = recv(fd, buf + total, sizeof(buf) - total - 1, 0);
            if (n <= 0) break;
            total += (size_t)n;
            if (total >= sizeof(buf) - 1) break;
        }
        buf[total] = 0;
        close(fd);
        int ok = total > 0 && strstr(buf, "200") != NULL;
        printf("iter %d: %zu bytes %s\n", iter, total, ok ? "OK" : "FAIL");
        if (iter == 0 && total < 600) printf("%s\n", buf);
        if (!ok) fails++;
    }

    printf("live conns at end: %d\n", reality_forwarder_conns(f));
    reality_forwarder_stop(f);
    printf(fails ? "FORWARDER FAIL (%d)\n" : "FORWARDER PASS\n", fails);
    return fails ? 1 : 0;
}
