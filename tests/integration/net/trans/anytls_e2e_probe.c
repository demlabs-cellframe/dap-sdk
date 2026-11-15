/* Standalone AnyTLS transport probe: drives the real anytls_session (TLS + auth
 * + session frames, single stream) against a live anytls-server and fetches the
 * exit IP through the tunnel. Session-level transport verification.
 * Usage: anytls_e2e_probe <host> <port> <password> <spki_hex> [dest_host] [dest_port] [iface] */
#define LOG_TAG "anytls_e2e"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/evp.h>
#include "dap_common.h"
#include "dap_tls.h"
#include "anytls/anytls.h"
#include "carrier/carrier.h"

const anytls_tls_ops_t *anytls_tls_dap(void);

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
    int port = argc > 2 ? atoi(argv[2]) : 2083;
    const char *password = argc > 3 ? argv[3] : "";
    const char *pin_hex = argc > 4 ? argv[4] : "";
    const char *dest_host = argc > 5 ? argv[5] : "ifconfig.me";
    int dest_port = argc > 6 ? atoi(argv[6]) : 80;
    const char *iface = argc > 7 ? argv[7] : NULL;

    dap_common_init("anytls-e2e", "/tmp/anytls-e2e.log");
    dap_tls_init();

    anytls_config_t cfg; memset(&cfg, 0, sizeof(cfg));
    unsigned int dlen = 0;
    if (EVP_Digest(password, strlen(password), cfg.password_hash, &dlen, EVP_sha256(), NULL) != 1) {
        fprintf(stderr, "SHA256 failed\n"); return 2;
    }
    uint8_t md5[16]; unsigned int md5len = 0;
    if (EVP_Digest(ANYTLS_DEFAULT_PADDING_SCHEME, strlen(ANYTLS_DEFAULT_PADDING_SCHEME),
                   md5, &md5len, EVP_md5(), NULL) == 1)
        for (int i = 0; i < 16; ++i) snprintf(cfg.padding_md5 + 2 * i, 3, "%02x", md5[i]);
    snprintf(cfg.sni, sizeof(cfg.sni), "www.samsung.com");
    if (hex_decode(pin_hex, cfg.spki_sha256, 32) != 0) { fprintf(stderr, "bad pin\n"); return 2; }
    cfg.have_pin = true;
    cfg.hello_profile = DAP_TLS_HELLO_CHROME;
    snprintf(cfg.dest_host, sizeof(cfg.dest_host), "%s", dest_host);
    cfg.dest_port = (uint16_t)dest_port;

    carrier_io_ops_t *io = carrier_io_socket_connect(host, (uint16_t)port, 15, iface);
    if (!io) { fprintf(stderr, "TCP connect to %s:%d failed\n", host, port); return 1; }

    anytls_session_t *s = anytls_session_connect(&cfg, (anytls_io_ops_t *)io, anytls_tls_dap());
    if (!s) { fprintf(stderr, "ANYTLS SESSION CONNECT FAILED\n"); carrier_io_socket_free(io); return 1; }
    printf("anytls session established (TLS + auth + SYN/SYNACK)\n");

    char req[256];
    int rl = snprintf(req, sizeof(req),
                      "GET /ip HTTP/1.1\r\nHost: %s\r\nUser-Agent: curl/8.4.0\r\nConnection: close\r\n\r\n",
                      dest_host);
    if (anytls_session_write(s, (const uint8_t *)req, (size_t)rl) != 0) {
        fprintf(stderr, "write failed\n"); anytls_session_close(s); return 1;
    }
    uint8_t buf[4096]; size_t got = 0; char resp[16384]; size_t resp_len = 0;
    for (int i = 0; i < 400; ++i) {
        int rc = anytls_session_read(s, buf, sizeof(buf), &got);
        if (rc == ANYTLS_ERR_NONE && got) {
            if (resp_len + got < sizeof(resp)) { memcpy(resp + resp_len, buf, got); resp_len += got; }
        } else if (rc == ANYTLS_ERR_CLOSED) {
            break;
        } else if (rc == ANYTLS_ERR_AGAIN) {
            usleep(50000); continue;
        } else break;
    }
    resp[resp_len] = '\0';
    char *body = strstr(resp, "\r\n\r\n");
    printf("=== exit IP (want %s) ===\n%s\n", host, body ? body + 4 : resp);
    anytls_session_close(s);
    int ok = body && strstr(body, host) != NULL;
    printf("RESULT: %s\n", ok ? "ANYTLS INTEROP PASS" : "ANYTLS INTEROP FAIL");
    return ok ? 0 : 1;
}