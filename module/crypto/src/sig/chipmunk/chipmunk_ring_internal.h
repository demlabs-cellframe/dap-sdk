/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 *
 * ChipmunkRing internal helpers shared between chipmunk_ring.c and
 * chipmunk_ring_acorn.c.  Not part of the public API surface — do NOT
 * expose the prototypes outside the ChipmunkRing implementation units.
 *
 * CR-D31 (Round-4): the previous static `s_domain_hash` was duplicated in
 * both .c files and concatenated `domain || salt || input` without any
 * length prefixes (with `strlen(domain)` as the domain width).  The
 * construction was therefore a TupleHash-style prefix-collision target
 * once any of the three components became attacker-controllable.  The
 * single source of truth below replaces both copies with a length-
 * prefixed encoding (each component is preceded by its little-endian
 * uint32 length), bumps the canonical "v2" suffix into the domain
 * literals, and adds an explicit invariant check that the domain string
 * does not contain an embedded NUL.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal helper for domain-separated SHA3-256 hashing with
 *        TupleHash-style length prefixing (CR-D31).
 *
 * Builds the PRK as
 *     SHA3-256( LE32(len(D)) || D ||
 *               LE32(len(S)) || S ||
 *               LE32(len(I)) || I )
 * then applies optional iterative key-stretching, then expands to the
 * requested output length using a SHA3-256 counter mode (HKDF-Expand
 * style: `T(i) = SHA3-256(PRK || T(i-1) || counter_i)`).
 *
 * The length prefixes guarantee that no choice of (D, S, I) tuples with
 * different component splits can collide, even when an adversary
 * controls one or more of the components.  The helper enforces the
 * canonical `/v2` suffix itself; older `/v1` literals are rejected before
 * hashing to prevent silent format drift.
 *
 * @param  a_domain      NUL-terminated domain string (must NOT contain
 *                       embedded NULs; `strnlen` is used defensively).
 * @param  a_salt        Optional salt buffer; may be NULL when
 *                       @p a_salt_size == 0.
 * @param  a_salt_size   Salt length in bytes (0..UINT32_MAX).
 * @param  a_input       Input buffer; MUST be non-NULL.
 * @param  a_input_size  Input length in bytes (1..UINT32_MAX).
 * @param  a_output      Output buffer of at least @p a_output_size bytes.
 * @param  a_output_size Output length in bytes (1..UINT32_MAX).
 * @param  a_iterations  Iterative-hash key-stretching count (0 ≡ 1).
 *
 * @return 0 on success; -1 on invalid input or hash failure;
 *         -ENOMEM on allocation failure;
 *         -EINVAL on length-prefix overflow (any component > UINT32_MAX
 *         or domain contains an embedded NUL).
 */
int chipmunk_ring_domain_hash_internal(const char *a_domain,
                                       const void *a_salt, size_t a_salt_size,
                                       const void *a_input, size_t a_input_size,
                                       void *a_output, size_t a_output_size,
                                       uint32_t a_iterations);

#ifdef __cplusplus
}
#endif
