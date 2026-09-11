/**
 * @file test_lifecycle_race.c
 * @brief confcall W59-R7.2: worker/proc-thread lifecycle race harness.
 *
 * @details Eight review waves (W51-W58) kept finding use-after-free and
 * double-free bugs at the exact seam this test stresses: posting owned-arg
 * callbacks to workers and proc-threads CONCURRENTLY with the reactor being
 * torn down (dap_events_stop_all() -> dap_events_wait() ->
 * dap_events_deinit()).  None of the per-module unit tests (test_dap_worker,
 * test_dap_proc_thread, test_dap_thread_pool_owned) exercise this
 * particular race: they either run happy-path single-threaded, or they
 * never call stop while a poster thread is still live.
 *
 * This harness runs N poster threads hammering
 * dap_worker_exec_callback_on() and dap_proc_thread_callback_add_pri_owned()
 * with heap-allocated, individually-tracked args while a separate
 * controller thread repeatedly starts and stops the whole reactor.  Every
 * arg is accounted exactly once via an atomic counter pair (posted vs.
 * released-exactly-once, detected via a poison write) — TSan catches any
 * data race on the shared esocket/queue/thread state, and the counters
 * catch any leak or double-release that TSan's race detector wouldn't
 * flag (e.g. a logically-lost post that just leaks quietly).
 */

#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include "dap_common.h"
#include "dap_test.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_proc_thread.h"

#define LOG_TAG "test_lifecycle_race"

#define POSTER_THREADS      4
#define POSTS_PER_THREAD    500
#define REACTOR_CYCLES      6
#define REACTOR_THREAD_COUNT 2

/* Every posted arg carries a poison guard: released_once() writes a
 * sentinel and asserts it wasn't already written, catching a double
 * release even in a build without TSan. */
typedef struct {
    _Atomic uint32_t guard;
    atomic_bool      ran;    /* true if the callback itself executed (vs. discarded on shutdown) */
} race_arg_t;

#define GUARD_LIVE     0x1abe11ed
#define GUARD_RELEASED 0xdeadbeef

static atomic_uint_fast64_t s_posted   = 0;   /* total race_arg_t allocated and successfully queued */
static atomic_uint_fast64_t s_rejected = 0;   /* total race_arg_t whose post was rejected up-front (freed inline) */
static atomic_uint_fast64_t s_released = 0;   /* total race_arg_t released exactly once (ran or discarded) */
static atomic_uint_fast64_t s_ran      = 0;   /* total race_arg_t whose callback actually executed */

static atomic_bool s_stop_posting = false;

static race_arg_t *s_race_arg_new(void)
{
    race_arg_t *l_arg = DAP_NEW_Z(race_arg_t);
    if (!l_arg)
        return NULL;
    atomic_store_explicit(&l_arg->guard, GUARD_LIVE, memory_order_relaxed);
    atomic_store_explicit(&l_arg->ran, false, memory_order_relaxed);
    return l_arg;
}

/* Owner-release callback: fires exactly once per arg, whether the item ran
 * or was discarded unrun at shutdown (dap_proc_thread's arg_free / the
 * worker queue drain path). */
static void s_race_arg_free(void *a_arg)
{
    race_arg_t *l_arg = a_arg;
    dap_assert_PIF(l_arg != NULL, "arg_free called with NULL arg");
    uint32_t l_expected = GUARD_LIVE;
    bool l_first = atomic_compare_exchange_strong_explicit(
        &l_arg->guard, &l_expected, GUARD_RELEASED,
        memory_order_acq_rel, memory_order_acquire);
    dap_assert_PIF(l_first, "race_arg released exactly once (arg_free)");
    atomic_fetch_add_explicit(&s_released, 1, memory_order_relaxed);
    DAP_DELETE(l_arg);
}

/* Proc-thread callback: dap_proc_queue_callback_t returns bool "repeat?" —
 * we never repeat, and the framework calls arg_free itself afterwards (see
 * dap_proc_thread_loop: on !repeat it just DAP_DEL_Z(l_item), NOT
 * arg_free — the arg is the CALLER's business once the callback consumed
 * it). To keep single-ownership simple, this harness treats "callback ran"
 * as the release point for proc-thread posts and does NOT also rely on
 * arg_free for the success path — only for the discard-on-shutdown path,
 * which is exactly what dap_proc_thread_callback_add_pri_owned's contract
 * documents (arg_free runs on discard, not after a normal run). */
static bool s_proc_thread_callback(void *a_arg)
{
    race_arg_t *l_arg = a_arg;
    dap_assert_PIF(l_arg != NULL, "proc_thread callback called with NULL arg");
    uint32_t l_expected = GUARD_LIVE;
    bool l_first = atomic_compare_exchange_strong_explicit(
        &l_arg->guard, &l_expected, GUARD_RELEASED,
        memory_order_acq_rel, memory_order_acquire);
    dap_assert_PIF(l_first, "race_arg released exactly once (proc_thread callback ran)");
    atomic_fetch_add_explicit(&s_ran, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&s_released, 1, memory_order_relaxed);
    DAP_DELETE(l_arg);
    return false; /* never repeat */
}

/* Worker callback: dap_worker_exec_callback_on has no owned-arg variant
 * (dap_worker_callback_t is void(*)(void*), no return, no arg_free
 * concept) — s_queue_callback_callback() in dap_worker.c always calls it
 * exactly once for anything that made it into the queue.  The only loss
 * path is dap_worker_exec_callback_on() itself returning nonzero (post
 * rejected before the arg is queued at all) — the caller below frees it
 * synchronously on that path, so every arg is still released exactly
 * once from the harness's point of view. */
static void s_worker_callback(void *a_arg)
{
    race_arg_t *l_arg = a_arg;
    dap_assert_PIF(l_arg != NULL, "worker callback called with NULL arg");
    uint32_t l_expected = GUARD_LIVE;
    bool l_first = atomic_compare_exchange_strong_explicit(
        &l_arg->guard, &l_expected, GUARD_RELEASED,
        memory_order_acq_rel, memory_order_acquire);
    dap_assert_PIF(l_first, "race_arg released exactly once (worker callback ran)");
    atomic_fetch_add_explicit(&s_ran, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&s_released, 1, memory_order_relaxed);
    DAP_DELETE(l_arg);
}

typedef struct {
    int id;
} poster_arg_t;

/* Poster thread: repeatedly grabs whatever worker/proc-thread happens to
 * be "current" (dap_events_worker_get_auto()/dap_proc_thread_get_auto())
 * and posts to it. Both getters may transiently return NULL or a stale
 * pointer while the controller thread is mid-teardown/mid-restart — that
 * is exactly the race window under test, so a rejected post (nonzero
 * return / NULL target) is a normal, expected outcome here, not a test
 * failure; only a LOST arg (never freed, never run) would be a bug, and
 * the final s_posted == s_released + s_rejected accounting below would
 * catch that.
 */
static void *s_poster_thread(void *a_arg)
{
    poster_arg_t *l_self = a_arg;
    unsigned l_seed = (unsigned)(uintptr_t)pthread_self() ^ (unsigned)l_self->id;
    for (int i = 0; i < POSTS_PER_THREAD && !atomic_load_explicit(&s_stop_posting, memory_order_relaxed); i++) {
        bool l_use_worker = (rand_r(&l_seed) & 1) != 0;
        if (l_use_worker) {
            dap_worker_t *l_worker = dap_events_worker_get_auto();
            race_arg_t *l_arg = s_race_arg_new();
            if (!l_arg)
                continue;
            atomic_fetch_add_explicit(&s_posted, 1, memory_order_relaxed);
            if (!l_worker) {
                /* No live reactor right now - the arg was never handed to
                 * anything, so the harness itself owns releasing it. */
                s_race_arg_free(l_arg);
                atomic_fetch_add_explicit(&s_rejected, 1, memory_order_relaxed);
                continue;
            }
            if (dap_worker_exec_callback_on(l_worker, s_worker_callback, l_arg) != 0) {
                s_race_arg_free(l_arg);
                atomic_fetch_add_explicit(&s_rejected, 1, memory_order_relaxed);
            }
        } else {
            dap_proc_thread_t *l_thread = dap_proc_thread_get_auto();
            race_arg_t *l_arg = s_race_arg_new();
            if (!l_arg)
                continue;
            atomic_fetch_add_explicit(&s_posted, 1, memory_order_relaxed);
            /* dap_proc_thread_callback_add_pri_owned() itself tolerates a
             * NULL thread (falls back to get_auto() again) and calls
             * arg_free on every rejection path (see module/io/
             * dap_proc_thread.c) - so we don't need a NULL short-circuit
             * here, unlike the worker branch above. */
            if (dap_proc_thread_callback_add_pri_owned(l_thread, s_proc_thread_callback, l_arg,
                                                        s_race_arg_free, DAP_QUEUE_MSG_PRIORITY_NORMAL) != 0) {
                atomic_fetch_add_explicit(&s_rejected, 1, memory_order_relaxed);
                /* arg already released by dap_proc_thread_callback_add_pri_owned's
                 * fail path (arg_free) - do not double-release here. */
            }
        }
    }
    return NULL;
}

/**
 * @brief Test: hammer workers+proc-threads with owned-arg posts while a
 * separate thread cycles the reactor through repeated start/stop/restart,
 * verifying every posted arg is accounted for exactly once (no UAF, no
 * double-free, no silent leak) under TSan.
 */
static void s_test_lifecycle_race(void)
{
    log_it(L_INFO, "Testing worker/proc-thread lifecycle race (%d posters x %d posts, %d reactor cycles)",
           POSTER_THREADS, POSTS_PER_THREAD, REACTOR_CYCLES);

    pthread_t l_posters[POSTER_THREADS];
    poster_arg_t l_poster_args[POSTER_THREADS];

    for (int i = 0; i < POSTER_THREADS; i++) {
        l_poster_args[i].id = i;
        int l_rc = pthread_create(&l_posters[i], NULL, s_poster_thread, &l_poster_args[i]);
        dap_assert(l_rc == 0, "Poster thread created");
    }

    /* Controller: cycle the reactor up/down repeatedly WHILE the posters
     * are hammering it - this is the actual race under test. Each cycle
     * gives the reactor a little time to run real callbacks before being
     * torn down again, so both the "callback ran" and "callback discarded
     * on shutdown" release paths get exercised. */
    for (int l_cycle = 0; l_cycle < REACTOR_CYCLES; l_cycle++) {
        int l_ret = dap_events_init(REACTOR_THREAD_COUNT, 60);
        dap_assert(l_ret == 0, "Reactor cycle: events_init");
        dap_assert(dap_events_start() == 0, "Reactor cycle: events_start");

        usleep(20000 + (l_cycle % 3) * 10000);   /* let posters and callbacks run for a bit */

        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
    }

    atomic_store_explicit(&s_stop_posting, true, memory_order_relaxed);
    for (int i = 0; i < POSTER_THREADS; i++)
        pthread_join(l_posters[i], NULL);

    uint64_t l_posted   = atomic_load_explicit(&s_posted, memory_order_relaxed);
    uint64_t l_rejected = atomic_load_explicit(&s_rejected, memory_order_relaxed);
    uint64_t l_released = atomic_load_explicit(&s_released, memory_order_relaxed);
    uint64_t l_ran      = atomic_load_explicit(&s_ran, memory_order_relaxed);

    log_it(L_INFO, "Lifecycle race summary: posted=%" PRIu64 " rejected_upfront=%" PRIu64
                   " released=%" PRIu64 " ran=%" PRIu64,
                   l_posted, l_rejected, l_released, l_ran);

    /* Every posted arg must be released exactly once by the time all
     * reactor cycles finished and every poster joined - whether that
     * happened up-front (the post itself was rejected and the harness
     * freed it inline), by actually running (s_worker_callback /
     * s_proc_thread_callback), or via the framework's discard-on-shutdown
     * arg_free path (dap_proc_thread's s_context_callback_stopped drain,
     * or a rejected dap_proc_thread_callback_add_pri_owned() call). Any
     * mismatch means an arg was lost (leaked, in flight forever) or freed
     * more than once (UAF/double-free) - both of which s_race_arg_free's
     * poison-guard CAS would also have aborted on immediately. */
    dap_assert(l_released == l_posted,
               "Every posted race_arg was released exactly once");
    dap_assert(l_posted > 0, "Poster threads actually posted work");
    dap_assert(l_ran > 0, "At least some callbacks actually ran on a live reactor");
}

int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);

    int l_ret = dap_common_init("test_lifecycle_race", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }

    log_it(L_INFO, "=== Lifecycle Race Harness (W59-R7.2) ===");

    s_test_lifecycle_race();

    log_it(L_INFO, "=== Lifecycle Race Harness PASSED! ===");

    dap_common_deinit();
    return 0;
}
