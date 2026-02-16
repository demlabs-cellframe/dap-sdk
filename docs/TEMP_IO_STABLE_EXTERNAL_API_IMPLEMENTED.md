# IO Module Public API Snapshot (Implemented Methods)

Generated: 2026-02-13

Scope:
- Scanned public headers: `module/io/include/*.h`, `module/io/linux/dap_network_monitor.h`.
- Checked real implementations in: `module/io/*.c`, `module/io/linux/*.c`.
- This document lists only methods that have an actual C implementation (non-inline) and separates them into stable external vs internal/unsafe API.

Stability rule used:
- `stable external`: public methods without `_unsafe` and without known internal runtime entrypoints.
- `internal/unsafe`: methods marked `_unsafe` or low-level runtime hooks (context internals, worker/proc loop callbacks, etc.).

## Stable External API (Implemented)

| Method | Declaration | Implementation |
|---|---|---|
| `dap_context_current` | `module/io/include/dap_context.h:144` | `module/io/dap_context.c:106` |
| `dap_context_deinit` | `module/io/include/dap_context.h:124` | `module/io/dap_context.c:102` |
| `dap_context_init` | `module/io/include/dap_context.h:123` | `module/io/dap_context.c:85` |
| `dap_context_new` | `module/io/include/dap_context.h:127` | `module/io/dap_context.c:114` |
| `dap_context_run` | `module/io/include/dap_context.h:132` | `module/io/dap_context.c:136` |
| `dap_context_stop_n_kill` | `module/io/include/dap_context.h:137` | `module/io/dap_context.c:203` |
| `dap_cpu_assign_thread_on` | `module/io/include/dap_events.h:53` | `module/io/dap_events.c:172` |
| `dap_events_deinit` | `module/io/include/dap_events.h:37` | `module/io/dap_events.c:298` |
| `dap_events_init` | `module/io/include/dap_events.h:36` | `module/io/dap_events.c:218` |
| `dap_events_socket_assign_on_worker` | `module/io/include/dap_events_socket.h:392` | `module/io/dap_events_socket.c:438` |
| `dap_events_socket_create` | `module/io/include/dap_events_socket.h:378` | `module/io/dap_events_socket.c:533` |
| `dap_events_socket_create_type_event` | `module/io/include/dap_events_socket.h:382` | `module/io/dap_events_socket.c:747` |
| `dap_events_socket_create_type_pipe` | `module/io/include/dap_events_socket.h:385` | `module/io/dap_events_socket.c:518` |
| `dap_events_socket_create_type_queue_ptr` | `module/io/include/dap_events_socket.h:379` | `module/io/dap_events_socket.c:611` |
| `dap_events_socket_deinit` | `module/io/include/dap_events_socket.h:376` | `module/io/dap_events_socket.c:376` |
| `dap_events_socket_descriptor_close` | `module/io/include/dap_events_socket.h:428` | `module/io/dap_events_socket.c:1079` |
| `dap_events_socket_event_signal` | `module/io/include/dap_events_socket.h:387` | `module/io/dap_events_socket.c:953` |
| `dap_events_socket_init` | `module/io/include/dap_events_socket.h:375` | `module/io/dap_events_socket.c:334` |
| `dap_events_socket_insert_buf_out` | `module/io/include/dap_events_socket.h:436` | `module/io/dap_events_socket.c:1977` |
| `dap_events_socket_pop_from_buf_in` | `module/io/include/dap_events_socket.h:435` | `module/io/dap_events_socket.c:1925` |
| `dap_events_socket_queue_data_send` | `module/io/include/dap_events_socket.h:411` | `module/io/dap_events_socket.c:273` |
| `dap_events_socket_queue_ptr_send` | `module/io/include/dap_events_socket.h:412` | `module/io/dap_events_socket.c:282` |
| `dap_events_socket_reassign_between_workers` | `module/io/include/dap_events_socket.h:393` | `module/io/dap_events_socket.c:469` |
| `dap_events_socket_remove_and_delete` | `module/io/include/dap_events_socket.h:422` | `module/io/dap_events_socket.c:1603` |
| `dap_events_socket_set_readable` | `module/io/include/dap_events_socket.h:400` | `module/io/dap_events_socket.c:1646` |
| `dap_events_socket_set_writable` | `module/io/include/dap_events_socket.h:401` | `module/io/dap_events_socket.c:1690` |
| `dap_events_socket_shrink_buf_in` | `module/io/include/dap_events_socket.h:433` | `module/io/dap_events_socket.c:1946` |
| `dap_events_socket_wrap_listener` | `module/io/include/dap_events_socket.h:390` | `module/io/dap_events_socket.c:1008` |
| `dap_events_socket_wrap_no_add` | `module/io/include/dap_events_socket.h:389` | `module/io/dap_events_socket.c:388` |
| `dap_events_socket_write` | `module/io/include/dap_events_socket.h:403` | `module/io/dap_events_socket.c:1737` |
| `dap_events_socket_write_f` | `module/io/include/dap_events_socket.h:404` | `module/io/dap_events_socket.c:1776` |
| `dap_events_start` | `module/io/include/dap_events.h:40` | `module/io/dap_events.c:313` |
| `dap_events_stop_all` | `module/io/include/dap_events.h:41` | `module/io/dap_events.c:422` |
| `dap_events_thread_get_count` | `module/io/include/dap_events.h:46` | `module/io/dap_events.c:471` |
| `dap_events_wait` | `module/io/include/dap_events.h:42` | `module/io/dap_events.c:384` |
| `dap_events_worker_get` | `module/io/include/dap_events.h:51` | `module/io/dap_events.c:503` |
| `dap_events_worker_get_auto` | `module/io/include/dap_events.h:47` | `module/io/dap_events.c:480` |
| `dap_events_workers_init_status` | `module/io/include/dap_events.h:49` | `module/io/dap_events.c:118` |
| `dap_get_cpu_count` | `module/io/include/dap_events.h:52` | `module/io/dap_events.c:127` |
| `dap_net_parse_config_address` | `module/io/include/dap_net.h:70` | `module/io/dap_net.c:72` |
| `dap_net_recv` | `module/io/include/dap_net.h:71` | `module/io/dap_net.c:130` |
| `dap_net_resolve_host` | `module/io/include/dap_net.h:69` | `module/io/dap_net.c:33` |
| `dap_network_monitor_deinit` | `module/io/linux/dap_network_monitor.h:81` | `module/io/linux/dap_network_monitor.c:143` |
| `dap_network_monitor_init` | `module/io/linux/dap_network_monitor.h:76` | `module/io/linux/dap_network_monitor.c:105` |
| `dap_proc_thread_callback_add_pri` | `module/io/include/dap_proc_thread.h:71` | `module/io/dap_proc_thread.c:146` |
| `dap_proc_thread_create` | `module/io/include/dap_proc_thread.h:64` | `module/io/dap_proc_thread.c:47` |
| `dap_proc_thread_deinit` | `module/io/include/dap_proc_thread.h:66` | `module/io/dap_proc_thread.c:85` |
| `dap_proc_thread_get` | `module/io/include/dap_proc_thread.h:69` | `module/io/dap_proc_thread.c:103` |
| `dap_proc_thread_get_auto` | `module/io/include/dap_proc_thread.h:70` | `module/io/dap_proc_thread.c:121` |
| `dap_proc_thread_get_avg_queue_size` | `module/io/include/dap_proc_thread.h:81` | `module/io/dap_proc_thread.c:138` |
| `dap_proc_thread_get_count` | `module/io/include/dap_proc_thread.h:82` | `module/io/dap_proc_thread.c:112` |
| `dap_proc_thread_init` | `module/io/include/dap_proc_thread.h:65` | `module/io/dap_proc_thread.c:68` |
| `dap_proc_thread_timer_add_pri` | `module/io/include/dap_proc_thread.h:76` | `module/io/dap_proc_thread.c:273` |
| `dap_server_callbacks_set` | `module/io/include/dap_server.h:77` | `module/io/dap_server.c:252` |
| `dap_server_deinit` | `module/io/include/dap_server.h:66` | `module/io/dap_server.c:100` |
| `dap_server_delete` | `module/io/include/dap_server.h:78` | `module/io/dap_server.c:428` |
| `dap_server_enabled` | `module/io/include/dap_server.h:67` | `module/io/dap_server.c:93` |
| `dap_server_get_default` | `module/io/include/dap_server.h:70` | `module/io/dap_server.c:109` |
| `dap_server_init` | `module/io/include/dap_server.h:65` | `module/io/dap_server.c:87` |
| `dap_server_listen_addr_add` | `module/io/include/dap_server.h:75` | `module/io/dap_server.c:122` |
| `dap_server_new` | `module/io/include/dap_server.h:72` | `module/io/dap_server.c:265` |
| `dap_server_set_default` | `module/io/include/dap_server.h:69` | `module/io/dap_server.c:104` |
| `dap_timerfd_create` | `module/io/include/dap_timerfd.h:69` | `module/io/dap_timerfd.c:247` |
| `dap_timerfd_delete` | `module/io/include/dap_timerfd.h:72` | `module/io/dap_timerfd.c:463` |
| `dap_timerfd_init` | `module/io/include/dap_timerfd.h:68` | `module/io/dap_timerfd.c:183` |
| `dap_timerfd_reset` | `module/io/include/dap_timerfd.h:73` | `module/io/dap_timerfd.c:430` |
| `dap_timerfd_start` | `module/io/include/dap_timerfd.h:70` | `module/io/dap_timerfd.c:202` |
| `dap_timerfd_start_on_worker` | `module/io/include/dap_timerfd.h:71` | `module/io/dap_timerfd.c:226` |
| `dap_worker_add_events_socket` | `module/io/include/dap_worker.h:83` | `module/io/dap_worker.c:472` |
| `dap_worker_add_events_socket_auto` | `module/io/include/dap_worker.h:84` | `module/io/dap_worker.c:530` |
| `dap_worker_deinit` | `module/io/include/dap_worker.h:77` | `module/io/dap_worker.c:102` |
| `dap_worker_exec_callback_on` | `module/io/include/dap_worker.h:85` | `module/io/dap_worker.c:510` |
| `dap_worker_get_current` | `module/io/include/dap_worker.h:79` | `module/io/dap_worker.c:84` |
| `dap_worker_init` | `module/io/include/dap_worker.h:76` | `module/io/dap_worker.c:94` |
| `dap_worker_print_all` | `module/io/include/dap_events.h:44` | `module/io/dap_events.c:529` |

Total stable external methods: 75

## Public But Internal/Unsafe (Implemented)

| Method | Declaration | Implementation |
|---|---|---|
| `dap_context_add` | `module/io/include/dap_context.h:148` | `module/io/dap_context.c:459` |
| `dap_context_create_event` | `module/io/include/dap_context.h:153` | `module/io/dap_context.c:932` |
| `dap_context_create_pipe` | `module/io/include/dap_context.h:154` | `module/io/dap_context.c:1009` |
| `dap_context_create_queue` | `module/io/include/dap_context.h:152` | `module/io/dap_context.c:766` |
| `dap_context_create_queues` | `module/io/include/dap_context.h:157` | `module/io/dap_context.c:1065` |
| `dap_context_find` | `module/io/include/dap_context.h:151` | `module/io/dap_context.c:752` |
| `dap_context_poll_update` | `module/io/include/dap_context.h:150` | `module/io/dap_context.c:340` |
| `dap_context_remove` | `module/io/include/dap_context.h:149` | `module/io/dap_context.c:628` |
| `dap_del_queuetimer` | `module/io/include/dap_timerfd.h:78` | `module/io/dap_timerfd.c:471` |
| `dap_events_socket_delete_unsafe` | `module/io/include/dap_events_socket.h:405` | `module/io/dap_events_socket.c:1582` |
| `dap_events_socket_event_proc_input_unsafe` | `module/io/include/dap_events_socket.h:383` | `module/io/dap_events_socket.c:760` |
| `dap_events_socket_queue_proc_input_unsafe` | `module/io/include/dap_events_socket.h:380` | `module/io/dap_events_socket.c:626` |
| `dap_events_socket_reassign_between_workers_unsafe` | `module/io/include/dap_events_socket.h:394` | `module/io/dap_events_socket.c:450` |
| `dap_events_socket_remove_and_delete_unsafe` | `module/io/include/dap_events_socket.h:406` | `module/io/dap_events_socket.c:1103` |
| `dap_events_socket_remove_and_delete_unsafe_delayed` | `module/io/include/dap_events_socket.h:425` | `module/io/dap_events_socket.c:1057` |
| `dap_events_socket_set_readable_unsafe` | `module/io/include/dap_events_socket.h:414` | `module/io/dap_events_socket.c:1376` |
| `dap_events_socket_set_readable_unsafe_ex` | `module/io/include/dap_events_socket.h:409` | `module/io/dap_events_socket.c:1170` |
| `dap_events_socket_set_writable_unsafe` | `module/io/include/dap_events_socket.h:415` | `module/io/dap_events_socket.c:1422` |
| `dap_events_socket_set_writable_unsafe_ex` | `module/io/include/dap_events_socket.h:410` | `module/io/dap_events_socket.c:1266` |
| `dap_events_socket_write_f_unsafe` | `module/io/include/dap_events_socket.h:397` | `module/io/dap_events_socket.c:1887` |
| `dap_events_socket_write_unsafe` | `module/io/include/dap_events_socket.h:396` | `module/io/dap_events_socket.c:1841` |
| `dap_new_es_id` | `module/io/include/dap_events_socket.h:374` | `module/io/dap_events_socket.c:130` |
| `dap_proc_thread_loop` | `module/io/include/dap_proc_thread.h:67` | `module/io/dap_proc_thread.c:188` |
| `dap_timerfd_delete_unsafe` | `module/io/include/dap_timerfd.h:74` | `module/io/dap_timerfd.c:452` |
| `dap_timerfd_reset_unsafe` | `module/io/include/dap_timerfd.h:75` | `module/io/dap_timerfd.c:353` |
| `dap_worker_add_events_socket_unsafe` | `module/io/include/dap_worker.h:82` | `module/io/dap_worker.c:200` |
| `dap_worker_context_callback_started` | `module/io/include/dap_worker.h:89` | `module/io/dap_worker.c:125` |
| `dap_worker_context_callback_stopped` | `module/io/include/dap_worker.h:90` | `module/io/dap_worker.c:187` |
| `dap_worker_thread_loop` | `module/io/include/dap_worker.h:91` | `module/io/dap_worker.c:545` |

Total internal/unsafe implemented methods: 29

## Declared In Public Headers But No Non-Inline C Implementation

### Contract gaps (likely bugs / drift)

| Method | Declared at | Note |
|---|---|---|
| `dap_context_wait` | `module/io/include/dap_context.h:138` | No matching definition found in module/io/*.c |
| `dap_events_socket_remove_from_worker_unsafe` | `module/io/include/dap_events_socket.h:430` | No matching definition found in module/io/*.c |
| `dap_events_thread_get_index_min` | `module/io/include/dap_events.h:45` | Implementation appears under different name: dap_events_worker_get_index_min |
| `dap_worker_check_esocket_polled_now` | `module/io/include/dap_worker.h:87` | No matching definition found in module/io/*.c |

### Header-inline only (no .c expected)

| Method | Declared at |
|---|---|
| `dap_close_socket` | `module/io/include/dap_events_socket.h:443` |
| `dap_events_socket_get_free_buf_size` | `module/io/include/dap_events_socket.h:434` |
| `dap_events_socket_get_type_str` | `module/io/include/dap_events_socket.h:438` |
| `dap_overlapped_free` | `module/io/include/dap_events_socket.h:454` |
| `dap_proc_thread_callback_add` | `module/io/include/dap_proc_thread.h:72` |
| `dap_proc_thread_timer_add` | `module/io/include/dap_proc_thread.h:77` |
| `dap_recvfrom` | `module/io/include/dap_events_socket.h:463` |
| `dap_sendto` | `module/io/include/dap_events_socket.h:473` |

## Implemented In C But Not Declared In Public Headers

| Method | Implementation |
|---|---|
| `dap_events_worker_get_index_min` | `module/io/dap_events.c:446` |

---
This is a temporary inventory document for API cleanup and stabilization.
