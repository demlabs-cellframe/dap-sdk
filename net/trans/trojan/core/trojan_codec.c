/**
 * @file trojan_codec.c
 * @brief Trojan request-header codec. SDK-free (only libc + socket address
 *        parsing). Unit-testable with golden vectors.
 *
 * Wire format (trojan-gfw client request):
 *   [56 hex chars SHA224(password)] CRLF
 *   [CMD 1 byte = 0x01 CONNECT]
 *   [ATYP 1 byte: 0x01 IPv4 | 0x03 domain | 0x04 IPv6]
 *   [ADDR: 4 bytes | 1+len domain | 16 bytes]
 *   [PORT 2 bytes big-endian]
 *   CRLF
 *   [payload ...]
 */

#include "trojan/trojan.h"

#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#endif

#define TROJAN_CMD_CONNECT 0x01
#define TROJAN_ATYP_IPV4   0x01
#define TROJAN_ATYP_DOMAIN 0x03
#define TROJAN_ATYP_IPV6   0x04

static bool s_is_hex56(const char *a_s)
{
    if (!a_s) return false;
    size_t l_n = strlen(a_s);
    if (l_n != TROJAN_HASH_HEX) return false;
    for (size_t i = 0; i < l_n; ++i) {
        char c = a_s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

int trojan_codec_build_request(const char *a_hash_hex, const char *a_dest_host,
                               uint16_t a_dest_port, uint8_t *a_buf, size_t a_buf_size)
{
    if (!s_is_hex56(a_hash_hex) || !a_dest_host || !a_dest_host[0] || !a_buf)
        return TROJAN_ERR_PARAM;
    if (!a_dest_port)
        return TROJAN_ERR_PARAM;

    size_t l_host_len = strlen(a_dest_host);
    if (l_host_len > 255)
        return TROJAN_ERR_PARAM;

    /* Detect address type. */
    struct in_addr l_v4;
    struct in6_addr l_v6;
    uint8_t l_atyp;
    size_t l_addr_len;
    if (inet_pton(AF_INET, a_dest_host, &l_v4) == 1) {
        l_atyp = TROJAN_ATYP_IPV4;
        l_addr_len = 4;
    } else if (inet_pton(AF_INET6, a_dest_host, &l_v6) == 1) {
        l_atyp = TROJAN_ATYP_IPV6;
        l_addr_len = 16;
    } else {
        l_atyp = TROJAN_ATYP_DOMAIN;
        l_addr_len = 1 + l_host_len;
    }

    /* hash(56) + CRLF(2) + cmd(1) + atyp(1) + addr + port(2) + CRLF(2) */
    size_t l_need = TROJAN_HASH_HEX + 2 + 1 + 1 + l_addr_len + 2 + 2;
    if (a_buf_size < l_need)
        return TROJAN_ERR_PARAM;

    size_t l_off = 0;
    memcpy(a_buf + l_off, a_hash_hex, TROJAN_HASH_HEX); l_off += TROJAN_HASH_HEX;
    a_buf[l_off++] = '\r'; a_buf[l_off++] = '\n';
    a_buf[l_off++] = TROJAN_CMD_CONNECT;
    a_buf[l_off++] = l_atyp;
    if (l_atyp == TROJAN_ATYP_IPV4) {
        memcpy(a_buf + l_off, &l_v4, 4); l_off += 4;
    } else if (l_atyp == TROJAN_ATYP_IPV6) {
        memcpy(a_buf + l_off, &l_v6, 16); l_off += 16;
    } else {
        a_buf[l_off++] = (uint8_t)l_host_len;
        memcpy(a_buf + l_off, a_dest_host, l_host_len); l_off += l_host_len;
    }
    a_buf[l_off++] = (uint8_t)(a_dest_port >> 8);
    a_buf[l_off++] = (uint8_t)(a_dest_port & 0xFF);
    a_buf[l_off++] = '\r'; a_buf[l_off++] = '\n';

    return (int)l_off;
}