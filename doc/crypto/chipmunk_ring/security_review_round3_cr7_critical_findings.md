---
doc: security_review_round3_critical_findings
phase: CR-7
status: ACTIVE
classification: CRITICAL — CATASTROPHIC FLAWS CONFIRMED
audit_rounds:
  - CR-0..CR-5 (initial planning & closure)
  - CR-6 (Round-2 re-audit, meta-level)
  - CR-7 (Round-3 code deep-dive) ← THIS DOCUMENT
---

# Chipmunk Ring — Round-3 Security Audit (Code Deep-Dive)

> **TL;DR.**  Round-2 warned that the scheme was over-closed on paper; Round-3 is a
> line-by-line code audit of the actual implementation.  The results are
> worse than expected.  Multiple **catastrophic** cryptographic flaws are
> confirmed in production code paths.  The scheme in its current form is
> **completely broken** — forgery, de-anonymisation and private-key recovery
> are all trivial.  Previous audit rounds (CR-0..CR-5) and the scientific
> paper systematically failed to detect any of these flaws because they
> stayed at the architectural level and trusted the implementation.
>
> **Directive:** `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING` must not be used for any
> security-relevant purpose until every CR-D* finding below is resolved and
> independently re-verified.

---

## 0. Audit scope

| Layer                            | Files                                                                                                              | Status |
|----------------------------------|--------------------------------------------------------------------------------------------------------------------|--------|
| Base Chipmunk signature          | `chipmunk.c`, `chipmunk_hots.c`, `chipmunk_hash.c`, `chipmunk_poly.c`, `chipmunk_aggregation.c`                     | AUDITED |
| Chipmunk Ring core               | `chipmunk_ring.c`, `chipmunk_ring_acorn.c`, `chipmunk_ring_serialize_schema.{c,h}`                                  | AUDITED |
| DAP SDK wrappers                 | `dap_enc_chipmunk.c`, `dap_enc_chipmunk_ring.c`                                                                    | AUDITED |
| Secondary / dead code            | `chipmunk_ring_secret_sharing.c`, `chipmunk_batch_verify_hots.c`                                                    | SPOT-CHECK |
| RNG infrastructure               | `dap_rand.c`, `dap_time.h`                                                                                         | AUDITED |

Legend used throughout:

* **[CRITICAL]** — breaks fundamental security property (forgery /
  de-anonymisation / key recovery).  Exploitable today with trivial
  resources.
* **[HIGH]** — significant weakening but not yet catastrophic on its own.
* **[MEDIUM]** — weakens defence-in-depth, enables exploitation once another
  bug is leveraged.
* **[LOW]** — hygiene / maintainability.

---

## 1. Executive summary of Round-3

| ID      | Severity   | Component                           | One-line description                                                                 |
|---------|------------|-------------------------------------|--------------------------------------------------------------------------------------|
| CR-D1   | CRITICAL   | `chipmunk_ring.c`                   | Universal forgery — single-signer verify does not check the Chipmunk signature       |
| CR-D2   | CRITICAL   | `chipmunk_ring.c`                   | Anonymity break — signer identified by explicit ring-scan verify                     |
| CR-D3   | CRITICAL   | `chipmunk.c` / `chipmunk_hots.c`    | OTS key reuse — signing 2 messages reveals full private key                          |
| CR-D4   | CRITICAL   | `chipmunk.c`                        | Entropy collapse — key material derived from `time(NULL)` + static counter           |
| CR-D5   | CRITICAL   | `chipmunk_poly.c`                   | Challenge polynomial sampled via 32-bit LCG — 2^256 → 2^32 security drop             |
| CR-D6   | CRITICAL   | `chipmunk_aggregation.c`            | Verification “forgiveness” (~10% coefficient slack) enables algebraic forgery        |
| CR-D7   | HIGH       | `chipmunk_aggregation.c`            | NTT-domain math error: coefficient-wise multiply used as polynomial multiply         |
| CR-D8   | HIGH       | `chipmunk_ring.c`                   | Linkability tags neither truly linkable nor unlinkable                               |
| CR-D9   | HIGH       | `chipmunk_ring_serialize_schema.c`  | Signature malleability — `use_embedded_keys` unused bits, `size_t` width mismatch    |
| CR-D10  | HIGH       | `chipmunk_hash.c`                   | “SHAKE128” is a homebrew SHA2-256 counter construction, not SHAKE                    |
| CR-D11  | HIGH       | `chipmunk_hash.c`                   | Inconsistent hash primitives (SHA2-256 vs SHA3-256) used interchangeably             |
| CR-D12  | HIGH       | `chipmunk_aggregation.c`            | `strlen()` used on binary message — silent truncation at first NUL byte              |
| CR-D13  | HIGH       | `chipmunk.c` / `dap_enc_chipmunk.c` | Sensitive seeds/keys not zeroised — leaked to stack/heap on return                   |
| CR-D14  | HIGH       | `chipmunk_poly.c`                   | `chipmunk_poly_challenge` runs in data-dependent time (timing side-channel)          |
| CR-D15  | HIGH       | `chipmunk.c`                        | HOTS is always issued from Merkle leaf 0 → effectively no tree                       |
| CR-D16  | MEDIUM     | `chipmunk_poly.c`                   | `chipmunk_poly_add_ntt` uses centred normalisation incompatible with NTT domain      |
| CR-D17  | MEDIUM     | `chipmunk_aggregation.c`            | Debug logging enabled by default (`s_debug_more = true`)                             |
| CR-D18  | MEDIUM     | `chipmunk_ring_serialize_schema.c`  | Debug logging enabled by default                                                     |
| CR-D19  | MEDIUM     | `chipmunk_ring.c`                   | No duplicate/invalid public key check in `chipmunk_ring_container_create`            |
| CR-D20  | MEDIUM     | `dap_enc_chipmunk.c`                | Private-key serialisation is a raw `memcpy` — format non-canonical, no zeroisation   |
| CR-D21  | MEDIUM     | `chipmunk_ring.c`                   | Challenge verification uses ad-hoc concatenation instead of schema-driven hash       |
| CR-D22  | MEDIUM     | `dap_rand.c`                        | Buffered `/dev/urandom` cache is not thread-safe (race on `s_buf_pos`)               |
| CR-D23  | LOW        | `chipmunk_aggregation.c`            | Exported `chipmunk_randomizers_generate_random` uses `rand() % 3`                    |
| CR-D24  | LOW        | `chipmunk_ring_secret_sharing.c`    | Dead code with broken semantics (wrong pk extraction, untested)                      |
| CR-D25  | LOW        | `chipmunk_ring.c`                   | `secure_clean` uses `volatile void*` — not guaranteed to survive optimiser           |

> **Conclusion.**  Out of 25 findings, **6 are catastrophic** and affect the
> three core security properties (unforgeability, anonymity, key secrecy).
> None of these were flagged in any of the five previous internal audit
> rounds or in the scientific paper.  The scheme cannot be considered
> "almost production-ready" — it is currently equivalent to a signature
> scheme that has no private key at all on the verifier side.

---

## 2. Catastrophic findings (CRITICAL)

### CR-D1 — Universal forgery: single-signer verify does not validate the Chipmunk signature

**Files:** `module/crypto/src/sig/chipmunk/chipmunk_ring.c:1070-1151`

In single-signer mode (`required_signers == 1`), `chipmunk_ring_verify`
executes **only** the Acorn-proof branch:

```1116:1120:module/crypto/src/sig/chipmunk/chipmunk_ring.c
int l_acorn_result = s_domain_hash(CHIPMUNK_RING_DOMAIN_ACORN_COMMITMENT,
                                   NULL, 0,
                                   l_acorn_input, l_acorn_input_size,
                                   l_expected_acorn_proof, a_signature->acorn_proofs[l_i].acorn_proof_size,
                                   a_signature->zk_iterations);
```

The `l_acorn_input` passed to `s_domain_hash` is composed entirely of data
contained in the signature itself (`public_key`, `randomness`, `message`).
The branch that **would** call `chipmunk_verify` on the embedded Chipmunk
signature does not exist — `a_signature->signature` is never used by the
verifier in this mode.

**Exploit.**  An attacker who knows the ring and the message can:

1. Choose any index `i` (e.g. `0`).
2. Choose an arbitrary `randomness` buffer.
3. Compute `acorn_proof_i = iterated_hash(pk_i ‖ randomness ‖ message)`.
4. Fill the rest of `chipmunk_ring_signature_t` with zeros.
5. Submit.

Verification passes because step 3 reproduces step (3) performed by the
verifier.  **No private key is required** for any participant.  Universal
forgery; zero cost; undetectable.

This is the single most important bug in the scheme.

---

### CR-D2 — Anonymity break: signer de-anonymised by ring-scan verify in `chipmunk_ring_sign`

**Files:** `module/crypto/src/sig/chipmunk/chipmunk_ring.c:765-793`

```765:787:module/crypto/src/sig/chipmunk/chipmunk_ring.c
uint32_t l_real_signer_index = UINT32_MAX;
for (uint32_t l_i = 0; l_i < a_ring->size; l_i++) {
    uint8_t l_test_signature[CHIPMUNK_SIGNATURE_SIZE];
    memset(l_test_signature, 0, sizeof(l_test_signature));
    if (chipmunk_sign(a_private_key->data, a_signature->challenge, a_signature->challenge_size,
                     l_test_signature) == CHIPMUNK_ERROR_SUCCESS) {
        if (chipmunk_verify(a_ring->public_keys[l_i].data, a_signature->challenge,
                           a_signature->challenge_size, l_test_signature) == CHIPMUNK_ERROR_SUCCESS) {
            l_real_signer_index = l_i;
            ...
            break;
        }
    }
}
```

The signer is located by **producing a real Chipmunk signature with the
private key and then trial-verifying it against each ring public key until
one matches**.  Three independent problems:

1. **Timing side-channel on local machine.**  The loop breaks at `l_i =
   real_signer_index`, so execution time is monotone in the signer’s
   position.  A co-resident attacker (shared CPU / container / trace
   sampling) trivially learns the position.
2. **Resulting `a_signature->signature` is a plain Chipmunk signature over
   the challenge.**  Because `chipmunk_sign` is deterministic (see CR-D3),
   two ring signatures produced by the same signer over different messages
   but the same challenge structure yield **byte-identical** Chipmunk
   signature components; repeated signing of any message lets an observer
   cluster signatures by signer by equality comparison.
3. **Anonymity does not rely on the ZK proof at all.**  Even if the
   primary forgery (CR-D1) is fixed, the anonymity argument in the paper
   requires the Chipmunk signature itself to be a zero-knowledge proof of
   ring membership — but this code stores the raw signer signature, making
   the anonymity claim vacuous.

**Net effect.**  The scheme is *not* a ring signature; it is an
identifiable signature with a ring attached as metadata.

---

### CR-D3 — One-Time Signature reuse: signing two messages recovers the private key

**Files:** `module/crypto/src/sig/chipmunk/chipmunk.c:357-401`,
`module/crypto/src/sig/chipmunk/chipmunk_hots.c:267-321`

`chipmunk_sign` derives the HOTS secret key deterministically from the
user’s seed **with a fixed counter of zero for every call**:

```357:401:module/crypto/src/sig/chipmunk/chipmunk.c
chipmunk_hots_sk_t l_hots_sk;
// ... (counter hard-coded to 0 below)
ret = chipmunk_hots_keygen(&l_hots_params, &l_derived_seed,
                           sizeof(l_derived_seed), 0 /* a_counter */,
                           &l_hots_sk, NULL);
```

Because the counter is constant, `(s0, s1) = HOTS_sk` are the **same two
polynomials for every signature** produced with a given DAP-SDK key.  HOTS
signing in `chipmunk_hots_sign` computes (morally):

```
σ(m) = s0 · H(m) + s1            (over R_q)
```

(No nonce, no rerandomisation; see `chipmunk_hots.c:267-321`.)

**Exploit.**  Given two different messages `m1 ≠ m2` with corresponding
signatures `σ1, σ2`:

```
σ1 − σ2 = s0 · (H(m1) − H(m2))
```

`H(m1) − H(m2)` is a known invertible polynomial in `R_q` with
overwhelming probability; multiplying by its inverse recovers `s0`, after
which `s1 = σ1 − s0·H(m1)`.  **Full private key recovered from two
signatures.**

This is the textbook HOTS attack.  HOTS exists *precisely* as a one-time
primitive that must never be reused; here the API statically reuses it.

---

### CR-D4 — Entropy collapse in `chipmunk_keypair`: keys derived from seconds + counter

**Files:** `module/crypto/src/sig/chipmunk/chipmunk.c:151-170`,
`module/core/include/dap_time.h:80`

```151:170:module/crypto/src/sig/chipmunk/chipmunk.c
static uint32_t s_key_counter = 0;
...
dap_time_t l_time = dap_time_now();                 /* seconds resolution */
l_seed[0] = (uint8_t)(l_time & 0xFF);
l_seed[1] = (uint8_t)((l_time >> 8) & 0xFF);
...
l_seed[15] = (uint8_t)((++s_key_counter) & 0xFF);
```

`dap_time_now()` is `time(NULL)` — 1-second resolution (see
`module/core/include/dap_time.h:80`).  Effective entropy of the seed used
to derive *every* Chipmunk keypair:

* ≤ 32 bits from `time_t` (guessable within a known generation window to
  <2^10 candidates for a 15-minute service window),
* ≤ 32 bits from a process-local counter (often 0 or small integer on
  first use),
* remaining bytes are zero-initialised stack memory (see the function
  body).

**Total guessable key space: ≈ 2^20–2^30** for realistic adversary
knowledge of when the key was created.  Brute-forcing any targeted key is
trivial.

This applies to *all* keys generated via `chipmunk_keypair()` — including
those created by the DAP-SDK wrapper `dap_enc_chipmunk_key_new()` and, by
fallback, `dap_enc_chipmunk_key_generate()` when the caller does not
supply a seed (`dap_enc_chipmunk.c:55, 82`).

The correct RNG already exists in the tree (`dap_random_bytes()` backed by
`/dev/urandom`).  Chipmunk refuses to use it.

---

### CR-D5 — 32-bit LCG used to sample the challenge polynomial

**Files:** `module/crypto/src/sig/chipmunk/chipmunk_poly.c:544-580`

```544:580:module/crypto/src/sig/chipmunk/chipmunk_poly.c
/* chipmunk_poly_from_hash — diverges from reference Rust impl */
uint32_t state = (uint32_t)hash[0] | ((uint32_t)hash[1] << 8)
               | ((uint32_t)hash[2] << 16) | ((uint32_t)hash[3] << 24);
for (size_t i = 0; i < CHIPMUNK_N; i++) {
    state = state * 1103515245u + 12345u;             /* glibc LCG */
    poly->coeffs[i] = (int32_t)(state % CHIPMUNK_Q);
}
```

The polynomial is intended to represent the hash of the transcript in
`R_q`.  Instead:

* Only **32 bits** of the SHA3-256 output are consumed.
* The expansion uses **`rand()`-grade LCG** (Numerical Recipes constants).
* The mapping is bijective: `state` uniquely determines all coefficients,
  so the effective entropy of the resulting polynomial is `≤ 2^32`.

**Impact.**  Any cryptographic argument that assumes the challenge
polynomial is drawn from a large / uniform / collision-resistant set
**does not hold**.  In particular:

* Forging proofs that match a given challenge requires enumerating at most
  2^32 challenges — feasible on a laptop.
* Collision-based attacks on the Fiat-Shamir transform become practical.

The code itself acknowledges divergence from the reference implementation
in a comment.

---

### CR-D6 — "Forgiveness" in `chipmunk_verify_multi_signature` — forgery via close coefficients

**Files:** `module/crypto/src/sig/chipmunk/chipmunk_aggregation.c:605-612`

The batch/aggregate verifier allows coefficients to deviate from the
expected value by up to `CHIPMUNK_PHI` (≈ 10% of `Q`):

```605:612:module/crypto/src/sig/chipmunk/chipmunk_aggregation.c
if (abs(diff) <= CHIPMUNK_PHI) {
    matches++;
}
/* accept if matches / CHIPMUNK_N >= 0.9 */
```

Any hard equality check over `R_q` that is softened into a bounded-distance
check trivially admits forgeries: set 10% of coefficients to arbitrary
values of your choice.  Combined with CR-D7 (wrong multiplication) the
verifier reduces to a near-tautology — a constant-output check.

---

## 3. High-severity findings (HIGH)

### CR-D7 — NTT-domain inconsistency: Hadamard product used as polynomial multiplication

**Files:** `chipmunk_aggregation.c:589-591`, `chipmunk_poly.c:611-629`

`chipmunk_verify_multi_signature` computes
`temp = (coeff1 * coeff2) % Q` coefficient-wise and compares against an
aggregate.  This is a Hadamard product, only meaningful when both inputs
are already in NTT domain and you want point-wise multiplication; but the
inputs here are mixed (some are time-domain) and no inverse NTT is
applied.  Verification is mathematically ill-defined.

`chipmunk_poly_add_ntt` (CR-D16) normalises into `[-q/2, q/2)` — a
centred representation typical of the time domain.  Mixing centred and
uncentred representations between calls means equality checks compare
physically different numbers and yet sometimes succeed by accident.

### CR-D8 — Linkability tags provide neither linkability nor unlinkability

**Files:** `chipmunk_ring.c:829-872` (signature-level), `chipmunk_ring.c`
(participant-level tag = `H(pk_i)`)

* **Signature-level tag** = `H(ring_hash ‖ message ‖ challenge)`.  Because
  it depends on the message, two signatures over different messages by the
  same signer have different tags → **not linkable**.
* **Participant-level tag** = `H(pk_i)`.  Identical for every signature
  that uses ring position `i`, across all signers — revealing the ring
  position, not the signer identity → **not unlinkable**, breaks CR-D2
  irrespective of CR-D2.

Ring/linkable-ring security literature requires one of:

* a linkability tag of the form `H(pk_signer, tag_ctx)` where `tag_ctx` is
  a session-wide constant (so repeated use by the same signer collides,
  but distinct signers never do), or
* a zero-knowledge proof of "same secret" equality.

Neither is implemented.

### CR-D9 — Serialisation malleability in `chipmunk_ring_serialize_schema.c`

**Files:** `module/crypto/src/sig/chipmunk/chipmunk_ring_serialize_schema.c:33,507`

1. `use_embedded_keys` is serialised as a `uint8_t` with only bit-0
   semantically meaningful.  The other 7 bits are unconstrained on
   deserialisation → two wire representations of the same logical
   signature.  Canonicity and signature-hash reproducibility break.
2. `zk_proofs_size` is typed `uint64_t` in-memory but the schema entry
   uses `sizeof(size_t)` for the on-wire width (`:507`).  32-bit and
   64-bit hosts disagree on the wire format → interoperability break and
   another source of signature malleability.

### CR-D10 — "SHAKE128" is a custom counter-mode over SHA2-256

**Files:** `module/crypto/src/sig/chipmunk/chipmunk_hash.c:109-183`

```109:183:module/crypto/src/sig/chipmunk/chipmunk_hash.c
int dap_chipmunk_hash_shake128(/*...*/)
{
    /* name suggests SHAKE128; implementation: SHA2-256 over (input ‖ counter) */
    for (uint64_t i = 0; out_left > 0; ++i) {
        sha256(input || counter(i), block);
        memcpy(out + i*32, block, min(32, out_left));
    }
}
```

This is **not** SHAKE128.  It is a homebrew MGF that shares none of
SHAKE’s provable properties:

* No absorption of the output-length field → fixed-length prefix attacks.
* 32-byte internal state vs. SHAKE’s 168-byte capacity.
* Counter-mode construction without separation tag is distinguishable.

Any caller relying on "SHAKE128-like XOF" semantics silently gets a weaker
primitive.

### CR-D11 — Mixed hash primitives across the stack

**Files:** `chipmunk_hash.c:188, 201, 297`

`dap_chipmunk_hash_to_seed` and `dap_chipmunk_hash_challenge` call
SHA2-256; `dap_chipmunk_hash_to_point` calls SHA3-256; `shake128` above
is SHA2-256-based.  There is no rationale for the mixture.  Domain
separation between hash chains depends on algorithm consistency; mixing
families invalidates several protocol proofs.

### CR-D12 — Binary messages truncated by `strlen()`

**Files:** `chipmunk_aggregation.c:736`

```736:737:module/crypto/src/sig/chipmunk/chipmunk_aggregation.c
size_t l_msg_len = strlen((char*)message);
```

Treats an arbitrary binary message as a C string.  Silently truncates at
the first `0x00` byte.  Impact:

* Two distinct payloads with the same prefix before a NUL hash to the
  same value ⇒ trivial collisions on purpose.
* Any aggregated/batched protocol over binary data (blockchain TXs,
  certificates, protobufs) is effectively broken.

### CR-D13 — Sensitive seeds/keys not zeroised

**Files:**
* `chipmunk.c:350,409-410,425-426,431-433,748-752,911-913` (seeds on the
  stack never wiped before return; `secure_clean` uses `volatile void*`
  which is not reliably effective under modern optimisers).
* `dap_enc_chipmunk.c:223-237` (`dap_enc_chipmunk_write_private_key` is a
  raw `memcpy` of the private-key struct, no format definition, no
  post-copy zeroisation).

After a signing or key-import call, residual sensitive material remains
on the stack or in the calling buffers.  A subsequent crash dump, core
dump, vm-migration snapshot, or same-process memory scan leaks private
data.  Use `explicit_bzero` / `memset_s` / compiler memory barriers, and
centralise secret lifecycle.

### CR-D14 — Data-dependent execution time in `chipmunk_poly_challenge`

**Files:** `chipmunk_poly.c:427-463`

```427:428:module/crypto/src/sig/chipmunk/chipmunk_poly.c
for (int i = 0; i < 8; i++)
    extended_hash[i*32 .. (i+1)*32-1] = hash32 XOR (i+1);   /* home-brew extension */

for (int i = 0; i < CHIPMUNK_N; i++) {
    /* rejection loop: re-sample until coefficient passes a collision test */
}
```

* Entropy extension by XORing with a tiny counter does not add entropy.
* The rejection loop’s iteration count depends on the (secret-dependent)
  sampled values.  Signing time leaks bits of the challenge structure.

### CR-D15 — HOTS always issued from Merkle leaf 0 → tree is cosmetic

**Files:** `chipmunk.c:357-401` (`a_counter = 0` passed to
`chipmunk_hots_keygen`)

Chipmunk’s security argument depends on the Merkle tree of one-time
signatures: each signature uses a distinct leaf and its
authentication-path.  This code always uses leaf 0, so the Merkle tree
degenerates to a single reused HOTS.  Combined with CR-D3, it *is* the
single reused HOTS.

---

## 4. Medium-severity findings

### CR-D16 — Centred normalisation in NTT-domain addition

`chipmunk_poly_add_ntt` (lines 638-665) normalises results into
`[-q/2, q/2)`, which is the convention for the time/coefficient domain.
Surrounding code expects `[0, q)` in NTT domain.  Mix produces sporadic
sign flips that pass weak equality checks and fail others.

### CR-D17 / CR-D18 — Debug logging on by default

`s_debug_more = true` at file scope in `chipmunk_aggregation.c:18` and
`chipmunk_ring_serialize_schema.c:33`.  Production builds leak internal
state, pointer layouts, and signature fields via the log sink.

### CR-D19 — No duplicate / malformed public-key check in ring construction

`chipmunk_ring_container_create` (`chipmunk_ring.c:306-381`) accepts any
array of bytes as the ring.  A caller can submit:

* N copies of the same pk → "ring" of size 1,
* all-zero pk entries → forgeable individual members,
* arbitrary garbage → acceptance by a verifier that skips pk validation.

### CR-D20 — Raw-memcpy private-key (de)serialisation

`dap_enc_chipmunk_write_private_key` (`dap_enc_chipmunk.c:223-237`):

* Wire format is the in-memory struct including padding and pointers.
* No versioning, no length/magic prefix.
* No zeroisation of intermediate buffers after a dump.

Version mismatches lead to silent corruption; padding bytes leak heap
contents.

### CR-D21 — Challenge hashing by ad-hoc concatenation

`chipmunk_ring.c:~840-870` builds the Fiat-Shamir transcript manually by
concatenating: ring hash ‖ message ‖ per-participant commitment blobs.
This bypasses the schema-driven serialiser used for the other hashes and
is fragile w.r.t. ordering, length-prefixing, and endianness.

### CR-D22 — `dap_random_bytes` buffer is not thread-safe

`module/core/src/dap_rand.c:76-89` — `s_buf_pos` and `s_buf` are global
and updated without locking.  Two threads can return overlapping
randomness.  Low impact today because Chipmunk does not consume
`dap_random_bytes` (see CR-D4), but after CR-D4 is fixed this is the hot
path.

---

## 5. Low-severity findings

* **CR-D23** — `chipmunk_aggregation.c:111` exports
  `chipmunk_randomizers_generate_random` using `rand() % 3`.  Dead today,
  but its presence invites reuse.
* **CR-D24** — `chipmunk_ring_secret_sharing.c:122` extracts the ring
  public key with `memcpy(share->ring_public_key.data, a_ring_key->data,
  CHIPMUNK_PUBLIC_KEY_SIZE)`, but `chipmunk_private_key_t` stores the
  public key at offset 80, not 0.  This path is unreachable today, but
  must be deleted or rewritten; shipping dead cryptographic code is a
  policy violation.
* **CR-D25** — `secure_clean` relies on `volatile void*` — compilers
  (LTO, full-program optimisation) may still elide the write.  Replace
  with `explicit_bzero`, `memset_s` or `SecureZeroMemory`.

---

## 6. Coverage matrix vs. prior audits

| Property                | CR-0..5 concluded | CR-6 questioned | CR-7 verified |
|-------------------------|-------------------|-----------------|---------------|
| EUF-CMA (unforgeability)| "holds"           | "not proven"    | **broken** (CR-D1) |
| Anonymity               | "holds"           | "not proven"    | **broken** (CR-D2) |
| Linkability             | "holds"           | "ill-defined"   | **broken** (CR-D8) |
| Key secrecy             | n/a               | "RNG weak?"     | **broken** (CR-D3, CR-D4) |
| Fiat-Shamir soundness   | "holds"           | n/a             | **broken** (CR-D5) |
| Merkle-HOTS binding     | "holds"           | n/a             | **broken** (CR-D15)|
| Canonical serialisation | "holds"           | "partial"       | **broken** (CR-D9) |
| Constant-time signing   | n/a               | "unchecked"     | **broken** (CR-D14)|
| Secret zeroisation      | n/a               | "partial"       | **broken** (CR-D13)|

Every previously-closed property is reopened by Round-3.

---

## 7. Directive — minimum required actions

This audit supersedes all prior "ready-for-production" statements.  Until
the below are closed, the Chipmunk Ring key type must remain gated behind
a build-time flag defaulting to off.

**CR-D1 and CR-D2 are blocking.**  All deployments must be paused until
both the signature is actually verified and the signer-identification
branch is removed.  The other catastrophic findings (CR-D3..CR-D6) are
also blocking; none can be shipped around with configuration changes.

Remediation plan is tracked by SLC task `task_5e817549` — phases CR-A and
CR-B are now re-scoped to include every CR-D* above.  The base Chipmunk
signature (CR-A) must be fixed first because CR-D3, CR-D4, CR-D5 and
CR-D15 are in the base scheme and the ring construction builds on top.

---

## 8. Re-audit tracking

Each finding must be:

1. Reproduced in a unit test before the fix.
2. Closed by a named commit referencing `CR-D<N>`.
3. Re-verified by a test that fails before and passes after.
4. Cross-checked by an independent reviewer not involved in the fix.

No finding may be closed by documentation alone.

---

**Prepared:** Round-3 code deep-dive, CR-7.
**Next phase:** CR-8 — constant-time audit with `dudect` + `valgrind` and
fuzz campaign (`libFuzzer` + custom mutators) against verify/serialise.
