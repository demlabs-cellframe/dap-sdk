---
doc: design_decision_cr9_5
phase: CR-9.5 — Proof-of-Possession against rogue-key attack
status: ACTIVE
predecessors:
  - doc/crypto/chipmunk_ring/design_decision_cr9.md     # CR-9 master design
  - doc/crypto/chipmunk_ring/design_decision_cr9_4.md   # CR-9.4 public threshold API
---

# CR-9.5 — Proof-of-Possession against rogue-key attack: Design Decision

> **Scope.**  CR-9.5 closes the rogue-key attack class for the
> CR-9.4.A trusted-dealer threshold flow (and reserves the same
> primitive for the deferred CR-9.4.B true-threshold flow).  The
> primitive is also useful outside the threshold context: any
> caller who collects ring-member public keys from untrusted
> sources can now demand a PoP at acceptance time and reject any
> public key whose claimer cannot prove possession of the
> corresponding secret.

---

## 1. Why we need PoP at all

In any signature aggregation scheme — threshold, ring, or plain
multi-sig — there is a classic *rogue-key* attack:

```
Honest participants publish pk_1, …, pk_{i-1}, pk_{i+1}, …, pk_n.
Attacker publishes  pk_rogue  such that
  pk_rogue := pk_target  −  Σ_{j ≠ i}  pk_j
```

If the aggregation operator is linear (`pk_combined = Σ pk_j`),
the attacker now controls a "combined" public key under which any
signature produced with their fully-known `pk_target` material
verifies, even though they never held the master secret.  The
defence is a **Proof of Possession**: at the moment a new
participant publishes `pk_i`, the verifier demands a signature on
a fixed, public, pk-bound message under `sk_i`.  An attacker who
chose `pk_rogue` algebraically cannot produce this signature
because they never knew the corresponding `sk_rogue`.

### 1.1 Applicability to CR-9.4.A (this slice)

CR-9.4.A is a *trusted-dealer* model: the master `sk` is shared
out by a dealer who already knows the master, and the combiner
reconstructs the same master `sk`.  The pure-form rogue-key
attack does *not* directly apply: the combiner never sums
participant pks; it Lagrange-interpolates over a Shamir
polynomial committed to by the dealer.

However, CR-9.4.A still has two practical gaps that PoP closes:

1. **Ring-container construction** (`chipmunk_ring_container_create`)
   currently accepts any 32-byte sequence as a "public key".  A
   participant who submits a pk for which they do not hold the
   corresponding sk can later claim accountability for a signature
   they did not produce (or shift blame for one they did).  PoP
   binds claim ↔ ownership at acceptance time, not at signing time.
2. **CR-9.4.B (deferred)** is a *true* threshold flow without
   reconstruction, where the aggregator linearly combines per-
   share pks.  PoP is mandatory there.  Shipping the PoP
   primitive in CR-9.5 means CR-9.4.B inherits the defence for
   free — no new design surface needed when it lands.

### 1.2 Out of scope for this slice

* Distributed key generation (DKG) — orthogonal; PoP works the
  same way regardless of how `sk` was created.
* Per-participant ID binding (PoP only proves ownership of `sk`,
  not "this `sk` belongs to user X").  Identity binding is the
  PKI layer's job and is tracked under CR-11.

---

## 2. Construction

The PoP is a hypertree signature under `sk_i` over a fixed,
domain-separated, pk-bound message:

```
pop_message = SHA3-256(  "chipmunk-ring-pop/v1"
                       || LE32(len(pk_i_bytes))
                       || pk_i_bytes )

pop_sig     = chipmunk_ht_sign(sk_i, pop_message, 32)
```

The wire blob is the canonical 8-byte header + serialised
hypertree signature:

```
+0   magic    'CRRP' LE     (4 B)
+4   version  1             (1 B)
+5   reserved 0x00 × 3      (3 B)   — keeps the body 8-byte aligned
+8   sig      ht_sig bytes  (CHIPMUNK_HT_SIGNATURE_SIZE)
```

Magic `'CRRP'` (Chipmunk Ring **R**ing-**P**roof) is distinct
from the threshold-share magic `'CRHS'` (CR-9.4.A) and from the
reserved partial-sig magic `'CRHP'` (CR-9.4.B) — no two CR-9
wire formats can collide.

### 2.1 Why hypertree and not a smaller primitive

`chipmunk_ht_sign` is the canonical primitive that already
verifies under `chipmunk_ring_public_key_t`.  Using anything
smaller would either:

* require introducing a *second* algorithm (and its security
  proof, and its KAT suite, and its parameter set) — substantial
  surface for marginal gain, or
* require deriving a separate HOTS PoP-keypair from
  `key_seed` under a new domain separator (cleaner but
  requires extending `chipmunk_ht_*` with a second derivation
  chain, which is itself a multi-day slice).

The trade-off is wire size: a PoP blob is ~40 KB.  This is
acceptable because PoP is a *one-time* per-participant artefact
sent at ring-registration, not per-signature.  A compact PoP
primitive is an optimisation, tracked under CR-11 ("publication
readiness") rather than here.

### 2.2 Why the PoP consumes one HOTS leaf slot

`chipmunk_ht_sign` advances `sk->leaf_index` atomically; the
PoP occupies the first leaf (`leaf_index = 0`), leaving 63 of
the 64 production-signing slots free.  The trade-off is honest:

* **Pros**: zero new code in `chipmunk_ht_*`, zero new wire
  format below the PoP envelope, zero new KAT surface.
* **Cons**: callers who already consumed `leaf_index = 0` on
  production data **cannot** retroactively produce a CR-9.5
  PoP under the same sk — the slot is gone.

The contract `chipmunk_ring_pop_create` enforces is:
**"call this before any production signing with this sk; the
function returns -EBUSY if `sk->leaf_index != 0`"**.  This is
the cleanest failure mode possible — silent re-use would let
two production signatures share a leaf, which is *exactly* the
CR-D3 bug we cannot tolerate.

A future CR-11 follow-up can move PoP onto a dedicated PoP-only
HOTS keypair derived from `key_seed` under
`"chipmunk-ring-pop-hots/v1"`, restoring the full 64-slot
production budget.  This document explicitly tracks that
follow-up as **CR-11.PoP-OPT-1** so it cannot get lost.

---

## 3. Public API surface

```c
/* module/crypto/include/dap_chipmunk_ring_threshold.h — extension */

#define CHIPMUNK_RING_POP_MAGIC            0x50525243u   /* 'CRRP' LE */
#define CHIPMUNK_RING_POP_VERSION          1u
#define CHIPMUNK_RING_POP_HEADER_BYTES     8u
#define CHIPMUNK_RING_POP_BODY_BYTES       CHIPMUNK_HT_SIGNATURE_SIZE
#define CHIPMUNK_RING_POP_BYTES                                                 \
    (CHIPMUNK_RING_POP_HEADER_BYTES + CHIPMUNK_RING_POP_BODY_BYTES)

/**
 * Produce a Proof-of-Possession for the public key extracted
 * from @a a_sk.  Internally:
 *   1. serialise pk = sk->pk via chipmunk_ht_public_key_to_bytes,
 *   2. derive pop_message = SHA3-256(domain || LE32(len) || pk_bytes),
 *   3. sign pop_message under sk (consumes leaf_index 0),
 *   4. wrap the signature with the CR-9.5 envelope.
 *
 * @return 0 on success;
 *         -EINVAL on NULL input;
 *         -EBUSY  if sk->leaf_index != 0 (the PoP slot is gone);
 *         negative chipmunk_ht_sign / serialise errors otherwise.
 *         On any error the output buffer is fully zeroised.
 */
int chipmunk_ring_pop_create(chipmunk_ht_private_key_t *a_sk,
                             uint8_t a_out_pop[CHIPMUNK_RING_POP_BYTES]);

/**
 * Verify a PoP blob against a public key.  Validates the envelope
 * (magic, version, reserved bytes all zero) before deserialising
 * the signature.  Recomputes pop_message from the supplied pk_bytes
 * — the verifier does NOT trust any message accompanying the blob.
 *
 * @return 0 on PoP success;
 *         -EINVAL on contract violation (NULL, bad magic/version,
 *                 non-zero reserved bytes, malformed signature);
 *         CHIPMUNK_ERROR_VERIFY_FAILED on signature mismatch.
 */
int chipmunk_ring_pop_verify(const chipmunk_ht_public_key_t *a_pk,
                             const uint8_t a_pop[CHIPMUNK_RING_POP_BYTES]);

/**
 * Convenience overload that takes the serialised pk-bytes form
 * (so a caller assembling a ring container from on-wire pk
 * blobs does not have to round-trip through the in-memory
 * struct).  Identical semantics; same return values.
 */
int chipmunk_ring_pop_verify_bytes(const uint8_t a_pk_bytes[CHIPMUNK_HT_PUBLIC_KEY_SIZE],
                                   const uint8_t a_pop[CHIPMUNK_RING_POP_BYTES]);
```

The PoP primitive is published alongside the threshold API (same
header) because every CR-9.4 caller is also the natural caller
of PoP — keeping them in one include avoids forcing callers to
hunt for a second header at registration time.

---

## 4. Failure-mode discipline

Same patterns as CR-9.3 / CR-9.4:

1. **Zeroise on every error path.**  `create` clears the PoP
   blob; `verify` is read-only (no output) but its `pop_message`
   scratch is wiped before return.
2. **No leaf re-use.**  `create` rejects `leaf_index != 0` with
   `-EBUSY` — the cleanest possible failure when the budget is
   gone.  Silent re-use would replay a leaf and leak the HOTS
   secret per CR-D3.
3. **Envelope integrity before crypto.**  `verify` rejects bad
   magic / version / reserved bytes *before* doing any
   signature verification — saves cycles and keeps the
   side-channel surface minimal for malformed inputs.
4. **No trusted message accompanying the blob.**  `verify`
   recomputes `pop_message` from the caller-supplied pk; a
   blob is not a self-describing message-and-signature pair.
   This eliminates the entire "did the attacker craft the
   message to match a leaked signature?" class.

---

## 5. Test acceptance (CR-9.5)

A new test executable
`test_unit_crypto_chipmunk_ring_pop` registers the following
contracts; the design rejects shipping any one of them as
"not-applicable".

| Test                                              | Contract                                                                                                                       |
|---------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------|
| `test_pop_roundtrip_deterministic`                | `verify(pk, create(sk)) == 0` for a deterministic seeded keypair                                                                |
| `test_pop_roundtrip_random`                       | Same, over many random keypairs                                                                                                 |
| `test_pop_create_rejects_used_sk`                 | After one `chipmunk_ht_sign`, `pop_create` returns `-EBUSY`, output zeroised                                                    |
| `test_pop_create_rejects_null`                    | NULL sk or NULL out → `-EINVAL`                                                                                                 |
| `test_pop_verify_rejects_wrong_pk`                | PoP made by `sk_A`, verified under `pk_B` → `CHIPMUNK_ERROR_VERIFY_FAILED`                                                      |
| `test_pop_verify_rejects_bad_magic`               | Tamper the magic byte → `-EINVAL`                                                                                               |
| `test_pop_verify_rejects_bad_version`             | Tamper the version byte → `-EINVAL`                                                                                             |
| `test_pop_verify_rejects_nonzero_reserved`        | Tamper any reserved byte → `-EINVAL`                                                                                            |
| `test_pop_verify_rejects_tampered_signature`      | Tamper one byte in the signature body → `CHIPMUNK_ERROR_VERIFY_FAILED`                                                          |
| `test_pop_verify_bytes_equivalence`               | `verify(pk, pop) == verify_bytes(pk_bytes, pop)` always                                                                          |
| `test_pop_rogue_key_attack_rejected`              | *The* regression test: construct `pk_rogue = pk_target ⊕ pk_alibi` (algebraically, no sk_rogue ever held), attempt to register pk_rogue with a forged PoP recovered from pk_target's PoP — `pop_verify` rejects with `CHIPMUNK_ERROR_VERIFY_FAILED`.  This is the explicit rogue-key attack scenario, exercised end-to-end. |
| `test_pop_zeroisation_on_error`                   | Every error path in `create` leaves the output buffer fully zeroised                                                            |

Total: 12 dedicated tests.

---

## 6. Decisions log

| #     | Decision                                                                                                                       | Rationale                                                                                                                  |
|-------|--------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------|
| D-1   | PoP signs `SHA3-256("chipmunk-ring-pop/v1" || LE32(len(pk)) || pk_bytes)`                                                       | Length-prefix is the TupleHash discipline established in CR-D31; eliminates prefix-collision attacks on the pop_message    |
| D-2   | Use `chipmunk_ht_sign` rather than introducing a new smaller primitive                                                          | Re-uses an already-audited primitive; eliminates new KAT/proof surface; wire-size cost is acceptable for a one-time artefact |
| D-3   | PoP consumes leaf_index 0; max production sigs = 63 after PoP                                                                  | Cleanest possible failure mode; silent leaf-reuse would replay HOTS per CR-D3.  CR-11.PoP-OPT-1 tracks the dedicated-PoP-keypair follow-up |
| D-4   | `create` returns `-EBUSY` if `sk->leaf_index != 0`                                                                              | Explicit refusal beats silent slot-stealing; matches POSIX semantics for "resource is in use elsewhere"                    |
| D-5   | Magic `'CRRP'` distinct from `'CRHS'` (shares) and reserved `'CRHP'` (partial sigs)                                              | All three CR-9 wire blobs can co-exist in storage without collision                                                         |
| D-6   | `verify` recomputes `pop_message` from caller-supplied pk; never trusts a message accompanying the blob                         | Eliminates the entire "attacker crafted the message to match a leaked signature" class                                      |
| D-7   | `verify_bytes` published as a separate entry so callers handling on-wire pk blobs do not have to round-trip through the struct  | Matches the typical ring-registration flow (pk_bytes received over the wire, validated, then collected into a container)    |
| D-8   | PoP envelope is a fixed 8-byte header — no length prefix on the sig body, because `CHIPMUNK_HT_SIGNATURE_SIZE` is compile-fixed | Removes one parser branch; any future signature-size change bumps `CHIPMUNK_RING_POP_VERSION`                                |

---

*CR-9.5 design slice, 2026-05-16.*
