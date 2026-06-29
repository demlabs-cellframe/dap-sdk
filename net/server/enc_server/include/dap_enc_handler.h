/**
 * @file dap_enc_handler.h
 * @brief Transport-independent encryption handshake handler
 *
 * Called by HTTP adapter and TLS server directly.
 * No HTTP dependency — works with dap_trans_request_t.
 */

#pragma once

#include "dap_trans_request.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Process enc_init request (encryption handshake).
 * Parses query string for enc params, decodes base64 body,
 * calls dap_enc_server_process_request, builds JSON response.
 *
 * @param a_req  Transport request with query_string and body set
 * @return 0 on success, negative on error
 */
int dap_enc_handler_process(dap_trans_request_t *a_req);

#ifdef __cplusplus
}
#endif
