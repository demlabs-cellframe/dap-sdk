/*
 * chipmunk_fri_transcript.c — Fiat-Shamir transcript for FRI-DEEP PCS.
 *
 * SHAKE256-based transcript: absorb commitments → squeeze challenges.
 * Includes 16-bit grinding PoW for computational soundness.
 */

#define LOG_TAG "chipmunk_fri_transcript"

#include "chipmunk_fri_transcript.h"

#include <string.h>

#include "dap_common.h"
#include "dap_hash_shake256.h"
#include "chipmunk_field.h"

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Count leading zero bits in a byte. */
static inline unsigned s_clz_byte(uint8_t b)
{
    if (b == 0) return 8u;
    unsigned n = 0;
    if (b < 0x10) { n += 4; b <<= 4; }
    if (b < 0x40) { n += 2; b <<= 2; }
    if (b < 0x80) { n += 1; }
    return n;
}

/* Count leading zero bits across a byte array. */
static unsigned s_clz_bytes(const uint8_t *data, size_t len)
{
    unsigned total = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned lz = s_clz_byte(data[i]);
        total += lz;
        if (lz < 8u)
            break;
    }
    return total;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

int chipmunk_fri_transcript_init(chipmunk_fri_transcript_t *tr,
                                  const uint8_t domain[16])
{
    if (!tr || !domain)
        return -1;

    memset(tr, 0, sizeof(*tr));

    /* Absorb domain separator into buffer. */
    memcpy(tr->buffer, domain, 16);
    tr->buf_len = 16;
    tr->initialized = true;
    return 0;
}

int chipmunk_fri_transcript_absorb(chipmunk_fri_transcript_t *tr,
                                    const uint8_t *data, size_t len)
{
    if (!tr || !tr->initialized || tr->grinding_done)
        return -1;
    if (!data && len > 0)
        return -1;
    if (tr->buf_len + len > CHIPMUNK_FRI_TRANSCRIPT_BUF)
        return -1;

    if (len > 0) {
        memcpy(tr->buffer + tr->buf_len, data, len);
        tr->buf_len += len;
    }
    return 0;
}

int chipmunk_fri_transcript_absorb_fq(chipmunk_fri_transcript_t *tr,
                                       int32_t val)
{
    if (val < 0 || val >= (int32_t)CHIPMUNK_Q)
        return -1;

    uint8_t bytes[4];
    bytes[0] = (uint8_t)(val & 0xFF);
    bytes[1] = (uint8_t)((val >> 8) & 0xFF);
    bytes[2] = (uint8_t)((val >> 16) & 0xFF);
    bytes[3] = (uint8_t)((val >> 24) & 0xFF);
    return chipmunk_fri_transcript_absorb(tr, bytes, 4);
}

int chipmunk_fri_transcript_absorb_cap(chipmunk_fri_transcript_t *tr,
                                        const int32_t *cap, uint32_t size)
{
    if (!tr || !cap || size == 0 || size > 16u)
        return -1;

    /* Absorb each cap node as 4 bytes LE. */
    for (uint32_t i = 0; i < size; ++i) {
        int rc = chipmunk_fri_transcript_absorb_fq(tr, cap[i]);
        if (rc < 0)
            return rc;
    }
    return 0;
}

int chipmunk_fri_transcript_squeeze_fq(chipmunk_fri_transcript_t *tr,
                                        int32_t *out)
{
    if (!tr || !tr->initialized || !out)
        return -1;

    /* Counter-based one-shot SHAKE256: hash(buffer || counter) → 32 bytes.
     * Buffer already includes domain + absorbed data + nonce (finalize appends).
     * Rejection-sample the first 4 bytes until value ∈ [0, q).
     * With q = 3168257 < 2^22, acceptance prob ≈ 73.8%.
     * On rejection, increment counter and retry. */
    if (tr->buf_len + 4 > CHIPMUNK_FRI_TRANSCRIPT_BUF + 4)
        return -1;

    uint8_t work[CHIPMUNK_FRI_TRANSCRIPT_BUF + 4];
    memcpy(work, tr->buffer, tr->buf_len);

    for (int attempt = 0; attempt < 200; ++attempt) {
        /* Append squeeze counter as 4 bytes LE. */
        work[tr->buf_len + 0] = (uint8_t)(tr->squeeze_counter & 0xFF);
        work[tr->buf_len + 1] = (uint8_t)((tr->squeeze_counter >> 8) & 0xFF);
        work[tr->buf_len + 2] = (uint8_t)((tr->squeeze_counter >> 16) & 0xFF);
        work[tr->buf_len + 3] = (uint8_t)((tr->squeeze_counter >> 24) & 0xFF);

        uint8_t digest[32];
        dap_hash_shake256(digest, sizeof(digest), work, tr->buf_len + 4);

        tr->squeeze_counter++;

        /* Rejection-sample 3 bytes → [0, q).
         * q = 3168257 < 2^22, so 3 bytes (24 bits) suffice.
         * Acceptance probability: q / 2^24 ≈ 18.9%. */
        uint32_t val = (uint32_t)digest[0]
                     | ((uint32_t)digest[1] << 8)
                     | ((uint32_t)digest[2] << 16);
        if (val < (uint32_t)CHIPMUNK_Q) {
            *out = (int32_t)val;
            return 0;
        }
    }
    log_it(L_ERROR, "FRI transcript: squeeze_fq failed after 200 attempts");
    return -1;
}

int chipmunk_fri_transcript_squeeze_fq_many(chipmunk_fri_transcript_t *tr,
                                             int32_t out[], uint32_t count)
{
    if (!tr || !out || count == 0)
        return -1;

    for (uint32_t i = 0; i < count; ++i) {
        int rc = chipmunk_fri_transcript_squeeze_fq(tr, &out[i]);
        if (rc < 0)
            return rc;
    }
    return 0;
}

int chipmunk_fri_transcript_grind(chipmunk_fri_transcript_t *tr,
                                   uint32_t *nonce)
{
    if (!tr || !tr->initialized || tr->grinding_done || !nonce)
        return -1;

    /* Expected work: 2^16 iterations. Cap at 2^24 for safety. */
    const uint32_t max_attempts = (1u << 24);

    /* Pre-allocate outside loop. */
    uint8_t input[CHIPMUNK_FRI_TRANSCRIPT_BUF + 4];
    size_t input_len = tr->buf_len;
    memcpy(input, tr->buffer, input_len);
    uint8_t digest[DAP_KECCAK_SHAKE256_RATE];

    for (uint32_t n = 0; n < max_attempts; ++n) {
        /* Append nonce to input. */
        input[input_len + 0] = (uint8_t)(n & 0xFF);
        input[input_len + 1] = (uint8_t)((n >> 8) & 0xFF);
        input[input_len + 2] = (uint8_t)((n >> 16) & 0xFF);
        input[input_len + 3] = (uint8_t)((n >> 24) & 0xFF);

        /* One-shot SHAKE256 → check leading zeros. */
        dap_hash_shake256(digest, sizeof(digest), input, input_len + 4);

        if (s_clz_bytes(digest, 2u) >= CHIPMUNK_FRI_GRINDING_BITS) {
            tr->grinding_nonce = n;
            tr->grinding_done = true;
            *nonce = n;
            return 0;
        }
    }

    log_it(L_ERROR, "FRI transcript: grinding failed after %u attempts",
           max_attempts);
    return -1;
}

bool chipmunk_fri_transcript_verify_grinding(const chipmunk_fri_transcript_t *tr,
                                              uint32_t nonce)
{
    if (!tr || !tr->initialized || tr->buf_len == 0)
        return false;

    uint8_t input[CHIPMUNK_FRI_TRANSCRIPT_BUF + 4];
    memcpy(input, tr->buffer, tr->buf_len);
    input[tr->buf_len + 0] = (uint8_t)(nonce & 0xFF);
    input[tr->buf_len + 1] = (uint8_t)((nonce >> 8) & 0xFF);
    input[tr->buf_len + 2] = (uint8_t)((nonce >> 16) & 0xFF);
    input[tr->buf_len + 3] = (uint8_t)((nonce >> 24) & 0xFF);

    uint8_t digest[DAP_KECCAK_SHAKE256_RATE];
    dap_hash_shake256(digest, sizeof(digest), input, tr->buf_len + 4);

    return s_clz_bytes(digest, 2u) >= CHIPMUNK_FRI_GRINDING_BITS;
}

int chipmunk_fri_transcript_finalize(chipmunk_fri_transcript_t *tr)
{
    if (!tr || !tr->initialized)
        return -1;

    /* Perform grinding PoW. */
    int rc = chipmunk_fri_transcript_grind(tr, &tr->grinding_nonce);
    if (rc < 0)
        return rc;

    /* Append nonce to buffer so squeeze can use buffer || counter. */
    if (tr->buf_len + 4 > CHIPMUNK_FRI_TRANSCRIPT_BUF)
        return -1;

    uint8_t nonce_bytes[4];
    nonce_bytes[0] = (uint8_t)(tr->grinding_nonce & 0xFF);
    nonce_bytes[1] = (uint8_t)((tr->grinding_nonce >> 8) & 0xFF);
    nonce_bytes[2] = (uint8_t)((tr->grinding_nonce >> 16) & 0xFF);
    nonce_bytes[3] = (uint8_t)((tr->grinding_nonce >> 24) & 0xFF);
    memcpy(tr->buffer + tr->buf_len, nonce_bytes, 4);
    tr->buf_len += 4;

    /* Initialize squeeze counter. */
    tr->squeeze_counter = 0;

    return 0;
}

int chipmunk_fri_transcript_finalize_verify(chipmunk_fri_transcript_t *tr,
                                             uint32_t nonce)
{
    if (!tr || !tr->initialized)
        return -1;
    if (tr->grinding_done)
        return -1;  /* already finalized */

    /* Verify grinding nonce (single hash check). */
    if (!chipmunk_fri_transcript_verify_grinding(tr, nonce))
        return -1;

    tr->grinding_nonce = nonce;
    tr->grinding_done = true;

    /* Append nonce to buffer so squeeze can use buffer || counter. */
    if (tr->buf_len + 4 > CHIPMUNK_FRI_TRANSCRIPT_BUF)
        return -1;

    uint8_t nonce_bytes[4];
    nonce_bytes[0] = (uint8_t)(nonce & 0xFF);
    nonce_bytes[1] = (uint8_t)((nonce >> 8) & 0xFF);
    nonce_bytes[2] = (uint8_t)((nonce >> 16) & 0xFF);
    nonce_bytes[3] = (uint8_t)((nonce >> 24) & 0xFF);
    memcpy(tr->buffer + tr->buf_len, nonce_bytes, 4);
    tr->buf_len += 4;

    /* Initialize squeeze counter. */
    tr->squeeze_counter = 0;

    return 0;
}

int chipmunk_fri_transcript_clone(chipmunk_fri_transcript_t *dst,
                                  const chipmunk_fri_transcript_t *src)
{
    if (!dst || !src)
        return -1;

    memcpy(dst, src, sizeof(chipmunk_fri_transcript_t));
    return 0;
}
