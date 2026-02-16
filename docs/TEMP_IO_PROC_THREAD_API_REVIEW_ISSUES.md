# TEMP IO Proc Thread API Review Issues

Generated: 2026-02-13

## Created Issues
1. `docs/BLK_io_dap_proc_thread_preinit_api_calls__fpe.md`
2. `docs/BLK_io_dap_proc_thread_stale_handle_after_deinit__hang.md`
3. `docs/BLK_io_dap_proc_thread_oom_guards_missing__null_deref_risk.md`
4. Migrated to `BLK-0029` in `docs/TEMP_PYTHON_DAP_BLOCKERS_REPORT.md`
5. `docs/BLK_io_dap_proc_thread_proc_queue_size__data_race.md`
6. `docs/BLK_io_dap_proc_thread_queue_timer_cleanup__leaks.md`

## Notes
- Runtime repro confirmed for issues 1, 2, 4, 6.
- For issue 5 (data race), stress run completed but deterministic crash was not observed without race tooling.
- For issue 3 (OOM guards), deterministic null-deref repro was not obtained in this environment; issue remains valid by code-path analysis.
