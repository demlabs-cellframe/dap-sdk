# MRNG G3.1 — Fold Soundness over a Ring Extension (Option B, corrected)

**Gate:** G3.1 (replaces G3 §1.2 challenge sampling and §4 soundness;
hard precondition for M4).
**Decision:** User selected **Option B** (ACK21 compressed Σ-protocol
over lattices).  This document locks the CORRECTED Option B, grounded in
the lattice-IPA literature, after the M4.0a finding proved the Chipmunk
ring fully splits (`MRNG_M4_INVERTIBILITY.md`).
**Status:** GATE SATISFIED — all §9 exit-checklist items (§9.1–§9.6) are
DONE (arithmetic + sampler implemented & tested, no-gap lemma written,
estimator re-pointed, size table corrected).  M4 fold code is unblocked,
subject to the §9.4 honesty-ledger obligations carried into M4.0/G4.

**Primary sources**
  - Lyubashevsky & Seiler, *Short, Invertible Elements in Partially
    Splitting Cyclotomic Rings*, Eurocrypt 2018 (challenge-set
    properties; invertibility).
  - Attema, Cramer & Kohl, *A Compressed Σ-Protocol Theory for
    Lattices*, Crypto 2021 (knowledge error κ ≈ 2·log n/|C|; tight
    (k₁,…,k_μ)-special-soundness extractor; ring-extension linearization
    for small rings).
  - *RoK, Paper, SISsors — Toolkit for Lattice-Based Succinct
    Arguments*, ePrint 2024/1972 (split/fold/norm-check reductions with
    NO soundness gap over rings admitting subtractive sets; CRT ↔
    coefficient consistency via automorphism arguments).

---

## §1. The obstacle (recap, now rigorous)

Chipmunk's ring R_q = Z_q[X]/(X⁵¹²+1) FULLY splits into 512 linear
factors (M4.0a, empirically confirmed: non-invertibility rate 8/50 000 ≈
n/q).  Three consequences:

  1. A challenge/subtractive set C ⊂ R_q with invertible pairwise
     differences has |C| ≤ q ≈ 2²¹·⁶ (the constant-polynomial diagonal
     is maximal; any larger set has two elements agreeing in some CRT
     slot, whose difference is a zero divisor).
  2. ACK21 knowledge error κ ≈ 2·log(n)/|C| ⇒ with |C| ≤ q, single-shot
     κ ≈ 2⁻¹⁷.
  3. Reaching 2⁻¹²⁸ by parallel repetition needs ≈ 7× → ≈ 175 KB,
     defeating the log-N size goal.

## §2. The fix — challenges from a degree-e ring extension

Following ACK21 (“small rings” linearization) and RoK 2024 (subtractive
sets over field/ring extensions):

Define the extension
   R_q^{(e)} := R_q[Y] / (g(Y)),     deg g = e,
where g(Y) is chosen so that R_q^{(e)} is a field extension of each CRT
slot F_q (equivalently, g is irreducible modulo every slot — e.g. g(Y)
irreducible over F_q so that each slot becomes F_{qᵉ}).  Then:

  • R_q^{(e)} ≅ (F_{qᵉ})⁵¹²  (each fully-split slot is lifted to the
    degree-e extension field F_{qᵉ}).
  • A subtractive set S ⊂ R_q^{(e)} of size qᵉ exists (the diagonal of
    constants from F_{qᵉ}, i.e. {a·1 : a ∈ F_{qᵉ}} under a fixed F_q-
    basis), with ALL nonzero differences invertible in R_q^{(e)}.
  • Choose e = 6: qᵉ ≈ (2²¹·⁶)⁶ ≈ 2¹²⁹·⁶ ≥ 2¹²⁸ ⇒ |S| ≥ 2¹²⁸.

Per-slot Schwartz–Zippel now runs over F_{qᵉ}: a nonzero degree-2 fold
identity fails for ≤ 2 of the qᵉ ≥ 2¹²⁸ challenge values, so

   per-round soundness  ≤  2·D / |S|  ≤  2·9 / 2¹²⁸  ≈ 2⁻¹²³·⁸,

a SINGLE-SHOT, no-repetition, 128-bit-class soundness.  (ACK21's tight
extractor tightens the constant further; RoK 2024 gives the no-gap
variant.)

## §3. Concrete extension parameters (to pin in M4.0)

| symbol | meaning | value |
|--------|---------|-------|
| e | extension degree | 6 |
| g(Y) | extension polynomial, irreducible over F_q | **Y⁶ + Y³ + 1 = Φ₉(Y)** (PINNED, §3.1) |
| \|S\| | subtractive-set size | qᵉ ≈ 2¹²⁹·⁶ |
| R_q^{(e)} element | e coefficients in R_q | 6 × chipmunk_poly_t |

### §3.1 Choice of g(Y) = Φ₉(Y) = Y⁶ + Y³ + 1 (PINNED, verified)

Selected the 9th cyclotomic polynomial Φ₉(Y) = Y⁶ + Y³ + 1.  Two
independent confirmations of irreducibility over F_q (q = 3 168 257):

  • **Cyclotomic criterion.**  Φ₉ is irreducible over F_q iff
    ord_9(q) = φ(9) = 6.  q ≡ 5 (mod 9) (digit-sum 32 → 5), and the
    multiplicative order of 5 mod 9 is 6 (5,7,8,4,2,1).  ⇒ irreducible.

  • **Rabin irreducibility test** (standalone search, Frobenius
    square-and-multiply + gcd): `Y⁶+Y³+1` passes both Rabin conditions;
    the sanity case `Y⁶−1` correctly tests reducible.

Why Φ₉ is the right pick:
  • Cyclotomic ⇒ F_{q⁶} = F_q[Y]/(Φ₉) carries a CYCLIC Galois group of
    order 6 (Frobenius Y ↦ Y�q acts as a power map on the primitive 9th
    roots of unity).  This is exactly the structure the RoK-2024
    CRT/automorphism consistency argument (§7) consumes.
  • SPARSE trinomial ⇒ reduction mod g costs few R_q operations
    (only the Y⁶→−Y³−1 and Y⁷→−Y⁴−Y, … rewrites).
  • Coefficients ∈ {0,1} ⇒ no extra multiplications in the reduction.

Note: q − 1 = 2¹¹·7·13·17 is NOT divisible by 3, so binomials Yⁿ−a are
never irreducible of degree 6 over F_q (Lidl–Niederreiter Thm 3.75
needs 3 | ord(a) | q−1); the cyclotomic trinomial sidesteps this.

A reproducible irreducibility unit test (Rabin) is required by §9.2 so
the choice is re-verifiable in CI, not just asserted here.

An element of R_q^{(e)} is represented as `chipmunk_poly_t coeff[e]`
(an R_q-polynomial in Y of degree < e).  Multiplication in R_q^{(e)} is
schoolbook in Y (e² R_q-mults) reduced mod g(Y); since e = 6 this is 36
R_q-multiplications per extension-mult — acceptable (fold has O(N)
extension-mults total, all NTT-backed).

## §4. Modified halving fold over R_q^{(e)}

The G3 §1 recursion structure is UNCHANGED; only the arithmetic domain
of the CHALLENGES and the running fold state changes:

  • Witness b̃ and public P̃ start in R_q (lifted to R_q^{(e)} via the
    natural embedding R_q ↪ R_q^{(e)}, Y-degree 0).
  • Fold challenge xᵢ ← S ⊂ R_q^{(e)} (subtractive set; sampled by
    Fiat-Shamir, §6).
  • Fold maps (unchanged form, now over R_q^{(e)}):
        b̃^{(i+1)} = bL + xᵢ · bR
        P̃^{(i+1)} = pL + xᵢ⁻¹ · pR
        ρ^{(i+1)} = ρ^{(i)} + xᵢ·Lᵢ + xᵢ⁻¹·Rᵢ
  • xᵢ⁻¹ computed in R_q^{(e)} (§5).  Non-invertibility (a subtractive-
    set element is invertible by construction, so the only failures come
    from the FS sampler landing OUTSIDE S — which it never does by
    construction; thus NO retry loop is needed for the challenge itself,
    a structural improvement over G3 §1.2).

The leaf-mask (G3 §3.3) and the per-round commitment compression (G3
§6.1 C1, seed-based openings) carry over unchanged, now over R_q^{(e)}.

## §5. Inversion in R_q^{(e)}

For x ∈ R_q^{(e)} represented as a degree-<e polynomial in Y over R_q,
x is invertible iff it is invertible in every CRT slot's F_{qᵉ}.  Since
challenges are drawn from the subtractive set S (constants from F_{qᵉ}
embedded diagonally), they are invertible BY CONSTRUCTION; their
inverses are likewise diagonal constants and reduce to per-slot F_{qᵉ}
inversion.  Implementation reuses `chipmunk_mring_poly_invert` (M4.0a)
slot-wise within an F_{qᵉ} inversion via extended Euclid in F_q[Y]/(g).

(The general R_q^{(e)} inversion — needed only if non-diagonal extension
elements ever require inverting — is extended-Euclid in F_q[Y]/(g)
composed with the M4.0a per-slot inverse.  Challenges never need it.)

## §6. Fiat-Shamir over the subtractive set

  • H_FS absorbs the round transcript and squeezes a uniform index into
    S; because |S| = qᵉ ≥ 2¹²⁸, a uniform draw needs ⌈e·log₂ q⌉ ≈ 130
    bits ⇒ SHAKE256 with ≥ 384-bit output (matches G3 §7 FS_OUT_BITS).
  • Counter-mode FS (G2 v2 §A4) and per-round domain separators carry
    over.
  • No non-invertibility retry (subtractive-set elements are invertible
    by construction), removing G3 §1.2 / §6.2-M2's nonce-bump rule.

## §7. R_q ↔ R_q^{(e)} consistency (no soundness gap)

The keys, the bind block (M3.3), and the vector commitment (M3.1) all
live in R_q.  The fold runs in R_q^{(e)}.  We must prevent a malicious
prover from exploiting extension-only freedom (ACK21's “prevent
dishonest provers from choosing secret elements in the extension ring”).

Mechanism (RoK 2024 CRT-consistency, adapted):
  • The witness embedding R_q ↪ R_q^{(e)} is the Y-degree-0 inclusion;
    honest b̃, P̃ have zero Y-degree-≥1 components.
  • The verifier checks (via the final base relation and the bind block)
    that the recovered witness lies in the R_q sub-ring (Y-components
    1..e-1 are zero).  This is a linear constraint over F_q, foldable
    into the same inner-product statement (one extra public lane), so it
    adds NO extra rounds.
  • The Galois/automorphism argument (RoK 2024) gives a succinct proof
    that the extension embedding is consistent with the R_q CRT encoding
    without a soundness gap.

## §8. Honest size & complexity estimate

  • Transcript stays log-N (D ≤ 9 rounds).  Per-round commitments and the
    final scalars are now R_q^{(e)} elements (e = 6) where they were R_q.
  • Rough upper bound: fold bytes ≈ e × (G3 seed-compressed fold) plus
    the consistency lane.  Using G3 §6.1's seed-compressed 25.3 KB @
    N=256 as the R_q baseline, the extension fold is ≈ 6 × (per-round
    qpack part only) — but only the COMMITTED cross-terms (L,R) and the
    base scalar are in R_q^{(e)}; the seed-derived openings stay 32 B.
    Realistic estimate: **≈ 70–95 KB @ N=256, ≈ 45–60 KB @ N=16**.
  • This MISSES the original ≤ 48 KB @ N=256 / ≤ 36 KB @ N=16 G2 v2.1
    targets, but is FAR below CRNG/v1 (786 KB @ N=16) and below the
    175 KB naive-repetition blow-up, while reaching TRUE 128-bit
    soundness with log-N scaling.  Exact constants are pinned in M4.0
    once the RoK-2024 element accounting is transcribed.
  • Complexity: extension arithmetic (e²=36 R_q-mults per ext-mult) and
    the consistency lane are the new implementation burden.

## §9. G3.1 exit checklist (must hold before M4 fold code)

  1. ☑ Pick & verify g(Y): **DONE** — g(Y) = Φ₉(Y) = Y⁶+Y³+1, verified
     irreducible over F_q by both the cyclotomic criterion (ord_9(q)=6)
     and Rabin's test (§3.1).  Coefficients to be stored as M4.0
     constants (sparse: only Y⁶, Y³, Y⁰ terms, all = 1).
  2. ☑ Implement R_q^{(e)} arithmetic: **DONE** —
     `module/crypto/src/sig/chipmunk/chipmunk_mring_ext.{h,c}`
     (`chipmunk_mring_ext_*`: zero/one, embed/project/is_in_base, add,
     sub, mul mod Φ₉ (Y⁶≡−Y³−1), scalar F_{q⁶} set/get/invert via
     extended Euclid in F_q[Y]/Φ₉, general per-slot F_{q⁶} invert via
     NTT, plus a CI Rabin self-check `_modulus_is_irreducible()`).
     Tests: `tests/unit/crypto/chipmunk_ring/test_chipmunk_mring_ext.c`
     (T0 Rabin irreducibility, T1 ring axioms comm/assoc/distrib + both
     identities, T2 scalar x·x⁻¹=1 / zero→-EDOM / non-scalar→-EINVAL,
     T3 general per-slot x·x⁻¹=1, T4 embed∘project=id, is_in_base,
     ring-hom embed(a)·embed(b)=embed(a·b)) — all PASS (≈0.2 s).
     Note: R_q^{(e)} is NOT a field (R_q has zero divisors); it is
     ≅ (F_{q⁶})⁵¹², so only the scalar (diagonal F_{q⁶}) challenge set
     and overwhelmingly-likely random elements are invertible — exactly
     what the fold needs (§5).
  3. ☑ Implement subtractive-set FS challenge sampler: **DONE** —
     `chipmunk_mring_ext_sample_challenge()` (domain-separated SHAKE256
     XOF over fs_hash‖counter, 22-bit rejection sampling per F_q
     coordinate, all-zero scalar rejection-resampled ⇒ output always
     nonzero/invertible).  Test
     `tests/unit/crypto/chipmunk_ring/test_chipmunk_mring_subtractive.c`:
     T1 determinism + scalar/nonzero/invertible, T2 pairwise distinctness,
     T3 SUBTRACTIVE — all 19 900 pairwise differences over a 200-challenge
     batch are invertible (11 spot-checked by full R_q^{(e)} multiply
     diff·diff⁻¹=1) — PASS (≈0.4 s).
  4. ☑ Transcribe the RoK-2024 no-gap soundness lemma: **DONE** —
     `MRNG_G3_1_NOGAP_LEMMA.md`.  States R_fold; proves per-round
     3-special-soundness via a Vandermonde extractor over S (invertible
     precisely because S is subtractive); cites AC20/ACK21 for
     κ_round ≤ 2/|S| and the tree extractor for
     κ_total ≤ D·2/|S| ≈ 2⁻¹²⁵·⁴ (single-shot, e=6, q, D≤9); shows the
     norm-check lane and the R_q↔R_q^{(e)} consistency lane (binding +
     Galois trace, g=Φ₉ cyclotomic ⇒ cyclic order-6 Frobenius) add NO
     challenge-dependent κ.  §6 honesty ledger pins three open
     obligations carried into M4.0/G4 (Galois-trace opening code; MSIS
     re-check at relaxed β*=2^D·β; QROM FS accounting).
  5. ☑ Re-point `chipmunk_mring_hardness_invertibility_bits()`: **DONE** —
     now returns log₂|S| = log₂(qᵉ−1) ≈ e·log₂ q ≈ **129** bits (e=6),
     replacing the stale ≈980 partial-splitting formula; doc comment in
     `chipmunk_mring_hardness.{c,h}` records the supersession.  Test
     `test_chipmunk_mring_hardness.c` updated with a tight [128,131]
     window that explicitly trips if the 980-bit model ever returns —
     PASS (reports 129).
  6. ☑ Pin the corrected size table (§8) in README_MRNG.md: **DONE** —
     README_MRNG.md §11 rewritten with a SUPERSESSION note retiring the
     G2 v2.1 §5 / G3 §6.1 tables; keeps the 12 060 B depth-independent
     floor, gives the honest G3.1 range (≈45–60 KB @ N=16,
     ≈70–95 KB @ N=256), and states plainly that the Amendment v2 §5.1
     targets are NOT met (cost of the degree-6 extension), while still
     ≪ CRNG/v1 and ≪ the 175 KB repetition alternative.  Exact bytes
     deferred to M4.0.

**G3.1 GATE: all six exit-checklist items satisfied (§9.1–§9.6).**
M4 fold code is unblocked, subject to the §9.4 honesty-ledger
obligations carried into M4.0 (Galois-trace consistency opening; MSIS
re-check at relaxed β*=2^D·β) and G4 (QROM FS accounting).

## §10. Supersession summary

| Prior claim | Status |
|-------------|--------|
| G2 v2 §A3 λ_inv ≈ 980 bits (partial-splitting model) | **WRONG**, retired |
| `_invertibility_bits()` ≈ 980 | **STALE**, to be re-pointed (§9.5) |
| G3 §1.2 sparse-ternary fold challenge + nonce-bump retry | replaced by subtractive-set over R_q^{(e)} (§4, §6) |
| G3 §4 per-slot soundness ≈ 2⁻¹⁷ | replaced by §2 single-shot ≈ 2⁻¹²³·⁸ |
| G3 §6.1 / G2 v2.1 size ≈ 42 KB @ N=256 | replaced by §8 ≈ 70–95 KB @ N=256 |
| “Option B keeps ≈ 42 KB” (M4 finding §5) | corrected by M4 finding §8 + this doc §8 |
