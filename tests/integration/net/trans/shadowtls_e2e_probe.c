/* Standalone ShadowTLS v3 transport probe: drives the real shadowtls_session
 * (relayed TLS handshake + HMAC framing) against a live shadow-tls server, then
 * speaks SOCKS5 to its backend and fetches the exit IP through the tunnel.
 * Usage: shadowtls_e2e_probe <host> <port> <password> <sni> [dest_host] [dest_port] [iface] */
#define LOG_TAG "shadowtls_e2e"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dap_common.h"
#include "shadowtls/shadowtls.h"
#include "carrier/carrier.h"

/* carrier_io -> shadowtls_io contract adapter (carrier write returns 0 on
 * success, AGAIN=-4/CLOSED=-3; the core expects >0 bytes, AGAIN=-5/CLOSED=-4). */
typedef struct { carrier_io_ops_t *c; shadowtls_io_ops_t ops; } stls_probe_shim_t;
static int sh_read(void *u, uint8_t *b, size_t l, size_t *o)
{
    stls_probe_shim_t *sh = u;
    int rc = sh->c->read(sh->c->user, b, l, o);
    if (rc == CARRIER_OK) return SHADOWTLS_ERR_NONE;
    if (rc == CARRIER_ERR_AGAIN) return SHADOWTLS_ERR_AGAIN;
    if (rc == CARRIER_ERR_CLOSED) return SHADOWTLS_ERR_CLOSED;
    return SHADOWTLS_ERR_IO;
}
static int sh_write(void *u, const uint8_t *b, size_t l)
{
    stls_probe_shim_t *sh = u;
    int rc = sh->c->write(sh->c->user, b, l);
    if (rc == CARRIER_OK) return (int)l;
    if (rc == CARRIER_ERR_AGAIN) return SHADOWTLS_ERR_AGAIN;
    return SHADOWTLS_ERR_IO;
}
static void sh_close(void *u) { stls_probe_shim_t *sh = u; if (sh->c->close) sh->c->close(sh->c->user); free(sh); }
static int sh_fd(void *u) { stls_probe_shim_t *sh = u; return sh->c->fd ? sh->c->fd(sh->c->user) : -1; }
static void sh_shutdown(void *u) { stls_probe_shim_t *sh = u; if (sh->c->shutdown) sh->c->shutdown(sh->c->user); }

static int read_full(shadowtls_session_t *s, uint8_t *buf, size_t need)
{
    size_t off = 0;
    for (int i = 0; i < 4000 && off < need; ++i) {
        size_t got = 0;
        int rc = shadowtls_session_read(s, buf + off, need - off, &got);
        if (rc == SHADOWTLS_ERR_NONE && got) off += got;
        else if (rc == SHADOWTLS_ERR_AGAIN) { usleep(20000); continue; }
        else return -1;
    }
    return off == (size_t)need ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "144.202.45.230";
    int port = argc > 2 ? atoi(argv[2]) : 2087;
    const char *password = argc > 3 ? argv[3] : "";
    const char *sni = argc > 4 ? argv[4] : "www.samsung.com";
    const char *dest_host = argc > 5 ? argv[5] : "ifconfig.me";
    int dest_port = argc > 6 ? atoi(argv[6]) : 80;
    const char *iface = argc > 7 ? argv[7] : NULL;

    dap_common_init("shadowtls-e2e", "/tmp/shadowtls-e2e.log");

    shadowtls_config_t cfg; memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.password, sizeof(cfg.password), "%s", password);
    snprintf(cfg.sni, sizeof(cfg.sni), "%s", sni);

    carrier_io_ops_t *io = carrier_io_socket_connect(host, (uint16_t)port, 20, iface);
    if (!io) { fprintf(stderr, "TCP connect to %s:%d failed\n", host, port); return 1; }
    stls_probe_shim_t *shim = calloc(1, sizeof(*shim));
    shim->c = io;
    shim->ops.user = shim;
    shim->ops.read = sh_read; shim->ops.write = sh_write; shim->ops.close = sh_close;
    shim->ops.fd = sh_fd; shim->ops.shutdown = sh_shutdown;

    shadowtls_session_t *s = shadowtls_session_connect(&cfg, &shim->ops);
    if (!s) { fprintf(stderr, "SHADOWTLS SESSION CONNECT FAILED\n"); free(shim); carrier_io_socket_free(io); return 1; }
    printf("shadowtls session established (relayed TLS handshake + HMAC auth)\n");

    /* SOCKS5 to the backend: greeting */
    uint8_t greet[3] = { 0x05, 0x01, 0x00 };
    if (shadowtls_session_write(s, greet, 3) != 0) { fprintf(stderr, "socks greet write fail\n"); return 1; }
    uint8_t gr[2];
    if (read_full(s, gr, 2) != 0 || gr[0] != 0x05 || gr[1] != 0x00) { fprintf(stderr, "socks greet bad\n"); return 1; }
    /* CONNECT dest_host:dest_port (domain) */
    uint8_t req[262]; size_t hl = strlen(dest_host);
    req[0] = 0x05; req[1] = 0x01; req[2] = 0x00; req[3] = 0x03; req[4] = (uint8_t)hl;
    memcpy(req + 5, dest_host, hl);
    req[5 + hl] = (uint8_t)(dest_port >> 8); req[6 + hl] = (uint8_t)(dest_port & 0xFF);
    if (shadowtls_session_write(s, req, 7 + hl) != 0) { fprintf(stderr, "socks connect write fail\n"); return 1; }
    uint8_t cr[4];
    if (read_full(s, cr, 4) != 0 || cr[1] != 0x00) { fprintf(stderr, "socks connect rejected\n"); return 1; }
    size_t skip = cr[3] == 0x01 ? 6 : (cr[3] == 0x04 ? 18 : (cr[3] == 0x03 ? (size_t)cr[4] + 1 + 2 : 0));
    if (cr[3] == 0x03) { uint8_t lb; if (read_full(s, &lb, 1) != 0) return 1; skip = lb + 2; }
    uint8_t sb[256]; if (skip && read_full(s, sb, skip) != 0) { fprintf(stderr, "socks bind read fail\n"); return 1; }

    char http[256];
    int rl = snprintf(http, sizeof(http),
                      "GET /ip HTTP/1.1\r\nHost: %s\r\nUser-Agent: curl/8.4.0\r\nConnection: close\r\n\r\n",
                      dest_host);
    if (shadowtls_session_write(s, (const uint8_t *)http, (size_t)rl) != 0) { fprintf(stderr, "http write fail\n"); return 1; }
    uint8_t buf[4096]; size_t got = 0; char resp[16384]; size_t resp_len = 0;
    for (int i = 0; i < 4000; ++i) {
        int rc = shadowtls_session_read(s, buf, sizeof(buf), &got);
        if (rc == SHADOWTLS_ERR_NONE && got) {
            if (resp_len + got < sizeof(resp)) { memcpy(resp + resp_len, buf, got); resp_len += got; }
        } else if (rc == SHADOWTLS_ERR_CLOSED) break;
        else if (rc == SHADOWTLS_ERR_AGAIN) { usleep(20000); continue; }
        else break;
    }
    resp[resp_len] = '\0';
    char *body = strstr(resp, "\r\n\r\n");
    printf("=== exit IP (want %s) ===\n%s\n", host, body ? body + 4 : resp);
    shadowtls_session_close(s);
    int ok = body && strstr(body, host) != NULL;
    printf("RESULT: %s\n", ok ? "SHADOWTLS INTEROP PASS" : "SHADOWTLS INTEROP FAIL");
    return ok ? 0 : 1;
}