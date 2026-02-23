/*
 * DAP CRC32 - IEEE 802.3 CRC-32 implementation
 *
 * Polynomial: 0xEDB88320 (reversed representation of 0x04C11DB7)
 */

#pragma once

#include "dap_common.h"

/**
 * Compute CRC32 checksum over a data buffer.
 *
 * @param a_data   Pointer to data
 * @param a_len    Length of data in bytes
 * @return         CRC32 checksum
 */
uint32_t dap_crc32(const void *a_data, size_t a_len);
