/**
 * @file reality_tls13.c
 * @brief Minimal TLS 1.3 client for the REALITY handshake + record layer,
 *        plus the public session API (connect / write / read / close).
 *
 * Scope (see docs/WSS_REALITY_INTEGRATION_PLAN_2026-11-07.md):
 *   - ciphers 0x1301 AES-128-GCM/SHA256, 0x1302 AES-256-GCM/SHA384,
 *     0x1303 ChaCha20-Poly1305/SHA256 (server picks; capture shows 0x1302)
 *   - full handshake only, no resumption; HelloRetryRequest = hard error
 *   - groups: X25519MLKEM768 (0x11EC, server-preferred) and X25519 (0x001D)
 *   - post-handshake: NewSessionTicket ignored (incl. 1-byte mimicry stub),
 *     KeyUpdate handled per RFC 8446 4.6.3
 *
 * Wire reference: Xray-core v26.10.31 (tag pinned, commit a4d3dff5b20c...).
 */

#include "reality_internal.h"
#include <stdlib.h>

#ifdef REALITY_DEBUG
#include <stdio.h>
static void dbg_hex(const char *tag, const uint8_t *b, size_t n)
{
    fprintf(stderr, "[dbg] %s: ", tag);
    for (size_t i = 0; i < n && i < 64; i++) fprintf(stderr, "%02x", b[i]);
    fprintf(stderr, "\n");
}
static void dbg_dump(const char *path, const uint8_t *b, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(b, 1, n, f); fclose(f); }
}
#define DBG(...) fprintf(stderr, "[dbg] " __VA_ARGS__)
#else
#define dbg_hex(t, b, n) ((void)0)
#define dbg_dump(p, b, n) ((void)0)
#define DBG(...) ((void)0)
#endif

#define CT_CHANGE_CIPHER 0x14
#define CT_ALERT         0x15
#define CT_HANDSHAKE     0x16
#define CT_APP           0x17

#define HS_EE   8
#define HS_CERT 11
#define HS_CV   15
#define HS_FIN  20
#define HS_NST  4
#define HS_KU   24

#define MAX_REC      16384u
#define VLESS_CHUNK  2048u

static const uint8_t hrr_random[32] = {
    0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11,0xBE,0x1D,0x8C,0x02,0x1E,0x65,0xB8,0x91,
    0xC2,0xA2,0x11,0x16,0x7A,0xBB,0x8C,0x5E,0x07,0x9E,0x09,0xE2,0xC8,0xA8,0x33,0x9C
};

static unsigned be16(const uint8_t *p) { return ((unsigned)p[0] << 8) | p[1]; }
static void put16b(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static size_t cipher_key_len(int a_cipher)  { return a_cipher == 0x1301 ? 16 : 32; }
static int    cipher_algo(int a_cipher)     { return a_cipher == 0x1303 ? 1 : 0; }
static size_t hash_len(reality_session_t *s){ return s->hash384 ? 48 : 32; }

/* ------------------------------------------------------------------ */
/* buffered exact read                                                 */
/* ------------------------------------------------------------------ */

static int rd_exact(reality_session_t *s, uint8_t *a_dst, size_t a_n)
{
    int again_tries = 0;
    while (s->inbuf_len < a_n) {
        size_t got = 0;
        int rc = s->io->read(s->io->user, s->inbuf + s->inbuf_len,
                             sizeof(s->inbuf) - s->inbuf_len, &got);
        if (rc == REALITY_ERR_AGAIN) {
            /* SO_RCVTIMEO tick. Retrying forever froze the session silently
             * (no data, no error, no fallback). We cannot resume safely after
             * a timeout in the middle of a record -- the record header lives
             * in a caller-local buffer -- so bail out hard after a bounded
             * number of timeouts and let the caller tear the session down. */
            if (++again_tries >= 2)
                RFAIL(s, REALITY_ERR_IO, "record read timed out");
            continue;
        }
        if (rc != 0) return rc;
        again_tries = 0;
        if (got == 0) return REALITY_ERR_CLOSED;
        s->inbuf_len += got;
    }
    memcpy(a_dst, s->inbuf, a_n);
    memmove(s->inbuf, s->inbuf + a_n, s->inbuf_len - a_n);
    s->inbuf_len -= a_n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* transcript                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    int sha384;
    uint8_t buf[64 * 1024];
    size_t len;
} rtls_th_t;

static void th_init(rtls_th_t *h, int sha384) { h->sha384 = sha384; h->len = 0; }

static int th_add(reality_session_t *s, rtls_th_t *h, const uint8_t *msg, size_t len)
{
    if (h->len + len > sizeof(h->buf))
        RFAIL(s, REALITY_ERR_PROTOCOL, "transcript overflow");
    memcpy(h->buf + h->len, msg, len);
    h->len += len;
    return 0;
}

static int th_digest(reality_session_t *s, rtls_th_t *h, uint8_t *out)
{
    if (s->crypto->hash(s->crypto->user, h->sha384 ? 1 : 0, h->buf, h->len, out) != 0)
        RFAIL(s, REALITY_ERR_CRYPTO, "transcript hash failed");
    return 0;
}

/* ------------------------------------------------------------------ */
/* key schedule (RFC 8446 7.1)                                         */
/* ------------------------------------------------------------------ */

static int hkdf_expand_label(reality_session_t *s, const uint8_t *secret,
                             const char *label, const uint8_t *ctx, size_t ctx_len,
                             uint8_t *out, size_t out_len)
{
    uint8_t info[300];
    size_t n = 0;
    static const char pfx[] = "tls13 ";
    size_t ll = strlen(label) + sizeof(pfx) - 1;
    info[n++] = (uint8_t)(out_len >> 8); info[n++] = (uint8_t)out_len;
    info[n++] = (uint8_t)ll;
    memcpy(info + n, pfx, sizeof(pfx) - 1); n += sizeof(pfx) - 1;
    memcpy(info + n, label, strlen(label)); n += strlen(label);
    info[n++] = (uint8_t)ctx_len;
    if (ctx_len) { memcpy(info + n, ctx, ctx_len); n += ctx_len; }
    if (s->crypto->hkdf(s->crypto->user, s->hash384, NULL, 0, secret, hash_len(s),
                        info, n, out, out_len) != 0)
        RFAIL(s, REALITY_ERR_CRYPTO, "hkdf expand-label failed");
    return 0;
}

static int derive_secret(reality_session_t *s, const uint8_t *secret, const char *label,
                         rtls_th_t *th, uint8_t *out)
{
    uint8_t dg[48];
    if (th_digest(s, th, dg) != 0) return s->err;
    return hkdf_expand_label(s, secret, label, dg, hash_len(s), out, hash_len(s));
}

static int hkdf_extract(reality_session_t *s, const uint8_t *salt,
                        const uint8_t *ikm, size_t ikm_len, uint8_t *out)
{
    if (s->crypto->hkdf(s->crypto->user, s->hash384, salt, hash_len(s), ikm, ikm_len,
                        NULL, 0, out, hash_len(s)) != 0)
        RFAIL(s, REALITY_ERR_CRYPTO, "hkdf extract failed");
    return 0;
}

static int keys_from_secret(reality_session_t *s, const uint8_t *secret,
                            uint8_t *key, size_t *key_len, uint8_t iv[12], int *algo)
{
    *key_len = cipher_key_len(s->cipher);
    *algo = cipher_algo(s->cipher);
    if (hkdf_expand_label(s, secret, "key", NULL, 0, key, *key_len) != 0) return s->err;
    if (hkdf_expand_label(s, secret, "iv", NULL, 0, iv, 12) != 0) return s->err;
    return 0;
}

/* Derive-Secret(., label, "") uses Transcript-Hash("") = Hash("") as context
 * (NOT a zero-length context!) -- RFC 8446 4.4.1 / 7.1; matches Go tls13.deriveSecret
 * with nil transcript (fresh hash .Sum(nil)). */
static int derive_secret_empty(reality_session_t *s, const uint8_t *secret,
                               const char *label, uint8_t *out)
{
    uint8_t empty_digest[48];
    if (s->crypto->hash(s->crypto->user, s->hash384 ? 1 : 0, NULL, 0, empty_digest) != 0)
        RFAIL(s, REALITY_ERR_CRYPTO, "hash(\"\") failed");
    return hkdf_expand_label(s, secret, label, empty_digest, hash_len(s), out, hash_len(s));
}

/* ------------------------------------------------------------------ */
/* record layer                                                        */
/* ------------------------------------------------------------------ */

static void nonce12(uint8_t nonce[12], const uint8_t iv[12], uint64_t seq)
{
    memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; i++)
        nonce[11 - i] ^= (uint8_t)(seq >> (8 * i));
}

static int record_write(reality_session_t *s, uint8_t inner_type,
                        const uint8_t *payload, size_t len)
{
    if (len > MAX_REC) RFAIL(s, REALITY_ERR_PROTOCOL, "record too big");

    uint8_t plain[MAX_REC + 1];
    uint8_t out[5 + MAX_REC + 1 + 16];
    memcpy(plain, payload, len);
    plain[len] = inner_type; /* RFC 8446 5.2: content || type */

    /* header length is part of the AAD -- must be final BEFORE sealing */
    out[0] = CT_APP; out[1] = 0x03; out[2] = 0x03;
    put16b(out + 3, (unsigned)(len + 1 + 16));

    uint8_t nonce[12];
    nonce12(nonce, s->c_iv, s->c_seq);
    size_t sealed = 0;
    if (s->crypto->aead_seal(s->crypto->user, s->c_algo, s->c_key, s->c_key_len, nonce,
                             out, 5, plain, len + 1, out + 5, &sealed) != 0)
        RFAIL(s, REALITY_ERR_CRYPTO, "record seal failed");
    if (sealed != len + 1 + 16)
        RFAIL(s, REALITY_ERR_CRYPTO, "unexpected seal length");

    if (s->io->write(s->io->user, out, 5 + sealed) != 0)
        RFAIL(s, REALITY_ERR_IO, "socket write failed");
    s->c_seq++;
    return 0;
}

/**
 * Read one decrypted record. Returns inner content type via *a_type;
 * payload (inner-type byte and zero padding stripped) in a_buf.
 * Handles: CCS skip, plaintext alerts, KeyUpdate, NST skip.
 */
static int record_read(reality_session_t *s, uint8_t *a_type,
                       uint8_t *a_buf, size_t cap, size_t *a_len)
{
    for (;;) {
        uint8_t hdr[5];
        int rc = rd_exact(s, hdr, 5);
        if (rc) return rc;
        uint8_t outer = hdr[0];
        unsigned rlen = be16(hdr + 3);
        if (rlen > MAX_REC + 256)
            RFAIL(s, REALITY_ERR_PROTOCOL, "peer record too big");

        if (outer == CT_CHANGE_CIPHER) {
            uint8_t cc[MAX_REC + 256];
            rc = rd_exact(s, cc, rlen);
            if (rc) return rc;
            continue;
        }
        if (outer == CT_ALERT) {
            uint8_t al[2] = {0, 0};
            if (rlen >= 2) { rc = rd_exact(s, al, 2); if (rc) return rc; }
            RFAILF(s, REALITY_ERR_CLOSED, "TLS alert %u/%u", al[0], al[1]);
        }
        if (outer != CT_APP)
            RFAILF(s, REALITY_ERR_PROTOCOL, "unexpected outer record type %u", outer);

        uint8_t body[MAX_REC + 256];
        rc = rd_exact(s, body, rlen);
        if (rc) return rc;

        uint8_t nonce[12];
        nonce12(nonce, s->s_iv, s->s_seq);
        uint8_t plain[MAX_REC + 256];
        size_t plain_len = 0;
        if (s->crypto->aead_open(s->crypto->user, s->s_algo, s->s_key, s->s_key_len,
                                 nonce, hdr, 5, body, rlen, plain, &plain_len) != 0)
            RFAIL(s, REALITY_ERR_CRYPTO, "record open failed (bad mac)");
        s->s_seq++;
        if (plain_len == 0)
            RFAIL(s, REALITY_ERR_PROTOCOL, "empty record");

        /* RFC 8446 5.2: content || type || zeros(padding). Scan back over
         * zeros; first non-zero byte = inner content type (Go semantics). */
        size_t k = plain_len;
        while (k > 0 && plain[k - 1] == 0) k--;
        if (k == 0) RFAIL(s, REALITY_ERR_PROTOCOL, "all-zero plaintext");
        uint8_t inner = plain[k - 1];
        size_t clen = k - 1;

        if (inner == CT_HANDSHAKE) {
            /* post-handshake messages: KeyUpdate / NewSessionTicket */
            size_t p = 0;
            int consumed_useful = 0;
            while (p < clen) {
                if (clen - p == 1 && plain[p] == HS_NST) { p++; continue; } /* mimicry stub */
                if (clen - p < 4)
                    RFAIL(s, REALITY_ERR_PROTOCOL, "truncated post-hs message");
                uint8_t mt = plain[p];
                size_t ml = ((size_t)plain[p+1] << 16) | ((size_t)plain[p+2] << 8) | plain[p+3];
                if (p + 4 + ml > clen)
                    RFAIL(s, REALITY_ERR_PROTOCOL, "post-hs message overrun");
                if (mt == HS_NST) { p += 4 + ml; continue; } /* ignore tickets */
                if (mt == HS_KU) {
                    if (ml != 1)
                        RFAIL(s, REALITY_ERR_PROTOCOL, "bad KeyUpdate len");
                    uint8_t req = plain[p + 4];
                    /* roll receiving keys */
                    uint8_t ns[48];
                    if (hkdf_expand_label(s, s->s_ap_secret, "traffic upd", NULL, 0,
                                          ns, hash_len(s)) != 0) return s->err;
                    memcpy(s->s_ap_secret, ns, hash_len(s));
                    if (keys_from_secret(s, s->s_ap_secret, s->s_key, &s->s_key_len,
                                         s->s_iv, &s->s_algo) != 0) return s->err;
                    s->s_seq = 0;
                    if (req == 1) {
                        uint8_t ku[5] = {HS_KU, 0, 0, 1, 0};
                        uint8_t ns2[48];
                        if (hkdf_expand_label(s, s->c_ap_secret, "traffic upd", NULL, 0,
                                              ns2, hash_len(s)) != 0) return s->err;
                        memcpy(s->c_ap_secret, ns2, hash_len(s));
                        if (keys_from_secret(s, s->c_ap_secret, s->c_key, &s->c_key_len,
                                             s->c_iv, &s->c_algo) != 0) return s->err;
                        s->c_seq = 0;
                        if (record_write(s, CT_HANDSHAKE, ku, 5) != 0) return s->err;
                    }
                    p += 4 + ml;
                    consumed_useful = 1;
                    continue;
                }
                RFAILF(s, REALITY_ERR_PROTOCOL, "unexpected post-hs message %u", mt);
            }
            (void)consumed_useful;
            continue; /* read next record */
        }
        if (inner == CT_ALERT) {
            if (clen >= 2 && plain[0] == 1 && plain[1] == 0) return REALITY_ERR_CLOSED;
            if (clen >= 2)
                RFAILF(s, REALITY_ERR_CLOSED, "TLS alert(inner) %u/%u", plain[0], plain[1]);
            return REALITY_ERR_CLOSED;
        }
        if (inner != CT_APP)
            RFAILF(s, REALITY_ERR_PROTOCOL, "bad inner content type %u", inner);
        if (clen > cap)
            RFAIL(s, REALITY_ERR_PROTOCOL, "record payload exceeds buffer");
        memcpy(a_buf, plain, clen);
        *a_type = inner;
        *a_len = clen;
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* ServerHello parse                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t cipher;
    uint16_t group;
    const uint8_t *share;
    size_t share_len;
} sh_t;

static int parse_sh(reality_session_t *s, const uint8_t *b, size_t len, sh_t *o)
{
    if (len < 39) RFAIL(s, REALITY_ERR_PROTOCOL, "ServerHello too short");
    if (memcmp(b + 6, hrr_random, 32) == 0)
        RFAIL(s, REALITY_ERR_PROTOCOL, "unexpected HelloRetryRequest");
    size_t p = 39 + b[38];
    if (p + 3 > len) RFAIL(s, REALITY_ERR_PROTOCOL, "SH truncated");
    o->cipher = (uint16_t)be16(b + p); p += 2;
    p += 1;
    if (p + 2 > len) RFAIL(s, REALITY_ERR_PROTOCOL, "SH truncated");
    size_t el = be16(b + p); p += 2;
    if (p + el > len) RFAIL(s, REALITY_ERR_PROTOCOL, "SH ext overrun");
    size_t end = p + el;
    int sv = 0;
    while (p + 4 <= end) {
        uint16_t et = (uint16_t)be16(b + p);
        size_t xl = be16(b + p + 2);
        const uint8_t *xb = b + p + 4;
        if (et == 0x002b) {
            if (xl < 2 || be16(xb) != 0x0304)
                RFAIL(s, REALITY_ERR_PROTOCOL, "negotiated version != TLS 1.3");
            sv = 1;
        } else if (et == 0x0033) {
            if (xl < 4) RFAIL(s, REALITY_ERR_PROTOCOL, "key_share too short");
            o->group = (uint16_t)be16(xb);
            o->share_len = be16(xb + 2);
            o->share = xb + 4;
            if (4 + o->share_len > xl)
                RFAIL(s, REALITY_ERR_PROTOCOL, "key_share overrun");
        }
        p += 4 + xl;
    }
    if (!sv) RFAIL(s, REALITY_ERR_PROTOCOL, "no supported_versions in SH");
    if (!o->share) RFAIL(s, REALITY_ERR_PROTOCOL, "no key_share in SH");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Certificate helpers                                                 */
/* ------------------------------------------------------------------ */

static int cert_ed25519_pub(const uint8_t *der, size_t len, const uint8_t **pub)
{
    static const uint8_t oid[] = {0x06, 0x03, 0x2B, 0x65, 0x70}; /* id-Ed25519 */
    for (size_t i = 0; i + 5 + 35 <= len; i++) {
        if (memcmp(der + i, oid, sizeof(oid)) == 0 &&
            der[i + 5] == 0x03 && der[i + 6] == 0x21 && der[i + 7] == 0x00) {
            *pub = der + i + 8;
            return 0;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Handshake driver                                                    */
/* ------------------------------------------------------------------ */

int rtls_handshake(reality_session_t *s, const char *sni)
{
    reality_crypto_ops_t *cr = (reality_crypto_ops_t *)s->crypto;

    /* --- fresh key material --- */
    uint8_t auth_pub[32];
    if (cr->x25519_keypair(cr->user, auth_pub, s->x25519_priv) != 0)
        RFAIL(s, REALITY_ERR_CRYPTO, "x25519 auth keypair failed");
    uint8_t hyb_pub[32];
    if (cr->x25519_keypair(cr->user, hyb_pub, s->hybrid_priv) != 0)
        RFAIL(s, REALITY_ERR_CRYPTO, "x25519 hybrid keypair failed");
    uint8_t mlkem_ek[1184];
    if (cr->mlkem_keypair(cr->user, mlkem_ek, s->mlkem_dk) != 0)
        RFAIL(s, REALITY_ERR_CRYPTO, "mlkem keypair failed");

    uint8_t random[32], sid[32], sid0[32] = {0};
    if (cr->rand_bytes(cr->user, random, 32) != 0)
        RFAIL(s, REALITY_ERR_CRYPTO, "rand failed");

    /* --- ClientHello: zero-sid build (AAD) -> seal -> final build --- */
    uint8_t *ch = NULL; size_t ch_len = 0;
    if (reality_fp_build_clienthello(&reality_fp_chrome, random, sid0, mlkem_ek,
                                     hyb_pub, auth_pub, sni, &ch, &ch_len) != 0)
        RFAIL(s, REALITY_ERR_NOMEM, "clienthello build failed");

    uint8_t srv_pub[32]; size_t sp = 0;
    if (reality_b64url_decode(s->cfg.public_key_b64url, srv_pub, sizeof(srv_pub), &sp) != 0
        || sp != 32) {
        free(ch);
        RFAIL(s, REALITY_ERR_PARAM, "bad server public key (b64url)");
    }
    int rc = reality_auth_seal(s, s->x25519_priv, srv_pub, s->cfg.short_id_hex,
                               ch, ch_len, random, sid);
    free(ch); ch = NULL;
    if (rc != 0) return rc;

    if (reality_fp_build_clienthello(&reality_fp_chrome, random, sid, mlkem_ek,
                                     hyb_pub, auth_pub, sni, &ch, &ch_len) != 0)
        RFAIL(s, REALITY_ERR_NOMEM, "clienthello rebuild failed");

#ifdef REALITY_DEBUG
    DBG("cipher-pending; ek/hyb/auth pub + privs:\n");
    dbg_hex("mlkem_ek[0:16]", mlkem_ek, 16);
    dbg_hex("hyb_pub", hyb_pub, 32);
    dbg_hex("hyb_priv", s->hybrid_priv, 32);
    dbg_hex("auth_pub", auth_pub, 32);
    dbg_dump("/tmp/rtls_ch.bin", ch, ch_len);
    dbg_dump("/tmp/rtls_mlkem_dk.bin", s->mlkem_dk, sizeof(s->mlkem_dk));
    dbg_dump("/tmp/rtls_ek.bin", mlkem_ek, sizeof(mlkem_ek));
#endif

    /* --- send ClientHello (plaintext record) --- */
    {
        uint8_t rec[5 + 2048];
        if (5 + ch_len > sizeof(rec)) { free(ch); RFAIL(s, REALITY_ERR_PROTOCOL, "CH too big"); }
        rec[0] = CT_HANDSHAKE; rec[1] = 0x03; rec[2] = 0x01;
        put16b(rec + 3, (unsigned)ch_len);
        memcpy(rec + 5, ch, ch_len);
        if (s->io->write(s->io->user, rec, 5 + ch_len) != 0) {
            free(ch);
            RFAIL(s, REALITY_ERR_IO, "write ClientHello failed");
        }
    }

    /* --- read ServerHello (plaintext; big: ML-KEM hybrid share ~1.2KB) --- */
    uint8_t sh_msg[4096];
    unsigned sh_len = 0;
    for (;;) {
        uint8_t hdr[5];
        rc = rd_exact(s, hdr, 5);
        if (rc) { free(ch); return rc; }
        unsigned rl = be16(hdr + 3);
        if (rl > MAX_REC + 256) { free(ch); RFAIL(s, REALITY_ERR_PROTOCOL, "SH record too big"); }
        if (hdr[0] == CT_CHANGE_CIPHER) {
            uint8_t cc[8];
            if (rl > sizeof(cc)) { free(ch); RFAIL(s, REALITY_ERR_PROTOCOL, "bad CCS"); }
            rc = rd_exact(s, cc, rl);
            if (rc) { free(ch); return rc; }
            continue;
        }
        if (hdr[0] == CT_ALERT) {
            uint8_t al[2] = {0, 0};
            if (rl >= 2) { rc = rd_exact(s, al, 2); if (rc) { free(ch); return rc; } }
            free(ch);
            RFAILF(s, REALITY_ERR_CLOSED, "server alert during hello: level=%u desc=%u", al[0], al[1]);
        }
        if (hdr[0] != CT_HANDSHAKE) {
            free(ch);
            RFAILF(s, REALITY_ERR_PROTOCOL, "expected SH, got record type %u", hdr[0]);
        }
        if (rl > sizeof(sh_msg)) { free(ch); RFAIL(s, REALITY_ERR_PROTOCOL, "SH too big"); }
        rc = rd_exact(s, sh_msg, rl);
        if (rc) { free(ch); return rc; }
        sh_len = rl;
        break;
    }
    if (sh_msg[0] != 0x02) { free(ch); RFAIL(s, REALITY_ERR_PROTOCOL, "not ServerHello"); }

    sh_t shi = {0};
    rc = parse_sh(s, sh_msg, sh_len, &shi);   /* full message incl. 4B hs header */
    if (rc) { free(ch); return rc; }
    s->cipher = shi.cipher;
    if (s->cipher != 0x1301 && s->cipher != 0x1302 && s->cipher != 0x1303) {
        free(ch);
        RFAILF(s, REALITY_ERR_PROTOCOL, "unsupported cipher 0x%04x", s->cipher);
    }
    s->hash384 = (s->cipher == 0x1302);

#ifdef REALITY_DEBUG
    DBG("cipher=0x%04x group=0x%04x share_len=%zu sh_len=%u\n",
        s->cipher, shi.group, shi.share_len, sh_len);
    dbg_dump("/tmp/rtls_sh.bin", sh_msg, sh_len);
    dbg_hex("sh_share[0:16]", shi.share, 16);
    if (shi.group == 0x11EC && shi.share_len == 1120)
        dbg_hex("sh_share x25519 part", shi.share + 1088, 32);
#endif

    /* --- transcript CH||SH, ECDHE --- */
    rtls_th_t th;
    th_init(&th, s->hash384);
    rc = th_add(s, &th, ch, ch_len);
    free(ch); ch = NULL;
    if (rc) return rc;
    rc = th_add(s, &th, sh_msg, sh_len);
    if (rc) return rc;

    uint8_t shared[64];
    size_t shared_len;
    if (shi.group == 0x11EC) {
        if (shi.share_len != 1120)
            RFAIL(s, REALITY_ERR_PROTOCOL, "bad hybrid share len");
        uint8_t mlkem_ss[32];
        if (cr->mlkem_decaps(cr->user, shi.share, s->mlkem_dk, mlkem_ss) != 0)
            RFAIL(s, REALITY_ERR_CRYPTO, "mlkem decaps failed");
        uint8_t x_ss[32];
        if (cr->x25519_shared(cr->user, x_ss, s->hybrid_priv, shi.share + 1088) != 0)
            RFAIL(s, REALITY_ERR_CRYPTO, "x25519 hybrid shared failed");
        memcpy(shared, mlkem_ss, 32);
        memcpy(shared + 32, x_ss, 32);
        shared_len = 64;
    } else if (shi.group == 0x001D) {
        if (shi.share_len != 32)
            RFAIL(s, REALITY_ERR_PROTOCOL, "bad x25519 share len");
        if (cr->x25519_shared(cr->user, shared, s->x25519_priv, shi.share) != 0)
            RFAIL(s, REALITY_ERR_CRYPTO, "x25519 shared failed");
        shared_len = 32;
    } else {
        RFAILF(s, REALITY_ERR_PROTOCOL, "unsupported group 0x%04x", shi.group);
    }

    /* --- key schedule --- */
    uint8_t zeros[48] = {0};
    uint8_t early[48], d0[48], hs_secret[48];
    if (hkdf_extract(s, zeros, zeros, hash_len(s), early) != 0) return s->err;
    if (derive_secret_empty(s, early, "derived", d0) != 0) return s->err;
    if (hkdf_extract(s, d0, shared, shared_len, hs_secret) != 0) return s->err;

#ifdef REALITY_DEBUG
    dbg_hex("shared", shared, shared_len);
    {
        uint8_t tdg[48];
        th_digest(s, &th, tdg);
        dbg_hex("transcript_digest(CH||SH)", tdg, hash_len(s));
    }
    dbg_hex("early_secret", early, hash_len(s));
    dbg_hex("derived0", d0, hash_len(s));
    dbg_hex("hs_secret", hs_secret, hash_len(s));
#endif

    if (derive_secret(s, hs_secret, "c hs traffic", &th, s->c_hs_secret) != 0) return s->err;
    if (derive_secret(s, hs_secret, "s hs traffic", &th, s->s_hs_secret) != 0) return s->err;

    /* RFC 8446 4.4.4: server Finished key derives from s_hs_traffic (NOT hs_secret) */
    uint8_t fin_key_s[48];
    if (hkdf_expand_label(s, s->s_hs_secret, "finished", NULL, 0, fin_key_s, hash_len(s)) != 0)
        return s->err;

#ifdef REALITY_DEBUG
    dbg_hex("c_hs_traffic", s->c_hs_secret, hash_len(s));
    dbg_hex("s_hs_traffic", s->s_hs_secret, hash_len(s));
#endif

    /* read keys = server handshake traffic */
    if (keys_from_secret(s, s->s_hs_secret, s->s_key, &s->s_key_len, s->s_iv, &s->s_algo) != 0)
        return s->err;
    s->s_seq = 0;
    /* write keys = client handshake traffic */
    if (keys_from_secret(s, s->c_hs_secret, s->c_key, &s->c_key_len, s->c_iv, &s->c_algo) != 0)
        return s->err;
    s->c_seq = 0;

    /* --- server flight: EE, Cert, CertVerify, Finished (+NST stub) --- */
    uint8_t flight[MAX_REC + 256];
    size_t flen = 0;
    int have_fin = 0, have_cert = 0, have_cv = 0, have_ee = 0;
    const uint8_t *ed_pub = NULL;
    uint8_t fin_verify[48];

    while (!have_fin) {
        rc = record_read_raw_hs(s, flight, sizeof(flight), &flen);
        if (rc) return rc;
        size_t p = 0;
        while (p < flen) {
            if (flen - p == 1 && flight[p] == HS_NST) break; /* mimicry stub */
            if (flen - p < 4)
                RFAIL(s, REALITY_ERR_PROTOCOL, "truncated hs message");
            uint8_t mt = flight[p];
            size_t ml = ((size_t)flight[p+1] << 16) | ((size_t)flight[p+2] << 8) | flight[p+3];
            const uint8_t *full = flight + p;
            size_t full_len = 4 + ml;
            if (p + full_len > flen)
                RFAIL(s, REALITY_ERR_PROTOCOL, "hs message overrun");
#ifdef REALITY_DEBUG
            DBG("flight msg: type=%u len=%zu (record %zu bytes)\n", mt, ml, flen);
#endif

            switch (mt) {
            case HS_EE:
                have_ee = 1;
                break;
            case HS_CERT: {
                if (ml < 4) RFAIL(s, REALITY_ERR_PROTOCOL, "cert msg short");
                const uint8_t *b = full + 4;
                size_t bl = ml;
                size_t cl = b[0];
                if (1 + cl + 3 > bl) RFAIL(s, REALITY_ERR_PROTOCOL, "cert ctx overrun");
                size_t q = 1 + cl;
                size_t list = ((size_t)b[q] << 16) | ((size_t)b[q+1] << 8) | b[q+2];
                q += 3;
                if (q + 3 > bl || list < 3) RFAIL(s, REALITY_ERR_PROTOCOL, "cert list");
                size_t dlen = ((size_t)b[q] << 16) | ((size_t)b[q+1] << 8) | b[q+2];
                q += 3;
                if (q + dlen > bl) RFAIL(s, REALITY_ERR_PROTOCOL, "cert der overrun");
                const uint8_t *der = b + q;
                if (cert_ed25519_pub(der, dlen, &ed_pub) != 0)
                    RFAIL(s, REALITY_ERR_VERIFY, "cert: not ed25519 (real certificate?)");
                /* REALITY evidence: HMAC-SHA512(authKey, ed_pub) == last 64B of DER */
                {
                    uint8_t mac[64];
                    if (s->crypto->hmac(s->crypto->user, 2, s->auth_key, 32,
                                        ed_pub, 32, mac) != 0)
                        RFAIL(s, REALITY_ERR_CRYPTO, "hmac-sha512 failed");
                    if (dlen < 64 || memcmp(mac, der + dlen - 64, 64) != 0)
                        RFAIL(s, REALITY_ERR_VERIFY,
                              "REALITY: received real certificate (potential MITM or redirection)");
                }
                have_cert = 1;
                break;
            }
            case HS_CV: {
                if (ml < 4) RFAIL(s, REALITY_ERR_PROTOCOL, "cv short");
                const uint8_t *b = full + 4;
                if (be16(b) != 0x0807)
                    RFAILF(s, REALITY_ERR_PROTOCOL, "cv scheme 0x%04x != ed25519", be16(b));
                size_t sl = be16(b + 2);
                if (sl != 64 || 4 + sl > ml)
                    RFAIL(s, REALITY_ERR_PROTOCOL, "cv sig len");
                /* transcript so far (CH..Certificate) */
                uint8_t dg[48];
                if (th_digest(s, &th, dg) != 0) return s->err;
                uint8_t msg[64 + 34 + 48];
                memset(msg, 0x20, 64);
                static const char ctxstr[] = "TLS 1.3, server CertificateVerify";
                memcpy(msg + 64, ctxstr, sizeof(ctxstr)); /* incl NUL = 34 */
                memcpy(msg + 64 + sizeof(ctxstr), dg, hash_len(s));
                if (!have_cert || !ed_pub)
                    RFAIL(s, REALITY_ERR_PROTOCOL, "cv before cert");
                if (s->crypto->ed25519_verify(s->crypto->user, ed_pub, msg,
                                              64 + sizeof(ctxstr) + hash_len(s), b + 4) != 0)
                    RFAIL(s, REALITY_ERR_VERIFY, "CertificateVerify ed25519 invalid");
                have_cv = 1;
                break;
            }
            case HS_FIN: {
                if (ml != hash_len(s))
                    RFAIL(s, REALITY_ERR_PROTOCOL, "finished len");
                uint8_t dg[48];
                if (th_digest(s, &th, dg) != 0) return s->err;
                if (s->crypto->hmac(s->crypto->user, s->hash384 ? 1 : 0,
                                    fin_key_s, hash_len(s), dg, hash_len(s),
                                    fin_verify) != 0)
                    RFAIL(s, REALITY_ERR_CRYPTO, "finished hmac failed");
#ifdef REALITY_DEBUG
                DBG("flight msg walk done; before-Fin check:\n");
                dbg_hex("fin transcript digest", dg, hash_len(s));
                dbg_hex("fin_key_s", fin_key_s, hash_len(s));
                dbg_hex("wire fin body", full + 4, hash_len(s));
                dbg_hex("my fin_verify", fin_verify, hash_len(s));
#endif
                if (memcmp(fin_verify, full + 4, hash_len(s)) != 0)
                    RFAIL(s, REALITY_ERR_VERIFY, "server Finished mismatch");
                have_fin = 1;
                break;
            }
            case HS_NST:
                break; /* ignore tickets */
            default:
                RFAILF(s, REALITY_ERR_PROTOCOL, "unexpected hs message %u in flight", mt);
            }
            rc = th_add(s, &th, full, full_len);
            if (rc) return rc;
            p += full_len;
        }
    }
    if (!have_ee || !have_cert || !have_cv)
        RFAIL(s, REALITY_ERR_PROTOCOL, "incomplete server flight");

    /* --- application keys (transcript CH..server Finished) --- */
    uint8_t d1[48], master[48];
    if (derive_secret_empty(s, hs_secret, "derived", d1) != 0) return s->err;
    if (hkdf_extract(s, d1, zeros, hash_len(s), master) != 0) return s->err;
    if (derive_secret(s, master, "c ap traffic", &th, s->c_ap_secret) != 0) return s->err;
    if (derive_secret(s, master, "s ap traffic", &th, s->s_ap_secret) != 0) return s->err;

    /* --- client Finished (still under handshake write keys) --- */
    {
        uint8_t fin_key_c[48];
        if (hkdf_expand_label(s, s->c_hs_secret, "finished", NULL, 0,
                              fin_key_c, hash_len(s)) != 0) return s->err;
        uint8_t dg[48], vd[48];
        if (th_digest(s, &th, dg) != 0) return s->err;
        if (s->crypto->hmac(s->crypto->user, s->hash384 ? 1 : 0,
                            fin_key_c, hash_len(s), dg, hash_len(s), vd) != 0)
            RFAIL(s, REALITY_ERR_CRYPTO, "client finished hmac failed");
        uint8_t msg[4 + 48];
        msg[0] = HS_FIN;
        msg[1] = 0; msg[2] = 0; msg[3] = (uint8_t)hash_len(s);
        memcpy(msg + 4, vd, hash_len(s));
        if (record_write(s, CT_HANDSHAKE, msg, 4 + hash_len(s)) != 0) return s->err;
    }

    /* --- switch to application keys --- */
    if (keys_from_secret(s, s->c_ap_secret, s->c_key, &s->c_key_len, s->c_iv, &s->c_algo) != 0)
        return s->err;
    s->c_seq = 0;
    if (keys_from_secret(s, s->s_ap_secret, s->s_key, &s->s_key_len, s->s_iv, &s->s_algo) != 0)
        return s->err;
    s->s_seq = 0;

    s->handshake_done = true;
    RLOGI(s, "REALITY handshake complete: cipher 0x%04x group 0x%04x", s->cipher, shi.group);
    return 0;
}

/**
 * record_read variant used during the handshake flight: like record_read but
 * passes inner handshake payloads through (record_read swallows post-hs msgs).
 */
int record_read_raw_hs(reality_session_t *s, uint8_t *buf, size_t cap, size_t *len)
{
    for (;;) {
        uint8_t hdr[5];
        int rc = rd_exact(s, hdr, 5);
        if (rc) return rc;
        unsigned rlen = be16(hdr + 3);
        if (rlen > MAX_REC + 256)
            RFAIL(s, REALITY_ERR_PROTOCOL, "peer record too big");
        if (hdr[0] == CT_CHANGE_CIPHER) {
            uint8_t cc[8];
            if (rlen > sizeof(cc)) RFAIL(s, REALITY_ERR_PROTOCOL, "bad CCS");
            rc = rd_exact(s, cc, rlen);
            if (rc) return rc;
            continue;
        }
        if (hdr[0] != CT_APP)
            RFAILF(s, REALITY_ERR_PROTOCOL, "expected app-data record, got %u", hdr[0]);
        uint8_t body[MAX_REC + 256];
        rc = rd_exact(s, body, rlen);
        if (rc) return rc;
        uint8_t nonce[12];
        nonce12(nonce, s->s_iv, s->s_seq);
        uint8_t plain[MAX_REC + 256];
        size_t plen = 0;
        if (s->crypto->aead_open(s->crypto->user, s->s_algo, s->s_key, s->s_key_len,
                                 nonce, hdr, 5, body, rlen, plain, &plen) != 0)
            RFAIL(s, REALITY_ERR_CRYPTO, "record open failed (bad mac)");
        s->s_seq++;
        if (plen == 0) RFAIL(s, REALITY_ERR_PROTOCOL, "empty record");
        /* RFC 8446 5.2 inner plaintext: content || type || zeros */
        size_t k = plen;
        while (k > 0 && plain[k - 1] == 0) k--;
        if (k == 0) RFAIL(s, REALITY_ERR_PROTOCOL, "all-zero plaintext");
        uint8_t inner = plain[k - 1];
        size_t clen = k - 1;
        if (inner != CT_HANDSHAKE)
            RFAILF(s, REALITY_ERR_PROTOCOL, "flight: inner type %u != handshake", inner);
        if (clen > cap) RFAIL(s, REALITY_ERR_PROTOCOL, "flight buffer too small");
        memcpy(buf, plain, clen);
        *len = clen;
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* App-data wrappers used by reality_session.c                         */
/* ------------------------------------------------------------------ */

int rtls_write_app(reality_session_t *s, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > MAX_REC) chunk = MAX_REC;
        int rc = record_write(s, CT_APP, buf + off, chunk);
        if (rc) return rc;
        off += chunk;
    }
    return 0;
}

int rtls_read_app(reality_session_t *s, uint8_t *buf, size_t cap, size_t *len)
{
    uint8_t type = 0;
    int rc = record_read(s, &type, buf, cap, len);
    if (rc) return rc;
    if (type != CT_APP)
        RFAIL(s, REALITY_ERR_PROTOCOL, "expected app data");
    return 0;
}

int rtls_close_notify(reality_session_t *s)
{
    uint8_t alert[2] = {1 /*warning*/, 0 /*close_notify*/};
    int rc = record_write(s, CT_ALERT, alert, 2);
    return rc; /* best effort: caller ignores */
}
