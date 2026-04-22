/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 *
 * CR-D15.B: Chipmunk Hypertree — stateful many-time signature built on
 * top of the one-time HOTS scheme via an HVC Merkle tree over
 * 2^(CHIPMUNK_HT_HEIGHT - 1) leaf HOTS public keys.
 *
 * All cryptographic primitives are reused from the single-shot pipeline
 * so the hypertree inherits every Round-3/4/5 remediation without
 * duplication (CR-D3 leaf derivation, CR-D5 deterministic A-matrix
 * setup, CR-D15.A honest Ajtai hasher and leaf-pinning path verify).
 */

#define LOG_TAG "dap_chipmunk_hypertree"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_rand.h"
#include "dap_memwipe.h"

#include "chipmunk_hypertree.h"
#include "chipmunk_hash.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_internal.h"

/* ---------------------------------------------------------------------- *
 *  Domain separators                                                     *
 * ---------------------------------------------------------------------- */
/*
 * Each derived seed under a distinct 16-byte label so that compromising
 * or grinding one of them cannot collide with another.  The labels are
 * kept inside the TU (static) — they are part of the wire protocol and
 * must never change without a version bump.
 */
static const uint8_t s_ht_rho_label[16]    = { 'C','H','P','-','H','T','-','R','H','O','-','v','1',0,0,0 };
static const uint8_t s_ht_hasher_label[16] = { 'C','H','P','-','H','T','-','H','S','R','-','v','1',0,0,0 };
static const uint8_t s_ht_keyseed_label[16]= { 'C','H','P','-','H','T','-','K','S','D','-','v','1',0,0,0 };

/* ---------------------------------------------------------------------- *
 *  Small helpers                                                         *
 * ---------------------------------------------------------------------- */

static void s_wipe(void *p, size_t n)
{
    if (p && n) {
        dap_memwipe(p, n);
    }
}

/*
 * SHA3-256( label_16 || in_len·in ) → out[32].
 *
 * Used for rho_seed / hasher_seed / key_seed derivation.  The 16-byte
 * label is right-padded with zeros in the header constants, so the
 * domain is always exactly 16 bytes + input.
 */
static void s_derive32(const uint8_t a_label[16],
                       const uint8_t *a_in, size_t a_in_len,
                       uint8_t a_out[32])
{
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, 16 + a_in_len);
    memcpy(l_buf, a_label, 16);
    if (a_in && a_in_len) memcpy(l_buf + 16, a_in, a_in_len);
    dap_hash_sha3_256_t l_h;
    dap_hash_sha3_256(l_buf, 16 + a_in_len, &l_h);
    memcpy(a_out, &l_h, 32);
    s_wipe(l_buf, 16 + a_in_len);
    DAP_DEL_MULTY(l_buf);
    s_wipe(&l_h, sizeof(l_h));
}

/*
 * Build the HOTS params (A[i] sampled from rho_seed, forward-NTT'd) in
 * the caller-provided slot.  Matches the sampling chipmunk.c /
 * chipmunk_verify do so leaves verify identically under any rho_seed.
 */
static int s_build_hots_params(const uint8_t a_rho_seed[32],
                               chipmunk_hots_params_t *a_params)
{
    memset(a_params, 0, sizeof(*a_params));
    for (int i = 0; i < CHIPMUNK_GAMMA; ++i) {
        if (dap_chipmunk_hash_sample_matrix(a_params->a[i].coeffs,
                                            a_rho_seed,
                                            i) != 0) {
            log_it(L_ERROR, "Failed to sample A[%d] from rho_seed", i);
            return CHIPMUNK_ERROR_HASH_FAILED;
        }
        chipmunk_poly_ntt(&a_params->a[i]);
    }
    return CHIPMUNK_ERROR_SUCCESS;
}

/*
 * Compute the HVC leaf digest of a given HOTS pk.  Wraps the
 * (hots_pk → chipmunk_public_key_t with rho_seed=0) adapter pattern
 * used throughout chipmunk_aggregation.c — the Ajtai hasher is linear
 * over the coefficient vector so a zero rho_seed does not weaken
 * collision resistance of the digest.
 */
static int s_leaf_digest_from_hots_pk(const chipmunk_hots_pk_t *a_hots_pk,
                                      chipmunk_hvc_poly_t *a_digest_out)
{
    chipmunk_public_key_t l_wrap;
    memset(&l_wrap, 0, sizeof(l_wrap));
    memcpy(&l_wrap.v0, &a_hots_pk->v0, sizeof(chipmunk_poly_t));
    memcpy(&l_wrap.v1, &a_hots_pk->v1, sizeof(chipmunk_poly_t));
    return chipmunk_hots_pk_to_hvc_poly(&l_wrap, a_digest_out);
}

/*
 * Fully materialise the tree, leaf_pks cache, HOTS params and HVC
 * hasher for a given sk (whose leaf_index / key_seed / pk fields are
 * already populated).  Returns CHIPMUNK_ERROR_SUCCESS or a negative
 * error; on failure all partially-allocated resources are released so
 * the struct is left in the zero state.
 *
 * This is the one place in the module that iterates over all
 * CHIPMUNK_HT_LEAF_COUNT leaves — both keypair and
 * private_key_from_bytes funnel through it so the tree is guaranteed
 * bit-identical between the two paths.
 */
static int s_materialise_tree(chipmunk_ht_private_key_t *a_sk)
{
    int l_rc = CHIPMUNK_ERROR_SUCCESS;
    chipmunk_hvc_poly_t *l_leaf_digests = NULL;
    chipmunk_hots_pk_t  *l_leaf_pks     = NULL;
    chipmunk_hots_sk_t   l_leaf_sk;
    memset(&l_leaf_sk, 0, sizeof(l_leaf_sk));

    l_rc = s_build_hots_params(a_sk->pk.rho_seed, &a_sk->hots_params);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
        goto fail;
    }
    l_rc = chipmunk_hvc_hasher_init(&a_sk->hasher, a_sk->pk.hasher_seed);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Hypertree hasher init failed: %d", l_rc);
        goto fail;
    }

    l_leaf_pks     = DAP_NEW_Z_COUNT(chipmunk_hots_pk_t,    CHIPMUNK_HT_LEAF_COUNT);
    l_leaf_digests = DAP_NEW_Z_COUNT(chipmunk_hvc_poly_t,   CHIPMUNK_HT_LEAF_COUNT);
    if (!l_leaf_pks || !l_leaf_digests) {
        log_it(L_ERROR, "Failed to allocate %u leaves for hypertree", (unsigned)CHIPMUNK_HT_LEAF_COUNT);
        l_rc = CHIPMUNK_ERROR_MEMORY;
        goto fail;
    }

    /*
     * Deterministic tree fill.  Each leaf derives an independent HOTS
     * secret from the master key_seed via the CR-D3 domain separator
     * shared with chipmunk_sign, so the root committed to at keygen is
     * exactly what chipmunk_ht_sign will re-derive at signing time.
     */
    for (uint32_t i = 0; i < CHIPMUNK_HT_LEAF_COUNT; ++i) {
        l_rc = dap_chipmunk_derive_hots_leaf_secret_internal(a_sk->key_seed, i, &l_leaf_sk);
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
            log_it(L_ERROR, "Leaf-%u secret derivation failed: %d", i, l_rc);
            goto fail;
        }
        dap_chipmunk_compute_hots_pk_internal(&a_sk->hots_params, &l_leaf_sk, &l_leaf_pks[i]);
        l_rc = s_leaf_digest_from_hots_pk(&l_leaf_pks[i], &l_leaf_digests[i]);
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
            log_it(L_ERROR, "Leaf-%u digest conversion failed: %d", i, l_rc);
            goto fail;
        }
        s_wipe(&l_leaf_sk, sizeof(l_leaf_sk));
    }

    l_rc = chipmunk_tree_new_with_leaf_nodes(&a_sk->tree,
                                             l_leaf_digests,
                                             CHIPMUNK_HT_LEAF_COUNT,
                                             &a_sk->hasher);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Merkle tree build failed: %d", l_rc);
        goto fail;
    }

    const chipmunk_hvc_poly_t *l_root = chipmunk_tree_root(&a_sk->tree);
    if (!l_root) {
        l_rc = CHIPMUNK_ERROR_INTERNAL;
        goto fail;
    }
    memcpy(&a_sk->pk.root, l_root, sizeof(*l_root));

    a_sk->leaf_pks     = l_leaf_pks;
    a_sk->materialised = true;

    DAP_DEL_MULTY(l_leaf_digests);
    s_wipe(&l_leaf_sk, sizeof(l_leaf_sk));
    return CHIPMUNK_ERROR_SUCCESS;

fail:
    s_wipe(&l_leaf_sk, sizeof(l_leaf_sk));
    if (l_leaf_digests) DAP_DEL_MULTY(l_leaf_digests);
    if (l_leaf_pks) {
        s_wipe(l_leaf_pks, sizeof(chipmunk_hots_pk_t) * CHIPMUNK_HT_LEAF_COUNT);
        DAP_DEL_MULTY(l_leaf_pks);
    }
    chipmunk_tree_free(&a_sk->tree);
    memset(&a_sk->hots_params, 0, sizeof(a_sk->hots_params));
    memset(&a_sk->hasher, 0, sizeof(a_sk->hasher));
    a_sk->leaf_pks     = NULL;
    a_sk->materialised = false;
    return l_rc;
}

/* ---------------------------------------------------------------------- *
 *  Keygen                                                                *
 * ---------------------------------------------------------------------- */

/*
 * Internal keygen.  `a_seed` is the 32-byte seed that drives EVERY
 * derivation (key_seed / rho_seed / hasher_seed).  Must be drawn from a
 * CS-RNG by the caller (chipmunk_ht_keypair) or passed deterministically
 * from a test vector (chipmunk_ht_keypair_from_seed).
 */
static int s_keypair_internal(const uint8_t a_seed[32],
                              chipmunk_ht_public_key_t  *a_pk,
                              chipmunk_ht_private_key_t *a_sk)
{
    if (!a_seed || !a_pk || !a_sk) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    /*
     * Start from a pristine sk; caller is responsible for passing a
     * zero-initialised struct but harden anyway in case the caller is
     * sloppy — we own the mutex/materialised flags going forward.
     */
    memset(a_sk, 0, sizeof(*a_sk));
    memset(a_pk, 0, sizeof(*a_pk));

    if (pthread_mutex_init(&a_sk->mutex, NULL) != 0) {
        log_it(L_ERROR, "Failed to init sk mutex");
        return CHIPMUNK_ERROR_INTERNAL;
    }
    a_sk->mutex_inited = true;

    s_derive32(s_ht_keyseed_label, a_seed, 32, a_sk->key_seed);
    s_derive32(s_ht_rho_label,     a_seed, 32, a_sk->pk.rho_seed);
    s_derive32(s_ht_hasher_label,  a_seed, 32, a_sk->pk.hasher_seed);
    a_sk->leaf_index = 0u;

    int l_rc = s_materialise_tree(a_sk);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
        chipmunk_ht_private_key_clear(a_sk);
        return l_rc;
    }

    /*
     * tr = SHA3-384( serialized(pk) ) binds every signature to the full
     * public-key bytes.  Any trial substitution of rho_seed / hasher_seed
     * / root would change tr and therefore break the signature chain.
     */
    uint8_t l_pk_bytes[CHIPMUNK_HT_PUBLIC_KEY_SIZE];
    l_rc = chipmunk_ht_public_key_to_bytes(l_pk_bytes, &a_sk->pk);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
        chipmunk_ht_private_key_clear(a_sk);
        return l_rc;
    }
    l_rc = dap_chipmunk_hash_sha3_384(a_sk->tr, l_pk_bytes, CHIPMUNK_HT_PUBLIC_KEY_SIZE);
    s_wipe(l_pk_bytes, sizeof(l_pk_bytes));
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
        chipmunk_ht_private_key_clear(a_sk);
        return l_rc;
    }

    /* Echo the public key out to the caller so they don't need to
     * reach into sk->pk. */
    memcpy(a_pk, &a_sk->pk, sizeof(*a_pk));
    return CHIPMUNK_ERROR_SUCCESS;
}

int chipmunk_ht_keypair(chipmunk_ht_public_key_t  *a_pk_out,
                         chipmunk_ht_private_key_t *a_sk_out)
{
    uint8_t l_seed[32];
    if (dap_random_bytes(l_seed, sizeof(l_seed)) != 0) {
        log_it(L_ERROR, "dap_random_bytes failed for hypertree seed");
        return CHIPMUNK_ERROR_INIT_FAILED;
    }
    int l_rc = s_keypair_internal(l_seed, a_pk_out, a_sk_out);
    s_wipe(l_seed, sizeof(l_seed));
    return l_rc;
}

int chipmunk_ht_keypair_from_seed(const uint8_t a_seed[32],
                                   chipmunk_ht_public_key_t  *a_pk_out,
                                   chipmunk_ht_private_key_t *a_sk_out)
{
    if (!a_seed) return CHIPMUNK_ERROR_NULL_PARAM;
    return s_keypair_internal(a_seed, a_pk_out, a_sk_out);
}

/* ---------------------------------------------------------------------- *
 *  Sign                                                                  *
 * ---------------------------------------------------------------------- */

int chipmunk_ht_sign(chipmunk_ht_private_key_t *a_sk,
                      const uint8_t *a_msg, size_t a_len,
                      chipmunk_ht_signature_t *a_sig)
{
    if (!a_sk || !a_msg || !a_sig) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    if (!a_sk->materialised || !a_sk->leaf_pks || !a_sk->mutex_inited) {
        log_it(L_ERROR, "Hypertree sk is not materialised; call keypair/from_bytes first");
        return CHIPMUNK_ERROR_INTERNAL;
    }
    if (a_len > 10 * 1024 * 1024) {
        log_it(L_ERROR, "Message too large for hypertree sign (%zu bytes)", a_len);
        return CHIPMUNK_ERROR_INVALID_SIZE;
    }

    /*
     * Initialise the signature to a benign state before we touch any of
     * its owning pointers so that an early failure path below leaves the
     * caller with a zero struct (chipmunk_ht_signature_clear is safe on
     * zero).
     */
    memset(a_sig, 0, sizeof(*a_sig));

    int l_rc = pthread_mutex_lock(&a_sk->mutex);
    if (l_rc != 0) {
        log_it(L_ERROR, "Failed to lock hypertree sk mutex: %d", l_rc);
        return CHIPMUNK_ERROR_INTERNAL;
    }

    chipmunk_hots_sk_t        l_leaf_sk;   memset(&l_leaf_sk, 0, sizeof(l_leaf_sk));
    chipmunk_hots_signature_t l_hots_sig;  memset(&l_hots_sig, 0, sizeof(l_hots_sig));
    int l_ret = CHIPMUNK_ERROR_SUCCESS;

    if (a_sk->leaf_index >= CHIPMUNK_HT_MAX_SIGNATURES) {
        log_it(L_ERROR,
               "CR-D15.B: hypertree exhausted (leaf_index=%u, max=%u). Rotate keypair.",
               a_sk->leaf_index, CHIPMUNK_HT_MAX_SIGNATURES);
        l_ret = CHIPMUNK_ERROR_KEY_EXHAUSTED;
        goto out;
    }

    const uint32_t l_idx = a_sk->leaf_index;

    /*
     * Derive the leaf HOTS secret exactly the way s_materialise_tree
     * did at keygen (CR-D3 shared domain separator).  Equality of the
     * two derivations is the invariant that makes the stored root verify
     * against the signature we are about to emit.
     */
    l_ret = dap_chipmunk_derive_hots_leaf_secret_internal(a_sk->key_seed, l_idx, &l_leaf_sk);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Hypertree sign: leaf-%u secret derivation failed", l_idx);
        goto out;
    }
    l_ret = chipmunk_hots_sign(&l_leaf_sk, a_msg, a_len, &l_hots_sig);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Hypertree sign: HOTS sign failed at leaf %u: %d", l_idx, l_ret);
        goto out;
    }

    /*
     * Build the auth path against the current materialised tree.  The
     * tree build/rebuild path normalises to the same heap indices that
     * chipmunk_path_verify later walks against the root.
     */
    chipmunk_path_t l_path;
    memset(&l_path, 0, sizeof(l_path));
    l_ret = chipmunk_tree_gen_proof(&a_sk->tree, (size_t)l_idx, &l_path);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Hypertree sign: gen_proof failed at leaf %u: %d", l_idx, l_ret);
        goto out;
    }

    /*
     * Populate the output signature.  Note: chipmunk_tree_gen_proof
     * already allocated l_path.nodes — we MOVE that allocation into
     * a_sig->auth_path, so the signature now owns it.
     */
    for (int i = 0; i < CHIPMUNK_GAMMA; ++i) {
        memcpy(&a_sig->hots_sig.sigma[i], &l_hots_sig.sigma[i], sizeof(chipmunk_poly_t));
    }
    a_sig->leaf_index = l_idx;
    memcpy(&a_sig->leaf_pk, &a_sk->leaf_pks[l_idx], sizeof(chipmunk_hots_pk_t));
    a_sig->auth_path = l_path;   /* ownership transfer */

    /* Advance the counter AFTER every output field is committed so a
     * transient failure during the sign doesn't burn a slot. */
    a_sk->leaf_index = l_idx + 1u;

out:
    s_wipe(&l_leaf_sk,  sizeof(l_leaf_sk));
    s_wipe(&l_hots_sig, sizeof(l_hots_sig));
    pthread_mutex_unlock(&a_sk->mutex);
    return l_ret;
}

/* ---------------------------------------------------------------------- *
 *  Verify                                                                *
 * ---------------------------------------------------------------------- */

int chipmunk_ht_verify(const chipmunk_ht_public_key_t *a_pk,
                        const uint8_t *a_msg, size_t a_len,
                        const chipmunk_ht_signature_t *a_sig)
{
    if (!a_pk || !a_msg || !a_sig) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    if (a_len > 10 * 1024 * 1024) {
        return CHIPMUNK_ERROR_INVALID_SIZE;
    }
    /*
     * Log level rationale: verify is called from multiple contexts —
     * standalone (where a failure is genuinely unexpected) AND from the
     * ring verifier's inner per-pk loop (where all-but-one pk are
     * EXPECTED to fail).  Flooding L_ERROR on every legitimate non-match
     * produced alarming logs in happy-path ring verifications.  We now
     * log structural violations at L_WARNING (malformed inputs) and
     * mere "signature doesn't verify under THIS pk" at L_DEBUG — the
     * caller knows the disposition and decides severity.
     */
    if (a_sig->leaf_index >= CHIPMUNK_HT_LEAF_COUNT) {
        log_it(L_WARNING, "Hypertree verify: leaf_index %u out of range [0,%u)",
               a_sig->leaf_index, CHIPMUNK_HT_LEAF_COUNT);
        return CHIPMUNK_ERROR_VERIFY_FAILED;
    }
    if (!a_sig->auth_path.nodes ||
         a_sig->auth_path.path_length != (size_t)(CHIPMUNK_HT_HEIGHT - 1u)) {
        log_it(L_WARNING, "Hypertree verify: malformed auth path (len=%zu)",
               a_sig->auth_path.path_length);
        return CHIPMUNK_ERROR_VERIFY_FAILED;
    }
    if (a_sig->auth_path.index != (size_t)a_sig->leaf_index) {
        log_it(L_WARNING, "Hypertree verify: auth_path.index=%zu != leaf_index=%u",
               a_sig->auth_path.index, a_sig->leaf_index);
        return CHIPMUNK_ERROR_VERIFY_FAILED;
    }

    /* 1. Rebuild HOTS params from the public rho_seed. */
    chipmunk_hots_params_t l_hots_params;
    int l_rc = s_build_hots_params(a_pk->rho_seed, &l_hots_params);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
        return l_rc;
    }

    /* 2. HOTS-verify (sigma, msg) against the signature's leaf_pk. */
    chipmunk_hots_signature_t l_hs;
    memset(&l_hs, 0, sizeof(l_hs));
    for (int i = 0; i < CHIPMUNK_GAMMA; ++i) {
        memcpy(&l_hs.sigma[i], &a_sig->hots_sig.sigma[i], sizeof(chipmunk_poly_t));
    }
    l_rc = chipmunk_hots_verify(&a_sig->leaf_pk, a_msg, a_len, &l_hs, &l_hots_params);
    if (l_rc != 0) {
        log_it(L_DEBUG, "Hypertree verify: HOTS-verify failed at leaf %u: %d",
               a_sig->leaf_index, l_rc);
        return CHIPMUNK_ERROR_VERIFY_FAILED;
    }

    /* 3. Compute the leaf digest from the signed leaf_pk and compare it
     *    against the auth path via honest leaf pinning (CR-D15.A). */
    chipmunk_hvc_poly_t l_leaf_digest;
    l_rc = s_leaf_digest_from_hots_pk(&a_sig->leaf_pk, &l_leaf_digest);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
        return l_rc;
    }

    chipmunk_hvc_hasher_t l_hasher;
    l_rc = chipmunk_hvc_hasher_init(&l_hasher, a_pk->hasher_seed);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
        return l_rc;
    }
    if (!chipmunk_path_verify(&a_sig->auth_path, &l_leaf_digest, &a_pk->root, &l_hasher)) {
        log_it(L_DEBUG, "Hypertree verify: Merkle path check failed at leaf %u",
               a_sig->leaf_index);
        return CHIPMUNK_ERROR_VERIFY_FAILED;
    }

    return CHIPMUNK_ERROR_SUCCESS;
}

/* ---------------------------------------------------------------------- *
 *  Serialisation helpers                                                 *
 * ---------------------------------------------------------------------- */

static void s_poly_to_le32(uint8_t *a_out, const chipmunk_poly_t *a_p)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        int32_t c = a_p->coeffs[i];
        a_out[i*4 + 0] = (uint8_t)( c        & 0xFFu);
        a_out[i*4 + 1] = (uint8_t)((c >> 8)  & 0xFFu);
        a_out[i*4 + 2] = (uint8_t)((c >> 16) & 0xFFu);
        a_out[i*4 + 3] = (uint8_t)((c >> 24) & 0xFFu);
    }
}

static void s_poly_from_le32(chipmunk_poly_t *a_p, const uint8_t *a_in)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        uint32_t u = (uint32_t)a_in[i*4 + 0]
                   | ((uint32_t)a_in[i*4 + 1] << 8)
                   | ((uint32_t)a_in[i*4 + 2] << 16)
                   | ((uint32_t)a_in[i*4 + 3] << 24);
        a_p->coeffs[i] = (int32_t)u;
    }
}

static void s_hvc_poly_to_le32(uint8_t *a_out, const chipmunk_hvc_poly_t *a_p)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        int32_t c = a_p->coeffs[i];
        a_out[i*4 + 0] = (uint8_t)( c        & 0xFFu);
        a_out[i*4 + 1] = (uint8_t)((c >> 8)  & 0xFFu);
        a_out[i*4 + 2] = (uint8_t)((c >> 16) & 0xFFu);
        a_out[i*4 + 3] = (uint8_t)((c >> 24) & 0xFFu);
    }
}

static void s_hvc_poly_from_le32(chipmunk_hvc_poly_t *a_p, const uint8_t *a_in)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        uint32_t u = (uint32_t)a_in[i*4 + 0]
                   | ((uint32_t)a_in[i*4 + 1] << 8)
                   | ((uint32_t)a_in[i*4 + 2] << 16)
                   | ((uint32_t)a_in[i*4 + 3] << 24);
        a_p->coeffs[i] = (int32_t)u;
    }
}

/* ---------------------------------------------------------------------- *
 *  Public-key serialisation                                              *
 * ---------------------------------------------------------------------- */

int chipmunk_ht_public_key_to_bytes(uint8_t a_out[CHIPMUNK_HT_PUBLIC_KEY_SIZE],
                                     const chipmunk_ht_public_key_t *a_pk)
{
    if (!a_out || !a_pk) return CHIPMUNK_ERROR_NULL_PARAM;
    memcpy(a_out,                      a_pk->rho_seed,    32);
    memcpy(a_out + 32,                 a_pk->hasher_seed, 32);
    s_hvc_poly_to_le32(a_out + 64,     &a_pk->root);
    return CHIPMUNK_ERROR_SUCCESS;
}

int chipmunk_ht_public_key_from_bytes(chipmunk_ht_public_key_t *a_pk,
                                       const uint8_t a_in[CHIPMUNK_HT_PUBLIC_KEY_SIZE])
{
    if (!a_pk || !a_in) return CHIPMUNK_ERROR_NULL_PARAM;
    memset(a_pk, 0, sizeof(*a_pk));
    memcpy(a_pk->rho_seed,    a_in,      32);
    memcpy(a_pk->hasher_seed, a_in + 32, 32);
    s_hvc_poly_from_le32(&a_pk->root, a_in + 64);
    return CHIPMUNK_ERROR_SUCCESS;
}

/* ---------------------------------------------------------------------- *
 *  Private-key serialisation                                             *
 * ---------------------------------------------------------------------- */

int chipmunk_ht_private_key_to_bytes(uint8_t a_out[CHIPMUNK_HT_PRIVATE_KEY_SIZE],
                                      const chipmunk_ht_private_key_t *a_sk)
{
    if (!a_out || !a_sk) return CHIPMUNK_ERROR_NULL_PARAM;
    /* BE32 leaf_index so the on-disk blob looks like chipmunk.c's encoding. */
    a_out[0] = (uint8_t)((a_sk->leaf_index >> 24) & 0xFFu);
    a_out[1] = (uint8_t)((a_sk->leaf_index >> 16) & 0xFFu);
    a_out[2] = (uint8_t)((a_sk->leaf_index >> 8)  & 0xFFu);
    a_out[3] = (uint8_t)( a_sk->leaf_index        & 0xFFu);

    memcpy(a_out + 4,      a_sk->key_seed, 32);
    memcpy(a_out + 4 + 32, a_sk->tr,       48);
    return chipmunk_ht_public_key_to_bytes(a_out + 4 + 32 + 48, &a_sk->pk);
}

int chipmunk_ht_private_key_from_bytes(chipmunk_ht_private_key_t *a_sk_out,
                                        const uint8_t a_in[CHIPMUNK_HT_PRIVATE_KEY_SIZE])
{
    if (!a_sk_out || !a_in) return CHIPMUNK_ERROR_NULL_PARAM;
    memset(a_sk_out, 0, sizeof(*a_sk_out));

    if (pthread_mutex_init(&a_sk_out->mutex, NULL) != 0) {
        return CHIPMUNK_ERROR_INTERNAL;
    }
    a_sk_out->mutex_inited = true;

    a_sk_out->leaf_index = ((uint32_t)a_in[0] << 24)
                         | ((uint32_t)a_in[1] << 16)
                         | ((uint32_t)a_in[2] << 8)
                         | ((uint32_t)a_in[3]);

    memcpy(a_sk_out->key_seed, a_in + 4,      32);
    memcpy(a_sk_out->tr,       a_in + 4 + 32, 48);

    int l_rc = chipmunk_ht_public_key_from_bytes(&a_sk_out->pk, a_in + 4 + 32 + 48);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) goto fail;

    /* Rebuild the tree from key_seed + pk.rho_seed + pk.hasher_seed. */
    l_rc = s_materialise_tree(a_sk_out);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) goto fail;

    /* Integrity self-check: the rebuilt root MUST match the one in the
     * serialised pk.  Otherwise the on-disk blob has been tampered with
     * OR the key_seed and pk disagree — either way the key is not usable. */
    if (memcmp(&a_sk_out->pk.root.coeffs, &a_sk_out->tree.non_leaf_nodes[0].coeffs,
               sizeof(a_sk_out->pk.root.coeffs)) != 0) {
        log_it(L_ERROR, "Hypertree sk: rebuilt root mismatches serialised pk root");
        l_rc = CHIPMUNK_ERROR_VERIFY_FAILED;
        goto fail;
    }
    return CHIPMUNK_ERROR_SUCCESS;

fail:
    chipmunk_ht_private_key_clear(a_sk_out);
    return l_rc;
}

/* ---------------------------------------------------------------------- *
 *  Signature serialisation                                               *
 * ---------------------------------------------------------------------- */

int chipmunk_ht_signature_to_bytes(uint8_t a_out[CHIPMUNK_HT_SIGNATURE_SIZE],
                                    const chipmunk_ht_signature_t *a_sig)
{
    if (!a_out || !a_sig) return CHIPMUNK_ERROR_NULL_PARAM;
    if (!a_sig->auth_path.nodes ||
         a_sig->auth_path.path_length != (size_t)(CHIPMUNK_HT_HEIGHT - 1u)) {
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }

    size_t l_off = 0;
    /* sigma[GAMMA] */
    for (int i = 0; i < CHIPMUNK_GAMMA; ++i) {
        s_poly_to_le32(a_out + l_off, &a_sig->hots_sig.sigma[i]);
        l_off += (size_t)CHIPMUNK_N * 4u;
    }
    /* leaf_index (BE32) */
    a_out[l_off + 0] = (uint8_t)((a_sig->leaf_index >> 24) & 0xFFu);
    a_out[l_off + 1] = (uint8_t)((a_sig->leaf_index >> 16) & 0xFFu);
    a_out[l_off + 2] = (uint8_t)((a_sig->leaf_index >> 8)  & 0xFFu);
    a_out[l_off + 3] = (uint8_t)( a_sig->leaf_index        & 0xFFu);
    l_off += 4;

    /* leaf_pk.v0 || leaf_pk.v1 */
    s_poly_to_le32(a_out + l_off, &a_sig->leaf_pk.v0);
    l_off += (size_t)CHIPMUNK_N * 4u;
    s_poly_to_le32(a_out + l_off, &a_sig->leaf_pk.v1);
    l_off += (size_t)CHIPMUNK_N * 4u;

    /* path: (left || right) per level */
    for (size_t lv = 0; lv < a_sig->auth_path.path_length; ++lv) {
        s_hvc_poly_to_le32(a_out + l_off, &a_sig->auth_path.nodes[lv].left);
        l_off += (size_t)CHIPMUNK_N * 4u;
        s_hvc_poly_to_le32(a_out + l_off, &a_sig->auth_path.nodes[lv].right);
        l_off += (size_t)CHIPMUNK_N * 4u;
    }
    if (l_off != CHIPMUNK_HT_SIGNATURE_SIZE) {
        log_it(L_ERROR, "Hypertree sig serialise: wrote %zu bytes, expected %u",
               l_off, (unsigned)CHIPMUNK_HT_SIGNATURE_SIZE);
        return CHIPMUNK_ERROR_INTERNAL;
    }
    return CHIPMUNK_ERROR_SUCCESS;
}

int chipmunk_ht_signature_from_bytes(chipmunk_ht_signature_t *a_sig,
                                      const uint8_t a_in[CHIPMUNK_HT_SIGNATURE_SIZE])
{
    if (!a_sig || !a_in) return CHIPMUNK_ERROR_NULL_PARAM;
    memset(a_sig, 0, sizeof(*a_sig));

    size_t l_off = 0;
    for (int i = 0; i < CHIPMUNK_GAMMA; ++i) {
        s_poly_from_le32(&a_sig->hots_sig.sigma[i], a_in + l_off);
        l_off += (size_t)CHIPMUNK_N * 4u;
    }
    a_sig->leaf_index = ((uint32_t)a_in[l_off + 0] << 24)
                      | ((uint32_t)a_in[l_off + 1] << 16)
                      | ((uint32_t)a_in[l_off + 2] << 8)
                      | ((uint32_t)a_in[l_off + 3]);
    l_off += 4;

    s_poly_from_le32(&a_sig->leaf_pk.v0, a_in + l_off);
    l_off += (size_t)CHIPMUNK_N * 4u;
    s_poly_from_le32(&a_sig->leaf_pk.v1, a_in + l_off);
    l_off += (size_t)CHIPMUNK_N * 4u;

    const size_t l_path_len = (size_t)CHIPMUNK_HT_HEIGHT - 1u;
    a_sig->auth_path.nodes = DAP_NEW_Z_COUNT(chipmunk_path_node_t, l_path_len);
    if (!a_sig->auth_path.nodes) {
        return CHIPMUNK_ERROR_MEMORY;
    }
    a_sig->auth_path.path_length = l_path_len;
    a_sig->auth_path.index       = (size_t)a_sig->leaf_index;
    for (size_t lv = 0; lv < l_path_len; ++lv) {
        s_hvc_poly_from_le32(&a_sig->auth_path.nodes[lv].left,  a_in + l_off);
        l_off += (size_t)CHIPMUNK_N * 4u;
        s_hvc_poly_from_le32(&a_sig->auth_path.nodes[lv].right, a_in + l_off);
        l_off += (size_t)CHIPMUNK_N * 4u;
    }
    if (l_off != CHIPMUNK_HT_SIGNATURE_SIZE) {
        chipmunk_ht_signature_clear(a_sig);
        return CHIPMUNK_ERROR_INVALID_SIZE;
    }
    return CHIPMUNK_ERROR_SUCCESS;
}

/* ---------------------------------------------------------------------- *
 *  Resource release                                                      *
 * ---------------------------------------------------------------------- */

void chipmunk_ht_private_key_clear(chipmunk_ht_private_key_t *a_sk)
{
    if (!a_sk) return;

    /*
     * The leaf_pks array and the chipmunk_tree carry the whole
     * materialisation; wipe and release them before touching the
     * scalar secret-bearing fields.
     */
    if (a_sk->leaf_pks) {
        s_wipe(a_sk->leaf_pks, sizeof(chipmunk_hots_pk_t) * CHIPMUNK_HT_LEAF_COUNT);
        DAP_DEL_MULTY(a_sk->leaf_pks);
        a_sk->leaf_pks = NULL;
    }
    chipmunk_tree_free(&a_sk->tree);
    s_wipe(&a_sk->hots_params, sizeof(a_sk->hots_params));
    s_wipe(&a_sk->hasher,      sizeof(a_sk->hasher));
    s_wipe(a_sk->key_seed,     sizeof(a_sk->key_seed));
    s_wipe(a_sk->tr,           sizeof(a_sk->tr));
    /*
     * pk and leaf_index are not secret but zero them to keep the
     * struct in a well-known post-clear state.  mutex destruction
     * comes last because any pending waiter would UB on a zeroed
     * mutex object.
     */
    memset(&a_sk->pk, 0, sizeof(a_sk->pk));
    a_sk->leaf_index  = 0u;
    a_sk->materialised = false;
    if (a_sk->mutex_inited) {
        pthread_mutex_destroy(&a_sk->mutex);
        a_sk->mutex_inited = false;
    }
}

void chipmunk_ht_signature_clear(chipmunk_ht_signature_t *a_sig)
{
    if (!a_sig) return;
    if (a_sig->auth_path.nodes) {
        DAP_DEL_MULTY(a_sig->auth_path.nodes);
    }
    memset(a_sig, 0, sizeof(*a_sig));
}
