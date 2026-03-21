/**
 * Stub implementations for pthread scheduling functions unavailable
 * in Emscripten single-threaded (no -pthread) builds.
 * These are referenced by dap_context.c but never actually called
 * at runtime in ST mode since the scheduling code paths are guarded.
 */
#if defined(__EMSCRIPTEN__) && !defined(DAP_WASM_PTHREADS)

int sched_get_priority_min(int policy) {
    (void)policy;
    return 0;
}

int sched_get_priority_max(int policy) {
    (void)policy;
    return 0;
}

struct sched_param;
int pthread_setschedparam(unsigned long thread, int policy, const struct sched_param *param) {
    (void)thread; (void)policy; (void)param;
    return 0;
}

#endif
