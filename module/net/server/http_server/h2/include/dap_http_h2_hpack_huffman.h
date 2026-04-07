/*
 * Authors:
 * DeM Labs Ltd. https://demlabs.net
 * Copyright (c) 2025
 *
 * This file is part of DAP the open source project.
 *
 * DAP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * DAP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * HPACK Huffman encoder/decoder (RFC 7541 Appendix B).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode an HPACK Huffman-encoded octet string into plain bytes.
 *
 * @param a_src      Huffman-encoded input.
 * @param a_src_len  Length of @a a_src in bytes.
 * @param a_dst      Output buffer for decoded octets.
 * @param a_dst_cap  Capacity of @a a_dst.
 * @param a_out_len  On success, number of bytes written to @a a_dst.
 * @return 0 on success, non-zero on error.
 */
int dap_hpack_huffman_decode(const uint8_t *a_src, size_t a_src_len,
                             uint8_t *a_dst, size_t a_dst_cap, size_t *a_out_len);

/**
 * @brief Huffman-encode a plain octet string for HPACK.
 *
 * @param a_src      Plain input octets.
 * @param a_src_len  Length of @a a_src.
 * @param a_dst      Output buffer for encoded bits (packed into bytes, MSB first per RFC 7541).
 * @param a_dst_cap  Capacity of @a a_dst.
 * @param a_out_len  On success, number of bytes written to @a a_dst.
 * @return 0 on success, non-zero on error.
 */
int dap_hpack_huffman_encode(const uint8_t *a_src, size_t a_src_len,
                             uint8_t *a_dst, size_t a_dst_cap, size_t *a_out_len);

/**
 * @brief Return the encoded length in bytes for a plain string (including bit padding to a byte boundary).
 *
 * @param a_src      Plain input octets.
 * @param a_src_len  Length of @a a_src.
 * @return Encoded size in bytes.
 */
size_t dap_hpack_huffman_encoded_len(const uint8_t *a_src, size_t a_src_len);

#ifdef __cplusplus
}
#endif
