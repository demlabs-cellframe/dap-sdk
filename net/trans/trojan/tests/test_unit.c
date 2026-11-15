/**
 * @file test_unit.c
 * @brief Offline unit tests for the Trojan core (no SDK, no network):
 *        codec golden vectors + session over mock io/tls ports.
 */

#include "trojan/trojan.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define LOG(...) do { printf("[trojan-unit] " __VA_ARGS__); printf("\n"); } while (0)

/* A fixed 56-char hex hash (SHA224 of "password" is not needed for format tests). */
static const char *HASH = "0123456789abcdef0123456789abcdef0123456789abcdef01234567";

static void test_codec_ipv4(void)
{
    LOG("test_codec_ipv4");
    uint8_t buf[512];
    int n = trojan_codec_build_request(HASH, "127.0.0.1", 8079, buf, sizeof(buf));
    /* 56 + 2 + 1 + 1 + 4 + 2 + 2 = 68 */
    assert(n == 68);
    assert(memcmp(buf, HASH, 56) == 0);
    assert(buf[56] == '\r' && buf[57] == '\n');
    assert(buf[58] == 0x01);            /* CMD CONNECT */
    assert(buf[59] == 0x01);            /* ATYP IPv4 */
    assert(buf[60] == 127 && buf[61] == 0 && buf[62] == 0 && buf[63] == 1);
    assert(buf[64] == (8079 >> 8) && buf[65] == (8079 & 0xFF));
    assert(buf[66] == '\r' && buf[67] == '\n');
}

static void test_codec_domain(void)
{
    LOG("test_codec_domain");
    uint8_t buf[512];
    const char *host = "example.com";
    int n = trojan_codec_build_request(HASH, host, 443, buf, sizeof(buf));
    /* 56 + 2 + 1 + 1 + (1+11) + 2 + 2 = 76 */
    assert(n == 76);
    assert(buf[58] == 0x01);
    assert(buf[59] == 0x03);            /* ATYP domain */
    assert(buf[60] == 11);              /* domain length */
    assert(memcmp(buf + 61, host, 11) == 0);
    assert(buf[72] == 0x01 && buf[73] == 0xBB);  /* 443 */
    assert(buf[74] == '\r' && buf[75] == '\n');
}

static void test_codec_invalid(void)
{
    LOG("test_codec_invalid");
    uint8_t buf[512];
    assert(trojan_codec_build_request("short", "127.0.0.1", 8079, buf, sizeof(buf)) == TROJAN_ERR_PARAM);
    assert(trojan_codec_build_request(HASH, "", 8079, buf, sizeof(buf)) == TROJAN_ERR_PARAM);
    assert(trojan_codec_build_request(HASH, "127.0.0.1", 0, buf, sizeof(buf)) == TROJAN_ERR_PARAM);
    assert(trojan_codec_build_request(HASH, "127.0.0.1", 8079, buf, 10) == TROJAN_ERR_PARAM);
    assert(trojan_codec_build_request(NULL, "127.0.0.1", 8079, buf, sizeof(buf)) == TROJAN_ERR_PARAM);
}

/* ---- mock ports for the session test ---- */
typedef struct {
    uint8_t written[1024]; size_t written_len;   /* tls write sink */
    uint8_t to_read[256]; size_t read_len, read_off;  /* tls read source */
    int closed;
} mock_tls_t;

static int mock_io_read(void *u, uint8_t *b, size_t l, size_t *o) { (void)u;(void)b;(void)l; *o = 0; return TROJAN_ERR_AGAIN; }
static int mock_io_write(void *u, const uint8_t *b, size_t l) { (void)u;(void)b;(void)l; return 0; }
static void mock_io_close(void *u) { (void)u; }
static int mock_io_fd(void *u) { (void)u; return 42; }
static void mock_io_shutdown(void *u) { (void)u; }

static void *mock_tls_connect(void *u, int fd, const trojan_tls_cfg_t *cfg)
{
    (void)fd; (void)cfg; return u;
}
static ssize_t mock_tls_read(void *u, void *t, void *b, size_t l)
{
    (void)t; mock_tls_t *m = u;
    size_t avail = m->read_len - m->read_off;
    if (!avail) return TROJAN_ERR_AGAIN;
    size_t n = avail < l ? avail : l;
    memcpy(b, m->to_read + m->read_off, n);
    m->read_off += n;
    return (ssize_t)n;
}
static ssize_t mock_tls_write(void *u, void *t, const void *b, size_t l)
{
    (void)t; mock_tls_t *m = u;
    if (m->written_len + l > sizeof(m->written)) return TROJAN_ERR_IO;
    memcpy(m->written + m->written_len, b, l);
    m->written_len += l;
    return (ssize_t)l;
}
static int mock_tls_pending(void *u, void *t) { (void)t; mock_tls_t *m = u; return (int)(m->read_len - m->read_off); }
static void mock_tls_close(void *u, void *t) { (void)t; mock_tls_t *m = u; m->closed = 1; }

static void test_session_mock(void)
{
    LOG("test_session_mock");
    mock_tls_t mt; memset(&mt, 0, sizeof(mt));
    const char payload[] = "hello-trojan";
    memcpy(mt.to_read, payload, sizeof(payload));
    mt.read_len = sizeof(payload);

    trojan_io_ops_t io = { .user = NULL, .read = mock_io_read, .write = mock_io_write,
                           .close = mock_io_close, .fd = mock_io_fd, .shutdown = mock_io_shutdown };
    trojan_tls_ops_t tls = { .user = &mt, .connect = mock_tls_connect, .read = mock_tls_read,
                             .write = mock_tls_write, .pending = mock_tls_pending, .close = mock_tls_close };
    trojan_config_t cfg; memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.password_hash_hex, sizeof(cfg.password_hash_hex), "%s", HASH);
    snprintf(cfg.sni, sizeof(cfg.sni), "www.samsung.com");
    snprintf(cfg.dest_host, sizeof(cfg.dest_host), "127.0.0.1");
    cfg.dest_port = 8079;

    trojan_session_t *s = trojan_session_connect(&cfg, &io, &tls);
    assert(s);
    /* the request header (68 bytes for IPv4 dest) was written on connect */
    assert(mt.written_len == 68);
    assert(memcmp(mt.written, HASH, 56) == 0);
    assert(mt.written[58] == 0x01 && mt.written[59] == 0x01);

    assert(trojan_session_has_pending(s));
    uint8_t buf[64]; size_t got = 0;
    assert(trojan_session_read(s, buf, sizeof(buf), &got) == TROJAN_ERR_NONE);
    assert(got == sizeof(payload) && memcmp(buf, payload, got) == 0);
    assert(!trojan_session_has_pending(s));

    assert(trojan_session_write(s, (const uint8_t *)"up", 2) == TROJAN_ERR_NONE);
    assert(mt.written_len == 70);

    trojan_session_close(s);
    assert(mt.closed == 1);
}

int main(void)
{
    test_codec_ipv4();
    test_codec_domain();
    test_codec_invalid();
    test_session_mock();
    LOG("ALL TROJAN UNIT TESTS PASSED");
    return 0;
}