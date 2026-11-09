/**
 * @file reality_crypto_openssl.c
 * @brief OpenSSL (>=1.1.1 / 3.x) implementation of reality_crypto_ops_t.
 *        ML-KEM-768 via vendored PQClean (port/mlkem). No SDK dependencies.
 */

#define OPENSSL_SUPPRESS_DEPRECATED

#include "reality/reality.h"
#include "mlkem/api.h"

#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <string.h>

static int p_rand(void *u, uint8_t *out, size_t len)
{
    (void)u;
    return RAND_bytes(out, (int)len) == 1 ? 0 : -1;
}

static int p_x25519_keypair(void *u, uint8_t pub[32], uint8_t priv[32])
{
    (void)u;
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    if (!ctx) return -1;
    int rc = -1;
    size_t pl = 32, sl = 32;
    if (EVP_PKEY_keygen_init(ctx) <= 0) goto out;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0) goto out;
    if (EVP_PKEY_get_raw_private_key(pkey, priv, &sl) <= 0 || sl != 32) goto out;
    if (EVP_PKEY_get_raw_public_key(pkey, pub, &pl) <= 0 || pl != 32) goto out;
    rc = 0;
out:
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return rc;
}

static int p_x25519_shared(void *u, uint8_t out[32], const uint8_t priv[32], const uint8_t peer[32])
{
    (void)u;
    int rc = -1;
    EVP_PKEY *pk = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, priv, 32);
    EVP_PKEY *pub = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, peer, 32);
    EVP_PKEY_CTX *ctx = pk ? EVP_PKEY_CTX_new(pk, NULL) : NULL;
    size_t olen = 32;
    if (!ctx) goto out;
    if (EVP_PKEY_derive_init(ctx) <= 0) goto out;
    if (EVP_PKEY_derive_set_peer(ctx, pub) <= 0) goto out;
    if (EVP_PKEY_derive(ctx, out, &olen) <= 0 || olen != 32) goto out;
    rc = 0;
out:
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pk);
    EVP_PKEY_free(pub);
    return rc;
}

/* convention (see reality.h): salt==NULL -> expand-only (PRK=ikm);
 * info==NULL && info_len==0 && out_len==hashlen -> extract-only. */
static int p_hkdf(void *u, int sha384,
                  const uint8_t *salt, size_t salt_len,
                  const uint8_t *ikm, size_t ikm_len,
                  const uint8_t *info, size_t info_len,
                  uint8_t *out, size_t out_len)
{
    (void)u;
    const EVP_MD *md = sha384 ? EVP_sha384() : EVP_sha256();
    size_t hlen = sha384 ? 48 : 32;
    int mode;
    if (salt == NULL) mode = EVP_KDF_HKDF_MODE_EXPAND_ONLY;
    else if (info == NULL && info_len == 0 && out_len == hlen) mode = EVP_KDF_HKDF_MODE_EXTRACT_ONLY;
    else mode = EVP_KDF_HKDF_MODE_EXTRACT_AND_EXPAND;

    int rc = -1;
    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    EVP_KDF_CTX *kctx = kdf ? EVP_KDF_CTX_new(kdf) : NULL;
    OSSL_PARAM params[6];
    int n = 0;
    params[n++] = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode);
    params[n++] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,
                    (char *)(sha384 ? "SHA384" : "SHA256"), 0);
    if (salt) params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                    (void *)salt, salt_len);
    params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (void *)ikm, ikm_len);
    if (info && info_len) params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
                    (void *)info, info_len);
    params[n] = OSSL_PARAM_construct_end();
    (void)md;
    if (!kctx) goto out;
    if (EVP_KDF_derive(kctx, out, out_len, params) <= 0) goto out;
    rc = 0;
out:
    EVP_KDF_CTX_free(kctx);
    EVP_KDF_free(kdf);
    return rc;
}

static const EVP_CIPHER *aead_cipher(int algo, size_t key_len)
{
    if (algo == 1) return EVP_chacha20_poly1305();
    return key_len == 16 ? EVP_aes_128_gcm() : EVP_aes_256_gcm();
}

static int p_aead_seal(void *u, int algo, const uint8_t *key, size_t key_len,
                       const uint8_t nonce[12], const uint8_t *aad, size_t aad_len,
                       const uint8_t *src, size_t src_len, uint8_t *dst, size_t *dst_len)
{
    (void)u;
    int rc = -1, len = 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    if (EVP_EncryptInit_ex(ctx, aead_cipher(algo, key_len), NULL, NULL, NULL) != 1) goto out;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) != 1) goto out;
    if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) != 1) goto out;
    if (aad_len && EVP_EncryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1) goto out;
    if (EVP_EncryptUpdate(ctx, dst, &len, src, (int)src_len) != 1) goto out;
    *dst_len = (size_t)len;
    if (EVP_EncryptFinal_ex(ctx, dst + *dst_len, &len) != 1) goto out;
    *dst_len += (size_t)len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, dst + *dst_len) != 1) goto out;
    *dst_len += 16;
    rc = 0;
out:
    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

static int p_aead_open(void *u, int algo, const uint8_t *key, size_t key_len,
                       const uint8_t nonce[12], const uint8_t *aad, size_t aad_len,
                       const uint8_t *src, size_t src_len, uint8_t *dst, size_t *dst_len)
{
    (void)u;
    if (src_len < 16) return -1;
    int rc = -1, len = 0;
    size_t ct_len = src_len - 16;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    if (EVP_DecryptInit_ex(ctx, aead_cipher(algo, key_len), NULL, NULL, NULL) != 1) goto out;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) != 1) goto out;
    if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) != 1) goto out;
    if (aad_len && EVP_DecryptUpdate(ctx, NULL, &len, aad, (int)aad_len) != 1) goto out;
    if (EVP_DecryptUpdate(ctx, dst, &len, src, (int)ct_len) != 1) goto out;
    *dst_len = (size_t)len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, 16, (void *)(src + ct_len)) != 1) goto out;
    if (EVP_DecryptFinal_ex(ctx, dst + *dst_len, &len) != 1) goto out; /* tag mismatch -> fail */
    *dst_len += (size_t)len;
    rc = 0;
out:
    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

static const EVP_MD *hmac_md(int h)
{
    return h == 2 ? EVP_sha512() : (h == 1 ? EVP_sha384() : EVP_sha256());
}

static int p_hmac(void *u, int h, const uint8_t *key, size_t key_len,
                  const uint8_t *msg, size_t msg_len, uint8_t *out)
{
    (void)u;
    unsigned olen = 0;
    return HMAC(hmac_md(h), key, (int)key_len, msg, msg_len, out, &olen) ? 0 : -1;
}

static int p_hash(void *u, int h, const uint8_t *msg, size_t msg_len, uint8_t *out)
{
    (void)u;
    unsigned olen = 0;
    const EVP_MD *md = h == 1 ? EVP_sha384() : EVP_sha256();
    return EVP_Digest(msg, msg_len, out, &olen, md, NULL) == 1 ? 0 : -1;
}

static int p_ed25519_verify(void *u, const uint8_t pub[32],
                            const uint8_t *msg, size_t msg_len, const uint8_t sig[64])
{
    (void)u;
    int rc = -1;
    EVP_PKEY *pk = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, pub, 32);
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!pk || !mdctx) goto out;
    if (EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, pk) != 1) goto out;
    if (EVP_DigestVerify(mdctx, sig, 64, msg, msg_len) != 1) goto out;
    rc = 0;
out:
    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pk);
    return rc;
}

static int p_mlkem_keypair(void *u, uint8_t ek[1184], uint8_t dk[2400])
{
    (void)u;
    return PQCLEAN_MLKEM768_CLEAN_crypto_kem_keypair(ek, dk) == 0 ? 0 : -1;
}

static int p_mlkem_encaps(void *u, const uint8_t ek[1184], uint8_t ct[1088], uint8_t ss[32])
{
    (void)u;
    return PQCLEAN_MLKEM768_CLEAN_crypto_kem_enc(ct, ss, ek) == 0 ? 0 : -1;
}

static int p_mlkem_decaps(void *u, const uint8_t ct[1088], const uint8_t dk[2400], uint8_t ss[32])
{
    (void)u;
    /* PQClean decaps is constant-time and never fails; implicit rejection on bad ct */
    return PQCLEAN_MLKEM768_CLEAN_crypto_kem_dec(ss, ct, dk) == 0 ? 0 : -1;
}

static const reality_crypto_ops_t g_reality_crypto_openssl = {
    .user = NULL,
    .rand_bytes = p_rand,
    .x25519_keypair = p_x25519_keypair,
    .x25519_shared = p_x25519_shared,
    .hkdf = p_hkdf,
    .aead_seal = p_aead_seal,
    .aead_open = p_aead_open,
    .hmac = p_hmac,
    .hash = p_hash,
    .ed25519_verify = p_ed25519_verify,
    .mlkem_keypair = p_mlkem_keypair,
    .mlkem_encaps = p_mlkem_encaps,
    .mlkem_decaps = p_mlkem_decaps,
};

const reality_crypto_ops_t *reality_crypto_openssl(void)
{
    return &g_reality_crypto_openssl;
}
