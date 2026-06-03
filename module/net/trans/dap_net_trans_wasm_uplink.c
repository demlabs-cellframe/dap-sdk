/*
 * WASM uplink URL prefix (e.g. /relay behind same-origin nginx).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifdef __EMSCRIPTEN__

#include <stdio.h>
#include <string.h>
#include "dap_net_trans_wasm_uplink.h"

static char s_uplink_prefix[128] = "";

void dap_net_trans_wasm_set_uplink_prefix(const char *a_prefix)
{
    s_uplink_prefix[0] = '\0';
    if (!a_prefix || !a_prefix[0])
        return;
    size_t l_len = strlen(a_prefix);
    if (l_len >= sizeof(s_uplink_prefix))
        l_len = sizeof(s_uplink_prefix) - 1;
    memcpy(s_uplink_prefix, a_prefix, l_len);
    s_uplink_prefix[l_len] = '\0';
    if (s_uplink_prefix[0] != '/') {
        memmove(s_uplink_prefix + 1, s_uplink_prefix, l_len + 1);
        s_uplink_prefix[0] = '/';
        l_len++;
    }
    while (l_len > 1 && s_uplink_prefix[l_len - 1] == '/')
        s_uplink_prefix[--l_len] = '\0';
}

const char *dap_net_trans_wasm_uplink_prefix(void)
{
    return s_uplink_prefix;
}

int dap_net_trans_wasm_format_uplink_url(char *a_buf, size_t a_size,
                                         const char *a_scheme,
                                         const char *a_host, uint16_t a_port,
                                         const char *a_path_suffix)
{
    if (!a_buf || a_size == 0 || !a_scheme || !a_host || !a_path_suffix)
        return -1;
    const char *l_prefix = s_uplink_prefix;
    if (l_prefix[0])
        return snprintf(a_buf, a_size, "%s://%s:%u%s/%s",
                        a_scheme, a_host, (unsigned)a_port, l_prefix, a_path_suffix);
    return snprintf(a_buf, a_size, "%s://%s:%u/%s",
                    a_scheme, a_host, (unsigned)a_port, a_path_suffix);
}

#endif /* __EMSCRIPTEN__ */
