# Findings Tracker

| ID | Date | Source commit | Priority | Status | File | Summary | Fix commit |
|---|---|---|---|---|---|---|---|
| RF-2026-04-21-001 | 2026-04-21 | cd3871efae3e703d4396619ea41d843803980736 | P1 | FIXED | `plugin/src/dap_plugin.c:262` | `dap_plugin_start()` ignores preinit/init return codes and can report false success | this-commit |
| RF-2026-04-21-002 | 2026-04-21 | cd3871efae3e703d4396619ea41d843803980736 | P2 | FIXED | `core/include/dap_common.h:520` | `L_TPS` removed from enum while still referenced under `DAP_TPS_TEST` | this-commit |
| RF-2026-04-21-003 | 2026-04-21 | cd3871efae3e703d4396619ea41d843803980736 | P3 | FIXED | `io/dap_events_socket.c:1744` | POSIX `mq_timedsend()` success condition checks wrong return value | this-commit |
| RF-2026-04-21-004 | 2026-04-21 | cd3871efae3e703d4396619ea41d843803980736 | P1 | FIXED | `net/stream/stream/dap_stream.c:1529` | Early `return` on detached esocket skips shared packet cleanup path (`pkt_cache` / fragments reset) | this-commit |
| RF-2026-04-21-005 | 2026-04-21 | cd3871efae3e703d4396619ea41d843803980736 | P2 | FIXED | `plugin/src/dap_plugin_command.c:104` | Restart flow does not aggregate/surface errors from `load/preinit/init` all-phases APIs | this-commit |
