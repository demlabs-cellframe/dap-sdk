/*
 * Harvest harness — produces deterministic Dilithium / SHAKE outputs for a
 * fixed seed.  Compiled against either the pre-fix or post-fix Keccak
 * primitives so the byte-level effect of the FIPS 202 change can be
 * captured and pinned in regression tests.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"
#include "dilithium_params.h"

static void hexdump(const char *label, const uint8_t *buf, size_t n)
{
    printf("%s len=%zu\n  ", label, n);
    size_t to_print = n < 64 ? n : 64;
    for (size_t i = 0; i < to_print; i++) printf("%02x", buf[i]);
    printf("%s\n", n > 64 ? "..." : "");
}

int main(void)
{
    /* SHAKE128("", 64) — both FIPS-202-correct and legacy variants */
    uint8_t out128_can[64], out128_leg[64];
    dap_hash_shake128(out128_can, sizeof(out128_can), (const uint8_t *)"", 0);
    dap_hash_shake128_legacy(out128_leg, sizeof(out128_leg), (const uint8_t *)"", 0);
    hexdump("SHAKE128(empty,64) canonical", out128_can, sizeof(out128_can));
    hexdump("SHAKE128(empty,64) legacy   ", out128_leg, sizeof(out128_leg));

    uint8_t out256_can[64], out256_leg[64];
    dap_hash_shake256(out256_can, sizeof(out256_can), (const uint8_t *)"", 0);
    dap_hash_shake256_legacy(out256_leg, sizeof(out256_leg), (const uint8_t *)"", 0);
    hexdump("SHAKE256(empty,64) canonical", out256_can, sizeof(out256_can));
    hexdump("SHAKE256(empty,64) legacy   ", out256_leg, sizeof(out256_leg));

    /* Deterministic Dilithium keypair from a fixed seed (DILITHIUM-2 / MODE_2). */
    static const uint8_t k_seed[32] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
        0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
        0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
        0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20
    };
    dilithium_public_key_t  pk;
    dilithium_private_key_t sk;
    memset(&pk, 0, sizeof(pk));
    memset(&sk, 0, sizeof(sk));

    int rc = dilithium_crypto_sign_keypair(&pk, &sk,
                                           MODE_2,
                                           k_seed, sizeof(k_seed));
    if (rc != 0) { fprintf(stderr, "keypair rc=%d\n", rc); return 1; }
    hexdump("Dilithium-2 pk[0..64]", pk.data, 64);
    hexdump("Dilithium-2 sk[0..64]", sk.data, 64);

    /* Skip signing — dilithium_crypto_sign mixes in dap_random_bytes for
     * the masking nonce, so signature output is non-deterministic.  The
     * seed-determined keypair (pk/sk) is sufficient to detect any change
     * in the SHAKE expansion path. */

    return 0;
}
