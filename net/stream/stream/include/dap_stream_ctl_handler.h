/**
 * @file dap_stream_ctl_handler.h
 * @brief Transport-independent stream control handler
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
 * Process stream_ctl request.
 * Parses URL params (channels, enc_type, enc_key_size, enc_headers),
 * creates stream session, generates encryption key, returns session info.
 *
 * @param a_req  Transport request with url_path, body, key_id set
 * @return 0 on success, negative on error
 */
int dap_stream_ctl_handler_process(dap_trans_request_t *a_req);

#ifdef __cplusplus
}
#endif
