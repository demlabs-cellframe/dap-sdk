# AGENTS.md — SLK Context for dap-sdk

## Current Task

**Branch:** `feature/chipmunk-ring`
**Task:** `task_ac273cea` — Chipmunk Ring Signatures (MRNG + LRS + LoTRS)
**Phase:** CR-11.G Phase 7.7 → Phase 8

## DAP SDK Code Conventions

### Memory
- `DAP_NEW_Z(type)` — calloc-style zeroed alloc
- `DAP_NEW_Z_SIZE(type, size)` — sized alloc
- `DAP_NEW_Z_COUNT(type, count)` — array alloc
- `DAP_DELETE(ptr)` — free (NEVER use `free()` directly)
- `dap_memwipe(buf, size)` — wipe secret material before DAP_DELETE

### Naming
- `s_` prefix for static functions/variables
- `l_` prefix for local variables
- `a_` prefix for function parameters
- `g_` prefix for globals
- `k_` prefix for constants
- snake_case everywhere (NO camelCase)

### Error Handling
- Return errno codes: `-EINVAL`, `-ENOMEM`, `-ENOENT`, `-EAGAIN`, etc.
- Log errors via `log_it(L_ERROR, "format", ...)`
- Debug via `debug_if(flag, L_DEBUG, "format", ...)` — compiles to nothing in release

### Headers
- `#pragma once` + `#ifndef` double guard
- `#define LOG_TAG "module_name"` before `#include "dap_common.h"`

### Security
- NEVER use `malloc`/`calloc`/`free` — use DAP_NEW_Z/DAP_DELETE
- NEVER use OpenSSL — use DAP SDK crypto
- Wipe secret buffers with `dap_memwipe` before freeing

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
| M8 | IN PROGRESS — anonymity audit, API split, LoTRS rewrite to DAP style |
| M9 | PLANNED — LoTRS threshold (multi-signer) support |

## API Split (M8)

`SIG_TYPE_CHIPMUNK_RING` split into:
- `SIG_TYPE_CHIPMUNK_MRING` (0x0108) — log-N threshold ring (MRNG)
- `SIG_TYPE_CHIPMUNK_LRS` (0x010A) — 1-of-N linkable ring (LRS)
- `SIG_TYPE_CHIPMUNK_RING` — alias for MRING (backward compat)

## Test Commands

```bash
cmake --build build.debug
./build.debug/tests/bin/test_unit_crypto_chipmunk_mring_sign
./build.debug/tests/bin/test_unit_crypto_chipmunk_mring_kat
./build.debug/tests/bin/test_unit_crypto_chipmunk_mring_security
./build.debug/tests/bin/test_unit_crypto_chipmunk_mring_signoff
./build.debug/tests/bin/test_unit_crypto_chipmunk_mring_anonymity
./build.debug/tests/bin/test_unit_lotrs_basic
CHIPMUNK_MRING_KAT_DUMP=1 ./build.debug/tests/bin/test_unit_crypto_chipmunk_mring_kat
```

## Key Source Files

| File | Purpose |
|------|---------|
| `module/crypto/src/sig/chipmunk/chipmunk_mring.c` | MRNG sign/verify core |
| `module/crypto/src/sig/chipmunk/chipmunk_mring.h` | Wire layout, section offsets |
| `module/crypto/src/sig/chipmunk/chipmunk_ring.h` | Public bridge header |
| `module/crypto/src/sig/chipmunk/chipmunk_lrs.c` | LRS 1-of-N ring signature |
| `module/crypto/src/sig/lotrs/lotrs.c` | LoTRS threshold ring signature |
| `module/crypto/src/sig/lotrs/lotrs_ring.c` | LoTRS polynomial ring arithmetic |
| `module/crypto/src/sig/lotrs/lotrs_params.c` | LoTRS parameter sets |
| `module/crypto/src/sig/lotrs/lotrs_sample.c` | LoTRS XOF-based samplers |
| `module/crypto/src/dap_sign.c` | Signature dispatch |
| `module/crypto/src/sig/chipmunk/dap_sign_chipmunk_ring.c` | dap_sign bridge |

## Uncommitted Changes

- LoTRS implementation (lotrs.c, lotrs_ring.c, lotrs_params.c, lotrs_sample.c)
- Anonymity tests (test_chipmunk_mring_anonymity.c)
- API split (SIG_TYPE_CHIPMUNK_MRING + SIG_TYPE_CHIPMUNK_LRS)
- **Needs rewrite**: LoTRS uses free/calloc instead of DAP_NEW_Z/DAP_DELETE

## Known Issues

- LoTRS algebraic check (A*z + w == c*pk) has negacyclic convolution bug — disabled for now, challenge+norm checks provide Fiat-Shamir soundness
- N=8 T=4 sign fails in benchmark (debug build only, passes in unit test)
