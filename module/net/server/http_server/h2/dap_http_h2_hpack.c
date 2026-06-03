/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "dap_common.h"
#include "dap_http_h2_hpack.h"
#include "dap_http_h2_hpack_huffman.h"

#define LOG_TAG "dap_hpack"

#define HPACK_MAX_STR_BYTES (256u * 1024u)

#define HPACK_ERR_OK       0
#define HPACK_ERR_BUF     -1
#define HPACK_ERR_PROTO   -2
#define HPACK_ERR_MEM     -3

#define HPACK_ST_ENTRY(n, v) \
    { (char *)(n), (char *)(v), sizeof(n) - 1, sizeof(v) - 1 }

static const dap_hpack_header_t s_hpack_static[DAP_HPACK_STATIC_TABLE_SIZE] = {
    HPACK_ST_ENTRY(":authority", ""),
    HPACK_ST_ENTRY(":method", "GET"),
    HPACK_ST_ENTRY(":method", "POST"),
    HPACK_ST_ENTRY(":path", "/"),
    HPACK_ST_ENTRY(":path", "/index.html"),
    HPACK_ST_ENTRY(":scheme", "http"),
    HPACK_ST_ENTRY(":scheme", "https"),
    HPACK_ST_ENTRY(":status", "200"),
    HPACK_ST_ENTRY(":status", "204"),
    HPACK_ST_ENTRY(":status", "206"),
    HPACK_ST_ENTRY(":status", "304"),
    HPACK_ST_ENTRY(":status", "400"),
    HPACK_ST_ENTRY(":status", "404"),
    HPACK_ST_ENTRY(":status", "500"),
    HPACK_ST_ENTRY("accept-charset", ""),
    HPACK_ST_ENTRY("accept-encoding", "gzip, deflate"),
    HPACK_ST_ENTRY("accept-language", ""),
    HPACK_ST_ENTRY("accept-ranges", ""),
    HPACK_ST_ENTRY("accept", ""),
    HPACK_ST_ENTRY("access-control-allow-origin", ""),
    HPACK_ST_ENTRY("age", ""),
    HPACK_ST_ENTRY("allow", ""),
    HPACK_ST_ENTRY("authorization", ""),
    HPACK_ST_ENTRY("cache-control", ""),
    HPACK_ST_ENTRY("content-disposition", ""),
    HPACK_ST_ENTRY("content-encoding", ""),
    HPACK_ST_ENTRY("content-language", ""),
    HPACK_ST_ENTRY("content-length", ""),
    HPACK_ST_ENTRY("content-location", ""),
    HPACK_ST_ENTRY("content-range", ""),
    HPACK_ST_ENTRY("content-type", ""),
    HPACK_ST_ENTRY("cookie", ""),
    HPACK_ST_ENTRY("date", ""),
    HPACK_ST_ENTRY("etag", ""),
    HPACK_ST_ENTRY("expect", ""),
    HPACK_ST_ENTRY("expires", ""),
    HPACK_ST_ENTRY("from", ""),
    HPACK_ST_ENTRY("host", ""),
    HPACK_ST_ENTRY("if-match", ""),
    HPACK_ST_ENTRY("if-modified-since", ""),
    HPACK_ST_ENTRY("if-none-match", ""),
    HPACK_ST_ENTRY("if-range", ""),
    HPACK_ST_ENTRY("if-unmodified-since", ""),
    HPACK_ST_ENTRY("last-modified", ""),
    HPACK_ST_ENTRY("link", ""),
    HPACK_ST_ENTRY("location", ""),
    HPACK_ST_ENTRY("max-forwards", ""),
    HPACK_ST_ENTRY("proxy-authenticate", ""),
    HPACK_ST_ENTRY("proxy-authorization", ""),
    HPACK_ST_ENTRY("range", ""),
    HPACK_ST_ENTRY("referer", ""),
    HPACK_ST_ENTRY("refresh", ""),
    HPACK_ST_ENTRY("retry-after", ""),
    HPACK_ST_ENTRY("server", ""),
    HPACK_ST_ENTRY("set-cookie", ""),
    HPACK_ST_ENTRY("strict-transport-security", ""),
    HPACK_ST_ENTRY("transfer-encoding", ""),
    HPACK_ST_ENTRY("user-agent", ""),
    HPACK_ST_ENTRY("vary", ""),
    HPACK_ST_ENTRY("via", ""),
    HPACK_ST_ENTRY("www-authenticate", ""),
};

static inline size_t s_huff_encoded_byte_count(const uint8_t *a_src, size_t a_len)
{
    return dap_hpack_huffman_encoded_len(a_src, a_len);
}

static inline int s_huff_encode_bytes(const uint8_t *a_src, size_t a_srclen, uint8_t *a_dst, size_t a_dst_cap, size_t *a_out_len)
{
    return dap_hpack_huffman_encode(a_src, a_srclen, a_dst, a_dst_cap, a_out_len) == 0 ? HPACK_ERR_OK : HPACK_ERR_BUF;
}

static inline int s_huff_decode_bytes(const uint8_t *a_src, size_t a_srclen, uint8_t *a_out, size_t a_out_cap, size_t *a_out_len, int a_final)
{
    (void) a_final;
    return dap_hpack_huffman_decode(a_src, a_srclen, a_out, a_out_cap, a_out_len) == 0 ? HPACK_ERR_OK : HPACK_ERR_PROTO;
}

int dap_hpack_int_encode(uint64_t a_value, uint8_t a_prefix_bits, uint8_t a_prefix_mask,
                         uint8_t *a_dst, size_t a_dst_cap, size_t *a_out_len)
{
    if (!a_dst || !a_out_len || a_prefix_bits == 0 || a_prefix_bits > 8)
        return HPACK_ERR_PROTO;

    uint8_t l_preserved = (uint8_t) (a_dst[0] & (uint8_t) ~a_prefix_mask);
    uint64_t l_I = a_value;
    uint32_t l_maxp = (1u << a_prefix_bits) - 1u;
    size_t l_pos = 0;

    if (l_I < l_maxp) {
        if (a_dst_cap < 1)
            return HPACK_ERR_BUF;
        a_dst[0] = l_preserved | (uint8_t) l_I;
        *a_out_len = 1;
        return HPACK_ERR_OK;
    }
    if (a_dst_cap < 1)
        return HPACK_ERR_BUF;
    a_dst[0] = l_preserved | (uint8_t) l_maxp;
    l_I -= l_maxp;
    l_pos = 1;

    while (l_I >= 128u) {
        if (l_pos >= a_dst_cap)
            return HPACK_ERR_BUF;
        a_dst[l_pos++] = (uint8_t) ((l_I % 128u) + 128u);
        l_I /= 128u;
    }
    if (l_pos >= a_dst_cap)
        return HPACK_ERR_BUF;
    a_dst[l_pos++] = (uint8_t) l_I;
    *a_out_len = l_pos;
    return HPACK_ERR_OK;
}

int dap_hpack_int_decode(const uint8_t *a_src, size_t a_src_len, uint8_t a_prefix_bits,
                         uint64_t *a_out_value, size_t *a_out_consumed)
{
    if (!a_src || !a_out_value || !a_out_consumed || a_src_len < 1 || a_prefix_bits == 0 || a_prefix_bits > 8)
        return HPACK_ERR_PROTO;

    uint32_t l_mask = (1u << a_prefix_bits) - 1u;
    uint64_t l_I = a_src[0] & l_mask;
    size_t l_pos = 1;

    if (l_I < l_mask) {
        *a_out_value = l_I;
        *a_out_consumed = 1;
        return HPACK_ERR_OK;
    }

    uint32_t l_M = 0;
    while (l_pos < a_src_len) {
        uint8_t l_B = a_src[l_pos++];
        l_I += (uint64_t) (l_B & 127u) << l_M;
        l_M += 7u;
        if ((l_B & 128u) == 0) {
            *a_out_value = l_I;
            *a_out_consumed = l_pos;
            return HPACK_ERR_OK;
        }
    }
    (void) l_I;
    (void) l_M;
    return HPACK_ERR_PROTO;
}

static void s_dyn_free_entry(dap_hpack_dyn_entry_t *a_e)
{
    if (!a_e)
        return;
    DAP_DELETE(a_e->name);
    DAP_DELETE(a_e->value);
    a_e->name = NULL;
    a_e->value = NULL;
    a_e->name_len = a_e->value_len = a_e->entry_size = 0;
}

static void s_dyn_evict_oldest(dap_hpack_context_t *a_ctx)
{
    if (!a_ctx || a_ctx->count == 0)
        return;
    size_t l_oldest = (a_ctx->head + a_ctx->count - 1u) % a_ctx->capacity;
    dap_hpack_dyn_entry_t *l_e = &a_ctx->entries[l_oldest];
    if (a_ctx->current_size >= l_e->entry_size)
        a_ctx->current_size -= l_e->entry_size;
    s_dyn_free_entry(l_e);
    a_ctx->count--;
}

static int s_dyn_insert(dap_hpack_context_t *a_ctx, const char *a_name, size_t a_nl, const char *a_val, size_t a_vl)
{
    size_t l_esize = a_nl + a_vl + DAP_HPACK_ENTRY_OVERHEAD;

    while (a_ctx->current_size + l_esize > a_ctx->max_size && a_ctx->count > 0)
        s_dyn_evict_oldest(a_ctx);
    while (a_ctx->count >= a_ctx->capacity && a_ctx->capacity > 0)
        s_dyn_evict_oldest(a_ctx);

    size_t l_idx;
    if (a_ctx->count == 0) {
        l_idx = a_ctx->head = 0;
    } else {
        a_ctx->head = (a_ctx->head + a_ctx->capacity - 1u) % a_ctx->capacity;
        l_idx = a_ctx->head;
    }

    dap_hpack_dyn_entry_t *l_e = &a_ctx->entries[l_idx];
    l_e->name = DAP_NEW_Z_SIZE(char, a_nl + 1);
    l_e->value = DAP_NEW_Z_SIZE(char, a_vl + 1);
    if (!l_e->name || !l_e->value) {
        s_dyn_free_entry(l_e);
        log_it(L_ERROR, "HPACK dynamic table OOM");
        return HPACK_ERR_MEM;
    }
    memcpy(l_e->name, a_name, a_nl);
    l_e->name[a_nl] = '\0';
    memcpy(l_e->value, a_val, a_vl);
    l_e->value[a_vl] = '\0';
    l_e->name_len = a_nl;
    l_e->value_len = a_vl;
    l_e->entry_size = l_esize;
    a_ctx->current_size += l_esize;
    a_ctx->count++;
    return HPACK_ERR_OK;
}

static int s_resolve_index(dap_hpack_context_t *a_ctx, uint64_t a_index,
                           const char **a_oname, size_t *a_onl, const char **a_oval, size_t *a_ovl)
{
    if (a_index == 0)
        return HPACK_ERR_PROTO;
    if (a_index <= DAP_HPACK_STATIC_TABLE_SIZE) {
        const dap_hpack_header_t *l_h = &s_hpack_static[a_index - 1u];
        *a_oname = l_h->name;
        *a_onl = l_h->name_len;
        *a_oval = l_h->value;
        *a_ovl = l_h->value_len;
        return HPACK_ERR_OK;
    }
    size_t l_d = (size_t) a_index - 62u;
    if (!a_ctx || l_d >= a_ctx->count)
        return HPACK_ERR_PROTO;
    size_t l_phys = (a_ctx->head + l_d) % a_ctx->capacity;
    dap_hpack_dyn_entry_t *l_e = &a_ctx->entries[l_phys];
    *a_oname = l_e->name;
    *a_onl = l_e->name_len;
    *a_oval = l_e->value;
    *a_ovl = l_e->value_len;
    return HPACK_ERR_OK;
}

int dap_hpack_context_init(dap_hpack_context_t *a_ctx, size_t a_max_size)
{
    if (!a_ctx)
        return HPACK_ERR_PROTO;
    memset(a_ctx, 0, sizeof *a_ctx);
    a_ctx->max_size = a_max_size ? a_max_size : DAP_HPACK_DEFAULT_TABLE_SIZE;

    size_t l_cap = a_ctx->max_size / (DAP_HPACK_ENTRY_OVERHEAD + 1u);
    if (l_cap < 32u)
        l_cap = 32u;
    if (l_cap > 4096u)
        l_cap = 4096u;

    a_ctx->entries = DAP_NEW_Z_COUNT(dap_hpack_dyn_entry_t, l_cap);
    if (!a_ctx->entries) {
        log_it(L_ERROR, "HPACK context init OOM");
        return HPACK_ERR_MEM;
    }
    a_ctx->capacity = l_cap;
    return HPACK_ERR_OK;
}

void dap_hpack_context_deinit(dap_hpack_context_t *a_ctx)
{
    if (!a_ctx)
        return;
    while (a_ctx->count > 0)
        s_dyn_evict_oldest(a_ctx);
    DAP_DELETE(a_ctx->entries);
    a_ctx->entries = NULL;
    a_ctx->capacity = 0;
    a_ctx->head = 0;
    a_ctx->current_size = 0;
}

void dap_hpack_context_resize(dap_hpack_context_t *a_ctx, size_t a_new_max_size)
{
    if (!a_ctx)
        return;
    a_ctx->max_size = a_new_max_size;
    while (a_ctx->current_size > a_ctx->max_size && a_ctx->count > 0)
        s_dyn_evict_oldest(a_ctx);
}

const dap_hpack_header_t *dap_hpack_static_find(const char *a_name, size_t a_name_len,
                                                const char *a_value, size_t a_value_len,
                                                size_t *a_out_index)
{
    if (!a_name || !a_value || !a_out_index)
        return NULL;
    for (size_t l_i = 0; l_i < DAP_HPACK_STATIC_TABLE_SIZE; l_i++) {
        const dap_hpack_header_t *l_h = &s_hpack_static[l_i];
        if (l_h->name_len == a_name_len && l_h->value_len == a_value_len &&
            memcmp(l_h->name, a_name, a_name_len) == 0 && memcmp(l_h->value, a_value, a_value_len) == 0) {
            *a_out_index = l_i + 1u;
            return l_h;
        }
    }
    return NULL;
}

static size_t s_static_find_name_index(const char *a_name, size_t a_name_len)
{
    for (size_t l_i = 0; l_i < DAP_HPACK_STATIC_TABLE_SIZE; l_i++) {
        const dap_hpack_header_t *l_h = &s_hpack_static[l_i];
        if (l_h->name_len == a_name_len && memcmp(l_h->name, a_name, a_name_len) == 0)
            return l_i + 1u;
    }
    return 0;
}

static int s_emit_string_huffman(uint8_t *a_dst, size_t a_dst_cap, size_t *a_pos,
                                 const uint8_t *a_data, size_t a_len)
{
    size_t l_enc_len = s_huff_encoded_byte_count(a_data, a_len);
    if (*a_pos >= a_dst_cap)
        return HPACK_ERR_BUF;
    a_dst[*a_pos] = 0x80;
    size_t l_ilen = 0;
    if (dap_hpack_int_encode(l_enc_len, 7, 0x7f, a_dst + *a_pos, a_dst_cap - *a_pos, &l_ilen) != HPACK_ERR_OK)
        return HPACK_ERR_BUF;
    *a_pos += l_ilen;
    if (*a_pos + l_enc_len > a_dst_cap)
        return HPACK_ERR_BUF;
    size_t l_hlen = 0;
    if (s_huff_encode_bytes(a_data, a_len, a_dst + *a_pos, a_dst_cap - *a_pos, &l_hlen) != HPACK_ERR_OK)
        return HPACK_ERR_BUF;
    *a_pos += l_hlen;
    return HPACK_ERR_OK;
}

static int s_decode_string_literal(const uint8_t *a_src, size_t a_src_len, size_t *a_off,
                                   char **a_out_str, size_t *a_out_len)
{
    if (*a_off >= a_src_len)
        return HPACK_ERR_PROTO;
    uint8_t l_first = a_src[*a_off];
    bool l_huff = (l_first & 0x80u) != 0;
    uint64_t l_slen64;
    size_t l_cused = 0;
    if (dap_hpack_int_decode(a_src + *a_off, a_src_len - *a_off, 7, &l_slen64, &l_cused) != HPACK_ERR_OK)
        return HPACK_ERR_PROTO;
    if (l_slen64 > HPACK_MAX_STR_BYTES)
        return HPACK_ERR_PROTO;
    size_t l_slen = (size_t) l_slen64;
    *a_off += l_cused;
    if (*a_off + l_slen > a_src_len)
        return HPACK_ERR_PROTO;
    const uint8_t *l_raw = a_src + *a_off;
    *a_off += l_slen;

    if (l_huff) {
        uint8_t *l_tmp = DAP_NEW_Z_SIZE(uint8_t, HPACK_MAX_STR_BYTES + 1u);
        if (!l_tmp)
            return HPACK_ERR_MEM;
        size_t l_dec_len = 0;
        if (s_huff_decode_bytes(l_raw, l_slen, l_tmp, HPACK_MAX_STR_BYTES, &l_dec_len, 1) != HPACK_ERR_OK) {
            DAP_DELETE(l_tmp);
            return HPACK_ERR_PROTO;
        }
        l_tmp[l_dec_len] = '\0';
        *a_out_str = (char *) l_tmp;
        *a_out_len = l_dec_len;
    } else {
        char *l_tmp = DAP_NEW_Z_SIZE(char, l_slen + 1u);
        if (!l_tmp)
            return HPACK_ERR_MEM;
        memcpy(l_tmp, l_raw, l_slen);
        l_tmp[l_slen] = '\0';
        *a_out_str = l_tmp;
        *a_out_len = l_slen;
    }
    return HPACK_ERR_OK;
}

int dap_hpack_decode(dap_hpack_context_t *a_ctx, const uint8_t *a_src, size_t a_src_len,
                     dap_hpack_header_cb_t a_cb, void *a_userdata)
{
    if (!a_ctx || (!a_src && a_src_len) || !a_cb)
        return HPACK_ERR_PROTO;

    size_t l_off = 0;
    while (l_off < a_src_len) {
        uint8_t l_b = a_src[l_off];

        if (l_b & 0x80u) {
            uint64_t l_index = 0;
            size_t l_cused = 0;
            if (dap_hpack_int_decode(a_src + l_off, a_src_len - l_off, 7, &l_index, &l_cused) != HPACK_ERR_OK)
                return HPACK_ERR_PROTO;
            l_off += l_cused;
            if (l_index == 0)
                return HPACK_ERR_PROTO;
            const char *l_n = NULL, *l_v = NULL;
            size_t l_nl = 0, l_vl = 0;
            if (s_resolve_index(a_ctx, l_index, &l_n, &l_nl, &l_v, &l_vl) != HPACK_ERR_OK)
                return HPACK_ERR_PROTO;
            a_cb(l_n, l_nl, l_v, l_vl, a_userdata);
            continue;
        }

        if ((l_b & 0xc0u) == 0x40u) {
            uint64_t l_index = 0;
            size_t l_cused = 0;
            if (dap_hpack_int_decode(a_src + l_off, a_src_len - l_off, 6, &l_index, &l_cused) != HPACK_ERR_OK)
                return HPACK_ERR_PROTO;
            l_off += l_cused;

            const char *l_n = NULL;
            size_t l_nl = 0;
            char *l_lit_name = NULL;
            char *l_lit_val = NULL;
            size_t l_lit_nl = 0, l_lit_vl = 0;

            if (l_index == 0) {
                if (s_decode_string_literal(a_src, a_src_len, &l_off, &l_lit_name, &l_lit_nl) != HPACK_ERR_OK) {
                    DAP_DELETE(l_lit_name);
                    return HPACK_ERR_PROTO;
                }
                l_n = l_lit_name;
                l_nl = l_lit_nl;
            } else {
                const char *l_nv = NULL;
                size_t l_vl_ignore = 0;
                if (s_resolve_index(a_ctx, l_index, &l_n, &l_nl, &l_nv, &l_vl_ignore) != HPACK_ERR_OK)
                    return HPACK_ERR_PROTO;
            }

            if (s_decode_string_literal(a_src, a_src_len, &l_off, &l_lit_val, &l_lit_vl) != HPACK_ERR_OK) {
                DAP_DELETE(l_lit_name);
                DAP_DELETE(l_lit_val);
                return HPACK_ERR_PROTO;
            }

            a_cb(l_n, l_nl, l_lit_val, l_lit_vl, a_userdata);

            if (s_dyn_insert(a_ctx, l_n, l_nl, l_lit_val, l_lit_vl) != HPACK_ERR_OK) {
                DAP_DELETE(l_lit_name);
                DAP_DELETE(l_lit_val);
                return HPACK_ERR_MEM;
            }
            DAP_DELETE(l_lit_name);
            DAP_DELETE(l_lit_val);
            continue;
        }

        if ((l_b & 0xe0u) == 0x20u) {
            uint64_t l_new_size = 0;
            size_t l_cused = 0;
            if (dap_hpack_int_decode(a_src + l_off, a_src_len - l_off, 5, &l_new_size, &l_cused) != HPACK_ERR_OK)
                return HPACK_ERR_PROTO;
            l_off += l_cused;
            dap_hpack_context_resize(a_ctx, (size_t) l_new_size);
            continue;
        }

        if ((l_b & 0xf0u) == 0x10u) {
            uint64_t l_index = 0;
            size_t l_cused = 0;
            if (dap_hpack_int_decode(a_src + l_off, a_src_len - l_off, 4, &l_index, &l_cused) != HPACK_ERR_OK)
                return HPACK_ERR_PROTO;
            l_off += l_cused;

            const char *l_n = NULL;
            size_t l_nl = 0;
            char *l_lit_name = NULL;
            char *l_lit_val = NULL;
            size_t l_lit_nl = 0, l_lit_vl = 0;

            if (l_index == 0) {
                if (s_decode_string_literal(a_src, a_src_len, &l_off, &l_lit_name, &l_lit_nl) != HPACK_ERR_OK) {
                    DAP_DELETE(l_lit_name);
                    return HPACK_ERR_PROTO;
                }
                l_n = l_lit_name;
                l_nl = l_lit_nl;
            } else {
                const char *l_nv = NULL;
                size_t l_vl_ignore = 0;
                if (s_resolve_index(a_ctx, l_index, &l_n, &l_nl, &l_nv, &l_vl_ignore) != HPACK_ERR_OK)
                    return HPACK_ERR_PROTO;
            }

            if (s_decode_string_literal(a_src, a_src_len, &l_off, &l_lit_val, &l_lit_vl) != HPACK_ERR_OK) {
                DAP_DELETE(l_lit_name);
                DAP_DELETE(l_lit_val);
                return HPACK_ERR_PROTO;
            }
            a_cb(l_n, l_nl, l_lit_val, l_lit_vl, a_userdata);
            DAP_DELETE(l_lit_name);
            DAP_DELETE(l_lit_val);
            continue;
        }

        if ((l_b & 0xf0u) == 0x00u) {
            uint64_t l_index = 0;
            size_t l_cused = 0;
            if (dap_hpack_int_decode(a_src + l_off, a_src_len - l_off, 4, &l_index, &l_cused) != HPACK_ERR_OK)
                return HPACK_ERR_PROTO;
            l_off += l_cused;

            const char *l_n = NULL;
            size_t l_nl = 0;
            char *l_lit_name = NULL;
            char *l_lit_val = NULL;
            size_t l_lit_nl = 0, l_lit_vl = 0;

            if (l_index == 0) {
                if (s_decode_string_literal(a_src, a_src_len, &l_off, &l_lit_name, &l_lit_nl) != HPACK_ERR_OK) {
                    DAP_DELETE(l_lit_name);
                    return HPACK_ERR_PROTO;
                }
                l_n = l_lit_name;
                l_nl = l_lit_nl;
            } else {
                const char *l_nv = NULL;
                size_t l_vl_ignore = 0;
                if (s_resolve_index(a_ctx, l_index, &l_n, &l_nl, &l_nv, &l_vl_ignore) != HPACK_ERR_OK)
                    return HPACK_ERR_PROTO;
            }

            if (s_decode_string_literal(a_src, a_src_len, &l_off, &l_lit_val, &l_lit_vl) != HPACK_ERR_OK) {
                DAP_DELETE(l_lit_name);
                DAP_DELETE(l_lit_val);
                return HPACK_ERR_PROTO;
            }
            a_cb(l_n, l_nl, l_lit_val, l_lit_vl, a_userdata);
            DAP_DELETE(l_lit_name);
            DAP_DELETE(l_lit_val);
            continue;
        }

        log_it(L_ERROR, "HPACK: unknown representation 0x%02x", l_b);
        return HPACK_ERR_PROTO;
    }
    return HPACK_ERR_OK;
}

int dap_hpack_encode(dap_hpack_context_t *a_ctx, const dap_hpack_header_t *a_headers, size_t a_count,
                     uint8_t *a_dst, size_t a_dst_cap, size_t *a_out_len)
{
    if (!a_ctx || (!a_headers && a_count) || !a_dst || !a_out_len)
        return HPACK_ERR_PROTO;

    size_t l_pos = 0;

    for (size_t l_h = 0; l_h < a_count; l_h++) {
        const dap_hpack_header_t *l_hdr = &a_headers[l_h];
        if (!l_hdr->name || l_hdr->name_len == 0)
            return HPACK_ERR_PROTO;

        size_t l_st_index = 0;
        const dap_hpack_header_t *l_full = dap_hpack_static_find(l_hdr->name, l_hdr->name_len,
                                                                 l_hdr->value ? l_hdr->value : "",
                                                                 l_hdr->value_len, &l_st_index);
        if (l_full) {
            if (l_pos >= a_dst_cap)
                return HPACK_ERR_BUF;
            a_dst[l_pos] = 0x80;
            size_t l_ilen = 0;
            if (dap_hpack_int_encode(l_st_index, 7, 0x7f, a_dst + l_pos, a_dst_cap - l_pos, &l_ilen) != HPACK_ERR_OK)
                return HPACK_ERR_BUF;
            l_pos += l_ilen;
            continue;
        }

        size_t l_name_idx = s_static_find_name_index(l_hdr->name, l_hdr->name_len);
        const char *l_v = l_hdr->value ? l_hdr->value : "";
        size_t l_vl = l_hdr->value_len;

        if (l_name_idx != 0) {
            if (l_pos >= a_dst_cap)
                return HPACK_ERR_BUF;
            a_dst[l_pos] = 0x40;
            size_t l_ilen = 0;
            if (dap_hpack_int_encode(l_name_idx, 6, 0x3f, a_dst + l_pos, a_dst_cap - l_pos, &l_ilen) != HPACK_ERR_OK)
                return HPACK_ERR_BUF;
            l_pos += l_ilen;
            if (s_emit_string_huffman(a_dst, a_dst_cap, &l_pos, (const uint8_t *) l_v, l_vl) != HPACK_ERR_OK)
                return HPACK_ERR_BUF;
            if (s_dyn_insert(a_ctx, l_hdr->name, l_hdr->name_len, l_v, l_vl) != HPACK_ERR_OK)
                return HPACK_ERR_MEM;
            continue;
        }

        if (l_pos >= a_dst_cap)
            return HPACK_ERR_BUF;
        a_dst[l_pos] = 0x40;
        size_t l_ilen = 0;
        if (dap_hpack_int_encode(0, 6, 0x3f, a_dst + l_pos, a_dst_cap - l_pos, &l_ilen) != HPACK_ERR_OK)
            return HPACK_ERR_BUF;
        l_pos += l_ilen;
        if (s_emit_string_huffman(a_dst, a_dst_cap, &l_pos, (const uint8_t *) l_hdr->name, l_hdr->name_len) != HPACK_ERR_OK)
            return HPACK_ERR_BUF;
        if (s_emit_string_huffman(a_dst, a_dst_cap, &l_pos, (const uint8_t *) l_v, l_vl) != HPACK_ERR_OK)
            return HPACK_ERR_BUF;
        if (s_dyn_insert(a_ctx, l_hdr->name, l_hdr->name_len, l_v, l_vl) != HPACK_ERR_OK)
            return HPACK_ERR_MEM;
    }

    *a_out_len = l_pos;
    return HPACK_ERR_OK;
}
