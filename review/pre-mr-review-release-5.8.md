# Pre-MR Review Tracker: feature/21948 -> release-5.8

Date: 2026-04-29

Branch under review: `feature/21948`
Target branch: `origin/release-5.8`
HEAD: `a6a55f5265cfe760d22c1f36b91aed01d6d4b265`
Target ref: `c889ce49e4f78794c422b178d20dd1c01ec7e120`
Merge-base: `c889ce49e4f78794c422b178d20dd1c01ec7e120`

Diff size:

- `160 files changed`
- `4590 insertions`
- `2295 deletions`

High-change areas:

- `net`
- `io`
- `core`
- `global-db`
- `plugin`
- `tests`
- `cmake`
- `3rdparty`

Current merge recommendation: the main Linux and ARM runtime test gates are now green on the current worktree, but do not merge until remaining MR readiness items and any still-open/deferred findings are resolved or waived. The branch is still not MR-ready as-is because committed history and local hygiene still need cleanup decisions, and platform/deferred risks remain.

The large fix wave resolved and verified RF-001..RF-014, RF-016..RF-018, RF-020..RF-022, RF-025, RF-029, RF-033, and several follow-up runtime findings. RF-030 is fixed and build-checked on Linux, but not fully Android-platform verified because no local NDK preset/toolchain/env was available. Final Release and targeted ASan verification are green. Earlier ASan runs intentionally disabled leak and ODR checks; the later current-head pass also has raw ASan coverage for the compiled non-`dap_tpl` suite and isolated WebSocket gates without leak/ODR suppressions. Linux host full functional gates are green, and ARM32/ARM64 QEMU runtime gates are green after making CBPF/MDBX tests reflect runtime capability instead of compile-time platform assumptions. The post-fix GlobalDB regressions from pipelines `72010`/`72009` and `72011` are targeted-verified across Linux, Windows/Wine, ARM32, and ARM64, but full CI is still pending. Remaining readiness is now dominated by MR history/hygiene, RF-015/no-mawk review, and Android-platform verification for RF-030.

## Status Legend

- `open`: not fixed yet.
- `fixed`: code change made.
- `verified`: fix was validated with the listed command/test.
- `waived`: intentionally accepted; must include reason.

Severity:

- `blocker`: should block MR.
- `high`: serious correctness/API/runtime risk.
- `medium`: important pre-release issue or coverage gap.
- `low`: cleanup, hygiene, or maintainability issue.

## Verification Summary

### Release Build

Status: passed.

Commands:

```sh
git diff --check origin/release-5.8
cmake --build build-review-linux --parallel "$(nproc)"
ctest --test-dir build-review-linux --output-on-failure
```

Results:

- `git diff --check origin/release-5.8`: passed with exit 0 and no output.
- Release build: passed with exit 0 and build completed to 100%.
- Full Release `ctest`: passed, `55/55`, 0 failed; total real time 225.10 sec.
- Full Release notable timings: `test_trans_integration` 81.42 sec, `test_global_db` 25.05 sec, and `test_udp_multiclient_regression` 2.35 sec.
- `test_io_flow_tier_ebpf` skipped by environment/permissions.
- Full Release coverage included `test_framework_*`, including `test_framework_return_type_macros_dap_tpl`.

Latest full Release gate on the current worktree, after ARM/runtime follow-up fixes:

```sh
ctest --test-dir build-review-linux --output-on-failure --timeout 900
```

Result: passed, `55/55`, 0 failed; total real time 269.44 sec. `test_trans_integration` passed in 68.68 sec, `test_global_db` passed in 22.59 sec, `test_udp_multiclient_regression` passed in 2.32 sec, and `test_io_flow_tier_ebpf` was the only skipped test.

Final full Release gate after the GlobalDB fix:

```sh
ctest --test-dir build-review-linux --output-on-failure --timeout 900
```

Result: passed, 100% tests passed, 0 failed out of 55; total real time 304.80 sec. `test_io_flow_tier_ebpf` did not run / skipped.

Targeted Release checks:

```sh
ctest --test-dir build-review-linux -R 'test_udp_multiclient_regression|test_cbpf_sticky_sessions|test_packet_routing_regression|test_io_flow_tiers|test_io_flow_tier|test_io_flow_ctrl|test_io_flow_multiclient|test_trans_udp|test_trans_dns|test_trans_http|test_trans_websocket|test_trans_integration|test_dap_stream_pkt|test_stream|test_dap_client|test_dap_file_utils|test_plugin_lifecycle|global_db|global-db|mdbx' --output-on-failure
```

Targeted Release results:

- Wide targeted Release suite: passed `25/25`, 0 failed.
- `test_io_flow_tier_ebpf` skipped by environment/permissions.
- CBPF and Application-tier IO-flow tests passed.

First must-fix batch Release checks:

```sh
git diff --check origin/release-5.8
cmake --build build-review-linux --parallel "$(nproc)"
ctest --test-dir build-review-linux -R 'test_dap_common|test_dap_common_cpp|test_dap_file_utils|test_dap_strfuncs|test_plugin_lifecycle|test_global_db|global_db|global-db' --output-on-failure
```

Results:

- `git diff --check origin/release-5.8`: passed.
- Release build: passed.
- Targeted first-batch Release suite: passed `6/6`, 0 failed.

Second must-fix batch Release checks:

```sh
git diff --check origin/release-5.8
cmake --build build-review-linux --parallel "$(nproc)"
ctest --test-dir build-review-linux -R 'test_trans_dns|test_trans_websocket|test_trans_udp|test_trans_integration|test_udp_multiclient_regression|test_udp_handshake_retrans|test_plugin_lifecycle|test_dap_common_cpp' --output-on-failure
cmake --build build-review-linux --target dap_chain_btc_rpc --parallel "$(nproc)"
ctest -N --test-dir build-review-linux -R 'json_rpc|btc'
```

Results:

- `git diff --check origin/release-5.8`: passed.
- Release build: passed.
- Targeted second-batch Release suite: passed `9/9`, including `test_trans_integration` in about 82.69 sec.
- Explicit `dap_chain_btc_rpc` target build: passed.
- JSON-RPC/BTC CTest discovery returned `Total Tests: 0`; no runtime JSON-RPC/BTC tests were registered locally.

### ASan Build

Status: targeted fixed-area checks passed; raw full ASan suite is not claimed clean.

Commands:

```sh
cmake -S . -B build-review-sanitizers/asan -DCMAKE_BUILD_TYPE=Asan -DBUILD_DAP_TESTS=ON -DBUILD_WITH_GDB_DRIVER_PGSQL=OFF
cmake --build build-review-sanitizers/asan --parallel 2
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0:detect_odr_violation=0 ctest --test-dir build-review-sanitizers/asan -R 'test_http_client_unit|test_http_client_server|test_http_simple|test_dap_http_server|test_io_flow_ctrl|test_io_flow_multiclient|test_io_flow_ctrl_multiclient|test_trans_udp|test_trans_dns|test_trans_http|test_trans_websocket|test_trans_integration|test_dap_stream_pkt|test_stream|test_udp_multiclient_regression|test_udp_handshake_retrans|test_cbpf_sticky_sessions|test_packet_routing_regression|test_dap_client|test_dap_common|test_dap_common_cpp|test_dap_file_utils|test_dap_strfuncs|test_plugin_lifecycle|test_global_db|global_db|global-db' --output-on-failure
```

Results:

- ASan configure: passed.
- ASan build: passed with exit 0, build completed to 100%, and target `dap_chain_btc_rpc` was built.
- Final combined targeted ASan suite: passed `29/29`, 0 failed; total real time 194.39 sec.
- The ASan command used `detect_leaks=0:detect_odr_violation=0` because raw ASan previously had known leak/ODR noise. Do not treat this as a clean raw full-ASan result.

First must-fix batch ASan checks:

```sh
cmake --build build-review-sanitizers/asan --parallel 2
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0:detect_odr_violation=0 ctest --test-dir build-review-sanitizers/asan -R 'test_dap_common|test_dap_common_cpp|test_dap_file_utils|test_dap_strfuncs|test_plugin_lifecycle|test_global_db|global_db|global-db' --output-on-failure
```

Results:

- ASan build: passed.
- Targeted first-batch ASan suite: passed `6/6`, 0 failed.
- Raw ASan leak and ODR findings remain deferred; this result does not close them.

ASan scope notes:

- `test_dap_config`: LeakSanitizer reports leaks from `dap_config_open()` failure path.
  - `core/src/dap_config.c:347`
  - `core/src/dap_config.c:359`
- Several transport tests hit ASan ODR detection for duplicate globals.
  - `io/dap_events.c:111`
  - `net/server/http_server/http_client/dap_http_ban_list_client.c:12`
- Leak checks were disabled for the final targeted ASan run, so leak-only findings are not closed by that result.

### Platform Checks

Status: Linux and ARM runtime gates passed on the current worktree; Windows/MXE cross-build and selected Wine runtime gates passed. The GlobalDB MDBX/Wine repair regression is targeted-verified after narrowing the skip to the test-only direct corruption injection path; full CI is still pending.

Windows/MXE:

- Skill used: `windows-mxe-container`.
- No repo-local Windows/MXE Dockerfile was found by `rg --files -g 'Dockerfile*' | rg -i 'win|windows|mxe|mingw'`.
- Repo workflow reference is `../prod_build/targets/windows.sh`; MXE is selected through `MXE_ROOT`.
- Image: `local/windows-mxe:latest`; `MXE_ROOT=/usr/src/mxe`.
- Toolchain: `/usr/src/mxe/usr/x86_64-w64-mingw32.static/share/cmake/mxe-conf.cmake`, `x86_64-w64-mingw32.static-gcc/g++ 11.4.0`, `windres` from binutils 2.38, Wine 8.0.
- Build dir: `build-review-win64`.
- Configure passed and build passed to 100%.
- Built Windows artifacts include 37 `.exe` test binaries and 91 `.a` files outside the Wine prefix.
- Windows-specific objects compiled, including `core_win32`, `wepoll`, and `io/windows/dap_io_flow_win_rio.c`.
- Wine `ctest` passed `40/41`.
- Only failing Wine test: `test_global_db`.
- First failing assertion: `tests/integration/global-db/test_global_db.c:117`, `Open MDBX env for master corruption`.
- Log included `Running under Wine -- applying MDBX compatibility workarounds`, followed by `Open MDBX env for master corruption FAILED!`.
- Relevant code path: `global-db/dap_global_db_driver_mdbx.c:587`.
- Second-wave Windows/MXE configure passed, full MXE build completed to 100%, and explicit `dap_chain_btc_rpc` target build passed.
- Second-wave CTest registration included `test_trans_websocket`, `test_trans_udp`, `test_trans_dns`, `test_plugin_lifecycle`, disabled `test_udp_multiclient_regression`, disabled `test_udp_handshake_retrans`, and active `test_udp_windows_routing_handshake_regression`.
- Second-wave Wine runtime command passed `5/5`: plugin lifecycle, DNS, WebSocket, UDP, and `test_udp_windows_routing_handshake_regression`. The Windows UDP routing/handshake regression took about 206.24 sec.
- Independent WebSocket no-PGSQL gate in `local/windows-mxe:latest` passed build and Wine CTest for `test_trans_websocket|test_trans_integration` (`2/2`; total real time 244.49 sec).
- GlobalDB follow-up verification: Windows/Wine `test_global_db` passed `1/1` in `58.49 sec`; only `mdbx master repair safety` was skipped because the test-only direct MDBX corruption injection path does not use the driver Wine workarounds.
- Residual risk: this validates the MXE cross-build and Wine runtime, not native Windows runtime. Packaging, MSYS, and native installer behavior were not validated. MXE configure emitted a non-fatal `TryRunResults.cmake` `Unknown CMake command "SET"` diagnostic while exiting 0.

ARM:

- Skill used: `arm32-arm64-container`.
- No general ARM/cross Dockerfile was found in the repo. Existing Dockerfiles are subtree-specific: `crypto/XKCP/CI/Dockerfile.ci` and `crypto/src/sig_shipovnik/streebog/Dockerfile`.
- Base local image: `cellframe/qt6-cross:bullseye`.
- ARM64 toolchain: `cmake/toolchains/arm64-linux-gnu.cmake`, `aarch64-linux-gnu-gcc 10.2.1`, `qemu-aarch64-static 5.2.0`.
- ARM32 toolchain: `cmake/toolchains/arm32-linux-gnueabihf.cmake`, `arm-linux-gnueabihf-gcc 10.2.1`, `qemu-arm-static 5.2.0`.
- The base image lacked target PostgreSQL/SQLite dev packages. The first ARM64 build failed at `global-db/dap_global_db_driver_pgsql.c:28` on missing `postgresql/libpq-fe.h`.
- Built local derived images: `local/dap-sdk-arm64-cross:bullseye` and `local/dap-sdk-arm32-cross:bullseye`.
- A combined arm64+armhf image was not viable because Debian `libpq-dev:arm64` conflicts with `libpq-dev:armhf`, so split images were used.
- ARM64 configure passed and build passed.
- ARM32 configure passed and build passed.
- ARM64 full QEMU runtime `ctest` passed `54/54`, 0 failed; expected skips: `test_io_flow_tier_ebpf` and `test_flow_tiers`.
- ARM32 full QEMU runtime `ctest` passed `54/54`, 0 failed; expected skips: `test_io_flow_tier_ebpf` and `test_flow_tiers`.
- After the final Linux WebSocket integration-test adjustment, `test_trans_integration` was rebuilt and rerun under both ARM containers:
  - ARM64: passed `1/1`, 45.36 sec.
  - ARM32: passed `1/1`, 47.26 sec.
- CBPF runtime handling was fixed so tests use `dap_io_flow_cbpf_is_available()` instead of assuming `__linux__` means attach is available. Under ARM QEMU, `SO_ATTACH_REUSEPORT_CBPF` can be present at compile time while runtime attach returns `errno=92`; CBPF-only tests now skip/pass clearly in that environment while Linux host CBPF coverage remains active.
- MDBX ARM QEMU handling was fixed without skipping the GlobalDB test by enabling `MDBX_SAFE4QEMU` for qemu cross-test builds and CI wrapper names `run-arm32`/`run-arm64`, and by making the fail-closed repair subtest independent of root-only `chmod` semantics.
- GlobalDB follow-up verification: ARM32 wrapper `run-arm32` passed in `32.38 sec` with `MDBX_SAFE4QEMU` present in flags; ARM64 wrapper `run-arm64` passed in `31.51 sec` with `MDBX_SAFE4QEMU` present in flags.
- Residual ARM risk: these are QEMU/container runtime results, not real ARM hardware results. Privileged BPF behavior and native ARM filesystem/kernel behavior still deserve hardware validation before treating ARM runtime coverage as equivalent to native production runtime.

### Additional Follow-Up Fix Coverage

The final green Release/ASan targeted suites also cover follow-up fixes that were found during the fix wave:

- UDP server stream/channel cleanup.
- UDP client cleanup use-after-free.
- Flow-control timer use-after-free and multiclient reliability.
- HTTP client double-connected callback handling and RST-response handling.
- DNS deferred stop/delete enqueue failure and checked-read cleanup.
- Stream keepalive owner-worker finalization and client live-registry cleanup.

DNS direct full-matrix follow-up on 2026-04-30: fixed existing-session DNS checked-read processing so the per-session datagram flow remains visible during synchronous packet callbacks, while delete-request cleanup still removes the session and frees the borrowed flow after checked read returns. Verified with:

```sh
cmake --build build-review-linux --target test_trans_dns test_trans_integration --parallel "$(nproc)"
build-review-linux/tests/integration/net/trans/test_trans_integration --trans=DNS --max-clients=10 --log-level=warning
build-review-linux/tests/integration/net/trans/test_trans_integration --max-clients=10 --log-level=warning
ctest --test-dir build-review-linux -R 'test_trans_dns|test_trans_integration' --output-on-failure
```

Results: build passed; DNS-only passed with `3` DNS scenarios passed, `0` failed; direct full matrix passed with `15` scenarios passed, `0` failed, including `DNS@auto - 10 servers, 10 clients`; targeted CTest passed `2/2`.

Independent DNS/WebSocket full-matrix follow-up verification at `2026-04-30 02:10:13 +07 +0700`: `git diff --check origin/release-5.8` passed with no output; Linux targeted build for `test_trans_dns`, `test_trans_websocket`, and `test_trans_integration` passed; DNS-only direct integration passed with `3` scenarios passed, `0` failed, including `DNS@auto - 10 servers, 10 clients`; full direct matrix passed with `15` scenarios passed, `0` failed in `83` sec, including WebSocket 10x10 and DNS 10x10; targeted Linux CTest passed `3/3` with `test_trans_integration` in `81.66` sec and total `81.82` sec. Targeted ASan transport CTest passed `3/3` after rebuilding the same targets, with `test_trans_integration` in `81.13` sec and total `81.35` sec, using `ASAN_OPTIONS=abort_on_error=1:detect_leaks=0:detect_odr_violation=0`; leak/ODR checks remain intentionally disabled. Windows/MXE no-PGSQL build and Wine CTest passed `3/3` in `254.75` sec total (`test_trans_websocket` `2.95` sec, `test_trans_dns` `2.57` sec, `test_trans_integration` `249.22` sec). ARM64 and ARM32 cross-builds passed for the same targets inside `local/dap-sdk-arm64-cross:bullseye` and `local/dap-sdk-arm32-cross:bullseye`; this makes no ARM runtime claim. Residual log noise remains: full-matrix green still emits existing cleanup/noisy warnings such as `dap_stream_get_links_info` sanity warnings and stream close waits, but no scenario failed.

GlobalDB regression follow-up on 2026-04-30: pipelines `72010` and `72009` regressed from the last green non-macOS pipeline `71989` on `a6a55f52`. Root causes were pgsql rejecting dotted test group/table names, ARM qemu CI wrappers `run-arm32`/`run-arm64` not matching the `MDBX_SAFE4QEMU` CMake condition, and the Wine-only failure in `s_test_mdbx_master_repair_preserves_spaces()` where test-only direct MDBX corruption injection bypassed the driver Wine compatibility flags. Changed files: `tests/integration/global-db/test_global_db.c` and `tests/integration/global-db/CMakeLists.txt`. Status: fixed and targeted-verified; full CI pending.

Verification:

- Linux `cmake --build build-review-linux --target test_global_db -j2`: passed.
- Linux `ctest --test-dir build-review-linux -R '^test_global_db$' --output-on-failure -V`: passed `1/1`, `19.95 sec`.
- Windows/Wine `test_global_db`: passed `1/1`, `58.49 sec`; skip only `mdbx master repair safety`.
- ARM32 wrapper `run-arm32`: passed, `32.38 sec`; `MDBX_SAFE4QEMU` present in flags.
- ARM64 wrapper `run-arm64`: passed, `31.51 sec`; `MDBX_SAFE4QEMU` present in flags.
- `git diff --check -- tests/integration/global-db/test_global_db.c tests/integration/global-db/CMakeLists.txt`: passed.

GlobalDB pgsql follow-up for pipeline `72011` on `58b42766`: Windows success, Android success, cppcheck skipped, macOS excluded. Failed non-macOS jobs were `428093 tests:amd64.debian`, `428094 tests:arm32.debian`, and `428095 tests:arm64.debian`.

- `428093`: pgsql failed with `Invalid key name: KEY$00000000`, `Can't write item ... (code -5)`, and `Write record to DB FAILED!`. Cause: pgsql-safe groups had been fixed, but standard test keys still used `$`.
- `428094`/`428095`: MDBX repair and fail-closed checks now passed, then the run failed later in pgsql setup with connection timeout and the hard compiled-driver init gate. In the last green non-macOS pipeline `71989`, pgsql did not run under ARM.
- Fix in `tests/integration/global-db/test_global_db.c`: pgsql standard keys use `KEY_...`; SQLite/MDBX keep `KEY$...` for existing coverage. pgsql setup/init failure returns optional skip; MDBX/SQLite init failures remain hard failures.
- Residual follow-up: real pgsql stress still logs duplicate table create / constraint messages under concurrency while exiting 0. Decide separately whether to clean that noise.

Verification:

- `git diff --check -- tests/integration/global-db/test_global_db.c`: passed.
- `cmake --build build-review-linux --target test_global_db -j2`: passed.
- `ctest --test-dir build-review-linux -R '^test_global_db$' --output-on-failure -V`: passed `1/1`, `26.69 sec` without `PG_CONNINFO`; pgsql optional skip.
- Local PostgreSQL container `postgres:16` with `PG_CONNINFO='host=127.0.0.1 port=55432 ... sslmode=disable'`: before the worker fix reproduced `Invalid key name: KEY$00000000`; after the fix the process exited 0 and functional pgsql write/read passed.
- ARM32/ARM64 wrapper verification with bad `PG_CONNINFO`: `MDBX_SAFE4QEMU` present, pgsql setup skipped, and `test_global_db` passed.

Status: fixed and targeted-verified; full CI pending until a new pipeline.

### Missing Local Tools

Static analysis was not run locally because these tools are missing:

- `cppcheck`
- `clang-tidy`
- `valgrind`
- `llvm-symbolizer`
- `scan-build`
- `bear`
- `ninja`

## Blockers And High-Risk Findings

### RF-001: Release ctest fails on UDP multiclient regression

Severity: blocker
Status: verified

Files:

- `tests/regression/net/trans/test_udp_multiclient_regression.c`
- `net/trans/udp/dap_net_trans_udp_server.c`
- `net/trans/udp/dap_net_trans_udp_stream.c`
- `io/dap_io_flow*.c`
- `net/stream/stream/dap_stream.c`

Problem:

The release build completes, but `ctest --output-on-failure` fails because `test_udp_multiclient_regression` reproduces the bug: `31/100` clients fail data exchange after all handshakes succeed.

Risk:

UDP multiclient data exchange is currently unstable in the MR. This directly affects transport reliability and should block merge into release.

Suggested fix:

Investigate the UDP flow ownership/routing/retransmit path first, especially changes in `io/dap_io_flow*`, UDP server/stream code, and stream packet processing.

Implementation summary:

- CBPF/eBPF SO_REUSEPORT programs now return `hash % listener_count`, a valid socket index.
- Linux BPF attach path passes actual listener count.
- Post-bind BPF attach failure now cleans up created listeners and fails clearly instead of hybrid SO_REUSEPORT + APPLICATION.
- BPF packet handling checks existing remote flow on other workers and forwards to owner instead of creating duplicate no-key flow.
- UDP finalize assigns `stream_worker` from actual current/owner worker when available.
- UDP server stream/channel cleanup and UDP client cleanup UAF follow-ups were fixed.
- Regression oracle tightened: receiver registration failure fails; payload compare requires exact size.
- Remaining risk: eBPF runtime was skipped by environment.

Verification:

```sh
ctest --test-dir build-review-linux -R 'test_udp_multiclient_regression|test_cbpf_sticky_sessions|test_packet_routing_regression|test_io_flow_tiers|test_io_flow_tier|test_io_flow_ctrl|test_io_flow_multiclient|test_trans_udp|test_trans_dns|test_trans_http|test_trans_websocket|test_trans_integration|test_dap_stream_pkt|test_stream|test_dap_client|test_dap_file_utils|test_plugin_lifecycle|global_db|global-db|mdbx' --output-on-failure
ASAN_OPTIONS=abort_on_error=1:detect_leaks=0:detect_odr_violation=0 ctest --test-dir build-review-sanitizers/asan -R 'test_http_client_unit|test_http_client_server|test_http_simple|test_dap_http_server|test_io_flow_ctrl|test_io_flow_multiclient|test_trans_udp|test_trans_dns|test_trans_http|test_trans_websocket|test_trans_integration|test_dap_stream_pkt|test_stream|test_udp_multiclient_regression|test_cbpf_sticky_sessions|test_packet_routing_regression|test_dap_client' --output-on-failure
ctest --test-dir build-review-linux --output-on-failure
```

Result: passed. `test_udp_multiclient_regression` passed in the wide targeted Release suite and final combined targeted ASan suite; wide targeted Release suite passed `25/25`; final combined targeted ASan suite passed `29/29`; final full Release `ctest` passed `55/55`, with `test_udp_multiclient_regression` in 2.35 sec and `test_io_flow_tier_ebpf` skipped by environment/permissions.

### RF-002: POSIX path normalization can target the wrong file

Severity: high
Status: verified

Files:

- `core/src/dap_file_utils.c:98`
- `core/src/dap_file_utils.c:1672`

Problem:

`dap_path_to_native_inplace()` converts backslash to slash on POSIX, and generic file helpers now call it before OS APIs. On Linux/macOS, backslash is a valid filename byte, not a separator.

Failure scenario:

`dap_rm_rf("cache/a\\b")` can remove `cache/a/b` instead of the literal file or directory named `cache/a\b`. `dap_file_test("a\\b")` can report on the wrong path.

Suggested fix:

Do not normalize backslashes in generic POSIX file utilities. Keep slash-to-backslash conversion Windows-only, or make POSIX conversion explicit at config/import boundaries only.

Verification:

Add POSIX tests that create both `a\\b` and `a/b`, then cover:

- `dap_file_test()`
- `dap_file_get_contents()`
- `dap_build_filename()`
- `dap_rm_rf()`

Implementation summary:

- POSIX `dap_path_to_native_inplace()` preserves backslashes; Windows conversion remains.
- Header docs updated.
- Added `test_dap_file_utils` coverage for literal `a\\b` vs nested `a/b` across path helper APIs.

Verification result: `ctest --test-dir build-review-linux -R 'test_dap_file_utils|test_plugin_lifecycle|global_db|global-db|mdbx' --output-on-failure` passed `3/3`, and targeted ASan fixed-area checks passed `6/6`.

### RF-003: Public plugin callback struct breaks API/ABI compatibility

Severity: high
Status: verified

Files:

- `plugin/include/dap_plugin.h:39`
- `plugin/src/dap_plugin.c:160`
- `plugin/src/dap_plugin.c:254`

Problem:

`dap_plugin_type_callbacks_t` inserts `preinit` and `init` before `unload`. Existing plugin type code using positional initializers like `{ load_cb, unload_cb }` now maps `unload_cb` to `preinit`, leaves `unload` NULL, and can crash later.

Separately compiled old callers are worse: the new `memcpy()` can read past the old two-field struct.

Suggested fix:

Keep the old field order, or add a size/versioned `dap_plugin_type_create_ex()`. Preserve `dap_plugin_type_create()` compatibility with the old `{ load, unload }` layout and zero new hooks by default.

Verification:

Build a dummy plugin type using the old positional initializer, then run load/preinit/start/stop under ASan/UBSan.

Implementation summary:

- Restored public `dap_plugin_type_callbacks_t` layout to legacy `{ load, unload }`.
- Added `dap_plugin_type_callbacks_ex_t` / `dap_plugin_type_create_ex()` for preinit/init.
- `dap_plugin_start_all()` compatibility wrapper now load+preinit+init; `dap_plugin_init_all()` is init phase.
- Module lifecycle state added; status running only after init; failed preinit rolls back and init is skipped.
- Follow-up: RF-020 now covers the real dlopen failure-cleanup fixture path, including no-entry shared library handling.

Verification result: `ctest --test-dir build-review-linux -R 'test_dap_file_utils|test_plugin_lifecycle|global_db|global-db|mdbx' --output-on-failure` passed `3/3`, and targeted ASan fixed-area checks passed `6/6`.

### RF-004: `dap_plugin_start_all()` public semantics changed

Severity: high
Status: verified

Files:

- `plugin/include/dap_plugin.h:55`
- `plugin/src/dap_plugin.c:200`

Problem:

`dap_plugin_start_all()` no longer starts all plugins. It only initializes already loaded modules. Existing users that do `dap_plugin_init(); dap_plugin_start_all();` can get success with no plugins loaded.

Suggested fix:

Keep old `dap_plugin_start_all()` semantics as a compatibility wrapper, or rename the new phase and require explicit migration.

Verification:

Old-sequence harness with one binary plugin should still call `plugin_init()`.

Implementation summary:

- `dap_plugin_start_all()` compatibility wrapper now preserves the old load+preinit+init sequence.
- `dap_plugin_init_all()` remains the explicit init phase for the split lifecycle.
- Follow-up: RF-020 now covers the real dlopen failure-cleanup fixture path, including no-entry shared library handling.

Verification result: `ctest --test-dir build-review-linux -R 'test_dap_file_utils|test_plugin_lifecycle|global_db|global-db|mdbx' --output-on-failure` passed `3/3`, and targeted ASan fixed-area checks passed `6/6`.

### RF-005: Bulk plugin restart calls init even after preinit failure

Severity: high
Status: verified

Files:

- `plugin/src/dap_plugin_command.c:104`
- `plugin/src/dap_plugin.c:185`
- `plugin/src/dap_plugin.c:200`

Problem:

Bulk restart proceeds to `dap_plugin_start_all()` even when `dap_plugin_preinit_all()` reports failures. A plugin whose `preinit` failed can still receive `init`, unlike single-plugin `dap_plugin_start()` which rolls back.

Suggested fix:

Track per-module lifecycle state and skip or unload modules whose preinit failed. Alternatively, make restart abort/rollback when `l_preinit_errors != 0`.

Verification:

Test plugin with `plugin_preinit()` returning nonzero and assert `plugin_init()` is not called and status is not running.

Implementation summary:

- Module lifecycle state added.
- Status is running only after init.
- Failed preinit rolls back and init is skipped.
- Follow-up: RF-020 now covers the real dlopen failure-cleanup fixture path, including no-entry shared library handling.

Verification result: `ctest --test-dir build-review-linux -R 'test_dap_file_utils|test_plugin_lifecycle|global_db|global-db|mdbx' --output-on-failure` passed `3/3`, and targeted ASan fixed-area checks passed `6/6`.

### RF-006: MDBX init failure silently falls back to SQLite

Severity: high
Status: verified

Files:

- `global-db/dap_global_db_driver.c:111`

Problem:

When configured for MDBX, any MDBX init error silently switches to SQLite.

Failure scenario:

A production node configured for `mdbx` hits corruption, permission, lock, map, or repair failure and starts against `gdb-sqlite`. This can look like an empty or different GlobalDB.

Suggested fix:

Do not fallback unless an explicit config/test mode enables it. Propagate MDBX init failure by default. If fallback remains, guarantee MDBX cleanup and make degraded mode visible.

Verification:

Make `gdb-mdbx` unreadable or corrupt and assert `dap_global_db_init()` fails and no `gdb-sqlite` is created/opened.

Implementation summary:

- Removed automatic MDBX -> SQLite fallback; fail closed by default.
- MDBX init failure paths close cursor/txn/env resources.

Verification result: `ctest --test-dir build-review-linux -R 'test_dap_file_utils|test_plugin_lifecycle|global_db|global-db|mdbx' --output-on-failure` passed `3/3`, and targeted ASan fixed-area checks passed `6/6`.

Post-regression status: fixed and targeted-verified for pipelines `72010`/`72009` and `72011`; full CI pending. Linux, Windows/Wine, ARM32, and ARM64 targeted `test_global_db` gates passed after keeping MDBX fail-closed coverage active, limiting skips to unsupported test-only MDBX repair injection, and restoring optional skip behavior for pgsql setup/init failures.

### RF-007: MDBX repair failure is ignored, then untrusted data is read as NUL-terminated

Severity: high
Status: verified

Files:

- `global-db/dap_global_db_driver_mdbx.c:565`
- `global-db/dap_global_db_driver_mdbx.c:584`

Problem:

`s_db_mdbx_check_and_repair_master()` return value is ignored. The later load path reads MDBX values using `dap_strdup()` as if they are NUL-terminated strings.

Failure scenario:

If `MDBX$MASTER` contains malformed or non-NUL data and repair fails, init continues and can read past the MDBX value buffer or open garbage group names.

Suggested fix:

Check repair return. Abort init and close the env on `<0`. Validate every master entry in the later load path and use length-bounded duplication.

Verification:

Inject a non-NUL master value and force repair failure. Run under ASan and expect clean init error, not OOB read.

Implementation summary:

- MDBX$MASTER repair result checked; init aborts on repair failure.
- Master values validated and copied by `iov_len` bounds.

Verification result: `ctest --test-dir build-review-linux -R 'test_dap_file_utils|test_plugin_lifecycle|global_db|global-db|mdbx' --output-on-failure` passed `3/3`, and targeted ASan fixed-area checks passed `6/6`.

Post-regression status: fixed and targeted-verified for pipelines `72010`/`72009` and `72011`; full CI pending. Linux, Windows/Wine, ARM32, and ARM64 targeted `test_global_db` gates passed after adding a narrow MDBX repair preflight/runtime compatibility path; MDBX repair/fail-closed checks still pass before optional pgsql setup is skipped when unavailable.

### RF-008: MDBX master repair can be destructive

Severity: high
Status: verified

Files:

- `global-db/dap_global_db_driver_mdbx.c:171`
- `global-db/dap_global_db_driver_mdbx.c:278`
- `global-db/dap_global_db_driver_mdbx.c:290`

Problem:

Repair validation rejects byte `0x20`, while public group/key validation can accept printable spaces. Also, `mdbx_put()` errors are logged but the partial rebuild can still be committed.

Failure scenario:

Repair can drop valid groups like `"foo bar"` from `MDBX$MASTER`. On write failure, it can commit an incomplete rebuilt master table.

Suggested fix:

Align validation with the public group/key validator. Abort the write transaction on any allocation, cursor, or `mdbx_put()` error.

Verification:

Create group `"foo bar"`, corrupt another master entry, restart, and verify the group is still listed. Inject `MDBX_MAP_FULL` during rebuild and verify the original master table is preserved.

Implementation summary:

- Repair validation accepts printable spaces consistently with public group/key validation.
- Repair rebuild aborts on allocation/cursor/drop/put errors and avoids committing partial master rebuild.
- Remaining risk: no direct fault injection for `mdbx_put()`/commit failure; test infra did not expose clean injector.

Verification result: `ctest --test-dir build-review-linux -R 'test_dap_file_utils|test_plugin_lifecycle|global_db|global-db|mdbx' --output-on-failure` passed `3/3`, and targeted ASan fixed-area checks passed `6/6`.

Post-regression status: fixed and targeted-verified for pipelines `72010`/`72009`; full CI pending. The repair-preserves-spaces test remains active on supported MDBX runtimes; Wine skips only the test-only direct corruption injection path.

### RF-009: Windows RIO enum checked as preprocessor macro

Severity: high
Status: verified

Files:

- `io/dap_io_flow.c:305`
- `io/dap_io_flow.c:867`
- `io/include/dap_io_flow.h:106`

Problem:

`DAP_IO_FLOW_LB_TIER_WIN_RIO` is an enum value, not a macro. `#ifdef DAP_IO_FLOW_LB_TIER_WIN_RIO` is always false.

Failure scenario:

Windows RIO sharded listener path can be reached, but inter-worker queues are not initialized and routing still only treats `DAP_IO_FLOW_LB_TIER_APPLICATION` as hash-forwarded. Same UDP flow can land on different workers with split ACK/retransmit state.

Suggested fix:

Use a runtime helper such as `s_tier_uses_application_routing(tier)` and include `WIN_RIO`. Only apply the IOCP "keep local" fallback when there is a single listener or no RIO.

Verification:

Force `WIN_RIO` with 2+ workers, inject packets for the same remote tuple through two listener workers, assert one flow owner and cross-worker forwarding.

Implementation summary:

- IO-flow routing now treats Windows RIO as a routed/sharded tier through runtime tier handling instead of dead `#ifdef` checks on enum values.
- The IO/Windows/sync-worker fix wave also added pending listener cleanup, real `dap_worker_add_events_socket()` result handling, accepted-client failure cleanup, and ASan fixture cleanup.
- Remaining risk: MXE confirmed that Windows-specific objects, including `io/windows/dap_io_flow_win_rio.c`, compile. Wine `ctest` validated general runtime coverage, but native Windows RIO behavior was not validated.

Verification result: final IO-focused Release tests passed in the wide targeted suite (`25/25`), including IO flow tier/control/multiclient coverage; final combined targeted ASan suite passed `29/29`; MXE x86_64 configure/build passed.

### RF-010: Sync worker callback can hang forever

Severity: high
Status: verified

Files:

- `io/dap_worker.c:746`
- `io/dap_server.c:644`
- `io/dap_server_helpers.c:130`

Problem:

`dap_worker_exec_callback_on_sync()` waits forever and `dap_worker_exec_callback_on()` does not report enqueue failure.

Failure scenario:

If the worker queue is full or the worker has stopped during shutdown, `dap_server_delete_sync()` or `dap_server_wait_for_ready()` can block forever. The previous delete path had timeout behavior.

Suggested fix:

Make sync callback enqueue return `bool` and support timed wait. Preserve timeout behavior for deletion and readiness checks.

Verification:

Fill or disable a worker callback queue, call `dap_server_delete_sync()` and `dap_server_wait_for_ready()`, assert they timeout instead of hanging.

Implementation summary:

- `dap_worker_exec_callback_on()` enqueue result is now observable by callers.
- The server sync-delete path was made safe for enqueue failure and stopped workers.
- Bounded fail-safe behavior was added so shutdown paths do not wait forever.
- Flow-control timer UAF and multiclient reliability follow-ups were fixed in the same IO family.

Verification result: final IO/server Release coverage passed in the wide targeted suite (`25/25`), including `test_io_flow_ctrl` and `test_io_flow_multiclient`; final combined targeted ASan suite passed `29/29`.

### RF-011: Windows `SOCKET` handle truncation

Severity: high
Status: verified

Files:

- `io/dap_io_flow_socket.c:634`
- `io/windows/dap_io_flow_win_rio.c:67`
- `net/trans/dns/dap_net_trans_dns_server.c:446`
- `net/trans/dns/dap_net_trans_dns_stream.c:730`
- `net/trans/dns/dap_net_trans_dns_stream.c:847`
- `io/include/dap_events_socket.h:269`

Problem:

New or activated socket paths store `socket()` results in `int` or pass `a_es->fd` into Winsock APIs. On 64-bit Windows, `SOCKET` is pointer-sized.

Failure scenario:

Winsock returns a handle above `INT_MAX`; truncation makes `setsockopt`, `recvfrom`, `bind`, IOCP/RIO wrapping, or cleanup operate on an invalid handle.

Suggested fix:

Use `SOCKET` end-to-end for Windows socket variables and Winsock calls. Prefer `l_es->socket` for socket APIs. Reserve `fd` for POSIX-only fd APIs.

Verification:

Build MXE x86_64 with warnings enabled, then run DNS UDP transport and Windows RIO/UDP listener tests under Wine.

Implementation summary:

- Windows socket handling was updated as part of the IO/Windows family fixes to avoid the new truncation paths and use the correct socket handle flow for Windows socket APIs.
- Accepted-client failure cleanup and listener cleanup were also tightened in the same fix wave.
- Remaining risk: MXE/Wine verification now covers cross-build and Wine runtime, but not native Windows runtime, packaging, MSYS, or installer behavior.

Verification result: Release build passed; wide targeted Release suite passed `25/25`; final combined targeted ASan suite passed `29/29`; MXE x86_64 configure/build passed and Wine `ctest` passed `40/41`, with only `test_global_db` failing on MDBX corruption/repair under Wine.

### RF-012: DNS server stop/delete races with live datagram processing

Severity: high
Status: verified

Files:

- `net/trans/dns/dap_net_trans_dns_server.c:313`
- `net/trans/dns/dap_net_trans_dns_server.c:487`
- `net/trans/dns/dap_net_trans_dns_server.c:507`
- `net/trans/dns/dap_net_trans_dns_server.c:554`

Problem:

DNS stop/delete mutates live streams and deletes the listener before all datagram processing has drained. Existing-session reads increment `datagram_reads_inflight`, but new handshakes and QoS sends are not counted.

Failure scenario:

Stop/delete during DNS traffic can race with `dap_stream_data_proc_read_ext()` or KEM response send, causing use-after-free or sendto on a deleted esocket.

Suggested fix:

Count the entire datagram callback lifetime, including new handshakes and QoS. Wait for the counter to drain before nulling stream/esocket fields or deleting the server. Re-check `stopping` before sending handshake responses.

Verification:

ASan/TSan stress test that floods DNS handshakes/data while repeatedly stopping/deleting the DNS server.

Implementation summary:

- DNS stop/delete now handles deferred stop/delete enqueue failure.
- Datagram read flow cleanup was tightened for checked-read paths.
- DNS server stream/session/channel cleanup was completed so teardown does not leave live stream session state behind.

Verification result: `test_trans_dns`, DNS coverage in `test_trans_integration`, and DNS entries in the final combined targeted ASan suite passed. Wide targeted Release suite passed `25/25`; final combined targeted ASan suite passed `29/29`.

### RF-013: DNS client writes use mutable `addr_storage`

Severity: high
Status: verified

Files:

- `net/trans/dns/dap_net_trans_dns_stream.c:636`
- `net/trans/dns/dap_net_trans_dns_stream.c:655`
- `net/trans/dns/dap_net_trans_dns_stream.c:816`
- `io/dap_context.c:1043`

Problem:

DNS client writes use `l_es->addr_storage` as the remote destination. UDP recv updates `addr_storage` with the sender of each datagram.

Failure scenario:

After a valid DNS tunnel is established, a stray or spoofed datagram from another address overwrites `addr_storage`; the next write or keepalive goes to the wrong peer.

Suggested fix:

Store resolved server sockaddr in DNS per-stream/per-transport state during prepare/connect. Use that stable address in write and remote addr callbacks. Reject incoming packets whose source does not match.

Verification:

Establish DNS transport, inject a datagram from a different UDP socket, then assert the next write still targets the original server.

Implementation summary:

- DNS remote address handling was fixed to avoid writes depending on mutable receive-side `addr_storage`.
- Checked-read flow cleanup now rejects/cleans up invalid source flows instead of mutating active tunnel destination state.
- DNS session/channel cleanup was included with the lifecycle fixes.

Verification result: `test_trans_dns`, DNS coverage in `test_trans_integration`, and DNS entries in the final combined targeted ASan suite passed. Wide targeted Release suite passed `25/25`; final combined targeted ASan suite passed `29/29`.

### RF-014: Stream packet callback can delete stream/socket while caller still dereferences it

Severity: high
Status: verified

Files:

- `net/stream/stream/dap_stream.c:1552`
- `net/stream/stream/dap_stream.c:1572`
- `net/stream/stream/dap_stream.c:1620`
- `net/stream/stream/dap_stream.c:1622`

Problem:

`s_stream_proc_pkt_in()` calls channel callback/notifiers, then checks `s_stream_esocket_is_detached(l_es)` and later cleans `a_stream->pkt_cache` and fragments. If the callback synchronously closes/removes the current stream/socket, those pointers may already be freed.

Suggested fix:

Defer stream/socket destruction while packet processing is active, or hold a real lifetime reference across callbacks. If callbacks may invalidate the stream, do not dereference `l_es` or `a_stream` after callback return.

Verification:

ASan regression where a channel callback/notifier deletes the current stream/socket during `dap_stream_data_proc_read_ext()`, including client `_inheritor` path.

Implementation summary:

- Stream packet processing was moved to checked-read APIs, with callers adapted so callback-triggered deletion does not leave the caller dereferencing freed stream/socket state.
- Keepalive owner-worker finalization and client live-registry handling were tightened as follow-ups.
- No ASan WebSocket use-after-free was reproduced after final verification.

Verification result: `test_dap_stream_pkt`, `test_stream`, `test_trans_websocket`, transport integration, and the final combined targeted ASan suite passed. Wide targeted Release suite passed `25/25`; final combined targeted ASan suite passed `29/29`.

### RF-015: Test-framework submodule drops awk fallback and requires mawk

Severity: high
Status: open

Files:

- `test-framework/dap_tpl`
- `test-framework/dap_tpl/tests/fixtures/test_helpers_common.sh:4`

Problem:

The submodule changes from `699a564` to `31a1f44`. Current helper says "strictly mawk only" and fails if `mawk` is absent. The prior fallback to `awk`/`gawk` is not present.

Failure scenario:

Test-framework scripts fail on runners with `awk` or `gawk` but without `mawk`.

Suggested fix:

Advance the submodule to a descendant that includes awk/gawk fallback, or reapply the fallback in the selected submodule revision.

Verification:

Run `tests/run.sh test-framework` in an environment with `gawk` but no `mawk`.

Current verification note:

- Full Release `ctest` now passes all registered tests, including `test_framework_*` and `test_framework_return_type_macros_dap_tpl`.
- This does not prove the documented no-`mawk` failure mode is fixed. Keep RF-015 open/deferred until the submodule/history concern is reviewed or an environment with `gawk` but no `mawk` is tested.
- The `test-framework/dap_tpl` submodule bump still needs explicit verification or waiver before MR readiness.

## Medium Findings

### RF-016: `DAP_DUP()` breaks C++ consumers

Severity: medium
Status: verified

Files:

- `core/include/dap_common.h:292`
- `tests/unit/core/test_dap_common_cpp.cpp`
- `tests/unit/core/CMakeLists.txt`

Problem:

`DAP_DUP_SIZE()` and `DAP_DUP()` lost their typed return cast and now evaluate to `void *`. This is C-compatible but breaks C++ consumers of the public header.

Suggested fix:

Restore typed result, preserving `__typeof__(p)`.

Verification:

Compile a tiny C++ TU including `dap_common.h`:

```cpp
int x = 1;
int *p = DAP_DUP(&x);
```

Implementation summary:

- Restored the public `DAP_DUP_SIZE()` / `DAP_DUP()` typed macro result for C++ consumers using `DAP_CAST(__typeof__(...))`.
- Added a C++ compile regression test for public-header compatibility.
- Changed `static atomic_int s = 0` to `ATOMIC_VAR_INIT(0)` so the public header compiles in the C++ test path.

Verification result: targeted first-batch Release and ASan suites passed `6/6`, including `test_dap_common_cpp`.

### RF-017: `dap_file_get_contents2(NULL, ...)` now crashes

Severity: medium
Status: verified

Files:

- `core/src/dap_file_utils.c:888`

Problem:

`dap_file_get_contents2()` calls `strlen(a_filename)` before validating `a_filename`.

Suggested fix:

Add `dap_return_val_if_fail(a_filename, NULL);` before the VLA copy. Consider replacing path VLAs with heap or bounded buffers.

Verification:

Add unit test for `dap_file_get_contents2(NULL, &len)` and long-path negative test.

Implementation summary:

- `dap_file_get_contents2()` validates `a_filename` before `strlen()`.
- Unit test covers NULL filename.
- Long-path/VLA broader concern remains not addressed unless already separately tracked.

Verification result: `ctest --test-dir build-review-linux -R 'test_dap_file_utils|test_plugin_lifecycle|global_db|global-db|mdbx' --output-on-failure` passed `3/3`, and targeted ASan fixed-area checks passed `6/6`.

### RF-018: BSD-derived Monero copyright notices removed

Severity: medium
Status: verified

Files:

- `core/src/common/int-util.h:1`
- `core/src/common/memwipe.c:1`
- `core/src/common/memwipe.h:1`

Problem:

The MR replaces Monero copyright notices in BSD-licensed derived files. BSD terms require retaining existing notices.

Suggested fix:

Restore Monero copyright lines and add DapCash/Cellframe notices alongside them.

Verification:

Diff these files against `origin/release-5.8` and confirm upstream notices remain.

Implementation summary:

- Restored Monero notices in `core/src/common/int-util.h`, `core/src/common/memwipe.c`, and `core/src/common/memwipe.h`.
- Notices were sourced from `origin/release-5.8`.
- Preserved the DapCash notice alongside the restored upstream notices.

Verification result: `git diff --check origin/release-5.8` passed, and targeted first-batch Release/ASan suites passed.

### RF-019: `dap_plugin_status()` reports loaded modules as running

Severity: medium
Status: verified

Files:

- `plugin/src/dap_plugin.c:398`

Problem:

With the new lifecycle, modules are inserted into `s_modules` during load before preinit/init. `dap_plugin_status()` returns `STATUS_RUNNING` for anything in `s_modules`.

Suggested fix:

Add explicit states: loaded, preinited, running, failed. Return `STATUS_RUNNING` only after successful init.

Verification:

Call `dap_plugin_load_all()` only and assert status is not running. Force init failure and assert stopped/failed.

Implementation summary:

- Plugin lifecycle state was added.
- `dap_plugin_status()` reports running only after successful init.
- Failed preinit/init paths no longer leave modules reported as running.
- Follow-up: RF-020 now covers the real dlopen failure-cleanup fixture path, including no-entry shared library handling.

Verification result: `test_plugin_lifecycle` passed in the wide targeted Release suite; earlier targeted ASan fixed-area checks also passed.

### RF-020: Binary plugin load failure cleanup is incomplete

Severity: medium
Status: verified

Files:

- `plugin/src/dap_plugin_binary.c:111`
- `plugin/src/dap_plugin_binary.c:137`
- `plugin/src/dap_plugin.c`
- plugin lifecycle tests, including new no-entry `.so` coverage

Problem:

Unix `dlerror()` is consumed twice. Also, if a library loads but has no accepted entry points, code frees private data without `dlclose()`. The function assigns `*a_pvt_data` before full success and does not clear it on failure.

Suggested fix:

Capture `dlerror()` once. Assign `*a_pvt_data` only after full success or reset it to `NULL` on failure. Close the library handle on entry-point validation failure.

Verification:

Try loading a missing `.so`, a broken `.so`, and a `.so` with no exported plugin entry points under ASan/Valgrind.

Implementation summary:

- Private plugin data is no longer published before full binary plugin load success.
- Unix error reporting uses a single `dlerror()` capture.
- Failure paths now close library handles with `dlclose()` / `FreeLibrary()`.
- Binary unload frees private data.
- Load bookkeeping rolls back through unload on failure.
- Added real no-entry shared-library test coverage.

Verification result: targeted first-batch Release and ASan suites passed `6/6`, including `test_plugin_lifecycle`.

### RF-021: GlobalDB cluster timer can outlive freed cluster

Severity: medium
Status: verified

Files:

- `global-db/dap_global_db_cluster.c:153`
- `global-db/dap_global_db_cluster.c:176`
- `global-db/dap_global_db.c:277`
- `global-db/include/dap_global_db_cluster.h`

Problem:

Cluster add schedules a recurring timer with raw cluster pointer. Deinit now frees clusters. There is no visible timer handle cancellation before freeing.

Suggested fix:

Store timer handles and cancel them before freeing clusters. Add shutdown guard for queued callbacks.

Verification:

ASan shutdown/reinit test with non-local cluster and low sync interval.

Implementation summary:

- Moved cluster sync scheduling to cancellable `dap_timerfd_start()` with owner context.
- Added shutdown/alive guarding around timer work.
- Cluster shutdown deletes sync workers before freeing cluster state.
- Residual behavior favors rare timeout leaks over timer use-after-free.

Verification result: targeted first-batch Release and ASan suites passed `6/6`, including GlobalDB coverage.

### RF-022: GlobalDB tests convert compiled driver failures into skips

Severity: medium
Status: verified

Files:

- `tests/integration/global-db/test_global_db.c`

Problem:

Compiled-in MDBX/SQLite init failures can be treated as skipped tests. CTest can go green without exercising the backend.

Suggested fix:

Fail for compiled local drivers. Only skip explicitly optional external setup such as PG without `PG_CONNINFO`.

Verification:

Force MDBX/SQLite init failure and confirm CTest fails.

Implementation summary:

- Compiled local GlobalDB drivers now fail the test gate instead of becoming skips.
- Optional PostgreSQL remains skipped when `PG_CONNINFO` is not set.

Verification result: targeted first-batch Release and ASan suites passed `6/6`, including GlobalDB coverage.

### RF-023: UDP socket sharding default changed to off

Severity: medium
Status: verified

Files:

- `io/dap_server.c:227`

Problem:

UDP socket sharding is now default-off behind `[server].udp_socket_sharding`. This avoids affinity bugs, but it is a default performance regression for multi-worker UDP deployments.

Suggested fix:

Document migration/config default decision, or gate the old behavior per protocol rather than globally.

Verification:

Run UDP throughput before/after with config absent and with `udp_socket_sharding=true`. Assert listener count and worker distribution.

### RF-024: DNS server session cleanup leaks stream sessions

Severity: medium
Status: verified

Files:

- `net/trans/dns/dap_net_trans_dns_server.c:74`
- `net/trans/dns/dap_net_trans_dns_server.c:623`
- `net/trans/dns/dap_net_trans_dns_server.c:652`

Problem:

DNS server cleanup manually frees stream pieces but does not close/free the `dap_stream_session_t` opened with `dap_stream_session_open()`.

Suggested fix:

Close via `dap_stream_session_close_mt(stream->session->id)` or route cleanup through the normal stream deletion path after safe detaching.

Verification:

DNS connect/disconnect loop under LSAN, or compare stream session list count before/after server stop.

Implementation summary:

- DNS server stream/session/channel cleanup was completed as part of RF-012/RF-013 follow-up fixes.
- Teardown now routes cleanup through the fixed lifecycle path instead of leaving session/channel state behind.
- Remaining risk: the final ASan verification disabled leak detection, so this is verified by functional DNS teardown coverage rather than LSAN.

Verification result: `test_trans_dns`, DNS integration coverage, and DNS entries in the final combined targeted ASan suite passed; wide targeted Release suite passed `25/25`.

### RF-025: DNS/WebSocket transport has POSIX-only code in Windows build paths

Severity: medium
Status: verified

Files:

- `net/trans/dns/dap_net_trans_dns_server.c:25`
- `net/trans/dns/dap_net_trans_dns_server.c:117`
- `net/trans/dns/dap_net_trans_dns_server.c:446`
- `net/trans/dns/include/dap_net_trans_dns_server.h:65`
- `net/trans/websocket/dap_net_trans_websocket_server.c:26`
- `net/trans/websocket/dap_net_trans_websocket_server.c:114`

Problem:

DNS includes `<unistd.h>`, uses `usleep()` and `MSG_DONTWAIT`, and exposes `pthread_rwlock_t` in a public DNS header. WebSocket includes `<strings.h>` and uses `strncasecmp()`.

Suggested fix:

Use project/platform wrappers for sleep, nonblocking flags, locks, and case-insensitive compare. Define Windows equivalents only when safe.

Verification:

Build `net/trans/dns` and `net/trans/websocket` on Windows/MSVC or MinGW CI.

Implementation summary:

- DNS/WebSocket Windows build paths were made portable enough for the MinGW/MXE toolchain.
- The second-wave MXE build compiled DNS and WebSocket transport test binaries, and Wine runtime coverage exercised both transports.

Verification result: second-wave Windows/MXE configure passed, full MXE build completed to 100%, and Wine `ctest` passed `5/5`, including `test_trans_dns` and `test_trans_websocket`. Linux second-wave targeted Release suite also passed `9/9`, including `test_trans_dns`, `test_trans_websocket`, `test_trans_udp`, and `test_trans_integration`.

### RF-026: WebSocket accept key accepts malformed keys and uses local SHA-1

Severity: medium
Status: fixed/verified

Files:

- `net/trans/websocket/dap_net_trans_websocket_handshake.c:12`
- `net/trans/websocket/dap_net_trans_websocket_handshake.c:146`
- `net/trans/websocket/dap_net_trans_websocket_handshake.c:154`
- `net/trans/websocket/dap_net_trans_websocket_server.c:394`

Problem:

Transport module adds private SHA-1 implementation and silently truncates `Sec-WebSocket-Key` to 64 bytes. Server does not validate that the header base64-decodes to exactly 16 bytes.

Suggested fix:

Use an existing crypto/provider API or add reusable crypto helper. Validate decoded client key length exactly 16 bytes. Reject invalid, short, empty, and overlong keys.

Verification:

Test RFC6455 vector:

- Input: `dGhlIHNhbXBsZSBub25jZQ==`
- Expected: `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`

Also test empty, non-base64, short, and overlong keys.

Fix/verification:

- Added strict `Sec-WebSocket-Key` validator: standard base64 syntax, no empty/non-base64 input, and decoded nonce length exactly 16 bytes; accept-key generation no longer truncates overlong keys.
- Covered RFC6455 accept vector and empty/non-base64/short/overlong malformed keys in `test_trans_websocket`.
- Added token-aware WebSocket upgrade header tests while preserving strict key validation.
- Existing local SHA-1 helper remains scoped to WebSocket because this tree exposes no reusable production SHA-1 provider/helper; available reusable hash API covers SHA2-256/SHA3-family, while other SHA-1 code is test-local.
- Verified with `cmake --build build-review-linux --parallel "$(nproc)"` and `ctest --test-dir build-review-linux -R 'websocket|test_trans_websocket|test_trans_integration' --output-on-failure`: passed `2/2`.

### RF-027: WebSocket upgrade flow is duplicated

Severity: medium
Status: fixed/verified

Files:

- `net/trans/websocket/dap_net_trans_websocket_server.c:362`
- `net/trans/websocket/dap_net_trans_websocket_server.c:432`

Problem:

Header lookup, validation, accept-key generation, `101` response, and protocol switch are duplicated across two paths.

Suggested fix:

Extract one shared upgrade helper or remove the unused path.

Verification:

```sh
rg -n "try_upgrade|upgrade_headers" net/trans/websocket
ctest -R websocket --output-on-failure
```

Fix/verification:

- Consolidated both WebSocket upgrade entry points through one shared helper for header lookup, upgrade/version/key validation, accept-key generation, `101 Switching Protocols`, and protocol switch.
- Tightened `Upgrade`/`Connection` matching to comma/OWS-delimited exact tokens; `notwebsocket` and `XUpgrade` are rejected, while `keep-alive, Upgrade` is accepted.
- Verified with `git diff --check -- net/trans/websocket/dap_net_trans_websocket_server.c tests/unit/net/trans/test_trans_websocket.c review/pre-mr-review-release-5.8.md`, `cmake --build build-review-linux --target test_trans_websocket --parallel "$(nproc)"`, and `ctest --test-dir build-review-linux -R 'test_trans_websocket|test_trans_integration' --output-on-failure`: passed `2/2`.

2026-04-30 WebSocket integration blocker follow-up:

- Root cause: the server could write `101 Switching Protocols` before `s_switch_to_websocket_protocol()` successfully bound the stream session and switched protocol. A failed switch left the client believing the upgrade had completed. WebSocket-upgraded streams also kept the generic stream keepalive timer, so raw stream packets could bypass WebSocket framing and reach the DAP stream parser.
- Fix: the server now switches/binds first and returns an HTTP error instead of `101` on switch failure. Server- and client-side WebSocket streams disable the generic stream keepalive timer after the WebSocket protocol is established.
- Verification: `cmake --build build-review-linux --target test_trans_websocket test_trans_integration --parallel "$(nproc)"` passed; `ctest --test-dir build-review-linux -R 'test_trans_websocket|test_trans_integration' --output-on-failure` passed `2/2`; five focused runs of `build-review-linux/tests/integration/net/trans/test_trans_integration --trans=WebSocket --max-clients=10 --log-level=info` passed `5/5` with logs `/tmp/ws-focused-repeat-{1..5}.log`.

Independent orchestrator follow-up verification, local timestamp `2026-04-30 01:17:13 +07 +0700`:

- `git diff --check origin/release-5.8`: passed with no output.
- Linux Release: `cmake --build build-review-linux --parallel "$(nproc)"` passed with exit 0 and build 100%; full `ctest --test-dir build-review-linux --output-on-failure` passed `55/55`, 0 failed, total real time 289.66 sec. `test_io_flow_tier_ebpf` skipped; `test_trans_integration` passed in 82.59 sec; `test_udp_multiclient_regression` passed in 2.37 sec.
- Targeted ASan WebSocket gate: `cmake --build build-review-sanitizers/asan --target test_trans_websocket test_trans_integration --parallel "$(nproc)"` passed; `ASAN_OPTIONS=abort_on_error=1:detect_leaks=0:detect_odr_violation=0 ctest --test-dir build-review-sanitizers/asan -R 'test_trans_websocket|test_trans_integration' --output-on-failure` passed `2/2`, total real time 76.64 sec, with `test_trans_integration` in 76.39 sec. This does not close deferred raw ASan leak/ODR work.
- Windows/MXE no-PGSQL gate inside `local/windows-mxe:latest`: `cmake --build build-review-win64-final-nopgsql --target test_trans_websocket test_trans_integration --parallel "$(nproc)"` passed; `ctest --test-dir build-review-win64-final-nopgsql -R "test_trans_websocket|test_trans_integration" --output-on-failure --timeout 240` passed `2/2`, total real time 244.49 sec; `test_trans_websocket` passed in 3.04 sec and `test_trans_integration` in 241.44 sec.
- ARM cross-build gates inside `local/dap-sdk-arm64-cross:bullseye` and `local/dap-sdk-arm32-cross:bullseye`: `cmake --build build-review-arm64 --target test_trans_websocket test_trans_integration --parallel "$(nproc)"` passed, and `cmake --build build-review-arm32 --target test_trans_websocket test_trans_integration --parallel "$(nproc)"` passed. No ARM runtime claim is made.

### RF-028: Link manager active-channel API does not work at documented preinit point

Severity: medium
Status: verified

Files:

- `net/link_manager/dap_link_manager.c:1365`

Problem:

Comment says `dap_link_manager_add_active_channel()` should be called before connections are established, e.g. from plugin preinit. But it returns `-1` unless `s_link_manager` already exists. `dap_link_manager_init()` also resets `s_active_channels` to `"RCGEND"`.

Suggested fix:

Allow pre-init registration and preserve it through `dap_link_manager_init()`, or change the documented hook and enforce ordering before first connection.

Verification:

Fixed by preserving preinit active-channel registration through `dap_link_manager_init()`.
Verified with `ctest -R 'link_manager|dap_link_manager' --output-on-failure` in `build-review-linux` (passes).

### RF-029: BTC JSON-RPC target/headers removed without compatibility path

Severity: medium
Status: verified

Files:

- `net/server/json_rpc/CMakeLists.txt:10`
- deleted `net/server/json_rpc/btc_rpc/*`

Problem:

The MR removes `btc_rpc` subdirectory, public headers, and likely `dap_chain_btc_rpc` target. Internal refs appear gone, but external plugins/apps built against release-5.8 SDK can fail.

Suggested fix:

If intentional, keep deprecated stub target/header or gate removal behind compatibility option for one release, plus migration notes. If accidental, restore.

Verification:

Build an external CMake consumer including `dap_chain_btc_rpc.h` and linking `dap_chain_btc_rpc`.

Implementation summary:

- The compatibility build path for `dap_chain_btc_rpc` is present again.
- Local verification covered the project target directly; no external consumer harness was run in this review pass.

Verification result: `cmake --build build-review-linux --target dap_chain_btc_rpc --parallel "$(nproc)"` passed. The same explicit target build also passed in the second-wave Windows/MXE environment. `ctest -N --test-dir build-review-linux -R 'json_rpc|btc'` returned `Total Tests: 0`, so this is build verification, not runtime test coverage.

### RF-030: Android MDBX `_FILE_OFFSET_BITS=64` regression risk

Severity: medium
Status: fixed

Files:

- `cmake/OS_Detection.cmake:82`
- `3rdparty/libmdbx/CMakeLists.txt:925`
- `3rdparty/libmdbx/mdbx.c:1201`

Problem:

Global `_FILE_OFFSET_BITS=64` remains for Android. MDBX rejects `_FILE_OFFSET_BITS != MDBX_WORDBITS` for `__ANDROID_API__ < 24`.

Failure scenario:

32-bit Android builds targeting API 23 or lower fail compilation.

Suggested fix:

Enforce/document Android API >= 24 for 32-bit MDBX, or strip/undef `_FILE_OFFSET_BITS` only for libmdbx on 32-bit Android API < 24.

Verification:

Configure/build:

```sh
cmake -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-23 ...
```

Then verify Android 24 still succeeds.

Implementation summary:

- The `_FILE_OFFSET_BITS`/MDBX regression path was adjusted so the host Linux build no longer regresses.
- Android-specific build verification was not run because this workspace does not have a local Android preset/toolchain/environment for the requested NDK build.

Verification result: Linux second-wave `git diff --check origin/release-5.8` passed, `cmake --build build-review-linux --parallel "$(nproc)"` passed, and the targeted second-wave Release suite passed `9/9`. This does not fully verify Android `armeabi-v7a` API 23 or API 24 behavior.

### RF-031: Global `-Werror` affects vendored/optional code

Severity: medium
Status: open

Files:

- `cmake/OS_Detection.cmake:361`
- `core/CMakeLists.txt:78`

Problem:

`add_compile_options(-Werror)` is global. Only `dap_json-c` is explicitly exempted.

Failure scenario:

Compiler upgrades can fail CI on warnings from vendored `secp256k1`, `libmdbx`, C++ MDBX, or optional crypto code.

Suggested fix:

Make `-Werror` target-scoped for first-party SDK/test targets, or add `DAP_WERROR` and explicitly opt vendored targets out.

Verification:

Build with newer GCC/Clang and:

```sh
cmake -DBUILD_DAP_SDK_TESTS=ON -DDAP_MANAGE_CFLAGS=ON ...
```

Implementation summary:

- Replaced project-global `add_compile_options(-Werror)` with `DAP_WERROR` and target-scoped `dap_target_enable_werror()`.
- Applied `-Werror` from the common first-party object target helper instead of the directory/global scope.
- Kept `DAP_WERROR_EXCLUDED_TARGETS` as an explicit cache override, with an empty default so first-party DAP targets remain strict by default.
- Vendored `dap_json-c`, `libmdbx`, and Kyber do not receive project-global `-Werror`.

Verification result:

```sh
cmake -S . -B build-rf031-werror-default -DBUILD_DAP_SDK_TESTS=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python3 - <<'PY'
import json
from pathlib import Path
cmds=json.loads(Path('build-rf031-werror-default/compile_commands.json').read_text())
checks={
 'dap_json-c':'/3rdparty/json-c/',
 'libmdbx':'/3rdparty/libmdbx/',
 'kyber512':'/crypto/src/Kyber/crypto_kem/kyber512/optimized/',
 'dap_crypto':'/crypto/src/dap_cert.c',
 'dap_trans_websocket':'/net/trans/websocket/dap_net_trans_websocket_server.c',
 'dap_core':'/core/src/dap_file_utils.c',
}
for name, marker in checks.items():
    rows=[c for c in cmds if marker in c['file']]
    print(name, [[p for p in c['command'].split() if p in ('-Werror','-Wno-error') or p.startswith('-Wno-error=')] for c in rows[:2]])
PY
cmake --build build-rf031-werror-default --parallel "$(nproc)"
```

`dap_core`, `dap_crypto`, and `dap_trans_websocket` keep target-scoped `-Werror`; `libmdbx` and Kyber have no `-Werror`. `dap_json-c` still emits its own upstream `-Werror`, followed by the existing `-Wno-error`, and no longer receives project-global `-Werror`.

### RF-032: MDBX CMake clears required/toolchain flags too broadly

Severity: medium
Status: fixed/verified

Files:

- `global-db/CMakeLists.txt:29`

Problem:

MDBX setup clears `CMAKE_REQUIRED_FLAGS`, `CMAKE_EXE_LINKER_FLAGS`, and `CMAKE_REQUIRED_LINK_OPTIONS` wholesale.

Failure scenario:

Cross toolchains relying on those linker flags for `try_compile` checks can mis-detect MDBX features or fail configure.

Suggested fix:

Filter only known bad project-injected flags such as `-Wl,--gc-sections` or `-Wl,--strip-all`. Preserve user/toolchain linker flags.

Verification:

Run MDBX-enabled configure with ARM/MXE/Android toolchains and non-empty required linker flags.

Verified:

```sh
cmake -S . -B build-rf032-linux -DCMAKE_BUILD_TYPE=Release \
  -DCELLFRAME_MODULES="core chains network" \
  -DBUILD_WITH_GDB_DRIVER_MDBX=ON \
  -DBUILD_WITH_GDB_DRIVER_SQLITE=OFF \
  -DBUILD_WITH_GDB_DRIVER_PGSQL=OFF \
  -DCMAKE_REQUIRED_FLAGS="-DRF032_REQUIRED_FLAG -Wl,-z,relro -Wl,--gc-sections -Wl,--strip-all" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,relro -Wl,--gc-sections -Wl,--strip-all" \
  -DCMAKE_REQUIRED_LINK_OPTIONS="-Wl,-z,relro;-Wl,--gc-sections;-Wl,--strip-all"
cmake --build build-rf032-linux --target dap_global_db -j2
cmake --build build-rf032-linux --target mdbx-static -j2

docker run --rm -v /home/andriyshkoy/work/node/cellframe-node/dap-sdk:/workspace \
  -w /workspace local/windows-mxe:latest bash -lc \
  'export PATH=/usr/src/mxe/usr/bin:$PATH; cmake --build build-windows-mxe --target mdbx-static -j2'
docker run --rm -v /home/andriyshkoy/work/node/cellframe-node/dap-sdk:/workspace \
  -w /workspace local/windows-mxe:latest bash -lc \
  'export PATH=/usr/src/mxe/usr/bin:$PATH; cmake --build build-windows-mxe --target dap_global_db -j2'
```

Result: Linux configure preserved non-empty required/linker flags while filtering only MDBX-known-bad `-Wl,--gc-sections`/`-Wl,--strip-all` during libmdbx configure; Linux `dap_global_db` and `mdbx-static` built. MXE reconfigure/build in `local/windows-mxe:latest` built `mdbx-static` and `dap_global_db`.

### RF-033: Windows UDP regression coverage is configure-time skipped

Severity: medium
Status: verified

Files:

- `tests/regression/net/trans/CMakeLists.txt:14`
- `tests/regression/net/trans/CMakeLists.txt:63`
- `tests/unit/io/CMakeLists.txt:284`
- `tests/unit/io/CMakeLists.txt:320`
- `tests/unit/net/trans/CMakeLists.txt:262`

Problem:

The MR changes Windows UDP/IOCP/RIO behavior, but multiclient, routing, and retransmission tests that would catch Windows-specific regressions are excluded with `if(NOT WIN32)`.

Suggested fix:

Keep a reduced Windows-specific version registered in CTest. Use runtime skip only for unavailable resources.

Verification:

MXE/Wine `ctest -N` should list at least one Windows UDP routing/handshake/retrans test.

Implementation summary:

- Windows CTest registration now keeps a reduced Windows UDP routing/handshake coverage path instead of relying only on non-Windows UDP regressions.
- Under MXE, `test_udp_multiclient_regression` and `test_udp_handshake_retrans` remain registered but disabled, while the Windows-specific routing/handshake regression is active.
- A MinGW regression in `tests/unit/plugin/test_plugin_lifecycle.c` was also fixed during this wave, keeping the second-wave Windows runtime command green.

Verification result: second-wave MXE `ctest -N` listed active `test_udp_windows_routing_handshake_regression` alongside `test_trans_websocket`, `test_trans_udp`, `test_trans_dns`, and `test_plugin_lifecycle`; it also listed disabled `test_udp_multiclient_regression` and disabled `test_udp_handshake_retrans`. The Wine runtime command passed `5/5`, including `test_udp_windows_routing_handshake_regression`, which took about 206.24 sec.

### RF-034: WebSocket HTTP unit test does not validate request body

Severity: medium
Status: open

Files:

- `tests/unit/net/trans/test_trans_websocket.c:206`

Problem:

The test captures request size, but mock returns HTTP 200 unconditionally and only checks method/path. `handshake_init()` could send empty or malformed ENC_INIT body and still pass.

Suggested fix:

Mock should validate request pointer, request size, content type, expected key material, and negative cases.

Verification:

Temporarily make production request body empty and confirm the test fails.

### RF-035: DNS integration silently clamps payload scenarios

Severity: medium
Status: open

Files:

- `tests/integration/net/trans/test_trans_integration.c:248`

Problem:

DNS payload scenarios are silently clamped to 64 KiB, but still count as passed.

Suggested fix:

Split DNS into explicit small-payload scenarios and add named large-payload skip/xfail or targeted fragmentation test.

Verification:

Run DNS-only integration and confirm report distinguishes clamped coverage from large-payload coverage.

## Low Findings And Cleanup

### RF-036: Trailing whitespace

Severity: low
Status: verified

Files:

- `crypto/src/dap_cert.c:312`
- `crypto/src/dap_cert.c:314`
- `crypto/src/oaes/oaes_lib.c:493`
- `crypto/src/oaes/oaes_lib.c:526`
- `crypto/src/oaes/oaes_lib.c:850`
- `global-db/dap_global_db_cluster.c:182`
- `global-db/dap_global_db_cluster.c:189`

Verification:

```sh
git diff --check origin/release-5.8
```

Verification result: passed.

### RF-037: CLI diagnostics write directly to stderr

Severity: low
Status: open

Files:

- `net/app-cli/dap_app_cli_net.c:93`
- multiple `[CLI-DIAG]` call sites in `net/app-cli/dap_app_cli_net.c`

Problem:

New diagnostics write directly to `stderr` and are not gated by config/log level.

Suggested fix:

Route through `log_it`/`debug_if` or gate behind explicit CLI debug flag.

Verification:

```sh
rg -n "CLI-DIAG|fprintf\\(stderr" net/app-cli/dap_app_cli_net.c
```

### RF-038: Unused/dead `dap_events_socket_queue_ptr_send()`

Severity: low
Status: open

Files:

- `io/dap_events_socket.c:1705`

Problem:

Function appears unused and contains commented-out code plus an unreachable `mq_send()` block after switch cases return.

Suggested fix:

Delete if unused, or wire properly and remove dead/commented code.

Verification:

```sh
rg -n "dap_events_socket_queue_ptr_send|queue_ptr_send" io net core
```

### RF-039: Link manager test style inconsistent

Severity: low
Status: open

Files:

- `net/link_manager/test/dap_link_manager_test.c:152`

Problem:

New test uses custom `main()`, manual `printf()`, and raw failure handling instead of local `dap_test` conventions.

Suggested fix:

Convert to local test framework style used by neighboring tests.

Verification:

```sh
ctest -R link_manager --output-on-failure
```

### RF-040: Link manager test gated only by `BUILD_DAP_SDK_TESTS`

Severity: low
Status: open

Files:

- `net/link_manager/CMakeLists.txt:39`
- `CMakeLists.txt:95`

Problem:

Top-level treats `BUILD_DAP_TESTS` as equivalent test switch, but link manager test only checks `BUILD_DAP_SDK_TESTS`.

Suggested fix:

Use:

```cmake
if(BUILD_DAP_SDK_TESTS OR BUILD_DAP_TESTS)
```

Verification:

Configure both variants and compare:

```sh
ctest -N -R dap_link_manager
```

### RF-041: UDP KEM callback error paths leak `handshake_key`

Severity: low
Status: open

Files:

- `net/trans/udp/dap_net_trans_udp_server.c:1781`
- `net/trans/udp/dap_net_trans_udp_server.c:1797`

Problem:

Failure paths free `bob_ciphertext` and result structs but not `l_result->handshake_key`.

Suggested fix:

Centralize KEM result cleanup and delete `handshake_key` on all error paths.

Verification:

Fault-inject allocation failure and worker-missing paths under LSAN.

### RF-042: uthash vendor snapshot is mixed

Severity: low
Status: open

Files:

- `3rdparty/uthash/src/uthash.h:2`
- `3rdparty/uthash/src/utlist.h:2`
- `3rdparty/uthash/src/utarray.h:2`

Problem:

`uthash.h` appears updated to a newer/current master content while sibling headers remain older and all still report version `2.3.0`.

Suggested fix:

Import the full `uthash/src` directory from one pinned commit, or document the single-header local patch.

Verification:

Compare `3rdparty/uthash/src` against chosen upstream commit with `diff -rq`.

## MR History And Hygiene

Status: open

These are not runtime blockers by themselves, but should be cleaned before review/merge.

History audit:

- Committed history `origin/release-5.8..HEAD` has 85 commits: 11 merge commits and 74 non-merge commits.
- RH-001..RH-010 remain applicable; history is not MR-ready.
- Recommended route: rebuild/squash/cherry-pick logical commits atop `origin/release-5.8`; do not carry the master merge graph unless explicitly intended.
- The `test-framework/dap_tpl` submodule bump remains RF-015/deferred and needs waiver or verification.

Hygiene audit:

- `git diff --check origin/release-5.8` is clean.
- No conflict markers, no mode-only changes, and no tracked binary/generated changes were found.
- Untracked source tests expected for MR: `tests/unit/core/test_dap_common_cpp.cpp`, `tests/unit/core/test_dap_file_utils.c`, and `tests/unit/plugin/`.
- Local artifacts/suspicious paths need decisions before MR: `(null)/` MDBX runtime state, `test_fc_multiclient.cfg`, `review/`, and `net/server/json_rpc/btc_rpc/` because release has tracked versions and current untracked files differ.

### RH-001: Very large mixed commit

Commit: `d4879598`

Concern:

GlobalDB/MDBX work, vendor updates, CMake policy changes, HTTP test rewrite, source deletions, and unrelated warning fixes are bundled under one message.

Recommendation:

Split into focused commits: vendor bump, GlobalDB behavior, test rewrite, deletions, build policy.

### RH-002: Vendor updates mixed into feature/fix commits

Commits:

- `d4879598`
- `f1e6f2ab`

Paths:

- `3rdparty/libmdbx/*`
- `3rdparty/uthash/src/uthash.h`
- `3rdparty/json-c/CMakeLists.txt`
- `3rdparty/secp256k1/src/modules/schnorrsig/main_impl.h`

Recommendation:

Isolate vendor updates in dedicated commits with upstream version/source noted. Any local vendor patch should be explicit and minimal.

Note:

`libmdbx` changes otherwise look like a clean upstream import to `v0.13.7` (`566b0f93...`), based on subagent review.

### RH-003: Deletions need MR-level justification

Commits:

- `d4879598`
- `d3be32f1`
- `f01e8437`

Paths:

- `examples/*`
- `net/server/json_rpc/btc_rpc/*`
- `test_trans.cfg`
- `docs/TRANSPORT_IMPLEMENTATION_PLAN.md`

Recommendation:

Restore or split into named cleanup/removal commits explaining why each file/module is obsolete.

### RH-004: Review artifact chain should not be in final history

Commits:

- `595d3685`
- `1bda5cb7`
- `923ab09f`
- `bf9ae38b`
- `f01e8437`

Concern:

Review-finding docs/reports are added and updated, then removed as "agent artifacts".

Recommendation:

Squash actual code fixes into relevant functional commits and drop transient report/tracker commits.

### RH-005: Revert/fix chains remain in history

Commits:

- `70c00afb`, `b7202740`
- `fe24bd72`, `cd62604c`, `c833bb9c`, `468f0841`

Recommendation:

Remove reverted attempts during rebase. Keep only final working implementation.

### RH-006: ARM32 hypothesis commits followed by rollback

Commits:

- `47619846`
- `3d6b6dfe`
- `89e68fa5`

Recommendation:

Drop or squash out reverted hypothesis work. Keep only surviving fixes.

### RH-007: Commit message `...`

Commit: `d3be32f1`

Concern:

Patch mixes IO epoll update handling, UDP sharding config, stream changes, vendored submodule change, and deletion of `test_trans.cfg`.

Recommendation:

Rename and split. The config deletion needs separate explanation.

### RH-008: Ambiguous issue-only/generic commit messages

Commits:

- `1a9aac7d`
- `336200e7`
- `a171eb14`

Recommendation:

Replace with behavioral summaries. Split unrelated fixes if they do not close the same root cause.

### RH-009: Duplicate typoed link-manager test commits

Commits:

- `dbd6dbc5`
- `3db5b5e6`

Recommendation:

Squash into one `link_manager: add unit tests` commit.

### RH-010: Confusing Werror history

Commits:

- `ee8d8211`
- `aa2ae20e`

Recommendation:

Reword/squash into a clear build-policy commit that states when `-Werror` is active.

## Current-HEAD Independent Orchestration Review Addendum

Status: fix pass completed; residual caveats tracked below.

Scope: independent pass against current HEAD `c68ee31626b9beaa8dc9f19a59779675a479915e` versus `origin/release-5.8` `c889ce49e4f78794c422b178d20dd1c01ec7e120`. This addendum preserves the older RF closure notes above. Items that overlap prior closed RFs are marked as current-head regressions or remaining gaps rather than replacing the old verification.

Latest local validation summary, updated after the current fix pass:

- `git diff --check`: passed.
- Affected-target Release build with `DAP_WERROR=ON` passed in `build-rf031-werror-default` for `dap_global_db`, `dap_io`, `dap_trans_websocket`, `test_io_flow_ctrl`, `test_io_flow_ctrl_multiclient`, `test_trans_integration`, `test_trans_udp`, and `test_trans_websocket`.
- Normal direct unit/regression gates passed: `test_io_flow_ctrl`, `test_io_flow_ctrl_multiclient`, `test_trans_websocket`, and `test_trans_udp`.
- Focused normal integration gates passed with `--log-level=warning`: HTTP `3/3`, WebSocket `3/3` including the 10-server/10-client scenario, and UDP `6/6` across Application and CBPF.
- One isolated HTTP focused run hit the known 10-server/10-client data-timeout path; the immediate isolated rerun passed `3/3`. Do not treat the invalid parallel port-conflict runs as transport failures.
- ASan rebuild in `/tmp/dap-sdk-review-asan` completed for the affected gates.
- Raw ASan, without `detect_leaks=0` or `detect_odr_violation=0`, passed for the compiled non-`dap_tpl` CTest suite (`49/49`) and for isolated WebSocket unit/focused integration gates.
- Earlier current-head ASan CTest for IO flow-control and WebSocket gates also passed `3/3` with leak/ODR suppressions; this is kept only as extra continuity with the older matrix.
- Public-header compare against `origin/release-5.8` shows the three new internal fields are no longer present in the public structs `dap_global_db_cluster_t`, `dap_io_flow_server_t`, and `dap_net_trans_websocket_private_t`.
- `git diff --name-only | rg '(^|/)dap_tpl|mawk'` returned no matches; this fix pass did not touch `dap_tpl`/mawk paths.
- `cppcheck`: not installed.
- CH-019 stabilization pass at `2026-04-30 16:02 +07`: accepted server sockets now use async worker assignment instead of blocking the accepting worker on cross-worker `dap_worker_add_events_socket()`; failed async worker-side context add now closes/frees the queued esocket.
- CH-019 verification: affected-target build passed in `build-rf031-werror-default` for `dap_io`, `dap_net_server_common`, `dap_client`, `dap_trans_http`, `dap_trans_websocket`, `test_trans_integration`, and `test_trans_websocket`; `test_trans_websocket` unit passed; focused stress passed HTTP `5/5` and WebSocket `10/10` with `--max-clients=10 --log-level=warning` (`/tmp/dap_ch019_async_summary_20260430_160252.txt`). Grep across those logs found no `Can't send es`, `slow iteration`, `Timeout for reading after connect`, `WebSocket not in OPEN state`, handshake timeout, sync delete failure, segfault, or failed scenario signature.

Current-head findings:

- CH-001: high/blocker, fixed/verified. WebSocket client CLOSE handling now consumes the CLOSE frame and returns without reusing freed private state; the original esocket delete callback is preserved by the esocket-delete wrapper and invoked during real esocket deletion.
- CH-002: high/blocker, fixed/verified. WebSocket parsing now has payload caps, overflow-safe length checks, allocation checks, RSV/opcode/control-frame validation, and explicit incomplete-frame handling.
- CH-003: high/blocker, fixed/verified. Flow-control timer callbacks now hold operation guards, delete waits for callback cleanup when needed, and callback-owned deletion avoids deadlock and stale callback arguments.
- CH-004: high, fixed/verified. Plugin dependency startup now recursively brings dependencies through load/preinit/init before the requested plugin and rolls back modules started by the failed call.
- CH-005: high/release ABI risk, fixed/verified for the newly introduced fields. `sync_timer_ctx`, `flows_deleting`, and `original_delete_callback` were removed from public structs and moved to private side tables/internal storage. Header compare against `origin/release-5.8` shows those struct layouts restored; separate public API/source additions should still be reviewed on their own merits.
- CH-006: medium, fixed/verified. Datagram flow creation now checks `dap_worker_add_events_socket()` and fails cleanly instead of continuing with an unregistered send socket.
- CH-007: medium, partially fixed/residual. Flow-control shells now have retired refs and are freed at deinit when safe. Bounded runtime reclamation is still not implemented, so long-lived churn can still retain retired shells until deinit.
- CH-008: medium, fixed/verified. `DAP_NEW_Z_SIZE` now routes through checked allocation.
- CH-009: medium, fixed/verified. POSIX `DAP_ALREALLOC` preserves the alignment contract by only reallocating allocator-native alignment and returning `NULL` without freeing the original block for unsupported over-aligned resize.
- CH-010: medium/coverage, fixed/verified. Focused WebSocket 10-server/10-client coverage is enabled and passes under the current warning-level focused gate; repeated 10x10 transport stress is covered by CH-019.
- CH-011: medium/coverage, fixed/verified. Windows file-utils normalization coverage was extended for slash-containing temp paths.
- CH-012: medium/low, fixed/verified. Legacy GlobalDB tests now skip only optional PostgreSQL without `PG_CONNINFO`; compiled local driver init failures fail the gate.
- CH-013: medium, fixed/verified. Link-manager active channel remove now handles the pre-init case symmetrically with add.
- CH-014: low, fixed with serialization caveat. BTC unsupported shims now return a JSON-RPC-shaped error object under the current handler API, but that object is still serialized through the existing result path.
- CH-015: low, fixed/verified. `dap_global_db_driver_hash_print` is restored as a `static inline` helper.
- CH-016: high, fixed/verified. `dap_timerfd_start_on_worker()` now checks `dap_worker_add_events_socket()` failure and cleans up the timer events socket/inheritor; `dap_timerfd_create()` also cleans up failed wrap/create/settime paths.
- CH-017: medium/validation, fixed/verified. UDP encrypted-packet/no-key warnings are now rate-limited, removing the ASan CTest log flood while keeping early and periodic warning signal.
- CH-018: high, fixed/verified. WebSocket client delete-callback preservation no longer keys cleanup state by soon-freed WebSocket-private memory or mutates the raw esocket callback from the close path. The side table is keyed by `dap_events_socket_t *`; the wrapper detaches stream state and invokes the original callback only during real esocket deletion. Verified by normal and raw-ASan WebSocket unit/focused integration gates.
- CH-019: medium, fixed/verified. The 10-server/10-client transport load sensitivity was traced to synchronous cross-worker accepted-socket assignment: worker accept callbacks could block waiting for another worker while that worker was also accepting and waiting back, producing `-ETIMEDOUT`, slow worker iterations, and handshake timeouts. Accepted server sockets now enqueue cross-worker assignment asynchronously via `dap_worker_add_events_socket_async()`, while client connect paths retain synchronous add semantics. HTTP `5/5` and WebSocket `10/10` focused 10x10 repeats passed with no old timeout/stall signatures.

## Suggested Fix Order

1. Keep RF-015/no-`mawk` as an explicit deferred or waived item. This pass intentionally did not touch `dap_tpl` or mawk-related files.
2. Run Android verification for RF-030 if it must be fully platform-verified before merge; Windows/MXE and ARM32/ARM64 cross-build gates are already covered by earlier notes.
3. Decide whether the remaining `test-framework`/`dap_tpl` ASan and no-`mawk` scope must be fixed before merge, or document a scoped waiver.
4. Rework MR history: isolate vendor updates, drop reverted/artifact commits, split deletions and mixed commits. Prefer rebuilding/squashing/cherry-picking logical commits on `origin/release-5.8` instead of carrying the current merge graph.

## Verification Checklist For Closure

Minimum before MR can be considered again:

- [x] `git diff --check origin/release-5.8` passes.
- [x] Release build passes.
- [x] Full Release `ctest --output-on-failure` passes (`55/55`; total real time 225.10 sec; `test_trans_integration` 81.42 sec; `test_global_db` 25.05 sec; `test_udp_multiclient_regression` 2.35 sec; `test_io_flow_tier_ebpf` skipped by environment).
- [x] Wide targeted Release suite passes (`25/25`; `test_io_flow_tier_ebpf` skipped by environment).
- [x] `test_udp_multiclient_regression` passes in final Release and ASan targeted suites.
- [x] Final targeted ASan suite passes (`29/29`; total real time 194.39 sec) with `abort_on_error=1:detect_leaks=0:detect_odr_violation=0`.
- [x] First must-fix targeted Release suite passes (`6/6`) for common/core, plugin lifecycle, and GlobalDB coverage.
- [x] First must-fix targeted ASan suite passes (`6/6`) with `abort_on_error=1:detect_leaks=0:detect_odr_violation=0`.
- [x] Second must-fix targeted Release suite passes (`9/9`) for DNS, WebSocket, UDP, UDP regressions, plugin lifecycle, and C++ common coverage.
- [x] `dap_chain_btc_rpc` target builds on Linux Release, Linux ASan, and Windows/MXE.
- [x] Raw compiled non-`dap_tpl` ASan CTest suite is clean without leak/ODR suppressing options (`49/49`); `test-framework`/`dap_tpl` remains RF-015/out-of-scope for this pass.
- [ ] RF-015 no-`mawk` submodule/test-framework condition is verified or waived.
- [x] Windows/MXE build passes.
- [x] At least one Windows UDP routing/handshake/retrans coverage path is registered in CTest.
- [x] Second-wave MXE/Wine runtime command passes for plugin lifecycle, DNS, WebSocket, UDP, and Windows UDP routing/handshake regression (`5/5`).
- [x] ARM32/ARM64 cross builds pass.
- [ ] Android build decision for MDBX + `_FILE_OFFSET_BITS` is documented and verified on Android; RF-030 is fixed and Linux-build checked, but not Android NDK verified locally.
- [x] Plugin compatibility path is verified with old callback initializer and old `dap_plugin_start_all()` sequence; coverage is in-process, not a real dlopen plugin fixture.
- [x] Plugin binary no-entry shared-library failure path is covered by real dlopen fixture testing.
- [x] GlobalDB MDBX failure/repair tests cover corrupt and non-NUL master records and space preservation; direct `mdbx_put()`/commit fault injection is still missing.
- [x] GlobalDB compiled local driver failures fail the test gate; optional PostgreSQL without `PG_CONNINFO` remains a skip.
- [x] DNS transport lifecycle/address handling is covered by final Release and targeted ASan suites.
- [x] Stream callback deletion fix is covered by final Release and targeted ASan suites.
- [x] WebSocket RF-026/RF-027 integration blocker follow-up is fixed and covered by Release build, targeted CTest, and five focused WebSocket integration repeats.
- [x] Independent WebSocket follow-up gates passed at `2026-04-30 01:17:13 +07 +0700`: Linux Release full CTest `55/55` in 289.66 sec, targeted ASan WebSocket gate `2/2` in 76.64 sec with leak/ODR checks disabled, Windows/MXE no-PGSQL WebSocket gate `2/2` in 244.49 sec, and ARM32/ARM64 WebSocket cross-build gates. Raw ASan leak/ODR and ARM runtime validation remain deferred.
- [x] Independent DNS/WebSocket full-matrix follow-up gates passed at `2026-04-30 02:10:13 +07 +0700`: `git diff --check origin/release-5.8` had no output; Linux targeted build passed; DNS-only direct passed `3/3`; full direct matrix passed `15/15` in 83 sec; Linux targeted CTest passed `3/3`; targeted ASan CTest passed `3/3` with leak/ODR checks disabled; Windows/MXE no-PGSQL Wine CTest passed `3/3`; ARM32/ARM64 cross-build gates passed. Existing cleanup/noisy warnings remain in logs, with no scenario failures; raw ASan leak/ODR, ARM runtime, Android RF-030, and RF-015/no-`mawk` remain deferred/open as tracked above.
- [x] WebSocket server-side switch cleanup gaps verified at `2026-04-30`: Linux targeted build passed, Linux targeted CTest `2/2` passed, and ASan targeted CTest `2/2` passed with leak/ODR checks disabled.
- [x] Current-head fix pass affected-target Release build passed in `build-rf031-werror-default` with `DAP_WERROR=ON`.
- [x] Current-head focused normal integration gates passed: HTTP `3/3`, WebSocket `3/3` including 10-server/10-client, and UDP `6/6` across Application and CBPF.
- [x] Current-head raw-ASan WebSocket unit and focused integration gates passed without leak/ODR suppressing options; earlier IO flow-control/WebSocket ASan CTest passed `3/3` with leak/ODR checks disabled.
- [x] Current-head hygiene passed: `git diff --check` clean and no changed paths matching `dap_tpl`/mawk.
- [x] CH-005 public struct-layout regression from new internal fields is fixed/verified by moving those fields out of public structs.
- [x] CH-018 WebSocket delete-callback side table is fixed/verified by normal and raw-ASan WebSocket gates.
- [x] CH-019 transport 10-server/10-client load sensitivity is fixed/verified by async accepted-socket worker assignment and focused HTTP `5/5` + WebSocket `10/10` stress with no old stall/timeout signatures.
- [ ] MR history is rebased/squashed into reviewable commits.
- [ ] MR hygiene decisions are made for expected untracked tests and local artifacts. Current untracked set includes `review/pre-mr-review-release-5.8.md`, `test_fc_multiclient.cfg`, and `(null)/var/lib/global_db/gdb-mdbx/{mdbx.dat,mdbx.lck}`.
