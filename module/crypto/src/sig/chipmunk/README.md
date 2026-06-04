# Chipmunk

Native post-quantum signature stack on the Chipmunk lattice substrate.

This directory holds two distinct, compatible algorithms that share the
same underlying primitives (SHA3/SHAKE hashing, Chipmunk polynomial
arithmetic):

| Algorithm                | Source files                              | Purpose                                                                |
|--------------------------|-------------------------------------------|------------------------------------------------------------------------|
| **Chipmunk (aggregated)**| `chipmunk*.c/.h`, `chipmunk_hypertree.c`  | Stateful many-time signatures with hypertree aggregation (CR-D10).     |
| **Chipmunk Ring**        | `chipmunk_lrs.{c,h}`, `chipmunk_ring_crng.c`, `dap_sign_chipmunk_ring.c` | PQ k-of-N linkable threshold ring; production wire **CRNG/v1** (CR-11.G). |

## DAP integration

| DAP slot                                | Backed by                  | Public API                          |
|-----------------------------------------|----------------------------|-------------------------------------|
| `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK`         | aggregated hypertree path  | `dap_enc_chipmunk.{h,c}`            |
| `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING`    | **CRNG/v1** (`chipmunk_ring_crng`) + Π_bw | `dap_enc_chipmunk_ring.{h,c}` + `dap_sign_create_ring` / `dap_sign_verify_ring` |

`dap_enc_chipmunk_ring` owns key-material (CLPK / CLSK byte-for-byte) and
registers the `SIG_TYPE_CHIPMUNK_RING` callbacks in the generic `dap_sign`
ring registry. Ring sign / verify need ring context and signer-subset context,
so they live outside the single-key `sign_get` / `sign_verify` callback
contract.

## Current status — CR-11.G production wire (CRNG/v1)

Production path: **R_CRNG** object (`CHIPMUNK_RING_MAGIC_CRNG`), single transcript,
Π_bw Hamming-weight proof (Phase 7.7, fold0 B-link + log-N weight chain),
single shared response `z_x` (the redundant `z_tr` mirror was retired in
Phase 7.2 cleanup; `z_x` already binds both A_pk membership and A_T tag).
**CLTP** slot scaffold is not accepted
on `chipmunk_ring_verify_from_bytes` (fail-close).

| CR-11.G Phase | Status | Artifact |
|---|---|---|
| 7.4 hardness | ✅ | `chipmunk_ring_hardness.c` |
| 7.5 / 7.12 security | ✅ | `test_chipmunk_ring_security.c` |
| 7.7 Π_bw | ✅ locked | `chipmunk_ring_bitweight.c`, `CHIPMUNK_RING_BITWEIGHT_PRODUCTION_LOCKED` |
| 7.10 CRNG wire | ✅ | `chipmunk_ring_crng.c`, `dap_sign_chipmunk_ring.c` |
| 7.11 KAT | ✅ | `test_chipmunk_ring_crng_kat.c` |
| 7.13 bench | ✅ | `bench_chipmunk_ring_crng.c` |
| 7.14 signoff | ✅ | `chipmunk_ring_production_signoff_selfcheck()` |

Release gate:

```bash
ctest -R 'chipmunk_ring|chipmunk_lrs' --output-on-failure
./build/tests/bin/test_unit_crypto_chipmunk_ring_signoff   # includes 2-of-4 CRNG
./build/tests/bin/test_unit_crypto_chipmunk_ring_threshold_api  # dap_sign 1-of-4 / 2-of-4
```

## Properties of Chipmunk Ring — CRNG/v1 (CR-11.G)

* **Quantum-resistant:** lattice-native (MLWE/MSIS), no classical-only assumptions.
* **Threshold:** proves exactly `t` distinct ring members signed; `t=1` is the 1-of-N special case.
* **Subset-hiding (SUB-IND):** verifier cannot distinguish which `t` of `N` members signed.
* **Linkable (per-scope):** within the same `(ring, ctx)` scope, reused signer tags collide and are detectable via `Link`.
* **Non-interactive:** no DKG or inter-signer rounds; each signer produces a local share, a combiner assembles the final signature.
* **Single public signature type:** all ring modes use `SIG_TYPE_CHIPMUNK_RING` / `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING`.

## Usage sketch

```c
#include "dap_enc_key.h"
#include "dap_sign.h"

// Each ring member generates its own key.
dap_enc_key_t *signer = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);

// Ring contains the signer pubkeys plus decoys.
dap_enc_key_t *ring[8] = { signer, decoy_1, /* ... */ };
dap_enc_key_t *signers[1] = { signer };

dap_sign_t *sig = dap_sign_create_ring(signers, 1, 1,
                                       msg, msg_size,
                                       ring, 8);

int rc = dap_sign_verify_ring(sig, msg, msg_size, ring, 8);
```

## Source map (lattice / primitive layer)

| File                                       | Role                                              |
|--------------------------------------------|---------------------------------------------------|
| `chipmunk.{c,h}`                           | Top-level Chipmunk parameter / context glue       |
| `chipmunk_poly.{c,h}`                      | Polynomial arithmetic over `Z_q[X]/(X^N+1)`       |
| `chipmunk_hash.{c,h}`                      | SHA3-256 / SHAKE128 / domain-hash KDF             |
| `chipmunk_hypertree.{c,h}`                 | Stateful many-time signature (aggregated)         |
| `chipmunk_multi_signature_codec.{c,h}`     | Aggregated-signature wire codec (CR-D10)          |
| `chipmunk_lrs.{c,h}`                       | Native linkable ring proof primitive (CLRS, 1-of-N) |
| `chipmunk_ring_crng.c`                     | Production CRNG/v1 sign/verify                     |
| `chipmunk_ring_bitweight.c`                | Π_bw Hamming-weight proof (Phase 7.7)              |
| `dap_sign_chipmunk_ring.c`                 | DAP ring bridge over CRNG wire                     |

See SLC `task_1a411fa5` (CR-11.E) for the security analysis, phase plan, and
design documents.
