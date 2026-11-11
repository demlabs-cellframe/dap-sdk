/*
 * Shared DAP-over-TLS client builders.
 *
 * The ENC/CTL request builders, the TCP+stream stage_prepare and the endpoint
 * trust hook are transport-agnostic: they are used by the tls_mimicry (0x06),
 * tls13 (0x08) and wss (0x09) client adapters alike. Keeping them here lets each
 * adapter live in its own translation unit and register independently, instead
 * of being #include'd into the mimicry transport.
 *
 * This layer knows the DAP request wire format and the SDK stream/socket API;
 * it owns no TLS engine and no protocol framing (those live in the tls13/wss
 * protocol headers and the mimicry engine respectively).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dap_common.h"
#include "dap_net_trans.h"
#include "dap_stream.h"
#include "dap_events_socket.h"
#include "dap_stream_trans_tls.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dap_tls_mimicry;

/* Client-side protocol phases, processed in order by the single read callback.
 * The state machine is driven by this enum, NOT by swapping read_callback
 * pointers — so there is no window where a server response can arrive with
 * no callback able to receive it. */
typedef enum tls_client_phase {
    TLS_PHASE_HANDSHAKE = 0,       /* waiting for ServerHello */
    TLS_PHASE_ENC_INIT_WAIT,       /* sent enc_init, waiting for response */
    TLS_PHASE_STREAM_CTL_WAIT,     /* sent stream_ctl, waiting for response */
    TLS_PHASE_STREAMING,           /* established — DAP stream packets */
} tls_client_phase_t;

/* Shared DAP-over-TLS client context. The mimicry/sni_hostname fields are used
 * only by the tls_mimicry transport; the tls13/wss adapter uses the DAP subset
 * (handshake/session callbacks, params, phase, tail/reply buffers). */
typedef struct tls_mimicry_ctx {
    struct dap_tls_mimicry *mimicry;
    char              *sni_hostname;
    dap_net_trans_handshake_cb_t handshake_cb;
    dap_net_handshake_params_t  handshake_params;  /* saved for enc_init after TLS handshake */
    dap_net_trans_session_cb_t  session_create_cb; /* callback for stream_ctl response */
    tls_client_phase_t           phase;            /* current protocol phase */
    uint8_t *stream_tail;
    size_t stream_tail_size;
    char *enc_reply;
    size_t enc_reply_size;
} tls_mimicry_ctx_t;

/* Build and send the enc_init HTTP POST through the transport's write path.
 * @return 0 on success, -1 on allocation/send failure. */
int dap_tls_dap_send_enc_init(dap_stream_t *a_stream, tls_mimicry_ctx_t *a_ctx,
                              dap_events_socket_t *a_es);

/* Build and send the stream_ctl HTTP POST through the transport's write path.
 * @return 0 on success, negative on failure. */
int dap_tls_dap_send_session_request(dap_stream_t *a_stream,
                                     dap_net_session_params_t *a_params);

/* Resolve+connect a TCP socket and create the client DAP stream (IPv6/NAT64
 * safe, TCP_NODELAY). Generic for all DAP-over-TLS transports.
 * @return 0 on success, -1 on failure (result->error_code set). */
int dap_tls_dap_stage_prepare(dap_net_trans_t *a_trans,
                              const dap_net_stage_prepare_params_t *a_params,
                              dap_net_stage_prepare_result_t *a_result);

/* Capability mask shared by the DAP-over-TLS client transports. */
uint32_t dap_tls_dap_get_caps(dap_net_trans_t *a_trans);

/* Endpoint trust hook (storage lives here; public setter keeps its legacy name
 * in dap_stream_trans_tls.h). Register before network workers start. */
void dap_tls_dap_set_trust_provider(dap_tls13_trust_provider_t a_provider, void *a_arg);
dap_tls13_trust_provider_t dap_tls_dap_trust_provider(void);
void *dap_tls_dap_trust_arg(void);

/* Debug gate for the builder logs (mirrors [tls] debug_more). */
void dap_tls_dap_set_debug_more(bool a_enabled);

#ifdef __cplusplus
}
#endif
