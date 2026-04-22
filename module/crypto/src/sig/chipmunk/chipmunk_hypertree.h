/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 *
 * Chipmunk Hypertree (CR-D15.B) — stateful post-quantum signature that
 * materialises a Merkle tree over 2^(HEIGHT-1) HOTS public keys at keygen
 * time, lets the holder sign up to that many messages by walking the
 * leaves sequentially, and binds every signature to its leaf through an
 * authentication path that the verifier checks against the public root.
 *
 * Each single-shot Chipmunk HOTS signature is existentially forgeable on
 * the SECOND signing under the same key; CR-D3 closed that hole by
 * refusing to sign a second time under the plain chipmunk_sign.  This
 * hypertree wrapper lifts the practical capacity from 1 to
 * CHIPMUNK_HT_MAX_SIGNATURES (=64 at HEIGHT=7) while retaining strict
 * one-time reuse per leaf — EXACTLY the classical "chipmunk hypertree"
 * construction from the paper.
 *
 * Threat model notes (CR-D15 remediation):
 *   - The leaf derivation uses the same domain-separated SHA3-256 chain
 *     as chipmunk_sign (CR-D3), so each leaf owns an independent (s0, s1)
 *     pair and cannot leak secrets to any other leaf.
 *   - The HVC Merkle tree is built with the Round-5 honest Ajtai hash
 *     (CR-D15.A); leaves are pinned in chipmunk_path_verify so a
 *     tampered leaf_pk no longer trivially verifies.
 *   - The runtime private key materialises the full tree in memory.
 *     Callers MUST free it via chipmunk_ht_private_key_clear before
 *     releasing the buffer, otherwise 2^HEIGHT leaf pks plus the HVC
 *     spine stay on the heap.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>

#include "chipmunk.h"
#include "chipmunk_hots.h"
#include "chipmunk_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------- *
 *  Parameters                                                            *
 * ---------------------------------------------------------------------- */

/*
 * HEIGHT=7 gives 2^(7-1) = 64 usable signing slots per keypair.  The
 * figure balances:
 *    - keygen cost  ~ 2^(HEIGHT-1) HOTS-keygens + tree build (≈1–2 s),
 *    - sig size     ~ 12 KiB sigma + 4 KiB leaf pk + (H-1)·4 KiB auth path,
 *    - capacity      enough for typical consensus / session usage.
 *
 * Chipmunk's underlying chipmunk_tree enforces HEIGHT ∈ [5, 16]; this
 * value sits comfortably in range and can be bumped later once the tree
 * build is parallelised (CR-D15.C follow-up).
 */
#define CHIPMUNK_HT_HEIGHT            7u
#define CHIPMUNK_HT_LEAF_COUNT        (1u << (CHIPMUNK_HT_HEIGHT - 1u))      /* 64 */
#define CHIPMUNK_HT_MAX_SIGNATURES    CHIPMUNK_HT_LEAF_COUNT

/*
 * Serialised public key = rho_seed(32) || hasher_seed(32) || root(CHIPMUNK_N·4).
 * The A-matrix is re-sampled from rho_seed; the HVC hasher from hasher_seed;
 * the root anchors every authentication path.
 */
#define CHIPMUNK_HT_PUBLIC_KEY_SIZE   (32u + 32u + (uint32_t)(CHIPMUNK_N) * 4u)

/*
 * Serialised signature = sigma[GAMMA] || leaf_index(4) || leaf_pk_v0 ||
 * leaf_pk_v1 || path[H-1] where every path node is (left || right),
 * both HVC polys (CHIPMUNK_N · 4 bytes each).
 */
#define CHIPMUNK_HT_SIGNATURE_SIZE                                             \
    ( (uint32_t)(CHIPMUNK_N) * 4u * (uint32_t)(CHIPMUNK_GAMMA)                 \
    + 4u                                                                       \
    + (uint32_t)(CHIPMUNK_N) * 4u * 2u                                         \
    + ((uint32_t)(CHIPMUNK_HT_HEIGHT) - 1u) * 2u * (uint32_t)(CHIPMUNK_N) * 4u )

/*
 * Serialised private key = leaf_index(4) || key_seed(32) || tr(48) ||
 * pk_bytes(CHIPMUNK_HT_PUBLIC_KEY_SIZE).  The tree itself is *NOT*
 * serialised — it is deterministically rebuilt from key_seed+rho_seed+
 * hasher_seed on load by chipmunk_ht_private_key_from_bytes.
 */
#define CHIPMUNK_HT_PRIVATE_KEY_SIZE                                           \
    (4u + 32u + 48u + CHIPMUNK_HT_PUBLIC_KEY_SIZE)

/* ---------------------------------------------------------------------- *
 *  Types                                                                 *
 * ---------------------------------------------------------------------- */

/*
 * Hypertree public key.  Size = CHIPMUNK_HT_PUBLIC_KEY_SIZE bytes once
 * serialised through chipmunk_ht_public_key_to_bytes.
 */
typedef struct chipmunk_ht_public_key {
    uint8_t              rho_seed[32];     ///< Seed for HOTS A-matrix (shared across leaves)
    uint8_t              hasher_seed[32];  ///< Seed for HVC Ajtai hasher
    chipmunk_hvc_poly_t  root;             ///< Merkle root over 2^(H-1) leaf digests
} chipmunk_ht_public_key_t;

/*
 * Hypertree private key.  Carries the full materialised tree in memory.
 * The struct OWNS heap resources (tree, leaf_pks) and a pthread_mutex —
 * it MUST be initialised via chipmunk_ht_keypair / chipmunk_ht_keypair_from_seed
 * and released via chipmunk_ht_private_key_clear.
 *
 * Thread-safety: chipmunk_ht_sign takes the mutex before check-and-
 * increment of leaf_index, so concurrent callers cannot race on the same
 * sk (which would trivially leak the HOTS secret for the doubly-used leaf).
 */
typedef struct chipmunk_ht_private_key {
    uint32_t                 leaf_index;      ///< Next unused leaf slot
    uint8_t                  key_seed[32];    ///< Master HOTS seed (CR-D3 derivation root)
    uint8_t                  tr[48];          ///< SHA3-384 of the public key bytes (domain binder)
    chipmunk_ht_public_key_t pk;              ///< Echoed copy of the public key

    /* --- Cached materialisations (NOT serialised) -------------------- */
    chipmunk_hots_params_t   hots_params;     ///< A[i] in NTT form, derived from pk.rho_seed
    chipmunk_hvc_hasher_t    hasher;          ///< HVC hasher built from pk.hasher_seed
    chipmunk_tree_t          tree;            ///< Merkle tree over leaf digests
    chipmunk_hots_pk_t      *leaf_pks;        ///< CHIPMUNK_HT_LEAF_COUNT HOTS pks (ownership: struct)

    pthread_mutex_t          mutex;           ///< Serialises leaf_index progression
    bool                     materialised;    ///< Internal flag (hots_params/hasher/tree ready)
    bool                     mutex_inited;    ///< Internal flag (mutex initialised)
} chipmunk_ht_private_key_t;

/*
 * Hypertree signature.  The `auth_path` field owns a heap-allocated
 * `nodes` array — callers MUST release via chipmunk_ht_signature_clear.
 */
typedef struct chipmunk_ht_signature {
    chipmunk_signature_t hots_sig;      ///< HOTS sigma[GAMMA]
    uint32_t             leaf_index;    ///< Leaf the signature was produced at
    chipmunk_hots_pk_t   leaf_pk;       ///< HOTS public key at that leaf
    chipmunk_path_t      auth_path;     ///< Path from leaf digest to root (owns nodes[])
} chipmunk_ht_signature_t;

/* ---------------------------------------------------------------------- *
 *  Keygen / sign / verify                                                *
 * ---------------------------------------------------------------------- */

/**
 * @brief Generate a fresh hypertree keypair from OS RNG.
 *
 * Allocates and materialises the in-memory tree.  On failure all partial
 * resources are released; the caller MUST call
 * chipmunk_ht_private_key_clear on success to free the owned resources.
 */
int chipmunk_ht_keypair(chipmunk_ht_public_key_t  *a_pk_out,
                         chipmunk_ht_private_key_t *a_sk_out);

/**
 * @brief Generate a hypertree keypair deterministically from 32-byte seed.
 *
 * Same semantics as chipmunk_ht_keypair but derives key_seed from a
 * caller-supplied seed under a fixed domain separator.  Useful for test
 * vectors and persistent key rederivation.
 */
int chipmunk_ht_keypair_from_seed(const uint8_t a_seed[32],
                                   chipmunk_ht_public_key_t  *a_pk_out,
                                   chipmunk_ht_private_key_t *a_sk_out);

/**
 * @brief Sign a message at the next free leaf.
 *
 * Advances sk->leaf_index atomically.  Returns CHIPMUNK_ERROR_KEY_EXHAUSTED
 * once all CHIPMUNK_HT_MAX_SIGNATURES slots are used — the caller MUST
 * rotate keys at that point.
 *
 * @param[in,out] a_sk   Private key (leaf_index mutated on success).
 * @param[in]     a_msg  Message to sign (≤ 10 MiB).
 * @param[in]     a_len  Message length.
 * @param[out]    a_sig  Hypertree signature.  The function initialises
 *                       a_sig->auth_path.nodes via DAP_NEW_*; caller MUST
 *                       chipmunk_ht_signature_clear after use.
 */
int chipmunk_ht_sign(chipmunk_ht_private_key_t *a_sk,
                      const uint8_t *a_msg, size_t a_len,
                      chipmunk_ht_signature_t *a_sig);

/**
 * @brief Verify a hypertree signature.
 *
 * Steps (every one MUST succeed):
 *   1. leaf_index < CHIPMUNK_HT_LEAF_COUNT,
 *   2. HOTS-verify (sigma, msg) against leaf_pk under the A-matrix
 *      derived from pk.rho_seed,
 *   3. chipmunk_hots_pk_to_hvc_poly(leaf_pk) hashes to the expected
 *      Merkle leaf digest,
 *   4. chipmunk_path_verify(auth_path, leaf_digest, pk.root, hasher).
 */
int chipmunk_ht_verify(const chipmunk_ht_public_key_t *a_pk,
                        const uint8_t *a_msg, size_t a_len,
                        const chipmunk_ht_signature_t *a_sig);

/* ---------------------------------------------------------------------- *
 *  Serialisation                                                         *
 * ---------------------------------------------------------------------- */

int chipmunk_ht_public_key_to_bytes(uint8_t  a_out[CHIPMUNK_HT_PUBLIC_KEY_SIZE],
                                     const chipmunk_ht_public_key_t *a_pk);

int chipmunk_ht_public_key_from_bytes(chipmunk_ht_public_key_t *a_pk,
                                       const uint8_t a_in[CHIPMUNK_HT_PUBLIC_KEY_SIZE]);

/*
 * Serialise the private-key header (leaf_index, key_seed, tr, pk) into
 * CHIPMUNK_HT_PRIVATE_KEY_SIZE bytes.  The in-memory tree is NOT part of
 * the serialised blob — it is rebuilt deterministically by
 * chipmunk_ht_private_key_from_bytes.
 */
int chipmunk_ht_private_key_to_bytes(uint8_t a_out[CHIPMUNK_HT_PRIVATE_KEY_SIZE],
                                      const chipmunk_ht_private_key_t *a_sk);

/*
 * Deserialise the header and rebuild the full tree.  `a_sk_out` MUST be
 * an uninitialised zero-filled struct — any prior allocations are NOT
 * freed to keep the semantics memmove-friendly.  Returns
 * CHIPMUNK_ERROR_VERIFY_FAILED if the rebuilt root does not match the
 * one stored in the serialised public key (integrity self-check).
 */
int chipmunk_ht_private_key_from_bytes(chipmunk_ht_private_key_t *a_sk_out,
                                        const uint8_t a_in[CHIPMUNK_HT_PRIVATE_KEY_SIZE]);

int chipmunk_ht_signature_to_bytes(uint8_t a_out[CHIPMUNK_HT_SIGNATURE_SIZE],
                                    const chipmunk_ht_signature_t *a_sig);

/*
 * Deserialise a signature.  Allocates a_sig->auth_path.nodes — caller
 * must release via chipmunk_ht_signature_clear.
 */
int chipmunk_ht_signature_from_bytes(chipmunk_ht_signature_t *a_sig,
                                      const uint8_t a_in[CHIPMUNK_HT_SIGNATURE_SIZE]);

/* ---------------------------------------------------------------------- *
 *  Resource release                                                      *
 * ---------------------------------------------------------------------- */

/**
 * @brief Release all heap resources and wipe sensitive material.
 *
 * Safe to call on a zero-initialised struct (no-op).  Safe to call
 * multiple times on the same struct (subsequent calls no-op).
 */
void chipmunk_ht_private_key_clear(chipmunk_ht_private_key_t *a_sk);

/**
 * @brief Release the auth_path.nodes array in a signature.
 *
 * Safe on zero-initialised / already-cleared signatures.
 */
void chipmunk_ht_signature_clear(chipmunk_ht_signature_t *a_sig);

#ifdef __cplusplus
}
#endif
