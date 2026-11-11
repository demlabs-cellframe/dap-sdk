/**
 * @file test_interop.c
 * @brief Live interop test: full REALITY+VLESS+Vision session against a real
 *        Xray v26.10.31 node. Env-configured (see ../CMakeLists.txt):
 *        REALITY_NODE REALITY_PORT REALITY_SNI REALITY_PUBKEY REALITY_SHORTID
 *        REALITY_UUID [REALITY_DEST_HOST REALITY_DEST_PORT REALITY_HTTP_PATH]
 */

#include "reality/reality.h"
#include "reality_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static void log_cb(void *u, int lvl, const char *fmt, ...)
{
    (void)u;
    static const char *tags[] = {"DBG", "INF", "WRN", "ERR"};
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s] ", tags[lvl & 3]);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

int main(void)
{
    const char *node    = getenv("REALITY_NODE");
    const char *port_s  = getenv("REALITY_PORT");
    const char *sni     = getenv("REALITY_SNI");
    const char *pubkey  = getenv("REALITY_PUBKEY");
    const char *shortid = getenv("REALITY_SHORTID");
    const char *uuid    = getenv("REALITY_UUID");
    const char *dhost   = getenv("REALITY_DEST_HOST");
    const char *dport_s = getenv("REALITY_DEST_PORT");
    const char *path    = getenv("REALITY_HTTP_PATH");

    if (!node || !port_s || !sni || !pubkey || !uuid) {
        fprintf(stderr, "set REALITY_NODE/PORT/SNI/PUBKEY/UUID env\n");
        return 2;
    }
    if (!dhost) dhost = "ifconfig.me";
    if (!dport_s) dport_s = "80";
    if (!path) path = "/ip";

    reality_log_ops_t log = { .user = NULL, .log = log_cb };

    reality_config_t cfg = {0};
    snprintf(cfg.sni, sizeof(cfg.sni), "%s", sni);
    snprintf(cfg.public_key_b64url, sizeof(cfg.public_key_b64url), "%s", pubkey);
    if (shortid) snprintf(cfg.short_id_hex, sizeof(cfg.short_id_hex), "%s", shortid);
    snprintf(cfg.uuid, sizeof(cfg.uuid), "%s", uuid);
    const char *rate_s = getenv("REALITY_RATE_LIMIT");
    cfg.rate_limit_bytes_per_sec = rate_s ? (uint32_t)strtoul(rate_s, NULL, 10) : 0;

    reality_io_ops_t *io = reality_io_socket_connect(node, (uint16_t)atoi(port_s), 15,
                                                     getenv("REALITY_IFACE"));
    if (!io) { fprintf(stderr, "TCP connect to %s:%s failed\n", node, port_s); return 1; }

    uint64_t t0 = now_ms();
    reality_session_t *s = reality_session_connect(&cfg, io, reality_crypto_openssl(),
                                                   reality_time_std(), &log,
                                                   dhost, (uint16_t)atoi(dport_s));
    uint64_t t1 = now_ms();
    if (!s) {
        fprintf(stderr, "SESSION CONNECT FAILED\n");
        reality_io_socket_free(io);
        return 1;
    }
    printf("handshake+vless ok in %llu ms\n", (unsigned long long)(t1 - t0));

    char req[512];
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: curl/8.4.0\r\n"
                      "Accept: */*\r\nConnection: close\r\n\r\n", path, dhost);
    if (reality_session_write(s, (const uint8_t *)req, (size_t)rl) != 0) {
        fprintf(stderr, "write failed\n");
        reality_session_close(s);
        reality_io_socket_free(io);
        return 1;
    }

    uint8_t buf[262144];
    size_t total = 0;
    for (;;) {
        size_t n = 0;
        int rc = reality_session_read(s, buf + total, sizeof(buf) - total - 1, &n);
        if (rc != 0) {
            fprintf(stderr, "read failed rc=%d err=%s\n", rc, reality_last_error(s));
            break;
        }
        if (n == 0) break;
        total += n;
        if (total >= sizeof(buf) - 1) break;
    }
    buf[total] = 0;
    uint64_t t2 = now_ms();

    {
        double secs = (double)(t2 - t1) / 1000.0;
        double kbps = secs > 0 ? (double)total / secs / 1024.0 : 0.0;
        printf("--- response (%zu bytes, %llu ms, %.1f KB/s) ---\n%s\n---\n",
               total, (unsigned long long)(t2 - t1), kbps,
               total > 600 ? "(truncated)" : (char *)buf);
    }

    int ok = total > 0 && strstr((char *)buf, "200") != NULL;
    reality_session_close(s);
    reality_io_socket_free(io);
    printf(ok ? "INTEROP PASS\n" : "INTEROP FAIL (no HTTP 200)\n");
    return ok ? 0 : 1;
}
