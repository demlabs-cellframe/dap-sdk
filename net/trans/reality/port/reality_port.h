/**
 * @file reality_port.h
 * @brief Port implementations shipped with the module (OpenSSL crypto,
 *        blocking-socket IO, std time). Production adapters may inject their own.
 */

#ifndef REALITY_PORT_H
#define REALITY_PORT_H

#include "reality/reality.h"

#ifdef __cplusplus
extern "C" {
#endif

const reality_crypto_ops_t *reality_crypto_openssl(void);
const reality_time_ops_t   *reality_time_std(void);

reality_io_ops_t *reality_io_socket_connect(const char *a_host, uint16_t a_port,
                                            int a_timeout_sec, const char *a_iface);
void              reality_io_socket_free(reality_io_ops_t *a_ops);
/** Underlying fd of a socket-port instance (for poll-based pumps); -1 if none. */
int               reality_io_socket_fd(reality_io_ops_t *a_ops);
/** Break blocking reads on the socket (shutdown SHUT_RDWR); fd stays valid. */
void              reality_io_socket_shutdown(reality_io_ops_t *a_ops);

#ifdef __cplusplus
}
#endif

#endif /* REALITY_PORT_H */
