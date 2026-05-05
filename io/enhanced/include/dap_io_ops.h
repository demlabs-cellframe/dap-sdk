/**
 * @file dap_io_ops.h
 * @brief Normal public entry for connection @c open (@ref dap_io_conn_open,
 *  @ref dap_io_conn_open_with_ext_dtor, @ref dap_io_conn_open_cfg), @c ext / recv-context
 *  helpers (including @ref DAP_IO_SPAN_EXT_TYPE), @ref dap_io_send.h references, and
 *  cross-thread timer cancel on @ref dap_io_t.
 *
 *  Baseline stock OLB receive path: @ref dap_io_olb_ext_setup on a @ref dap_io_olb_parser_t,
 *  then @ref dap_io_conn_open with @ref dap_io_rx_bridge. Advanced users may install a custom
 *  @c read_cb instead; they own the parser descriptor and any @ref dap_worker_rx_olb invocation.
 *
 * Split from @ref dap_io.h so the base header stays topology + accessors +
 * create/destroy/shutdown only.
 *
 * @par Public contract (@ref dap_io_conn_open)
 * For @c DAP_IO_SOCK / @c DAP_IO_FILE fds driven by the worker event loop, @a a_fd must
 * already be non-blocking; this path does not set @c O_NONBLOCK / socket equivalents.
 * On success the fd is stored on the connection and managed with it until the documented
 * close/delete path removes it (do not @c close the fd while the connection is active).
 * @a a_ext points at caller-owned memory stored in @c conn->ext. For heap- or pool-backed
 * @a a_ext, prefer @ref dap_io_conn_open_with_ext_dtor on success so @c ext_dtor is set in
 * the same validated open path; otherwise call @ref dap_conn_set_ext_dtor after a successful
 * @ref dap_io_conn_open. Destructors run at slab quarantine drain, not on close
 * (see @ref dap_conn.h). Stock OLB ingest
 * with @ref dap_io_rx_bridge requires @c ext to begin with @ref dap_io_olb_ext_t,
 * non-NULL @c parser and non-NULL @c parser->parse (@ref dap_io_olb_ext_is_ready). A custom
 * @a a_read_cb may use a different @c ext layout but is responsible for calling
 * @ref dap_worker_rx_olb with the same @c parser and pull arguments when OLB ingest
 * is desired. Recv/send OLB capacities use @ref DAP_IO_OLB_MIN_CAP as a hard minimum (default
 * @c 1 MiB unless overridden at compile time); a smaller @a a_olb_cap is raised. When the
 * slab is busy, adaptive sizing may lower the effective cap, never below @ref DAP_IO_OLB_MIN_CAP.
 *
 * The base @ref dap_conn_t does not store a transport "kind" or a @c dap_rx_pull_fn.
 * That information is held in user-allocated @c conn->ext (or supplied when opening).
 * The worker event loop only calls @c read_cb; it does not switch on @c dap_io_kind_t.
 *
 * @par Connection setup and protocol @c ext
 * Treat @c ext as your protocol state block. The receive side needs a
 * @ref dap_io_rx_ctx_t (byte source: @c pull and optional @c pull_ctx) at the
 * start of the block, or embedded as the first field of @ref dap_io_olb_ext_t.
 * The stock OLB ingest path also needs @ref dap_io_olb_ext_t: advanced users call
 * @ref dap_io_olb_ext_setup with a @ref dap_io_olb_parse_fn;
 * normal users should prefer @ref dap_io_span_parser_setup so user code is @ref dap_io_parse_cb_t only.
 * Initialise protocol fields (parser structs, stream chunk size, etc.) before @c open.
 * @ref dap_io_olb_ext_setup / @ref dap_io_span_parser_setup wire parser state;
 * @ref DAP_IO_OLB_EXT_FIRST / @ref DAP_IO_RX_CTX_FIRST (via @ref DAP_IO_FIELD_FIRST)
 * assert a leading layout at offset 0.
 *
 * @par Stock receive path
 * 1. Build @c ext with @ref dap_io_olb_ext_t as its first field.
 * 2. Wire @ref dap_io_olb_ext_setup (or @ref dap_io_span_parser_setup) with a @ref dap_io_olb_parser_t
 *    storage block, or define a struct with @ref DAP_IO_SPAN_EXT_TYPE and @ref DAP_IO_SPAN_EXT_INIT.
 * 3. Call @ref dap_io_conn_open or @ref dap_io_conn_open_cfg with the desired @ref dap_io_kind_t, @c a_rx set to
 *    @c NULL (to initialise the @ref dap_io_rx_ctx_t prefix of @a a_ext from @a a_kind) or
 *    to an explicit @c dap_io_rx_ctx_t* when reusing a pooled slice,
 *    @a a_read_cb = @ref dap_io_rx_bridge, and @a a_ext pointing at your block.
 * 4. At runtime the worker runs @ref dap_io_rx_bridge, which calls
 *    @ref dap_worker_rx_olb with the stored @c pull and callbacks.
 *
 * @par Custom @c read_cb
 * Pass your own @a a_read_cb when you need a state machine or logic outside the
 * @ref dap_io_olb_ext_t shape. @ref dap_io_conn_open still assigns @c pull / @c pull_ctx from
 * @a a_kind on the selected @ref dap_io_rx_ctx_t (@a a_rx or @a a_ext prefix). If @a a_rx is
 * @c NULL and @a a_ext is non-@c NULL, that prefix is the first sizeof(@ref dap_io_rx_ctx_t)
 * bytes of @a a_ext; layouts that do not start with @ref dap_io_rx_ctx_t (or
 * @ref dap_io_olb_ext_t, which embeds it) must supply an explicit @a a_rx or keep @a a_ext
 * @c NULL for open. Inside your callback, call @ref dap_worker_rx_olb with the same @c pull,
 * @c pull_ctx, and @c parser you would have wired into @ref dap_io_olb_ext_t.
 *
 * @par Send path
 * Cross-thread and owner-thread writes go through @c dap_io_tx_send and
 * @c dap_io_tx_send_direct (inlines in @c dap_io_send.h). They enqueue into
 * the connection send OLB; the owner drains with @ref dap_worker_tx_flush on
 * EPOLLOUT, pending rescan, etc. No second public name: these are the only
 * user-facing send entry points for that pipeline.
 *
 * @par Examples
 * Normal stock OLB+span parse: define an @c ext with @ref DAP_IO_SPAN_EXT_TYPE,
 * initialize it with @ref DAP_IO_SPAN_EXT_INIT, then open with @ref dap_io_rx_bridge.
 * @code{.c}
 * DAP_IO_SPAN_EXT_TYPE(my_ext_t, size_t max_frame;);
 * static dap_io_parse_result_t my_parse(const char *data, size_t n, void *arg) {
 *     (void)data; (void)arg; return (dap_io_parse_result_t){ n, 0 };
 * }
 * my_ext_t *e = calloc(1, sizeof *e);
 * DAP_IO_SPAN_EXT_INIT_SELF(e, my_parse, NULL);
 * dap_io_conn_cfg_t cfg = DAP_IO_CONN_CFG_INIT;
 * cfg.io = io; cfg.worker_id = wid; cfg.kind = DAP_IO_SOCK; cfg.fd = fd;
 * cfg.olb_cap = olb_cap; cfg.read_cb = dap_io_rx_bridge; cfg.ext = e;
 * dap_conn_t *c = dap_io_conn_open_cfg(&cfg);
 * @endcode
 * Advanced custom @c read_cb: call @ref dap_worker_rx_olb with @c e->olb.parser; open with @c my_read instead of @ref dap_io_rx_bridge.
 * @code{.c}
 * static void my_read(dap_conn_t *a_c) {
 *     my_ext_t *e = a_c->ext;
 *     dap_worker_rx_olb(a_c, e->olb.parser, e->olb.rx.pull, e->olb.rx.pull_ctx);
 * }
 * @endcode
 * Send (declared in @c dap_io_send.h):
 * @code{.c}
 * (void)dap_io_tx_send(a_handle, data, len);
 * (void)dap_io_tx_send_direct(a_conn, data, len);
 * @endcode
 *
 * @par See also
 * @ref dap_io_rx_ctx_init for manual re-init, @ref dap_io_timer_cancel_async for
 * async timer cancellation from this header.
 */
#pragma once

#include "dap_io.h"
#include <stddef.h>
#include "dap_io_plat.h"

/** @brief High-level I/O class for @ref dap_io_conn_open (rx pull + taxonomy). */
typedef enum dap_io_kind {
    DAP_IO_SOCK  = 0, /**< stream socket: recv(2) pull */
    DAP_IO_FILE  = 1, /**< regular file / pipe: read(2) pull (POSIX) */
    DAP_IO_TIMER = 2, /**< reserved / taxonomy — not a user @c dap_io_conn_open path */
} dap_io_kind_t;

#define DAP_IO_KIND_COUNT 3

/** @brief First field of @c conn->ext when using @ref dap_io_rx_ctx_init or @ref dap_io_conn_open. */
typedef struct dap_io_rx_ctx {
    dap_rx_pull_fn  pull;     /**< from @ref dap_io_rx_ctx_init */
    void             *pull_ctx; /**< pull-specific; default NULL */
} dap_io_rx_ctx_t;

/**
 * @brief How much of the supplied byte span is a full logical prefix, and a recv size hint
 *  for the next incomplete item (stored into recv OLB by the stock span parser; 0 = no hint).
 */
typedef struct dap_io_parse_result {
    size_t consumed;     /**< Bytes consumed as complete prefix from this span. */
    size_t bytes_needed; /**< Hint for next read when data ends mid-item; 0 if unknown. */
} dap_io_parse_result_t;

/**
 * @brief User-level parse over a byte span and opaque @a a_arg only — no @c dap_conn_t,
 *  OLB, or tail/ack fields.
 */
typedef dap_io_parse_result_t (*dap_io_parse_cb_t)(const char *a_data, size_t a_size, void *a_arg);

typedef struct dap_io_olb_parser dap_io_olb_parser_t;

/**
 * @brief OLB ingest parse step: inspect @c a_conn->olb, advance @c a_parser->tail,
 *  return logical items parsed.
 */
typedef size_t (*dap_io_olb_parse_fn)(dap_conn_t *a_conn, dap_io_olb_parser_t *a_parser);

/** @brief Parser descriptor for @ref dap_worker_rx_olb: tail, parse, optional compact, span fields. */
struct dap_io_olb_parser {
    uint64_t                tail;
    dap_io_olb_parse_fn     parse;
    dap_worker_compact_fn   compact;
    dap_io_parse_cb_t       span_parse;
    void                    *arg;
};

/** @brief Public prefix for @ref dap_io_rx_bridge: @c rx first, then OLB parser pointer. */
typedef struct dap_io_olb_ext {
    dap_io_rx_ctx_t         rx;
    dap_io_olb_parser_t     *parser;
} dap_io_olb_ext_t;

size_t dap_io_olb_parse_span(dap_conn_t *a_conn, dap_io_olb_parser_t *a_parser);

/** Compile-time: @c offsetof(type, field) == 0. */
#define DAP_IO_FIELD_FIRST(type, field) \
    _Static_assert(offsetof(type, field) == 0, #type "." #field " must be the first field")

/** Same as @ref DAP_IO_FIELD_FIRST for a struct whose first member is @ref dap_io_olb_ext_t. */
#define DAP_IO_OLB_EXT_FIRST(type, field) DAP_IO_FIELD_FIRST(type, field)
/** Same as @ref DAP_IO_FIELD_FIRST for a struct whose first member is @ref dap_io_rx_ctx_t. */
#define DAP_IO_RX_CTX_FIRST(type, field)  DAP_IO_FIELD_FIRST(type, field)

/** @brief User @c ext struct: @ref dap_io_olb_ext_t, embedded @ref dap_io_olb_parser_t, then custom fields (see @ref DAP_MSG_TYPE). */
#define DAP_IO_SPAN_EXT_TYPE(name, ...)                                 \
    typedef struct name {                                               \
        dap_io_olb_ext_t     olb;                                       \
        dap_io_olb_parser_t  parser;                                    \
        __VA_ARGS__                                                     \
    } name;                                                             \
    DAP_IO_OLB_EXT_FIRST(name, olb)

#define DAP_IO_SPAN_EXT_INIT(a_ext, a_parse, a_arg, a_compact)          \
    dap_io_span_parser_setup(&(a_ext)->olb, &(a_ext)->parser, (a_parse), (a_arg), (a_compact))

#define DAP_IO_SPAN_EXT_INIT_SELF(a_ext, a_parse, a_compact)            \
    DAP_IO_SPAN_EXT_INIT((a_ext), (a_parse), (a_ext), (a_compact))

/** @brief Wire @a a_ext->parser to @a a_parser with @a a_parse and @a a_compact; clears span fields. */
DAP_STATIC_INLINE bool
dap_io_olb_ext_setup(dap_io_olb_ext_t *a_ext, dap_io_olb_parser_t *a_parser,
                     dap_io_olb_parse_fn a_parse, dap_worker_compact_fn a_compact)
{
    if (!a_ext || !a_parser || !a_parse)
        return false;
    a_parser->tail = 0;
    a_parser->parse = a_parse;
    a_parser->compact = a_compact;
    a_parser->span_parse = NULL;
    a_parser->arg = NULL;
    a_ext->parser = a_parser;
    return true;
}

/** @brief True if @a a_ext and @c parser->parse are non-NULL (ready for @ref dap_io_rx_bridge). */
DAP_STATIC_INLINE bool
dap_io_olb_ext_is_ready(const dap_io_olb_ext_t *a_ext)
{
    return a_ext && a_ext->parser && a_ext->parser->parse;
}

/**
 * @brief Wire span mode on @a a_parser: @c parse is @ref dap_io_olb_parse_span, @c span_parse/@c arg
 *  hold the user callback; @c tail starts at 0.
 */
DAP_STATIC_INLINE bool
dap_io_span_parser_setup(dap_io_olb_ext_t *a_ext, dap_io_olb_parser_t *a_parser,
                        dap_io_parse_cb_t a_user_parse, void *a_arg, dap_worker_compact_fn a_compact)
{
    if (!a_ext || !a_parser || !a_user_parse) return false;
    a_parser->tail = 0;
    a_parser->parse = dap_io_olb_parse_span;
    a_parser->compact = a_compact;
    a_parser->span_parse = a_user_parse;
    a_parser->arg = a_arg;
    a_ext->parser = a_parser;
    return true;
}

void dap_io_rx_ctx_init(dap_io_rx_ctx_t *a_rx, dap_io_kind_t a_kind);

/** @brief Default @c read_cb: @ref dap_worker_rx_olb using @ref dap_io_olb_ext in @c conn->ext. */
void dap_io_rx_bridge(dap_conn_t *a_c);

/**
 * @brief Allocate a connection with OLB pair and add it to the worker's poller.
 *
 * One-call connection setup: slab_alloc + OLB creation + conn_attach +
 * conn_add + pull assignment for the target rx slice + callback assignment +
 * EPOLLIN arm when @a a_read_cb is set.
 *
 * Effective OLB capacity is at least @ref DAP_IO_OLB_MIN_CAP and may be reduced when
 * the connection slab is busy (never below @ref DAP_IO_OLB_MIN_CAP).
 *
 * @param a_io         I/O topology (owns the slab).
 * @param a_worker_id  Worker index (0..n_workers-1).
 * @param a_kind       I/O class — @c DAP_IO_TIMER is invalid (returns NULL);
 *                      @c DAP_IO_FILE unsupported on some platforms (returns NULL).
 * @param a_fd         Socket or file descriptor; must already be non-blocking for event-loop use.
 * @param a_olb_cap    Requested recv/send OLB capacity in bytes; clamped up to @ref DAP_IO_OLB_MIN_CAP,
 *                     then possibly lowered by adaptive sizing (see file-level contract).
 * @param a_rx         If NULL and @a a_ext is set, @c pull / @c pull_ctx are set on the first
 *                     @ref dap_io_rx_ctx_t-sized prefix of @a a_ext from @a a_kind; if non-NULL,
 *                     that pointer is updated instead (e.g. pooled @c &ext->rx). Skipped if both
 *                     are NULL. @a a_kind always defines @c pull / @c pull_ctx. Custom @a a_read_cb
 *                     still runs after this init, so a mismatched @a a_kind is a user bug.
 * @param a_read_cb    Read callback (NULL: arm reads later via dap_worker_conn_arm_read);
 *                     use @ref dap_io_rx_bridge for the stock OLB+parse path when
 *                     @a a_ext begins with @ref dap_io_olb_ext_t.
 * @param a_ext        Stored in @c conn->ext (caller-owned unless cleanup is bound with
 *                     @ref dap_conn_set_ext_dtor; see file-level contract).
 * @param a_max_frame  Max frame size for compact threshold; 0 uses default.
 * @return Connection pointer, or NULL on failure.
 */
#ifndef DAP_IO_OLB_MIN_CAP
/** Minimum recv/send OLB capacity for @ref dap_io_conn_open; default @c Mbytes(1) if unset. */
#  define DAP_IO_OLB_MIN_CAP Mbytes(1)
#endif

dap_conn_t *dap_io_conn_open(dap_io_t *a_io, unsigned a_worker_id, dap_io_kind_t a_kind,
                             dap_fd_t a_fd, size_t a_olb_cap, dap_io_rx_ctx_t *a_rx,
                             dap_conn_read_cb_t a_read_cb, void *a_ext,
                             size_t a_max_frame);

/**
 * @brief Same as @ref dap_io_conn_open, but on full success sets @c conn->ext_dtor to @a a_ext_dtor
 *  when @a a_ext and @a a_ext_dtor are both non-NULL. On any failure, @a a_ext (if non-NULL) is not
 *  owned by the connection — the destructor is not registered and the caller must release it.
 * @a a_ext_dtor may be NULL to match plain @ref dap_io_conn_open.
 */
dap_conn_t *dap_io_conn_open_with_ext_dtor(dap_io_t *a_io, unsigned a_worker_id, dap_io_kind_t a_kind,
                                           dap_fd_t a_fd, size_t a_olb_cap, dap_io_rx_ctx_t *a_rx,
                                           dap_conn_read_cb_t a_read_cb, void *a_ext, size_t a_max_frame,
                                           dap_conn_ext_dtor_t a_ext_dtor);

/** @brief Designated-initializer open parameters; same contract as @ref dap_io_conn_open / @ref dap_io_conn_open_with_ext_dtor. */
typedef struct dap_io_conn_cfg {
    dap_io_t             *io;
    unsigned              worker_id;
    dap_io_kind_t         kind;
    dap_fd_t              fd;
    size_t                olb_cap;
    dap_io_rx_ctx_t      *rx;
    dap_conn_read_cb_t    read_cb;
    void                 *ext;
    size_t                max_frame;
    dap_conn_ext_dtor_t   ext_dtor;
} dap_io_conn_cfg_t;

/** @brief Soft initializer with invalid sentinels for fields that are unsafe to forget. */
#define DAP_IO_CONN_CFG_INIT { \
    .io = NULL, .worker_id = UINT_MAX, .kind = (dap_io_kind_t)DAP_IO_KIND_COUNT, \
    .fd = (dap_fd_t)-1, .olb_cap = 0, .rx = NULL, .read_cb = NULL, \
    .ext = NULL, .max_frame = 0, .ext_dtor = NULL \
}

/** @brief Open with @a a_cfg (NULL @a a_cfg or @c ext_dtor without @c ext returns NULL; @c ext_dtor is success-only, like @ref dap_io_conn_open_with_ext_dtor). */
dap_conn_t *dap_io_conn_open_cfg(const dap_io_conn_cfg_t *a_cfg);

/**
 * @brief Cancel a timer from any thread by its self-routing handle.
 *  (Public API — not called internally; for user timer management.)
 *
 *  Processor timers (@c worker_slot == @c DAP_TIMER_SLOT_PROC) are cancelled
 *  via the processor's WFQ FAST lane.  Worker timers are posted to the
 *  worker's Treiber @c ctrl_stack + eventfd kick.
 *
 *  Returns true if the cancel request was posted, not that the timer
 *  has already been removed — actual deletion is asynchronous.
 */
bool dap_io_timer_cancel_async(dap_io_t *a_io, dap_timer_handle_t a_h);
