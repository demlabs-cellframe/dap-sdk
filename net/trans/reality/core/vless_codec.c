/**
 * @file vless_codec.c
 * @brief VLESS request header encoder (Xray wire format, encoding.go v26.10.31).
 *
 * Request:  [ver=0][uuid 16][addons_len][addons pb][command][port 2B BE]
 *           [addr_type][addr]
 *   addons with flow "xtls-rprx-vision": protobuf field 1 (string)
 *   addr_type: 1 = IPv4(4B), 2 = domain(1B len + bytes), 3 = IPv6(16B)
 */

#include "reality_internal.h"
#include <stdio.h>

#define VLESS_CMD_TCP 0x01
#define VLESS_CMD_UDP 0x02

int vless_build_request(const uint8_t a_uuid[16], const char *a_host, uint16_t a_port,
                        int a_command_tcp, uint8_t *a_out, size_t a_cap, size_t *a_out_len)
{
    if (!a_uuid || !a_host || !a_out || !a_out_len) return REALITY_ERR_PARAM;

    size_t host_len = strlen(a_host);
    if (host_len == 0 || host_len > 255) return REALITY_ERR_PARAM;

    /* addons: len byte + protobuf {1: "xtls-rprx-vision"} */
    static const char flow[] = "xtls-rprx-vision";
    const size_t addons_len = 1 /* tag */ + 1 /* len */ + sizeof(flow) - 1; /* 18 */

    uint8_t addr_type; size_t addr_len;
    unsigned hip[4]; unsigned hp0, hp1, hp2, hp3;
    struct { unsigned x[8]; } ip6;
    if (sscanf(a_host, "%u.%u.%u.%u", &hp0, &hp1, &hp2, &hp3) == 4 &&
        hp0 < 256 && hp1 < 256 && hp2 < 256 && hp3 < 256) {
        addr_type = 1; addr_len = 4;
        hip[0] = hp0; hip[1] = hp1; hip[2] = hp2; hip[3] = hp3;
    } else if (sscanf(a_host, "%x:%x:%x:%x:%x:%x:%x:%x", &ip6.x[0], &ip6.x[1], &ip6.x[2],
                      &ip6.x[3], &ip6.x[4], &ip6.x[5], &ip6.x[6], &ip6.x[7]) == 8) {
        addr_type = 3; addr_len = 16;
    } else {
        addr_type = 2; addr_len = 1 + host_len;
    }

    size_t need = 1 + 16 + 1 + addons_len + 1 + 2 + 1 + addr_len;
    if (a_cap < need) return REALITY_ERR_PARAM;

    uint8_t *p = a_out;
    *p++ = 0;                       /* version */
    memcpy(p, a_uuid, 16); p += 16;
    *p++ = (uint8_t)addons_len;     /* addons length */
    *p++ = 0x0A; *p++ = (uint8_t)(sizeof(flow) - 1); /* protobuf field 1, string */
    memcpy(p, flow, sizeof(flow) - 1); p += sizeof(flow) - 1;
    *p++ = a_command_tcp ? VLESS_CMD_TCP : VLESS_CMD_UDP;
    *p++ = (uint8_t)(a_port >> 8); *p++ = (uint8_t)a_port;   /* port first */
    *p++ = addr_type;
    if (addr_type == 1) {
        for (int i = 0; i < 4; i++) *p++ = (uint8_t)hip[i];
    } else if (addr_type == 3) {
        for (int i = 0; i < 8; i++) { *p++ = (uint8_t)(ip6.x[i] >> 8); *p++ = (uint8_t)ip6.x[i]; }
    } else {
        *p++ = (uint8_t)host_len;
        memcpy(p, a_host, host_len); p += host_len;
    }

    *a_out_len = (size_t)(p - a_out);
    return REALITY_ERR_NONE;
}
