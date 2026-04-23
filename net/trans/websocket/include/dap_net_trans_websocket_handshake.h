#ifndef DAP_NET_TRANS_WEBSOCKET_HANDSHAKE_H
#define DAP_NET_TRANS_WEBSOCKET_HANDSHAKE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build Sec-WebSocket-Accept value from a client Sec-WebSocket-Key (RFC 6455).
 *
 * @param a_client_key Base64 client key from HTTP Upgrade request
 * @param a_accept_key Output buffer for base64 accept key
 * @param a_accept_key_size Output buffer size, must be at least 29 bytes
 * @return 0 on success, negative value on error
 */
int dap_net_trans_websocket_build_accept_key(const char *a_client_key,
                                             char *a_accept_key,
                                             size_t a_accept_key_size);

#ifdef __cplusplus
}
#endif

#endif /* DAP_NET_TRANS_WEBSOCKET_HANDSHAKE_H */
