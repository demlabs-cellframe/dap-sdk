/*
 * DAP CRC32 - IEEE 802.3 CRC-32 implementation
 *
 * Polynomial: 0xEDB88320 (reversed representation of 0x04C11DB7)
 * Table-driven for performance.
 */

#include "dap_crc32.h"

static uint32_t s_crc32_table[256];
static bool s_crc32_initialized = false;

static void s_crc32_init(void)
{
    if (s_crc32_initialized)
        return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        s_crc32_table[i] = c;
    }
    s_crc32_initialized = true;
}

uint32_t dap_crc32(const void *a_data, size_t a_len)
{
    s_crc32_init();
    const uint8_t *p = (const uint8_t *)a_data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < a_len; i++)
        crc = s_crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}
