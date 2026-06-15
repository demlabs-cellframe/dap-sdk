/*
 * Chipmunk MRNG — log-N compressed threshold ring signature (public bridge).
 *
 * CR-11.G Phase 7.7 / M0: this header is the integration boundary between
 * the generic dap_sign / dap_enc layer and the new MRNG ("MatRiCT+-inspired
 * Chipmunk-native") ring signature.  All prior CRNG/v1 (CLTS-Plus) code has
 * been removed in M0 along with its scaffolds (CLTP/CLTS).
 *
 * IMPLEMENTATION STATUS: M6 sign/verify wired (task_ac273cea).
 * Bind block carries z_x and c* qpack for verifier FS closure (M6).
 *
 * Design references:
 *   MRNG design lock v1                : documentation_c36c57f25e91f318
 *   MRNG design self-review            : documentation_05b11e509b63f097
 *   MRNG amendment v2 (Chipmunk-native): documentation_0539a9f3f1b8ae5d
 */

#pragma once
#ifndef _CHIPMUNK_RING_H_
#define _CHIPMUNK_RING_H_

#include <stddef.h>
#include <stdint.h>

#include "chipmunk.h"
#include "chipmunk_lrs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Wire magic / version / parameter profile id (M1 will pin layout).
 *
 *   'MRNG' = 0x474e524du  (little-endian on the wire, see chipmunk_mring.h)
 *   v1     = 1
 *   'MRV1' = 0x31565252u  (params profile id, MatRiCT+-inspired v1)
 *
 * All earlier magics (CRNG, CLTP, CLTS) are intentionally rejected.
 * ---------------------------------------------------------------------- */

#define CHIPMUNK_RING_MAGIC_MRNG     0x474e524du
#define CHIPMUNK_RING_VERSION        1u
#define CHIPMUNK_RING_PARAMS_MRNG_V1 0x31565252u

/* -------------------------------------------------------------------------
 * Ring / threshold envelope.
 *
 * MRNG targets logarithmic scaling in N up to 256; the integration layer
 * (dap_sign_chipmunk_ring.c) still uses these as soft caps until M1 pins
 * the wire-level limits.
 * ---------------------------------------------------------------------- */

#define CHIPMUNK_RING_NRING_MIN          2u
#define CHIPMUNK_RING_NRING_MAX          256u
#define CHIPMUNK_RING_THRESHOLD_MIN      1u
#define CHIPMUNK_RING_THRESHOLD_MAX      CHIPMUNK_RING_NRING_MAX

#define CHIPMUNK_RING_RING_MIN           CHIPMUNK_RING_NRING_MIN
#define CHIPMUNK_RING_RING_MAX           CHIPMUNK_RING_NRING_MAX
#define CHIPMUNK_RING_RING_MINIMUM_ANON  4u

/* -------------------------------------------------------------------------
 * Consolidated error code enumeration.
 *
 * Codes < -100 are reserved for protocol-internal subsystems; integration
 * layers should only branch on the public values below + the explicit
 * NOT_IMPLEMENTED sentinel returned by the M0 stub.
 * ---------------------------------------------------------------------- */

typedef enum chipmunk_ring_error {
    CHIPMUNK_RING_OK                       =    0,
    CHIPMUNK_RING_ERR_NULL_PARAM           =   -1,
    CHIPMUNK_RING_ERR_BUFFER_TOO_SMALL     =   -2,
    CHIPMUNK_RING_ERR_MAGIC_MISMATCH       =   -3,
    CHIPMUNK_RING_ERR_VERSION_MISMATCH     =   -4,
    CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE  =   -5,
    CHIPMUNK_RING_ERR_T_OUT_OF_RANGE       =   -6,
    CHIPMUNK_RING_ERR_RING_HASH_MISMATCH   =   -7,
    CHIPMUNK_RING_ERR_CTX_HASH_MISMATCH    =   -8,
    CHIPMUNK_RING_ERR_TAG_ORDER            =   -9,
    CHIPMUNK_RING_ERR_TAG_DUPLICATE        =  -10,
    CHIPMUNK_RING_ERR_NORM_BOUND           =  -11,
    CHIPMUNK_RING_ERR_PROOF_FAIL           =  -12,
    CHIPMUNK_RING_ERR_FIAT_SHAMIR_MISMATCH =  -13,
    CHIPMUNK_RING_ERR_PARAMS_MISMATCH      =  -14,
    CHIPMUNK_RING_ERR_RING_PK_DUPLICATE    =  -15,
    CHIPMUNK_RING_ERR_RING_NOT_CANONICAL   =  -16,
    CHIPMUNK_RING_ERR_NOT_IMPLEMENTED      =  -99,
    CHIPMUNK_RING_ERR_INTERNAL             = -100
} chipmunk_ring_error_t;

/* Static human-readable translation; never returns NULL. */
const char *chipmunk_ring_strerror(chipmunk_ring_error_t a_err);

/* -------------------------------------------------------------------------
 * Public wire API — MRNG stub in M0; real implementation lands in M3+.
 *
 * The dap_sign shim only requires these two entry points; everything else
 * (NTT-batched verify, audit hooks, etc.) lives below the public surface
 * inside chipmunk_mring_* compilation units.
 * ---------------------------------------------------------------------- */

struct chipmunk_lrs_secret_key;
struct chipmunk_lrs_public_key;

/*
 * Sign + serialise.
 *
 * On success *a_out_buf is allocated via DAP_NEW and *a_out_size is set
 * to its length; caller wipes & frees with dap_memwipe + DAP_DELETE.
 *
 * Current stub: always returns CHIPMUNK_RING_ERR_NOT_IMPLEMENTED, leaves
 * (*a_out_buf, *a_out_size) = (NULL, 0).
 */
chipmunk_ring_error_t chipmunk_ring_sign_to_bytes(
    uint8_t **a_out_buf, size_t *a_out_size,
    const struct chipmunk_lrs_secret_key *const *a_signer_sk,
    size_t a_signer_count,
    const struct chipmunk_lrs_public_key *a_ring,
    size_t a_ring_size,
    uint32_t a_threshold,
    const uint8_t *a_message, size_t a_message_size,
    const void *a_ctx, size_t a_ctx_size,
    const uint8_t *a_randomness_seeds);

/*
 * Deserialise + verify.  Stub returns CHIPMUNK_RING_ERR_NOT_IMPLEMENTED.
 */
chipmunk_ring_error_t chipmunk_ring_verify_from_bytes(
    const uint8_t *a_buf, size_t a_buf_size,
    const struct chipmunk_lrs_public_key *a_ring,
    size_t a_ring_size,
    const uint8_t *a_message, size_t a_message_size,
    const void *a_ctx, size_t a_ctx_size);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_RING_H_ */
