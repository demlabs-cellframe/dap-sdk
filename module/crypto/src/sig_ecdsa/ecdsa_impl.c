/*
 * Internal ECDSA algorithm implementation
 */

#include "ecdsa_impl.h"
#include "dap_hash_sha3.h"
#include <string.h>

// =============================================================================
// RFC6979 Deterministic Nonce
// =============================================================================

// HMAC-SHA256 for RFC6979
static void hmac_sha256(uint8_t *out, const uint8_t *key, size_t keylen,
                        const uint8_t *msg, size_t msglen) {
    uint8_t k_ipad[64], k_opad[64];
    uint8_t tk[32];
    
    // If key > 64 bytes, hash it
    if (keylen > 64) {
        dap_hash_sha3_256_raw(tk, key, keylen);
        key = tk;
        keylen = 32;
    }
    
    // Pad key
    memset(k_ipad, 0x36, 64);
    memset(k_opad, 0x5c, 64);
    for (size_t i = 0; i < keylen; i++) {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }
    
    // Inner hash: H(k_ipad || msg)
    uint8_t inner[64 + 256];  // Max msg size we support
    memcpy(inner, k_ipad, 64);
    size_t total = 64;
    if (msglen <= 256) {
        memcpy(inner + 64, msg, msglen);
        total += msglen;
    }
    uint8_t inner_hash[32];
    dap_hash_sha3_256_raw(inner_hash, inner, total);
    
    // Outer hash: H(k_opad || inner_hash)
    uint8_t outer[64 + 32];
    memcpy(outer, k_opad, 64);
    memcpy(outer + 64, inner_hash, 32);
    dap_hash_sha3_256_raw(out, outer, 96);
}

bool ecdsa_nonce_rfc6979(
    ecdsa_scalar_t *k,
    const uint8_t *msg32,
    const uint8_t *seckey32,
    const uint8_t *algo16,
    const void *data,
    size_t datalen,
    unsigned int counter)
{
    // RFC6979 section 3.2
    uint8_t v[32], key[32];
    uint8_t buf[32 + 1 + 32 + 32 + 16 + 256];  // V || 0x00/0x01 || x || h || algo || data
    size_t buflen;
    
    // Step b: V = 0x01 * 32
    memset(v, 0x01, 32);
    
    // Step c: K = 0x00 * 32
    memset(key, 0x00, 32);
    
    // Step d: K = HMAC(K, V || 0x00 || x || h [|| algo || data])
    buflen = 0;
    memcpy(buf + buflen, v, 32); buflen += 32;
    buf[buflen++] = 0x00;
    memcpy(buf + buflen, seckey32, 32); buflen += 32;
    memcpy(buf + buflen, msg32, 32); buflen += 32;
    if (algo16) {
        memcpy(buf + buflen, algo16, 16); buflen += 16;
    }
    if (data && datalen > 0 && datalen <= 256) {
        memcpy(buf + buflen, data, datalen); buflen += datalen;
    }
    hmac_sha256(key, key, 32, buf, buflen);
    
    // Step e: V = HMAC(K, V)
    hmac_sha256(v, key, 32, v, 32);
    
    // Step f: K = HMAC(K, V || 0x01 || x || h [|| algo || data])
    buflen = 0;
    memcpy(buf + buflen, v, 32); buflen += 32;
    buf[buflen++] = 0x01;
    memcpy(buf + buflen, seckey32, 32); buflen += 32;
    memcpy(buf + buflen, msg32, 32); buflen += 32;
    if (algo16) {
        memcpy(buf + buflen, algo16, 16); buflen += 16;
    }
    if (data && datalen > 0 && datalen <= 256) {
        memcpy(buf + buflen, data, datalen); buflen += datalen;
    }
    hmac_sha256(key, key, 32, buf, buflen);
    
    // Step g: V = HMAC(K, V)
    hmac_sha256(v, key, 32, v, 32);
    
    // Step h: Generate candidates
    for (unsigned int i = 0; i <= counter; i++) {
        // V = HMAC(K, V)
        hmac_sha256(v, key, 32, v, 32);
        
        // Try to use V as nonce
        int overflow = 0;
        ecdsa_scalar_set_b32(k, v, &overflow);
        
        if (!overflow && !ecdsa_scalar_is_zero(k)) {
            if (i == counter) {
                return true;
            }
        }
        
        // If failed, update K and V
        if (i < counter) {
            buflen = 0;
            memcpy(buf + buflen, v, 32); buflen += 32;
            buf[buflen++] = 0x00;
            hmac_sha256(key, key, 32, buf, buflen);
            hmac_sha256(v, key, 32, v, 32);
        }
    }
    
    return false;
}

// =============================================================================
// ECDSA Sign
// =============================================================================

bool ecdsa_sign_inner(
    ecdsa_sig_t *sig,
    const uint8_t *msg32,
    const ecdsa_scalar_t *seckey,
    const ecdsa_scalar_t *nonce)
{
    // r = (k*G).x mod n
    // s = k^(-1) * (m + r*d) mod n
    
    ecdsa_gej_t rp;
    ecdsa_ge_t rp_affine;
    
    // R = k*G
    ecdsa_ecmult_gen(&rp, nonce);
    ecdsa_ge_set_gej(&rp_affine, &rp);
    
    if (rp_affine.infinity) {
        return false;
    }
    
    // r = R.x mod n
    uint8_t rx[32];
    ecdsa_field_get_b32(rx, &rp_affine.x);
    
    int overflow = 0;
    ecdsa_scalar_set_b32(&sig->r, rx, &overflow);
    
    if (ecdsa_scalar_is_zero(&sig->r)) {
        return false;
    }
    
    // s = k^(-1) * (m + r*d) mod n
    ecdsa_scalar_t m, rd, sum, k_inv;
    
    // m = message as scalar
    ecdsa_scalar_set_b32(&m, msg32, &overflow);
    
    // rd = r * d
    ecdsa_scalar_mul(&rd, &sig->r, seckey);
    
    // sum = m + rd
    ecdsa_scalar_add(&sum, &m, &rd);
    
    // k_inv = k^(-1)
    ecdsa_scalar_inv(&k_inv, nonce);
    
    // s = k_inv * sum
    ecdsa_scalar_mul(&sig->s, &k_inv, &sum);
    
    if (ecdsa_scalar_is_zero(&sig->s)) {
        return false;
    }
    
    // Normalize to low-S
    ecdsa_sig_normalize(sig);
    
    return true;
}

// =============================================================================
// ECDSA Verify
// =============================================================================

bool ecdsa_verify_inner(
    const ecdsa_sig_t *sig,
    const uint8_t *msg32,
    const ecdsa_ge_t *pubkey)
{
    // Verify: r == (m*s^(-1)*G + r*s^(-1)*P).x mod n
    
    if (ecdsa_scalar_is_zero(&sig->r) || ecdsa_scalar_is_zero(&sig->s)) {
        return false;
    }
    
    // Check r, s < n (already ensured by scalar parsing)
    
    // s_inv = s^(-1)
    ecdsa_scalar_t s_inv;
    ecdsa_scalar_inv(&s_inv, &sig->s);
    
    // u1 = m * s_inv
    ecdsa_scalar_t m, u1, u2;
    int overflow;
    ecdsa_scalar_set_b32(&m, msg32, &overflow);
    ecdsa_scalar_mul(&u1, &m, &s_inv);
    
    // u2 = r * s_inv
    ecdsa_scalar_mul(&u2, &sig->r, &s_inv);
    
    // R = u1*G + u2*P
    ecdsa_gej_t pubkey_j, r_point;
    ecdsa_gej_set_ge(&pubkey_j, pubkey);
    ecdsa_ecmult(&r_point, &pubkey_j, &u2, &u1);
    
    if (ecdsa_gej_is_infinity(&r_point)) {
        return false;
    }
    
    // Convert to affine and check x-coordinate
    ecdsa_ge_t r_affine;
    ecdsa_ge_set_gej(&r_affine, &r_point);
    
    uint8_t rx[32];
    ecdsa_field_get_b32(rx, &r_affine.x);
    
    ecdsa_scalar_t rx_scalar;
    ecdsa_scalar_set_b32(&rx_scalar, rx, &overflow);
    
    return ecdsa_scalar_equal(&rx_scalar, &sig->r);
}

// =============================================================================
// Signature Serialization
// =============================================================================

void ecdsa_sig_serialize(uint8_t *output64, const ecdsa_sig_t *sig) {
    ecdsa_scalar_get_b32(output64, &sig->r);
    ecdsa_scalar_get_b32(output64 + 32, &sig->s);
}

bool ecdsa_sig_parse(ecdsa_sig_t *sig, const uint8_t *input64) {
    int overflow = 0;
    
    ecdsa_scalar_set_b32(&sig->r, input64, &overflow);
    if (overflow || ecdsa_scalar_is_zero(&sig->r)) {
        return false;
    }
    
    ecdsa_scalar_set_b32(&sig->s, input64 + 32, &overflow);
    if (overflow || ecdsa_scalar_is_zero(&sig->s)) {
        return false;
    }
    
    return true;
}

bool ecdsa_sig_normalize(ecdsa_sig_t *sig) {
    if (ecdsa_scalar_is_high(&sig->s)) {
        ecdsa_scalar_negate(&sig->s, &sig->s);
        return true;
    }
    return false;
}

// =============================================================================
// Public Key
// =============================================================================

bool ecdsa_pubkey_create(ecdsa_ge_t *pubkey, const ecdsa_scalar_t *seckey) {
    if (ecdsa_scalar_is_zero(seckey)) {
        return false;
    }
    
    ecdsa_gej_t pubkey_j;
    ecdsa_ecmult_gen(&pubkey_j, seckey);
    ecdsa_ge_set_gej(pubkey, &pubkey_j);
    
    return !pubkey->infinity;
}
