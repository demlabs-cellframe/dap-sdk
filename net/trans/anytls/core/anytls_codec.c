/**
 * @file anytls_codec.c
 * @brief AnyTLS frame/auth/SocksAddr codec + default padding scheme. SDK-free.
 */

#include "anytls/anytls.h"

#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#endif

/* Default padding scheme (protocol constant from the AnyTLS spec). Not a
 * secret — the password is the only provisioned credential. */
const char *ANYTLS_DEFAULT_PADDING_SCHEME =
    "stop=8\n"
    "0=30-30\n"
    "1=100-400\n"
    "2=400-500,c,500-1000,c,500-1000,c,500-1000,c,500-1000\n"
    "3=9-9,500-1000\n"
    "4=500-1000\n"
    "5=500-1000\n"
    "6=500-1000\n"
    "7=500-1000\n";

int anytls_codec_build_auth(const uint8_t a_hash[32], uint16_t a_pad0_len,
                            uint8_t *a_buf, size_t a_buf_size)
{
    if (!a_hash || !a_buf) return ANYTLS_ERR_PARAM;
    size_t l_need = 32 + 2 + (size_t)a_pad0_len;
    if (a_buf_size < l_need) return ANYTLS_ERR_PARAM;
    memcpy(a_buf, a_hash, 32);
    a_buf[32] = (uint8_t)(a_pad0_len >> 8);
    a_buf[33] = (uint8_t)(a_pad0_len & 0xFF);
    if (a_pad0_len) memset(a_buf + 34, 0, a_pad0_len);
    return (int)l_need;
}

int anytls_codec_build_frame(uint8_t a_cmd, uint32_t a_stream_id,
                             const uint8_t *a_data, size_t a_data_len,
                             uint8_t *a_buf, size_t a_buf_size)
{
    if (!a_buf || a_data_len > 0xFFFF) return ANYTLS_ERR_PARAM;
    if (a_data_len && !a_data) return ANYTLS_ERR_PARAM;
    size_t l_need = 1 + 4 + 2 + a_data_len;
    if (a_buf_size < l_need) return ANYTLS_ERR_PARAM;
    size_t o = 0;
    a_buf[o++] = a_cmd;
    a_buf[o++] = (uint8_t)(a_stream_id >> 24);
    a_buf[o++] = (uint8_t)(a_stream_id >> 16);
    a_buf[o++] = (uint8_t)(a_stream_id >> 8);
    a_buf[o++] = (uint8_t)(a_stream_id);
    a_buf[o++] = (uint8_t)(a_data_len >> 8);
    a_buf[o++] = (uint8_t)(a_data_len & 0xFF);
    if (a_data_len) { memcpy(a_buf + o, a_data, a_data_len); o += a_data_len; }
    return (int)o;
}

int anytls_codec_build_socksaddr(const char *a_host, uint16_t a_port,
                                 uint8_t *a_buf, size_t a_buf_size)
{
    if (!a_host || !a_host[0] || !a_buf || !a_port) return ANYTLS_ERR_PARAM;
    size_t l_host_len = strlen(a_host);
    if (l_host_len > 255) return ANYTLS_ERR_PARAM;

    struct in_addr l_v4;
    struct in6_addr l_v6;
    uint8_t l_atyp; size_t l_addr_len;
    if (inet_pton(AF_INET, a_host, &l_v4) == 1) { l_atyp = 0x01; l_addr_len = 4; }
    else if (inet_pton(AF_INET6, a_host, &l_v6) == 1) { l_atyp = 0x04; l_addr_len = 16; }
    else { l_atyp = 0x03; l_addr_len = 1 + l_host_len; }

    size_t l_need = 1 + l_addr_len + 2;
    if (a_buf_size < l_need) return ANYTLS_ERR_PARAM;
    size_t o = 0;
    a_buf[o++] = l_atyp;
    if (l_atyp == 0x01) { memcpy(a_buf + o, &l_v4, 4); o += 4; }
    else if (l_atyp == 0x04) { memcpy(a_buf + o, &l_v6, 16); o += 16; }
    else { a_buf[o++] = (uint8_t)l_host_len; memcpy(a_buf + o, a_host, l_host_len); o += l_host_len; }
    a_buf[o++] = (uint8_t)(a_port >> 8);
    a_buf[o++] = (uint8_t)(a_port & 0xFF);
    return (int)o;
}