/**
 * @file shadowtls_tls13.c
 * @brief Minimal manual TLS 1.3 client for ShadowTLS v3.
 *
 * Why manual: ShadowTLS signs the ClientHello legacy_session_id (last 4 bytes
 * = HMAC-SHA1(password, ClientHello)). OpenSSL cannot set the client
 * session_id, and patching it post-hoc breaks the TLS 1.3 transcript hash and
 * the legacy_session_id_echo check. Building the ClientHello ourselves keeps
 * the transcript consistent (the signed session_id is part of the hashed CH).
 *
 * Scope: X25519 (0x001D); ciphers 0x1301/0x1302/0x1303; full handshake, no
 * resumption; ServerRandom extracted for the ShadowTLS HMAC keying. Crypto via
 * OpenSSL EVP. After the handshake the caller switches to ShadowTLS raw
 * ApplicationData framing (this layer only does the relayed TLS handshake).
 */

#include "shadowtls/shadowtls.h"
#include "shadowtls_tls13.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>

#include <stdlib.h>
#include <string.h>

#define CT_HANDSHAKE 0x16
#define CT_ALERT     0x15
#define CT_CCS       0x14
#define CT_APP       0x17

static unsigned be16(const uint8_t *p) { return ((unsigned)p[0] << 8) | p[1]; }
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put24(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 16); p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)v; }

/* ---- ClientHello builder ---- */
/* Builds a standard TLS 1.3 ClientHello (X25519). session_id[28..32] left zero
 * for the caller to fill with the HMAC. Returns handshake message (incl. 4B
 * header); *sid_off = offset of session_id within the message. */
static uint8_t *build_client_hello(const char *sni, const uint8_t random[32],
                                   const uint8_t x25519_pub[32], uint8_t sid[32],
                                   size_t *out_len, size_t *sid_off)
{
    size_t sni_len = strlen(sni);
    /* extensions */
    uint8_t ext[1024]; size_t e = 0;
    /* server_name */
    ext[e++] = 0x00; ext[e++] = 0x00;
    size_t sni_ext_len_off = e; e += 2;
    size_t sni_list_len_off = e; e += 2;
    ext[e++] = 0x00; /* host_name */
    put16(ext + e, (unsigned)sni_len); e += 2;
    memcpy(ext + e, sni, sni_len); e += sni_len;
    put16(ext + sni_ext_len_off, (unsigned)(e - sni_ext_len_off - 2));
    put16(ext + sni_list_len_off, (unsigned)(e - sni_list_len_off - 2));
    /* supported_groups: x25519 */
    ext[e++] = 0x00; ext[e++] = 0x0a; put16(ext + e, 4); e += 2;
    put16(ext + e, 2); e += 2; ext[e++] = 0x00; ext[e++] = 0x1d;
    /* signature_algorithms */
    ext[e++] = 0x00; ext[e++] = 0x0d; put16(ext + e, 20); e += 2;
    put16(ext + e, 18); e += 2;
    const uint8_t sigalgs[] = { 0x04,0x03, 0x05,0x03, 0x06,0x03, 0x08,0x04, 0x08,0x05,
                                0x08,0x06, 0x04,0x01, 0x05,0x01, 0x06,0x01 };
    memcpy(ext + e, sigalgs, sizeof(sigalgs)); e += sizeof(sigalgs);
    /* supported_versions: TLS 1.3 */
    ext[e++] = 0x00; ext[e++] = 0x2b; put16(ext + e, 3); e += 2;
    ext[e++] = 0x02; ext[e++] = 0x03; ext[e++] = 0x04;
    /* key_share: x25519 */
    ext[e++] = 0x00; ext[e++] = 0x33; put16(ext + e, 38); e += 2;
    put16(ext + e, 36); e += 2;
    ext[e++] = 0x00; ext[e++] = 0x1d; put16(ext + e, 32); e += 2;
    memcpy(ext + e, x25519_pub, 32); e += 32;

    size_t cs_len = 6; /* 3 ciphers */
    size_t body_len = 2 + 32 + 1 + 32 + 2 + cs_len + 1 + 1 + 2 + e;
    size_t msg_len = 4 + body_len;
    uint8_t *m = (uint8_t *)malloc(msg_len);
    if (!m) return NULL;
    size_t o = 0;
    m[o++] = 0x01; put24(m + o, (unsigned)body_len); o += 3;
    m[o++] = 0x03; m[o++] = 0x03;
    memcpy(m + o, random, 32); o += 32;
    m[o++] = 32;
    *sid_off = o;
    memcpy(m + o, sid, 32); o += 32;
    put16(m + o, (unsigned)cs_len); o += 2;
    m[o++] = 0x13; m[o++] = 0x01; m[o++] = 0x13; m[o++] = 0x02; m[o++] = 0x13; m[o++] = 0x03;
    m[o++] = 0x01; m[o++] = 0x00;
    put16(m + o, (unsigned)e); o += 2;
    memcpy(m + o, ext, e); o += e;
    *out_len = o;
    return m;
}

/* ---- crypto helpers (HKDF via HMAC, portable) ---- */
static int hkdf_expand(int sha384, const uint8_t *prk, size_t prk_len,
                       const uint8_t *info, size_t info_len, uint8_t *out, size_t out_len)
{
    const EVP_MD *md = sha384 ? EVP_sha384() : EVP_sha256();
    size_t hash_len = sha384 ? 48 : 32;
    uint8_t t[48]; size_t t_len = 0;
    size_t done = 0; uint8_t ctr = 1;
    while (done < out_len) {
        HMAC_CTX *h = HMAC_CTX_new();
        if (!h) return -1;
        HMAC_Init_ex(h, prk, (int)prk_len, md, NULL);
        if (t_len) HMAC_Update(h, t, t_len);
        if (info_len) HMAC_Update(h, info, info_len);
        HMAC_Update(h, &ctr, 1);
        unsigned int len = 0; HMAC_Final(h, t, &len);
        HMAC_CTX_free(h);
        t_len = len;
        size_t cp = out_len - done < t_len ? out_len - done : t_len;
        memcpy(out + done, t, cp);
        done += cp; ctr++;
    }
    return 0;
}

static int hkdf_expand_label(int sha384, const uint8_t *secret, size_t secret_len,
                             const char *label, const uint8_t *ctx, size_t ctx_len,
                             uint8_t *out, size_t out_len)
{
    uint8_t info[300]; size_t n = 0;
    static const char pfx[] = "tls13 ";
    size_t ll = strlen(label) + sizeof(pfx) - 1;
    info[n++] = (uint8_t)(out_len >> 8); info[n++] = (uint8_t)out_len;
    info[n++] = (uint8_t)ll;
    memcpy(info + n, pfx, sizeof(pfx) - 1); n += sizeof(pfx) - 1;
    memcpy(info + n, label, strlen(label)); n += strlen(label);
    info[n++] = (uint8_t)ctx_len;
    if (ctx_len) { memcpy(info + n, ctx, ctx_len); n += ctx_len; }
    return hkdf_expand(sha384, secret, secret_len, info, n, out, out_len);
}

static int hkdf_extract(int sha384, const uint8_t *salt, size_t salt_len,
                        const uint8_t *ikm, size_t ikm_len, uint8_t *out)
{
    const EVP_MD *md = sha384 ? EVP_sha384() : EVP_sha256();
    uint8_t zero[48] = {0};
    HMAC_CTX *h = HMAC_CTX_new();
    if (!h) return -1;
    if (!salt || salt_len == 0) HMAC_Init_ex(h, zero, sha384 ? 48 : 32, md, NULL);
    else HMAC_Init_ex(h, salt, (int)salt_len, md, NULL);
    HMAC_Update(h, ikm, ikm_len);
    unsigned int len = 0; HMAC_Final(h, out, &len);
    HMAC_CTX_free(h);
    return 0;
}

static void hash_buf(int sha384, const uint8_t *data, size_t len, uint8_t *out)
{
    if (sha384) SHA384(data, len, out); else SHA256(data, len, out);
}

/* ---- record io ---- */
static int rd_exact(stls_tls_t *t, uint8_t *dst, size_t n)
{
    int again = 0;
    while (t->inbuf_len < n) {
        size_t got = 0;
        int rc = t->io->read(t->io->user, t->inbuf + t->inbuf_len, sizeof(t->inbuf) - t->inbuf_len, &got);
        if (rc == SHADOWTLS_ERR_AGAIN) { if (++again >= 4) return SHADOWTLS_ERR_IO; continue; }
        if (rc != SHADOWTLS_ERR_NONE) return rc;
        again = 0;
        if (got == 0) return SHADOWTLS_ERR_CLOSED;
        /* v2 challenge: capture all raw server bytes during handshake */
        if (!t->handshake_done) {
            uint8_t *nb = realloc(t->hs_capture, t->hs_capture_len + got);
            if (nb) { memcpy(nb + t->hs_capture_len, t->inbuf + t->inbuf_len, got); t->hs_capture = nb; t->hs_capture_len += got; }
        }
        t->inbuf_len += got;
    }
    memcpy(dst, t->inbuf, n);
    memmove(t->inbuf, t->inbuf + n, t->inbuf_len - n);
    t->inbuf_len -= n;
    return SHADOWTLS_ERR_NONE;
}

static int wr_all(stls_tls_t *t, const uint8_t *d, size_t len)
{
    size_t off = 0; int again = 0;
    while (off < len) {
        ssize_t w = t->io->write(t->io->user, d + off, len - off);
        if (w > 0) { off += (size_t)w; again = 0; continue; }
        if (w == SHADOWTLS_ERR_AGAIN) { if (++again >= 32) return SHADOWTLS_ERR_IO; continue; }
        return SHADOWTLS_ERR_IO;
    }
    return SHADOWTLS_ERR_NONE;
}

static int wr_record(stls_tls_t *t, uint8_t type, const uint8_t *data, size_t len)
{
    uint8_t hdr[5] = { type, 0x03, 0x03, 0, 0 };
    put16(hdr + 3, (unsigned)len);
    if (wr_all(t, hdr, 5) != SHADOWTLS_ERR_NONE) return SHADOWTLS_ERR_IO;
    if (len && wr_all(t, data, len) != SHADOWTLS_ERR_NONE) return SHADOWTLS_ERR_IO;
    return SHADOWTLS_ERR_NONE;
}

/* read one handshake message (reassembled across records); returns malloc'd msg */
static int rd_hs_msg(stls_tls_t *t, uint8_t **out, size_t *out_len)
{
    uint8_t *msg = NULL; size_t msg_len = 0, need = 4;
    for (;;) {
        uint8_t hdr[5];
        int rc = rd_exact(t, hdr, 5);
        if (rc) { free(msg); return rc; }
        unsigned rl = be16(hdr + 3);
        if (hdr[0] == CT_CCS) { uint8_t cc[8]; if (rl > 8) { free(msg); return SHADOWTLS_ERR_TLS; } rc = rd_exact(t, cc, rl); if (rc) { free(msg); return rc; } continue; }
        if (hdr[0] == CT_ALERT) { free(msg); return SHADOWTLS_ERR_CLOSED; }
        if (hdr[0] != CT_HANDSHAKE) { free(msg); return SHADOWTLS_ERR_TLS; }
        uint8_t *rec = (uint8_t *)malloc(rl);
        if (!rec) { free(msg); return SHADOWTLS_ERR_NOMEM; }
        rc = rd_exact(t, rec, rl);
        if (rc) { free(rec); free(msg); return rc; }
        uint8_t *nb = realloc(msg, msg_len + rl);
        if (!nb) { free(rec); free(msg); return SHADOWTLS_ERR_NOMEM; }
        memcpy(nb + msg_len, rec, rl); msg = nb; msg_len += rl; free(rec);
        if (msg_len >= 4) {
            need = 4 + (((size_t)msg[1] << 16) | ((size_t)msg[2] << 8) | msg[3]);
            if (msg_len >= need) break;
        }
    }
    *out = msg; *out_len = msg_len;
    return SHADOWTLS_ERR_NONE;
}

/* ---- transcript ---- */
static void th_add(stls_tls_t *t, const uint8_t *msg, size_t len)
{
    if (t->th_len + len <= sizeof(t->th_buf)) {
        memcpy(t->th_buf + t->th_len, msg, len);
        t->th_len += len;
    }
}

static int derive_secret(stls_tls_t *t, const uint8_t *secret, const char *label, uint8_t *out)
{
    uint8_t dg[48];
    hash_buf(t->hash384, t->th_buf, t->th_len, dg);
    return hkdf_expand_label(t->hash384, secret, t->hash_len, label, dg, t->hash_len, out, t->hash_len);
}

/* ---- AEAD record protection ---- */
static int aead_crypt(stls_tls_t *t, int encrypt, const uint8_t *key, size_t key_len,
                      const uint8_t *iv, uint64_t seq, const uint8_t *aad, size_t aad_len,
                      const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len)
{
    uint8_t nonce[12];
    memcpy(nonce, iv, 12);
    for (int i = 0; i < 8; ++i) nonce[11 - i] ^= (uint8_t)(seq >> (8 * i));
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    const EVP_CIPHER *c = (key_len == 16) ? EVP_aes_128_gcm() : EVP_aes_256_gcm();
    int rc = -1; int len1 = 0, len2 = 0;
    if (EVP_CipherInit_ex(ctx, c, NULL, NULL, NULL, encrypt) <= 0) goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) <= 0) goto done;
    if (EVP_CipherInit_ex(ctx, NULL, NULL, key, nonce, encrypt) <= 0) goto done;
    if (aad_len && EVP_CipherUpdate(ctx, NULL, &len1, aad, (int)aad_len) <= 0) goto done;
    if (encrypt) {
        if (EVP_CipherUpdate(ctx, out, &len1, in, (int)in_len) <= 0) goto done;
        if (EVP_CipherFinal_ex(ctx, out + len1, &len2) <= 0) goto done;
        *out_len = (size_t)(len1 + len2);
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, out + *out_len) <= 0) goto done;
        *out_len += 16;
    } else {
        if (in_len < 16) goto done;
        size_t ct_len = in_len - 16;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void *)(in + ct_len)) <= 0) goto done;
        if (EVP_CipherUpdate(ctx, out, &len1, in, (int)ct_len) <= 0) goto done;
        if (EVP_CipherFinal_ex(ctx, out + len1, &len2) <= 0) goto done;
        *out_len = (size_t)(len1 + len2);
    }
    rc = 0;
done:
    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

/* ---- handshake ---- */
int stls_tls_handshake(stls_tls_t *t, const char *sni, const char *password)
{
    /* X25519 keypair */
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    if (!kctx) return SHADOWTLS_ERR_TLS;
    if (EVP_PKEY_keygen_init(kctx) <= 0 || EVP_PKEY_keygen(kctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(kctx); return SHADOWTLS_ERR_TLS;
    }
    EVP_PKEY_CTX_free(kctx);
    uint8_t pub[32]; size_t pub_len = 32;
    EVP_PKEY_get_raw_public_key(pkey, pub, &pub_len);
    uint8_t priv[32]; size_t priv_len = 32;
    EVP_PKEY_get_raw_private_key(pkey, priv, &priv_len);

    uint8_t random[32]; RAND_bytes(random, 32);
    memcpy(t->client_random, random, 32);
    uint8_t sid[32]; RAND_bytes(sid, 28); memset(sid + 28, 0, 4);

    size_t ch_len = 0, sid_off = 0;
    uint8_t *ch = build_client_hello(sni, random, pub, sid, &ch_len, &sid_off);
    EVP_PKEY_free(pkey);
    if (!ch) return SHADOWTLS_ERR_NOMEM;

    /* sign session_id: HMAC-SHA1(password, CH[0..sid_off] + sid + CH[sid_off+32..]) */
    HMAC_CTX *h = HMAC_CTX_new();
    HMAC_Init_ex(h, password, (int)strlen(password), EVP_sha1(), NULL);
    HMAC_Update(h, ch, sid_off);
    HMAC_Update(h, sid, 32);
    HMAC_Update(h, ch + sid_off + 32, ch_len - sid_off - 32);
    uint8_t full[EVP_MAX_MD_SIZE]; unsigned int flen = 0;
    HMAC_Final(h, full, &flen);
    memcpy(sid + 28, full, 4);
    memcpy(ch + sid_off, sid, 32);
    HMAC_CTX_free(h);

    th_add(t, ch, ch_len);
    if (wr_record(t, CT_HANDSHAKE, ch, ch_len) != SHADOWTLS_ERR_NONE) { free(ch); return SHADOWTLS_ERR_IO; }
    free(ch);
    /* TLS 1.3 middlebox-compatibility dummy CCS (the v2 server requires seeing
     * a ChangeCipherSpec from the client before it checks the data HMAC). */
    {
        uint8_t one = 0x01;
        if (wr_record(t, CT_CCS, &one, 1) != SHADOWTLS_ERR_NONE) return SHADOWTLS_ERR_IO;
    }

    /* ServerHello */
    uint8_t *sh = NULL; size_t sh_len = 0;
    int rc = rd_hs_msg(t, &sh, &sh_len);
    if (rc) return rc;
    if (sh_len < 4 + 2 + 32 + 1 || sh[0] != 0x02) { free(sh); return SHADOWTLS_ERR_TLS; }
    memcpy(t->server_random, sh + 4 + 2, 32);
    /* ShadowTLS residual keying from ServerRandom */
    t->have_sr = true;
    t->hmac_sr = HMAC_CTX_new();
    HMAC_Init_ex(t->hmac_sr, password, (int)strlen(password), EVP_sha1(), NULL);
    HMAC_Update(t->hmac_sr, t->server_random, 32);
    {
        SHA256_CTX kc; SHA256_Init(&kc);
        SHA256_Update(&kc, password, strlen(password));
        SHA256_Update(&kc, t->server_random, 32);
        SHA256_Final(t->xor_key, &kc);
    }
    size_t p = 4 + 2 + 32;
    uint8_t sid_len = sh[p++]; p += sid_len;
    if (p + 3 > sh_len) { free(sh); return SHADOWTLS_ERR_TLS; }
    t->cipher = (sh[p] << 8) | sh[p + 1]; p += 2;
    p += 1; /* compression */
    t->hash384 = (t->cipher == 0x1302);
    t->hash_len = t->hash384 ? 48 : 32;
    /* extensions: find key_share */
    uint8_t srv_share[32]; int have_share = 0;
    if (p + 2 <= sh_len) {
        size_t ext_total = be16(sh + p); p += 2;
        size_t ext_end = p + ext_total;
        if (ext_end > sh_len) ext_end = sh_len;
        while (p + 4 <= ext_end) {
            unsigned et = be16(sh + p); unsigned el = be16(sh + p + 2); p += 4;
            if (et == 0x0033 && el >= 4) {
                unsigned grp = be16(sh + p); unsigned kl = be16(sh + p + 2);
                if (grp == 0x001d && kl == 32 && p + 4 + 32 <= ext_end) {
                    memcpy(srv_share, sh + p + 4, 32); have_share = 1;
                }
            }
            p += el;
        }
    }
    th_add(t, sh, sh_len);
    free(sh);
    if (!have_share) return SHADOWTLS_ERR_TLS;
    if (t->cipher != 0x1301 && t->cipher != 0x1302 && t->cipher != 0x1303) return SHADOWTLS_ERR_TLS;

    /* ECDHE shared secret */
    EVP_PKEY *srv = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, srv_share, 32);
    EVP_PKEY *cli = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, priv, 32);
    uint8_t shared[32]; size_t shared_len = 32;
    EVP_PKEY_CTX *dctx = EVP_PKEY_CTX_new(cli, NULL);
    int ok = srv && cli && dctx && EVP_PKEY_derive_init(dctx) > 0 &&
             EVP_PKEY_derive_set_peer(dctx, srv) > 0 &&
             EVP_PKEY_derive(dctx, shared, &shared_len) > 0 && shared_len == 32;
    if (dctx) EVP_PKEY_CTX_free(dctx);
    if (srv) EVP_PKEY_free(srv);
    if (cli) EVP_PKEY_free(cli);
    if (!ok) return SHADOWTLS_ERR_TLS;

    /* key schedule: early -> handshake */
    uint8_t early[48], zeros[48] = {0}, derived[48], hs[48];
    hkdf_extract(t->hash384, NULL, 0, zeros, t->hash_len, early);
    uint8_t empty_hash[48]; hash_buf(t->hash384, NULL, 0, empty_hash);
    hkdf_expand_label(t->hash384, early, t->hash_len, "derived", empty_hash, t->hash_len, derived, t->hash_len);
    hkdf_extract(t->hash384, derived, t->hash_len, shared, 32, hs);

    uint8_t c_hs[48], s_hs[48];
    derive_secret(t, hs, "c hs traffic", c_hs);
    derive_secret(t, hs, "s hs traffic", s_hs);

    size_t key_len = (t->cipher == 0x1301) ? 16 : 32;
    uint8_t c_key[32], c_iv[12], s_key[32], s_iv[12];
    hkdf_expand_label(t->hash384, c_hs, t->hash_len, "key", NULL, 0, c_key, key_len);
    hkdf_expand_label(t->hash384, c_hs, t->hash_len, "iv", NULL, 0, c_iv, 12);
    hkdf_expand_label(t->hash384, s_hs, t->hash_len, "key", NULL, 0, s_key, key_len);
    hkdf_expand_label(t->hash384, s_hs, t->hash_len, "iv", NULL, 0, s_iv, 12);
    t->c_key_len = t->s_key_len = key_len;
    memcpy(t->c_key, c_key, key_len); memcpy(t->s_key, s_key, key_len);
    memcpy(t->c_iv, c_iv, 12); memcpy(t->s_iv, s_iv, 12);
    t->c_seq = t->s_seq = 0;

    /* read encrypted handshake: EE, Cert, CV, Finished */
    uint8_t s_hs_key[32], s_hs_iv[12];
    memcpy(s_hs_key, s_key, key_len); memcpy(s_hs_iv, s_iv, 12);
    int finished_ok = 0;
    for (int guard = 0; guard < 32 && !finished_ok; ++guard) {
        uint8_t hdr[5];
        rc = rd_exact(t, hdr, 5);
        if (rc) return rc;
        unsigned rl = be16(hdr + 3);
        if (hdr[0] == CT_CCS) { uint8_t cc[8]; if (rl > 8) return SHADOWTLS_ERR_TLS; rc = rd_exact(t, cc, rl); if (rc) return rc; continue; }
        if (hdr[0] == CT_ALERT) return SHADOWTLS_ERR_CLOSED;
        if (hdr[0] != CT_APP) return SHADOWTLS_ERR_TLS;
        uint8_t *ct = (uint8_t *)malloc(rl);
        if (!ct) return SHADOWTLS_ERR_NOMEM;
        rc = rd_exact(t, ct, rl);
        if (rc) { free(ct); return rc; }
        /* ShadowTLS residual filter: server sends [4B HMAC][XORed ciphertext].
         * Verify HMAC (advances hmac_sr), XOR back, strip the 4B HMAC. */
        if (t->have_sr && rl > 4) {
            HMAC_Update(t->hmac_sr, ct + 4, rl - 4);
            HMAC_CTX *clone = HMAC_CTX_new(); HMAC_CTX_copy(clone, t->hmac_sr);
            uint8_t full[EVP_MAX_MD_SIZE]; unsigned int fl = 0;
            HMAC_Final(clone, full, &fl); HMAC_CTX_free(clone);
            if (fl >= 4 && memcmp(full, ct, 4) == 0) {
                for (size_t i = 4; i < rl; ++i) ct[i] ^= t->xor_key[(i - 4) % 32];
                memmove(ct, ct + 4, rl - 4);
                rl -= 4;
                t->authorized = true;
            }
        }
        uint8_t aad[5] = { CT_APP, 0x03, 0x03, 0, 0 };
        put16(aad + 3, rl);
        uint8_t *pt = (uint8_t *)malloc(rl + 1);
        size_t pt_len = 0;
        if (aead_crypt(t, 0, s_hs_key, key_len, s_hs_iv, t->s_seq++, aad, 5, ct, rl, pt, &pt_len) != 0) {
            free(ct); free(pt); return SHADOWTLS_ERR_TLS;
        }
        free(ct);
        /* strip trailing content-type byte(s) */
        while (pt_len > 0 && pt[pt_len - 1] == 0) pt_len--;
        if (pt_len == 0) { free(pt); continue; }
        uint8_t inner_type = pt[pt_len - 1];
        pt_len--;
        if (inner_type == CT_HANDSHAKE) {
            /* may contain multiple handshake messages */
            size_t off = 0;
            while (off + 4 <= pt_len) {
                size_t ml = 4 + (((size_t)pt[off + 1] << 16) | ((size_t)pt[off + 2] << 8) | pt[off + 3]);
                if (off + ml > pt_len) break;
                uint8_t ht = pt[off];
                if (ht == 20) { /* Finished */
                    uint8_t fin_key[48], fin_verify[48];
                    hkdf_expand_label(t->hash384, s_hs, t->hash_len, "finished", NULL, 0, fin_key, t->hash_len);
                    uint8_t th_hash[48]; hash_buf(t->hash384, t->th_buf, t->th_len, th_hash);
                    HMAC_CTX *fh = HMAC_CTX_new();
                    HMAC_Init_ex(fh, fin_key, (int)t->hash_len, t->hash384 ? EVP_sha384() : EVP_sha256(), NULL);
                    HMAC_Update(fh, th_hash, t->hash_len);
                    unsigned int fvl = 0; HMAC_Final(fh, fin_verify, &fvl);
                    HMAC_CTX_free(fh);
                    if (fvl == ml - 4 && memcmp(fin_verify, pt + off + 4, ml - 4) == 0) finished_ok = 1;
                }
                th_add(t, pt + off, ml);
                off += ml;
            }
        }
        free(pt);
    }
    if (!finished_ok) return SHADOWTLS_ERR_TLS;
    /* stop capturing the v2 challenge here (server Finished verified); matches
     * the reference which hashes only the handshake, not post-handshake NST. */
    t->handshake_done = true;

    /* client Finished */
    uint8_t c_fin_key[48], c_fin_verify[48];
    hkdf_expand_label(t->hash384, c_hs, t->hash_len, "finished", NULL, 0, c_fin_key, t->hash_len);
    uint8_t th_hash[48]; hash_buf(t->hash384, t->th_buf, t->th_len, th_hash);
    HMAC_CTX *fh = HMAC_CTX_new();
    HMAC_Init_ex(fh, c_fin_key, (int)t->hash_len, t->hash384 ? EVP_sha384() : EVP_sha256(), NULL);
    HMAC_Update(fh, th_hash, t->hash_len);
    unsigned int fvl = 0; HMAC_Final(fh, c_fin_verify, &fvl);
    HMAC_CTX_free(fh);
    uint8_t fin_msg[4 + 48];
    fin_msg[0] = 20; put24(fin_msg + 1, t->hash_len);
    memcpy(fin_msg + 4, c_fin_verify, t->hash_len);
    th_add(t, fin_msg, 4 + t->hash_len);

    /* encrypt client Finished as AppData */
    size_t inner_len = 4 + t->hash_len + 1;
    uint8_t *inner = (uint8_t *)malloc(inner_len);
    memcpy(inner, fin_msg, 4 + t->hash_len);
    inner[inner_len - 1] = CT_HANDSHAKE;
    uint8_t aad[5] = { CT_APP, 0x03, 0x03, 0, 0 };
    put16(aad + 3, (unsigned)(inner_len + 16));
    uint8_t *ct = (uint8_t *)malloc(inner_len + 16);
    size_t ct_len = 0;
    if (aead_crypt(t, 1, c_key, key_len, c_iv, t->c_seq++, aad, 5, inner, inner_len, ct, &ct_len) != 0) {
        free(inner); free(ct); return SHADOWTLS_ERR_TLS;
    }
    free(inner);
    rc = wr_record(t, CT_APP, ct, ct_len);
    free(ct);
    if (rc) return rc;

    /* application traffic secrets */
    uint8_t derived2[48], ap[48];
    uint8_t th_hash2[48]; hash_buf(t->hash384, t->th_buf, t->th_len, th_hash2);
    hkdf_expand_label(t->hash384, hs, t->hash_len, "derived", empty_hash, t->hash_len, derived2, t->hash_len);
    hkdf_extract(t->hash384, derived2, t->hash_len, zeros, t->hash_len, ap);
    uint8_t c_ap[48], s_ap[48];
    derive_secret(t, ap, "c ap traffic", c_ap);
    derive_secret(t, ap, "s ap traffic", s_ap);
    hkdf_expand_label(t->hash384, c_ap, t->hash_len, "key", NULL, 0, t->c_key, key_len);
    hkdf_expand_label(t->hash384, c_ap, t->hash_len, "iv", NULL, 0, t->c_iv, 12);
    hkdf_expand_label(t->hash384, s_ap, t->hash_len, "key", NULL, 0, t->s_key, key_len);
    hkdf_expand_label(t->hash384, s_ap, t->hash_len, "iv", NULL, 0, t->s_iv, 12);
    t->c_seq = t->s_seq = 0;
    t->handshake_done = true;
    return SHADOWTLS_ERR_NONE;
}