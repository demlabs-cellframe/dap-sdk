/*
 * Internal ECDSA algorithm implementation
 */

#include "ecdsa_impl.h"
#include "dap_hash_sha2.h"
#include <string.h>

// =============================================================================
// RFC6979 Deterministic Nonce
// =============================================================================

// Note: RFC6979 uses HMAC-SHA256 (not SHA3!) for nonce generation

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
    dap_hash_hmac_sha2_256(key, key, 32, buf, buflen);
    
    // Step e: V = HMAC(K, V)
    dap_hash_hmac_sha2_256(v, key, 32, v, 32);
    
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
    dap_hash_hmac_sha2_256(key, key, 32, buf, buflen);
    
    // Step g: V = HMAC(K, V)
    dap_hash_hmac_sha2_256(v, key, 32, v, 32);
    
    // Step h: Generate candidates
    for (unsigned int i = 0; i <= counter; i++) {
        // V = HMAC(K, V)
        dap_hash_hmac_sha2_256(v, key, 32, v, 32);
        
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
            dap_hash_hmac_sha2_256(key, key, 32, buf, buflen);
            dap_hash_hmac_sha2_256(v, key, 32, v, 32);
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
    
    // Optimization: verify r*Z² == X without converting to affine
    // This avoids one expensive field inversion (~260 squarings)
    // If R = (X, Y, Z) in Jacobian, then x_affine = X/Z²
    // We check: sig->r * Z² == X (mod p)
    
    // Get r as field element  
    uint8_t r_bytes[32];
    ecdsa_scalar_get_b32(r_bytes, &sig->r);
    ecdsa_field_t r_field;
    ecdsa_field_set_b32(&r_field, r_bytes);
    
    // Compute Z² and r * Z²
    ecdsa_field_t z2, rz2;
    ecdsa_field_sqr(&z2, &r_point.z);
    ecdsa_field_mul(&rz2, &r_field, &z2);
    
    // Normalize for comparison
    ecdsa_field_normalize(&rz2);
    ecdsa_field_t x_norm;
    ecdsa_field_copy(&x_norm, &r_point.x);
    ecdsa_field_normalize(&x_norm);
    
    return ecdsa_field_equal(&rz2, &x_norm);
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
