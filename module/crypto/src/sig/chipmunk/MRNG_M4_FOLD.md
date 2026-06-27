# MRNG M4 — Halving Fold Implementation Design Lock (G3.1)

**Gate:** M4 (fold prove/verify over R_q^{(e)}).
**Status:** BINDING for `chipmunk_mring_fold.{h,c}`.
**Supersedes:** G3 §1.2 challenge sampling, G3 §6.1 wire-size table (fold section
only); G3.1 is authoritative for arithmetic domain and soundness.
**Depends on:** G2 v2.1 (Claim 1), G3.1 (extension + subtractive set),
M3.1/M3.2/M3.3, M4.0a/b (invert + Galois + relaxed MSIS).

---

## 1. Scope of M4.0 (this sprint)

Deliver in-memory fold prove/verify over the unified inner-product statement

    ⟨b̃, P̃(c)⟩ = ρ(c)   (REL-fold, embedded in R_q^{(e)})

with:

* Halving fold over **R_q^{(e)}** (G3.1 §4), not bare R_q.
* Subtractive-set challenges via `chipmunk_mring_ext_sample_challenge` (no
  nonce-bump retry — challenges are invertible by construction).
* Per-round cross-terms L_i, R_i stored as **ext scalars** in the transcript.
* Galois consistency lane: honest final witness b* satisfies σ(b*) = b*
  (`chipmunk_mring_ext_frobenius`, NOGAP §4.1).
* INV invariant checked at verify time for every round.

**M4.1 (this sprint):** wire pack/unpack of ext fold tree via
`chipmunk_mring_ext_qpack`, `chipmunk_mring_fold_write/read`.

**M4.2 (this sprint):** seed-compressed VCom openings (G3 §6.1 C1):
`fold_opening_seed` (32 B) + per-round `(C_L, C_R)` commitments; FS hashes
commitments, verifier opens via `chipmunk_mring_fold_derive_opening`.

**M4.3 (this sprint):** leaf-mask ω (G3 §6.1 C2):
49-bit pack (`LEAF_MASK_BYTES = 3136`), `chipmunk_mring_leaf_mask_*`;
prover sends β = b̃^{(D)} + ω, verifier unmasks for INV check.

**Deferred to M4.4+:** integration into `chipmunk_mring_sign`.

---

## 2. Padded dimension and fold depth

    aug_dim     = 2 · N
    padded_dim  = 2 · 2^{⌈log₂ N⌉}   (zero-pad high slots; does not change ρ)
    fold_depth  = log₂(padded_dim) = 1 + ⌈log₂ N⌉ = chipmunk_mring_fold_depth_for(N)

Witness b̃ and public P̃(c) are materialised in R_q (M3.2), embedded into
R_q^{(e)} via `chipmunk_mring_ext_embed`, then zero-padded to padded_dim.

---

## 3. One fold round (domain R_q^{(e)})

Split current vectors of length L into halves of length h = L/2:

    b̃ = (bL ‖ bR),   P̃ = (pL ‖ pR)

Cross-terms (ext inner products):

    L_i := ⟨bL, pR⟩ ∈ R_q^{(e)}
    R_i := ⟨bR, pL⟩ ∈ R_q^{(e)}

Fiat-Shamir challenge (subtractive set):

    x_i ← S ⊂ F_{q⁶}   via sample_challenge(round_fs_hash, 0)

Fold maps:

    b̃'[j]  = bL[j] + x_i · bR[j]
    P̃'[j]  = pL[j] + x_i⁻¹ · pR[j]
    ρ'     = ρ + x_i · L_i + x_i⁻¹ · R_i

INV preservation: ⟨b̃', P̃'⟩ = ρ' (standard Bulletproof identity).

---

## 4. Fiat-Shamir schedule (M4.0)

Per round r ∈ [0, fold_depth):

    round_fs_hash = SHAKE256( "MRNG-M4-fold-round-fs-v1"
                            ‖ fs_seed[32]
                            ‖ r (u32 LE)
                            ‖ serialise(L_r)
                            ‖ serialise(R_r) )

    x_r = sample_challenge(round_fs_hash, 0)
    x_r⁻¹ = scalar_invert(x_r)

Full transcript hash for sign/verify glue is G4; M4.0 uses fs_seed from the
wire header as the FS anchor.

---

## 5. Base case and verifier checks

After fold_depth rounds, length-1 vectors collapse to ext scalars a* = P̃^{(D)},
b* = b̃^{(D)}.  Verifier accepts iff:

1. Recomputed P̃^{(D)} from public inputs matches a* (public side check).
2. ⟨b*, a*⟩ = ρ^{(D)} (INV at base).
3. σ(b*) = b* (Galois consistency — witness in ι(R_q)).
4. chipmunk_mring_ext_is_in_base(b*).

Honest prover additionally has is_in_base(b*) by construction.

---

## 6. Wire format (M4.1 — DONE)

G3.1 §8: each committed L_r, R_r is an R_q^{(e)} element (e = 6 R_q polys),
packed as 6 consecutive qpacks (Y^0 .. Y^5) via `chipmunk_mring_ext_qpack`.

`CHIPMUNK_MRING_FOLD_ROUND_BYTES = 2 · EXT_QPACK_BYTES = 16 896`.

Exact totals: `28 956 + D · 16 896` bytes (see README_MRNG.md §11).

**M4.2 additions:** `fold_opening_seed` at
`chipmunk_mring_section_off_fold_opening_seed()`; fold rounds carry
`(C_L, C_R)` VCom commitments (same EXT_QPACK_BYTES as M4.1 raw slots).
Total: `28 988 + D · 16 896` bytes (+32 B vs M4.1).

**M4.3 additions:** leaf-mask ω at `chipmunk_mring_section_off_leaf_mask()`;
49-bit pack, `LEAF_MASK_BYTES = 3136`.  Total: `32 124 + D · 16 896` bytes.

---

## 7. API surface (`chipmunk_mring_fold.h`)

* `chipmunk_mring_fold_padded_dim(n_ring)`
* `chipmunk_mring_fold_proof_{alloc,free}`
* `chipmunk_mring_fold_prove(...)` — prover with secret b
* `chipmunk_mring_fold_verify(...)` — verifier, public inputs only

---

## 8. Test plan (`test_chipmunk_mring_fold.c`)

* T1: padded_dim / fold_depth formulas.
* T2: honest fold prove → verify PASS (N=4, t=2, multiple fs_seeds).
* T3: tampered L_r → verify FAIL.
* T4: tampered final b* → verify FAIL (consistency / INV break).

---

## 9. Open obligations (unchanged)

* **G4:** QROM Fiat–Shamir accounting — transcript pinned in
  `MRNG_G4_TRANSCRIPT.md`; implementation in `chipmunk_mring_transcript.c`.
* **M4.1:** wire pack/unpack + README size table pin.
* **M6/M7:** sign/verify integration.
