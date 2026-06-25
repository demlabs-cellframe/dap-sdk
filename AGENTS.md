# AGENTS.md — SLK Context for dap-sdk

## Current Task

**Branch:** `feature/chipmunk-ring`
**Task:** `task_ac273cea` — Chipmunk Ring Signatures
**Phase:** Phase 10 — Chipmunk Ring

## DAP SDK Code Conventions

### Memory
- `DAP_NEW_Z(type)` / `DAP_NEW_Z_SIZE(type, size)` / `DAP_NEW_Z_COUNT(type, count)`
- `DAP_DELETE(ptr)` — NEVER use `free()` directly
- `dap_memwipe(buf, size)` — wipe secret material before DAP_DELETE

### Naming
- `s_` static, `l_` local, `a_` parameter, `g_` global, `k_` constant
- snake_case everywhere

### Serialization
- `dap_serialize` for structured wire formats
- `dap_serialize_ptr_to_buffer` / `dap_serialize_ptr_from_buffer` for raw

### Error Handling
- Return errno codes: `-EINVAL`, `-ENOMEM`, `-ENOENT`, `-EAGAIN`
- `log_it(L_ERROR, ...)` for errors, `debug_if(1, L_DEBUG, ...)` for debug

### Headers
- `#pragma once` + `#ifndef` double guard
- `#define LOG_TAG "module"` before `#include "dap_common.h"`

### Security
- NEVER use `malloc`/`calloc`/`free` — use DAP_NEW_Z/DAP_DELETE
- Wipe secret buffers with `dap_memwipe` before freeing

## Phase Status

| Phase | Status |
|-------|--------|
| 7: MRNG M0-M7 | DONE |
| 8: Audit + API split + CMake cleanup | DONE |
| 9: LoTRS (keygen, sign, verify, wire, KAT, threshold) | DONE |
| 10: Chipmunk Ring | IN PROGRESS |

## Phase 10 — Chipmunk Ring

Non-interactive lattice ring signature based on LoTRS RS proof.

| Sub-phase | Description | Status |
|-----------|-------------|--------|
| 10.1 | Wire format with Rice-coded coefficients | TODO |
| 10.2 | Ring aggregation + pk_sum computation | TODO |
| 10.3 | Full sign with rejection sampling | TODO |
| 10.4 | Full verify with algebraic check | TODO |
| 10.5 | dap_sign integration + SIG_TYPE_CHIPMUNK_RING | TODO |
| 10.6 | KAT + security + benchmarks | TODO |

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
| `chipmunk_mring.c` | MRNG sign/verify |
| `chipmunk_lrs.c` | LRS 1-of-N |
| `lotrs/lotrs.c` | LoTRS threshold |
| `chipmunk_ring.c` | Chipmunk Ring (non-interactive) |
| `dap_sign_ring.c` | Unified ring API |
| `dap_serialize.c` | Schema-based serialization |

## Known Issues

- None remaining
