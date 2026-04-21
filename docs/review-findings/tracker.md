# Findings Tracker

| ID | Date | Source commit | Priority | Status | File | Summary | Fix commit |
|---|---|---|---|---|---|---|---|
| RF-2026-04-21-001 | 2026-04-21 | cd3871efae3e703d4396619ea41d843803980736 | P1 | FIXED | `plugin/src/dap_plugin.c:262` | `dap_plugin_start()` ignores preinit/init return codes and can report false success | this-commit |
| RF-2026-04-21-002 | 2026-04-21 | cd3871efae3e703d4396619ea41d843803980736 | P2 | FIXED | `core/include/dap_common.h:520` | `L_TPS` removed from enum while still referenced under `DAP_TPS_TEST` | this-commit |
| RF-2026-04-21-003 | 2026-04-21 | cd3871efae3e703d4396619ea41d843803980736 | P3 | FIXED | `io/dap_events_socket.c:1744` | POSIX `mq_timedsend()` success condition checks wrong return value | this-commit |
