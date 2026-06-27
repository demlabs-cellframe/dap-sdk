# MRNG M4.0a — Ring-Splitting Finding (CRITICAL, blocks fold soundness)

**Gate:** M4.0a (pre-fold algebra check).
**Status:** ⚠ CRITICAL FINDING — supersedes G2 v2 §A3 and forces an
architectural decision before M4 (the halving fold) can proceed.
**Evidence:** `tests/unit/crypto/chipmunk_ring/test_chipmunk_mring_invert.c`
(empirical), `chipmunk_mring_statement.c::chipmunk_mring_poly_invert`
(implementation), `chipmunk_ntt.c` (root cause).

---

## §1. What was measured

`chipmunk_mring_poly_invert` computes x⁻¹ in R_q = Z_q[X]/(X⁵¹²+1) by
inverting each NTT coordinate mod q.  The empirical test sampled
**50 000 sparse-ternary challenges** (weight w = 37, the LRS C0
challenge distribution used everywhere in MRNG) and counted how many
are non-invertible:

```
8 / 50000 non-invertible      measured rate = 1.600e-04
full-split prediction n/q     =               1.616e-04
```

The measured rate matches the **fully-splitting** prediction
`n/q = 512 / 3 168 257` to three significant figures.

## §2. Root cause

The active Chipmunk NTT:
  - `chipmunk_ntt.c`: `zetas_len = 1024`, a COMPLETE size-512 negacyclic NTT
    (1024 | q−1 since 3 168 256 = 1024 · 3094).
  - `chipmunk_poly_mul_ntt`: PLAIN coefficient-wise `c[i] = a[i]·b[i] mod q`.

Therefore R_q **fully splits** into n = 512 linear factors over F_q:
  R_q  ≅  F_q × F_q × … × F_q     (512 copies, via CRT / NTT).

This CONTRADICTS the model used in G2 v2 §A3 and in
`chipmunk_mring_hardness_invertibility_bits()`, which assumed a
PARTIALLY-splitting cyclotomic (degree-4 CRT factors, giving the bogus
λ_inv ≈ 980 bits).  In a partially-splitting ring of factor degree d, a
short element is non-invertible with prob ≈ k·q^{−d}; for d = 4 that is
≈ 2⁻⁸¹.  But d = 1 here, so the real rate is ≈ n·q⁻¹ ≈ 2⁻¹²·⁶.

**G2 v2 §A3 and the `_invertibility_bits()` estimator are WRONG for this
ring and must be retired / corrected.**

## §3. Why this is more than an efficiency issue

### §3.1 Efficiency (minor, solvable)

A 2⁻¹²·⁶ non-invertibility rate (≈1 in 6 250) is harmless for the
prover’s per-round challenge: a deterministic, verifier-mirrored
FS-nonce-bump retry loop resamples x on the rare miss.  Expected retries
per round ≈ 1.0002; over D ≤ 9 rounds the overhead is negligible.

### §3.2 Soundness (CRITICAL)

The halving fold (Bulletproofs / MatRiCT+ inner-product argument) relies
on a Schwartz–Zippel-type argument: a cheating prover who commits to a
FALSE statement survives a round only if the random challenge x is a
root of a fixed degree-≤2 polynomial identity.

Over a **field** this fails for ≤ 2 of the |challenge-space| values, so
soundness per round ≈ 2 / |challenge space| — negligible because the
sparse-ternary space has > 2²⁵⁶ elements.

Over a **fully-splitting ring** the protocol decomposes by CRT into 512
INDEPENDENT scalar inner-product arguments, one per F_q slot.  A false
statement differs from a true one in ≥ 1 slot.  In that slot the
challenge’s projection x_slot ∈ F_q is (heuristically) ~uniform, and the
degree-2 identity fails for ≤ 2 of the q values.  Hence

   per-round, per-slot soundness  ≈  2 / q  ≈  2⁻²⁰·⁶
   over D = 9 rounds (union bound) ≈  2D / q  ≈  2⁻¹⁷·⁶.

**The single-shot fold therefore delivers only ≈ 17–18 bits of
soundness, NOT 128.**  The MSIS-binding of the round commitments
(λ ≈ 3297 bits) binds the committed VALUES but does NOT rescue the
algebraic soundness of the fold relation.

This is the well-known reason MatRiCT+ (Esgin–Steinfeld–Zhao 2022) and
related lattice IPAs work over **partially-splitting** rings (factor
degree d chosen so q^d is large): they need q^d, not q, in the
denominator.  Chipmunk’s q with full splitting cannot provide a
large-norm CRT slot, so the MatRiCT+ soundness does NOT transfer.

## §4. Resolution options (architectural decision required)

| # | Approach | Soundness | Size impact | Complexity | Notes |
|---|----------|-----------|-------------|------------|-------|
| **A** | **Parallel repetition** of the fold with κ independent FS challenge streams; accept iff all κ pass | 17.6·κ bits ⇒ κ = 8 for ≥ 128 | ×8 fold bytes (≈ +200 KB @N=256) | Low | Defeats the whole point — size explodes, back to CRNG-scale |
| **B** | **Compressed Σ-protocol theory for lattices** (Attema–Cramer–Kohl, Crypto 2021) — uses a non-CRT-decomposable challenge (Galois/uniform over R_q with invertible differences) so soundness is governed by the full ring, not per-slot | ≥ 128 bits | log-N, ≈ G3 target (≈ 42 KB @N=256) | **High** (research-grade; needs uniform-R_q challenges + exceptional-set argument) | Most correct, keeps log-N; large implementation effort |
| **C** | **Change the modulus/ring** for the fold to a partially-splitting q′ (factor degree d with q′^d ≥ 2¹²⁸), keep Chipmunk q for keys; bridge via an extra commitment | ≥ 128 bits | log-N | **Very high** | Two rings, cross-ring binding proof — heaviest, most invasive |
| **D** | **Abandon log-N fold; use a linear-size but compact Σ-protocol** (e.g. one-shot aggregated proof, size O(N) but with tiny constant) | ≥ 128 bits | O(N) — but possibly < CRNG | Medium | Gives up the headline log-N goal |
| **E** | **Soundness-boosted fold with a structured exceptional challenge set** (Lyubashevsky–Nguyen–Seiler “practical exact proofs” style: challenges from a monomial set {±X^i} whose differences are invertible AND whose evaluation hits a large subset per slot, combined with a small constant repetition κ≈2–3) | ≥ 128 bits | log-N × (2–3) (≈ 85–130 KB @N=256) | High | Middle ground; still overshoots the 48 KB target |

### §4.1 Assessment

- **A** is operationally simplest but reintroduces the very size blow-up
  that killed CRNG/v1 — rejected on the user’s standing size mandate.
- **B** is the “hardest, longest, most correct” path: it preserves the
  log-N size target AND reaches 128-bit soundness, at the cost of a
  research-grade implementation (uniform-R_q challenge sampling, an
  exceptional-set / invertible-difference argument over the full ring,
  and an adapted extractor).  This aligns with the user’s repeated
  directive to take the most correct path.
- **C** is even heavier (dual-ring) and offers no size advantage over B.
- **D** abandons the headline goal; only a fallback.
- **E** is a pragmatic compromise but still misses the 48 KB target and
  carries most of B’s complexity without B’s cleanliness.

## §5. Recommendation

Adopt **Option B (Compressed Σ-protocol theory for lattices,
ACK21-style)** as the corrected fold foundation:

  1. Replace the sparse-ternary fold challenge with a UNIFORM-over-R_q
     challenge restricted to an exceptional set E ⊂ R_q where every
     non-zero difference is invertible (Lyubashevsky–Seiler style
     “fully-splitting-safe” set, or a Galois-orbit construction).
  2. Re-derive the fold soundness from the ACK21 amortized argument,
     whose soundness is governed by |E| (≥ 2¹²⁸ achievable) rather than
     by per-slot q.
  3. Keep everything else from G3 (the recursion structure, the
     simulator skeleton, the bind composition) — only the challenge
     space and the soundness lemma change.

This keeps the log-N size (≈ 42 KB @N=256) and reaches the 128-bit
soundness floor, at the price of the most involved implementation.
It supersedes:
  - G2 v2 §A3 (invertibility analysis — wrong ring model),
  - `chipmunk_mring_hardness_invertibility_bits()` (to be replaced with
    an exceptional-set size estimator),
  - G3 §1.2 / §4.2 (challenge sampling and soundness — to be re-derived).

## §6. Immediate consequences for the codebase

- `chipmunk_mring_poly_invert` (M4.0a) is CORRECT and stays — Option B
  still needs R_q inversion (more often, since uniform-R_q challenges
  are also occasionally non-invertible, at the same ≈n/q rate).
- The G1 hardness test’s invertibility assertion (≈980 bits) must be
  re-pointed at the exceptional-set size, not the partial-splitting
  formula.  Until Option B’s estimator lands, that sub-assertion is
  marked KNOWN-STALE here (NOT silently passing — see §7).
- No fold code is written until the Option B challenge/soundness
  redesign (a new gate, call it **G3.1**) is locked.

## §8. ADDENDUM — ACK21 size correction (refutes §4 Option B size claim)

After the user selected Option B, a closer read of ACK21 (Attema–Cramer–
Kohl, *A Compressed Σ-Protocol Theory for Lattices*, Crypto 2021,
eprint 2021/307) forces a correction to the §4 table.

**ACK21’s actual mechanism.**  ACK21 does NOT remove the small-challenge
problem; it provides a *tight* extractor showing
(k₁,…,k_μ)-special-soundness implies knowledge error

   κ  ≈  Σ_i (k_iʹ) / |C|   ≈  2 · log₂(2N) / |C|

for the μ = log₂(2N)-round compressed fold (k_i = 3 per round).  Crucially
ACK21 STILL relies on **parallel repetition** to drive κ to 2⁻¹²⁸ when the
challenge set C is poly-small — its contribution is making that repetition
*tight* (fewer repeats than the naïve 8.16·log n/|C| bound), not removing it.

**The exceptional-set cap on THIS ring.**  In a fully-splitting ring
R_q ≅ F_q^512, a set with all pairwise-invertible differences must differ
in *every* CRT slot.  The maximum such set is the diagonal of constant
polynomials, of size exactly **|C| = q ≈ 2²¹·⁶** (an MDS/repetition-code
bound — you cannot beat q distinct values per slot in all slots at once).

**Resulting size.**  With |C| = q and N = 256 (μ = 9):

   κ_single  ≈  2·9 / 2²¹·⁶  ≈  2⁻¹⁷·⁴
   repetitions t to reach 2⁻¹²⁸:   t ≥ 128 / 17.4  ⇒  t = 8.

Eight parallel fold transcripts (≈ 25 KB each at N = 256) ⇒ **≈ 200 KB**,
i.e. Option B **collapses into Option A** for this ring.  **The earlier
§4 claim that Option B keeps ≈ 42 KB is WRONG** and is retracted here.

**Fundamental conclusion.**  Over Chipmunk’s ring (small q ≈ 2²¹·⁶, FULL
splitting) NO exceptional-set IPA fold can achieve log-N size *and*
128-bit soundness: the per-slot field is only F_q, capping |C| at q, so
≈ 8× repetition is unavoidable.  Hitting the ≤ 48 KB target at 128-bit
soundness therefore REQUIRES a ring whose factor degree δ gives
(q)^δ ≥ 2¹²⁸ (MatRiCT+ uses exactly this: q ≈ 2³², δ = 4 ⇒ 2¹²⁸).

**Corrected viable paths (supersede §4/§5):**

  - **C′ (size-preserving, hardest):** run the fold over a *MatRiCT+-class
    ring* R\* = Z_{q\*}[X]/(Φ) chosen so X-split factor degree δ satisfies
    (q\*)^δ ≥ 2¹²⁸ (e.g. q\* ≈ 2³², δ = 4).  Chipmunk keys/witnesses are
    bridged into R\* via a binding commitment + a cross-ring norm proof.
    True log-N (~42 KB), 128-bit soundness.  Gives up “Chipmunk-native
    fold arithmetic” (keys stay Chipmunk; the *proof* ring changes).
  - **A/B-rep:** stay in Chipmunk ring, 8× parallel repetition, ~200 KB.
    128-bit, but far over the size target (still < CRNG’s 786 KB).
  - **D:** O(N) compact Σ-protocol in the Chipmunk ring (no fold), size
    O(N) with a small constant.

This addendum is itself a §7-style honesty disclosure: the size premise
under which Option B was chosen does not hold for this ring, so the choice
must be revisited before any fold code is written.

## §7. Honesty note (per core_ai_behavior_correction)

This finding was surfaced the moment the empirical data contradicted the
recorded design (G2 v2 §A3).  It is NOT deferred as a “known issue”: the
fold implementation is explicitly BLOCKED until the soundness gap is
closed by a locked Option B (G3.1) redesign.  The stale
`_invertibility_bits()` estimator and the G2 §A3 claim are flagged for
correction, not left to silently mislead later gates.

## §8. Second-order correction (post-literature review) — supersedes §4/§5 size claims

A follow-up review of the lattice-IPA literature (Lyubashevsky–Seiler
2018; Attema–Cramer–Kohl, Crypto 2021; RoK-Paper-SISsors, Asiacrypt
2024) corrects an over-optimistic claim made in §4/§5 above:

  • **The challenge/exceptional ("subtractive") set must satisfy three
    properties**: size ≈ 2²⁵⁶, small norm, and invertible pairwise
    differences (Lyubashevsky–Seiler).  Over a FULLY-splitting ring the
    maximum subtractive set has size ≤ q ≈ 2²¹·⁶ (the constant-polynomial
    diagonal) — far short of 2¹²⁸.

  • **ACK21 knowledge error** is κ ≈ 2·log(n)/|C|.  With |C| ≤ q ≈
    2²¹·⁶, a single-shot fold gives only ≈ 2⁻¹⁷ — so ACK21 over the bare
    Chipmunk ring would STILL need ≈ 7× parallel repetition (≈ 7 × 25 KB
    ≈ 175 KB).  The §5 claim that “Option B keeps ≈ 42 KB” was therefore
    WRONG for the bare ring.

  • **Canonical fix (ACK21 §“small rings”, RoK 2024)**: when R_q lacks a
    large subtractive set, define the secret-sharing / fold challenges
    over a **ring EXTENSION** R_q^{(e)} = R_q[Y]/(g(Y)) of degree e, in
    which a subtractive set of size ≈ qᵉ exists.  Choosing e ≥
    ⌈128 / log₂ q⌉ = ⌈128/21.6⌉ = 6 yields |C| ≥ 2¹²⁸ and a
    **single-shot, no-gap, log-N** fold — no parallel repetition.
    Consistency between R_q (where keys/commitments live) and R_q^{(e)}
    is proven via the CRT/automorphism-decomposition argument (RoK 2024).

  • **Corrected Option B** is therefore “ACK21/RoK fold over a degree-6
    ring extension with subtractive-set challenges”, NOT “sparse-ternary
    fold + exceptional set in R_q”.  The detailed locked design is
    `MRNG_G3_1_EXTENSION_SOUNDNESS.md` (G3.1).

  • **Honest size note**: the extension makes challenge elements and a
    bounded number of auxiliary transcript elements e× larger, but the
    transcript stays polylogarithmic in N.  The realistic size is
    therefore BETWEEN the naive 42 KB (wrong) and the 175 KB repetition
    blow-up — pinned precisely in G3.1 once the RoK-2024 split/fold/
    norm-check element accounting is transcribed during the M-sprints.
