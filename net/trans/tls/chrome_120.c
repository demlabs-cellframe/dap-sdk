/**
 * @file chrome_120.c
 * @brief Chrome 120 TLS fingerprint profile
 *
 * JA3 string: 771,4866-4865-4867-49195-49199-49196-49200-52393-52392-49171-49172-156-157-47-53,0-23-65281-10-11-35-16-5-13-18-51-45-43-27-17513,29-23-24,0
 * JA3 hash: cd08e31494f9531f560d64c695473da9
 *
 * Wire format (after TLS record header):
 *   [2] legacy_version (0x0301)
 *   [32] random
 *   [1] session_id_length + [0] session_id
 *   [2] cipher_suites_length + [N] cipher_suites
 *   [1] compression_methods_length + [1] null
 *   [2] extensions_length + [N] extensions
 *
 * SNI extension structure (type 0x0000):
 *   [2] type=0x0000
 *   [2] data_length
 *   [2] server_name_list_length
 *   [1] server_name_type=0x00
 *   [2] hostname_length
 *   [N] hostname
 */

#include "dap_tls_fingerprint.h"

/*
 * Chrome 120 ClientHello template.
 * SNI hostname placeholder: 0 bytes (will be appended at build time).
 * SNI extension is at the beginning of the extensions block.
 *
 * Offsets computed from start of handshake body (after 4-byte handshake header):
 *   legacy_version(2) + random(32) + session_id(1) +
 *   cipher_suites(2+34) + compression(1+1) + extensions_length(2) = 75
 *   SNI extension type(2) + data_length(2) + list_length(2) + name_type(1) = 7
 *   => sni_length_offset = 75 + 3 = 78
 *   => sni_hostname_offset = 75 + 5 = 80
 */

static const uint8_t s_chrome_120_ch[] = {
    /* Handshake type=ClientHello(1), length=0x0000 (placeholder) */
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

    /* Cipher suites (GREASE 0x0a0a + 16 suites) */
    0x00, 0x24,
    0x0a, 0x0a,
    0x13, 0x01, 0x13, 0x02, 0x13, 0x03,
    0xc0, 0x2b, 0xc0, 0x2f,
    0xc0, 0x2c, 0xc0, 0x30,
    0xcc, 0xa9, 0xcc, 0xa8,
    0xc0, 0x13, 0xc0, 0x14,
    0x00, 0x9c, 0x00, 0x9d,
    0x00, 0x2f, 0x00, 0x35,

    /* Compression: null */
    0x01, 0x00,

    /* Extensions length (2 bytes) — will be patched */
    0x00, 0x00,

    /* === Extensions begin at offset 75 === */

    /* Extension: SNI (type=0x0000) */
    0x00, 0x00,                 /* type */
    0x00, 0x00,                 /* data length (patched) */
    0x00, 0x00,                 /* server_name list length */
    0x00,                       /* name type=host_name */
    0x00, 0x00,                 /* hostname length (patched at offset 78) */
    /* hostname bytes appended here at build time */

    /* Extension: supported_groups (type=0x000a) */
    0x00, 0x0a,
    0x00, 0x06,
    0x00, 0x04,
    0x00, 0x1d, 0x00, 0x17,

    /* Extension: ec_point_formats (type=0x000b) */
    0x00, 0x0b,
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

    /* Extension: ALPN (type=0x0010) */
    0x00, 0x10,
    0x00, 0x05,
    0x00, 0x03,
    0x02, 0x68, 0x32,

    /* Extension: supported_versions (type=0x002b) */
    0x00, 0x2b,
    0x00, 0x03,
    0x02, 0x03, 0x04,

    /* Extension: psk_key_exchange_modes (type=0x002d) */
    0x00, 0x2d,
    0x00, 0x02,
    0x01, 0x00,

    /* Extension: key_share (type=0x0033) */
    0x00, 0x33,
    0x00, 0x02,
    0x00, 0x00,
};

static const dap_tls_fp_profile_t s_profile = {
    .name              = "chrome_120",
    .ja3_string        = "771,4866-4865-4867-49195-49199-49196-49200-52393-52392-49171-49172-156-157-47-53,0-23-65281-10-11-35-16-5-13-18-51-45-43-27-17513,29-23-24,0",
    .ja3_hash          = "cd08e31494f9531f560d64c695473da9",
    .clienthello       = s_chrome_120_ch,
    .clienthello_size  = sizeof(s_chrome_120_ch),
    /* SNI: offset 75 + type(2) + data_len(2) + list_len(2) + name_type(1) = 82 */
    .sni_length_offset = 78,   /* offset of 2-byte hostname length */
    .sni_offset        = 80,   /* offset of hostname bytes */
};

const dap_tls_fp_profile_t *dap_tls_fp_chrome_120(void)
{
    return &s_profile;
}
