#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "dap_enc_ks.h"
/* Per-connection key identity. Zero initialize, never share between peers.
 * Response is malloc-owned even on failure and must be freed by caller. */
typedef struct dap_tls_dap_control {
    char key_id[DAP_ENC_KS_KEY_ID_SIZE + 1];
} dap_tls_dap_control_t;
int dap_tls_dap_control_request(dap_tls_dap_control_t *control, bool enc,
    const void *data, size_t size, void **response, size_t *response_size);
