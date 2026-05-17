---
doc: design_decision_cr9_7
phase: CR-9.7 — Security proof sketch + peer review
status: ACTIVE
predecessors:
  - doc/crypto/chipmunk_ring/design_decision_cr9.md
  - doc/crypto/chipmunk_ring/design_decision_cr9_4.md
  - doc/crypto/chipmunk_ring/design_decision_cr9_5.md
  - doc/crypto/chipmunk_ring/design_decision_cr9_6.md
  - doc/crypto/chipmunk_ring/security_review_round4_post_remediation_findings.md
---

# CR-9.7 — Security Proof Sketch & Peer Review

> **Purpose.**  With CR-9.4.A (trusted-dealer threshold), CR-9.5 (PoP),
> and CR-9.6 (SDK integration) landed, the *threshold governance
> protocol* is structurally complete for the Cellframe launch model.
> This document states the security claims precisely, sketches the
> proofs (or cites standard results), lists explicit assumptions, and
> provides a peer-review checklist.  It does **not** claim production
> readiness for the full ChipmunkRing anonymity layer — that remains
> under CR-11.

---

## 1. Protocol under review (CR-9 slice)

```
Setup (dealer, trusted):
  master_seed ← CSPRNG
  shares[1..n] ← Shamir_split(master_seed, t, n)   // CR-9.3 / CR-9.4.A

Registration (each participant i):
  (pk_i, sk_i) ← HT_keygen_from_policy
  pop_i ← Sign_sk_i( H_domain("chipmunk-ring-pop/v1" || pk_i) )   // CR-9.5

Ring admission (combiner / validator):
  ∀i : Verify_pop(pk_i, pop_i) = OK    // CR-9.6 container_create_with_pop
  ring ← Container({pk_i})

Signing (combiner, after OOB share collection):
  master_seed' ← Shamir_reconstruct(any t shares)
  sk_ring ← HT_keygen(master_seed')
  σ ← chipmunk_ring_sign(sk_ring, message, ring)   // existing ring path
```

**Out of scope for this proof sketch:** CR-9.4.B (true threshold signing
without reconstruction), ChipmunkRing anonymity (CR-C2 / CR-D2 / CR-11),
DKG, proactive resharing.

---

## 2. Assumptions

| ID | Assumption |
|----|------------|
| A-1 | `dap_random_bytes` is a cryptographically secure RNG (OS CSPRNG). |
| A-2 | `chipmunk_ht_sign` / `chipmunk_ht_verify` are EUF-CMA under the HOTS + hypertree construction for the deployed parameter set (same assumption as base Chipmunk). |
| A-3 | SHA3-256 (`dap_hash_sha3_256`) is collision-resistant and behaves as a random oracle in the PoP message derivation. |
| A-4 | **Trusted dealer** for CR-9.4.A: the dealer honestly runs `threshold_deal`, distributes one share per participant over confidential channels, and does not retain shares after distribution (operational, not cryptographic). |
| A-5 | **PoP before sign:** each participant runs `pop_create` with `leaf_index == 0` before any production `chipmunk_ht_sign` on that sk (enforced by `-EBUSY` in CR-9.5). |
| A-6 | Share blobs (`CRHS`, 72 B) are transmitted confidentially; integrity/authenticity of OOB channels is the deployer's responsibility. |

---

## 3. Security claims & proof sketches

### 3.1 Claim T1 — `(t-1)`-privacy of shares (information-theoretic)

**Statement.**  Any set of fewer than `t` valid shares produced by
`chipmunk_ring_threshold_deal` reveals **zero** Shannon information about
the 32-byte `master_seed`, provided coefficients are uniform in `Z_q`
with independent polynomials per chunk.

**Proof sketch.**  Standard Shamir over field `F_q` (`|F_q| = CHIPMUNK_Q =
3168257`).  For each 16-bit chunk, the dealer samples
`f(x) = s + a_1 x + … + a_{t-1} x^{t-1}` with
`s = chunk_value` and `a_j ←$ F_q` via rejection sampling (CR-9.3 D-5).
Any `t-1` evaluations `(x_i, f(x_i))` are consistent with exactly
`|F_q|^{t-1}` master values.  With `q > 2^{16}`, the posterior on `s`
remains uniform.  Chunks are independent ⇒ the full 32-byte seed is
perfectly hidden.

**Implementation anchors:** `test_chipmunk_ring_shamir.c`
(`s_test_statistical_zero_leakage`), `test_chipmunk_ring_threshold.c`
(subset invariance).

**Peer-review focus:** confirm no coefficient reuse across chunks or
shares; confirm `t == 1` is rejected (CR-9.3 D-3).

---

### 3.2 Claim T2 — Correct reconstruction

**Statement.**  Any `t` shares with matching `(magic, version, n, t)` and
distinct indices in `[1, n]` reconstruct the original `master_seed` with
probability 1.

**Proof sketch.**  Lagrange interpolation in `F_q` per chunk; 16
independent applications; endianness / chunk lift is bijective for
16-bit values `< q`.

**Implementation anchors:** roundtrip tests in
`test_chipmunk_ring_threshold.c`, `test_chipmunk_ring_governance.c`.

**Peer-review focus:** duplicate-index rejection (Lagrange denominator
zero); mixed-round rejection; chunk range `v < CHIPMUNK_Q`.

---

### 3.3 Claim P1 — Rogue-key resistance at registration

**Statement.**  Let adversary `A` choose `pk*` without knowing
`sk*` such that `pk*` verifies under honest participants' aggregation
strategy.  `A` cannot produce `pop*` that passes
`chipmunk_ring_pop_verify(pk*, pop*)` except with negligible probability
in the HOTS EUF-CMA game.

**Proof sketch (reduction).**  Suppose `A` wins with non-negligible
probability `ε`.  Build forgery challenger for `chipmunk_ht_verify`:
  * Honest users publish `(pk_i, pop_i)` where
    `pop_i = Sign_{sk_i}(m_i)` and `m_i = H("chipmunk-ring-pop/v1" || pk_i)`.
  * `A` outputs `(pk*, pop*)` with `Verify(pk*, pop*) = 1` and `pk*`
    not owned by `A`.
  * TupleHash-style domain separation (CR-D31) ensures `m*` cannot
    equal any honest `m_i` unless `pk* = pk_i` (collision on SHA3-256
    or identical pk).
  * Therefore `pop*` is an EUF-CMA forgery on `m* = H(domain || pk*)`
    under a key `A` does not possess — contradicting A-2.

**Implementation anchors:** `test_chipmunk_ring_pop.c`
(`s_test_pop_rogue_key_attack_rejected`),
`test_chipmunk_ring_governance.c`
(`s_test_container_with_pop_rejects_rogue`).

**Limitation:** PoP proves ownership of `sk*` for the registered `pk*`,
not identity binding to a legal entity (out of scope).

---

### 3.4 Claim G1 — Governance signing key matches dealer intent

**Statement.**  If the dealer is honest (A-4), at least `t` honest
participants return authentic shares, and the combiner runs
`governance_combine_to_key` correctly, the resulting `dap_enc_key` is
the hypertree key for the dealer's original `master_seed`.

**Proof sketch.**  T2 + deterministic `chipmunk_ht_keypair_from_seed`.

**Implementation anchors:** `test_chipmunk_ring_governance.c`
(deal/combine roundtrip + ht_sign after combine).

---

### 3.5 Explicit non-claims (deferred)

| Property | Status | Owner phase |
|----------|--------|-------------|
| True threshold signing without full `sk` reconstruction | **Not claimed** | CR-9.4.B |
| Ring signer anonymity | **Not claimed** | CR-11 |
| EUF-CMA of full `chipmunk_ring_sign` | **Not re-proven here** | CR-11 + base Chipmunk audit |
| Dealer robustness (malicious dealer) | **Not claimed** | DKG / CR-11 |
| Post-compromise share refresh | **Not claimed** | Proactive resharing / CR-11 |

---

## 4. Peer-review checklist

Reviewers sign off each item before marking CR-9.7 **CLOSED**.

### 4.1 Mathematics & protocol

- [ ] Shamir parameters: `2 ≤ t ≤ n ≤ 64`, `t ≥ 2`, field = `CHIPMUNK_Q`
- [ ] Independent random polynomial per chunk (deal loop)
- [ ] Lagrange basis uses participant indices in `[1, n]`, not `[0, n-1]`
- [ ] PoP message binds `pk_bytes` with length prefix (CR-D31 discipline)

### 4.2 Implementation hygiene

- [ ] All error paths zeroise secrets (`dap_memwipe` / `memset` on seeds)
- [ ] `pop_create` refuses `leaf_index != 0` (`-EBUSY`)
- [ ] `container_create_with_pop` fails closed (no partial ring)
- [ ] Wire magics: `CRHS` / `CRRP` / reserved `CRHP` non-colliding

### 4.3 Test evidence

- [ ] `test_unit_crypto_chipmunk_ring_shamir` — green
- [ ] `test_unit_crypto_chipmunk_ring_threshold` — green
- [ ] `test_unit_crypto_chipmunk_ring_pop` — green (incl. rogue-key)
- [ ] `test_unit_crypto_chipmunk_ring_governance` — green
- [ ] Full `ctest` — green (no regressions)

### 4.4 Documentation alignment

- [ ] `design_decision_cr9.md` §7 table matches shipped code
- [ ] `cellframe_integration_guide.md` governance section matches API
- [ ] Round-4 findings CR-D26..D35 marked FIXED in audit trail

### 4.5 Operational / Cellframe

- [ ] Cellframe team acknowledges trusted-dealer model (A-4)
- [ ] Cert registry design for `(pk, pop)` documented (node repo follow-up)
- [ ] PoP wire size budget accepted (~40 KB per member, one-time)

---

## 5. Review record

### 5.1 SDK-side automated evidence (CR-9.7 closure — SDK)

| Item | Source | Result |
|------|--------|--------|
| §4.1 Math: `t∈[2,n]`, `n≤64`, field=`CHIPMUNK_Q` | `chipmunk_ring_shamir.c` + `chipmunk_ring_threshold.c` validation | **PASS** — rejection paths return `-EINVAL` |
| §4.1 Independent random polynomial per chunk | `chipmunk_ring_threshold_deal` loop calls `chipmunk_ring_shamir_share` per chunk with fresh CSPRNG coeffs | **PASS** |
| §4.1 Lagrange basis uses `[1,n]` | `chipmunk_ring_shamir_reconstruct` rejects `index==0` and duplicates | **PASS** |
| §4.1 PoP length-prefixed pk binding | `s_pop_message_derive` → `chipmunk_ring_domain_hash_internal` (TupleHash) | **PASS** |
| §4.2 Error-path zeroisation | `goto fail` blocks + `dap_memwipe` in `pop_create`, `threshold_deal`, `threshold_combine`, `combine_to_key` | **PASS** |
| §4.2 `pop_create` refuses `leaf_index != 0` | `chipmunk_ring_pop_create` returns `-EBUSY`; locked by `test_pop_create_rejects_used_sk` | **PASS** |
| §4.2 `container_create_with_pop` fails closed | First failing PoP aborts before allocation; locked by `test_container_with_pop_rejects_rogue` | **PASS** |
| §4.2 Wire magics non-colliding | `'CRHS'`/`'CRRP'`/`'CRHP'` reserved; documented in cr9_6 wire table | **PASS** |
| §4.3 `test_unit_crypto_chipmunk_ring_shamir` | ctest 2026-05-17 | **PASS** |
| §4.3 `test_unit_crypto_chipmunk_ring_threshold` | ctest 2026-05-17 | **PASS** |
| §4.3 `test_unit_crypto_chipmunk_ring_pop` | ctest 2026-05-17 (incl. rogue-key) | **PASS** |
| §4.3 `test_unit_crypto_chipmunk_ring_governance` | ctest 2026-05-17 | **PASS** |
| §4.3 Full ctest | **107/107 PASS** (209 s) | **PASS** |
| §4.4 `design_decision_cr9.md` §7 matches code | manual diff vs commits `db86a383`..`9a7cf730` | **PASS** |
| §4.4 `cellframe_integration_guide.md` matches API | Governance Multisig section anchors `dap_enc_chipmunk_ring_governance.h` symbols byte-for-byte | **PASS** |
| §4.4 Round-4 audit trail | `security_review_round4_post_remediation_findings.md` marks CR-D26..D33, D35 FIXED; CR-D34 WITHDRAWN; CR-INFRA-1 split to `task_c1e525d4` | **PASS** |

### 5.2 Reviewer sign-off

| Reviewer | Date | Result | Notes |
|----------|------|--------|-------|
| SDK self-audit (agent) | 2026-05-17 | **CLOSED (SDK)** | §4.1–4.4 verified via the table above |
| Internal crypto reviewer | — | PENDING | §4 re-validation by human peer |
| Cellframe stakeholder | — | PENDING (§4.5) | Trusted-dealer + OOB share model acknowledgement |

---

## 6. Closure criteria for CR-9.7

CR-9.7 is **CLOSED** when:

1. This document's checklist §4.1–4.3 are signed off by at least one
   internal crypto reviewer.
2. §4.4 is verified (doc/code alignment PR merged).
3. §4.5 is acknowledged by Cellframe stakeholder OR explicitly waived
   with written acceptance of trusted-dealer + OOB share model.

Until §4.5 is met, CR-9.7 may be marked **CLOSED (SDK)** with
**OPEN (Cellframe ops)** — same pattern as CR-9.6.

---

*CR-9.7 proof sketch, 2026-05-16.*
