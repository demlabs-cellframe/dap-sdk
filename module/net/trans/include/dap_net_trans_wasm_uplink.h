/*
 * WASM uplink URL prefix helpers.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __EMSCRIPTEN__

void dap_net_trans_wasm_set_uplink_prefix(const char *a_prefix);
const char *dap_net_trans_wasm_uplink_prefix(void);

/** @a_path_suffix is e.g. "enc_init/gd4y5yh78w42aaagh?enc_type=0,..." */
int dap_net_trans_wasm_format_uplink_url(char *a_buf, size_t a_size,
                                         const char *a_scheme,
                                         const char *a_host, uint16_t a_port,
                                         const char *a_path_suffix);

#else

static inline void dap_net_trans_wasm_set_uplink_prefix(const char *a_prefix)
{
    (void)a_prefix;
}

static inline const char *dap_net_trans_wasm_uplink_prefix(void)
{
    return "";
}

#endif /* __EMSCRIPTEN__ */
