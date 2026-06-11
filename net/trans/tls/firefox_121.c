/**
 * @file firefox_121.c
 * @brief Firefox 121 TLS fingerprint profile
 *
 * JA3 string: 771,4865-4867-4866-49195-49199-52393-52392-49196-49200-49171-49172-156-157-47-53,0-23-65281-11-10-35-16-5-13-18-51-45-43-27-17513,29-23-24,0
 * JA3 hash: b32309a26951912be7dba376398abc3b
 *
 * Firefox differs from Chrome in:
 * - Cipher suite ordering (Firefox prefers 4865 over 4866)
 * - Extension ordering (SNI last in Firefox)
 * - No GREASE values
 * - Different signature algorithms list
 */

#include "dap_tls_fingerprint.h"

static const uint8_t s_firefox_121_ch[] = {
    /* Handshake type=ClientHello(1), length placeholder */
    0x01, 0x00, 0x00, 0x00,

    /* Legacy version TLS 1.0 */
    0x03, 0x01,

    /* Random (32 bytes) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* Session ID: empty */
    0x00,

    /* Cipher suites (no GREASE, Firefox order) */
    0x00, 0x22,
    0x13, 0x01, 0x13, 0x03, 0x13, 0x02,
    0xc0, 0x2b, 0xc0, 0x2f,
    0xc0, 0x2c, 0xc0, 0x30,
    0xcc, 0xa9, 0xcc, 0xa8,
    0xc0, 0x13, 0xc0, 0x14,
    0x00, 0x9c, 0x00, 0x9d,
    0x00, 0x2f, 0x00, 0x35,

    /* Compression: null */
    0x01, 0x00,

    /* Extensions length (2 bytes) — patched */
    0x00, 0x00,

    /* === Extensions begin === */

    /* Extension: key_share (type=0x0033) — Firefox puts this early */
    0x00, 0x33,
    0x00, 0x02,
    0x00, 0x00,

    /* Extension: supported_versions (type=0x002b) */
    0x00, 0x2b,
    0x00, 0x03,
    0x02, 0x03, 0x04,

    /* Extension: psk_key_exchange_modes (type=0x002d) */
    0x00, 0x2d,
    0x00, 0x02,
    0x01, 0x00,

    /* Extension: signature_algorithms (type=0x000d) */
    0x00, 0x0d,
    0x00, 0x14,
    0x00, 0x12,
    0x04, 0x03, 0x08, 0x04,
    0x04, 0x01, 0x05, 0x03,
    0x08, 0x05, 0x05, 0x01,
    0x08, 0x04, 0x04, 0x02,
    0x02, 0x03,

    /* Extension: supported_groups (type=0x000a) */
    0x00, 0x0a,
    0x00, 0x06,
    0x00, 0x04,
    0x00, 0x1d, 0x00, 0x17,

    /* Extension: ec_point_formats (type=0x000b) */
    0x00, 0x0b,
    0x00, 0x02,
    0x01, 0x00,

    /* Extension: ALPN (type=0x0010) */
    0x00, 0x10,
    0x00, 0x05,
    0x00, 0x03,
    0x02, 0x68, 0x32,

    /* Extension: SNI (type=0x0000) — Firefox puts SNI last */
    0x00, 0x00,
    0x00, 0x00,                 /* data length (patched) */
    0x00, 0x00,                 /* list length */
    0x00,                       /* name type */
    0x00, 0x00,                 /* hostname length (patched) */
    /* hostname bytes appended here */
};

/*
 * Offsets for Firefox:
 *   4(handshake header) + 2(version) + 32(random) + 1(session_id) +
 *   2+34(cipher_suites) + 1+1(compression) + 2(extensions_length) = 79
 *   SNI type(2) + data_len(2) + list_len(2) + name_type(1) = 7
 *   => sni_length_offset = 79 + 3 = 82
 *   => sni_hostname_offset = 79 + 5 = 84
 */
static const dap_tls_fp_profile_t s_profile = {
    .name              = "firefox_121",
    .ja3_string        = "771,4865-4867-4866-49195-49199-52393-52392-49196-49200-49171-49172-156-157-47-53,0-23-65281-11-10-35-16-5-13-18-51-45-43-27-17513,29-23-24,0",
    .ja3_hash          = "b32309a26951912be7dba376398abc3b",
    .clienthello       = s_firefox_121_ch,
    .clienthello_size  = sizeof(s_firefox_121_ch),
    .sni_length_offset = 82,
    .sni_offset        = 84,
};

const dap_tls_fp_profile_t *dap_tls_fp_firefox_121(void)
{
    return &s_profile;
}
