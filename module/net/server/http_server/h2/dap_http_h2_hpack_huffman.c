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
 */

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_http_h2_hpack_huffman.h"

#define LOG_TAG "dap_hpack_huffman"

enum { HPACK_HUFF_SYM_COUNT = 257, HPACK_HUFF_EOS = 256, HPACK_HUFF_MAX_NODES = 4096 };

typedef struct hpack_huff_entry {
    uint8_t nbits;
    uint32_t code_msb;
} hpack_huff_entry_t;

typedef struct hpack_huff_node {
    int16_t child[2];
    int16_t sym;
} hpack_huff_node_t;

/*
 * RFC 7541 Appendix B — Huffman code for requests/responses (257 symbols).
 * Indices 0..255 are octet values; index 256 is EOS (30-bit codeword 0x3fffffff, MSB first).
 * code_msb holds the codeword left-aligned in 32 bits (bit 31 is the first bit on the wire).
 */
static const hpack_huff_entry_t s_huff[HPACK_HUFF_SYM_COUNT] = {
    {13, 0xffc00000u}, {23, 0xffffb000u}, {28, 0xfffffe20u}, {28, 0xfffffe30u},
    {28, 0xfffffe40u}, {28, 0xfffffe50u}, {28, 0xfffffe60u}, {28, 0xfffffe70u},
    {28, 0xfffffe80u}, {24, 0xffffea00u}, {30, 0xfffffff0u}, {28, 0xfffffe90u},
    {28, 0xfffffea0u}, {30, 0xfffffff4u}, {28, 0xfffffeb0u}, {28, 0xfffffec0u},
    {28, 0xfffffed0u}, {28, 0xfffffee0u}, {28, 0xfffffef0u}, {28, 0xffffff00u},
    {28, 0xffffff10u}, {28, 0xffffff20u}, {30, 0xfffffff8u}, {28, 0xffffff30u},
    {28, 0xffffff40u}, {28, 0xffffff50u}, {28, 0xffffff60u}, {28, 0xffffff70u},
    {28, 0xffffff80u}, {28, 0xffffff90u}, {28, 0xffffffa0u}, {28, 0xffffffb0u},
    {6, 0x50000000u}, {10, 0xfe000000u}, {10, 0xfe400000u}, {12, 0xffa00000u},
    {13, 0xffc80000u}, {6, 0x54000000u}, {8, 0xf8000000u}, {11, 0xff400000u},
    {10, 0xfe800000u}, {10, 0xfec00000u}, {8, 0xf9000000u}, {11, 0xff600000u},
    {8, 0xfa000000u}, {6, 0x58000000u}, {6, 0x5c000000u}, {6, 0x60000000u},
    {5, 0x0u}, {5, 0x8000000u}, {5, 0x10000000u}, {6, 0x64000000u},
    {6, 0x68000000u}, {6, 0x6c000000u}, {6, 0x70000000u}, {6, 0x74000000u},
    {6, 0x78000000u}, {6, 0x7c000000u}, {7, 0xb8000000u}, {8, 0xfb000000u},
    {15, 0xfff80000u}, {6, 0x80000000u}, {12, 0xffb00000u}, {10, 0xff000000u},
    {13, 0xffd00000u}, {6, 0x84000000u}, {7, 0xba000000u}, {7, 0xbc000000u},
    {7, 0xbe000000u}, {7, 0xc0000000u}, {7, 0xc2000000u}, {7, 0xc4000000u},
    {7, 0xc6000000u}, {7, 0xc8000000u}, {7, 0xca000000u}, {7, 0xcc000000u},
    {7, 0xce000000u}, {7, 0xd0000000u}, {7, 0xd2000000u}, {7, 0xd4000000u},
    {7, 0xd6000000u}, {7, 0xd8000000u}, {7, 0xda000000u}, {7, 0xdc000000u},
    {7, 0xde000000u}, {7, 0xe0000000u}, {7, 0xe2000000u}, {7, 0xe4000000u},
    {8, 0xfc000000u}, {7, 0xe6000000u}, {8, 0xfd000000u}, {13, 0xffd80000u},
    {19, 0xfffe0000u}, {13, 0xffe00000u}, {14, 0xfff00000u}, {6, 0x88000000u},
    {15, 0xfffa0000u}, {5, 0x18000000u}, {6, 0x8c000000u}, {5, 0x20000000u},
    {6, 0x90000000u}, {5, 0x28000000u}, {6, 0x94000000u}, {6, 0x98000000u},
    {6, 0x9c000000u}, {5, 0x30000000u}, {7, 0xe8000000u}, {7, 0xea000000u},
    {6, 0xa0000000u}, {6, 0xa4000000u}, {6, 0xa8000000u}, {5, 0x38000000u},
    {6, 0xac000000u}, {7, 0xec000000u}, {6, 0xb0000000u}, {5, 0x40000000u},
    {5, 0x48000000u}, {6, 0xb4000000u}, {7, 0xee000000u}, {7, 0xf0000000u},
    {7, 0xf2000000u}, {7, 0xf4000000u}, {7, 0xf6000000u}, {15, 0xfffc0000u},
    {11, 0xff800000u}, {14, 0xfff40000u}, {13, 0xffe80000u}, {28, 0xffffffc0u},
    {20, 0xfffe6000u}, {22, 0xffff4800u}, {20, 0xfffe7000u}, {20, 0xfffe8000u},
    {22, 0xffff4c00u}, {22, 0xffff5000u}, {22, 0xffff5400u}, {23, 0xffffb200u},
    {22, 0xffff5800u}, {23, 0xffffb400u}, {23, 0xffffb600u}, {23, 0xffffb800u},
    {23, 0xffffba00u}, {23, 0xffffbc00u}, {24, 0xffffeb00u}, {23, 0xffffbe00u},
    {24, 0xffffec00u}, {24, 0xffffed00u}, {22, 0xffff5c00u}, {23, 0xffffc000u},
    {24, 0xffffee00u}, {23, 0xffffc200u}, {23, 0xffffc400u}, {23, 0xffffc600u},
    {23, 0xffffc800u}, {21, 0xfffee000u}, {22, 0xffff6000u}, {23, 0xffffca00u},
    {22, 0xffff6400u}, {23, 0xffffcc00u}, {23, 0xffffce00u}, {24, 0xffffef00u},
    {22, 0xffff6800u}, {21, 0xfffee800u}, {20, 0xfffe9000u}, {22, 0xffff6c00u},
    {22, 0xffff7000u}, {23, 0xffffd000u}, {23, 0xffffd200u}, {21, 0xfffef000u},
    {23, 0xffffd400u}, {22, 0xffff7400u}, {22, 0xffff7800u}, {24, 0xfffff000u},
    {21, 0xfffef800u}, {22, 0xffff7c00u}, {23, 0xffffd600u}, {23, 0xffffd800u},
    {21, 0xffff0000u}, {21, 0xffff0800u}, {22, 0xffff8000u}, {21, 0xffff1000u},
    {23, 0xffffda00u}, {22, 0xffff8400u}, {23, 0xffffdc00u}, {23, 0xffffde00u},
    {20, 0xfffea000u}, {22, 0xffff8800u}, {22, 0xffff8c00u}, {22, 0xffff9000u},
    {23, 0xffffe000u}, {22, 0xffff9400u}, {22, 0xffff9800u}, {23, 0xffffe200u},
    {26, 0xfffff800u}, {26, 0xfffff840u}, {20, 0xfffeb000u}, {19, 0xfffe2000u},
    {22, 0xffff9c00u}, {23, 0xffffe400u}, {22, 0xffffa000u}, {25, 0xfffff600u},
    {26, 0xfffff880u}, {26, 0xfffff8c0u}, {26, 0xfffff900u}, {27, 0xfffffbc0u},
    {27, 0xfffffbe0u}, {26, 0xfffff940u}, {24, 0xfffff100u}, {25, 0xfffff680u},
    {19, 0xfffe4000u}, {21, 0xffff1800u}, {26, 0xfffff980u}, {27, 0xfffffc00u},
    {27, 0xfffffc20u}, {26, 0xfffff9c0u}, {27, 0xfffffc40u}, {24, 0xfffff200u},
    {21, 0xffff2000u}, {21, 0xffff2800u}, {26, 0xfffffa00u}, {26, 0xfffffa40u},
    {28, 0xffffffd0u}, {27, 0xfffffc60u}, {27, 0xfffffc80u}, {27, 0xfffffca0u},
    {20, 0xfffec000u}, {24, 0xfffff300u}, {20, 0xfffed000u}, {21, 0xffff3000u},
    {22, 0xffffa400u}, {21, 0xffff3800u}, {21, 0xffff4000u}, {23, 0xffffe600u},
    {22, 0xffffa800u}, {22, 0xffffac00u}, {25, 0xfffff700u}, {25, 0xfffff780u},
    {24, 0xfffff400u}, {24, 0xfffff500u}, {26, 0xfffffa80u}, {23, 0xffffe800u},
    {26, 0xfffffac0u}, {27, 0xfffffcc0u}, {26, 0xfffffb00u}, {26, 0xfffffb40u},
    {27, 0xfffffce0u}, {27, 0xfffffd00u}, {27, 0xfffffd20u}, {27, 0xfffffd40u},
    {27, 0xfffffd60u}, {28, 0xffffffe0u}, {27, 0xfffffd80u}, {27, 0xfffffda0u},
    {27, 0xfffffdc0u}, {27, 0xfffffde0u}, {27, 0xfffffe00u}, {26, 0xfffffb80u},
    {30, 0xfffffffcu}

};

static hpack_huff_node_t s_nodes[HPACK_HUFF_MAX_NODES];
static size_t s_node_count;
static pthread_once_t s_tree_once = PTHREAD_ONCE_INIT;

static int s_huff_new_node(void)
{
    if (s_node_count >= HPACK_HUFF_MAX_NODES)
        return -1;
    int l_idx = (int)s_node_count++;
    s_nodes[l_idx].child[0] = s_nodes[l_idx].child[1] = -1;
    s_nodes[l_idx].sym = -1;
    return l_idx;
}

static int s_huff_insert(int a_sym_idx)
{
    const uint32_t l_code = s_huff[a_sym_idx].code_msb;
    const unsigned l_nbits = (unsigned)s_huff[a_sym_idx].nbits;
    int l_node = 0;
    for (unsigned l_i = 0; l_i < l_nbits; l_i++) {
        const int l_bit = (int)((l_code >> (31U - l_i)) & 1U);
        if (s_nodes[l_node].sym >= 0)
            return -1;
        if (s_nodes[l_node].child[l_bit] < 0) {
            const int l_ch = s_huff_new_node();
            if (l_ch < 0)
                return -1;
            s_nodes[l_node].child[l_bit] = (int16_t)l_ch;
        }
        l_node = s_nodes[l_node].child[l_bit];
        if (s_nodes[l_node].sym >= 0 && l_i + 1U < l_nbits)
            return -1;
    }
    if (s_nodes[l_node].child[0] >= 0 || s_nodes[l_node].child[1] >= 0)
        return -1;
    s_nodes[l_node].sym = (int16_t)(a_sym_idx == HPACK_HUFF_EOS ? HPACK_HUFF_EOS : a_sym_idx);
    return 0;
}

static void s_huff_build_tree(void)
{
    s_node_count = 0;
    (void)s_huff_new_node();
    for (int l_i = 0; l_i < HPACK_HUFF_SYM_COUNT; l_i++) {
        if (s_huff_insert(l_i) != 0) {
            log_it(L_CRITICAL, "HPACK Huffman tree build failed at sym %d", l_i);
            break;
        }
    }
}

static void s_huff_ensure_tree(void)
{
    (void)pthread_once(&s_tree_once, s_huff_build_tree);
}

static int s_huff_read_bit(const uint8_t *a_src, size_t a_src_len, size_t a_bit_pos)
{
    if (a_bit_pos >= a_src_len * 8U)
        return -1;
    const size_t l_byte = a_bit_pos / 8U;
    const unsigned l_rem = (unsigned)(a_bit_pos % 8U);
    return (int)(a_src[l_byte] >> (7U - l_rem)) & 1;
}

static int s_huff_padding_ok(const uint8_t *a_src, size_t a_src_len, size_t a_from_bit, size_t a_to_bit)
{
    for (size_t l_b = a_from_bit; l_b < a_to_bit; l_b++) {
        const int l_v = s_huff_read_bit(a_src, a_src_len, l_b);
        if (l_v != 1)
            return 0;
    }
    return 1;
}

size_t dap_hpack_huffman_encoded_len(const uint8_t *a_src, size_t a_src_len)
{
    size_t l_bits = 0;
    for (size_t l_i = 0; l_i < a_src_len; l_i++)
        l_bits += (size_t)s_huff[a_src[l_i]].nbits;
    return (l_bits + 7U) / 8U;
}

int dap_hpack_huffman_encode(const uint8_t *a_src, size_t a_src_len, uint8_t *a_dst, size_t a_dst_cap, size_t *a_out_len)
{
    if (!a_out_len)
        return -1;
    *a_out_len = 0;
    if (!a_dst && a_dst_cap > 0)
        return -1;
    uint64_t l_acc = 0;
    unsigned l_acc_bits = 0;
    size_t l_out = 0;
    for (size_t l_i = 0; l_i < a_src_len; l_i++) {
        const hpack_huff_entry_t *l_e = &s_huff[a_src[l_i]];
        const uint64_t l_part = (uint64_t)(l_e->code_msb >> (32U - l_e->nbits));
        l_acc = (l_acc << l_e->nbits) | l_part;
        l_acc_bits += l_e->nbits;
        while (l_acc_bits >= 8U) {
            l_acc_bits -= 8U;
            const uint8_t l_ob = (uint8_t)((l_acc >> l_acc_bits) & 0xFFU);
            if (l_out >= a_dst_cap) {
                log_it(L_ERROR, "Huffman encode: destination buffer too small");
                return -1;
            }
            a_dst[l_out++] = l_ob;
        }
    }
    while ((l_acc_bits & 7U) != 0U) {
        l_acc = (l_acc << 1) | 1U;
        l_acc_bits++;
    }
    while (l_acc_bits >= 8U) {
        l_acc_bits -= 8U;
        const uint8_t l_ob = (uint8_t)((l_acc >> l_acc_bits) & 0xFFU);
        if (l_out >= a_dst_cap) {
            log_it(L_ERROR, "Huffman encode: destination buffer too small");
            return -1;
        }
        a_dst[l_out++] = l_ob;
    }
    *a_out_len = l_out;
    return 0;
}

int dap_hpack_huffman_decode(const uint8_t *a_src, size_t a_src_len, uint8_t *a_dst, size_t a_dst_cap, size_t *a_out_len)
{
    if (!a_out_len)
        return -1;
    *a_out_len = 0;
    if (a_src_len == 0) {
        return 0;
    }
    if (!a_dst && a_dst_cap > 0)
        return -1;
    s_huff_ensure_tree();
    const size_t l_bit_total = a_src_len * 8U;
    size_t l_bit_pos = 0;
    size_t l_written = 0;
    while (l_bit_pos < l_bit_total) {
        const size_t l_path_start = l_bit_pos;
        int l_node = 0;
        while (s_nodes[l_node].sym < 0) {
            if (l_bit_pos >= l_bit_total) {
                if (!s_huff_padding_ok(a_src, a_src_len, l_path_start, l_bit_total)) {
                    log_it(L_ERROR, "Huffman decode: invalid padding (not EOS prefix)");
                    return -1;
                }
                *a_out_len = l_written;
                return 0;
            }
            const int l_b = s_huff_read_bit(a_src, a_src_len, l_bit_pos++);
            if (l_b < 0) {
                log_it(L_ERROR, "Huffman decode: read bit failed");
                return -1;
            }
            l_node = s_nodes[l_node].child[l_b];
            if (l_node < 0) {
                log_it(L_ERROR, "Huffman decode: unknown bit sequence");
                return -1;
            }
        }
        const int l_sym = s_nodes[l_node].sym;
        if (l_sym == HPACK_HUFF_EOS) {
            log_it(L_ERROR, "Huffman decode: EOS symbol in string data");
            return -1;
        }
        if (l_written >= a_dst_cap) {
            log_it(L_ERROR, "Huffman decode: destination buffer too small");
            return -1;
        }
        a_dst[l_written++] = (uint8_t)l_sym;
    }
    *a_out_len = l_written;
    return 0;
}
