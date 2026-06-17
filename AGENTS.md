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

### Serialization
- Use `dap_serialize` for structured wire formats (header, schema-based)
- Use `dap_serialize_ptr_to_buffer` / `dap_serialize_ptr_from_buffer` for raw pointer serialization
- Schema definition: `DAP_SERIALIZE_SCHEMA_DEFINE(name, struct_type, fields_array)`
- Field macros: `DAP_SERIALIZE_FIELD_SIMPLE`, `DAP_SERIALIZE_FIELD_FIXED_ARRAY`, `DAP_SERIALIZE_FIELD_NESTED`

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
| M7.3 | DONE — Benchmarks (N=2,4,8,16) + competitors (LoTRS, RingTAIL, Raptor, ML-DSA) |
| M7.4 | DONE — Production signoff selfcheck |
| M7.5 | DONE — CT audit of fold arithmetic (aggregate_X fixed) |
| M8 | DONE — anonymity audit, API split, CMake cleanup |
| M9.1 | DONE — LoTRS basic keygen/sign/verify (DAP SDK style) |
| M9.2 | DONE — LoTRS wire format + dap_sign integration |
| M9.3 | DONE — LoTRS KAT + security + benchmarks |
| M9.4 | DONE — LoTRS threshold (multi-signer) support |

## Ring Signature API

### Signature Types
| Type | ID | Description |
|------|-----|-------------|
| `SIG_TYPE_CHIPMUNK_MRING` | 0x0108 | Chipmunk MRNG log-N threshold ring |
| `SIG_TYPE_CHIPMUNK_LRS` | 0x010A | Chipmunk LRS 1-of-N linkable ring |
| `SIG_TYPE_LOTRS` | 0x010B | LoTRS lattice threshold ring (standalone) |

### Unified API (`dap_sign_ring.h`)
```c
// Non-interactive (MRNG, LRS)
dap_sign_ring_create(params, sks, ring, msg, ...) → dap_sign_t
dap_sign_ring_verify(sign, ring, msg, ...) → 0 / -errno

// Interactive (LoTRS)
dap_sign_ring_session_create(params, ring, msg, ...)
dap_sign_ring_session_round(sess, sk, idx, in, ...) → out
dap_sign_ring_session_finish(sess, ...) → dap_sign_t

// Keygen
dap_sign_ring_keygen(alg, pk, sk) → 0 / -errno
```

### LoTRS Parameters
- TEST: d=32, q=4194389, N=4, T=2
- BENCH_4OF32: d=128, q=274877906837, N=32, T=4
- BENCH: d=128, q=274877906837, N=32, T=16

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
cd tests/performance/crypto && ./run_competitors.sh
```

## Key Source Files

| File | Purpose |
|------|---------|
| `module/crypto/src/sig/chipmunk/chipmunk_mring.c` | MRNG sign/verify core |
| `module/crypto/src/sig/chipmunk/chipmunk_mring.h` | Wire layout, section offsets |
| `module/crypto/src/sig/chipmunk/chipmunk_ring.h` | Public bridge header |
| `module/crypto/src/sig/chipmunk/chipmunk_lrs.c` | LRS 1-of-N ring signature |
| `module/crypto/src/sig/lotrs/lotrs.c` | LoTRS threshold ring signature |
| `module/crypto/src/sig/lotrs/lotrs_ring.c` | LoTRS polynomial ring + dap_serialize schema |
| `module/crypto/src/sig/lotrs/lotrs_params.c` | LoTRS parameter sets |
| `module/crypto/src/sig/lotrs/lotrs_sample.c` | LoTRS SHA3-256 XOF samplers |
| `module/crypto/src/dap_sign.c` | Signature dispatch |
| `module/crypto/src/dap_sign_ring.c` | Unified ring signature API |
| `module/core/src/dap_serialize.c` | Schema-based serialization |

## Known Issues

- MRNG wire size dominated by fold tree commitments (80%) — structural cost of log-N fold
- N=8 T=4 sign fails in benchmark only (passes in unit test)
