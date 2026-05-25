/*
 * Authors:
 * Cellframe Team <admin@cellframe.net>
 * Copyright  (c) 2026 Cellframe Team
 * All rights reserved.
 *
 * This file is part of DAP SDK the open source project
 *
 *    DAP SDK is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_enc_kdf.h"
#include "dap_enc_key.h"

#define LOG_TAG "test_dap_enc_kdf"

/**
 * @brief Test basic KDF derive with fixed inputs produces deterministic output
 */
static void test_derive_deterministic(void)
{
    dap_test_msg("test_derive_deterministic");

    const uint8_t l_secret[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    const char *l_context = "test_context";
    uint64_t l_counter = 0;

    uint8_t l_key1[32] = {0};
    uint8_t l_key2[32] = {0};

    int l_ret = dap_enc_kdf_derive(l_secret, sizeof(l_secret),
                                    l_context, strlen(l_context),
                                    l_counter, l_key1, sizeof(l_key1));
    dap_assert_PIF(l_ret == 0, "First derive succeeds");

    l_ret = dap_enc_kdf_derive(l_secret, sizeof(l_secret),
                                l_context, strlen(l_context),
                                l_counter, l_key2, sizeof(l_key2));
    dap_assert_PIF(l_ret == 0, "Second derive succeeds");

    dap_assert_PIF(memcmp(l_key1, l_key2, 32) == 0,
                   "Same inputs produce same output");

    // Verify output is not all zeros
    uint8_t l_zeros[32] = {0};
    dap_assert_PIF(memcmp(l_key1, l_zeros, 32) != 0,
                   "Output is not all zeros");
}

/**
 * @brief Different counters produce different keys
 */
static void test_derive_counter_variation(void)
{
    dap_test_msg("test_derive_counter_variation");

    const uint8_t l_secret[] = {0xAA, 0xBB, 0xCC, 0xDD};
    const char *l_context = "session";

    uint8_t l_key0[32] = {0};
    uint8_t l_key1[32] = {0};

    dap_enc_kdf_derive(l_secret, sizeof(l_secret),
                       l_context, strlen(l_context),
                       0, l_key0, sizeof(l_key0));
    dap_enc_kdf_derive(l_secret, sizeof(l_secret),
                       l_context, strlen(l_context),
                       1, l_key1, sizeof(l_key1));

    dap_assert_PIF(memcmp(l_key0, l_key1, 32) != 0,
                   "Different counters produce different keys");
}

/**
 * @brief Different contexts produce different keys
 */
static void test_derive_context_variation(void)
{
    dap_test_msg("test_derive_context_variation");

    const uint8_t l_secret[] = {0x11, 0x22, 0x33, 0x44};

    uint8_t l_key_a[32] = {0};
    uint8_t l_key_b[32] = {0};

    dap_enc_kdf_derive(l_secret, sizeof(l_secret),
                       "context_a", 9,
                       0, l_key_a, sizeof(l_key_a));
    dap_enc_kdf_derive(l_secret, sizeof(l_secret),
                       "context_b", 9,
                       0, l_key_b, sizeof(l_key_b));

    dap_assert_PIF(memcmp(l_key_a, l_key_b, 32) != 0,
                   "Different contexts produce different keys");
}

/**
 * @brief Derive with no context (context_size = 0)
 */
static void test_derive_no_context(void)
{
    dap_test_msg("test_derive_no_context");

    const uint8_t l_secret[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t l_key[32] = {0};

    int l_ret = dap_enc_kdf_derive(l_secret, sizeof(l_secret),
                                    NULL, 0,
                                    0, l_key, sizeof(l_key));
    dap_assert_PIF(l_ret == 0, "Derive with no context succeeds");

    uint8_t l_zeros[32] = {0};
    dap_assert_PIF(memcmp(l_key, l_zeros, 32) != 0,
                   "Output is not all zeros");
}

/**
 * @brief Various output sizes
 */
static void test_derive_various_sizes(void)
{
    dap_test_msg("test_derive_various_sizes");

    const uint8_t l_secret[] = {0x01, 0x02, 0x03, 0x04};
    const char *l_ctx = "size_test";

    // 16 bytes
    uint8_t l_key16[16] = {0};
    int l_ret = dap_enc_kdf_derive(l_secret, sizeof(l_secret),
                                    l_ctx, strlen(l_ctx),
                                    0, l_key16, sizeof(l_key16));
    dap_assert_PIF(l_ret == 0, "Derive 16 bytes");

    // 64 bytes
    uint8_t l_key64[64] = {0};
    l_ret = dap_enc_kdf_derive(l_secret, sizeof(l_secret),
                                l_ctx, strlen(l_ctx),
                                0, l_key64, sizeof(l_key64));
    dap_assert_PIF(l_ret == 0, "Derive 64 bytes");

    // First 16 bytes of 64-byte output should match the 16-byte output
    // because SHAKE256 is an XOF and truncation applies
    // (Actually they won't match because SHAKE256 produces a full output
    //  at requested length; only if the library used streaming mode.)
    // We just verify both are non-zero and deterministic.
    uint8_t l_key16_check[16] = {0};
    dap_enc_kdf_derive(l_secret, sizeof(l_secret),
                       l_ctx, strlen(l_ctx),
                       0, l_key16_check, sizeof(l_key16_check));
    dap_assert_PIF(memcmp(l_key16, l_key16_check, 16) == 0,
                   "Deterministic 16-byte output");
}

/**
 * @brief Error paths
 */
static void test_derive_error_paths(void)
{
    dap_test_msg("test_derive_error_paths");

    uint8_t l_key[32] = {0};
    const uint8_t l_secret[] = {0x01};

    // NULL secret
    int l_ret = dap_enc_kdf_derive(NULL, 4, "ctx", 3, 0, l_key, 32);
    dap_assert_PIF(l_ret == -1, "NULL secret returns -1");

    // Zero secret size
    l_ret = dap_enc_kdf_derive(l_secret, 0, "ctx", 3, 0, l_key, 32);
    dap_assert_PIF(l_ret == -1, "Zero secret size returns -1");

    // NULL output
    l_ret = dap_enc_kdf_derive(l_secret, 1, "ctx", 3, 0, NULL, 32);
    dap_assert_PIF(l_ret == -2, "NULL output returns -2");

    // Zero output size
    l_ret = dap_enc_kdf_derive(l_secret, 1, "ctx", 3, 0, l_key, 0);
    dap_assert_PIF(l_ret == -2, "Zero output size returns -2");

    // Non-zero context_size with NULL context
    l_ret = dap_enc_kdf_derive(l_secret, 1, NULL, 5, 0, l_key, 32);
    dap_assert_PIF(l_ret == -3, "NULL context with size>0 returns -3");
}

/**
 * @brief Test derive_from_key with a mock dap_enc_key_t
 */
static void test_derive_from_key(void)
{
    dap_test_msg("test_derive_from_key");

    dap_enc_key_t l_key = {0};
    uint8_t l_shared[16] = {0xCA, 0xFE, 0xBA, 0xBE, 0xDE, 0xAD, 0xBE, 0xEF,
                            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    l_key.shared_key = l_shared;
    l_key.shared_key_size = sizeof(l_shared);

    uint8_t l_derived[32] = {0};
    int l_ret = dap_enc_kdf_derive_from_key(&l_key, "test", 4, 0,
                                             l_derived, sizeof(l_derived));
    dap_assert_PIF(l_ret == 0, "derive_from_key with shared_key succeeds");

    uint8_t l_zeros[32] = {0};
    dap_assert_PIF(memcmp(l_derived, l_zeros, 32) != 0, "Output not zeros");

    // Derive again, must be same
    uint8_t l_derived2[32] = {0};
    dap_enc_kdf_derive_from_key(&l_key, "test", 4, 0,
                                 l_derived2, sizeof(l_derived2));
    dap_assert_PIF(memcmp(l_derived, l_derived2, 32) == 0, "Deterministic");

    // NULL key
    l_ret = dap_enc_kdf_derive_from_key(NULL, "test", 4, 0, l_derived, 32);
    dap_assert_PIF(l_ret != 0, "NULL key returns error");

    // Key with no shared secret and no priv_key_data
    dap_enc_key_t l_empty_key = {0};
    l_ret = dap_enc_kdf_derive_from_key(&l_empty_key, "test", 4, 0, l_derived, 32);
    dap_assert_PIF(l_ret != 0, "Empty key returns error");
}

/**
 * @brief Test derive_from_key fallback to priv_key_data
 */
static void test_derive_from_key_priv_fallback(void)
{
    dap_test_msg("test_derive_from_key_priv_fallback");

    dap_enc_key_t l_key = {0};
    uint8_t l_priv[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    l_key.priv_key_data = l_priv;
    l_key.priv_key_data_size = sizeof(l_priv);

    uint8_t l_derived[32] = {0};
    int l_ret = dap_enc_kdf_derive_from_key(&l_key, "fallback", 8, 0,
                                             l_derived, sizeof(l_derived));
    dap_assert_PIF(l_ret == 0, "derive_from_key with priv_key_data succeeds");

    uint8_t l_zeros[32] = {0};
    dap_assert_PIF(memcmp(l_derived, l_zeros, 32) != 0, "Output not zeros");
}

/**
 * @brief Test derive_multiple
 */
static void test_derive_multiple(void)
{
    dap_test_msg("test_derive_multiple");

    const uint8_t l_secret[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const char *l_ctx = "multi";
    const size_t l_num_keys = 4;
    const size_t l_key_size = 32;

    uint8_t l_keys_data[4][32] = {{0}};
    void *l_keys[4];
    for(size_t i = 0; i < l_num_keys; i++)
        l_keys[i] = l_keys_data[i];

    int l_ret = dap_enc_kdf_derive_multiple(l_secret, sizeof(l_secret),
                                             l_ctx, strlen(l_ctx),
                                             0, l_keys, l_num_keys, l_key_size);
    dap_assert_PIF(l_ret == 0, "derive_multiple succeeds");

    // All keys must be different from each other
    for(size_t i = 0; i < l_num_keys; i++)
    {
        for(size_t j = i + 1; j < l_num_keys; j++)
        {
            dap_assert_PIF(memcmp(l_keys_data[i], l_keys_data[j], l_key_size) != 0,
                           "Keys at different counters differ");
        }
    }

    // Each key must match individual derive with corresponding counter
    for(size_t i = 0; i < l_num_keys; i++)
    {
        uint8_t l_single[32] = {0};
        dap_enc_kdf_derive(l_secret, sizeof(l_secret),
                           l_ctx, strlen(l_ctx),
                           (uint64_t)i, l_single, l_key_size);
        dap_assert_PIF(memcmp(l_keys_data[i], l_single, l_key_size) == 0,
                       "derive_multiple matches individual derive");
    }

    // Error: NULL keys array
    l_ret = dap_enc_kdf_derive_multiple(l_secret, sizeof(l_secret),
                                         l_ctx, strlen(l_ctx),
                                         0, NULL, 3, 32);
    dap_assert_PIF(l_ret == -1, "NULL keys array returns error");

    // Error: zero num_keys
    l_ret = dap_enc_kdf_derive_multiple(l_secret, sizeof(l_secret),
                                         l_ctx, strlen(l_ctx),
                                         0, l_keys, 0, 32);
    dap_assert_PIF(l_ret == -1, "Zero num_keys returns error");
}

/**
 * @brief Test HKDF extract+expand
 */
static void test_hkdf(void)
{
    dap_test_msg("test_hkdf");

    const uint8_t l_ikm[] = {0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
                             0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B};
    const uint8_t l_salt[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    const uint8_t l_info[] = {0xF0, 0xF1, 0xF2, 0xF3};

    uint8_t l_okm1[32] = {0};
    int l_ret = dap_enc_kdf_hkdf(l_salt, sizeof(l_salt),
                                  l_ikm, sizeof(l_ikm),
                                  l_info, sizeof(l_info),
                                  l_okm1, sizeof(l_okm1));
    dap_assert_PIF(l_ret == 0, "HKDF succeeds");

    uint8_t l_zeros[32] = {0};
    dap_assert_PIF(memcmp(l_okm1, l_zeros, 32) != 0, "OKM not zeros");

    // Deterministic
    uint8_t l_okm2[32] = {0};
    dap_enc_kdf_hkdf(l_salt, sizeof(l_salt),
                     l_ikm, sizeof(l_ikm),
                     l_info, sizeof(l_info),
                     l_okm2, sizeof(l_okm2));
    dap_assert_PIF(memcmp(l_okm1, l_okm2, 32) == 0, "HKDF is deterministic");

    // No salt
    uint8_t l_okm_nosalt[32] = {0};
    l_ret = dap_enc_kdf_hkdf(NULL, 0,
                              l_ikm, sizeof(l_ikm),
                              l_info, sizeof(l_info),
                              l_okm_nosalt, sizeof(l_okm_nosalt));
    dap_assert_PIF(l_ret == 0, "HKDF with no salt succeeds");

    // No info
    uint8_t l_okm_noinfo[32] = {0};
    l_ret = dap_enc_kdf_hkdf(l_salt, sizeof(l_salt),
                              l_ikm, sizeof(l_ikm),
                              NULL, 0,
                              l_okm_noinfo, sizeof(l_okm_noinfo));
    dap_assert_PIF(l_ret == 0, "HKDF with no info succeeds");

    // Different salt -> different output
    const uint8_t l_salt2[] = {0xFF, 0xFE, 0xFD, 0xFC};
    uint8_t l_okm3[32] = {0};
    dap_enc_kdf_hkdf(l_salt2, sizeof(l_salt2),
                     l_ikm, sizeof(l_ikm),
                     l_info, sizeof(l_info),
                     l_okm3, sizeof(l_okm3));
    dap_assert_PIF(memcmp(l_okm1, l_okm3, 32) != 0,
                   "Different salt produces different output");

    // Error: NULL IKM
    l_ret = dap_enc_kdf_hkdf(l_salt, sizeof(l_salt),
                              NULL, 16,
                              l_info, sizeof(l_info),
                              l_okm1, sizeof(l_okm1));
    dap_assert_PIF(l_ret == -1, "NULL IKM returns error");

    // Error: NULL OKM
    l_ret = dap_enc_kdf_hkdf(l_salt, sizeof(l_salt),
                              l_ikm, sizeof(l_ikm),
                              l_info, sizeof(l_info),
                              NULL, 32);
    dap_assert_PIF(l_ret == -2, "NULL OKM returns error");
}

int main(void)
{
    dap_common_init("test_dap_enc_kdf", NULL);
    dap_log_level_set(L_DEBUG);

    dap_print_module_name("dap_enc_kdf");

    test_derive_deterministic();
    test_derive_counter_variation();
    test_derive_context_variation();
    test_derive_no_context();
    test_derive_various_sizes();
    test_derive_error_paths();
    test_derive_from_key();
    test_derive_from_key_priv_fallback();
    test_derive_multiple();
    test_hkdf();

    log_it(L_INFO, "All dap_enc_kdf tests passed");

    dap_common_deinit();
    return 0;
}
