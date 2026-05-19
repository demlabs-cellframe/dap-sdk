# Chipmunk

Native post-quantum signature stack on the Chipmunk lattice substrate.

This directory holds two distinct, compatible algorithms that share the
same underlying primitives (SHA3/SHAKE hashing, Chipmunk polynomial
arithmetic):

| Algorithm                | Source files                              | Purpose                                                                |
|--------------------------|-------------------------------------------|------------------------------------------------------------------------|
| **Chipmunk (aggregated)**| `chipmunk*.c/.h`, `chipmunk_hypertree.c`  | Stateful many-time signatures with hypertree aggregation (CR-D10).     |
| **Chipmunk LRS**         | `chipmunk_lrs.{c,h}`                      | Anonymous CLSAG-style **linkable ring signature** (CR-11.D).            |

## DAP integration

| DAP slot                                | Backed by                  | Public API                          |
|-----------------------------------------|----------------------------|-------------------------------------|
| `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK`         | aggregated hypertree path  | `dap_enc_chipmunk.{h,c}`            |
| `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING`    | `chipmunk_lrs`             | `dap_enc_chipmunk_ring.{h,c}` (key lifecycle) + `dap_sign_create_ring` / `dap_sign_verify_ring` (sign/verify) |

`dap_enc_chipmunk_ring` only owns key-material (CLPK / CLSK byte-for-byte).
Ring sign / verify need an extra ring argument and live outside the
single-key `sign_get` / `sign_verify` callback contract.

## Properties of Chipmunk LRS (CR-11.D)

* **Quantum-resistant:** lattice-native, no classical-only assumptions.
* **Anonymous:** verifier learns only that *some* ring member produced
  the signature, never the signer index.
* **Linkable:** two signatures by the same signer share an identical
  key image, enabling double-spend detection without breaking anonymity.
* **Single canonical wire family:** magic `CLPK` / `CLSK` / `CLRP` /
  `CLRS`, parameter profile `LSC0`.  No version negotiation; no legacy
  compatibility layer.
* **Ring size:** `[CHIPMUNK_LRS_RING_MIN, CHIPMUNK_LRS_RING_MAX]`
  (`[2, 64]`).

## Usage sketch

```c
#include "dap_enc_key.h"
#include "dap_sign.h"

// Each ring member generates its own key.
dap_enc_key_t *signer = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);

// Ring contains the signer's pubkey plus decoys.
dap_enc_key_t *ring[8] = { signer, decoy_1, /* ... */ };

dap_sign_t *sig = dap_sign_create_ring(signer, msg, msg_size,
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
| `chipmunk_lrs.{c,h}`                       | Linkable ring signature implementation (CR-11.D)  |

See SLC tasks for the security analysis and KAT vectors.
