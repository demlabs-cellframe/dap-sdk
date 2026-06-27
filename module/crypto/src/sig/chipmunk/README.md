# Chipmunk

Native post-quantum signature stack on the Chipmunk lattice substrate.

This directory holds two distinct, compatible algorithms that share the
same underlying primitives (SHA3/SHAKE hashing, Chipmunk polynomial
arithmetic):

| Algorithm                | Source files                              | Purpose                                                                |
|--------------------------|-------------------------------------------|------------------------------------------------------------------------|
| **Chipmunk (aggregated)**| `chipmunk*.c/.h`, `chipmunk_hypertree.c`  | Stateful many-time signatures with hypertree aggregation (CR-D10).     |
| **Chipmunk Ring**        | `chipmunk_lrs.{c,h}`, `chipmunk_mring*.c`, `dap_sign_chipmunk_ring.c` | PQ k-of-N linkable threshold ring; production wire **MRNG/v1** (CR-11.G). |

## DAP integration

| DAP slot                                | Backed by                  | Public API                          |
|-----------------------------------------|----------------------------|-------------------------------------|
| `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK`         | aggregated hypertree path  | `dap_enc_chipmunk.{h,c}`            |
| `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING`    | **MRNG/v1** (`chipmunk_mring`) + fold/bind | `dap_enc_chipmunk_ring.{h,c}` + `dap_sign_create_ring` / `dap_sign_verify_ring` |

`dap_enc_chipmunk_ring` owns key-material (CLPK / CLSK byte-for-byte) and
registers the `SIG_TYPE_CHIPMUNK_RING` callbacks in the generic `dap_sign`
ring registry. Ring sign / verify need ring context and signer-subset context,
so they live outside the single-key `sign_get` / `sign_verify` callback
contract.

## Current status — CR-11.G production wire (MRNG/v1)

Production path: **MRNG/v1** object (`CHIPMUNK_MRING_MAGIC`), log-N halving fold over R_q^{(e)},
bind-block (z_x + c*), Fiat-Shamir transcript.

| Milestone | Status | Artifact |
|---|---|---|
| M0 | ✅ | stub sign/verify, public bridge header |
| M1 | ✅ | 28-byte header parser/serialiser |
| M3.1 | ✅ | VCom layer |
| M3.2 | ✅ | unified inner-product statement |
| M3.3 | ✅ | bind-block helpers |
| M4 | ✅ | halving fold over R_q^{(e)} |
| M4.1–4.3 | ✅ | wire pack/unpack, seed-compressed openings, leaf-mask ω |
| M6 | ✅ | end-to-end sign/verify wire glue |
| M7.1 | ✅ | KAT (Known-Answer Tests) |
| M7.2 | ✅ | Security/adversarial tests |
| M7.3 | ✅ | Benchmarks |
| M7.4 | ✅ | Production signoff selfcheck |
| M7.5 | ✅ | CT audit of fold arithmetic |

Release gate:

```bash
ctest -R 'chipmunk_mring' --output-on-failure
./build.debug/tests/bin/test_unit_crypto_chipmunk_mring_signoff
```

## Properties of Chipmunk Ring — MRNG/v1 (CR-11.G)

* **Quantum-resistant:** lattice-native (MLWE/MSIS), no classical-only assumptions.
* **Log-N compressed:** halving fold over degree-6 ring extension; signature size ~67–118 KB for N=2..16.
* **Threshold:** proves exactly `t` distinct ring members signed; `t=1` is the 1-of-N special case.
* **Subset-hiding (SUB-IND):** verifier cannot distinguish which `t` of `N` members signed.
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
| `chipmunk_mring.{c,h}`                     | MRNG/v1 sign/verify + header (de)serialisation    |
| `chipmunk_mring_params.h`                  | MRV1 parameter profile                            |
| `chipmunk_mring_statement.{c,h}`           | Statement layer (aggregate_X, bind helpers)       |
| `chipmunk_mring_fold.{c,h}`                | Halving fold prove/verify over R_q^{(e)}          |
| `chipmunk_mring_transcript.{c,h}`          | Fiat-Shamir transcript                            |
| `chipmunk_mring_ext.{c,h}`                 | Ring extension R_q^{(e)} arithmetic               |
| `chipmunk_mring_hardness.{c,h}`            | MSIS/MLWE hardness estimator                      |
| `dap_sign_chipmunk_ring.c`                 | DAP ring bridge over MRNG wire                    |

See SLC `task_1a411fa5` (CR-11.E) for the security analysis, phase plan, and
design documents.
