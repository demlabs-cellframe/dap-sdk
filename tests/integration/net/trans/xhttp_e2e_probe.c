/* Standalone XHTTP transport probe: drives the real xhttp_session (nghttp2 +
 * dap_tls, VLESS over HTTP/2 packet-up) against a live Xray xhttp front and
 * fetches the exit IP through the tunnel. Session-level transport verification.
 * Usage: xhttp_e2e_probe <host> <port> <uuid> <spki_hex> <http_host> <path> [dest_host] [dest_port] [iface] */
#define LOG_TAG "xhttp_e2e"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dap_common.h"
#include "dap_tls.h"
#include "xhttp/xhttp.h"
#include "carrier/carrier.h"

const xhttp_tls_ops_t *xhttp_tls_dap(void);

static int hex_decode(const char *hex, uint8_t *out, size_t out_len)
{
    if (strlen(hex) != out_len * 2) return -1;
    for (size_t i = 0; i < out_len; ++i) {
        unsigned int b;
        if (sscanf(hex + 2 * i, "%2x", &b) != 1) return -1;
        out[i] = (uint8_t)b;
    }
    return 0;
}

static int uuid_parse(const char *s, uint8_t out[16])
{
    if (strlen(s) != 36) return -1;
    int o = 0;
    for (int i = 0; i < 36 && o < 16; ++i) {
        if (s[i] == '-') continue;
        unsigned int b;
        if (i + 1 >= 36 || sscanf(s + i, "%2x", &b) != 1) return -1;
        out[o++] = (uint8_t)b; ++i;
    }
    return o == 16 ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "144.202.45.230";
    int port = argc > 2 ? atoi(argv[2]) : 443;
    const char *uuid = argc > 3 ? argv[3] : "";
    const char *pin_hex = argc > 4 ? argv[4] : "";
    const char *http_host = argc > 5 ? argv[5] : "www.samsung.com";
    const char *path = argc > 6 ? argv[6] : "/api/v1/data";
    const char *dest_host = argc > 7 ? argv[7] : "ifconfig.me";
    int dest_port = argc > 8 ? atoi(argv[8]) : 80;
    const char *iface = argc > 9 ? argv[9] : NULL;

    dap_common_init("xhttp-e2e", "/tmp/xhttp-e2e.log");
    dap_tls_init();

    xhttp_config_t cfg; memset(&cfg, 0, sizeof(cfg));
    if (uuid_parse(uuid, cfg.uuid) != 0) { fprintf(stderr, "bad uuid\n"); return 2; }
    if (hex_decode(pin_hex, cfg.spki_sha256, 32) != 0) { fprintf(stderr, "bad pin\n"); return 2; }
    cfg.have_pin = true;
    cfg.hello_profile = DAP_TLS_HELLO_CHROME;
    snprintf(cfg.sni, sizeof(cfg.sni), "%s", http_host);
    snprintf(cfg.host, sizeof(cfg.host), "%s", http_host);
    snprintf(cfg.path, sizeof(cfg.path), "%s", path);
    snprintf(cfg.dest_host, sizeof(cfg.dest_host), "%s", dest_host);
    cfg.dest_port = (uint16_t)dest_port;

    carrier_io_ops_t *io = carrier_io_socket_connect(host, (uint16_t)port, 20, iface);
    if (!io) { fprintf(stderr, "TCP connect to %s:%d failed\n", host, port); return 1; }

    xhttp_session_t *s = xhttp_session_connect(&cfg, (xhttp_io_ops_t *)io, xhttp_tls_dap());
    if (!s) { fprintf(stderr, "XHTTP SESSION CONNECT FAILED\n"); carrier_io_socket_free(io); return 1; }
    printf("xhttp session established (TLS h2 + VLESS + HTTP/2 streams)\n");

    char req[256];
    int rl = snprintf(req, sizeof(req),
                      "GET /ip HTTP/1.1\r\nHost: %s\r\nUser-Agent: curl/8.4.0\r\nConnection: close\r\n\r\n",
                      dest_host);
    if (xhttp_session_write(s, (const uint8_t *)req, (size_t)rl) != 0) {
        fprintf(stderr, "write failed\n"); xhttp_session_close(s); return 1;
    }
    uint8_t buf[4096]; size_t got = 0; char resp[16384]; size_t resp_len = 0;
    for (int i = 0; i < 600; ++i) {
        int rc = xhttp_session_read(s, buf, sizeof(buf), &got);
        if (rc == XHTTP_ERR_NONE && got) {
            if (resp_len + got < sizeof(resp)) { memcpy(resp + resp_len, buf, got); resp_len += got; }
        } else if (rc == XHTTP_ERR_CLOSED) {
            break;
        } else if (rc == XHTTP_ERR_AGAIN) {
            usleep(50000); continue;
        } else break;
    }
    resp[resp_len] = '\0';
    char *body = strstr(resp, "\r\n\r\n");
    printf("=== exit IP (want %s) ===\n%s\n", host, body ? body + 4 : resp);
    xhttp_session_close(s);
    int ok = body && strstr(body, host) != NULL;
    printf("RESULT: %s\n", ok ? "XHTTP INTEROP PASS" : "XHTTP INTEROP FAIL");
    return ok ? 0 : 1;
}