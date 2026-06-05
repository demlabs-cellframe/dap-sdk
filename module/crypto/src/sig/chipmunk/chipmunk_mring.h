/*
 * Chipmunk MRNG — internal protocol header (M1 wire spec lock).
 *
 * CR-11.G Phase 7.7 / task_ac273cea.  Internal symbols ONLY; the public
 * boundary stays in chipmunk_ring.h (see dap_sign_chipmunk_ring.c).
 *
 * Wire layout MRV1 (see README_MRNG.md for the exact byte tables):
 *
 *   ┌─ header (28 B, packed LE) ────────────────────────────────────┐
 *   │  magic 'MRNG'          u32                                    │
 *   │  version               u32                                    │
 *   │  params_id 'MRV1'      u32                                    │
 *   │  N                     u32                                    │
 *   │  t                     u32                                    │
 *   │  fold_depth            u32   // 1 + ceil(log2(N))  (G2 v2)    │
 *   │  flags                 u32   // bit0=linkable (always 1 in v1)│
 *   ├─ fixed hashes (128 B) ────────────────────────────────────────┤
 *   │  ring_hash[32] ctx_hash[32] msg_hash[32] fs_seed[32]          │
 *   ├─ T block (qpack = 1408 B) [G2 v2.1 §1] ──────────────────────┤
 *   │  link tag T = A_T · X ∈ R_q   (single poly, see G2 v2.1)      │
 *   ├─ C_b block (qpack = 1408 B) ──────────────────────────────────┤
 *   │  vector commitment to b ∈ {0,1}^N                             │
 *   ├─ Y_pk block (qpack = 1408 B) [G2 v2.1 §1, §4] ────────────────┤
 *   │  prover-claimed Y_pk = A_pk · X ∈ R_q   (single poly)         │
 *   ├─ fold opening seed (32 B) [G3 §6.1 C1] ───────────────────────┤
 *   │  SHAKE256-derived VCom openings for all fold rounds            │
 *   ├─ fold tree (fold_depth · FOLD_ROUND_BYTES) ───────────────────┤
 *   │  for r in [0, fold_depth):  C_L, C_R   (ext VCom qpack)       │
 *   ├─ final scalars (FINAL_SCALARS_BYTES) ─────────────────────────┤
 *   │  a*, b*  (final folded R_q^{(e)} scalars, 6 qpack each)       │
 *   ├─ leaf mask (LEAF_MASK_BYTES = 3136 B) [G3 §6.1 C2 / M4.3] ────┤
 *   │  ω ∈ R_q, 49-bit pack, ‖ω‖∞ ≤ WC^fold_depth                  │
 *   ├─ bind block (K_PK·zpack = 7680 B) [G2 v2.1 §4] ───────────────┤
 *   │  z_x = ρ_x + c*·X ∈ R_q^{K_pk}   (K_pk zpacks);               │
 *   │  Π_norm not on wire — verifier recomputes (G2 v2 §A6)         │
 *   └───────────────────────────────────────────────────────────────┘
 *
 * Total = 28 + 128 + T_BYTES + CB_BYTES + YPK_BYTES
 *       + fold_depth·FOLD_ROUND_BYTES
 *       + FINAL_SCALARS_BYTES + BIND_BYTES
 *
 * Size table (G2 v2.1 §5) — with K_PK=K_T=6, qpack=1408, zpack=1280,
 * fold_depth = 1 + ceil(log2 N):
 *
 *   N=2   fold_depth=2 →  20 508 B (~20.0 KB)
 *   N=4   fold_depth=3 →  23 324 B (~22.8 KB)
 *   N=16  fold_depth=5 →  28 956 B (~28.3 KB)
 *   N=64  fold_depth=7 →  34 588 B (~33.8 KB)
 *   N=256 fold_depth=9 →  40 220 B (~39.3 KB)
 *
 * 20–28× smaller than CRNG/v1; amendment v2 §5.1 targets (≤36 KB @N=16,
 * ≤48 KB @N=256) met with margin.  Pure structural lower bound for
 * (K_pk = 6, zpack = 1 280): a fixed overhead of 12 060 B per signature
 * irrespective of N (header + hashes + T + C_b + Y_pk + z_x).
 *
 * NOTE: this header pins LAYOUT only.  The cryptographic statement
 * (unified inner-product ⟨b, P(c)⟩ = rhs(c), see amendment v2 §5.2)
 * lands in chipmunk_mring_statement.c / chipmunk_mring_fold.c under
 * G1 + G2 gates.  The M0/M1 stub validates the header and returns
 * appropriate error codes; sign + verify cryptography is M3+.
 */

#pragma once
#ifndef _CHIPMUNK_MRING_H_
#define _CHIPMUNK_MRING_H_

#include <stddef.h>
#include <stdint.h>

#include "chipmunk_ring.h"
#include "chipmunk_mring_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Wire flags (header bit field).
 *
 * Bit 0 (linkable): always 1 in MRV1 — set-link tag T is mandatory.
 * Bits 1..31      : reserved, MUST be 0 on the wire.  Verifier rejects
 *                   with CHIPMUNK_RING_ERR_PARAMS_MISMATCH on any other
 *                   bit set in MRV1.
 * ---------------------------------------------------------------------- */

#define CHIPMUNK_MRING_FLAG_LINKABLE   0x00000001u
#define CHIPMUNK_MRING_FLAGS_DEFAULT   CHIPMUNK_MRING_FLAG_LINKABLE
#define CHIPMUNK_MRING_FLAGS_RESERVED  (~CHIPMUNK_MRING_FLAG_LINKABLE & 0xFFFFFFFFu)

/* -------------------------------------------------------------------------
 * Header (28 bytes, packed little-endian on the wire).
 *
 * The C-struct mirrors the on-wire order; readers and writers go through
 * chipmunk_mring_header_read / _write (little-endian explicit), which
 * are added in M1.4 alongside header validation.
 * ---------------------------------------------------------------------- */

typedef struct chipmunk_mring_header {
    uint32_t magic;       /* 'MRNG' = CHIPMUNK_MRING_MAGIC */
    uint32_t version;     /* CHIPMUNK_MRING_VERSION */
    uint32_t params_id;   /* 'MRV1' = CHIPMUNK_MRING_PARAMS_ID */
    uint32_t n_ring;      /* N ∈ [2, 256] */
    uint32_t threshold;   /* t ∈ [1, N] */
    uint32_t fold_depth;  /* ceil(log2(N)), 1..8 */
    uint32_t flags;       /* CHIPMUNK_MRING_FLAGS_* */
} chipmunk_mring_header_t;

_Static_assert(sizeof(chipmunk_mring_header_t) ==
               CHIPMUNK_MRING_HEADER_BYTES,
               "MRNG: header struct must be exactly 28 bytes");

/* -------------------------------------------------------------------------
 * Computed wire-section offsets.
 *
 * Given a validated header, these helpers return byte offsets into the
 * signature buffer.  All sections are contiguous in the order described
 * in the file-level layout comment.
 * ---------------------------------------------------------------------- */

static inline uint32_t chipmunk_mring_section_off_fixed_hashes(void)
{
    return CHIPMUNK_MRING_HEADER_BYTES;
}

static inline uint32_t chipmunk_mring_section_off_T(void)
{
    return CHIPMUNK_MRING_HEADER_BYTES + CHIPMUNK_MRING_FIXED_HASH_BYTES;
}

static inline uint32_t chipmunk_mring_section_off_cb(void)
{
    return chipmunk_mring_section_off_T() + CHIPMUNK_MRING_T_BYTES;
}

static inline uint32_t chipmunk_mring_section_off_ypk(void)
{
    return chipmunk_mring_section_off_cb() + CHIPMUNK_MRING_CB_BYTES;
}

static inline uint32_t chipmunk_mring_section_off_fold_opening_seed(void)
{
    return chipmunk_mring_section_off_ypk() + CHIPMUNK_MRING_YPK_BYTES;
}

static inline uint32_t chipmunk_mring_section_off_fold(void)
{
    return chipmunk_mring_section_off_fold_opening_seed()
           + CHIPMUNK_MRING_FOLD_OPENING_BYTES;
}

static inline uint32_t chipmunk_mring_section_off_final(uint32_t a_fold_depth)
{
    return chipmunk_mring_section_off_fold()
           + a_fold_depth * CHIPMUNK_MRING_FOLD_ROUND_BYTES;
}

static inline uint32_t chipmunk_mring_section_off_leaf_mask(uint32_t a_fold_depth)
{
    return chipmunk_mring_section_off_final(a_fold_depth)
           + CHIPMUNK_MRING_FINAL_SCALARS_BYTES;
}

static inline uint32_t chipmunk_mring_section_off_bind(uint32_t a_fold_depth)
{
    return chipmunk_mring_section_off_leaf_mask(a_fold_depth)
           + CHIPMUNK_MRING_LEAF_MASK_BYTES;
}

/* Total signature size in bytes for the given (validated) fold_depth. */
static inline uint32_t chipmunk_mring_wire_size(uint32_t a_fold_depth)
{
    return chipmunk_mring_section_off_bind(a_fold_depth)
           + CHIPMUNK_MRING_BIND_BYTES;
}

/* -------------------------------------------------------------------------
 * Header (de)serialisation helpers (M1.4).
 *
 * chipmunk_mring_header_read parses 28 bytes into a struct AND validates
 * all profile-level constants (magic / version / params / N / t /
 * fold_depth consistency / flags).  Returns CHIPMUNK_RING_OK on success
 * or one of:
 *   CHIPMUNK_RING_ERR_NULL_PARAM
 *   CHIPMUNK_RING_ERR_BUFFER_TOO_SMALL
 *   CHIPMUNK_RING_ERR_MAGIC_MISMATCH
 *   CHIPMUNK_RING_ERR_VERSION_MISMATCH
 *   CHIPMUNK_RING_ERR_PARAMS_MISMATCH    (params_id / flags / fold_depth)
 *   CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE
 *   CHIPMUNK_RING_ERR_T_OUT_OF_RANGE
 *
 * chipmunk_mring_header_write serialises a struct to 28 bytes in LE
 * order; it does NOT re-validate (caller must have filled valid fields).
 * ---------------------------------------------------------------------- */

chipmunk_ring_error_t chipmunk_mring_header_read(
    chipmunk_mring_header_t *a_hdr_out,
    const uint8_t *a_buf, size_t a_buf_size);

void chipmunk_mring_header_write(
    uint8_t a_buf[CHIPMUNK_MRING_HEADER_BYTES],
    const chipmunk_mring_header_t *a_hdr);

/*
 * Compute the canonical fold_depth = ceil(log2(N)) for N ∈ [2, 256].
 * Returns 0 if N is out of range.
 */
uint32_t chipmunk_mring_fold_depth_for(uint32_t a_n_ring);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_MRING_H_ */
