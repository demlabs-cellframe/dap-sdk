/**
 * @file test_unit.c
 * @brief Offline unit tests for the AnyTLS core (no SDK, no network):
 *        codec vectors + session over mock io/tls ports.
 */

#include "anytls/anytls.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define LOG(...) do { printf("[anytls-unit] " __VA_ARGS__); printf("\n"); } while (0)

static void test_codec_auth(void)
{
    LOG("test_codec_auth");
    uint8_t hash[32]; for (int i = 0; i < 32; ++i) hash[i] = (uint8_t)i;
    uint8_t buf[128];
    int n = anytls_codec_build_auth(hash, 30, buf, sizeof(buf));
    assert(n == 64);
    assert(memcmp(buf, hash, 32) == 0);
    assert(buf[32] == 0 && buf[33] == 30);   /* pad0 len BE */
    assert(anytls_codec_build_auth(hash, 30, buf, 10) == ANYTLS_ERR_PARAM);
}

static void test_codec_frame(void)
{
    LOG("test_codec_frame");
    uint8_t buf[64];
    const uint8_t data[] = {0xAA, 0xBB};
    int n = anytls_codec_build_frame(2 /*PSH*/, 0x01020304, data, 2, buf, sizeof(buf));
    assert(n == 9);
    assert(buf[0] == 2);
    assert(buf[1] == 0x01 && buf[2] == 0x02 && buf[3] == 0x03 && buf[4] == 0x04);
    assert(buf[5] == 0 && buf[6] == 2);
    assert(buf[7] == 0xAA && buf[8] == 0xBB);
    /* empty data frame */
    n = anytls_codec_build_frame(1 /*SYN*/, 1, NULL, 0, buf, sizeof(buf));
    assert(n == 7 && buf[0] == 1 && buf[4] == 1 && buf[6] == 0);
}

static void test_codec_socksaddr(void)
{
    LOG("test_codec_socksaddr");
    uint8_t buf[264];
    int n = anytls_codec_build_socksaddr("127.0.0.1", 8079, buf, sizeof(buf));
    assert(n == 7);
    assert(buf[0] == 0x01);
    assert(buf[1] == 127 && buf[4] == 1);
    assert(buf[5] == (8079 >> 8) && buf[6] == (8079 & 0xFF));
    n = anytls_codec_build_socksaddr("example.com", 443, buf, sizeof(buf));
    assert(n == 1 + 1 + 11 + 2);
    assert(buf[0] == 0x03 && buf[1] == 11);
    assert(memcmp(buf + 2, "example.com", 11) == 0);
}

/* ---- mock ports ---- */
typedef struct {
    uint8_t written[8192]; size_t written_len;
    uint8_t to_read[8192]; size_t read_len, read_off;
    int closed;
} mock_tls_t;

static int mock_io_read(void *u, uint8_t *b, size_t l, size_t *o) { (void)u;(void)b;(void)l; *o=0; return ANYTLS_ERR_AGAIN; }
static int mock_io_write(void *u, const uint8_t *b, size_t l) { (void)u;(void)b;(void)l; return 0; }
static void mock_io_close(void *u) { (void)u; }
static int mock_io_fd(void *u) { (void)u; return 42; }
static void mock_io_shutdown(void *u) { (void)u; }

static void *mock_tls_connect(void *u, int fd, const anytls_tls_cfg_t *c) { (void)fd;(void)c; return u; }
static ssize_t mock_tls_read(void *u, void *t, void *b, size_t l)
{
    (void)t; mock_tls_t *m = u;
    size_t avail = m->read_len - m->read_off;
    if (!avail) return ANYTLS_ERR_AGAIN;
    size_t n = avail < l ? avail : l;
    memcpy(b, m->to_read + m->read_off, n); m->read_off += n;
    return (ssize_t)n;
}
static ssize_t mock_tls_write(void *u, void *t, const void *b, size_t l)
{
    (void)t; mock_tls_t *m = u;
    if (m->written_len + l > sizeof(m->written)) return ANYTLS_ERR_IO;
    memcpy(m->written + m->written_len, b, l); m->written_len += l;
    return (ssize_t)l;
}
static int mock_tls_pending(void *u, void *t) { (void)t; mock_tls_t *m = u; return (int)(m->read_len - m->read_off); }
static void mock_tls_close(void *u, void *t) { (void)t; mock_tls_t *m = u; m->closed = 1; }

static void test_session_mock(void)
{
    LOG("test_session_mock");
    mock_tls_t mt; memset(&mt, 0, sizeof(mt));

    /* Pre-load server frames: cmdServerSettings(v=2) + cmdSYNACK(stream 1) +
     * cmdPSH(stream 1, "hello-anytls"). */
    size_t off = 0;
    const char *sset = "v=2\n";
    off += anytls_codec_build_frame(10, 0, (const uint8_t *)sset, strlen(sset), mt.to_read + off, sizeof(mt.to_read) - off);
    off += anytls_codec_build_frame(7, 1, NULL, 0, mt.to_read + off, sizeof(mt.to_read) - off);
    const char *payload = "hello-anytls";
    off += anytls_codec_build_frame(2, 1, (const uint8_t *)payload, strlen(payload), mt.to_read + off, sizeof(mt.to_read) - off);
    mt.read_len = off;

    anytls_io_ops_t io = { .user=NULL, .read=mock_io_read, .write=mock_io_write,
                           .close=mock_io_close, .fd=mock_io_fd, .shutdown=mock_io_shutdown };
    anytls_tls_ops_t tls = { .user=&mt, .connect=mock_tls_connect, .read=mock_tls_read,
                             .write=mock_tls_write, .pending=mock_tls_pending, .close=mock_tls_close };
    anytls_config_t cfg; memset(&cfg, 0, sizeof(cfg));
    for (int i = 0; i < 32; ++i) cfg.password_hash[i] = (uint8_t)i;
    snprintf(cfg.sni, sizeof(cfg.sni), "www.samsung.com");
    snprintf(cfg.dest_host, sizeof(cfg.dest_host), "127.0.0.1");
    cfg.dest_port = 8079;
    snprintf(cfg.padding_md5, sizeof(cfg.padding_md5), "0123456789abcdef0123456789abcdef");

    anytls_session_t *s = anytls_session_connect(&cfg, &io, &tls);
    assert(s);
    /* auth written first: 32 hash + 2 padlen + pad0 */
    assert(mt.written_len >= 34);
    assert(memcmp(mt.written, cfg.password_hash, 32) == 0);

    /* read returns the PSH payload */
    uint8_t buf[64]; size_t got = 0;
    assert(anytls_session_read(s, buf, sizeof(buf), &got) == ANYTLS_ERR_NONE);
    assert(got == strlen(payload) && memcmp(buf, payload, got) == 0);

    /* write produces a PSH frame (cmd 2, stream 1) somewhere in the output */
    size_t before = mt.written_len;
    assert(anytls_session_write(s, (const uint8_t *)"up", 2) == ANYTLS_ERR_NONE);
    assert(mt.written_len > before);

    anytls_session_close(s);
    assert(mt.closed == 1);
}

int main(void)
{
    test_codec_auth();
    test_codec_frame();
    test_codec_socksaddr();
    test_session_mock();
    LOG("ALL ANYTLS UNIT TESTS PASSED");
    return 0;
}