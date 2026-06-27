/*
 * LoTRS — Golomb-Rice codec for compact polynomial serialization.
 *
 * Signed coefficients are encoded as:
 *   [low k bits] [unary high part] [sign bit (if nonzero)]
 * Then byte-aligned.
 */

#pragma once
#ifndef _LOTRS_CODEC_H_
#define _LOTRS_CODEC_H_

#include <stdint.h>
#include <stddef.h>

/* Bitstream writer. */
typedef struct lotrs_bitwriter {
    uint8_t *buf;
    size_t   cap;
    size_t   byte_pos;
    uint8_t  bit_pos;   /* 0..7 */
} lotrs_bitwriter_t;

/* Bitstream reader. */
typedef struct lotrs_bitreader {
    const uint8_t *buf;
    size_t         cap;
    size_t         byte_pos;
    uint8_t        bit_pos;   /* 0..7 */
} lotrs_bitreader_t;

/* Writer API. */
int  lotrs_bitwriter_init(lotrs_bitwriter_t *w, uint8_t *buf, size_t cap);
void lotrs_bitwriter_write_bits(lotrs_bitwriter_t *w, uint64_t val, uint8_t nbits);
void lotrs_bitwriter_write_unary(lotrs_bitwriter_t *w, uint64_t val);
void lotrs_bitwriter_pad_to_byte(lotrs_bitwriter_t *w);
size_t lotrs_bitwriter_bytes_used(const lotrs_bitwriter_t *w);

/* Reader API. */
int  lotrs_bitreader_init(lotrs_bitreader_t *r, const uint8_t *buf, size_t cap);
int  lotrs_bitreader_read_bits(lotrs_bitreader_t *r, uint8_t nbits, uint64_t *out);
int  lotrs_bitreader_read_unary(lotrs_bitreader_t *r, uint64_t max_val, uint64_t *out);
size_t lotrs_bitreader_bytes_consumed(const lotrs_bitreader_t *r);

/* Optimal Rice parameter for standard deviation sigma. */
uint32_t lotrs_optimal_rice_k(double sigma);

/* Encode signed coefficients with Golomb-Rice. Returns bytes written. */
int lotrs_rice_pack(uint8_t *a_out, size_t a_out_cap,
                    const int64_t *a_coeffs, uint32_t a_d,
                    uint32_t a_rice_k, int64_t a_bound,
                    size_t *a_bytes_written);

/* Decode signed coefficients. Returns 0 on success. */
int lotrs_rice_unpack(int64_t *a_coeffs, uint32_t a_d,
                      const uint8_t *a_in, size_t a_in_len,
                      uint32_t a_rice_k, int64_t a_bound,
                      size_t *a_bytes_consumed);

#endif /* _LOTRS_CODEC_H_ */
