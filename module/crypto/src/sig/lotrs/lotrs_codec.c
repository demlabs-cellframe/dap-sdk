/*
 * LoTRS — Golomb-Rice codec implementation.
 */

#include "lotrs_codec.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#define LOG_TAG "lotrs_codec"
#include "dap_common.h"

/* --- Bitstream writer --- */

int lotrs_bitwriter_init(lotrs_bitwriter_t *w, uint8_t *buf, size_t cap)
{
    if (!w || !buf) return -EINVAL;
    w->buf = buf;
    w->cap = cap;
    w->byte_pos = 0u;
    w->bit_pos = 0u;
    memset(buf, 0, cap);
    return 0;
}

void lotrs_bitwriter_write_bits(lotrs_bitwriter_t *w, uint64_t val, uint8_t nbits)
{
    for (uint8_t i = 0u; i < nbits; ++i) {
        if (w->byte_pos >= w->cap) return;
        uint8_t bit = (val >> i) & 1u;
        w->buf[w->byte_pos] |= (uint8_t)(bit << w->bit_pos);
        w->bit_pos++;
        if (w->bit_pos >= 8u) {
            w->bit_pos = 0u;
            w->byte_pos++;
        }
    }
}

void lotrs_bitwriter_write_unary(lotrs_bitwriter_t *w, uint64_t val)
{
    for (uint64_t i = 0u; i < val; ++i) {
        lotrs_bitwriter_write_bits(w, 1u, 1);
    }
    lotrs_bitwriter_write_bits(w, 0u, 1);
}

void lotrs_bitwriter_pad_to_byte(lotrs_bitwriter_t *w)
{
    if (w->bit_pos > 0u) {
        w->bit_pos = 0u;
        w->byte_pos++;
    }
}

size_t lotrs_bitwriter_bytes_used(const lotrs_bitwriter_t *w)
{
    return w->byte_pos + (w->bit_pos > 0u ? 1u : 0u);
}

/* --- Bitstream reader --- */

int lotrs_bitreader_init(lotrs_bitreader_t *r, const uint8_t *buf, size_t cap)
{
    if (!r || !buf) return -EINVAL;
    r->buf = buf;
    r->cap = cap;
    r->byte_pos = 0u;
    r->bit_pos = 0u;
    return 0;
}

int lotrs_bitreader_read_bits(lotrs_bitreader_t *r, uint8_t nbits, uint64_t *out)
{
    *out = 0u;
    for (uint8_t i = 0u; i < nbits; ++i) {
        if (r->byte_pos >= r->cap) return -EINVAL;
        uint8_t bit = (r->buf[r->byte_pos] >> r->bit_pos) & 1u;
        *out |= (uint64_t)bit << i;
        r->bit_pos++;
        if (r->bit_pos >= 8u) {
            r->bit_pos = 0u;
            r->byte_pos++;
        }
    }
    return 0;
}

int lotrs_bitreader_read_unary(lotrs_bitreader_t *r, uint64_t max_val, uint64_t *out)
{
    *out = 0u;
    while (*out < max_val) {
        uint64_t bit = 0u;
        int rc = lotrs_bitreader_read_bits(r, 1, &bit);
        if (rc != 0) return rc;
        if (bit == 0u) break;
        (*out)++;
    }
    return 0;
}

size_t lotrs_bitreader_bytes_consumed(const lotrs_bitreader_t *r)
{
    return r->byte_pos + (r->bit_pos > 0u ? 1u : 0u);
}

/* --- Golomb-Rice codec --- */

uint32_t lotrs_optimal_rice_k(double sigma)
{
    if (sigma < 1.0) return 0u;
    double v = floor(log2(1.1774 * sigma));
    return (v < 0.0) ? 0u : (uint32_t)v;
}

int lotrs_rice_pack(uint8_t *a_out, size_t a_out_cap,
                    const int64_t *a_coeffs, uint32_t a_d,
                    uint32_t a_rice_k, int64_t a_bound,
                    size_t *a_bytes_written)
{
    if (!a_out || !a_coeffs || !a_bytes_written) return -EINVAL;

    lotrs_bitwriter_t w;
    int rc = lotrs_bitwriter_init(&w, a_out, a_out_cap);
    if (rc != 0) return rc;

    uint64_t l_low_mask = (a_rice_k == 0u) ? 0u : ((1uLL << a_rice_k) - 1u);

    for (uint32_t i = 0u; i < a_d; ++i) {
        int64_t c = a_coeffs[i];
        uint64_t abs_c = (uint64_t)(c < 0 ? -c : c);
        if ((int64_t)abs_c > a_bound) return -ERANGE;

        uint64_t low = abs_c & l_low_mask;
        uint64_t high = abs_c >> a_rice_k;

        lotrs_bitwriter_write_bits(&w, low, (uint8_t)a_rice_k);
        lotrs_bitwriter_write_unary(&w, high);
        if (abs_c != 0u) {
            lotrs_bitwriter_write_bits(&w, (c < 0) ? 1u : 0u, 1);
        }
    }

    lotrs_bitwriter_pad_to_byte(&w);
    *a_bytes_written = lotrs_bitwriter_bytes_used(&w);
    return 0;
}

int lotrs_rice_unpack(int64_t *a_coeffs, uint32_t a_d,
                      const uint8_t *a_in, size_t a_in_len,
                      uint32_t a_rice_k, int64_t a_bound,
                      size_t *a_bytes_consumed)
{
    if (!a_coeffs || !a_in || !a_bytes_consumed) return -EINVAL;

    lotrs_bitreader_t r;
    int rc = lotrs_bitreader_init(&r, a_in, a_in_len);
    if (rc != 0) return rc;

    uint64_t max_high = (uint64_t)(a_bound >> a_rice_k) + 1u;

    for (uint32_t i = 0u; i < a_d; ++i) {
        uint64_t low = 0u, high = 0u;
        rc = lotrs_bitreader_read_bits(&r, (uint8_t)a_rice_k, &low);
        if (rc != 0) return rc;
        rc = lotrs_bitreader_read_unary(&r, max_high, &high);
        if (rc != 0) return rc;

        uint64_t abs_c = (high << a_rice_k) | low;
        if ((int64_t)abs_c > a_bound) return -ERANGE;

        if (abs_c == 0u) {
            a_coeffs[i] = 0;
        } else {
            uint64_t sign = 0u;
            rc = lotrs_bitreader_read_bits(&r, 1, &sign);
            if (rc != 0) return rc;
            a_coeffs[i] = sign ? -(int64_t)abs_c : (int64_t)abs_c;
        }
    }

    *a_bytes_consumed = lotrs_bitreader_bytes_consumed(&r);
    return 0;
}
