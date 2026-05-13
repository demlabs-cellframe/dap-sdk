---
doc: design_decision_cr9_4
phase: CR-9.4 — Public threshold dealer/combiner API (kick-off)
status: ACTIVE — CR-9.4.A in this slice; CR-9.4.B (true threshold sign_partial) explicitly deferred
predecessors:
  - doc/crypto/chipmunk_ring/design_decision_cr9.md   # CR-9 master design
  - module/crypto/src/sig/chipmunk/chipmunk_ring_shamir.h  # CR-9.3 primitive
---

# CR-9.4 — Public threshold dealer/combiner API: Design Decision

> **Scope of this document.**  CR-9.4 in the master plan
> (task_6516dac58ef91416) names three public entry points:
> `threshold_deal`, `threshold_sign_partial`, `threshold_combine`.
> Naively those names suggest a *true* threshold signature scheme
> (no key reconstruction at the combiner).  This document
> separates that ambition into two tractable slices and locks in
> the design for the **first** slice; the second is explicitly
> deferred with a clear roadmap so future work cannot regress
> on the contract laid down here.
>
> **CR-9.4.A (this slice)** — *trusted-dealer dealer-based
> threshold via key reconstruction.*  Public `_deal` and
> `_combine` API; signing uses the existing `chipmunk_ring_sign`
> after the combiner reconstructs the master seed.  This is the
> design used by Vault, libgfshare, SLIP-39, and every social-
> recovery wallet shipping today; it is sound, fits every CR-9.0
> use-case (governance multi-sig, social-recovery, corporate
> signing, DAO votes), and ships in days rather than months.
>
> **CR-9.4.B (deferred)** — *true threshold signature without
> key reconstruction* (`_sign_partial` + Lagrange-in-the-exponent
> aggregation).  Out of scope for this slice because it requires
> a peer-reviewed proof of HOTS linearity under leaf-derivation,
> a brand-new wire-format for partial signatures, and a verifier
> that accepts an aggregate without the master public key
> materialising as a single hypertree pk.  Tracked at the bottom
> of this document as the explicit follow-up so the deferral is
> not silent.

---

## 1. Why "trusted-dealer reconstruction" is the right first step

| Property                                       | CR-9.4.A (this slice)                   | CR-9.4.B (deferred)                |
|------------------------------------------------|------------------------------------------|------------------------------------|
| Master sk materialises somewhere                | At the *combiner* (one party, one moment) | Never                              |
| Compatible with existing `chipmunk_ring_sign`   | Yes — verifier sees a normal ring sig    | No — needs new wire-format         |
| Compatible with existing verifier               | Yes                                      | No                                 |
| Cellframe block-validator change required       | None                                     | Substantial                        |
| Peer-review surface for security proof          | Shamir + Lagrange (textbook)             | HOTS linearity + leaf-derivation   |
| Engineering cost                                | Days                                     | Months                             |
| Closes Cellframe governance / social-recovery   | Yes                                      | Yes (later)                        |
| Closes corporate-signing-policy use-case        | Yes                                      | Yes (later)                        |
| Closes DAO-vote use-case                        | Yes                                      | Yes (later)                        |

Every CR-9.0 target use-case is satisfied by CR-9.4.A.  CR-9.4.B
adds defence-in-depth ("the combiner is also untrusted") which
matters for a subset of governance scenarios (purely on-chain
threshold-of-validators with no off-chain coordinator) but does
not block the launch of *any* of the named use-cases.

The design therefore ships CR-9.4.A now and tracks CR-9.4.B as
a dedicated follow-up with explicit acceptance criteria (§7).

---

## 2. Secret-carrier choice (CR-9.4.A)

ChipmunkRing private keys are 32-byte hypertree `key_seed`s
(`chipmunk_ht_private_key_t::key_seed[32]`).  CR-9.3 ships a
Shamir primitive over `Z_CHIPMUNK_Q` with `q = 3168257` (≈ 21.6
bits / chunk).

Splitting a 32-byte (256-bit) seed therefore needs multiple
independent share rows.  Two encodings were considered:

* **8 × 32-bit words** — natural alignment but each word can
  exceed `q-1`, forcing a mod-q reduction that is *not*
  bijective.  Two distinct seeds map to the same share row,
  breaking the secret-sharing security claim.
* **16 × 16-bit chunks** — every chunk in `[0, 2^16) ⊂ [0, q)`
  by construction.  Reconstruction is bijective (each chunk
  maps 1-1 to a field element), so the perfect-secrecy
  guarantee of Shamir lifts to the full seed.

**Decision D-1**: encode the 32-byte seed as 16 × 16-bit
little-endian chunks, share each chunk independently, and
concatenate on reconstruction.  Each ring participant therefore
receives a row of 16 `(index, value)` pairs — one per chunk.

The wire size of a single share is therefore:
```
wire(share_i) = 4 (index) + 16 × 4 (value) = 68 bytes
            ≈  4.25 × the secret size
```
Comparable to SLIP-39 (≈ 4×) and Vault (4×) Shamir overheads;
acceptable.

---

## 3. Public API surface (CR-9.4.A)

```c
/* module/crypto/include/dap_chipmunk_ring_threshold.h
 *
 * The public threshold dealer/combiner API.  Internal helpers
 * (chipmunk_ring_shamir.*) stay private; only the seed-level
 * dealer/combiner is exposed at the public boundary.
 */

#define CHIPMUNK_RING_THRESHOLD_SHARE_VERSION  1u
#define CHIPMUNK_RING_THRESHOLD_SHARE_MAGIC    0x43524853u  /* 'CRHS' LE */
#define CHIPMUNK_RING_THRESHOLD_SHARE_CHUNKS   16u           /* 32 B / 2 B */

typedef struct chipmunk_ring_threshold_share {
    uint8_t  data[1 + 1 + 1 + 1 + 4 +
                  CHIPMUNK_RING_THRESHOLD_SHARE_CHUNKS * 4u];
    /* Layout (little-endian):
     *   magic(4) || version(1) || n(1) || t(1) || index(1) ||
     *   chunks[16] = uint32_t LE per chunk
     *
     * 4-byte magic + 1-byte ver pin the wire format; n and t
     * are echoed so a stray share cannot be combined under the
     * wrong (n, t) by a careless caller; index is the chunk-
     * common participant index in [1, n].  The 16 chunks are
     * the per-chunk Shamir y-values.
     *
     * Total = 8 + 64 = 72 bytes per share.
     */
} chipmunk_ring_threshold_share_t;

/**
 * Split @a a_master_seed (32 bytes) into @a a_n threshold shares
 * with reconstruction threshold @a a_t.
 *
 * Coefficients are sampled from the system CSPRNG via
 * dap_random_bytes + rejection sampling (CR-9.3 primitive).
 * Master seed bytes never leave the dealer except as evaluations
 * of the polynomial — the dealer wipes its own scratch.
 *
 * Caller responsibility:
 *   - distributes the resulting shares out-of-band over
 *     authenticated channels (the on-the-wire share blob is not
 *     itself encrypted; confidentiality is the channel's job);
 *   - the caller MUST keep @a a_n × sizeof(share) bytes of
 *     output buffer alive for the duration of the call.
 *
 * Returns:
 *   0       on success;
 *   -EINVAL on contract violation (n < 2, t < 2, t > n,
 *           n > CHIPMUNK_RING_THRESHOLD_MAX_N, NULL seed/out);
 *   -ENOMEM on internal allocation failure;
 *   -EIO    on CSPRNG failure (propagated from CR-9.3).
 *
 * On any error the output buffer is fully zeroised.
 */
int chipmunk_ring_threshold_deal(const uint8_t a_master_seed[32],
                                 uint32_t a_n,
                                 uint32_t a_t,
                                 chipmunk_ring_threshold_share_t *a_out_shares);

/**
 * Reconstruct the master seed from any @a a_t valid shares
 * produced by `chipmunk_ring_threshold_deal`.
 *
 * Validates each share's magic, version, (n, t) echo, and index
 * range before use.  Cross-checks that all shares advertise the
 * SAME (n, t) — mixing shares from different dealing rounds is
 * a deployment foot-gun and is rejected up-front rather than
 * producing a garbage seed.  Duplicate indices (the only case
 * where Lagrange divides by zero) are also rejected.
 *
 * On success the reconstructed @a a_out_master_seed equals the
 * dealer's input bit-for-bit (proven by the CR-9.3 unit-test
 * suite + the CR-9.4 acceptance suite).
 *
 * Returns:
 *   0       on success;
 *   -EINVAL on any of: NULL output, NULL shares, t < 2,
 *           t > MAX_N, magic mismatch, version mismatch,
 *           (n, t) mismatch across shares, duplicate indices,
 *           any index outside [1, n];
 *   ECANCELED is reserved for future "shares are valid but the
 *           reconstructed seed fails an integrity check" — not
 *           used in CR-9.4.A; callers must treat any non-zero
 *           return as "unable to reconstruct, do NOT use the
 *           output buffer".
 *
 * On any error the output buffer is fully zeroised.
 */
int chipmunk_ring_threshold_combine(const chipmunk_ring_threshold_share_t *a_shares,
                                    uint32_t a_t,
                                    uint8_t a_out_master_seed[32]);

/**
 * Securely wipe a share buffer.  Convenience wrapper around
 * dap_memwipe so callers do not have to include the internal
 * memwipe header.  Idempotent; NULL-safe.
 */
void chipmunk_ring_threshold_share_wipe(chipmunk_ring_threshold_share_t *a_share);
```

### 3.1 Why no `_sign_partial` in CR-9.4.A

CR-9.4.A does not introduce a "partial signature" object
because the CR-9.4.A signer flow is:

```
{shares} → combine → master_seed → chipmunk_ht_keypair_from_seed
                                  → chipmunk_ring_sign (existing path)
                                  → chipmunk_ht_private_key_clear (wipes seed)
```

i.e. the existing `chipmunk_ring_sign` is the "combine + sign"
step.  Adding a stub `_sign_partial` in CR-9.4.A that simply
delegates to `chipmunk_ring_sign` after reconstruction would be
the *worst* of both worlds: it would create a wire-level
partial-signature shape that callers depend on, then break it
when CR-9.4.B introduces the real one.  We therefore do *not*
ship a stub; CR-9.4.B introduces `_sign_partial` (with a
genuinely new contract) when ready.

---

## 4. Wire format and forward-compatibility

The 72-byte share wire format is deliberately conservative:

* `magic` is the ASCII string `'CRHS'` little-endian — easy to
  spot in hex dumps and impossible to confuse with chipmunk's
  `'CHMP'` aggregate-sig magic.
* `version` is a 1-byte counter starting at 1.  Future
  CR-9.4.B partial-signature objects use a *different* magic
  (`'CRHP'` is reserved) so the two never collide on the wire.
* `n` and `t` are echoed in every share so a `_combine` caller
  who picks shares out of a pile of files can detect mismatched
  dealing rounds before producing garbage.
* `index` is 1 byte because `CHIPMUNK_RING_THRESHOLD_MAX_N = 64`
  fits in one byte; if MAX_N is ever raised above 255 the
  version bumps and the field widens.
* The 16 chunk slots are fixed-width 4-byte little-endian — even
  though only the low 22 bits are ever populated, padding to 4
  bytes keeps the layout cache-line-aligned and makes the wire
  blob trivially memory-mappable.

No length prefix on the chunk array because the count is fixed
by `version` (= 16 for v1).  Future versions that change the
chunking strategy bump the version byte.

---

## 5. Failure-mode discipline

Inherited from CR-9.3 and applied at every CR-9.4 entry/exit:

1. **Zeroise on every error path.**  Output buffers (shares
   array in `_deal`, master_seed in `_combine`) are fully
   `memset(0)` before any contract violation is reported.
2. **No silent truncation.**  `_combine` rejects shares that
   advertise indices > MAX_N or chunk values >= q rather than
   masking — masking would let a malicious participant inject
   an out-of-range value and still pass through Lagrange.
3. **Cross-share consistency checks.**  All shares passed to
   `_combine` must agree on (magic, version, n, t).
4. **CSPRNG failure is `-EIO`, not `-1`.**  Propagated from
   CR-9.3 so callers can distinguish "bad input" (-EINVAL) from
   "system entropy unavailable" (-EIO).
5. **`chipmunk_ring_threshold_share_wipe`** is the only public
   API that touches share memory directly; it is implemented
   via `dap_memwipe` and cannot be optimised away.

---

## 6. Test acceptance for CR-9.4.A

A new test executable
`test_unit_crypto_chipmunk_ring_threshold` registers the
following dedicated tests:

| Test                                                     | Contract                                                                                                                  |
|----------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------|
| `test_seed_roundtrip`                                    | `combine(deal(seed, n, t)[any t]) == seed` for a deterministic seed                                                       |
| `test_seed_roundtrip_random`                              | Same, over many random seeds and (n, t) triples                                                                            |
| `test_seed_roundtrip_full_64_byte_entropy`                | Verify all 256 bits round-trip (probe both halves of every chunk)                                                          |
| `test_subset_invariance`                                  | Every C(n, t) subset reconstructs the same seed                                                                            |
| `test_signing_after_combine`                              | `combine → chipmunk_ht_keypair_from_seed → chipmunk_ht_sign → chipmunk_ht_verify` succeeds end-to-end                      |
| `test_combine_rejects_mixed_dealing_rounds`               | Shares from `deal(seedA)` mixed with `deal(seedB)` — `_combine` returns -EINVAL                                            |
| `test_combine_rejects_truncated_share`                    | Tamper a single byte in the magic/version → -EINVAL, output zeroised                                                       |
| `test_combine_rejects_index_zero`                         | Rejected with -EINVAL (would expose master seed directly)                                                                  |
| `test_combine_rejects_duplicate_indices`                  | Rejected with -EINVAL (Lagrange divide-by-zero)                                                                            |
| `test_combine_rejects_oversized_chunk`                    | `chunks[i] >= q` → -EINVAL (no silent masking)                                                                             |
| `test_share_wipe_idempotent`                              | `share_wipe(NULL)` and `share_wipe(share); share_wipe(share)` both safe                                                    |
| `test_zeroisation_on_error`                               | Output buffer fully zero after any -EINVAL/-ENOMEM/-EIO path                                                               |

Total: 12 dedicated tests; the underlying `_share` /
`_reconstruct` invariants from CR-9.3 stay locked in by the
existing 11-test suite.

---

## 7. Deferred follow-up — CR-9.4.B (true threshold sign_partial)

Tracked here so the deferral is not silent.  CR-9.4.B closes
the "combiner-also-untrusted" gap by introducing a true
threshold signature scheme on top of HOTS.

### 7.1 Open research questions

1. **HOTS linearity under leaf-derivation.**  Each hypertree
   leaf has its own HOTS (s0, s1) sampled from a domain-
   separated derivation chain (CR-D3 fix).  For a true
   threshold signature `Σ y_j · σ_j ≡ σ_aggregate` to verify
   under the master pk, the leaf-derivation must commute with
   share-linear combination — i.e. `key_seed = Σ λ_j · seed_j`
   must imply `leaf_secret_at_index_k(key_seed) = Σ λ_j ·
   leaf_secret_at_index_k(seed_j)`.  Today's derivation
   (`SHAKE256(domain || key_seed || leaf_index)`) is **not**
   linear; it is a non-linear PRF.  CR-9.4.B requires either a
   linear leaf-derivation (security proof needed) or moving the
   threshold-friendly derivation chain onto a separate code-path.
2. **Merkle root stability under linear seed combination.**  The
   tree root anchors verifier security.  If leaf-derivation is
   linearised, the root must also be a linear function of the
   per-leaf HOTS pks — currently it is a Merkle hash, which is
   non-linear.  Either the root-construction switches to a
   homomorphic accumulator, or threshold mode uses a different
   "combined pk" that is the Lagrange combination of dealer-
   provided per-share pks.
3. **Wire format for partial signatures.**  Reserved magic
   `'CRHP'` (1 byte version, 1 byte (n, t), 4 byte index, plus
   a Lagrange-in-the-exponent partial HOTS sig of size ≈
   `CHIPMUNK_HT_SIGNATURE_SIZE`).
4. **Verifier compatibility.**  Cellframe block-validators
   currently call `dap_sign_verify` which fans out to
   `chipmunk_ring_verify`.  Threshold-aggregate signatures
   need either a new `dap_sign_type_t` (preferred, no
   compatibility break) or a `required_signers > 1` branch in
   the existing verifier that checks an aggregate equation
   (compatibility break for the wire format of `signature`).

### 7.2 Acceptance criteria (CR-9.4.B)

- [ ] `chipmunk_ring_threshold_sign_partial(share, msg, msg_len, out_partial)` produces a wire-formatted partial signature that is *not* a complete signature on its own.
- [ ] `chipmunk_ring_threshold_combine_signatures(partials[t], indices[t], out_aggregate_sig)` produces a signature that verifies under the dealer-published "combined pk" (which is *not* a single-party hypertree pk).
- [ ] At most `t-1` partial signatures reveal nothing about the master seed (proven, not asserted).
- [ ] Wire-format documented in `doc/crypto/chipmunk_ring/threshold_partial_sig_wire_format.md`.
- [ ] New `dap_sign_type_t` value `SIG_TYPE_CHIPMUNK_RING_THRESHOLD` so the verifier dispatch is explicit.
- [ ] Test suite covers correctness, t-1 zero-leakage, rogue-key resistance (depends on CR-9.5), and forgery resistance under standard threshold security model.

### 7.3 Estimated effort

CR-9.4.B is sized at **2-3 weeks** of focused work plus a
**peer-review cycle** for the linearity proof.  Splitting it
out from CR-9.4.A is what allows CR-9.4.A to land in days.

---

## 8. Decisions log (CR-9.4.A)

| #     | Decision                                                                                                                       | Rationale                                                                                                                  |
|-------|--------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------|
| D-1   | Encode the 32-byte master seed as 16 × 16-bit chunks, share each chunk independently with the CR-9.3 primitive                  | 22-bit `q` cannot losslessly carry a 32-bit word; 16-bit chunks fit losslessly and keep Shamir's perfect-secrecy claim     |
| D-2   | Public dealer/combiner API only in CR-9.4.A; defer `sign_partial` to CR-9.4.B with explicit roadmap                            | A stub `_sign_partial` would create a wire-shape commitment that CR-9.4.B then has to break; the deferral is honest        |
| D-3   | Wire format: 72-byte fixed share with magic `'CRHS'` + version + (n, t) + index + 16 chunks (4 B each, only 22 bits used)       | Magic/version pin the format; (n, t) echo catches mixed-dealing-round mistakes; chunk padding keeps the blob mmap-friendly |
| D-4   | Future partial-sig magic reserved as `'CRHP'`                                                                                   | No collision risk between CR-9.4.A and CR-9.4.B blobs even when stored side-by-side                                        |
| D-5   | `_combine` cross-checks (magic, version, n, t) across all shares before reconstruction                                          | Mixed dealing rounds are a deployment foot-gun; rejecting up-front beats producing a garbage seed                          |
| D-6   | `chipmunk_ring_threshold_share_wipe` is part of the public API                                                                 | Callers cannot wipe via `dap_memwipe` directly without including an internal header; making wipe public is a hard contract |
| D-7   | CR-9.4.A is the production path for every CR-9.0 use-case; CR-9.4.B is a defence-in-depth follow-up, not a launch blocker      | Every named use-case (governance, social-recovery, corporate signing, DAO votes) is satisfied by trusted-dealer reconstruction |

---

*CR-9.4 design slice, 2026-05-13.*
