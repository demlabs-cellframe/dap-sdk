---
doc: design_decision_cr9
phase: CR-9 — Threshold (Shamir) Scheme
status: ACTIVE — CR-9.4.A/9.5/9.6 SDK slices landed; CR-9.4.B + CR-9.7 deferred
predecessors:
  - documentation_81adcdbaa2c7f8e3   # Round-3 audit
  - doc/crypto/chipmunk_ring/security_review_round4_post_remediation_findings.md
  - task_6516dac58ef91416             # SLC: CR-9 plan
---

# CR-9 — Threshold (Shamir) Scheme: Design Decision

> **Status.**  CR-9 is a multi-week master-plan phase confirmed
> mandatory by the product owner on 2026-04-20.  This document
> captures the design decisions for the *kick-off* slice (CR-9.0
> use-case scope + CR-9.3 canonical Shamir-over-Z_q primitive with
> full unit-test coverage).  The remaining sub-phases (CR-9.1
> integrate-into-active-path, CR-9.4 dealer/sign-partial/combine
> API, CR-9.5 PoP, CR-9.6 Cellframe integration, CR-9.7 security
> proof + peer review) build on the foundation laid here.
>
> **Reset note.**  The pre-fix `chipmunk_ring_secret_sharing.c/.h`
> module was deleted as dead code with broken semantics under
> Round-3 (see CR-D24 closure); the CR-9 work therefore starts from
> scratch rather than patching legacy.

---

## 1. Use-cases (CR-9.0)

The threshold scheme provides `t-of-n` signing on top of the
existing ChipmunkRing primitive.  ChipmunkRing supplies anonymity
among `n` ring members; the threshold layer adds the orthogonal
requirement that **at least `t` out of `n` ring members must
co-operate** to produce a valid signature.

The two layers compose:

* **Ring-only (current code path)** — `t = 1`, any single ring
  member can sign for the ring.  Anonymity-by-indistinguishability
  (currently soft, hardened to true OR-proof under CR-11).
* **Threshold-with-ring (this phase)** — `t > 1`, at least `t`
  ring members must co-operate.  Anonymity properties of the
  underlying ring layer are preserved.
* **Threshold-without-ring (degenerate)** — pure t-of-n signing
  with no anonymity claim.  Not a primary target but emerges
  naturally from the API and is therefore tested.

### 1.1 Target scenarios

| ID         | Scenario                                            | Typical (t, n)        | Latency budget     |
|------------|-----------------------------------------------------|-----------------------|--------------------|
| GOV-MULTI  | Cellframe governance: multi-sig operator decisions  | 3-of-5 / 5-of-9       | seconds (interactive) |
| WALLET-REC | Social-recovery wallet: m-of-n guardians            | 3-of-5 (typ.)         | minutes (out-of-band) |
| CORP-POL   | Corporate signing policy (board approvals)          | 4-of-7 / 5-of-9       | minutes (out-of-band) |
| DAO-VOTE   | DAO threshold votes (binding on-chain action)       | varies                | minutes (block-time)  |

### 1.2 Out of scope (deferred)

* Distributed key generation (DKG).  CR-9 dealer-based key sharing
  is the launch model; DKG is a CR-11 follow-up.
* Resharing / proactive secret sharing.
* Cross-chain threshold orchestration.

---

## 2. Field choice (CR-9.3 foundation)

Shamir secret sharing requires arithmetic over a finite field with
modular inverse.  ChipmunkRing already carries a 22-bit prime
modulus `CHIPMUNK_Q = 3168257` for HOTS arithmetic
(see `module/crypto/src/sig/chipmunk/chipmunk.h:74`).  Re-using
the same prime for the threshold layer:

* eliminates a parameter mismatch foot-gun (one arithmetic
  primitive across the codebase),
* keeps the share carrier compatible with the polynomial
  coefficient type already used by the rest of the chipmunk
  module (`int32_t` lifted to `[0, q)`),
* gives ample room for `n` and `t`: the construction supports any
  `n < q`, far above `CHIPMUNK_RING_MAX_RING_SIZE = 1024`.

Modular inverse is computed via Fermat's little theorem:
`a^{-1} ≡ a^{q-2} (mod q)`.  The exponent fits in 32 bits, and
`q-2 = 3168255` so the binary-exponentiation loop runs in 22
iterations.  No extended-Euclidean implementation is needed at
this scale.

### 2.1 Coefficient carrier

* **Wire**: `uint32_t` little-endian (4 bytes per coefficient on
  the share blob).  This matches the existing schema convention
  (CR-D26).
* **In-memory**: `uint32_t` constrained to `[0, q)` everywhere; a
  helper `s_mod_q(int64_t)` lifts arbitrary signed integers into
  the canonical residue.
* **Secret representation**: a master secret is split into a
  sequence of `uint32_t` field elements.  For ChipmunkRing the
  natural carrier is the seed array `key_seed[32]` of the
  ring private key, treated as 8 × 32-bit little-endian words.
  The ring level deals each word independently.

---

## 3. Canonical Shamir construction (CR-9.3)

### 3.1 `share(secret, n, t)`

Given a secret `s ∈ Z_q`, threshold `t`, and a participant count
`n`, the dealer:

1. Samples coefficients `a_1, …, a_{t-1} ∈ Z_q` uniformly at
   random via `dap_random_bytes` + rejection sampling against the
   bias bound `q · floor(2^32 / q)`.
2. Constructs the polynomial `P(x) = s + a_1·x + … + a_{t-1}·x^{t-1}
   (mod q)`.
3. Evaluates `share_i = (i, P(i))` for `i ∈ {1, …, n}` (note:
   participant indices start at 1 — index 0 would expose the
   secret directly).
4. Zeroises `a_1..a_{t-1}` via `dap_memwipe` before returning.

### 3.2 `reconstruct(shares[t], indices[t])`

Given any `t` shares with their indices, the combiner computes the
Lagrange interpolation at `x = 0`:

```
s ≡ Σ_{j=0..t-1} y_j · Π_{m ≠ j} (-x_m / (x_j - x_m))   (mod q)
```

with all arithmetic in `Z_q`.  No information about `s` is
leakable from any `t-1` shares (information-theoretic perfect
secrecy of Shamir).

### 3.3 Boundary contracts

* `t == 1` is rejected at the API surface — a 1-of-n threshold
  degenerates to "any single share is the secret", which is a
  deployment foot-gun and breaks the secret-sharing claim.  This
  matches industry practice (e.g. Vault's Shamir requires t ≥ 2).
* `t > n` is rejected: nobody could ever reconstruct.
* `n` is bounded by `CHIPMUNK_RING_THRESHOLD_MAX_N = 64`
  (operationally large enough for every target use-case; keeps
  the wire-size predictable; the `n < q` ceiling at ~3 million is
  not a useful operational bound).
* Duplicate indices in `reconstruct` are rejected — they would
  divide by zero in the Lagrange basis.

### 3.4 Failure modes and zeroisation

* Every intermediate (coefficients, partial products) is wiped
  via `dap_memwipe` on both the success path and every error
  return.
* The dealer's secret seed is **not** copied into the share
  buffers — only the `P(i)` evaluations leave the dealer.
* Reconstruction failures (insufficient shares, duplicate indices,
  bad index range) return `-EINVAL` and zero-fill the output
  buffer to avoid leaking partial computations.

---

## 4. API surface for the kick-off slice

```c
/* Internal Shamir primitive over Z_CHIPMUNK_Q (CR-9.3).  The
 * public threshold API (CR-9.4) will wrap these helpers with the
 * ChipmunkRing-level dealer / sign-partial / combine entry
 * points; the helpers below stay private to the chipmunk_ring
 * module so future tweaks (e.g. proactive resharing) do not bleed
 * into downstream code. */

typedef struct chipmunk_ring_shamir_share {
    uint32_t index;     /* participant index in [1, n] */
    uint32_t value;     /* P(index) in [0, q)          */
} chipmunk_ring_shamir_share_t;

/* Returns 0 on success, negative errno on input violation. */
int chipmunk_ring_shamir_share(uint32_t a_secret,
                               uint32_t a_n,
                               uint32_t a_t,
                               chipmunk_ring_shamir_share_t *a_out_shares);

int chipmunk_ring_shamir_reconstruct(const chipmunk_ring_shamir_share_t *a_shares,
                                     uint32_t a_t,
                                     uint32_t *a_out_secret);
```

Both functions are in `module/crypto/src/sig/chipmunk/chipmunk_ring_shamir.c`,
declared in the new internal header
`module/crypto/src/sig/chipmunk/chipmunk_ring_shamir.h`.

The public threshold API
(`chipmunk_ring_threshold_deal/sign_partial/combine`) is the
**next** slice; this slice deliberately stops at the primitive
level so the boundary between "math is correct" and "protocol is
correct" stays bisect-friendly.

---

## 5. Test acceptance for the kick-off slice (CR-9.3)

A new test executable `test_unit_crypto_chipmunk_ring_shamir`
must cover the following invariants before the slice is
considered done:

| Test                                  | Contract                                                                                       |
|---------------------------------------|-------------------------------------------------------------------------------------------------|
| `test_correctness_basic`              | `share(s, n, t) → t shares; reconstruct(any-t-of-n) == s` for a deterministic seed              |
| `test_correctness_random`             | Same, but over many random `(s, n, t)` triples                                                  |
| `test_subset_invariance`              | Every C(n,t) subset of shares reconstructs the same secret                                      |
| `test_insufficient_shares`            | `reconstruct(t-1 shares)` returns error, leaves output zeroised                                  |
| `test_field_arithmetic_inverse`       | `s_mod_inverse(a) · a ≡ 1 (mod q)` for a battery of `a` values                                   |
| `test_t_one_rejected`                 | `share(s, n, 1)` returns `-EINVAL` (boundary contract)                                           |
| `test_t_greater_than_n_rejected`      | `share(s, n, t)` with `t > n` returns `-EINVAL`                                                  |
| `test_index_zero_rejected`            | `reconstruct` with any `index == 0` returns `-EINVAL`                                            |
| `test_duplicate_indices_rejected`     | `reconstruct` with duplicate indices returns `-EINVAL`, output zeroised                          |
| `test_zero_leakage_statistical`       | t-1 shares from `secret == 0` and `secret == q-1` produce statistically indistinguishable outputs |

The last test is the closest practical proxy for the
information-theoretic perfect-secrecy property; it confirms that
the dealer's coefficient sampling does not leak the secret
through a side channel in the share distribution.

---

## 6. Decisions log (CR-9 kick-off)

| #   | Decision                                                                                          | Rationale                                                                                                                |
|-----|---------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------|
| D-1 | Re-use `CHIPMUNK_Q = 3168257` rather than introducing a new prime                                  | One arithmetic primitive across the chipmunk module; eliminates the parameter-mismatch foot-gun that already burned CR-C8 |
| D-2 | Modular inverse via Fermat (`a^{q-2} mod q`), not extended-Euclidean                               | Exponent fits in 22 bits → 22 binary-exp iterations; no algorithmic risk; constant-time-ish (data-independent inner loop) |
| D-3 | Reject `t == 1` at the API surface                                                                 | A 1-of-n threshold is a deployment foot-gun; matches industry practice (Vault, libgfshare)                                |
| D-4 | Cap `n` at `CHIPMUNK_RING_THRESHOLD_MAX_N = 64`                                                    | Wire-size predictable; covers every target use-case; the `n < q ≈ 3M` mathematical bound is not a useful operational cap |
| D-5 | Coefficients sampled via `dap_random_bytes` + rejection sampling                                   | Closes Round-3 CR-C7 (deterministic coeffs); thread-safe after CR-D22; rejection bound = `q · floor(2^32 / q)`            |
| D-6 | Zeroisation via `dap_memwipe` on every error path and every intermediate                           | Closes Round-3 CR-D13 / CR-D25 in the new module from day one                                                              |
| D-7 | Internal-only API for the kick-off slice; public threshold API in CR-9.4                            | Bisect-friendly boundary between "math correct" (this slice) and "protocol correct" (next slice)                          |

---

## 7. Pointers to follow-up sub-phases

| Sub-phase | Status            | Notes                                                                                  |
|-----------|-------------------|----------------------------------------------------------------------------------------|
| CR-9.0    | **CLOSED** | Use-case scope captured in §1 (`db86a383`)                                                |
| CR-9.1    | **CLOSED** | CSPRNG coefficients via `dap_random_bytes` — shipped in CR-9.3 primitive                  |
| CR-9.2    | DEFERRED to CR-9.4.B | Correct pk-offset for true threshold partial sigs                                 |
| CR-9.3    | **CLOSED** | Canonical Shamir over `Z_q` + acceptance suite (`db86a383`)                             |
| CR-9.4.A  | **CLOSED** | Trusted-dealer `threshold_deal/combine` API (`2e3fc016`)                                |
| CR-9.4.B  | DEFERRED  | True `sign_partial/combine` without key reconstruction (`design_decision_cr9_4.md` §7)   |
| CR-9.5    | **CLOSED** | PoP against rogue-key (`7a5d91de`, `design_decision_cr9_5.md`)                          |
| CR-9.6    | **CLOSED** (SDK slice) | `dap_enc_chipmunk_ring_governance_*` + integration guide (`design_decision_cr9_6.md`) |
| CR-9.7    | NEXT              | Security proof sketch + peer review; protocol locked by CR-9.4.A + CR-9.5               |

---

*CR-9 kick-off, 2026-05-12.*
