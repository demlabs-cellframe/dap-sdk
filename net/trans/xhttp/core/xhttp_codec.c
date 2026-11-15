/**
 * @file xhttp_codec.c
 * @brief VLESS request-header codec for XHTTP. SDK-free.
 *        VLESS request: ver(0) uuid(16) addons_len(0) cmd(1=TCP) port(2 BE)
 *        atyp(1) addr. No flow (addons empty).
 */

#include "xhttp/xhttp.h"

#include <string.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#endif

int xhttp_codec_build_vless_request(const uint8_t a_uuid[16], const char *a_dest_host,
                                    uint16_t a_dest_port, uint8_t *a_buf, size_t a_buf_size)
{
    if (!a_uuid || !a_dest_host || !a_dest_host[0] || !a_buf || !a_dest_port)
        return XHTTP_ERR_PARAM;
    size_t l_host_len = strlen(a_dest_host);
    if (l_host_len > 255) return XHTTP_ERR_PARAM;

    struct in_addr l_v4;
    struct in6_addr l_v6;
    uint8_t l_atyp; size_t l_addr_len;
    if (inet_pton(AF_INET, a_dest_host, &l_v4) == 1) { l_atyp = 0x01; l_addr_len = 4; }
    else if (inet_pton(AF_INET6, a_dest_host, &l_v6) == 1) { l_atyp = 0x03; l_addr_len = 16; }
    else { l_atyp = 0x02; l_addr_len = 1 + l_host_len; }

    /* ver(1) + uuid(16) + addons_len(1) + cmd(1) + port(2) + atyp(1) + addr + */
    size_t l_need = 1 + 16 + 1 + 1 + 2 + 1 + l_addr_len;
    if (a_buf_size < l_need) return XHTTP_ERR_PARAM;

    size_t o = 0;
    a_buf[o++] = 0x00;                       /* version */
    memcpy(a_buf + o, a_uuid, 16); o += 16;  /* uuid */
    a_buf[o++] = 0x00;                       /* addons length = 0 (no flow) */
    a_buf[o++] = 0x01;                       /* command = TCP */
    a_buf[o++] = (uint8_t)(a_dest_port >> 8);
    a_buf[o++] = (uint8_t)(a_dest_port & 0xFF);
    a_buf[o++] = l_atyp;
    if (l_atyp == 0x01) { memcpy(a_buf + o, &l_v4, 4); o += 4; }
    else if (l_atyp == 0x03) { memcpy(a_buf + o, &l_v6, 16); o += 16; }
    else { a_buf[o++] = (uint8_t)l_host_len; memcpy(a_buf + o, a_dest_host, l_host_len); o += l_host_len; }
    return (int)o;
}