/**
 * @file carrier.h
 * @brief Shared transport-carrier contract: session-agnostic thread+pipe bridge
 *        and prepared-trust contract for TLS-based VPN transports.
 *
 * The carrier layer is protocol-independent glue extracted from the REALITY
 * adapter (see docs/PROTOCOL_INTEGRATION_PLAN_2026-11-14.md, phase 0):
 *   - carrier_bridge runs a BLOCKING protocol session on a dedicated thread
 *     and exposes it as a bidirectional byte pipe + control-event pipe, so an
 *     event-loop consumer (dap_events_socket) never blocks on the protocol.
 *   - carrier_io_ops / carrier_session_ops are dependency-injection ports;
 *     the protocol core stays non-blocking-agnostic and SDK-free.
 *
 * Cross-platform: POSIX (Linux/macOS/iOS/Android) and Windows (MinGW).
 * No SDK dependencies. Error codes are protocol-native: the bridge is told
 * which values mean IO failure / closed / again via carrier_bridge_cfg_t,
 * so each protocol keeps its own code space unchanged.
 *
 * FD ownership: the bridge owns the THREAD-side fds and closes them in
 * carrier_bridge_stop(). The SDK-side fds returned by the getters become the
 * CALLER's responsibility (typically handed to dap_events_socket which closes
 * them on delete); carrier_bridge_stop() does NOT close them.
 */

#ifndef CARRIER_H
#define CARRIER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical carrier codes (used by carrier's own io port and by new
 * transports; existing protocols may pass their own code space in cfg). */
#define CARRIER_OK            0
#define CARRIER_ERR_PARAM   (-1)
#define CARRIER_ERR_IO      (-2)
#define CARRIER_ERR_CLOSED  (-3)
#define CARRIER_ERR_AGAIN   (-4)
#define CARRIER_ERR_VERIFY  (-5)
#define CARRIER_ERR_NOMEM   (-6)

#define CARRIER_HOST_MAX 256

/* ------------------------------------------------------------------ */
/* IO port: blocking byte transport (typically TCP).                   */
/* read returns 0 with *out_len=0 on EOF, err_again on timeout/EAGAIN, */
/* err_io on failure. write returns 0 on success.                      */
/* ------------------------------------------------------------------ */
typedef struct carrier_io_ops {
    void *user;
    int  (*read)(void *a_user, uint8_t *a_buf, size_t a_len, size_t *a_out_len);
    int  (*write)(void *a_user, const uint8_t *a_buf, size_t a_len);
    void (*close)(void *a_user);
    /** Pollable underlying fd (-1 if none). Used by the pump's poll loop. */
    int  (*fd)(void *a_user);
    /** Interrupt blocking IO (shutdown SHUT_RDWR); fd stays valid. */
    void (*shutdown)(void *a_user);
} carrier_io_ops_t;

/* ------------------------------------------------------------------ */
/* Session port: the protocol session established over a connected io. */
/* connect returns an opaque session handle or NULL on failure.        */
/* read/write/has_pending/close operate on that handle. read returns   */
/* protocol-native codes; cfg.err_again means "no data now, keep alive".*/
/* ------------------------------------------------------------------ */
typedef struct carrier_session_ops {
    void *user;
    void *(*connect)(void *a_user, carrier_io_ops_t *a_io);
    int   (*read)(void *a_user, void *a_session, uint8_t *a_buf, size_t a_len, size_t *a_out_len);
    int   (*write)(void *a_user, void *a_session, const uint8_t *a_buf, size_t a_len);
    bool  (*has_pending)(void *a_user, void *a_session);
    void  (*close)(void *a_user, void *a_session);
} carrier_session_ops_t;

/* ------------------------------------------------------------------ */
/* Bridge                                                              */
/* ------------------------------------------------------------------ */
typedef struct carrier_bridge_cfg {
    char     host[CARRIER_HOST_MAX];
    uint16_t port;
    int      timeout_sec;                 /* connect/IO timeouts, 0 = default */
    char     iface[32];                   /* optional outgoing interface bind */

    /** IO factory (called on the pump thread). Returns heap carrier_io_ops_t;
     *  the bridge calls ops->close(ops->user) then frees the ops struct. */
    carrier_io_ops_t *(*io_connect)(void *a_user, const char *a_host, uint16_t a_port,
                                    int a_timeout_sec, const char *a_iface);
    void *io_user;

    /** Protocol session factory + operations. */
    carrier_session_ops_t session;

    /* Protocol-native error code space (bridge is code-agnostic): */
    int err_io;       /* IO failure code (e.g. REALITY_ERR_IO / CARRIER_ERR_IO) */
    int err_closed;   /* graceful-close code */
    int err_again;    /* "no data now" code (read timeout/EAGAIN) */
} carrier_bridge_cfg_t;

typedef struct carrier_bridge carrier_bridge_t;

/** Start the bridge (non-blocking: connect + handshake run on the thread). */
carrier_bridge_t *carrier_bridge_start(const carrier_bridge_cfg_t *a_cfg);

/**
 * Variant: pump between an EXTERNAL fd (ownership transferred, closed by the
 * bridge) and the session. No data pipe is created; carrier_bridge_data_fd()
 * returns -1. Used by local TCP forwarders.
 */
carrier_bridge_t *carrier_bridge_start_fd(const carrier_bridge_cfg_t *a_cfg, int a_ext_fd);

/** Block until the pump thread exits (session closed/error/stopped). */
void carrier_bridge_wait(carrier_bridge_t *a_b);

/** SDK-side pipe fd (byte stream, both directions). Caller owns after first call. */
int carrier_bridge_data_fd(carrier_bridge_t *a_b);
/** SDK-side control fd: single-byte events 'R'/'E'/'C'; after 'E' one more byte
 *  carries the protocol error code. Caller owns after first call. */
int carrier_bridge_ctrl_fd(carrier_bridge_t *a_b);

/** 0 = connecting, 1 = ready, 2 = closed by peer, negative = protocol error. */
int carrier_bridge_state(carrier_bridge_t *a_b);

/** Signal the pump to stop (non-blocking). Safe from another thread. */
void carrier_bridge_signal_stop(carrier_bridge_t *a_b);

/** Signal, join the thread, close thread-side fds. Does NOT close SDK-side fds. */
void carrier_bridge_stop(carrier_bridge_t *a_b);

/* ------------------------------------------------------------------ */
/* Default portable IO port (blocking TCP) and pipe helpers.           */
/* ------------------------------------------------------------------ */
carrier_io_ops_t *carrier_io_socket_connect(const char *a_host, uint16_t a_port,
                                            int a_timeout_sec, const char *a_iface);
void              carrier_io_socket_free(carrier_io_ops_t *a_ops);
int               carrier_io_socket_fd(carrier_io_ops_t *a_ops);
void              carrier_io_socket_shutdown(carrier_io_ops_t *a_ops);

/** Create a bidirectional byte pipe: fds[0] <-> fds[1]. 0 on success.
 *  POSIX: socketpair; Windows: TCP loopback pair. */
int  carrier_pipe_pair(int a_fds[2]);
/** Close one pipe end (portable). */
void carrier_pipe_close(int a_fd);

#ifdef __cplusplus
}
#endif

#endif /* CARRIER_H */