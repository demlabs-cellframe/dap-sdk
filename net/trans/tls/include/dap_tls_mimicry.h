/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * TLS 1.3 Mimicry Engine
 *
 * Generates wire-compatible TLS 1.3 handshake messages (ClientHello,
 * ServerHello, ChangeCipherSpec, fake EncryptedExtensions) and wraps
 * application data in TLS Application Data record framing.
 *
 * No real cryptographic TLS operations are performed. This layer is
 * purely a transport foundation: DAP stream with its own DSHP handshake
 * and dap_enc encryption runs on top, treating the mimicry layer as a
 * transparent byte pipe wrapped in TLS record framing.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAP_TLS_MIMICRY_RECORD_HDR_SIZE  5
#define DAP_TLS_MIMICRY_MAX_RECORD_PAYLOAD  16384

typedef enum {
    DAP_TLS_MIMICRY_STATE_INIT = 0,
    DAP_TLS_MIMICRY_STATE_CLIENT_HELLO_SENT,
    DAP_TLS_MIMICRY_STATE_SERVER_HELLO_RCVD,
    DAP_TLS_MIMICRY_STATE_ESTABLISHED,
} dap_tls_mimicry_state_t;

typedef struct dap_tls_mimicry dap_tls_mimicry_t;

dap_tls_mimicry_t *dap_tls_mimicry_new(bool a_is_server);
void               dap_tls_mimicry_free(dap_tls_mimicry_t *a_m);

void dap_tls_mimicry_set_sni(dap_tls_mimicry_t *a_m, const char *a_hostname);

/**
 * Client: generate a TLS 1.3 ClientHello.
 * Caller must free(*a_out) with DAP_DELETE.
 */
int dap_tls_mimicry_create_client_hello(dap_tls_mimicry_t *a_m,
                                        void **a_out, size_t *a_out_size);

/**
 * Server: consume ClientHello, produce ServerHello + CCS + fake encrypted extensions.
 * Caller must free(*a_response) with DAP_DELETE.
 */
int dap_tls_mimicry_process_client_hello(dap_tls_mimicry_t *a_m,
                                         const void *a_data, size_t a_size,
                                         void **a_response, size_t *a_response_size);

/**
 * Client: consume ServerHello + CCS + encrypted extensions, produce client Finished.
 * Caller must free(*a_response) with DAP_DELETE if *a_response_size > 0.
 */
int dap_tls_mimicry_process_server_hello(dap_tls_mimicry_t *a_m,
                                         const void *a_data, size_t a_size,
                                         void **a_response, size_t *a_response_size);

/**
 * Wrap payload into one or more TLS Application Data records.
 * Caller must free(*a_out) with DAP_DELETE.
 * @return 0 on success, -1 on error
 */
int dap_tls_mimicry_wrap(dap_tls_mimicry_t *a_m,
                         const void *a_data, size_t a_size,
                         void **a_out, size_t *a_out_size);

/**
 * Unwrap TLS Application Data records from raw wire data.
 * *a_consumed = bytes consumed from a_data (may be < a_size if partial record).
 * Caller must free(*a_out) with DAP_DELETE.
 * @return 0 on success, 1 if need more data, -1 on error
 */
int dap_tls_mimicry_unwrap(dap_tls_mimicry_t *a_m,
                           const void *a_data, size_t a_size,
                           void **a_out, size_t *a_out_size,
                           size_t *a_consumed);

dap_tls_mimicry_state_t dap_tls_mimicry_get_state(const dap_tls_mimicry_t *a_m);

#ifdef __cplusplus
}
#endif
