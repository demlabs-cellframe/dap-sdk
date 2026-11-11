/*
 * TLS13 (0x08) / WSS (0x09) client transport registration.
 * Independent of the tls_mimicry engine; built when DAP_TLS13_OPENSSL.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int dap_net_trans_tls13_register(void);
int dap_net_trans_tls13_unregister(void);

#ifdef __cplusplus
}
#endif
