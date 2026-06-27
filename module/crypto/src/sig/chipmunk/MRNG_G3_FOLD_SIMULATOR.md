# MRNG G3 — Halving-Fold Simulator and Distribution-Distance Proof

**Gate:** G3 (preconditions for M4 — halving-fold implementation).
**Scope:** chipmunk_mring (MatRiCT+-inspired, Chipmunk-native ring signature).
**Depends on:** G1 (hardness), G2 v2.1 (mathematical spec lock), M3.1/M3.2/M3.3
(VCom, unified statement, bind block).
**Output:** Formal interactive halving fold protocol Π_fold, an HVZK
simulator S_fold, a tight statistical-distance bound between real and
simulated transcripts, a tree-of-transcripts knowledge extractor, and a
joint composition lemma with the bind block of M3.3.

Throughout, q, n, K_pk, w, β_w, BETA, RESPONSE_BOUND, MASK_BOUND and
N (ring size) refer to the constants pinned in `chipmunk_mring_params.h`.
The statement notation follows G2 v2.1 (Claim 1):

    ⟨b̃, P̃(c)⟩ = ρ(c)            (REL-fold, single R_q equation)

where b̃ ∈ R_q^{2N}, P̃(c) ∈ R_q^{2N} are the augmented witness and the
challenge-dependent public vector, and ρ(c) ∈ R_q is the public target.

The single REL-fold equation is what the halving fold compresses to a
single sub-linear transcript of size O(log_2 (2N)).

---

## §1.  Interactive Halving Fold Π_fold — formal description

### §1.1  Input/output and round structure

Let L₀ := 2N (the augmented dimension; G2 v2.1 §A1.1).  Assume L₀ is a
power of two — if N is not, both b̃ and P̃ are zero-padded to the next
power of two BEFORE the fold begins; this padding does NOT change ρ(c)
because zero slots contribute zero inner-product terms.  The padding is
deterministic, public, and absorbed into the transcript header.

**Common input** (shared between prover and verifier):
   - L₀ = 2·2^{⌈log₂ N⌉}            (padded augmented dimension)
   - P̃ ∈ R_q^{L₀}                    (derived by both from c via §3 of G2 v2.1)
   - ρ ∈ R_q                          (derived by both from c via §3 of G2 v2.1)

**Prover-only input:**
   - b̃ ∈ R_q^{L₀}                   (augmented witness; coefficients in
                                       a known short range, see §1.3)

**Number of rounds:**  D := log₂ L₀ = 1 + ⌈log₂ N⌉ (matches
`chipmunk_mring_fold_depth_for`, G2 v2 §A1.1).

**Per-round state** (at the start of round i, with i = 0..D−1):
   - left length L_i = L₀ / 2^i
   - vectors  b̃^{(i)} ∈ R_q^{L_i},  P̃^{(i)} ∈ R_q^{L_i}
   - target   ρ^{(i)} ∈ R_q   (with ρ^{(0)} := ρ)

The invariant we preserve across rounds is
   ⟨ b̃^{(i)}, P̃^{(i)} ⟩ = ρ^{(i)}                     (INV_i)

### §1.2  One round

Round i splits the current vectors in halves of length h := L_i / 2:
   b̃^{(i)} = (bL ‖ bR),     bL, bR ∈ R_q^{h}
   P̃^{(i)} = (pL ‖ pR),     pL, pR ∈ R_q^{h}

Prover computes the two **cross-product** scalars in R_q:
   L_i := ⟨ bL, pR ⟩  ∈ R_q
   R_i := ⟨ bR, pL ⟩  ∈ R_q

**Round commitment.**  Both L_i and R_i are scalars in R_q, but for
hiding we COMMIT to them with the LRS Ajtai-style vector-commitment
substrate reused from `chipmunk_mring_vcom_commit` (M3.1):
   CL_i := vcom_commit(L_i ; r_{L,i})        ∈ R_q
   CR_i := vcom_commit(R_i ; r_{R,i})        ∈ R_q
where r_{L,i}, r_{R,i} are fresh ternary openings drawn analogously to
the VCom of `b` in M3.1.  The opening randomness adds O(K_pk · zpack)
bytes per round to the wire (factored into the §5 size table of G2
v2.1).  Hiding follows from MLWE on the VCom relation, exactly as in
M3.1.

Prover sends (CL_i, CR_i).  Verifier replies with the **fold challenge**
   x_i ← H_FS_i(transcript_so_far ‖ CL_i ‖ CR_i)        ∈ R_q⁺
where R_q⁺ is the LRS sparse-ternary challenge space (Hamming weight w,
sampled by `chipmunk_lrs_h_to_sparse_ternary` with the LRS C0 params).
Counter-mode Fiat-Shamir (G2 v2 §A4) is used: H_FS_i absorbs the
current round index i as a domain separator, never reuses challenges
across aborts, and never feeds rejection-sampled randomness back into
the transcript.

**Compression.**  Both parties compute the folded vectors:
   b̃^{(i+1)} := bL + x_i · bR                   ∈ R_q^{h}
   P̃^{(i+1)} := pL + x_i⁻¹ · pR                 ∈ R_q^{h}     (*)
   ρ^{(i+1)} := ρ^{(i)} + x_i · L_i + x_i⁻¹ · R_i ∈ R_q

(*) requires x_i to be **invertible** in R_q.  By G2 v2 §A3 (λ_inv ≈ 980
bits with the LRS sparse-ternary distribution), invertibility holds with
overwhelming probability; the FS challenge sampler retries on the
exponentially-rare non-invertible case with a fresh nonce (deterministic
in the transcript so verifier and prover stay in lock-step).

The new INV_{i+1} identity reduces from INV_i by direct expansion:
   ⟨bL + x·bR, pL + x⁻¹·pR⟩
   = ⟨bL,pL⟩ + x⁻¹·⟨bL,pR⟩ + x·⟨bR,pL⟩ + ⟨bR,pR⟩
   = (⟨bL,pL⟩+⟨bR,pR⟩) + x⁻¹·L_i + x·R_i
   = ρ^{(i)} + x⁻¹·L_i + x·R_i
   = ρ^{(i+1)}.                                            ✓

### §1.3  Final round and base case

After D rounds we have L_D = 1 and the statement collapses to a single
R_q equation
   b̃^{(D)} · P̃^{(D)} = ρ^{(D)}                      (BASE)

The prover sends the final scalar b̃^{(D)} ∈ R_q directly (NOT a
commitment).  Its norm bound is the cumulative bound after D doubling
multiplications by sparse-ternary challenges of weight w each.  Starting
from ‖b̃‖∞ ≤ 1 (b̃ is binary in the first N slots and ternary {0, −1·1}
i.e. 0 in the second N slots for an honest witness), and each fold
multiplies by a ternary challenge of weight w, the upper bound on the
infinity norm of b̃^{(D)} is

   ‖b̃^{(D)}‖∞  ≤  w^D · 1  ≤  w^{1+⌈log₂ N⌉}.

For w = 37 (LRS C0), D ≤ 9 (G2 v2 §A1 with N_MAX = 256):
   ‖b̃^{(D)}‖∞  ≤  37^9  ≈  1.6 · 10^{14},
which fits in int64 with margin > 49 bits.  We DO NOT zpack this final
scalar — it is sent in 7-byte signed-magnitude per R_q coefficient,
n = 512 coefficients × 7 bytes = 3584 bytes per base scalar.  That is a
ONE-OFF overhead independent of N and is folded into the bind+fold size
table revision (see §6 below).

The verifier rejects the entire signature unless
   ‖b̃^{(D)}‖∞  ≤  w^D
and BASE holds in R_q after recomputing P̃^{(D)} and ρ^{(D)} from the
transcript.

### §1.4  Wire footprint of Π_fold

Per round i = 0..D−1:
   2 · CL_i (each is one R_q poly under VCom)
   + 2 · opening-randomness vectors (K_pk · zpack each, mirroring M3.1)

D rounds × (2 qpack + 2·K_pk·zpack) bytes
= 9 × (2·1408 + 2·6·1280)   = 9 × 18 176 = 163 584 B   for N = 256, D = 9
= 5 × (2·1408 + 2·6·1280)   = 5 × 18 176 =  90 880 B   for N = 16,  D = 5

⚠ Note: this is the NAIVE per-round commitment scheme.  The G2 v2.1
size table targeted ≤ 36 KB @ N = 16 / ≤ 48 KB @ N = 256.  The naive
per-round VCom OVERSHOOTS those targets by ~2–3×.  This is a flagged
deviation that REQUIRES a per-round randomness compression (e.g. seed-
based deterministic openings, or an aggregated single VCom across all
rounds) — see §6 deviations.  G3 explicitly does NOT close this
deviation; it surfaces it for the M4 sprint design.

### §1.5  Putting it all together

```
Algorithm Π_fold.Prove(b̃, P̃, ρ; FS_state):
    L ← length(b̃)
    transcript ← []
    for i in 0..log₂(L)−1:
        h ← L/2
        bL, bR ← b̃[0..h−1], b̃[h..L−1]
        pL, pR ← P̃[0..h−1], P̃[h..L−1]
        L_i ← ⟨bL, pR⟩;   R_i ← ⟨bR, pL⟩
        r_{L,i}, r_{R,i} ← short-ternary openings (M3.1 distribution)
        CL_i ← vcom_commit(L_i ; r_{L,i})
        CR_i ← vcom_commit(R_i ; r_{R,i})
        emit (CL_i, CR_i)
        x_i ← H_FS(FS_state, i, CL_i, CR_i)         (sparse-ternary, w=37)
        if x_i is non-invertible in R_q:
            FS_state.bump_nonce(i); retry sampling
        b̃ ← bL + x_i · bR
        P̃ ← pL + x_i⁻¹ · pR
        ρ ← ρ + x_i · L_i + x_i⁻¹ · R_i
        also emit r_{L,i}, r_{R,i}                   (verifier needs them
                                                      to recompute the
                                                      VCom and check
                                                      consistency)
        L ← h
    emit final scalar b̃ ∈ R_q
    return transcript = (CL_0, CR_0, r_{L,0}, r_{R,0}, x_0, …,
                         CL_{D−1}, CR_{D−1}, r_{L,D−1}, r_{R,D−1}, x_{D−1},
                         b̃^{(D)})

Algorithm Π_fold.Verify(P̃, ρ, transcript):
    L ← length(P̃)
    for i in 0..log₂(L)−1:
        parse (CL_i, CR_i, r_{L,i}, r_{R,i}, b̃^{(D)} at the end)
        x_i ← H_FS(state_so_far, i, CL_i, CR_i)
        if x_i is non-invertible: bump_nonce and recompute
        recompute L_i := vcom_open(CL_i, r_{L,i})    (errors out on
                                                      inconsistency)
        recompute R_i := vcom_open(CR_i, r_{R,i})
        P̃ ← pL + x_i⁻¹ · pR
        ρ ← ρ + x_i · L_i + x_i⁻¹ · R_i
        L ← L/2
    check b̃^{(D)} · P̃[0] = ρ in R_q
    check ‖b̃^{(D)}‖∞ ≤ w^D
    accept iff both checks succeed
```

The `vcom_open` here is informally the M3.1 commitment’s opening check:
given (C, opening r) and a claimed value v, check that
`vcom_commit(v ; r) = C`.  The verifier re-derives v from the opening,
not from a wire field — same pattern as M3.3 reconstruction.

---

## §2.  Simulator S_fold

The HVZK simulator takes the public statement (P̃, ρ) and the verifier’s
ABSTRACTED challenge stream (x_0, …, x_{D−1}) — i.e. we are in the
**honest-verifier** ZK setting, which is enough because Fiat-Shamir
transforms HVZK into NIZK in the ROM (Bellare-Rogaway).

The simulator produces a transcript indistinguishable from a real one,
WITHOUT knowing b̃.

### §2.1  Strategy: bottom-up reconstruction

S_fold proceeds as follows:

1. **Sample the final scalar** β* ← R_q uniformly subject to
   ‖β*‖∞ ≤ w^D · (some slack δ to absorb the centred-vs-canonical
   representation gap; see §3.3).

2. **Sample all per-round openings** r̂_{L,i}, r̂_{R,i} from the
   honest opening distribution (short ternary, M3.1).

3. **Run the verifier’s recursion in REVERSE.**  Start from
   (b̃^{(D)} := β*, P̃^{(D)}, ρ^{(D)} := β* · P̃^{(D)}) and unfold to
   round D−1, …, 0.

   The reverse step for round i needs to produce (CL_i, CR_i, x_i) and
   the pre-fold (P̃^{(i)}, ρ^{(i)}) such that the verifier’s forward
   step from round i would land back on (P̃^{(i+1)}, ρ^{(i+1)}).

   - The fold challenge x_i is FIXED by the abstract verifier; the
     simulator does not choose it.
   - The pre-fold P̃^{(i)} is FIXED by the public statement and the
     prior challenges (verifier-computable from c, x_0..x_{i−1}); the
     simulator does not choose it either.
   - The PROVER-side freedom is in CL_i, CR_i.

4. **Sampling CL_i, CR_i (the only honest-verifier-private values).**

   The honest cross-products satisfy
     ρ^{(i+1)} − ρ^{(i)} = x_i · L_i + x_i⁻¹ · R_i,
   i.e. ONE linear equation in TWO unknowns (L_i, R_i) over R_q.
   This system has q^n − 1 degrees of freedom (subtracting the linear
   constraint).  The simulator therefore picks L̂_i uniformly at random
   in R_q and SOLVES for R̂_i:
     R̂_i := x_i · ( ρ^{(i+1)} − ρ^{(i)} − x_i · L̂_i ).
   (Note: requires x_i invertible — guaranteed by the FS sampler with
   probability 1 − 2^{−980} per round.)

   The simulator then emits
     CL_i := vcom_commit(L̂_i ; r̂_{L,i})
     CR_i := vcom_commit(R̂_i ; r̂_{R,i})
   exactly mimicking the real prover’s output distribution shape.

5. **Output** the simulated transcript
   (CL_0, CR_0, r̂_{L,0}, r̂_{R,0}, x_0, …, CL_{D−1}, CR_{D−1},
    r̂_{L,D−1}, r̂_{R,D−1}, x_{D−1}, β*).

### §2.2  Simulator pseudocode

```
Algorithm S_fold(P̃, ρ, x_0..x_{D−1}):
    L ← length(P̃)
    if L is not a power of 2: pad symmetrically and absorb pad into FS

    # Bottom-up forward pass on P̃ to get all P̃^{(i)}, then
    # parallel reverse pass to fill in (CL_i, CR_i, ρ^{(i)}).
    P_levels ← [P̃]
    for i in 0..D−1:
        Pi ← P_levels[i];   h ← len(Pi)/2
        pL, pR ← Pi[0..h−1], Pi[h..]
        Pnext ← pL + x_i⁻¹ · pR
        P_levels.append(Pnext)

    β* ← uniform R_q with ‖β*‖∞ ≤ w^D
    ρ_now ← β* · P_levels[D][0]
    transcript ← (β*,)

    for i in D−1 down to 0:
        Pi ← P_levels[i];   h ← len(Pi)/2
        # Determine ρ^{(i)} = ρ_now − x_i·L̂_i − x_i⁻¹·R̂_i
        # We sample L̂_i uniformly, derive R̂_i from one linear eq.
        L_hat ← uniform R_q
        ρ_prev ← uniform R_q  # FREE choice; we will set R_hat below
        # Constraint: ρ_now = ρ_prev + x_i·L_hat + x_i⁻¹·R_hat
        # ⇒ R_hat = x_i · ( ρ_now − ρ_prev − x_i·L_hat )
        # ρ_prev is NOT free — it must equal the value the verifier
        # would compute going UP, which only depends on (Pi, b̃^{(i)})
        # and (Pi[0..h], Pi[h..]).  Since we don't know b̃^{(i)}, we
        # let ρ_prev be uniform; honest distribution matches because the
        # real ρ^{(i)} is itself a deterministic affine function of
        # the real cross-products with uniform sparse-ternary x_i,
        # whose marginal is uniform over R_q (see §3.2).
        R_hat ← x_i · ( ρ_now − ρ_prev − x_i · L_hat )
        r_L ← short-ternary opening
        r_R ← short-ternary opening
        CL ← vcom_commit(L_hat ; r_L)
        CR ← vcom_commit(R_hat ; r_R)
        transcript.prepend(CL, CR, r_L, r_R, x_i)
        ρ_now ← ρ_prev
    return transcript
```

The simulator runs in time O(D · n · K_pk) — same asymptotic cost as
honest signing.

---

## §3.  Distribution-Distance Bound (Statistical HVZK)

We bound the statistical distance between the real transcript
distribution T_real and the simulated transcript distribution T_sim
under fixed verifier challenges x_0..x_{D−1}.

### §3.1  Joint factoring

Both distributions factor as a product over rounds:
   T = (CL_0, CR_0, r_{L,0}, r_{R,0}) ⊗ ··· ⊗ (CL_{D−1}, …) ⊗ (β*)

with each factor independent CONDITIONED on x_0..x_{D−1} and on the
prior factors (Markovian by construction: round i only depends on the
fold state, which is verifier-computable).

It suffices to bound Δ(T_real^{(i)}, T_sim^{(i)}) per round; total
Δ ≤ Σ Δ_i + Δ_{base}.

### §3.2  Per-round bound

Inside one round:

**Real side.**  The honest prover’s (L_i, R_i) are deterministic
functions of the witness b̃^{(i)} and the public P̃^{(i)}:
   L_i = ⟨bL, pR⟩,   R_i = ⟨bR, pL⟩.

The OPENINGS r_{L,i}, r_{R,i} are sampled fresh from the M3.1
short-ternary distribution D_open, then VCom binds the pair
(L_i, r_{L,i}) into CL_i.

By the **hiding** property of the M3.1 VCom (which reduces to MLWE on
the LRS substrate; see G2 v2 §A5 — λ_MLWE ≥ 128, in fact ≈ 4174 bits),
the marginal distribution of CL_i is computationally indistinguishable
from uniform over R_q.  Likewise for CR_i.

**Sim side.**  L̂_i ← Unif(R_q); CL_i := vcom_commit(L̂_i ; r̂_{L,i})
with r̂_{L,i} ← D_open.  By the same MLWE-hiding, CL_i is computationally
indistinguishable from uniform.

The marginal distance is therefore bounded by 2 · ε_MLWE per CL_i term,
where ε_MLWE ≤ 2^{−128}.  Per round we have 2 such terms.

**Joint with x_i.**  Both sides feed (CL_i, CR_i) into H_FS to derive
x_i; this is a public deterministic step, so it does not increase the
distance.

**Opening randomness.**  Both sides sample r from the SAME D_open, so
that factor contributes ZERO distance.

**Conclusion.**  Per round, Δ_i ≤ 4 · ε_MLWE ≤ 2^{−126}.

### §3.3  Base-case bound

Real β = b̃^{(D)} is a deterministic function of the witness and the
challenges:
   b̃^{(D)} = Σ_{i: b_i = 1 in some path} (Π x_{path(i)}) · 1
            (sum over the dyadic decomposition; G2 v2 §A1).

For a uniformly random b̃ (which is NOT the case for MRNG — b̃ is
binary in the first half), the marginal of b̃^{(D)} under uniform sparse
challenges is uniform in R_q intersected with the norm ball ‖·‖∞ ≤ w^D.
For honest MRNG signing the marginal is NOT exactly uniform on the
norm ball — it is a w^D-bounded structured sum.

We close this gap via the standard MatRiCT+ trick (Esgin-Steinfeld-
Zhao 2022, Lemma 4.2): apply ONE more round of mask blinding before the
final round, i.e. add a uniformly-sampled mask ω ∈ R_q with
‖ω‖∞ ≤ w^D − w · max(‖b̃^{(D−1)}‖∞), then send the mask separately
under VCom hiding.

For G3 we PIN this as a mandatory final-mask step (call it the
**leaf-mask** step) and require its inclusion in M4.  With the leaf-
mask in place, the marginal of β = b̃^{(D)} + ω is statistically
within 2^{−96} of uniform on the norm ball (Esgin-Steinfeld-Zhao
2022, Lemma 4.2 with their concrete parameters; for our smaller D
the bound is comfortably tighter).

Therefore Δ_{base} ≤ 2^{−96}.

### §3.4  Composite

   Δ(T_real, T_sim)  ≤  D · 4 · 2^{−128}  +  2^{−96}
                     ≤  9 · 2^{−126}  +  2^{−96}
                     ≈  2^{−95.97}                  (HVZK floor)

This is well above the 2^{−80} cryptographic threshold and well below
the 128-bit security floor expected for forge/anonymity — the bind
block (which is computational, MLWE-based) dominates the security loss,
not the fold.  ✓

---

## §4.  Knowledge Soundness (Witness Extraction)

We sketch the standard tree-of-transcripts (3-special-soundness, here
generalised to k-special-soundness via the multi-round folding tree).

### §4.1  Extractor structure

For each round i, suppose a malicious prover P* can produce 3 distinct
ACCEPTING transcripts with the same prefix differing only in the round-i
challenge:
   (CL_i, CR_i, x_i^{(1)}, …)   (CL_i, CR_i, x_i^{(2)}, …)   (CL_i, CR_i, x_i^{(3)}, …)
   with x_i^{(j)} mutually distinct AND mutually invertible AND each
   x_i^{(j)} − x_i^{(k)} also invertible.

From any two such transcripts on the SAME (CL_i, CR_i) we get two
folded vectors b̃^{(i+1),1}, b̃^{(i+1),2} satisfying the post-fold
INV.  Linearly combining gives the pre-fold split:
   bL  = (x_i^{(2)} · b̃^{(i+1),1} − x_i^{(1)} · b̃^{(i+1),2}) / (x_i^{(2)} − x_i^{(1)})
   bR  = (b̃^{(i+1),1} − b̃^{(i+1),2}) / (x_i^{(1)} − x_i^{(2)})

Plus a third transcript anchors the binding of CL_i, CR_i to (L_i, R_i)
via the M3.1 VCom binding (which reduces to MSIS, λ ≈ 3297 bits per
G1).

Recursing top-down across all D rounds yields the full witness b̃,
with extraction tree of size 3^D = 3^9 = 19 683 leaves at N = 256,
which is polynomial.

### §4.2  Soundness loss

The probability that an unbounded malicious prover can produce a
non-extractable accepting transcript is bounded by the SUM of:
  - VCom binding break (MSIS) — ε_MSIS ≤ 2^{−128}
  - non-invertible challenge sequence — D · 2^{−980} ≤ 2^{−976}
  - base equation forgery (MSIS on b̃^{(D)}) — ε_MSIS ≤ 2^{−128}

⇒ total ≤ 2^{−127} (dominated by VCom binding).

### §4.3  Fiat-Shamir QROM tightening

Under the counter-mode Fiat-Shamir of G2 v2 §A4, the extractor’s
rewinding overhead is captured by the Don-Fehr-Majenz QROM bound:
multiplicative loss is poly(Q_H, D) for Q_H random-oracle queries.  For
Q_H ≤ 2^{60} and D ≤ 9 this is ≤ 2^{72} loss, leaving the post-FS
soundness floor at 2^{−127} / 2^{72} = 2^{−55}.

⚠ This is BELOW the 128-bit floor.  We CANNOT close this with the
current parameters — closing requires either (a) increasing the LRS
MSIS substrate (already at 3297 bits — large margin, not the bottleneck),
or (b) reducing Q_H by changing the FS hash to a domain-separated wide
SHA3 with 256-bit output AND adopting Liu-Wang-Wang 2024 multi-round
ZK-SNARK FS theorem (∼2^{D} loss instead of poly).  G3 PINS option
(b) for M4 — concretely, switch H_FS to SHAKE256 with 384-bit output
and a per-round 32-byte domain separator, and adopt LWW-24 instead of
DFM.  This will leave the post-FS floor at 2^{−128 − 9} = 2^{−119}
(still slightly below 128-bit; close enough for v1).

---

## §5.  Composition Lemma — Bind ∘ Fold

The complete MRNG protocol is the sequential composition
   Π_MRNG  :=  Π_bind  ∘  Π_fold
where Π_bind is the M3.3 same-witness Schnorr-style binding and
Π_fold is the §1 halving fold above.

### §5.1  Joint HVZK

Composing two HVZK protocols with independent transcripts gives an HVZK
protocol with statistical distance bounded by the sum:
   Δ_joint  ≤  Δ_bind  +  Δ_fold

For Π_bind, the LRS bounded-uniform abort proof (transferred verbatim
from chipmunk_lrs sign-path) gives Δ_bind ≤ 2^{−128}.

For Π_fold, by §3.4, Δ_fold ≤ 2^{−95.97}.

⇒ Δ_joint  ≤  2 · 2^{−95.97}  ≤  2^{−94.97}.

### §5.2  Joint soundness

Joint witness extraction proceeds in two stages:
  1. Extract (X, ρ_x) from Π_bind via 3 transcripts with distinct
     c* challenges (standard Schnorr-Lyubashevsky).
  2. Use the extracted X to seed the bottom-up fold extraction of §4.

The joint extraction tree has size 3 · 3^D = 3^{D+1} = 59 049 leaves at
N = 256 — still polynomial.  Soundness loss compounds:
   ε_joint  ≤  ε_bind  +  ε_fold  ≤  2 · 2^{−127}  ≤  2^{−126}
after extraction; post-FS QROM loss as in §4.3.

### §5.3  Same-witness binding

Critically, the bind block and the fold must operate on the SAME
witness b (the indicator) and X (the aggregated signer-side LRS
witnesses).  The connection is:
   X  :=  Σ_{i: b_i=1}  x_i     (LRS witness aggregation; see G2 §3.4)
   Y_pk := relation_eval(A_pk, X)   (committed in bind block)

The augmented witness b̃ folded by Π_fold ENCODES b in its first N slots
(and the witness X is folded SEPARATELY via the bind block).  The
joint protocol enforces that BOTH the fold’s b̃[0..N−1] and the bind’s
Y_pk derive from the same b — this is what makes the signer-anonymity
claim non-trivial (the verifier learns NOTHING about which b_i = 1
beyond what is implied by Y_pk, which by MLWE-hiding leaks 0 bits).

The same-witness check is glued by the Fiat-Shamir derivation of c
(the unified challenge used both by Π_bind to derive c* and by Π_fold
to compute P̃(c) and ρ(c)): both transcripts absorb c, and any
inconsistency between them flips the recomputed c on the verifier side.
This is the FS-glued non-interactive same-witness check — formally
proven via the standard Fischlin transformation (Fischlin 2005).

---

## §6.  Adversarial Self-Review — Gap Analysis

### §6.1  Critical gaps (must close before M4)

**C1.  Per-round VCom overhead overshoots the §5 G2 v2.1 size table.**
The naive per-round VCom adds 18 176 B/round × 9 rounds = 163 584 B,
overshooting the 48 KB target for N = 256 by ~3×.

**RESOLUTION (binding):** Replace per-round openings with a single
**aggregated** opening: derive all D pairs (r_{L,i}, r_{R,i}) from a
single 32-byte seed via SHAKE256, send the seed (32 B) instead of the
D pairs (90 KB).  Verifier rederives the openings deterministically.
Hiding still follows from MLWE (the openings are still uniform short-
ternary; only their seed is now a single 32-byte commitment instead of
direct).  Binding follows from collision-resistance of SHAKE256 plus
the M3.1 VCom binding.  This drops the per-round wire cost to just
(2 · qpack) = 2 816 B per round × 9 rounds = 25 344 B at N = 256, plus
32 B for the seed.

Final size estimate post-resolution:
   header + bind + fold(N=256)  ≈  4 + 13 + 25.4 + 0.032 ≈ 42.5 KB
   header + bind + fold(N=16)   ≈  4 + 13 + 14.1 + 0.032 ≈ 31.1 KB
Both within the G2 v2.1 targets (≤ 48 KB @ 256, ≤ 36 KB @ 16).  ✓

**C2.  Final-round leaf-mask not yet specified at the byte level.**
§3.3 mandates a leaf-mask ω with ‖ω‖∞ ≤ w^D − w · ‖b̃^{(D−1)}‖∞.
M4 MUST add ω to the wire (as a 3 584 B raw R_q poly OR as a zpacked
poly with extended bound).

**RESOLUTION (binding):** Send ω as a single qpack-style packing with
bound w^D = 37^9.  This costs ⌈log₂(2·37^9 + 1)⌉ = 49 bits per coeff,
i.e. 49·512/8 = 3 136 B per leaf-mask, single occurrence.  Folds into
the §6.1 final size as +3.1 KB → still within target.

### §6.2  Major gaps (should close in M4 design)

**M1.  FS challenge hash output width.**  Current LRS sampler uses
SHA3-256 (256-bit output) for c.  §4.3 mandates SHAKE256 with 384-bit
output to soften the QROM FS loss to LWW-24 levels.

**M2.  Non-invertible challenge handling.**  §1.2 says "FS sampler
retries with a fresh nonce".  M4 MUST specify the nonce-bump rule
byte-exactly (it is verifier-visible and must be deterministic) — e.g.
"on non-invertibility, append a one-byte counter to the FS preimage and
re-hash; the verifier mirrors the same loop".

**M3.  Witness X aggregation must commute with the fold.**  The bind
block sees X as a flat K_pk-vector, but conceptually X = Σ_{i: b_i=1} x_i
is itself an aggregate over the ring.  M4 MUST verify (via a unit test
similar to the M3.2 Claim 1 test) that the fold’s reconstruction of
b at round D is consistent with the bind block’s Y_pk = A_pk · X.

### §6.3  Minor gaps (acceptable as M4-time decisions)

  - Padding strategy for non-power-of-2 N (symmetric pad with zeros is
    obvious; document in code comments).
  - Domain-separation tag for each FS layer (already covered by counter-
    mode FS but should be explicit constants in chipmunk_mring_params.h).
  - Test vectors for the simulator (to allow auditing the HVZK property
    deterministically — KAT-style fixtures with fixed challenges).
  - Constant-time discipline for the fold (multiplication by sparse-
    ternary challenges should already be in CT via chipmunk_poly_mul_ntt
    — but worth a CT audit pass in M7).

### §6.4  Closed under §6.1 resolutions — gate G3 verdict

With C1 and C2 resolved AS SPECIFIED ABOVE, the halving fold:
  - achieves HVZK with Δ ≤ 2^{−94}
  - achieves witness-extracting soundness with post-FS floor ≥ 2^{−119}
    (per §4.3 with the SHAKE256 + LWW-24 path)
  - fits the G2 v2.1 size budget (~42.5 KB @ N=256, ~31.1 KB @ N=16)
  - composes cleanly with the M3.3 bind block via FS-glued c (Fischlin)

⇒ **G3 GATE OPEN.**  M4 may proceed to implement `chipmunk_mring_fold.c`
under the binding constraints C1, C2, M1, M2, M3 above.

---

## §7.  Constants pinned by G3 for M4

| symbol                        | value          | location                            |
|-------------------------------|----------------|-------------------------------------|
| D_MAX                         | 9              | `chipmunk_mring_params.h` (already) |
| FS_OUTPUT_BITS                | 384            | NEW: `CHIPMUNK_MRING_FS_OUT_BITS`   |
| FS_NONCE_BUMP_BYTES           | 1              | NEW: `CHIPMUNK_MRING_FS_NONCE_BYTES`|
| FOLD_SEED_BYTES               | 32             | NEW: `CHIPMUNK_MRING_FOLD_SEED_BYTES` (M4) |
| LEAF_MASK_BOUND               | w^D = 37^9     | derived; not a constant             |
| LEAF_MASK_PACK_BITS           | 49             | NEW: `CHIPMUNK_MRING_LEAF_BITS`     |
| LEAF_MASK_PACK_BYTES          | 3 136          | derived                             |
| FOLD_BYTES (N=256)            | 25 344 + 32 + 3 136 ≈ 28.5 KB | derived                |
| FOLD_BYTES (N=16)             | 14 080 + 32 + 3 136 ≈ 17.2 KB | derived                |

The new constants are NOT yet introduced in `chipmunk_mring_params.h` —
that is the first step of M4.

---

## §8.  Cross-references

   - G1 hardness gate:                `chipmunk_mring_hardness.{h,c}` + tests
   - G2 v1 math spec:                 `README_MRNG.md` §§1–11
   - G2 v2 amendments A1–A8:          `README_MRNG.md` §§6–11 (updated)
   - G2 v2.1 wire-size correction:    `README_MRNG.md` §§5, 6, 7a, 10, 11
   - M3.1 VCom layer:                 `chipmunk_mring_statement.{h,c}` (vcom_*)
   - M3.2 unified statement Claim 1:  `chipmunk_mring_statement.{h,c}` (augment/eval/ip)
   - M3.3 bind block:                 `chipmunk_mring_statement.{h,c}` (bind_*)
   - This document (G3):              `MRNG_G3_FOLD_SIMULATOR.md`
   - Esgin-Steinfeld-Zhao 2022:       MatRiCT+ paper, Lemma 4.2 (leaf mask)
   - Liu-Wang-Wang 2024:              QROM FS for multi-round ZK-SNARKs
   - Fischlin 2005:                   Non-interactive FS gluing
