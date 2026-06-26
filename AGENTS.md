# AGENTS.md — SLK Context for dap-sdk

## Current Task

**Branch:** `feature/chipmunk-ring`
**Task:** `task_ac273cea` — Chipmunk Ring Signatures + Post-Quantum Anonymous Consensus
**Phase:** Phase 11 — Security Hardening

## DAP SDK Code Conventions

### Memory
- `DAP_NEW_Z(type)` / `DAP_NEW_Z_SIZE(type, size)` / `DAP_NEW_Z_COUNT(type, count)`
- `DAP_DELETE(ptr)` — NEVER use `free()` directly
- `dap_memwipe(buf, size)` — wipe secret material before DAP_DELETE

### Naming
- `s_` static, `l_` local, `a_` parameter, `g_` global, `k_` constant
- snake_case everywhere

### Error Handling
- Return errno codes: `-EINVAL`, `-ENOMEM`, `-ENOENT`, `-EAGAIN`
- `log_it(L_ERROR, ...)` for errors, `debug_if(0, L_DEBUG, ...)` for debug (use 0 to suppress in prod)

### Headers
- `#pragma once` + `#ifndef` double guard
- `#define LOG_TAG "module"` before `#include "dap_common.h"`

### Security
- NEVER use `malloc`/`calloc`/`free` — use DAP_NEW_Z/DAP_DELETE
- Wipe secret buffers with `dap_memwipe` before freeing
- Safe modular reduction: use `s_mod_q()` helper, never raw `%` on signed values
- All crypto PRNG must use SHAKE256 XOF, never LCG
- All polynomial from-hash calls must check return values

### Crypto Constants
- `CHIPMUNK_N = 512`, `CHIPMUNK_Q = 3168257`, `CHIPMUNK_LRS_K = 6`
- `CHIPMUNK_SNARK_Q = 3168257`, `CHIPMUNK_SNARK_EXT_DEG = 6`
- Ring extension: `R_q^(e) = R_q[Y]/(Φ_9(Y))`, |S| = q^6 - 1 ≈ 2^{129.6}

## Phase Status

| Phase | Status |
|-------|--------|
| 7: MRNG M0-M7 | DONE |
| 8: Audit + API split + CMake cleanup | DONE |
| 9: LoTRS (keygen, sign, verify, wire, KAT, threshold) | DONE |
| 10: Chipmunk Ring | DONE |
| 11: Security Hardening (audit + fixes) | DONE |

## Phase 11 — Security Hardening

Two full security audit iterations completed. All CRITICAL and HIGH issues resolved.

### Crypto Modules (in `dap-sdk/module/crypto/src/sig/chipmunk/`)

| Module | Files | Description |
|--------|-------|-------------|
| SNARK | `chipmunk_snark.c/h` | Ligero-style lattice SNARK with R_q^(e) extension |
| Range Proof | `chipmunk_range_proof.c/h` | Stern-like range proof with ZK blinding |
| Pedersen | `chipmunk_pedersen.c/h` | Lattice Pedersen commitments |
| Mixnet | `chipmunk_mixnet.c/h` | DC-net + batch shuffle |
| Ring | `chipmunk_ring.c/h` | Non-interactive lattice ring signature |
| HOTS | `chipmunk_hots.c/h` | Hash-based one-time signature |
| Aggregation | `chipmunk_aggregation.c/h` | Multi-signature aggregation |
| NTT | `chipmunk_ntt.c/h` | Number-theoretic transform |

### Ledger Integration (in `cellframe-sdk/modules/`)

| Module | Files | Description |
|--------|-------|-------------|
| Ledger Type | `ledger/dap_chain_ledger_type.c/h` | Open/anon ledger dispatch |
| TX Anon Create | `ledger/dap_chain_tx_anon_create.c/h` | Algorithm-agnostic anonymous TX |
| Mixnet Consensus | `ledger/dap_chain_mixnet_consensus.c/h` | Mixnet batching for consensus |
| Chipchain | `consensus/chipchain/dap_chain_cs_chipchain.c` | ESBOCS copy with stream channel 'C' |

### Security Fixes Applied

**SNARK (4 CRITICAL + 4 HIGH):**
- FRI folding uses full extension element (was scalar-only, 21-bit soundness)
- FRI final layer checks all 6 extension components
- Opening proof verifies polynomial relation z(X) = q(X)·(X-α) at random point
- Opening proof reconstructs b, z, q polynomials and verifies commitments
- Alpha stored in proof struct (const-correctness)
- z(alpha) validation before quotient computation
- Safe modular reduction via `s_mod_q()` (fixes negative % UB)

**Range Proof (4 CRITICAL + 4 HIGH):**
- Full rewrite: transcript bound to commitment C, range bits, A, B
- ZK blinding: responses masked with random polynomials
- Verifier unblinds responses and checks binary constraint
- Safe modular arithmetic for weighted addition

**Mixnet (1 CRITICAL + 1 HIGH):**
- DC-net: deterministic shared pairwise pads via SHAKE256 (was random, no cancellation)
- round_id field for pad derivation
- Memory leak fix on repeated generate_shares calls

**HOTS (1 CRITICAL + 2 HIGH):**
- Matrix A: SHAKE256 XOF replaces LCG PRNG
- chipmunk_poly_from_hash return values checked

**Ring (2 HIGH):**
- Blind mask: derive from retry seed, not static seed
- Key image: pack all k polynomials, not just first

**Ledger (2 HIGH):**
- Double-spend TOCTOU: record key images before TX add
- uint256_t truncation: check amount fits in int64_t

**Aggregation (2 HIGH):**
- Randomizer underflow: check SHAKE output sufficiency
- Double verification: cache results from first pass

### Build Status
- 0 warnings, 0 errors
- 6/6 test suites pass (SNARK, Pedersen, Range Proof, Mixnet, Ledger Anon, E2E)

### SHAKE256 API Warning
- `dap_hash_shake256_absorb` is NOT incremental — it zeroes state on every call (keccak_ref.c:311)
- Always concatenate inputs into a single buffer before calling absorb
- Never call absorb twice on the same state

## Ring Signature Types

| Type | ID | Description |
|------|-----|-------------|
| SIG_TYPE_CHIPMUNK_MRING | 0x0108 | Log-N threshold ring |
| SIG_TYPE_CHIPMUNK_LRS | 0x010A | 1-of-N linkable ring |
| SIG_TYPE_LOTRS | 0x010B | Lattice threshold ring (interactive) |
| SIG_TYPE_CHIPMUNK_RING | 0x010C | Non-interactive lattice ring |

## Key Source Files

| File | Purpose |
|------|---------|
| `chipmunk_snark.c` | Ligero-style lattice SNARK |
| `chipmunk_range_proof.c` | Stern-like range proof |
| `chipmunk_pedersen.c` | Lattice Pedersen commitments |
| `chipmunk_mixnet.c` | DC-net + batch shuffle |
| `chipmunk_ring.c` | Non-interactive lattice ring |
| `chipmunk_hots.c` | Hash-based one-time signature |
| `chipmunk_aggregation.c` | Multi-signature aggregation |
| `chipmunk_ntt.c` | Number-theoretic transform |
| `dap_chain_ledger_type.c` | Ledger type dispatch |
| `dap_chain_tx_anon_create.c` | Anonymous TX creation |
| `dap_chain_cs_chipchain.c` | Chipchain consensus |

## Known Issues

- None remaining (all CRITICAL + HIGH resolved across 5 audit iterations)

## Audit History

| Iteration | Focus | CRITICAL | HIGH | MEDIUM | LOW |
|-----------|-------|----------|------|--------|-----|
| 1st | Initial crypto audit | 7 | 11 | - | - |
| 2nd | Fix verification | 4 | 6 | - | - |
| 3rd | SHAKE256 + deep audit | 1 | 5 | - | - |
| 4th | SNARK soundness + Range Proof ZK | 3 | 3 | - | - |
| 5th | Protocol correctness | 5 | 6 | 4 | 8 |
| 6th | Message binding + TOCTOU + sign ext | 3 | 2 | 2 | - |
| 7th | All remaining MEDIUM+LOW cleanup | 0 | 0 | 4 | 6 |
| 8th | Final verification — 3 LOW fixed | 0 | 0 | 0 | 3 |
| 9th | Key image adapter + dead code cleanup | 0 | 1 | 7 | - |
| 10th | Dead include removal — completely clean | 0 | 0 | 0 | 3 |
| 11th | Absolute final verification — 0 issues | 0 | 0 | 0 | 0 | (all CRITICAL + HIGH resolved)
