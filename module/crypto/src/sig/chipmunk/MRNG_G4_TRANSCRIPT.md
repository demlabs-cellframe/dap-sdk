# MRNG G4 — Byte-exact Fiat-Shamir transcript

**Status:** BINDING for `chipmunk_mring_transcript.{h,c}`.  
**Gate:** G4 (joint bind ∘ fold FS glue).

---

## 1. Hash prefix format

All SHA3-256 digests use the LRS length-prefixed envelope:

```
LE32(domain_len) ‖ domain ‖ LE32(payload_len) ‖ payload
```

(`chipmunk_lrs.c::s_hash_len_prefixed` pattern.)

---

## 2. Fixed header hashes (128 B on wire)

| field       | domain                     | payload                                      |
|-------------|----------------------------|----------------------------------------------|
| `ring_hash` | `"chipmunk-mring-ring-v1"` | `LE32(MRV1) ‖ LE32(N) ‖ qpack(P_0) ‖ …` sorted lex by `P` bytes |
| `ctx_hash`  | `"chipmunk-mring-ctx-v1"`  | `LE32(MRV1) ‖ ctx_bytes`                     |
| `msg_hash`  | `"chipmunk-mring-msg-v1"`  | `LE32(MRV1) ‖ msg_bytes`                     |
| `fs_seed`   | `"chipmunk-mring-fs-v1"`   | `ring_hash ‖ ctx_hash ‖ msg_hash ‖ qpack(T) ‖ qpack(C_b)` |

Duplicate CLPK `P` qpacks → `-EEXIST` at canonicalise time (G2 v2 §A7).

---

## 3. Statement challenge `c`

```
c ← χ_ter^37( XOF("chipmunk-mring-c-v1", MRV1, fs_seed) )
```

Implementation: `chipmunk_lrs_h_to_sparse_ternary` with domain
`chipmunk-mring-c-v1` and seed material `fs_seed`.

---

## 4. Fold-round FS (G3.1 ext, counter-mode via round index)

```
round_fs ← SHAKE256("MRNG-M4-fold-round-fs-v1" ‖ fs_seed ‖ LE32(r)
                     ‖ coeffs(CL_r) ‖ coeffs(CR_r)) → 32 B
x_r ← sample_challenge(round_fs, ctr=0) ∈ R_q^{(e)}^+
```

Absorption uses raw `int32_t coeffs[512]` per R_q limb (6 limbs per ext
element), matching M4 prove/verify.

---

## 5. Bind-block FS and `c*`

After fold prove (prover knows mask polys `M_pk = A_pk·ρ_x`, `M_T = A_T·ρ_x`):

```
bind_fs ← SHA3-256("chipmunk-mring-bind-fs-v1" ‖
                   fs_seed ‖ qpack(c) ‖ qpack(M_pk) ‖ qpack(M_T) ‖
                   ∀r: qpack_ext(C_L,r) ‖ qpack_ext(C_R,r) ‖
                   qpack_ext(a*) ‖ qpack_ext(b*))

c* ← χ_ter^37( XOF("chipmunk-mring-bind-fs-v1", MRV1, bind_fs) )
z_x ← ρ_x + c*·X   (reject if ‖z_x‖∞ > β_z)
```

Verifier recomputes `bind_fs` from the wire (same byte layout), derives
`c*`, reconstructs `M_pk' = A_pk·z_x − c*·Y_pk` and checks
`M_pk' = A_pk·ρ_x` implicitly via the Schnorr equalities in
`chipmunk_mring_bind_verify_reconstruct`.

---

## 6. QROM accounting (honest ledger)

Per `MRNG_G3_FOLD_SIMULATOR.md` §4.3: post-FS floor ≥ 2^{−119} with
`FS_OUT_BITS = 384` and LWW-24-style multi-round loss.  Full numeric
closure remains documentation-only until M6 sign/verify integration.

---

## 7. API (`chipmunk_mring_transcript.h`)

* `chipmunk_mring_canonicalise_ring`
* `chipmunk_mring_hash_{ring,ctx,msg}`
* `chipmunk_mring_fs_seed`
* `chipmunk_mring_transcript_sample_c`
* `chipmunk_mring_transcript_fold_round_fs`
* `chipmunk_mring_transcript_bind_fs`
* `chipmunk_mring_transcript_sample_c_star`

Tests: `test_chipmunk_mring_transcript.c`.
