# Temporary Python-DAP Blockers Report

This file is a temporary working log for blocker bugs found while testing `python-dap` wrappers against `dap-sdk`.

Goal:
- keep reproducible reports in one place;
- track fix status and validation evidence;
- simplify preparing MR descriptions later.

## How To Add New Entry

Copy this template:

```md
## BLK-XXXX - <short title>
- Date: YYYY-MM-DD
- Status: `open` | `in_progress` | `fixed` | `verified`
- Reporter: <name/team>
- Components:
  - `path/to/file`
- Repro:
  - <command/script>
- Root cause:
  - <short explanation>
- Fix:
  - <what changed>
- Validation:
  - <what was run and result>
- Notes:
  - <follow-ups/risks>
```

## Entries

## BLK-0001 - `_dap_aligned_realloc` reallocates aligned pointer (SIGSEGV)
- Date: 2026-02-10
- Status: `verified`
- Reporter: python-dap wrapper testing
- Components:
  - `module/core/include/dap_common.h`
  - `src/python_dap/core/python_dap_common.c`
- Repro:
  - `PYTHONPATH=src python3 - <<'PY'`
  - `import python_dap`
  - `cap = python_dap.py_dap_aligned_alloc(64, 128)`
  - `python_dap.py_dap_aligned_realloc(cap, 64, 256)`
  - `PY`
- Root cause:
  - `_dap_aligned_alloc()` stores base pointer in `((uintptr_t *)al_ptr)[-1]`.
  - `_dap_aligned_realloc()` called `DAP_REALLOC((uint8_t*)bptr, ...)` where `bptr` is the aligned pointer, violating realloc contract.
- Fix:
  - `_dap_aligned_realloc()` now reads base pointer from `((uintptr_t *)bptr)[-1]` and calls realloc on base pointer.
  - Added alignment and size-overflow validation in `_dap_aligned_alloc()` and `_dap_aligned_realloc()`.
  - Added wrapper-level alignment validation (`non-zero power of two`) in:
    - `py_dap_aligned_alloc_wrapper`
    - `py_dap_aligned_realloc_wrapper`
- Validation:
  - Repro script no longer crashes; `py_dap_aligned_realloc(...)` returns updated capsule.
  - Invalid alignment (`0`, non-power-of-two) now rejected early.
- Notes:
  - Because aligned allocator helpers are `DAP_STATIC_INLINE` in `dap_common.h`, consumers must be rebuilt after this header fix.
