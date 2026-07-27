/*
 * LoTRS — wire format definition.
 *
 * Wire layout:
 *   ┌─ header (28 B) ────────────────────────────────────────────────────┐
 *   │  magic 'LTRS'          u32    0x4C545253                          │
 *   │  version               u32    1                                    │
 *   │  params_id             u32    'LTS1' = 0x4C545331                  │
 *   │  d (ring dimension)    u32    32 or 128                            │
 *   │  N (ring size)         u32    beta^kappa                           │
 *   │  T (threshold)         u32    1..N                                 │
 *   │  flags                 u32    reserved, must be 0                  │
 *   ├─ w block (k · d · 8 B) ───────────────────────────────────────────┤
 *   │  w[0..k-1] — k polynomials, d uint64 coefficients each (LE)       │
 *   ├─ c block (d · 8 B) ───────────────────────────────────────────────┤
 *   │  c — challenge polynomial, d uint64 coefficients (LE)             │
 *   ├─ z block ((l+k) · d · 8 B) ──────────────────────────────────────┤
 *   │  z[0..l+k-1] — response polynomials                              │
 *   └───────────────────────────────────────────────────────────────────┘
 *
 * Total = 28 + k·d·8 + d·8 + (l+k)·d·8 = 28 + (2k+l)·d·8 bytes
 *   TEST:  28 + (2·2+2)·32·8  = 28 + 1536 = 1564 B
 *   BENCH: 28 + (2·12+5)·128·8 = 28 + 29440 = 29468 B
 */

#pragma once
#ifndef _LOTRS_WIRE_H_
#define _LOTRS_WIRE_H_

#include <stdint.h>
#include <stddef.h>

#include "lotrs_params.h"
#include "lotrs_ring.h"  /* lotrs_poly_bytes() for compact coefficient encoding */

#define LOTRS_WIRE_MAGIC     0x4C545253u  /* 'LTRS' LE */
#define LOTRS_WIRE_VERSION   1u
#define LOTRS_WIRE_PARAMS_ID 0x4C545331u  /* 'LTS1' LE */
#define LOTRS_WIRE_HEADER_BYTES 28u

#define LOTRS_WIRE_FLAG_NONE 0u

/* Header struct (packed LE on wire). */
typedef struct lotrs_wire_header {
    uint32_t magic;
    uint32_t version;
    uint32_t params_id;
    uint32_t d;
    uint32_t N;
    uint32_t T;
    uint32_t flags;
} lotrs_wire_header_t;

/* Wire size for given parameters. Uses compact coefficient encoding
 * (ceil(ceil(log2(q))/8) bytes per coefficient, not always 8). */
static inline uint32_t lotrs_wire_size(const lotrs_params_t *a_par)
{
    uint32_t l_k = a_par->k;
    uint32_t l_l = a_par->l;
    uint32_t l_poly_bytes = (uint32_t)lotrs_poly_bytes(a_par);
    return LOTRS_WIRE_HEADER_BYTES
         + l_k * l_poly_bytes               /* w block */
         + l_poly_bytes                      /* c block */
         + (l_l + l_k) * l_poly_bytes;       /* z block */
}

/* Serialize/deserialize header. */
int lotrs_wire_header_pack(uint8_t *a_buf, size_t a_buf_len,
                           const lotrs_wire_header_t *a_hdr);
int lotrs_wire_header_unpack(lotrs_wire_header_t *a_hdr,
                             const uint8_t *a_buf, size_t a_buf_len);

/* dap_sign bridge registration. */
int dap_sign_lotrs_register_callbacks(void);

#endif /* _LOTRS_WIRE_H_ */
