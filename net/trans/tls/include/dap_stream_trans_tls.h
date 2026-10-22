/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * TLS Mimicry Transport for DAP Stream
 *
 * Provides a transport layer that mimics TLS 1.3 on the wire.
 * DPI systems see a valid TLS 1.3 handshake and Application Data
 * records. No real TLS crypto is performed -- the DAP stream layer
 * with DSHP handshake and dap_enc runs on top of this transport.
 *
 * Network stack:
 *   1. TCP connect
 *   2. TLS mimicry handshake (fake ClientHello / ServerHello)
 *   3. DSHP handshake (inside "TLS tunnel")
 *   4. DAP stream packets encrypted by dap_enc
 *
 * @see dap_tls_mimicry.h  -- underlying mimicry engine
 * @see dap_net_trans.h     -- transport abstraction
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dap_net_trans.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dap_stream_trans_tls_config {
    char *sni_hostname;
} dap_stream_trans_tls_config_t;

int  dap_stream_trans_tls_register(void);
int  dap_stream_trans_tls_unregister(void);

dap_stream_trans_tls_config_t dap_stream_trans_tls_config_default(void);

int dap_stream_trans_tls_set_config(dap_net_trans_t *a_trans,
                                    const dap_stream_trans_tls_config_t *a_config);
int dap_stream_trans_tls_get_config(dap_net_trans_t *a_trans,
                                    dap_stream_trans_tls_config_t *a_config);

bool dap_stream_trans_is_tls(const dap_stream_t *a_stream);

/* Optional endpoint trust source (local config now, CDB later).
 * Register before network workers start; callback/context outlive connections.
 * Called on the connection worker; must not block or perform network I/O.
 * Fill NUL-terminated SPKI hex (64 chars) and optional SNI into supplied buffers.
 * Nonzero return rejects the endpoint; no fallback to another trust source. */
typedef int (*dap_tls13_trust_provider_t)(const char *host, uint16_t port,
    char *pin_hex, size_t pin_capacity, char *server_name, size_t name_capacity, void *arg);
void dap_stream_trans_tls13_set_trust_provider(dap_tls13_trust_provider_t provider, void *arg);

#ifdef __cplusplus
}
#endif
