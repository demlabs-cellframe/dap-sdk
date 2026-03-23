/**
 * Stub/fallback implementations for POSIX functions unavailable
 * in Emscripten single-threaded (no -pthread) builds.
 */
/**
 * Stub/fallback implementations for POSIX functions unavailable
 * in Emscripten WASM builds.
 *
 * pipe2() stub: needed for both MT and ST WASM builds.
 * Scheduler stubs: only for non-pthreads (ST) builds where
 * Emscripten doesn't provide these symbols.
 */
#if defined(__EMSCRIPTEN__)

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifndef DAP_WASM_PTHREADS
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

int pipe2(int pipefd[2], int flags) {
    if (pipe(pipefd) < 0)
        return -1;
    if (flags & O_NONBLOCK) {
        int fl = fcntl(pipefd[0], F_GETFL, 0);
        if (fl != -1) fcntl(pipefd[0], F_SETFL, fl | O_NONBLOCK);
        fl = fcntl(pipefd[1], F_GETFL, 0);
        if (fl != -1) fcntl(pipefd[1], F_SETFL, fl | O_NONBLOCK);
    }
    return 0;
}

#endif
