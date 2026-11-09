/**
 * @file reality_bridge.h
 * @brief Thread + socketpair bridge: runs a blocking reality_session on a
 *        dedicated thread and exposes it as a plain bidirectional byte pipe.
 *
 * Purpose: event-loop integration (dap_events_socket / worker threads) without
 * making the protocol core non-blocking. The consumer wraps data_fd in its
 * event socket; ctrl_fd delivers state events ('R' ready, 'E' error, 'C' closed)
 * so readiness is observable via the same event loop.
 *
 * Pure POSIX (pthread/socketpair/poll) -- no SDK dependencies.
 *
 * FD ownership: the bridge owns the THREAD-side fds and closes them in
 * reality_bridge_stop(). The SDK-side fds returned by the getters become the
 * CALLER's responsibility (typically handed to dap_events_socket which closes
 * them on delete); reality_bridge_stop() does NOT close them.
 */

#ifndef REALITY_BRIDGE_H
#define REALITY_BRIDGE_H

#include "reality/reality.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REALITY_BRIDGE_HOST_MAX 256

typedef struct reality_bridge_cfg {
    reality_config_t rcfg;                     /* sni/pubkey/shortid/uuid/rate cap */
    char     host[REALITY_BRIDGE_HOST_MAX];    /* reality node */
    uint16_t port;
    char     dest_host[REALITY_BRIDGE_HOST_MAX];/* VLESS destination (DAP listener) */
    uint16_t dest_port;
    int      timeout_sec;                      /* connect/IO timeouts, 0 = default */
    char     iface[32];                        /* optional outgoing interface bind */
} reality_bridge_cfg_t;

typedef struct reality_bridge reality_bridge_t;

/** Start the bridge (non-blocking: TCP connect + handshake run on the thread). */
reality_bridge_t *reality_bridge_start(const reality_bridge_cfg_t *a_cfg);

/**
 * Variant: pump between an EXTERNAL fd (ownership transferred, closed by the
 * bridge) and the reality session. No data socketpair is created;
 * reality_bridge_data_fd() returns -1. Used by the local TCP forwarder.
 */
reality_bridge_t *reality_bridge_start_fd(const reality_bridge_cfg_t *a_cfg, int a_ext_fd);

/** Block until the pump thread exits (session closed/error/stopped). */
void reality_bridge_wait(reality_bridge_t *a_b);

/** SDK-side pipe fd (byte stream, both directions). Caller owns after first call. */
int reality_bridge_data_fd(reality_bridge_t *a_b);
/** SDK-side control fd: single-byte events 'R'/'E'/'C'; after 'E' one more byte
 *  carries the negated reality error code. Caller owns after first call. */
int reality_bridge_ctrl_fd(reality_bridge_t *a_b);

/** 0 = connecting, 1 = ready, negative = REALITY_ERR_* failure, 2 = closed by peer. */
int reality_bridge_state(reality_bridge_t *a_b);

/** Signal the pump to stop (non-blocking, thread keeps running until it exits).
 *  Safe to call from another thread while a consumer is inside bridge_wait(). */
void reality_bridge_signal_stop(reality_bridge_t *a_b);

/** Signal the thread, join it, close thread-side fds. Does NOT close SDK-side fds. */
void reality_bridge_stop(reality_bridge_t *a_b);

#ifdef __cplusplus
}
#endif

#endif /* REALITY_BRIDGE_H */
