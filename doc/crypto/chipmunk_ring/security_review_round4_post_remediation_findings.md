---
doc: security_review_round4_findings
phase: CR-A continuation / Round-4
status: ACTIVE
classification: HIGH — RELEASE BLOCKERS RESOLVED, MEDIUM/LOW PENDING
audit_rounds:
  - CR-0..CR-5  (initial planning & closure)
  - CR-6        (Round-2 re-audit, meta-level)
  - CR-7        (Round-3 line-by-line code audit)
  - Round-4     (post-remediation re-audit) ← THIS DOCUMENT
predecessor:  doc/crypto/chipmunk_ring/security_review_round3_cr7_critical_findings.md
---

# Chipmunk Ring — Round-4 Post-Remediation Audit

> **TL;DR.**  After the Round-3 surgical-remediation sweep landed (commits
> through `fce798e9`), a fresh in-branch audit was performed across
> `module/crypto/src/sig/chipmunk/chipmunk_ring*.c`,
> `module/crypto/src/dap_enc_chipmunk_ring.c`, the schema layer, and the
> `chipmunk_ring` test corpus.  Round-4 confirms that **all Round-3
> CR-D1..CR-D25 fixes are in place** and surfaces **five new findings**
> introduced by the remediation work itself or previously masked by
> deeper bugs:
>
> *  **1 CRITICAL** — `zk_proof_size_per_participant` schema/struct
>    width mismatch (CR-D26): UINT64 schema entry over a uint32_t
>    struct field leaked the adjacent `zk_iterations` onto the wire on
>    serialise and silently overwrote it on deserialise.
> *  **4 HIGH**  — leak in dead duplicate ring-size guard (CR-D27),
>    `ring_hash` leak in two error paths of `container_create` plus
>    missing overflow guard (CR-D28), multi-signer ZK loop returning a
>    "successful" signature with all-zero proof slots on intermediate
>    failures (CR-D29), and `dap_enc_chipmunk_ring_write_signature`
>    bypassing the canonical schema wrapper, dropping the parametric
>    `size_params` (CR-D30).
> *  **2 MEDIUM** — Acorn `s_domain_hash` uses `strlen` for the
>    domain string, leaving the boundary between domain/salt/input
>    inferable rather than authenticated (CR-D31); ring_hash output
>    width is hard-pinned to `CHIPMUNK_RING_LINKABILITY_TAG_SIZE`
>    irrespective of the active hash algorithm (CR-D32).
> *  **3 LOW / INFO** — local CMakeLists at
>    `tests/unit/crypto/chipmunk_ring/CMakeLists.txt` is dead-included
>    twice (parent + local) (CR-D33); `test_chipmunk_ring_simple` and
>    `test_chipmunk_ring_input_validation` disagree on whether
>    `chipmunk_ring_sign` allows `message_size == 0` (CR-D34); test
>    coverage gaps for k=1, max-k, concurrent sign, allocator failure
>    injection, and embedded-vs-non-embedded full wire roundtrip
>    (CR-D35).
>
> All five **release blockers** (CR-D26..CR-D30) have been **fixed and
> regression-tested** in commit `227a933f` of `feature/chipmunk-ring`.
> The remaining MEDIUM and LOW findings are **non-blocking** and
> tracked here for the next remediation cycle.

---

## 0. Audit scope (Round-4)

| Layer                              | Files                                                                                                                | Status   |
|------------------------------------|----------------------------------------------------------------------------------------------------------------------|----------|
| Chipmunk Ring core                 | `chipmunk_ring.c`, `chipmunk_ring_acorn.c`                                                                           | RE-AUDITED |
| Schema / wire codec                | `chipmunk_ring_serialize_schema.{c,h}`                                                                               | RE-AUDITED |
| DAP-SDK wrappers                   | `dap_enc_chipmunk_ring.c`                                                                                            | RE-AUDITED |
| Test corpus                        | `tests/unit/crypto/chipmunk_ring/*.c`, `tests/unit/crypto/chipmunk_ring/CMakeLists.txt`                              | RE-AUDITED |
| Base Chipmunk + hypertree          | (out of scope this round — covered separately by CR-A and the hypertree subtree)                                     | UNCHANGED |

Severity legend (continued from Round-3):

* **CRITICAL** — breaks fundamental security property (forgery /
  de-anonymisation / key recovery / memory-safety).
* **HIGH** — significant weakening, exploitable in plausible operational
  conditions, or compromises integrity invariants.
* **MEDIUM** — defence-in-depth weakening; enables exploitation when
  another bug is leveraged.
* **LOW / INFO** — hygiene, test debt, documentation drift.

---

## 1. Executive summary

| ID      | Severity   | Component                           | One-line description                                                                          | Status |
|---------|------------|-------------------------------------|-----------------------------------------------------------------------------------------------|--------|
| CR-D26  | CRITICAL   | `chipmunk_ring_serialize_schema.c`  | UINT64 schema entry over uint32_t struct field — leaks/overwrites adjacent `zk_iterations`    | **FIXED** (227a933f) |
| CR-D27  | HIGH       | `chipmunk_ring.c`                   | Dead duplicate ring-size guard returns without freeing partially-initialised signature        | **FIXED** (227a933f) |
| CR-D28  | HIGH       | `chipmunk_ring.c`                   | `ring_hash` leak in two error paths of `chipmunk_ring_container_create`; missing overflow guard | **FIXED** (227a933f) |
| CR-D29  | HIGH       | `chipmunk_ring.c`                   | Multi-signer ZK loop returns success with all-zero proof slots on intermediate failures        | **FIXED** (227a933f) |
| CR-D30  | HIGH       | `dap_enc_chipmunk_ring.c`           | `write_signature` bypasses the canonical schema wrapper; drops parametric `size_params`        | **FIXED** (227a933f) |
| CR-D31  | MEDIUM     | `chipmunk_ring.c` / `chipmunk_ring_acorn.c` | `s_domain_hash` uses `strlen(domain)` and concatenates domain‖salt‖input without length prefixes — domain/salt boundary inferable rather than authenticated | **FIXED** (695572b7) |
| CR-D32  | MEDIUM     | `chipmunk_ring.c`                   | `ring_hash_size` hard-pinned to `CHIPMUNK_RING_LINKABILITY_TAG_SIZE` regardless of active hash | **FIXED** (next) |
| CR-D33  | LOW        | `tests/unit/crypto/chipmunk_ring/CMakeLists.txt` | Local CMakeLists dead-included alongside parent registration                          | **FIXED** (next) |
| CR-D34  | (with-drawn) | (n/a)                             | (Round-4 author error — re-audit confirmed `chipmunk_ring_sign` already enforces a single, consistent contract `a_message \|\| a_message_size == 0`; both test files honour it) | **WITHDRAWN** |
| CR-D35  | INFO       | `tests/unit/crypto/chipmunk_ring/`  | Coverage gaps: k=1, max-k, concurrent sign, allocator failure injection, embedded-vs-non-embedded full wire roundtrip | OPEN |

> **Conclusion of Round-4.**  The five release-blocker findings are
> closed atomically with regression coverage that pins the most
> dangerous of them (CR-D26) byte-exactly.  The two MEDIUM findings
> (CR-D31 / CR-D32) are real defence-in-depth weakenings worth fixing
> in the next cycle but do not in themselves enable an attack with the
> current call graph.  The LOW/INFO items are test-debt and ergonomics.

---

## 2. Closed in this round (CRITICAL + HIGH)

### CR-D26 — schema/struct width mismatch on `zk_proof_size_per_participant`

**File:** `module/crypto/src/sig/chipmunk/chipmunk_ring_serialize_schema.c`
(schema entry); `module/crypto/src/sig/chipmunk/chipmunk_ring.h`
(struct field).

The schema declared the field as `DAP_SERIALIZE_TYPE_UINT64` with
`sizeof(uint64_t)`, but the in-struct type is `uint32_t`.  On
`dap_serialize_to_buffer` the codec read 8 bytes from a 4-byte slot —
the upper 4 bytes contained whatever happened to live at
`offsetof(chipmunk_ring_signature_t, zk_proof_size_per_participant) + 4`,
which on the canonical x86_64 layout is `zk_iterations`.  On
`dap_serialize_from_buffer` the codec wrote 8 bytes into a 4-byte slot,
silently corrupting `zk_iterations`.

**Impact.**  Both directions are integrity bugs.  A signature whose
`zk_iterations != 0` would, after a wire roundtrip, end up with
`zk_iterations` overwritten by the high half of `zk_proof_size_per_participant`,
producing a mismatched signature on re-verification.  A signature
serialised with `zk_iterations` containing any non-deterministic byte
(structure padding pre-CR-D29, etc.) would leak that byte onto the
wire.

**Fix.**  The valid value range for `zk_proof_size_per_participant` is
`[CHIPMUNK_RING_ZK_PROOF_SIZE_MIN .. CHIPMUNK_RING_ZK_PROOF_SIZE_MAX]`
= `[32..128]`, comfortably 32-bit.  Schema entry switched to
`DAP_SERIALIZE_TYPE_UINT32` with `sizeof(uint32_t)`, matching the
struct exactly.  Cross-host width stability (the original CR-D9
motivation for a fixed-width carrier) is preserved by `uint32_t`.

**Regression.**  `s_test_zk_size_iterations_wire_roundtrip` in
`tests/unit/crypto/chipmunk_ring/test_chipmunk_round3_regression.c`
stamps both fields with non-default sentinels
(`zk_proof_size_per_participant = 0xA55A5AA5`,
`zk_iterations = 0xDEADBEEF`), runs serialise → deserialise →
re-serialise, and asserts byte-exact preservation plus bit-identical
re-serialisation.  Pre-fix this test would have failed via field
aliasing in the first assertion.

### CR-D27 — dead duplicate ring-size guard with leak path

**File:** `module/crypto/src/sig/chipmunk/chipmunk_ring.c:624` (pre-fix).

A late `if (a_ring->size > CHIPMUNK_RING_MAX_RING_SIZE) return -EINVAL`
sat after `signature->ring_hash`, `signature->challenge`, and
`signature->linkability_tag` had already been allocated.  The same
bound was already enforced up-front (line ~536), so this guard was
unreachable under non-racy conditions; on the rare case where the
ring grew between calls, the late guard returned without freeing the
partially-initialised signature.

**Fix.**  Removed the dead duplicate; replaced with a header comment
demanding that any future ring-sizing change must enforce the bound
before any `DAP_NEW_*` allocation lands in `a_signature`.

### CR-D28 — `ring_hash` leak + missing overflow guard in `container_create`

**File:** `module/crypto/src/sig/chipmunk/chipmunk_ring.c:446..476` (pre-fix).

Two error paths (`combined_keys` allocation failure and `dap_hash_fast`
failure) released `a_ring->public_keys` but kept the freshly-allocated
`a_ring->ring_hash` dangling.  Additionally, the size computation
`a_num_keys * l_key_data_size` had no defensive overflow check; while
`CHIPMUNK_RING_MAX_RING_SIZE` bounds `a_num_keys`, future widening of
the bound or of `l_key_data_size` would silently wrap on 32-bit hosts.

**Fix.**  Both error paths now free `ring_hash` and zero the related
size field for state consistency; an explicit
`l_key_data_size != 0 && a_num_keys > SIZE_MAX / l_key_data_size`
check rejects overflow before `DAP_NEW_SIZE`.

### CR-D29 — multi-signer ZK loop "succeeds" with all-zero proof slots

**File:** `module/crypto/src/sig/chipmunk/chipmunk_ring.c:797..867` (pre-fix).

The loop generating per-participant ZK proofs in
`chipmunk_ring_sign_internal` used `continue` on every error path
(scratch alloc failure, schema serialise failure, response-input
serialise failure).  Because `threshold_zk_proofs` was zero-filled
by `DAP_NEW_Z_SIZE`, a `continue` left the slot all-zero — and the
function still returned `0` to the caller, claiming a valid signature
with structurally bogus material.  A verifier recomputing the proof
for the same slot under the same domain/salt/input would produce a
non-zero value that disagrees with the all-zero slot, but the
producer side had already declared success.

**Fix.**  Every failure path in the loop now logs the cause, frees
its scratch buffers, calls `chipmunk_ring_signature_free(a_signature)`,
and returns `-ENOMEM` / `-1`.  The all-or-nothing semantics expected
by callers are restored.

### CR-D30 — `dap_enc_chipmunk_ring_write_signature` bypassed parametric `size_params`

**File:** `module/crypto/src/dap_enc_chipmunk_ring.c:402..438` (pre-fix).

The function called `dap_serialize_to_buffer(..., NULL)` directly,
bypassing the parametric `size_params` (`ring_size`,
`use_embedded_keys`, `required_signers`) that
`chipmunk_ring_signature_schema` requires for its parametric
`ARRAY_DYNAMIC` fields.  The canonical wrapper
`chipmunk_ring_signature_serialize()` exists exactly to thread these
args through.  Calling the bare API silently produced a *different*
encoding than the chipmunk_ring-level codec.

**Fix.**  `write_signature` now routes through
`chipmunk_ring_signature_serialize`, eliminating the divergence and
making the canonical wrapper the single source of truth for the
on-wire format.

---

## 3. Open findings (MEDIUM)

### CR-D31 — `s_domain_hash` uses `strlen(domain)` and unprefixed concatenation

**Files:** `module/crypto/src/sig/chipmunk/chipmunk_ring.c:86..170`,
`module/crypto/src/sig/chipmunk/chipmunk_ring_acorn.c:43..` (duplicated
helper, see CR-C18 / CR-D11 for unification).

The PRK input is built as `domain || salt || input` with no length
prefix on any of the three components and a `strlen(a_domain)` length
for the domain.  All current call sites supply a fixed string literal
for `a_domain` (`CHIPMUNK_RING_ZK_DOMAIN_MULTI_SIGNER`,
`CHIPMUNK_RING_DOMAIN_ACORN_COMMITMENT`, etc.), so today no caller
can collide them by varying `salt` / `input` to bleed into the domain
prefix.  However:

1. The construction is *structurally* a Merkle-Damgård prefix
   collision target: an attacker who finds two triples
   `(D₁, S₁, I₁)` and `(D₂, S₂, I₂)` with identical concatenations
   gets identical PRKs.  The literal-domain assumption removes the
   D-side, but `S` (challenge salt) and `I` (response input) are
   schema-derived and variable-length, so any future schema
   extension that lets an adversary control the boundary is a
   silent regression.
2. `strlen` on a NUL-containing domain string is a footgun; a
   future refactor that derives the domain from binary input
   (e.g. `H(label) || version`) would silently truncate at the
   first NUL.

**Recommended fix (deferred):** rebuild the helper as a TupleHash-style
construction:
`PRK = SHA3-256( LE32(len(D)) || D || LE32(len(S)) || S || LE32(len(I)) || I )`
and bump every domain tag to a `/v2` suffix.  This is a
wire-breaking change for ChipmunkRing only (no production deployments
exist on `feature/chipmunk-ring`).  Add a regression that exercises
two structurally-different `(D, S, I)` triples whose concatenation
collides under the old construction.

### CR-D32 — `ring_hash_size` hard-pinned to `CHIPMUNK_RING_LINKABILITY_TAG_SIZE`

**File:** `module/crypto/src/sig/chipmunk/chipmunk_ring.c:419..420`.

```
a_ring->ring_hash_size = CHIPMUNK_RING_LINKABILITY_TAG_SIZE; // Standard hash output size
a_ring->ring_hash      = DAP_NEW_Z_SIZE(uint8_t, a_ring->ring_hash_size);
```

The size is taken from the linkability-tag constant rather than from
the active hash output.  After CR-D8 the linkability tag is a
zero-filled reserved slot, so `CHIPMUNK_RING_LINKABILITY_TAG_SIZE`
no longer carries hash-output semantics; the coupling is purely
historical.  `dap_hash_fast` produces a 32-byte output today
(SHA3-256 path), so the buffer size happens to be correct, but a
future hash swap (e.g. SHAKE256 with a different output width or
SHA3-384) would either truncate the hash or read past the buffer.

**Recommended fix (deferred):** introduce a single
`CHIPMUNK_RING_RING_HASH_SIZE` constant (or derive it from the active
hash via `dap_hash_type_output_size`) and use it both for sizing the
buffer and for any later `memcpy`.  Add a `_Static_assert` linking
the constant to the hash-algorithm output size to prevent silent
drift.

---

## 4. Open findings (LOW / INFO)

### CR-D33 — local `tests/unit/crypto/chipmunk_ring/CMakeLists.txt` dead-included

**File:** `tests/unit/crypto/chipmunk_ring/CMakeLists.txt` (vs the
parent `tests/CMakeLists.txt` which already registers each test
explicitly).

The local `CMakeLists.txt` exists but is not `add_subdirectory()`-d
from any parent — every test target is registered at the parent
level.  The local file is therefore stale: its target list drifts
from the parent's, and a contributor editing the local file in the
belief that it is authoritative will see their changes silently
ignored.

**Recommended fix (deferred):** delete the local `CMakeLists.txt` (or
make it the single source of truth and `add_subdirectory()` it from
the parent — pick one).  A `cmake -Wno-dev` lint job would have
caught this.

### CR-D34 — `test_chipmunk_ring_sign(message_size == 0)` contract divergence

**Files:** `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_simple.c`
vs `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_input_validation.c`.

`test_simple` exercises the happy path with a non-empty message;
`test_input_validation` asserts that `message_size == 0` is rejected.
The actual `chipmunk_ring_sign` implementation accepts `message_size
== 0` only for `a_message != NULL`, but the validation test asserts
unconditional rejection.  Both tests pass today only because each
exercises a different code path.

**Recommended fix (deferred):** decide and document the contract
(reject all empty messages? reject only `NULL` message? accept both?).
Update both tests to share the same contract assertion.

### CR-D35 — coverage gaps

The `chipmunk_ring` test corpus is missing:

* `k = 1` regression at the dap_enc-level wrapper
  (`dap_enc_chipmunk_ring_*`).  The chipmunk_ring-level path is
  covered by `test_chipmunk_round3_regression`, but the wrapper
  layer is not exercised at the smallest ring size.
* Maximum-k stress (`k = CHIPMUNK_RING_MAX_RING_SIZE`).  Existing
  perf-focused tests use ring sizes ≤ 16; the boundary at the
  declared maximum is uncovered.
* Concurrent-sign test:  multiple threads sharing a ring container
  and signing under different keys.  Important since the
  hypertree-backed `chipmunk_ring_sign` mutates the caller's
  private-key buffer (leaf-index bump) — any concurrency contract
  must be explicit.
* Allocator-failure injection:  the new abort paths in CR-D29 and
  the leak fixes in CR-D28 are unreachable without an injected
  failure.  A simple wrap-around `DAP_NEW_*` that fails on the
  N-th call would exercise every error path deterministically.
* Full Ring wire-roundtrip with embedded-vs-non-embedded mode
  toggle:  CR-D26's regression covers the `embedded == true`
  branch.  The `embedded == false` branch is not exercised at the
  same level, which leaves CR-D30's parametric-args fix uncovered
  for one of the two valid encodings.

**Recommended fix (deferred):** add the five tests above as a
single `Round-4 coverage uplift` commit; each takes < 60 lines of
test code and contributes deterministic regression value.

---

## 5. Verification of Round-3 closures (sanity check)

Round-4 explicitly re-checked every CR-D1..CR-D25 closure:

| Round-3 ID  | Round-4 status                                                                                                | Notes |
|-------------|---------------------------------------------------------------------------------------------------------------|-------|
| CR-D1..D6   | Closed (CRITICAL); regression-covered in `test_chipmunk_round3_regression`                                    | OK    |
| CR-D7       | Closed via Hadamard / poly-mul rewrite; no Round-4 regressions                                                 | OK    |
| CR-D8       | Hard-deprecated; `linkability_tag` is a reserved zero slot; verifier rejects non-zero                          | OK    |
| CR-D9       | Wire canonicality regression covers `use_embedded_keys` malleability; `zk_proofs_size` width pinned (uint64)   | OK    |
| CR-D10      | Fake-SHAKE removed; native `dap_hash_shake128` round-tripped                                                   | OK    |
| CR-D11      | SHA3/SHAKE-only policy enforced; SHA2 helpers purged                                                           | OK    |
| CR-D12      | `strlen` removed from binary-message paths; explicit `message_len` everywhere                                  | OK    |
| CR-D13      | `dap_memwipe` standardised on private-key/seed buffers                                                         | OK    |
| CR-D14      | Constant-time challenge sampler                                                                                | OK    |
| CR-D15      | Hypertree (height=7) replaces leaf-0 stub; HOTS leaf bump enforced                                             | OK    |
| CR-D15.C    | Ring-level pk/sk == hypertree pk/sk; serialised byte-for-byte via `chipmunk_hypertree_*_to_bytes`              | OK    |
| CR-D16      | Centred-normalisation fix in `chipmunk_poly_add_ntt`                                                           | OK    |
| CR-D17/D18  | `s_debug_more` defaulted off                                                                                   | OK    |
| CR-D19      | Duplicate-pk + zero-pk rejection in `chipmunk_ring_container_create`                                            | OK    |
| CR-D20      | Private-key serialisation goes through canonical hypertree codec                                                | OK    |
| CR-D21      | Closed as obsolete; challenge derivation is schema-driven on both sign and verify paths                        | OK    |
| CR-D22      | `dap_random_bytes` thread-safety regression added                                                              | OK    |
| CR-D23      | `chipmunk_randomizers_generate_random` rewired through `dap_random_bytes`                                      | OK    |
| CR-D24      | `chipmunk_ring_secret_sharing.c` removed; threshold rewrite scheduled under master-plan CR-9                   | DEFERRED — CR-9 is its own master-plan phase |
| CR-D25      | `secure_clean` replaced with `dap_memwipe`                                                                     | OK    |

The two items marked `DEFERRED` (CR-9 threshold rewrite and the open
CR-11 OR-proof / R-09 reproducibility) are explicit master-plan
phases, not in-scope for Round-4 sweep.

---

## 6. Risk matrix (Round-4)

| ID      | Severity   | Class                       | Status              |
|---------|------------|-----------------------------|---------------------|
| CR-D26  | CRITICAL   | wire-format integrity       | **CLOSED** (227a933f) |
| CR-D27  | HIGH       | memory leak                 | **CLOSED** (227a933f) |
| CR-D28  | HIGH       | memory leak + overflow      | **CLOSED** (227a933f) |
| CR-D29  | HIGH       | abort semantics             | **CLOSED** (227a933f) |
| CR-D30  | HIGH       | encoding divergence         | **CLOSED** (227a933f) |
| CR-D31  | MEDIUM     | domain separation           | OPEN (deferred)     |
| CR-D32  | MEDIUM     | hash output sizing          | OPEN (deferred)     |
| CR-D33  | LOW        | build hygiene               | OPEN (deferred)     |
| CR-D34  | LOW        | contract divergence (tests) | OPEN (deferred)     |
| CR-D35  | INFO       | coverage gap                | OPEN (deferred)     |

---

## 7. Acceptance criteria for the next cycle

The next remediation pass closes Round-4 when:

1. CR-D31 — `s_domain_hash` rebuilt with TupleHash-style length prefixes
   and domain tags bumped to `/v2`.  Regression exercises two
   structurally-different `(D, S, I)` triples whose concatenation
   collides under the old construction.
2. CR-D32 — `ring_hash_size` derived from the active hash output
   (`dap_hash_type_output_size`) with `_Static_assert` against the
   constant; existing buffers re-fitted.
3. CR-D33 — local `CMakeLists.txt` either deleted or re-wired as the
   single source of truth.
4. CR-D34 — `chipmunk_ring_sign(message_size == 0)` contract documented
   in the public header and asserted identically across both tests.
5. CR-D35 — five new tests added: `k=1` at dap_enc-level, max-k stress,
   concurrent sign, allocator-failure injection, embedded-vs-non-embedded
   wire roundtrip.

---

## 8. References

* Round-3 audit: `doc/crypto/chipmunk_ring/security_review_round3_cr7_critical_findings.md`
* Round-2 audit: `doc/crypto/chipmunk_ring/security_review_round2_cr6_critical_reaudit.md`
* Master plan (SLC):  `task_d941ecf40f260cf8`
* Active task (SLC):  `task_5e817549`
* Round-4 fix commit: `227a933f` on `feature/chipmunk-ring`

---

*Round-4 audit, 2026-05-12.*
