/**
 * @file test_unit.c
 * @brief Offline unit tests for the XHTTP VLESS codec (no SDK, no network).
 */

#include "xhttp/xhttp.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define LOG(...) do { printf("[xhttp-unit] " __VA_ARGS__); printf("\n"); } while (0)

static void test_vless_request_ipv4(void)
{
    LOG("test_vless_request_ipv4");
    uint8_t uuid[16]; for (int i = 0; i < 16; ++i) uuid[i] = (uint8_t)(i + 1);
    uint8_t buf[264];
    int n = xhttp_codec_build_vless_request(uuid, "127.0.0.1", 8079, buf, sizeof(buf));
    /* ver(1)+uuid(16)+addons(1)+cmd(1)+port(2)+atyp(1)+addr(4) = 26 */
    assert(n == 26);
    assert(buf[0] == 0x00);
    assert(memcmp(buf + 1, uuid, 16) == 0);
    assert(buf[17] == 0x00);          /* addons len */
    assert(buf[18] == 0x01);          /* cmd TCP */
    assert(buf[19] == (8079 >> 8) && buf[20] == (8079 & 0xFF));
    assert(buf[21] == 0x01);          /* atyp IPv4 */
    assert(buf[22] == 127 && buf[25] == 1);
}

static void test_vless_request_domain(void)
{
    LOG("test_vless_request_domain");
    uint8_t uuid[16] = {0};
    uint8_t buf[264];
    int n = xhttp_codec_build_vless_request(uuid, "example.com", 443, buf, sizeof(buf));
    /* 22 + 1 + 11 = 34 */
    assert(n == 34);
    assert(buf[21] == 0x02);          /* atyp domain (VLESS) */
    assert(buf[22] == 11);
    assert(memcmp(buf + 23, "example.com", 11) == 0);
    assert(buf[19] == 0x01 && buf[20] == 0xBB);  /* port 443 at offset 19-20 */
}

static void test_vless_request_invalid(void)
{
    LOG("test_vless_request_invalid");
    uint8_t uuid[16] = {0};
    uint8_t buf[264];
    assert(xhttp_codec_build_vless_request(uuid, "", 443, buf, sizeof(buf)) == XHTTP_ERR_PARAM);
    assert(xhttp_codec_build_vless_request(uuid, "host", 0, buf, sizeof(buf)) == XHTTP_ERR_PARAM);
    assert(xhttp_codec_build_vless_request(uuid, "host", 443, buf, 5) == XHTTP_ERR_PARAM);
    assert(xhttp_codec_build_vless_request(NULL, "host", 443, buf, sizeof(buf)) == XHTTP_ERR_PARAM);
}

int main(void)
{
    test_vless_request_ipv4();
    test_vless_request_domain();
    test_vless_request_invalid();
    LOG("ALL XHTTP UNIT TESTS PASSED");
    return 0;
}