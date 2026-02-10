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

## BLK-0002 - `dap_config_open()` `.d` scan suffix check reads before buffer
- Date: 2026-02-11
- Status: `verified`
- Reporter: python-dap wrapper testing
- Components:
  - `module/core/src/dap_config.c`
  - `tests/integration/core/test_dap_config_bindings.py`
- Repro:
  - `config.cfg` + `config.cfg.d`, then `dap_config_open("config")`
  - `.d` scan sees `.`/`..` and short names during `readdir/scandir`
- Root cause:
  - `name + strlen(name) - 4` used without length guard in both Windows and POSIX branches.
  - For short names (`.`/`..`) pointer underflow produced UB.
- Fix:
  - Added `s_is_cfg_filename()` helper with safe length check (`len >= 4`) and explicit skip for `.`/`..`.
  - Replaced raw suffix checks in both scan branches with `s_is_cfg_filename(...)`.
- Validation:
  - `tests/integration/core/test_dap_config_bindings.py::IT_core_dap_config_dap_config_open__dot_d_merge_flow` now passes.
  - Full integration file: `13 passed`.
  - Merge test confirms only `*.cfg` files are applied; non-`*.cfg` files ignored.
- Notes:
  - This removes the blocker `BLK_core_dap_config_open__dot_dir_filename_underflow`.

## BLK-0003 - `dap_config_open()` unsafe `s_configs_path` prefix math (`NULL`/underflow)
- Date: 2026-02-11
- Status: `verified`
- Reporter: python-dap wrapper testing
- Components:
  - `module/core/src/dap_config.c`
  - `tests/unit/core/test_dap_config_bindings.py`
  - `tests/integration/core/test_dap_config_bindings.py`
- Repro:
  - C-level call to `dap_config_open("x")` before `dap_config_init()`.
  - Short init path (`"."`, `"/"`, etc.) with old `strlen(s_configs_path) - 4` arithmetic.
- Root cause:
  - Prefix check used `strlen(s_configs_path) - 4` with no `NULL`/length guards.
  - Could call `dap_strncmp` with invalid length and crash; also broke short-path prefix logic.
- Fix:
  - Added early guard in `dap_config_open()` for uninitialized config path (`!s_configs_path || !s_configs_path[0]`).
  - Reworked prefix decision:
    - no arithmetic underflow;
    - safe `l_configs_path_len > 4` check before prefix compare;
    - absolute paths handled without forcing `s_configs_path` prepend.
- Validation:
  - C-level `ctypes` call to `dap_config_open()` before init now returns `NULL` without crash.
  - Unit marker `UT_core_dap_config_dap_config_open__short_init_path_marker` passes.
  - Integration marker `IT_core_dap_config_dap_config_open__short_path_prefix_flow` passes.
  - Full unit + integration config binding suites pass (`85 + 13`).
- Notes:
  - This removes blocker `BLK_core_dap_config_open__configs_path_prefix_underflow`.

## BLK-0004 - `dap_config_create_empty()` NULL-deref on OOM path
- Date: 2026-02-11
- Status: `verified`
- Reporter: python-dap wrapper testing
- Components:
  - `module/core/src/dap_config.c`
  - `tests/unit/core/test_dap_config_bindings.py`
- Repro:
  - Force allocator pressure/failure around `dap_config_create_empty()`.
- Root cause:
  - `DAP_NEW_Z(dap_config_t)` result was dereferenced without `NULL` check.
  - `dap_strdup("<memory>")` result was not validated before returning object.
- Fix:
  - Added allocation checks for both `l_conf` and `l_conf->path`.
  - On failure: log allocation error, free partial allocation, return `NULL`.
- Validation:
  - Unit marker `UT_core_dap_config_dap_config_create_empty__oom_marker` converted from `xfail` to executable isolated stress scenario and now passes.
  - Full unit config binding suite passes (`85 passed`).
- Notes:
  - OOM path no longer dereferences `NULL`; returns safely.

## BLK-0005 - `g_config` dangling after close (UAF risk in `dap_config_open`)
- Date: 2026-02-11
- Status: `verified`
- Reporter: python-dap wrapper testing
- Components:
  - `module/core/src/dap_config.c`
  - `tests/unit/core/test_dap_config_bindings.py`
- Repro:
  - `global = dap_config_open("global")`
  - `dap_config_set_global(global)`
  - `dap_config_close(global)`
  - `dap_config_open("worker")` (debug path dereferences stale `g_config`)
- Root cause:
  - `dap_config_set_global()` stores borrowed pointer.
  - `dap_config_close()` did not clear `g_config` when closing the same object.
- Fix:
  - In `dap_config_close()`, added guard:
    - if `a_conf == g_config`, clear `g_config` and reset `debug_config`.
- Validation:
  - Unit marker `UT_core_dap_config_dap_config_set_global__dangling_pointer_marker` converted from `xfail` to isolated subprocess lifecycle check and now passes.
  - Integration `debug_config_flow` still passes.
  - Full unit + integration config binding suites pass.
- Notes:
  - This removes blocker `BLK_core_dap_config_set_global__dangling_global_pointer`.
