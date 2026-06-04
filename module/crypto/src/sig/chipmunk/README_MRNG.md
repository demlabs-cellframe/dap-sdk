# MRNG/v1 — Chipmunk-native log-N Threshold Ring Signature (wire spec)

**Status:** M1 + G2 v2 locked layout, cryptographic core stubbed (returns
`CHIPMUNK_RING_ERR_NOT_IMPLEMENTED` until M3+).

**Owning task:** `task_ac273cea` (CR-11.G Phase 7.7).

**Design references**
* Design lock v1: `documentation_c36c57f25e91f318`
* Self-review of design lock: `documentation_05b11e509b63f097`
* Amendment v2 (Chipmunk-native, max reuse): `documentation_0539a9f3f1b8ae5d`
* G2 math lock v1: `documentation_2bac0e05b29ab6ad`
* G2 math lock self-review: `documentation_045c56db2bce1cd6`
* G2 math lock v2 (BINDING, supersedes v1 in §2.1, §5, §8): `documentation_ae16ac5717c3ce9c`
* G2 math lock v2.1 (BINDING, wire-size correction supersedes v2 in §A1.1, §A1.2, §A1.4): `documentation_fc6bb792148137c5`
* M0 evidence: `history_82fa392a84fe2969`
* M3.1 evidence: `history_3720a18487e67dbb`
* M3.3 evidence (bind-block helpers): `history_380ae7bd1e1096f5`
* G3 fold simulator + soundness + composition: `MRNG_G3_FOLD_SIMULATOR.md`
  (pins constants for M4: FOLD_SEED_BYTES=32, FS_OUT_BITS=384,
  LEAF_MASK_BOUND=w^D, LEAF_MASK_PACK_BITS=49; resolves C1
  per-round VCom overhead via seed-based opening derivation)
* M4 fold design lock + in-memory prove/verify: `MRNG_M4_FOLD.md`,
  `chipmunk_mring_fold.{h,c}` (G3.1 §4 halving fold over R_q^{(e)})
* M4.0a CRITICAL ring-splitting finding: `MRNG_M4_INVERTIBILITY.md`
  (empirically proves R_q FULLY splits ⇒ naive fold soundness ≈2⁻¹⁷,
  NOT 128-bit; G2 v2 §A3 λ_inv≈980 retired as wrong-ring model)
* G3.1 DESIGN LOCK (corrected Option B, supersedes G3 §1.2/§4 and the
  G3/G2 v2.1 size tables): `MRNG_G3_1_EXTENSION_SOUNDNESS.md`
  (ACK21/RoK-2024 fold over a degree-6 ring extension with a
  subtractive set of size qᵉ≈2¹²⁹·⁶ ⇒ single-shot 128-bit soundness,
  log-N; honest size ≈70–95 KB @ N=256 / ≈45–60 KB @ N=16)

---

## 1. Identity

| field          | value          | hex (LE)       | rationale                                 |
|----------------|----------------|----------------|-------------------------------------------|
| magic          | `'MRNG'`       | `0x474E524D`   | distinct from CLRS / CLRP / CLPK / CLSK   |
| version        | `1`            | `0x00000001`   | strict equality on verifier               |
| params profile | `'MRV1'`       | `0x31565252`   | future profiles bump this, not `version`  |

Verifier rejects any other tuple immediately with the matching error
(`ERR_MAGIC_MISMATCH`, `ERR_VERSION_MISMATCH`, `ERR_PARAMS_MISMATCH`).

---

## 2. Algebraic substrate (inherited from Chipmunk)

| symbol      | value             | source                                  |
|-------------|-------------------|-----------------------------------------|
| `n`         | 512               | `CHIPMUNK_N`                            |
| `q`         | 3 168 257         | `CHIPMUNK_Q`                            |
| `q_bits`    | 22                | `CHIPMUNK_LRS_Q_BITS`                   |
| `z_bits`    | 20                | `CHIPMUNK_LRS_Z_BITS`                   |
| `K_PK`      | 6                 | `CHIPMUNK_LRS_K` (aggregated-pk dim.)   |
| `K_T`       | 6                 | `CHIPMUNK_LRS_K` (link-tag dim.)        |
| `w_c`       | 37                | `CHIPMUNK_LRS_CHALLENGE_WEIGHT`         |
| `β_w`       | 13                | `CHIPMUNK_LRS_WITNESS_BOUND`            |
| `qpack`     | 1 408 B / poly    | `(n · q_bits) / 8 = 512·22/8`           |
| `zpack`     | 1 280 B / poly    | `(n · z_bits) / 8 = 512·20/8`           |

All MRNG primitives use these in unmodified form. No new NTT, no new
sampler. See amendment v2 §2 for the audit of reused symbols.

---

## 3. Envelope

| symbol      | value           | meaning                                |
|-------------|-----------------|----------------------------------------|
| `N`         | `[2, 256]`      | ring size                              |
| `t`         | `[1, N]`        | threshold (# of signers in the subset) |
| `fold_depth`| `1 + ceil(log2 N)` | 2, 3, 4, 5, 6, 7, 8, 9              |

`fold_depth` is the depth of the Bulletproof-style halving fold over the
**augmented** vector `b̃ = (b, b∘(b−1)) ∈ R_q^{2N}` (G2 v2 §A1.1) — the
extra round absorbs the binary check inline with the Hamming-weight check.
It is derived from `N`; the verifier RE-COMPUTES `fold_depth` from `N` and
rejects with `ERR_PARAMS_MISMATCH` if the on-wire value disagrees.

---

## 4. Header (28 bytes, little-endian)

| offset | size | field         | type | constraint                                |
|-------:|-----:|---------------|------|-------------------------------------------|
|     0  |   4  | `magic`       | u32  | `== 0x474E524D`                           |
|     4  |   4  | `version`     | u32  | `== 1`                                    |
|     8  |   4  | `params_id`   | u32  | `== 0x31565252`                           |
|    12  |   4  | `N`           | u32  | `2 ≤ N ≤ 256`                             |
|    16  |   4  | `t`           | u32  | `1 ≤ t ≤ N`                               |
|    20  |   4  | `fold_depth`  | u32  | `== 1 + ceil(log2 N)`, `2 ≤ d ≤ 9`        |
|    24  |   4  | `flags`       | u32  | bit 0 = 1 (linkable); bits 1..31 reserved |

Reserved flag bits MUST be 0 on the wire; verifier rejects with
`ERR_PARAMS_MISMATCH` if any reserved bit is set.

---

## 5. Fixed hashes (128 bytes)

Four 32-byte SHA3-256 digests, in this order:

| offset | size | name        | binding                                                                                  |
|-------:|-----:|-------------|------------------------------------------------------------------------------------------|
|    28  |  32  | `ring_hash` | `SHA3-256("chipmunk-mring-ring-v1" ‖ params_id ‖ N ‖ canonical-sorted pk_1..pk_N)`       |
|    60  |  32  | `ctx_hash`  | `SHA3-256("chipmunk-mring-ctx-v1"  ‖ params_id ‖ ctx_len ‖ ctx_bytes)`                   |
|    92  |  32  | `msg_hash`  | `SHA3-256("chipmunk-mring-msg-v1"  ‖ params_id ‖ msg_len ‖ msg_bytes)`                   |
|   124  |  32  | `fs_seed`   | Fiat-Shamir transcript root: `SHA3-256("chipmunk-mring-fs-v1" ‖ ring_hash ‖ ctx_hash ‖ msg_hash ‖ T ‖ C_b)` |

`ring_hash` is order-invariant: the verifier canonicalises the ring by
lex-sorting the qpacked `pk_i` bytes BEFORE hashing.

---

## 6. T block (1 408 bytes) [G2 v2.1 §1]

Single qpacked Chipmunk poly representing the link tag
`T = A_T · X ∈ R_q`, where `X = Σ_{i∈S} x_i ∈ R_q^{K_pk}` is the
aggregate witness over the secret signer subset `S`.

| offset | size  | layout    |
|-------:|------:|-----------|
|   156  | 1 408 | `T` qpack |

`A_T ∈ R_q^{K_pk}` is derived from `ring_hash` and `ctx_hash` via
`chipmunk_lrs_derive_A_I` (reused, not forked).  The relation
`T = chipmunk_lrs_relation_eval(A_T, X)` contracts a K_pk-vector
witness against a K_pk-vector matrix-row into a **single** R_q
polynomial — see G2 v2.1 §1 for the dimensional analysis.

---

## 7. C_b block (1 408 bytes)

Single qpacked poly: vector commitment to the binary indicator vector
`b ∈ {0,1}^N` together with its randomness `r_b`.

| offset | size  | layout      |
|-------:|------:|-------------|
|  1 564 | 1 408 | `C_b` qpack |

The commitment is `C_b = ⟨a, b⟩ + ⟨h, r_b⟩ mod q` where `a, h` are
public generators derived from `ring_hash` via `chipmunk_poly_uniform`.
N up to 256 fits in a single `R_q` poly (`n = 512` coefficients);
`b` is bit-encoded into the low N coefficients, high `512 − N`
coefficients are zero — the verifier checks this implicitly via the
fold relation.

---

## 7a. Y_pk block (1 408 bytes) [G2 v2.1 §1, §3, §4]

Prover-committed value claiming `Y_pk = A_pk · X = Σ_j A_pk[j] · X[j] ∈ R_q`
(single R_q poly — see G2 v2.1 §1 for the dimensional analysis;
`chipmunk_lrs_relation_eval` contracts `A_pk : R_q^{K_pk}` against
`X : R_q^{K_pk}` to one `R_q` element).  The bind block ties `Y_pk` to
the SAME `z_x` that binds `T = A_T · X`, so any malicious prover trying
to split the witness between the pk-side and the tag-side must break
MSIS over the stacked vector `[A_pk ‖ A_T] ∈ R_q^{2K_pk}` (≥ 3 297 bits
per G1).  Cross-binding from `Y_pk` to `b` rides inside the fold via
the `c³` coefficient of the unified statement (G2 v2.1 §3).

| offset | size  | layout      |
|-------:|------:|-------------|
|  2 972 | 1 408 | `Y_pk` qpack |

---

## 8. Fold tree (`fold_depth · 2 816 bytes`)

Bulletproof-style halving fold over the unified inner-product statement

  ⟨b, P(c)⟩  =  rhs(c)

where `P(c)` is the public vector-polynomial encoding (binary check,
Hamming weight `t`, aggregated-pk identity, tag binding) — see
amendment v2 §5.2 for the exact construction.

Per round `r ∈ [0, fold_depth)`:

| offset (rel.)            | size  | field      |
|-------------------------:|------:|------------|
|  `r · 2 816 + 0`         | 1 408 | `L_r` qpack |
|  `r · 2 816 + 1 408`     | 1 408 | `R_r` qpack |

After `fold_depth` rounds the vector is folded to length 2.

---

## 9. Final scalars (2 816 bytes)

Two qpacked polys — the final folded `a*, b* ∈ R_q`:

| offset (rel.) | size  | field |
|--------------:|------:|-------|
|   0           | 1 408 | `a*`  |
|   1 408       | 1 408 | `b*`  |

Verifier checks `⟨a*, b*⟩ == rhs(c_final)`.

---

## 10. Bind block (7 680 bytes) [G2 v2.1 §4]

| offset (rel.) | size  | field                                                          |
|--------------:|------:|----------------------------------------------------------------|
|   0           | 7 680 | `z_x[0]‖z_x[1]‖…‖z_x[5]` each zpacked (K_PK = 6 polys, 20 bit) |

`z_x = ρ_x + c*·X ∈ R_q^{K_pk}` is the aggregate witness response.
The SAME six polys appear in both verifier checks:

  A_pk · z_x = M_pk + c* · Y_pk     (one R_q equation)
  A_T  · z_x = M_T  + c* · T        (one R_q equation)

where `M_pk` and `M_T` are NOT on the wire — the verifier reconstructs
them as `M_pk := chipmunk_lrs_relation_eval(A_pk, z_x) − c*·Y_pk` and
analogously for `M_T`, then folds them back into the Fiat-Shamir
transcript to re-derive `c*` and accept iff it matches.  This is the
Schnorr-style same-witness binding closing DR-008 by construction.

No on-wire `Π_norm` (G2 v2 §A6).  The verifier unpacks `z_x` from the
zpack representation and recomputes `‖z_x‖∞`; if it exceeds `β_z = β_ρ`
the verifier rejects with `CHIPMUNK_RING_ERR_NORM_BOUND`.  Cost:
`O(n · K_PK) = O(3 072)` integer comparisons per signature.

---

## 11. Total size (G3.1 §8 — corrected; supersedes G2 v2.1 §5 & G3 §6.1)

> **SUPERSESSION.** The earlier G2 v2.1 §5 table (≈ 28.3 KB @ N=16,
> ≈ 39.3 KB @ N=256) assumed fold elements live in R_q.  The M4.0a finding
> (`MRNG_M4_INVERTIBILITY.md`) proved R_q fully splits, so the fold
> challenges/state were moved into the degree-e = 6 extension
> R_q^{(e)} = R_q[Y]/Φ₉ to obtain a 2¹²⁹·⁶ subtractive set
> (`MRNG_G3_1_EXTENSION_SOUNDNESS.md`, `MRNG_G3_1_NOGAP_LEMMA.md`).  The
> committed cross-terms (L, R) and the final base scalars that the fold
> commits to are now R_q^{(e)} elements (6× an R_q poly), while seed-
> derived openings stay 32 B.  The precise per-element byte table is
> **pinned in M4.0** once the RoK-2024 element accounting is transcribed;
> the honest *range* below is what the G3.1 design commits to.

The fixed, depth-independent part is unchanged from G2 v2.1:

```
fixed(N) = 28          // header
         + 128         // 4 hashes
         + 1 408       // T     (single R_q poly)
         + 1 408       // C_b
         + 1 408       // Y_pk  (single R_q poly)
         + 7 680       // bind block z_x = K_pk·zpack
         = 12 060 B    // structural lower bound, any N ≥ 2
```

The fold transcript is log-N in the number of rounds (D = 1 + ⌈log₂ N⌉ ≤ 9)
but each committed fold element is now an R_q^{(e)} element (≈ 6×1 408 B
before seed-compression), so the honest size estimate (G3.1 §8) is:

| N    | fold_depth D | honest total (est.) |
|-----:|-------------:|--------------------:|
|   16 | 5            | **≈ 45–60 KB**      |
|  256 | 9            | **≈ 70–95 KB**      |

This **misses** the Amendment v2 §5.1 targets (≤ 36 KB @ N=16,
≤ 48 KB @ N=256) — the targets are NOT met, a direct cost of the
fully-splitting ring requiring a degree-6 extension for true 128-bit
single-shot soundness.  It remains **far** below CRNG/v1 (786 KB @ N=16,
~12 MB @ N=256) and below the ≈175 KB naive 7× parallel-repetition
alternative, while growing logarithmically in N.  The exact bytes are
deferred to M4.0 (see the §8 honesty note in the G3.1 design doc).

---

## 12. Error code mapping (header validation)

| code                                       | trigger                                                       |
|--------------------------------------------|---------------------------------------------------------------|
| `CHIPMUNK_RING_ERR_NULL_PARAM`             | NULL buffer or NULL header destination                        |
| `CHIPMUNK_RING_ERR_BUFFER_TOO_SMALL`       | `buf_size < 28` or `< chipmunk_mring_wire_size(fold_depth)`   |
| `CHIPMUNK_RING_ERR_MAGIC_MISMATCH`         | `magic != 'MRNG'`                                             |
| `CHIPMUNK_RING_ERR_VERSION_MISMATCH`       | `version != 1`                                                |
| `CHIPMUNK_RING_ERR_PARAMS_MISMATCH`        | wrong `params_id`, reserved flags set, or fold_depth disagrees with `ceil(log2 N)` |
| `CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE`    | `N < 2` or `N > 256`                                          |
| `CHIPMUNK_RING_ERR_T_OUT_OF_RANGE`         | `t < 1` or `t > N`                                            |
| `CHIPMUNK_RING_ERR_NOT_IMPLEMENTED`        | header is well-formed but cryptographic core is M0/M1 stub    |

---

## 13. Hard gates blocking M3+ cryptography

* **G1** — MSIS estimator ≥ 128 bits.  ✅ CLOSED (3 297 bits).
* **G2** — Mathematical write-up of the unified statement, fold,
  simulator, soundness.  ✅ CLOSED via
  `documentation_2bac0e05b29ab6ad` (v1)
  + `documentation_045c56db2bce1cd6` (self-review with F1–F10)
  + `documentation_ae16ac5717c3ce9c` (v2, BINDING — split fold,
    augmented bind, counter-mode FS, dropped Π_norm).
  Plus code: invertibility estimator (≥ 980 bits) and MLWE estimator
  (≥ 128 bits) added to `chipmunk_mring_hardness.{c,h}`.
* **G3** — Simulator skeleton for the fold ZK proof (statistical
  distance `≤ 2^{−90}`).
* **G4** — Joint simulator for the bind block; byte-exact FS transcript
  pinned.
* **G5** — Grep-guard: no occurrences of `ntt`, `sample`, `expand` in
  `chipmunk_mring_*.c` outside of calls into `chipmunk_lrs_*` or
  `chipmunk_poly_*`.

Until G1 is closed the cryptographic core remains a stub returning
`CHIPMUNK_RING_ERR_NOT_IMPLEMENTED`; the header parser introduced in
M1 returns the precise error code above instead.
