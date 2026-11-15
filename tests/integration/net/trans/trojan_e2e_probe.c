/* Standalone Trojan transport probe: drives the real trojan_session (TLS via
 * dap_tls + request header) against a live Xray trojan front and fetches the
 * exit IP through the tunnel. Verifies the transport end-to-end at the session
 * level (the stand front forwards to a raw backend, not the DAP node).
 * Usage: trojan_e2e_probe <host> <port> <password> <spki_hex> [dest_host] [dest_port] [iface] */
#define LOG_TAG "trojan_e2e"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/evp.h>
#include "dap_common.h"
#include "dap_tls.h"
#include "trojan/trojan.h"
#include "carrier/carrier.h"

const trojan_tls_ops_t *trojan_tls_dap(void);

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

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "144.202.45.230";
    int port = argc > 2 ? atoi(argv[2]) : 2053;
    const char *password = argc > 3 ? argv[3] : "";
    const char *pin_hex = argc > 4 ? argv[4] : "";
    const char *dest_host = argc > 5 ? argv[5] : "ifconfig.me";
    int dest_port = argc > 6 ? atoi(argv[6]) : 80;
    const char *iface = argc > 7 ? argv[7] : NULL;

    dap_common_init("trojan-e2e", "/tmp/trojan-e2e.log");
    dap_tls_init();

    trojan_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    uint8_t digest[28]; unsigned int dlen = 0;
    if (EVP_Digest(password, strlen(password), digest, &dlen, EVP_sha224(), NULL) != 1) {
        fprintf(stderr, "SHA224 failed\n"); return 2;
    }
    for (int i = 0; i < 28; ++i) snprintf(cfg.password_hash_hex + 2 * i, 3, "%02x", digest[i]);
    snprintf(cfg.sni, sizeof(cfg.sni), "www.samsung.com");
    if (hex_decode(pin_hex, cfg.spki_sha256, 32) != 0) {
        fprintf(stderr, "bad pin\n"); return 2;
    }
    cfg.have_pin = true;
    cfg.hello_profile = DAP_TLS_HELLO_CHROME;
    snprintf(cfg.dest_host, sizeof(cfg.dest_host), "%s", dest_host);
    cfg.dest_port = (uint16_t)dest_port;

    carrier_io_ops_t *io = carrier_io_socket_connect(host, (uint16_t)port, 15, iface);
    if (!io) { fprintf(stderr, "TCP connect to %s:%d failed\n", host, port); return 1; }

    trojan_session_t *s = trojan_session_connect(&cfg, (trojan_io_ops_t *)io, trojan_tls_dap());
    if (!s) { fprintf(stderr, "TROJAN SESSION CONNECT FAILED\n"); carrier_io_socket_free(io); return 1; }
    printf("trojan session established (TLS + header)\n");

    char req[256];
    int rl = snprintf(req, sizeof(req),
                      "GET /ip HTTP/1.1\r\nHost: %s\r\nUser-Agent: curl/8.4.0\r\nConnection: close\r\n\r\n",
                      dest_host);
    if (trojan_session_write(s, (const uint8_t *)req, (size_t)rl) != 0) {
        fprintf(stderr, "write failed\n"); trojan_session_close(s); return 1;
    }
    uint8_t buf[4096]; size_t got = 0; char resp[8192]; size_t resp_len = 0;
    for (int i = 0; i < 100; ++i) {
        int rc = trojan_session_read(s, buf, sizeof(buf), &got);
        if (rc == TROJAN_ERR_NONE && got) {
            if (resp_len + got < sizeof(resp)) { memcpy(resp + resp_len, buf, got); resp_len += got; }
        } else if (rc == TROJAN_ERR_CLOSED) {
            break;
        } else if (rc == TROJAN_ERR_AGAIN) {
            usleep(50000); continue;
        } else {
            break;
        }
    }
    resp[resp_len] = '\0';
    char *body = strstr(resp, "\r\n\r\n");
    printf("=== exit IP (want %s) ===\n%s\n", host, body ? body + 4 : resp);
    trojan_session_close(s);
    int ok = body && strstr(body, host) != NULL;
    printf("RESULT: %s\n", ok ? "TROJAN INTEROP PASS" : "TROJAN INTEROP FAIL");
    return ok ? 0 : 1;
}