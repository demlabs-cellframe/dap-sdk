# AGENTS.md — SLK Context for dap-sdk

## Current Task

**Branch:** `feature/chipmunk-ring`
**Task:** `task_ac273cea` — Chipmunk Ring Signatures (MRNG + LRS + LoTRS)
**Phase:** CR-11.G Phase 7.7 → Phase 8

## Milestone Status

| Milestone | Status |
|-----------|--------|
| M0–M4 | DONE — MRNG cryptographic core |
| M6 | DONE — end-to-end sign/verify wire glue |
| M7.1 | DONE — KAT (Known-Answer Tests) |
| M7.2 | DONE — Security/adversarial tests |
| M7.3 | DONE — Benchmarks (N=2,4,8,16) + competitors (LoTRS, RingTAIL, Raptor) |
| M7.4 | DONE — Production signoff selfcheck |
| M7.5 | DONE — CT audit of fold arithmetic |
| M8 | IN PROGRESS — anonymity audit, API split, expanded tests |
| M9 | PLANNED — LoTRS implementation in C |

## API Split (M8)

`SIG_TYPE_CHIPMUNK_RING` split into:
- `SIG_TYPE_CHIPMUNK_MRING` (0x0108) — log-N threshold ring (MRNG)
- `SIG_TYPE_CHIPMUNK_LRS` (0x010A) — 1-of-N linkable ring (LRS)
- `SIG_TYPE_CHIPMUNK_RING` — alias for MRING (backward compat)

## Anonymity Audit Findings (M8)

### LRS
- Wire format: no position leakage (all per-member blocks fixed-size)
- Key image: deterministic per-signer (intentional linkability)
- Timing: rejection sampling leaks global attempt count, not per-position
- Missing: N=2 anonymity test, wire-level byte comparison across signers

### MRNG
- Wire format: no subset leakage (all sections fixed-size for given N)
- T block: deterministic per-subset (linkability tag)
- Timing: bind-mask rejection leaks aggregated witness norm, not subset
- CT fix applied: `aggregate_X` uses constant-time mask
- Missing: N=2 T=1 anonymity test, b-indicator recovery test, fold proof distribution test

### Gaps to close
1. N=2 anonymity set tests (LRS + MRNG)
2. Wire-level indistinguishability tests (byte comparison across signers)
3. Timing analysis tests (rejection attempt count independence)
4. b-indicator recovery from wire test (MRNG)
5. Fold proof distribution test (MRNG)
6. N=64/256 anonymity smoke tests

## LoTRS Implementation Plan (M9)

### Overview
Implement LoTRS (Practical Post-Quantum Structured Threshold Ring Signatures from Lattices) in C, following the Rust reference implementation at `lotrs-sig/lotrs/lotrs-rs`.

### Architecture
- **DualMS**: Multi-signature component (T signers produce aggregated signature)
- **RS**: Binary ring proof (proves signer subset without revealing which)
- **Threshold**: Shamir secret sharing over R_q
- **Parameters**: Compatible with Chipmunk substrate (q=3168257, N=512)

### Key Differences from MRNG
| | MRNG | LoTRS |
|---|---|---|
| Rounds | 1 (non-interactive) | 2+ (interactive) |
| Signers | 1 | T cooperatively |
| Sig size N=4 t=2 | 84 KB | 877 B |
| Verify | 21 ms | 0.7 ms |
| Model | Ring (1 hides in N) | Threshold (T of N cooperate) |

### Implementation Steps
1. Port DualMS multi-signature (commitments, aggregated response)
2. Port RS binary ring proof (challenge sampling, response computation)
3. Port Shamir secret sharing over R_q
4. Wire format (header + DualMS + RS sections)
5. Integration with dap_sign API under `SIG_TYPE_CHIPMUNK_LRS`
6. KAT tests, security tests, benchmarks
7. Comparison with Rust reference (byte-for-byte interop)

### Source Reference
- Paper: IACR ePrint 2026/974
- Rust: `lotrs-sig/lotrs/lotrs-rs/`
- Python: `lotrs-sig/lotrs/lotrs-py/`

## Code Conventions

- Language: C11 (gnu11)
- Build: CMake 3.10+, debug build in `build.debug/`
- Test framework: `dap_test.h` — `dap_assert()`, `dap_pass_msg()`, `dap_fail()`
- KAT pattern: SHA3-256 pinning + `CHIPMUNK_*_KAT_DUMP` env var for regeneration
- Memory: `DAP_NEW_Z`/`DAP_DELETE` + `dap_memwipe` for secrets
- Logging: `log_it(L_INFO/L_ERROR, ...)`
- Error codes: `chipmunk_ring_error_t` enum, `chipmunk_ring_strerror()`
- Naming: `s_` prefix for static functions, `l_` for locals, `a_` for parameters, `k_` for constants
- Headers: `#pragma once` + `#ifndef` guard
- No comments unless asked
- Use propagation for include paths (dap_sdk exports all PUBLIC dirs)

## Test Commands

```bash
# Build all
cmake --build build.debug

# Run all MRNG tests
for t in test_unit_crypto_chipmunk_mring_sign test_unit_crypto_chipmunk_mring_kat test_unit_crypto_chipmunk_mring_security test_unit_crypto_chipmunk_mring_signoff test_unit_crypto_chipmunk_mring_fold; do ./build.debug/tests/bin/$t; done

# Regenerate KAT vectors
CHIPMUNK_MRING_KAT_DUMP=1 ./build.debug/tests/bin/test_unit_crypto_chipmunk_mring_kat

# Run competitors
cd tests/performance/crypto && ./run_competitors.sh
```

## Key Source Files

| File | Purpose |
|------|---------|
| `module/crypto/src/sig/chipmunk/chipmunk_mring.c` | MRNG sign/verify core |
| `module/crypto/src/sig/chipmunk/chipmunk_mring.h` | Wire layout, section offsets |
| `module/crypto/src/sig/chipmunk/chipmunk_ring.h` | Public bridge header |
| `module/crypto/src/sig/chipmunk/chipmunk_mring_params.h` | MRV1 parameter profile |
| `module/crypto/src/sig/chipmunk/chipmunk_mring_statement.c` | Statement layer |
| `module/crypto/src/sig/chipmunk/chipmunk_mring_fold.c` | Halving fold prove/verify |
| `module/crypto/src/sig/chipmunk/chipmunk_mring_transcript.c` | Fiat-Shamir transcript |
| `module/crypto/src/sig/chipmunk/chipmunk_lrs.c` | LRS 1-of-N ring signature |
| `module/crypto/src/dap_sign.c` | Signature dispatch |
| `module/crypto/src/sig/chipmunk/dap_sign_chipmunk_ring.c` | dap_sign bridge |

## Uncommitted Changes

None — all committed and pushed.

## Known Issues

- N=8 T=4 sign fails in benchmark (debug build only, passes in unit test)
- Raptor (Falcon-based) cannot be adapted to Chipmunk without full rewrite (different algebraic structure)
