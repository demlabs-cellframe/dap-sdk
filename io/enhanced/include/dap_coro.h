/*
 * dap_coro.h — Lightweight coroutines (stackful + stackless)
 *
 * Stackful (dap_coro_*):
 *   Metadata (dap_coro_t, 64 B) lives in a pool-managed region.
 *   POSIX:   slot = [guard page | usable stack | dap_coro_t].
 *            ASM context switch (~20 ns per yield/resume).
 *   Windows: slot = [dap_coro_t] only; fiber stacks managed by OS.
 *            SwitchToFiber with full SEH/FLS/XMM state save.
 *   The region can be the tail of an MPSC mmap/VirtualAlloc
 *   or a standalone allocation.
 *
 * Stackless (dap_sl_*):
 *   State-machine coroutines: step() returns DONE / YIELD / WAIT.
 *   Inline state buffer (DAP_SL_STATE_MAX bytes) per coroutine.
 *   Heap-backed slab pool — no mmap, no guard pages (~224 B/slot).
 *   Selective wake by opaque key (fd, timer_id, etc.).
 *   Best for massive counts of simple automata (parsers, protocols).
 *
 * Both flavors share the same two-list scheduler pattern
 * (run carousel + wait queue).  Pool is single-threaded
 * (processor thread only).
 *
 * Supported: x86_64, aarch64, Windows x64 (Fibers).
 * Single-header, inline implementation.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif

/* DAP_THREADLOCAL defined in dap_io_plat.h */

/* ================================================================== */
/*  Coroutine states                                                    */
/* ================================================================== */

enum {
    DAP_CORO_READY     = 0,  /* freshly created, not yet resumed */
    DAP_CORO_RUNNING   = 1,  /* currently executing (active on CPU) */
    DAP_CORO_SUSPENDED = 2,  /* runnable — scheduler picks up next tick */
    DAP_CORO_WAITING   = 3,  /* blocked on I/O or event — needs explicit wake */
    DAP_CORO_DONE      = 4   /* function returned, slot can be released */
};

#define DAP_CORO_STACK_DEFAULT  (64u * 1024)
#define DAP_CORO_META_SIZE      64

/* ================================================================== */
/*  Platform-specific context switch (POSIX only)                       */
/*                                                                     */
/*  void dap_coro_switch(void **from_sp, void *to_sp)                  */
/*    Saves callee-saved registers + rsp into *from_sp,                */
/*    restores from to_sp.                                             */
/*                                                                     */
/*  On Windows, SwitchToFiber handles context switching — no ASM.      */
/* ================================================================== */

#ifndef _WIN32

#if defined(__x86_64__) || defined(_M_X64)

static __attribute__((naked, used, noinline))
void dap_coro_switch(void **a_from_sp __attribute__((unused)),
                     void  *a_to_sp   __attribute__((unused)))
{
    __asm__ volatile (
        "pushq %%rbx\n\t"
        "pushq %%rbp\n\t"
        "pushq %%r12\n\t"
        "pushq %%r13\n\t"
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"
        "movq  %%rsp, (%%rdi)\n\t"
        "movq  %%rsi, %%rsp\n\t"
        "popq  %%r15\n\t"
        "popq  %%r14\n\t"
        "popq  %%r13\n\t"
        "popq  %%r12\n\t"
        "popq  %%rbp\n\t"
        "popq  %%rbx\n\t"
        "ret\n\t"
        ::: "memory"
    );
}

#define DAP_CORO_NREGS 6

#elif defined(__aarch64__)

static __attribute__((naked, used, noinline))
void dap_coro_switch(void **a_from_sp __attribute__((unused)),
                     void  *a_to_sp   __attribute__((unused)))
{
    __asm__ volatile (
        "stp x19, x20, [sp, #-160]!\n\t"
        "stp x21, x22, [sp, #16]\n\t"
        "stp x23, x24, [sp, #32]\n\t"
        "stp x25, x26, [sp, #48]\n\t"
        "stp x27, x28, [sp, #64]\n\t"
        "stp x29, x30, [sp, #80]\n\t"
        "stp d8,  d9,  [sp, #96]\n\t"
        "stp d10, d11, [sp, #112]\n\t"
        "stp d12, d13, [sp, #128]\n\t"
        "stp d14, d15, [sp, #144]\n\t"
        "mov x2, sp\n\t"
        "str x2, [x0]\n\t"
        "mov sp, x1\n\t"
        "ldp d14, d15, [sp, #144]\n\t"
        "ldp d12, d13, [sp, #128]\n\t"
        "ldp d10, d11, [sp, #112]\n\t"
        "ldp d8,  d9,  [sp, #96]\n\t"
        "ldp x29, x30, [sp, #80]\n\t"
        "ldp x27, x28, [sp, #64]\n\t"
        "ldp x25, x26, [sp, #48]\n\t"
        "ldp x23, x24, [sp, #32]\n\t"
        "ldp x21, x22, [sp, #16]\n\t"
        "ldp x19, x20, [sp], #160\n\t"
        "ret\n\t"
        ::: "memory"
    );
}

#define DAP_CORO_NREGS 20

#else
#error "dap_coroutine: unsupported architecture"
#endif

#endif /* !_WIN32 */

/* ================================================================== */
/*  Coroutine structure — embedded at top of each pool slot             */
/* ================================================================== */

typedef void (*dap_coro_func_t)(void *);

/** @brief Coroutine metadata (64 bytes, one cache line). Processor-private. */
typedef struct dap_coro {
#ifdef _WIN32
    void            *fiber;         /* LPVOID from CreateFiberEx */
#else
    void            *sp;
    char            *stack_base;    /* usable stack start (after guard) */
#endif
    dap_coro_func_t  func;
    void            *arg;
    struct dap_coro *next;          /* freelist link when cached */
    int              state;
} dap_coro_t;

_Static_assert(sizeof(dap_coro_t) <= DAP_CORO_META_SIZE,
               "dap_coro_t exceeds DAP_CORO_META_SIZE");

/** Thread-local scheduler context used by yield/resume to switch back. */
#ifdef _WIN32
static DAP_THREADLOCAL void       *s_sched_fiber;
#else
static DAP_THREADLOCAL void       *s_sched_sp;
#endif
static DAP_THREADLOCAL dap_coro_t *s_coro_current;

/* ================================================================== */
/*  Coroutine entry trampoline                                          */
/* ================================================================== */

#ifdef _WIN32

/** @brief Windows fiber entry point: run the user function and switch back. */
static void CALLBACK s_coro_fiber_entry(void *a_param)
{
    dap_coro_t *l_co = (dap_coro_t *)a_param;
    l_co->func(l_co->arg);
    l_co->state = DAP_CORO_DONE;
    SwitchToFiber(s_sched_fiber);
    for (;;) SwitchToFiber(s_sched_fiber);
}

#else

/** @brief Posix asm trampoline target: run the user function and switch back. */
static void s_coro_entry(void)
{
    dap_coro_t *l_co = s_coro_current;
    l_co->func(l_co->arg);
    l_co->state = DAP_CORO_DONE;
    dap_coro_switch(&l_co->sp, s_sched_sp);
    __builtin_unreachable();
}

#endif

/* ================================================================== */
/*  Thread init/fini — must bracket coroutine usage on each thread      */
/*                                                                     */
/*  POSIX:   noop (s_sched_sp is saved implicitly by dap_coro_switch). */
/*  Windows: ConvertThreadToFiber so SwitchToFiber works both ways.    */
/* ================================================================== */

/** @brief Per-thread init for coroutine usage (Windows: convert to fiber). */
static inline bool dap_coro_thread_init(void)
{
#ifdef _WIN32
    s_sched_fiber = IsThreadAFiber()
        ? GetCurrentFiber()
        : ConvertThreadToFiber(NULL);
    return s_sched_fiber != NULL;
#else
    return true;
#endif
}

/** @brief Per-thread cleanup (Windows: revert fiber to thread). */
static inline void dap_coro_thread_fini(void)
{
#ifdef _WIN32
    if (s_sched_fiber) {
        ConvertFiberToThread();
        s_sched_fiber = NULL;
    }
#endif
}

/* ================================================================== */
/*  Pool — region-based, lazy commit, freelist reuse                    */
/*                                                                     */
/*  POSIX slot layout (low → high):                                    */
/*    [guard_size] [stack_usable] [dap_coro_t DAP_CORO_META_SIZE]      */
/*    Stack grows from just below dap_coro_t toward the guard page.    */
/*                                                                     */
/*  Windows slot layout:                                               */
/*    [dap_coro_t DAP_CORO_META_SIZE]                                  */
/*    Fiber stacks managed by OS via CreateFiberEx.                    */
/* ================================================================== */

typedef struct dap_coro_pool {
    char        *base;              /* start of coro zone */
    size_t       zone_size;
    size_t       slot_size;         /* POSIX: guard+stack+meta; Win: meta only */
    size_t       guard_size;        /* 0 on Windows */
    size_t       stack_usable;      /* POSIX: in-slot stack; Win: fiber reserve */
    unsigned     capacity;          /* total slots that fit */
    unsigned     n_committed;       /* bump: virgin slots activated */
    unsigned     n_alive;           /* currently running/suspended */
    bool         owns_region;       /* true → free region on fini */
    dap_coro_t  *free_list;         /* recycled slots */
} dap_coro_pool_t;

/** @brief Initialize a coroutine pool from a caller-provided memory region. */
static inline bool
dap_coro_pool_init(dap_coro_pool_t *a_pool, void *a_region,
                   size_t a_region_size, size_t a_stack_size)
{
    *a_pool = (dap_coro_pool_t){0};
    size_t l_stack = a_stack_size ? a_stack_size : DAP_CORO_STACK_DEFAULT;

#ifdef _WIN32
    unsigned l_cap = (unsigned)(a_region_size / DAP_CORO_META_SIZE);
    if (!l_cap) return false;
    a_pool->base         = (char *)a_region;
    a_pool->zone_size    = a_region_size;
    a_pool->slot_size    = DAP_CORO_META_SIZE;
    a_pool->stack_usable = l_stack;
    a_pool->capacity     = l_cap;
#else
    long l_pgsz = sysconf(_SC_PAGESIZE);

    uintptr_t l_raw     = (uintptr_t)a_region;
    uintptr_t l_aligned = (l_raw + (uintptr_t)l_pgsz - 1)
                          & ~((uintptr_t)l_pgsz - 1);
    size_t l_skip = l_aligned - l_raw;
    if (l_skip >= a_region_size)
        return false;

    size_t l_avail = a_region_size - l_skip;
    size_t l_guard = (size_t)l_pgsz;
    l_stack = (l_stack + (size_t)l_pgsz - 1) & ~((size_t)l_pgsz - 1);
    size_t l_slot  = l_guard + l_stack;
    unsigned l_cap = (unsigned)(l_avail / l_slot);
    if (!l_cap) return false;

    a_pool->base         = (char *)l_aligned;
    a_pool->zone_size    = l_avail;
    a_pool->slot_size    = l_slot;
    a_pool->guard_size   = l_guard;
    a_pool->stack_usable = l_stack - DAP_CORO_META_SIZE;
    a_pool->capacity     = l_cap;
#endif
    return true;
}

/** @brief Allocate a standalone memory region (mmap/VirtualAlloc) and init the pool. */
static inline bool
dap_coro_pool_mmap(dap_coro_pool_t *a_pool, unsigned a_max_slots,
                   size_t a_stack_size)
{
#ifdef _WIN32
    size_t l_zone = (size_t)a_max_slots * DAP_CORO_META_SIZE;
    void *l_region = VirtualAlloc(NULL, l_zone,
                                  MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!l_region) return false;
#else
    long l_pgsz = sysconf(_SC_PAGESIZE);
    size_t l_guard = (size_t)l_pgsz;
    size_t l_stack = a_stack_size ? a_stack_size : DAP_CORO_STACK_DEFAULT;
    l_stack = (l_stack + (size_t)l_pgsz - 1) & ~((size_t)l_pgsz - 1);
    size_t l_zone = (size_t)a_max_slots * (l_guard + l_stack);

    void *l_region = mmap(NULL, l_zone, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (l_region == MAP_FAILED) return false;
#endif
    if (!dap_coro_pool_init(a_pool, l_region, l_zone, a_stack_size)) {
#ifdef _WIN32
        VirtualFree(l_region, 0, MEM_RELEASE);
#else
        munmap(l_region, l_zone);
#endif
        return false;
    }
    a_pool->owns_region = true;
    return true;
}

/** @brief Release the pool: destroy fibers (Windows), unmap region if owned. */
static inline void
dap_coro_pool_fini(dap_coro_pool_t *a_pool)
{
#ifdef _WIN32
    for (unsigned i = 0; i < a_pool->n_committed; ++i) {
        dap_coro_t *l_co = (dap_coro_t *)(a_pool->base
                            + (size_t)i * a_pool->slot_size);
        if (l_co->fiber) DeleteFiber(l_co->fiber);
    }
    if (a_pool->owns_region && a_pool->base)
        VirtualFree(a_pool->base, 0, MEM_RELEASE);
#else
    if (a_pool->owns_region && a_pool->base)
        munmap(a_pool->base, a_pool->zone_size);
#endif
    *a_pool = (dap_coro_pool_t){0};
}

/**
 * @brief Commit the next virgin pool slot (lazy activation).
 *
 * Posix: sets a guard page and places dap_coro_t at the top of the stack.
 * Windows: zeroes the metadata (fiber stack is managed by the OS).
 */
static inline dap_coro_t *s_coro_commit(dap_coro_pool_t *a_pool)
{
    if (a_pool->n_committed >= a_pool->capacity) return NULL;
#ifdef _WIN32
    dap_coro_t *l_co = (dap_coro_t *)(a_pool->base
                        + (size_t)a_pool->n_committed * a_pool->slot_size);
    memset(l_co, 0, sizeof(*l_co));
#else
    char *l_slot = a_pool->base + (size_t)a_pool->n_committed * a_pool->slot_size;
    if (mprotect(l_slot, a_pool->guard_size, PROT_NONE) != 0)
        return NULL;
    char *l_stack = l_slot + a_pool->guard_size;
    dap_coro_t *l_co = (dap_coro_t *)(l_stack + a_pool->stack_usable);
    memset(l_co, 0, sizeof(*l_co));
    l_co->stack_base = l_stack;
#endif
    ++a_pool->n_committed;
    return l_co;
}

/** @brief Acquire a slot: try the freelist first, then commit a fresh one. */
static inline dap_coro_t *s_coro_acquire(dap_coro_pool_t *a_pool)
{
    dap_coro_t *l_co = a_pool->free_list;
    if (l_co) {
        a_pool->free_list = l_co->next;
        l_co->next = NULL;
    } else {
        l_co = s_coro_commit(a_pool);
    }
    if (l_co) ++a_pool->n_alive;
    return l_co;
}

/**
 * @brief Return a slot to the freelist.
 *
 * Posix: MADV_DONTNEED reclaims physical pages (re-fault on next use).
 * Windows: DeleteFiber releases the OS fiber and its stack.
 */
static inline void
s_coro_release(dap_coro_pool_t *a_pool, dap_coro_t *a_co)
{
    --a_pool->n_alive;
#ifdef _WIN32
    if (a_co->fiber) {
        DeleteFiber(a_co->fiber);
        a_co->fiber = NULL;
    }
#else
    size_t l_safe = a_pool->stack_usable & ~(a_pool->guard_size - 1);
    if (l_safe)
        madvise(a_co->stack_base, l_safe, MADV_DONTNEED);
#endif
    a_co->next = a_pool->free_list;
    a_pool->free_list = a_co;
}

/* ================================================================== */
/*  Coroutine lifecycle                                                 */
/* ================================================================== */

/** @brief Create a stackful coroutine: acquire a slot and set up its initial stack frame. */
static inline dap_coro_t *
dap_coro_create(dap_coro_pool_t *a_pool, dap_coro_func_t a_func, void *a_arg)
{
    dap_coro_t *l_co = s_coro_acquire(a_pool);
    if (!l_co) return NULL;

    l_co->func  = a_func;
    l_co->arg   = a_arg;
    l_co->state = DAP_CORO_READY;

#ifdef _WIN32
    l_co->fiber = CreateFiberEx(0, a_pool->stack_usable, 0,
                                s_coro_fiber_entry, l_co);
    if (!l_co->fiber) {
        s_coro_release(a_pool, l_co);
        return NULL;
    }
#else
    char *l_top = l_co->stack_base + a_pool->stack_usable;

    /* Place s_coro_entry address onto the synthetic stack so that the
       ASM context switch "returns" into it on first resume.
       memcpy avoids the ISO C forbidden function-to-object pointer cast
       while producing identical codegen (optimized to a register move). */
    void (*l_fn)(void) = s_coro_entry;
    void *l_entry;
    _Static_assert(sizeof(l_fn) == sizeof(l_entry),
                   "function and data pointers must be same size");
    memcpy(&l_entry, &l_fn, sizeof(l_entry));

#if defined(__x86_64__) || defined(_M_X64)
    void **l_sp = (void **)((uintptr_t)l_top & ~(uintptr_t)15);
    *(--l_sp) = NULL;
    *(--l_sp) = l_entry;
    l_sp -= DAP_CORO_NREGS;
    memset(l_sp, 0, DAP_CORO_NREGS * sizeof(void *));
#elif defined(__aarch64__)
    void **l_sp = (void **)((uintptr_t)l_top & ~(uintptr_t)15);
    l_sp -= DAP_CORO_NREGS;
    memset(l_sp, 0, DAP_CORO_NREGS * sizeof(void *));
    l_sp[11] = l_entry;
#endif

    l_co->sp = l_sp;
#endif /* _WIN32 */
    return l_co;
}

/** @brief Resume a coroutine: switch from the scheduler context to the coroutine. */
static inline void dap_coro_resume(dap_coro_t *a_co)
{
    a_co->state = DAP_CORO_RUNNING;
    s_coro_current = a_co;
#ifdef _WIN32
    SwitchToFiber(a_co->fiber);
#else
    dap_coro_switch(&s_sched_sp, a_co->sp);
#endif
}

/** @brief Voluntarily suspend the running coroutine (re-queued as runnable). */
static inline void dap_coro_yield(void)
{
    dap_coro_t *l_co = s_coro_current;
    l_co->state = DAP_CORO_SUSPENDED;
#ifdef _WIN32
    SwitchToFiber(s_sched_fiber);
#else
    dap_coro_switch(&l_co->sp, s_sched_sp);
#endif
}

/** @brief Block the running coroutine until an explicit wake event. */
static inline void dap_coro_wait(void)
{
    dap_coro_t *l_co = s_coro_current;
    l_co->state = DAP_CORO_WAITING;
#ifdef _WIN32
    SwitchToFiber(s_sched_fiber);
#else
    dap_coro_switch(&l_co->sp, s_sched_sp);
#endif
}

/** @brief Mark the coroutine as done and release its slot back to the pool. */
static inline void
dap_coro_destroy(dap_coro_pool_t *a_pool, dap_coro_t *a_co)
{
    a_co->state = DAP_CORO_DONE;
    a_co->func = NULL;
    a_co->arg = NULL;
    s_coro_release(a_pool, a_co);
}

/** @brief True if the coroutine has not yet finished (ready, running, suspended, or waiting). */
static inline bool dap_coro_alive(dap_coro_t *a_co)
{
    return a_co->state >= DAP_CORO_READY && a_co->state <= DAP_CORO_WAITING;
}

/** @brief True if the coroutine can be resumed on the next scheduler tick. */
static inline bool dap_coro_runnable(dap_coro_t *a_co)
{
    return a_co->state == DAP_CORO_READY || a_co->state == DAP_CORO_SUSPENDED;
}

/** @brief Processor-private coroutine scheduler: run carousel + wait queue. */
typedef struct dap_coro_sched {
    dap_coro_t *run_head;       /* SUSPENDED — resume on next tick */
    dap_coro_t *run_tail;
    dap_coro_t *wait_head;      /* WAITING — don't touch */
    unsigned    n_run;
    unsigned    n_wait;
} dap_coro_sched_t;

/** @brief Zero-initialize the scheduler. */
static inline void dap_coro_sched_init(dap_coro_sched_t *a_s)
{
    *a_s = (dap_coro_sched_t){0};
}

/** @brief Route a coroutine into the correct list based on its state after resume. */
static inline void
dap_coro_sched_put(dap_coro_sched_t *a_s, dap_coro_t *a_co)
{
    a_co->next = NULL;
    if (a_co->state == DAP_CORO_SUSPENDED || a_co->state == DAP_CORO_READY) {
        if (a_s->run_tail)
            a_s->run_tail->next = a_co;
        else
            a_s->run_head = a_co;
        a_s->run_tail = a_co;
        ++a_s->n_run;
    } else if (a_co->state == DAP_CORO_WAITING) {
        a_co->next = a_s->wait_head;
        a_s->wait_head = a_co;
        ++a_s->n_wait;
    }
}

/** @brief Pop the next runnable coroutine (null if the carousel is empty). */
static inline dap_coro_t *
dap_coro_sched_next(dap_coro_sched_t *a_s)
{
    dap_coro_t *l_co = a_s->run_head;
    if (!l_co) return NULL;
    a_s->run_head = l_co->next;
    if (!a_s->run_head) a_s->run_tail = NULL;
    l_co->next = NULL;
    --a_s->n_run;
    return l_co;
}

/** @brief Move a waiting coroutine to the run list (called on external event). */
static inline void
dap_coro_sched_wake(dap_coro_sched_t *a_s, dap_coro_t *a_co)
{
    if (a_co->state != DAP_CORO_WAITING) return;
    dap_coro_t **l_pp = &a_s->wait_head;
    while (*l_pp && *l_pp != a_co) l_pp = &(*l_pp)->next;
    if (!*l_pp) return;
    *l_pp = a_co->next;
    --a_s->n_wait;
    a_co->state = DAP_CORO_SUSPENDED;
    a_co->next = NULL;
    if (a_s->run_tail)
        a_s->run_tail->next = a_co;
    else
        a_s->run_head = a_co;
    a_s->run_tail = a_co;
    ++a_s->n_run;
}

/** @brief Remove a coroutine from whichever scheduler list it occupies (call before destroy). */
static inline void
dap_coro_sched_remove(dap_coro_sched_t *a_s, dap_coro_t *a_co)
{
    if (a_co->state == DAP_CORO_WAITING) {
        dap_coro_t **l_pp = &a_s->wait_head;
        while (*l_pp && *l_pp != a_co) l_pp = &(*l_pp)->next;
        if (*l_pp) { *l_pp = a_co->next; --a_s->n_wait; }
    } else {
        dap_coro_t **l_pp = &a_s->run_head;
        while (*l_pp && *l_pp != a_co) l_pp = &(*l_pp)->next;
        if (*l_pp) {
            *l_pp = a_co->next;
            --a_s->n_run;
            if (a_s->run_tail == a_co) {
                a_s->run_tail = a_s->run_head;
                if (a_s->run_tail)
                    while (a_s->run_tail->next)
                        a_s->run_tail = a_s->run_tail->next;
            }
        }
    }
    a_co->next = NULL;
}

/* ================================================================== */
/*  Stackless coroutines                                                */
/* ================================================================== */

enum {
    DAP_SL_DONE  = 0,  /* step completed, release slot back to pool */
    DAP_SL_YIELD = 1,  /* more work pending, re-queue in run carousel */
    DAP_SL_WAIT  = 2   /* blocked on wake_key, park in wait list */
};

#define DAP_SL_STATE_MAX  192

typedef int (*dap_sl_step_t)(void *);

/** @brief Stackless coroutine slot (~224 bytes). Processor-private. */
typedef struct dap_sl_coro {
    dap_sl_step_t       step;
    uintptr_t           wake_key;
    struct dap_sl_coro *next;
    _Alignas(16) char   state[DAP_SL_STATE_MAX];
} dap_sl_coro_t;

/** @brief Heap-backed slab pool for stackless coroutines. */
typedef struct dap_sl_pool {
    dap_sl_coro_t *slots;
    unsigned       capacity;
    unsigned       n_alive;
    dap_sl_coro_t *free_list;
} dap_sl_pool_t;

/** @brief Allocate and initialize the stackless pool with the given slot count. */
static inline bool
dap_sl_pool_init(dap_sl_pool_t *a_pool, unsigned a_capacity)
{
    *a_pool = (dap_sl_pool_t){0};
    a_pool->slots = (dap_sl_coro_t *)calloc(a_capacity, sizeof(dap_sl_coro_t));
    if (!a_pool->slots) return false;
    a_pool->capacity = a_capacity;
    for (unsigned i = 0; i < a_capacity; ++i) {
        a_pool->slots[i].next = a_pool->free_list;
        a_pool->free_list = &a_pool->slots[i];
    }
    return true;
}

/** @brief Free the stackless pool and all its slots. */
static inline void
dap_sl_pool_fini(dap_sl_pool_t *a_pool)
{
    free(a_pool->slots);
    *a_pool = (dap_sl_pool_t){0};
}

/** @brief Acquire a stackless slot, binding a step function and an opaque wake key. */
static inline dap_sl_coro_t *
dap_sl_acquire(dap_sl_pool_t *a_pool, dap_sl_step_t a_step, uintptr_t a_wake_key)
{
    dap_sl_coro_t *l_co = a_pool->free_list;
    if (!l_co) return NULL;
    a_pool->free_list = l_co->next;
    l_co->next     = NULL;
    l_co->step     = a_step;
    l_co->wake_key = a_wake_key;
    memset(l_co->state, 0, DAP_SL_STATE_MAX);
    ++a_pool->n_alive;
    return l_co;
}

/** @brief Return a stackless slot to the freelist. */
static inline void
dap_sl_release(dap_sl_pool_t *a_pool, dap_sl_coro_t *a_co)
{
    --a_pool->n_alive;
    a_co->step = NULL;
    a_co->next = a_pool->free_list;
    a_pool->free_list = a_co;
}

/** @brief Stackless scheduler: run carousel + wait queue with key-based wake. */
typedef struct dap_sl_sched {
    dap_sl_coro_t *run_head, *run_tail;
    dap_sl_coro_t *wait_head;
    unsigned       n_run;
    unsigned       n_wait;
} dap_sl_sched_t;

/** @brief Zero-initialize the stackless scheduler. */
static inline void dap_sl_sched_init(dap_sl_sched_t *a_s)
{
    *a_s = (dap_sl_sched_t){0};
}

/** @brief Append a coroutine to the run carousel. */
static inline void
dap_sl_sched_put_run(dap_sl_sched_t *a_s, dap_sl_coro_t *a_co)
{
    a_co->next = NULL;
    if (a_s->run_tail)
        a_s->run_tail->next = a_co;
    else
        a_s->run_head = a_co;
    a_s->run_tail = a_co;
    ++a_s->n_run;
}

/** @brief Park a coroutine in the wait queue. */
static inline void
dap_sl_sched_put_wait(dap_sl_sched_t *a_s, dap_sl_coro_t *a_co)
{
    a_co->next = a_s->wait_head;
    a_s->wait_head = a_co;
    ++a_s->n_wait;
}

/** @brief Pop the next runnable stackless coroutine (null if carousel empty). */
static inline dap_sl_coro_t *
dap_sl_sched_next(dap_sl_sched_t *a_s)
{
    dap_sl_coro_t *l_co = a_s->run_head;
    if (!l_co) return NULL;
    a_s->run_head = l_co->next;
    if (!a_s->run_head) a_s->run_tail = NULL;
    l_co->next = NULL;
    --a_s->n_run;
    return l_co;
}

/** @brief Wake the first coroutine whose wake_key matches @a a_key (null if none). */
static inline dap_sl_coro_t *
dap_sl_sched_wake_key(dap_sl_sched_t *a_s, uintptr_t a_key)
{
    dap_sl_coro_t **l_pp = &a_s->wait_head;
    while (*l_pp) {
        if ((*l_pp)->wake_key == a_key) {
            dap_sl_coro_t *l_co = *l_pp;
            *l_pp = l_co->next;
            --a_s->n_wait;
            dap_sl_sched_put_run(a_s, l_co);
            return l_co;
        }
        l_pp = &(*l_pp)->next;
    }
    return NULL;
}

/** @brief Wake all coroutines matching @a a_key; return the count woken. */
static inline unsigned
dap_sl_sched_wake_all(dap_sl_sched_t *a_s, uintptr_t a_key)
{
    unsigned l_n = 0;
    dap_sl_coro_t **l_pp = &a_s->wait_head;
    while (*l_pp) {
        if ((*l_pp)->wake_key == a_key) {
            dap_sl_coro_t *l_co = *l_pp;
            *l_pp = l_co->next;
            --a_s->n_wait;
            dap_sl_sched_put_run(a_s, l_co);
            ++l_n;
        } else {
            l_pp = &(*l_pp)->next;
        }
    }
    return l_n;
}

/* ================================================================== */
/*  Helpers                                                             */
/* ================================================================== */

/** @brief Calculate the tail reserve needed for MPSC create_ex to host coroutine slots. */
static inline size_t
dap_coro_reserve(unsigned a_max_slots, size_t a_stack_size)
{
#ifdef _WIN32
    (void)a_stack_size;
    return (size_t)a_max_slots * DAP_CORO_META_SIZE;
#else
    long l_pgsz = sysconf(_SC_PAGESIZE);
    size_t l_guard = (size_t)l_pgsz;
    size_t l_stack = a_stack_size ? a_stack_size : DAP_CORO_STACK_DEFAULT;
    l_stack = (l_stack + (size_t)l_pgsz - 1) & ~((size_t)l_pgsz - 1);
    return (size_t)a_max_slots * (l_guard + l_stack);
#endif
}
