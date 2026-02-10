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

## BLK-0006 - `exec_silent` guard mismatch and invalid POSIX execution path
- Date: 2026-02-11
- Status: `verified`
- Reporter: python-dap wrapper testing
- Components:
  - `module/core/include/dap_common.h`
  - `module/core/src/dap_common.c`
  - `src/python_dap/core/python_dap_common.c`
- Repro:
  - Static:
    - `rg -n "exec_silent|__MINGW32__" dap-sdk/module/core/include/dap_common.h dap-sdk/module/core/src/dap_common.c`
  - Runtime:
    - `python3 - <<'PY'`
    - `import sys; sys.path.insert(0, "src"); import python_dap as d`
    - `print(d.py_dap_exec_silent("true"))`
    - `PY`
- Root cause:
  - Header declaration was under `__MINGW32__` only, while function implementation is cross-platform.
  - POSIX branch used `execl(".", "%s", a_cmd, NULL)` which is an invalid contract and attempted process replacement.
  - Python wrapper enforced an obsolete POSIX guard (`allow_replace_process` requirement).
- Fix:
  - Made `exec_silent` declaration unguarded in header.
  - Reworked POSIX branch to `fork()` + `/bin/sh -c` + stdout/stderr redirection to `/dev/null`, with `waitpid()` status check.
  - Hardened Windows branch command buffer construction (dynamic allocation, bounded formatting).
  - Removed obsolete POSIX replacement-process guard in `py_dap_exec_silent_wrapper`.
- Validation:
  - `py_dap_exec_silent("true")` and `py_dap_exec_silent("true", True)` both return integer rc (`0` in validation run).
  - Targeted tests pass with updated contract:
    - `.venv-tests/bin/python -m pytest tests/unit/core/test_dap_common_bindings.py -k "exec_silent or exec_with_ret_multistring or dap_log_get_item or dap_log_get_last_n_lines or dap_aligned_realloc or dap_gettid" -q`
    - `.venv-tests/bin/python -m pytest tests/integration/core/test_dap_common_bindings.py -k "exec_silent or exec_with_ret_multistring or dap_aligned_realloc or dap_gettid" -q`
- Notes:
  - This closes `BLK_core_dap_common_exec_silent__guard_mismatch_and_posix_exec`.

## BLK-0007 - `exec_with_ret_multistring` fixed-size stack buffer overflow
- Date: 2026-02-11
- Status: `verified`
- Reporter: python-dap wrapper testing
- Components:
  - `module/core/src/dap_common.c`
- Repro:
  - `python3 - <<'PY'`
  - `import sys; sys.path.insert(0, "src"); import python_dap as d`
  - `print(len(d.py_dap_exec_with_ret_multistring("python3 -c \\\"print('A'*10000)\\\"")))`
  - `PY`
- Root cause:
  - Implementation concatenated command output into `char retbuf[4096]` using unbounded `strcat`.
  - Old control flow also had unsafe `goto` behavior around return-buffer lifetime.
- Fix:
  - Replaced fixed 4K `retbuf` with dynamically growing heap buffer.
  - Added overflow checks for cumulative size growth (`SIZE_MAX` guard).
  - Kept return contract (`char*` string result) without UB paths.
- Validation:
  - Long-output repro returns `str` with `len=10000`, no crash (`*** buffer overflow detected ***` no longer occurs).
- Notes:
  - This closes `BLK_core_dap_common_exec_with_ret_multistring__fixed_stack_overflow`.

## BLK-0008 - `dap_log_get_last_n_lines` early-return `FILE*` leaks
- Date: 2026-02-11
- Status: `verified`
- Reporter: python-dap wrapper testing
- Components:
  - `module/core/src/dap_common.c`
- Repro:
  - FIFO-based deterministic leak check:
    - create FIFO, repeatedly call `py_dap_log_get_last_n_lines()` where internal `fseek/ftell` fail.
    - compare `/proc/self/fd` count before/after loop.
- Root cause:
  - Multiple early returns in error branches skipped `fclose(file)`.
- Fix:
  - Added `fclose(file)` before every early return in affected error paths.
  - Added seek error handling before tail read (`fseek(file, l_n_line_pos, SEEK_SET)`).
- Validation:
  - Repeated FIFO error-path run now shows `fd_delta 0` (was `+200` before fix).
- Notes:
  - This closes `BLK_core_dap_common_dap_log_get_last_n_lines__fd_leak_on_error`.

## BLK-0009 - `dap_log_get_item` wrong `tm_year` normalization
- Date: 2026-02-11
- Status: `verified`
- Reporter: python-dap wrapper testing
- Components:
  - `module/core/src/dap_common.c`
  - `module/core/src/dap_strptime.c`
- Repro:
  - Create log with `[%m/%d/%Y-%H:%M:%S]` lines for 2024/2025.
  - Query with start time in 2026.
- Root cause:
  - `dap_log_get_item()` applied `tm_year += 2000` after `dap_strptime(..., "%Y", ...)`.
  - `struct tm.tm_year` is already years since 1900; extra addition corrupted filter timestamps.
- Fix:
  - Removed incorrect `tm_year += 2000` adjustment.
- Validation:
  - Filtering with start time in 2026 now correctly returns `None` for 2024/2025-only log file.
- Notes:
  - This closes `BLK_core_dap_common_dap_log_get_item__tm_year_adjustment`.

## BLK-0010 - `dap_interval_timer_create` fail-path memory leak
- Date: 2026-02-11
- Status: `verified`
- Reporter: python-dap wrapper testing
- Components:
  - `module/core/src/dap_common.c`
- Repro:
  - Fail-injection by setting `RLIMIT_SIGPENDING=0`, then repeated calls to `py_dap_interval_timer_create(...)`.
- Root cause:
  - Preallocated `l_timer_obj` was not freed when `CreateTimerQueueTimer`/`timer_create` failed.
  - `timer_settime` failure path lacked cleanup.
- Fix:
  - Added `DAP_DELETE(l_timer_obj)` on platform timer creation failures.
  - Added POSIX `timer_settime` failure cleanup (`timer_delete` + `DAP_DELETE`).
- Validation:
  - 120k fail-injected create calls show only negligible RSS drift (`rss_delta_kb 128`), no linear growth.
- Notes:
  - This closes `BLK_core_dap_common_dap_interval_timer_create__allocation_leak_on_create_fail`.

## BLK-0011 - debug allocator macros had invalid arity/signature
- Date: 2026-02-11
- Status: `verified`
- Reporter: python-dap wrapper testing
- Components:
  - `module/core/include/dap_common.h`
- Repro:
  - Compile minimal TU with `-DDAP_SYS_DEBUG=1` and `DAP_ALFREE(ptr)`.
  - Compile minimal TU using `DAP_ALREALLOC(...)`.
- Root cause:
  - `DAP_ALFREE(a)` expanded to `_dap_aligned_free(a, b)` (invalid arity).
  - `DAP_ALREALLOC` macros passed wrong argument list to `_dap_aligned_realloc`.
  - Debug macro block also contained malformed `MEMSTAT$SZ_NAME` define line.
- Fix:
  - Corrected `DAP_ALFREE` to one-argument form.
  - Corrected `DAP_ALREALLOC` macro signatures to pass alignment/pointer/size explicitly.
  - Fixed malformed debug define for `MEMSTAT$SZ_NAME`.
- Validation:
  - `cc -std=gnu11 -DDAP_SYS_DEBUG=1 -I dap-sdk/module/core/include -c /tmp/check_dap_alfree.c` passes.
  - `cc -std=gnu11 -I dap-sdk/module/core/include -c /tmp/check_dap_alrealloc.c` passes.
- Notes:
  - This closes `BLK_core_dap_common_dap_aligned_free__debug_macro_bad_arity`.
  - Also resolves macro-level `_dap_aligned_realloc` blocker variant where call site arity was invalid.

## BLK-0012 - platform/debug guard mismatches for `wdap_common_init` and `dap_gettid`
- Date: 2026-02-11
- Status: `verified`
- Reporter: python-dap wrapper testing
- Components:
  - `module/core/include/dap_common.h`
  - `module/core/src/dap_common.c`
  - `src/python_dap/core/python_dap_common.c`
- Repro:
  - Static:
    - `rg -n "wdap_common_init|DAP_OS_WINDOWS|WIN32|dap_gettid|DAP_SYS_DEBUG" dap-sdk/module/core/include/dap_common.h dap-sdk/module/core/src/dap_common.c`
- Root cause:
  - `wdap_common_init` declaration/implementation used different platform guards.
  - `dap_gettid` declaration was unconditional while implementation was debug-only.
- Fix:
  - Guarded `wdap_common_init` declaration with `DAP_OS_WINDOWS` and aligned source guard to `DAP_OS_WINDOWS`.
  - Guarded `dap_gettid` declaration with `DAP_SYS_DEBUG` to match implementation.
- Validation:
  - Static checks show aligned guard usage in header/source.
  - Existing wrapper behavior preserved in non-debug builds (`NotImplementedError`), and targeted unit/integration selections pass.
- Notes:
  - This closes:
    - `BLK_core_dap_common_wdap_common_init__guard_name_mismatch`
    - `BLK_core_dap_common_dap_gettid__non_debug_unavailable`
