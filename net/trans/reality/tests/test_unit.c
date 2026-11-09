/**
 * @file test_unit.c
 * @brief White-box unit tests for the reality core (no network).
 *        Build: see ../CMakeLists.txt (ctest target "unit").
 */

#include "reality_internal.h"
#include "reality_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); g_fail++; } \
    else printf("ok   %s\n", msg); \
} while (0)

static const reality_crypto_ops_t *CR(void) { return reality_crypto_openssl(); }

static reality_session_t *make_bare_session(void)
{
    reality_session_t *s = (reality_session_t *)calloc(1, sizeof(*s));
    s->crypto = CR();
    s->time = reality_time_std();
    s->hash384 = 0;
    return s;
}

/* ------------------------------------------------------------------ */

static void test_parsers(void)
{
    uint8_t out[64]; size_t n = 0;
    CHECK(reality_b64url_decode("wOhxSy2vj3r1DMsUoBuNwPyElcrvj3dGbr_Mv4i4TQQ",
                                out, sizeof(out), &n) == 0 && n == 32, "b64url decode 32B");
    CHECK(out[0] == 0xC0 && out[31] == 0x04, "b64url first/last byte");

    CHECK(reality_hex_decode("b7598d95", out, sizeof(out), &n) == 0 && n == 4 &&
          out[0] == 0xB7 && out[3] == 0x95, "hex decode shortid");

    uint8_t uuid[16];
    CHECK(reality_uuid_parse("95e571e3-af83-42c4-8eb6-0c06b189b0dc", uuid) == 0 &&
          uuid[0] == 0x95 && uuid[15] == 0xDC, "uuid parse");
    CHECK(reality_uuid_parse("bad", uuid) != 0, "uuid parse rejects garbage");
}

static void test_hkdf_rfc5869(void)
{
    uint8_t ikm[22]; memset(ikm, 0x0b, sizeof(ikm));
    uint8_t salt[13]; for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;
    uint8_t info[10]; for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i);
    uint8_t okm[42];
    static const uint8_t want[42] = {
        0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a,0x90,0x43,0x4f,0x64,0xd0,0x36,
        0x2f,0x2a,0x2d,0x2d,0x0a,0x90,0xcf,0x1a,0x5a,0x4c,0x5d,0xb0,0x2d,0x56,
        0xec,0xc4,0xc5,0xbf,0x34,0x00,0x72,0x08,0xd5,0xb8,0x87,0x18,0x58,0x65
    };
    CHECK(CR()->hkdf(NULL, 0, salt, 13, ikm, 22, info, 10, okm, 42) == 0 &&
          memcmp(okm, want, 42) == 0, "HKDF-SHA256 RFC5869 A.1 vector");
}

static void test_mlkem_roundtrip(void)
{
    uint8_t ek[1184], dk[2400], ct[1088], ss1[32], ss2[32];
    CHECK(CR()->mlkem_keypair(NULL, ek, dk) == 0, "mlkem keypair");
    CHECK(CR()->mlkem_encaps(NULL, ek, ct, ss1) == 0, "mlkem encaps");
    CHECK(CR()->mlkem_decaps(NULL, ct, dk, ss2) == 0, "mlkem decaps");
    CHECK(memcmp(ss1, ss2, 32) == 0, "mlkem shared secrets match");
}

static void test_vless_codec(void)
{
    uint8_t uuid[16];
    reality_uuid_parse("95e571e3-af83-42c4-8eb6-0c06b189b0dc", uuid);
    uint8_t out[512]; size_t n = 0;
    CHECK(vless_build_request(uuid, "ifconfig.me", 80, 1, out, sizeof(out), &n) == 0,
          "vless build domain");
    /* golden: ver uuid addons(18) cmd port atyp alen name */
    CHECK(n == 52, "vless domain total 52B");
    CHECK(out[0] == 0 && memcmp(out + 1, uuid, 16) == 0, "vless ver+uuid");
    CHECK(out[17] == 18 && out[18] == 0x0A && out[19] == 0x10 &&
          memcmp(out + 20, "xtls-rprx-vision", 16) == 0, "vless addons vision");
    CHECK(out[36] == 0x01, "vless cmd TCP");
    CHECK(out[37] == 0x00 && out[38] == 0x50, "vless port 80 BE");
    CHECK(out[39] == 0x02 && out[40] == 11 && memcmp(out + 41, "ifconfig.me", 11) == 0,
          "vless domain addr");

    CHECK(vless_build_request(uuid, "1.2.3.4", 443, 1, out, sizeof(out), &n) == 0 &&
          n == 1 + 16 + 1 + 18 + 1 + 2 + 1 + 4 && out[39] == 0x01 && out[40] == 1 &&
          out[41] == 2 && out[42] == 3 && out[43] == 4, "vless ipv4 addr");
}

static void test_vision_roundtrip(void)
{
    uint8_t uuid[16]; memset(uuid, 0xAB, 16);
    vision_writer_t w; vision_reader_t r;
    vision_writer_init(&w, uuid);
    vision_reader_init(&r, uuid);

    uint8_t payload[4096];
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)(i * 7 + 1);

    uint8_t frames[16384]; size_t foff = 0;
    uint8_t recovered[8192]; size_t roff = 0;

    /* writer: first frames padded, filter window 8 -> ends padding */
    const size_t chunks[] = {5, 100, 900, 1500, 64, 33, 7, 2048, 512, 128};
    for (size_t c = 0; c < sizeof(chunks)/sizeof(chunks[0]); c++) {
        uint8_t frame[4096 + 64];
        size_t fl = vision_write(&w, payload + (c * 97) % 2048, chunks[c], frame, sizeof(frame));
        CHECK(fl >= chunks[c], "vision frame not smaller than payload");
        memcpy(frames + foff, frame, fl); foff += fl;
    }
    /* after padding ends, writer passes raw */
    size_t raw_at = foff;
    size_t fl = vision_write(&w, payload, 64, frames + foff, sizeof(frames) - foff);
    CHECK(fl == 64, "vision raw passthrough after END");
    foff += fl;

    /* reader over the whole buffer */
    size_t consumed = 0;
    int64_t got = vision_read(&r, frames, raw_at, recovered, sizeof(recovered), &consumed);
    CHECK(got >= 0 && consumed == raw_at, "vision reader consumed padded region");
    roff = (size_t)got;
    got = vision_read(&r, frames + raw_at, foff - raw_at, recovered + roff,
                      sizeof(recovered) - roff, &consumed);
    CHECK(got == 64 && consumed == foff - raw_at, "vision reader raw tail");
    roff += (size_t)got;

    /* verify chunk contents appear in order in recovered stream */
    size_t expect_off = 0; int order_ok = 1;
    size_t pad_chunks[] = {5, 100, 900, 1500, 64, 33, 7};
    for (size_t c = 0; c < sizeof(pad_chunks)/sizeof(pad_chunks[0]); c++) {
        if (memcmp(recovered + expect_off, payload + (c * 97) % 2048, pad_chunks[c]) != 0)
            order_ok = 0;
        expect_off += pad_chunks[c];
    }
    CHECK(order_ok, "vision payload order/content preserved");
    CHECK(memcmp(recovered + expect_off, payload + (7 * 97) % 2048, 64) == 0,
          "vision raw tail content");

    /* server->client DIRECT switch */
    vision_reader_t r2; vision_reader_init(&r2, uuid);
    uint8_t direct_frame[16 + 5 + 8];
    memcpy(direct_frame, uuid, 16);
    direct_frame[16] = 0x02; /* CMD_DIRECT */
    direct_frame[17] = 0; direct_frame[18] = 0;   /* content 0 */
    direct_frame[19] = 0; direct_frame[20] = 0;   /* padding 0 */
    memcpy(direct_frame + 21, "RAW DATA", 8);
    uint8_t dbuf[64]; size_t dc = 0;
    int64_t dn = vision_read(&r2, direct_frame, sizeof(direct_frame), dbuf, sizeof(dbuf), &dc);
    CHECK(dn == 8 && dc == sizeof(direct_frame) && memcmp(dbuf, "RAW DATA", 8) == 0,
          "vision DIRECT command switches to raw");
    CHECK(r2.direct, "vision reader in direct mode");
}

static void test_fp_builder(void)
{
    uint8_t random[32], sid[32], ek[1184], hyb[32], auth[32];
    for (int i = 0; i < 32; i++) { random[i] = (uint8_t)(i + 1); sid[i] = (uint8_t)(0x80 + i); }
    memset(ek, 0xEE, sizeof(ek));
    memset(hyb, 0x11, 32);
    memset(auth, 0x22, 32);

    uint8_t *ch = NULL; size_t ch_len = 0;
    CHECK(reality_fp_build_clienthello(&reality_fp_chrome, random, sid, ek, hyb, auth,
                                       "www.samsung.com", &ch, &ch_len) == 0, "fp build same-sni");
    CHECK(ch_len == reality_fp_chrome.clienthello_size, "fp size unchanged for equal sni");
    CHECK(memcmp(ch + 6, random, 32) == 0, "fp random patched");
    CHECK(memcmp(ch + 39, sid, 32) == 0, "fp session_id patched");
    CHECK(memcmp(ch + reality_fp_chrome.ks_hybrid_off, ek, 1184) == 0, "fp mlkem ek patched");
    CHECK(memcmp(ch + reality_fp_chrome.ks_hybrid_off + 1184, hyb, 32) == 0, "fp hybrid x25519");
    CHECK(memcmp(ch + reality_fp_chrome.ks_x25519_off, auth, 32) == 0, "fp auth x25519");
    CHECK(ch[0] == 0x01, "fp is ClientHello");
    unsigned hs_len = ((unsigned)ch[1] << 16) | ((unsigned)ch[2] << 8) | ch[3];
    CHECK(hs_len == ch_len - 4, "fp handshake length consistent");
    free(ch);

    /* different-length SNI: everything after shifts */
    CHECK(reality_fp_build_clienthello(&reality_fp_chrome, random, sid, ek, hyb, auth,
                                       "example.com", &ch, &ch_len) == 0, "fp build shorter sni");
    long delta = (long)strlen("example.com") - (long)reality_fp_chrome.sni_name_len;
    CHECK(ch_len == reality_fp_chrome.clienthello_size + (size_t)delta, "fp size shifted");
    hs_len = ((unsigned)ch[1] << 16) | ((unsigned)ch[2] << 8) | ch[3];
    CHECK(hs_len == ch_len - 4, "fp shifted handshake length");
    CHECK(memcmp(ch + reality_fp_chrome.ks_hybrid_off + delta, ek, 1184) == 0,
          "fp shifted ek offset");
    CHECK(memcmp(ch + reality_fp_chrome.ks_x25519_off + delta, auth, 32) == 0,
          "fp shifted auth offset");
    /* SNI lengths patched: ext elen = 5+len, list = 3+len, name len */
    size_t name_off = reality_fp_chrome.sni_name_off;
    unsigned name_len = (unsigned)((ch[name_off - 2] << 8) | ch[name_off - 1]);
    CHECK(name_len == strlen("example.com"), "fp sni name length patched");
    CHECK(memcmp(ch + name_off, "example.com", 11) == 0, "fp sni name patched");
    free(ch);
}

static void test_auth_seal_roundtrip(void)
{
    reality_session_t *s = make_bare_session();

    /* "server" static keypair */
    uint8_t srv_pub[32], srv_priv[32];
    CHECK(CR()->x25519_keypair(NULL, srv_pub, srv_priv) == 0, "auth: server keypair");

    /* client ephemeral (auth share) */
    uint8_t cli_pub[32], cli_priv[32];
    CHECK(CR()->x25519_keypair(NULL, cli_pub, cli_priv) == 0, "auth: client keypair");

    uint8_t random[32];
    CR()->rand_bytes(NULL, random, 32);

    /* build CH with zero sid, place cli_pub at both x25519 slots */
    uint8_t ek[1184]; memset(ek, 0x5A, sizeof(ek));
    uint8_t *ch = NULL; size_t ch_len = 0;
    uint8_t sid0[32] = {0};
    CHECK(reality_fp_build_clienthello(&reality_fp_chrome, random, sid0, ek, cli_pub, cli_pub,
                                       "www.samsung.com", &ch, &ch_len) == 0, "auth: build ch");

    /* publish server key as b64url into config */
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    char b64[64];
    uint8_t rt[32]; size_t rtn = 0;

    reality_config_t cfg = {0};
    snprintf(cfg.sni, sizeof(cfg.sni), "www.samsung.com");
    /* b64url of 32 bytes = 43 chars, no padding */
    {
        size_t o = 0;
        for (size_t i = 0; i < 32;) {
            unsigned v = (unsigned)srv_pub[i] << 16;
            size_t have = 1;
            if (i + 1 < 32) { v |= (unsigned)srv_pub[i+1] << 8; have = 2; }
            if (i + 2 < 32) { v |= srv_pub[i+2]; have = 3; }
            b64[o++] = tbl[(v >> 18) & 63];
            b64[o++] = tbl[(v >> 12) & 63];
            if (have >= 2) b64[o++] = tbl[(v >> 6) & 63];
            if (have >= 3) b64[o++] = tbl[v & 63];
            i += 3;
        }
        b64[o] = 0;
    }
    snprintf(cfg.public_key_b64url, sizeof(cfg.public_key_b64url), "%s", b64);
    snprintf(cfg.short_id_hex, sizeof(cfg.short_id_hex), "b7598d95");
    CHECK(reality_b64url_decode(cfg.public_key_b64url, rt, sizeof(rt), &rtn) == 0 &&
          rtn == 32 && memcmp(rt, srv_pub, 32) == 0, "auth: b64url roundtrip");

    s->cfg = cfg;
    uint8_t sid[32];
    CHECK(reality_auth_seal(s, cli_priv, srv_pub, cfg.short_id_hex, ch, ch_len,
                            random, sid) == 0, "auth: seal ok");

    /* --- simulate SERVER --- */
    uint8_t shared[32];
    CHECK(CR()->x25519_shared(NULL, shared, srv_priv, cli_pub) == 0, "auth: srv ecdh");
    uint8_t auth_key[32];
    CHECK(CR()->hkdf(NULL, 0, random, 20, shared, 32,
                     (const uint8_t *)"REALITY", 7, auth_key, 32) == 0, "auth: srv hkdf");
    uint8_t pt[32]; size_t ptl = 0;
    CHECK(CR()->aead_open(NULL, 0, auth_key, 32, random + 20, ch, ch_len,
                          sid, 32, pt, &ptl) == 0 && ptl == 16, "auth: srv open ok");
    CHECK(pt[0] == 26 && pt[1] == 10 && pt[2] == 31 && pt[3] == 0, "auth: version bytes");
    CHECK(pt[8] == 0xB7 && pt[9] == 0x59 && pt[10] == 0x8D && pt[11] == 0x95,
          "auth: shortid in plaintext");
    int64_t t = (int64_t)((uint32_t)pt[4] << 24 | (uint32_t)pt[5] << 16 |
                          (uint32_t)pt[6] << 8 | pt[7]);
    int64_t now = reality_time_std()->unix_now(NULL);
    CHECK(t > now - 120 && t < now + 120, "auth: timestamp sane");
    CHECK(memcmp(auth_key, s->auth_key, 32) == 0, "auth: client/server authKey equal");

    free(ch);
    free(s);
}

static void test_rate_limiter(void)
{
    rate_limiter_t rl;
    rate_limiter_init(&rl, 1000); /* 1000 B/s */
    uint64_t t = 1000;
    CHECK(rate_limiter_allow(&rl, t, 500), "rl: first 500B allowed");
    CHECK(rate_limiter_allow(&rl, t, 500), "rl: second 500B allowed (bucket)");
    CHECK(!rate_limiter_allow(&rl, t, 500), "rl: third 500B denied same ms");
    t += 1000; /* +1s -> refill 1000 */
    CHECK(rate_limiter_allow(&rl, t, 900), "rl: refilled after 1s");
    uint64_t d = rate_limiter_delay_ms(&rl, t, 5000);
    CHECK(d > 0, "rl: delay reported when over budget");
    rate_limiter_t rl0; rate_limiter_init(&rl0, 0);
    CHECK(rate_limiter_allow(&rl0, t, 1u << 20), "rl: rate 0 = unlimited");
}

int main(void)
{
    printf("== reality unit tests ==\n");
    test_parsers();
    test_hkdf_rfc5869();
    test_mlkem_roundtrip();
    test_vless_codec();
    test_vision_roundtrip();
    test_fp_builder();
    test_auth_seal_roundtrip();
    test_rate_limiter();
    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
