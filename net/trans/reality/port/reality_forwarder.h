/**
 * @file reality_forwarder.h
 * @brief Local TCP forwarder into a REALITY tunnel.
 *
 * Listens on 127.0.0.1:<port>; every accepted TCP connection is tunneled by a
 * dedicated reality bridge (VLESS session to the node, destination fixed by
 * cfg.bridge.dest_host:dest_port -- typically the node's DAP/HTTP endpoint via
 * the Xray front).
 *
 * Product usage (zero FSM/transport changes): point the VPN client's uplink
 * address at 127.0.0.1:<forwarder port> -- the existing HTTP-DAP machinery
 * then rides inside the REALITY tunnel unchanged.
 *
 * Pure POSIX; no SDK dependencies.
 */

#ifndef REALITY_FORWARDER_H
#define REALITY_FORWARDER_H

#include "reality_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct reality_fwd_cfg {
    reality_bridge_cfg_t bridge;   /* tunnel params; dest = endpoint behind the node */
    uint16_t listen_port;          /* 0 = ephemeral */
} reality_fwd_cfg_t;

typedef struct reality_forwarder reality_forwarder_t;

reality_forwarder_t *reality_forwarder_start(const reality_fwd_cfg_t *a_cfg);
uint16_t             reality_forwarder_port(reality_forwarder_t *a_f);
/** Live tunnel count (for diagnostics). */
int                  reality_forwarder_conns(reality_forwarder_t *a_f);
/** Stop: close listener, tear down all bridges, join threads, free. */
void                 reality_forwarder_stop(reality_forwarder_t *a_f);

#ifdef __cplusplus
}
#endif

#endif /* REALITY_FORWARDER_H */
