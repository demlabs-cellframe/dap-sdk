# MRNG G3.1 §9.4 — No-Gap Fold Soundness Lemma (RoK-2024, instantiated)

**Gate:** G3.1 §9.4 (companion to `MRNG_G3_1_EXTENSION_SOUNDNESS.md`).
**Purpose:** Transcribe the RoK-2024 split / fold / norm-check no-soundness-
gap statement into an MRNG-specific knowledge-soundness lemma with the
pinned numbers (e = 6, q = 3 168 257, D ≤ 9) and the explicit
R_q ↔ R_q^{(e)} consistency lane. This is the theory deliverable that
must hold *before* any M4 fold code is written.

**Sources** (see also the parent doc §0):
  - **ACK21** — Attema, Cramer, Kohl, *A Compressed Σ-Protocol Theory for
    Lattices*, Crypto 2021. Tight (k₁,…,k_μ)-special-soundness ⇒
    knowledge-error theorem; ring-extension linearization for small rings.
  - **RoK 2024** — *RoK, Paper, SISsors*, ePrint 2024/1972. Split/fold/
    norm-check reductions of knowledge with **no soundness gap** over
    rings carrying a subtractive set; CRT ↔ coefficient consistency by
    automorphism (Galois) arguments.
  - **AC20** — Attema, Cramer, *Compressed Σ-Protocol Theory…*, Crypto
    2020 (the tree-of-transcripts extractor lemma reused below).

---

## §1. Setting and notation

Recall (parent doc §2–§5):

  - R_q = Z_q[X]/(X⁵¹²+1), q = 3 168 257, which **fully splits**:
    R_q ≅ F_q⁵¹² (M4.0a).
  - R_q^{(e)} = R_q[Y]/(g(Y)), g = Φ₉ = Y⁶+Y³+1, **e = 6**, irreducible
    over every CRT slot ⇒ R_q^{(e)} ≅ (F_{qᵉ})⁵¹² (a product of 512
    copies of the field F_{q⁶}).
  - **Subtractive set** S = { a·1 : a ∈ F_{q⁶} } \ {0}, the diagonal of
    F_{q⁶}-constants embedded into all 512 slots, with
        |S| = qᵉ − 1 ≈ 2¹²⁹·⁶,
    and the defining property: **for all distinct s, s′ ∈ S ∪ {0},
    (s − s′) is invertible in R_q^{(e)}** (it is a nonzero element of the
    field F_{q⁶} in every slot). Implemented and tested in §9.2/§9.3.
  - Embedding ι : R_q ↪ R_q^{(e)} is the Y-degree-0 inclusion; it is an
    injective ring homomorphism (tested: `embed(a)·embed(b)=embed(a·b)`),
    with retraction π = `project` (π∘ι = id_{R_q}, tested).

The fold operates on the post-bind inner-product statement (parent §4,
G3 §1). Let the augmented witness/public vectors have length
m = 2^D (zero-padded), D ≤ 9, with all entries lifted into R_q^{(e)} via
ι. Write the statement as

  **R_fold** : ∃ b̃ ∈ (R_q)^m (lifted), with ‖b̃‖ ≤ β, such that
      (i)   ⟨b̃, P̃⟩ = ρ                          (inner product, over R_q^{(e)})
      (ii)  b̃ ∈ ι(R_q)^m                          (consistency: no Y-degree ≥1 part)
      (iii) Com(b̃) = C_b                          (binding commitment, M3.1, over R_q)

where P̃, ρ, C_b are public, and ‖·‖ is the coefficient ∞-norm in R_q
(the Y-degree-0 part; (ii) forces the higher Y-parts to 0).

---

## §2. One fold round as a reduction of knowledge

A round splits each length-m vector into halves (L = first m/2, R = last
m/2) and folds with a challenge x ∈ S:

  b̃′ = b̃_L + x·b̃_R                  ∈ (R_q^{(e)})^{m/2}
  P̃′ = P̃_L + x⁻¹·P̃_R               ∈ (R_q^{(e)})^{m/2}
  ρ′ = ρ + x·L + x⁻¹·R               ∈ R_q^{(e)}

where the prover first sends the cross-terms
  L = ⟨b̃_R, P̃_L⟩,   R = ⟨b̃_L, P̃_R⟩            (the only new committed data),
then the verifier sends x ← S, then both compute (b̃′,P̃′,ρ′). The folded
inner product expands as

  ⟨b̃′, P̃′⟩ = ⟨b̃_L,P̃_L⟩ + x⁻¹⟨b̃_L,P̃_R⟩ + x⟨b̃_R,P̃_L⟩ + ⟨b̃_R,P̃_R⟩
            = ρ + x·L + x⁻¹·R                                          (★)

so an honest prover satisfies ρ′ = ⟨b̃′,P̃′⟩ identically. (★) is, after
clearing x, a **degree-2 Laurent identity in x** (degree D_round = 2 in
the numerator x²·(·)); equivalently a degree-2 polynomial identity in x.

### §2.1 Round special-soundness (extraction)

**Claim.** From **3** accepting transcripts with pairwise-distinct
challenges x₁, x₂, x₃ ∈ S (same L, R, same first message), one extracts a
valid folded witness for the parent statement.

*Argument.* Multiply (★) by x and view it as the quadratic
  F(x) := x·⟨b̃_L,P̃_L⟩ + x²·⟨b̃_R,P̃_L⟩ + ⟨b̃_L,P̃_R⟩ − x·ρ − x²·L − R = 0.
Three accepting (xᵢ, b̃′ᵢ) give three evaluations of the *same* quadratic
whose coefficient vectors are the unknowns ⟨b̃_·,P̃_·⟩. The 3×3
Vandermonde-type matrix in (1, xᵢ, xᵢ²) is invertible **iff all pairwise
differences (xᵢ − x_j) are invertible** — which holds because
x₁,x₂,x₃ ∈ S and S is subtractive. Inverting it recovers b̃_L, b̃_R (hence
b̃) and the cross terms, i.e. a witness for the pre-fold statement. ∎

Thus each round is **3-special-sound** over S, and crucially the
extractor’s linear-algebra step **never divides by a non-invertible
element** — this is precisely the property a fully-splitting ring lacks
for short challenges, and which the extension S restores.

### §2.2 Round knowledge error

By the AC20/ACK21 special-soundness ⇒ knowledge-error lemma, a
k-special-sound round over challenge space C has knowledge error
(k−1)/|C|. Here k = 3, C = S, so

  κ_round ≤ (k − 1)/|S| = 2 / (qᵉ − 1) ≈ 2 / 2¹²⁹·⁶ ≈ 2⁻¹²⁸·⁶.

(The Schwartz–Zippel reading of parent §2 gives the equivalent bound
2·D_round/|S| = 4/|S|; we keep the tighter (k−1)/|S| from the explicit
Vandermonde extractor. Both are ≤ 2⁻¹²⁷.)

---

## §3. The norm-check lane

The fold preserves the inner-product relation but a cheating prover could
fold a witness of inflated norm. RoK 2024 attaches a **norm-check
reduction** to each round; in MRNG this is the bind-block bound (M3.3)
plus the per-round response-norm gate already specified in G3 §3.3:

  - The base-level relation (after D rounds the statement is length 1) is
    verified by the M3.3 bind block, whose response gate
    ‖z_x‖ < CHIPMUNK_MRING_RESPONSE_BOUND enforces a concrete norm bound
    on the extracted base witness.
  - Folding with x ∈ S multiplies coefficient norms by at most the
    operator norm of multiplication-by-x in R_q^{(e)}. Because challenges
    are **constant (Y-degree-0) diagonal scalars** in the canonical
    representative set, mult-by-x acts as scalar multiplication by an
    F_q-element in each slot; the *honest* relaxation factor over D rounds
    is the standard 2^D soundness-slack of Bulletproofs-style folds,
    absorbed into β as in G3 §3.3. No new slack beyond G3 is introduced by
    the extension (the Y-parts of honest challenges and witnesses are 0).
  - Consequently the extracted witness satisfies the *relaxed* norm bound
    β* = 2^D·β, and Module-SIS hardness is evaluated at β* (this is the
    same relaxed-binding accounting as G2 v2 §A; the G1 estimator margin
    must cover β*, checked in §9.5/M-sprints).

The norm-check lane adds **no challenge-dependent soundness gap**: it is a
deterministic predicate on the (already-extracted) witness, so it does not
enlarge κ.

---

## §4. The R_q ↔ R_q^{(e)} consistency lane (no gap)

This is the crux of "no soundness gap" for using a strictly larger ring.

**Threat.** A malicious prover might commit/fold a witness b̃ with nonzero
Y-degree ≥ 1 components, exploiting freedom that does not exist in R_q,
and thereby satisfy (i) without a genuine R_q witness.

**Constraint (ii) folded into the statement.** Membership b̃ ∈ ι(R_q)^m is
the F_q-linear constraint "all Y-coefficients of index 1..e−1 vanish".
Define the F_q-linear map
  τ : R_q^{(e)} → (R_q)^{e−1},   τ(w) = (w₁, …, w_{e−1})   (the higher Y-parts).
Then (ii) ⇔ τ(b̃ⱼ) = 0 for all j. Two facts make this no-gap:

  1. **τ commutes with the fold.** τ is F_q-linear and the fold is an
     F_q-linear combination with scalar coefficients x, x⁻¹ that are
     **themselves Y-degree-0** (S ⊂ ι-image at the scalar level, i.e. a
     diagonal F_{q⁶}-constant has Y-parts but acts on the *X*-structure;
     careful statement below). Hence
        τ(b̃′) = τ(b̃_L) + x·τ(b̃_R)
     is again a fold of the τ-images, so the consistency predicate
     propagates through the recursion and need only be checked **once, at
     the base level** (length-1 statement), where the bind block already
     opens the base witness in R_q.

     *Caveat to discharge in M4.0:* x ∈ S has nonzero Y-parts as an
     element of R_q^{(e)}; "x·τ(b̃_R)" therefore mixes Y-degrees. The
     clean way (RoK 2024 §automorphism) is to verify (ii) **not** by
     pushing τ through the fold but by the **automorphism/Galois trace**
     argument: apply the Frobenius σ : Y ↦ Y^q (cyclic order 6, available
     because g = Φ₉ is cyclotomic, parent §3.1) and use that
     w ∈ ι(R_q) ⇔ σ(w) = w. The verifier checks the trace identity
     Tr_{R_q^{(e)}/R_q}(b̃) = e·π(b̃) on the *committed* base witness,
     which is a single linear opening, adding **no round** and **no
     challenge** (hence no κ contribution).

  2. **Binding ties b̃ to R_q.** The commitment C_b = Com(b̃) (M3.1) is a
     module-SIS commitment computed in R_q over the Y-degree-0 part; any
     b̃ with nonzero Y-parts that still matches C_b would yield a module-
     SIS collision. So binding already forces π(b̃) to be the committed
     R_q value, and the §4.1 trace check forces the Y-parts to 0. Together
     they pin b̃ ∈ ι(R_q)^m **unconditionally given binding**, i.e. with no
     extra challenge and thus **no soundness gap**.

**Conclusion (consistency lane).** The R_q ↔ R_q^{(e)} consistency is
enforced by (a) the existing module-SIS binding on the base witness and
(b) a single Galois-trace linear opening at the base level. Neither is
challenge-dependent, so the lane contributes **0** to the knowledge error.
This matches the RoK-2024 "no soundness gap" guarantee for moving to a
ring extension that carries a subtractive set.

---

## §5. Composition over D rounds — total knowledge error

By the AC20 tree-of-transcripts extractor (reused in ACK21/RoK 2024), a
μ-round protocol that is (k₁,…,k_μ)-special-sound with each round over
challenge space S has knowledge error

  κ_total ≤ 1 − ∏_{i=1}^{μ} (1 − κ_round,i)  ≤  Σ_{i=1}^{μ} (kᵢ − 1)/|S|.

Here μ = D ≤ 9, kᵢ = 3, |S| = qᵉ − 1:

  **κ_total ≤ D · 2/|S| ≤ 9 · 2 / (q⁶ − 1) ≈ 18 / 2¹²⁹·⁶ ≈ 2⁻¹²⁵·⁴.**

The norm-check lane (§3) and the consistency lane (§4) add **no**
challenge-dependent term, so the *single-shot* (no parallel repetition)
knowledge error of the whole fold is ≈ 2⁻¹²⁵·⁴, i.e. **≥ 125-bit** knowledge
soundness. Combined with the Fiat–Shamir transform in the QROM (counter-
mode, G2 v2 §A4), the standard QROM loss is an additional factor
polynomial in the number of RO queries Q (≈ Q²/2⁻λ slack), which the
λ = 128 floor with this margin absorbs; the precise QROM accounting is the
G4 deliverable.

This is the no-gap statement promised in `MRNG_G3_1_EXTENSION_SOUNDNESS.md`
§2 (single-shot ≈ 2⁻¹²³·⁸ Schwartz–Zippel reading; the explicit-extractor
reading here, ≈ 2⁻¹²⁵·⁴, is tighter and supersedes it).

---

## §6. Honesty ledger — what is proven vs. assumed

| Item | Status |
|------|--------|
| S is subtractive; all pairwise diffs invertible | **PROVEN** in code (§9.2/§9.3 tests: 19 900 diffs) |
| Round 3-special-soundness via Vandermonde over S | **PROVEN** here (§2.1), modulo the standard AC20 lemma |
| κ_round ≤ 2/\|S\| | follows from AC20/ACK21 k-special-soundness ⇒ knowledge-error theorem (cited) |
| κ_total ≤ D·2/\|S\| ≈ 2⁻¹²⁵·⁴ | follows from AC20 tree extractor (cited) |
| Norm-check adds no κ | argued §3; **relaxed bound β\*=2^D·β must be covered by the G1 MSIS estimator** — verification deferred to §9.5/M-sprint |
| Consistency lane adds no κ | argued §4 via binding + Galois trace; **the trace-opening must be implemented and unit-tested in M4.0** (currently a design obligation, not yet code) |
| QROM Fiat–Shamir loss | **DEFERRED to G4** (not claimed here) |

**Open obligations carried into M4.0 (must not be silently dropped):**
  1. ☑ **DONE (M4.0a).** Galois-trace consistency opening implemented &
     tested: `chipmunk_mring_ext_frobenius` (σ:Y↦Y², order-6 generator,
     proven ring automorphism) and `chipmunk_mring_ext_trace`
     (Tr=Σσⁱ, lands in base, Tr∘embed = e·π).  The consistency predicate
     w ∈ ι(R_q) ⟺ σ(w)=w is unit-tested in both directions
     (`test_chipmunk_mring_ext.c` T5).  Wiring this opening into the fold
     transcript is part of M4 proper.
  2. ☑ **DONE (M4.0b).** `chipmunk_mring_hardness_msis_bits_relaxed(D)`
     evaluates binding at β* = 2^D·β; at D = FOLD_DEPTH_MAX = 9 it reports
     ≈ 734 bits ≫ 128 (`test_chipmunk_mring_hardness.c`), so the relaxed
     binding floor holds with large margin.
  3. ☐ G4: QROM Fiat–Shamir accounting for the D-round fold (still open).

---

## §7. Implementation hooks (for M4.0)

  - Challenge x ← `chipmunk_mring_ext_sample_challenge()` (§9.3).
  - x⁻¹ ← `chipmunk_mring_ext_scalar_invert()` (§9.2).
  - Fold maps use `chipmunk_mring_ext_{add,mul}` (§9.2).
  - Embedding/projection: `chipmunk_mring_ext_{embed,project,is_in_base}`.
  - Consistency check: `chipmunk_mring_ext_is_in_base()` on the base
    witness, plus the Galois-trace opening (TO BE ADDED in M4.0).
  - Base relation: M3.3 bind block (`chipmunk_mring_bind_*`), norm gate
    `CHIPMUNK_MRING_RESPONSE_BOUND`.
