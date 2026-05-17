---
doc: design_decision_cr11
phase: CR-11 — Publication readiness
status: ACTIVE — CR-11.A in this slice; CR-11.B..F structured but deferred
predecessors:
  - doc/crypto/chipmunk_ring/security_review_round2_cr6_critical_reaudit.md   # CR-11 origin (§5)
  - doc/crypto/chipmunk_ring/design_decision_cr9.md                            # CR-9 master
  - doc/crypto/chipmunk_ring/design_decision_cr9_5.md                          # CR-11.PoP-OPT-1 origin
  - doc/crypto/chipmunk_ring/design_decision_cr9_7.md                          # SDK proof sketch
---

# CR-11 — Publication Readiness: Design Decision (Master)

> **Scope.**  Round-2 §5 defined CR-11 as "publication readiness":
> NIST-Level claim correction, reproducible builds, KAT publication,
> formal Acorn redefinition.  As Round-3/4 and CR-9 evolved, four
> additional follow-ups were tagged for CR-11:
>
> * **CR-11.PoP-OPT-1** — dedicated PoP keypair (from CR-9.5 D-3)
> * **CR-11.RING-ANON** — true ring anonymity hardening (OR-proof)
> * **CR-11.DKG** — distributed key generation (from CR-9 §1.2)
> * **CR-11.RESHARE** — proactive resharing / share refresh (CR-9.7 §3.5)
>
> This master document carves CR-11 into atomic shippable slices.
> **This slice** ships **CR-11.A** (NIST-claim correction + experimental
> marker on `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING`).  CR-11.B..F are
> documented with explicit acceptance criteria so the next slice can
> pick up any of them without re-design.

---

## 1. Slice map

| Slice | Title | Effort | Status | Notes |
|-------|-------|--------|--------|-------|
| **CR-11.A** | NIST Level claim correction + experimental marker | days | **ACTIVE** (this slice) | No crypto change; surface marking only |
| CR-11.B | Reproducible builds + KAT publication | 1 week | DEFERRED | Needs CI artefact pipeline + release process |
| CR-11.C | Acorn formal redefinition (or terminology change) | 1 week | DEFERRED | Choice between "make ZK", "rebrand", or "remove" |
| CR-11.D — RING-ANON | True ring anonymity (OR-proof / CLSAG-style) | **3–6 weeks** | DEFERRED | The biggest remaining crypto rewrite |
| CR-11.E — PoP-OPT-1 | Dedicated PoP keypair (restore 64-leaf budget) | 1 week | DEFERRED | From CR-9.5 D-3 |
| CR-11.F — DKG / RESHARE | Distributed key generation + proactive resharing | **6+ weeks** | DEFERRED | Removes trusted-dealer (A-4 of CR-9.7) |

---

## 2. CR-11.A — NIST Level claim correction + experimental marker

### 2.1 Problem

Current state (verified 2026-05-17):

* `module/crypto/include/dap_enc_chipmunk_ring.h` exposes
  `chipmunk_ring_security_level_t` with values mapped to **NIST Level I
  / III / V** and an extra `V_PLUS`.
* `chipmunk_ring.c` security-preset table claims "AES-128 equivalent",
  "AES-192 equivalent", etc., based on a fixed `0.292 × n` Ring-LWE
  estimate, **without** referencing the deployed primitive
  (chipmunk_ht over a SIS-style lattice).  These claims were
  originally pinned to legacy NewHope-style lattice parameters that
  no longer match the shipped algorithm.
* `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING` is offered with no contract
  banner in `dap_enc_key.h`, despite Round-2 §6.1 recommending an
  immediate `experimental / do-not-use-in-production` marker until
  CR-11 closes.
* Round-2 §6.1 explicitly required this marker as a same-day PR; it
  was missed by Round-1 closure and never reopened.

### 2.2 Decisions

| # | Decision | Rationale |
|---|----------|-----------|
| A-D-1 | Add a banner doc-comment to `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING` marking it **experimental** with explicit pointers to CR-11.D (anonymity) and CR-9.7 (governance proof sketch) | Round-2 §6.1 directive, unblocks Cellframe legal review |
| A-D-2 | Rename `chipmunk_ring_security_level_t` description strings to drop hard-coded "AES-N equivalent" claims; keep level IDs (binary compatible) but reword to "Reference parameter preset, security TBD by CR-11.B" | The NIST level claim is unsupported by a security proof for the shipped construction; we keep the preset IDs because callers depend on them, but stop overstating them |
| A-D-3 | Add module-level banner in `dap_enc_chipmunk_ring.h` explaining experimental scope, current production-ready surface (governance per CR-9.6), and what is not yet claimed | Single source of truth callers will read |
| A-D-4 | Add a unit-test `test_chipmunk_ring_experimental_marker` that fails if the banner string is removed from the public header | Locks the marker in CI; no silent rollback |
| A-D-5 | Do **NOT** change the wire format, key sizes, function signatures, or runtime behaviour in this slice | Bisect-friendly; pure surface marking, zero risk of regression |

### 2.3 Acceptance criteria (CR-11.A)

- [ ] `dap_enc_key.h`: `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING` carries an
      experimental doc-comment with pointers to CR-9.7 (SDK proof
      sketch) and CR-11.D (ring anonymity hardening).
- [ ] `dap_enc_chipmunk_ring.h`: top-of-file banner with explicit
      "experimental until CR-11" clause and a "production-ready
      governance via CR-9.6" callout.
- [ ] `chipmunk_ring.c` preset table description strings reworded to
      drop "AES-N equivalent" wording; reference parameter preset
      semantics only.
- [ ] No change to wire format, struct layouts, function signatures.
- [ ] New unit test asserts banner is present.
- [ ] Full ctest green.

---

## 3. CR-11.B — Reproducible builds + KAT publication (deferred)

### 3.1 Acceptance criteria

- [ ] Deterministic key generation KATs (seed → pk/sk bytes) for
      every shipped chipmunk variant.
- [ ] Deterministic sign-with-fixed-randomness KATs for
      `chipmunk_ring_sign`, `chipmunk_ht_sign`, `chipmunk_ring_pop_create`.
- [ ] Threshold deal/combine KAT for `chipmunk_ring_threshold_*`.
- [ ] KAT files committed under `doc/crypto/chipmunk_ring/kat/` with
      stable filenames and a `MANIFEST.sha256`.
- [ ] CI job verifies KATs on every push.

### 3.2 Out of scope

* Cross-compiler reproducibility (separately tracked under build
  hygiene; not a CR-11 deliverable).

---

## 4. CR-11.C — Acorn formal redefinition (deferred)

### 4.1 Decision required

Per CR-C20 and CR-D8 (Round-3): the term "Acorn ZK proof" is currently
a misnomer — the code computes `H^I(pk || M || r)` without
witness-extraction.  Three viable paths:

| Option | Description | Trade-off |
|--------|-------------|-----------|
| **C-1** | Promote to a real ZK proof (Sigma protocol over the HOTS keypair) | Largest crypto work; needs new soundness proof |
| **C-2** | Rebrand to "Acorn commitment" and stop claiming ZK properties | Cheapest; loses the marketing claim |
| **C-3** | Remove Acorn entirely (PoP via CR-9.5 plus ring sig is sufficient) | Cleanest; removes a non-functional artefact |

### 4.2 Acceptance criteria

- [ ] One of {C-1, C-2, C-3} selected and committed in a follow-up
      design slice.
- [ ] Documentation and code consistent with the chosen path.
- [ ] No reference to "ZK" / "zero-knowledge" anywhere unless the
      chosen path is C-1.

---

## 5. CR-11.D — RING-ANON: True ring anonymity hardening (deferred, BIG)

### 5.1 Problem

Current `chipmunk_ring_sign` runs a *constant-time* signer-index scan
inside the SDK (verified at `chipmunk_ring.c:1012–1063` post-CR-D2),
but the underlying signature primitive (`chipmunk_ht_sign` of a
Fiat-Shamir challenge) does **not** offer an anonymity proof: a
verifier who learns the signer's public key can re-derive the
challenge and check `chipmunk_ht_verify(pk_i, …)` for each candidate.

Round-3 CR-D1/D2 fixed *unforgeability* and *index-scan side channels*
but explicitly deferred *cryptographic* anonymity to CR-11.

### 5.2 Candidate constructions

| Option | Description | Estimated cost |
|--------|-------------|----------------|
| **D-1** | CLSAG-style ring sig over hypertree keys (linkable) | 3 weeks |
| **D-2** | SAG / LSAG analogue (unlinkable) | 4 weeks |
| **D-3** | OR-proof: Sigma protocol disjunction of `n` chipmunk_ht statements | 6 weeks |
| **D-4** | Lattice-native ring sig (e.g., Falcon-based MLSAG analogue) | 6+ weeks |

### 5.3 Acceptance criteria

- [ ] Design slice document `design_decision_cr11_d.md` selecting one
      of D-1..D-4 with a written proof of *signer-indistinguishability*
      reducing to a standard assumption.
- [ ] Wire format change tagged with a new `SIG_TYPE_*` constant; old
      format continues to verify (legacy compat) until deprecation
      window closes.
- [ ] Anonymity test suite: indistinguishability by ring-scan oracle,
      transcript-replay rejection, cross-message linkability bound
      (LSAG/CLSAG only).
- [ ] Performance budget: ring size 64 signs in ≤ 5 × current cost,
      verifies in ≤ 10 × current cost (publishable).

---

## 6. CR-11.E — PoP-OPT-1: Dedicated PoP keypair (deferred)

Inherited from `design_decision_cr9_5.md` D-3.

### 6.1 Decisions

- [ ] Add a PoP-only HOTS keypair derived from `key_seed` under
      `"chipmunk-ring-pop-hots/v1"`.
- [ ] Bump `CHIPMUNK_RING_POP_VERSION` to 2; v1 PoP blobs continue to
      verify against the legacy leaf-0 keypair.
- [ ] Restore full 64-leaf production-signing budget per sk.
- [ ] Migration note in `cellframe_integration_guide.md`.

---

## 7. CR-11.F — DKG / proactive resharing (deferred, BIG)

Removes the trusted-dealer assumption (A-4) of CR-9.7 §2.

### 7.1 Slicing

| Sub-slice | Title | Effort |
|-----------|-------|--------|
| F-1 | DKG over `Z_q` (Pedersen-style; commits via SHA3-256 challenge) | 4 weeks |
| F-2 | Proactive resharing (refresh shares without changing master) | 2 weeks |
| F-3 | Verifiable Secret Sharing (commits exposed; cheating dealer detection) | 2 weeks |

---

## 8. Closure pattern

Each sub-slice (A..F) is **independently closeable** with its own
design doc, commit set, and acceptance tests.  CR-11 is **CLOSED** as
a phase when:

* CR-11.A landed (this slice), AND
* CR-11.B + CR-11.C landed (publication artefacts + Acorn semantics
  resolved), AND
* CR-11.D landed (true ring anonymity hardening), AND
* CR-11.E / CR-11.F either landed or explicitly **WAIVED** by Cellframe
  ops with written acceptance of the carried assumption.

This document is the single source of truth for the slice map; every
sub-slice's design doc must reference back to it.

---

*CR-11 master design + CR-11.A slice, 2026-05-17.*
