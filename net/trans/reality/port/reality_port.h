/**
 * @file reality_port.h
 * @brief Port implementations shipped with the module (OpenSSL crypto,
 *        blocking-socket IO, std time). Production adapters may inject their own.
 */

#ifndef REALITY_PORT_H
#define REALITY_PORT_H

#include <sys/socket.h>

#include "reality/reality.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SIGPIPE-safe send helpers (pure POSIX, no SDK dependencies).
 * The pump can write to a pipe whose peer disappeared during teardown; an
 * unhandled SIGPIPE kills the whole process -- fatal inside a Network
 * Extension. Use MSG_NOSIGNAL where available and SO_NOSIGPIPE on Apple. */
#ifdef MSG_NOSIGNAL
#define REALITY_SEND_FLAGS MSG_NOSIGNAL
#else
#define REALITY_SEND_FLAGS 0
#endif

static inline void reality_no_sigpipe(int a_fd)
{
#ifdef SO_NOSIGPIPE
    int l_on = 1;
    (void)setsockopt(a_fd, SOL_SOCKET, SO_NOSIGPIPE, &l_on, sizeof(l_on));
#else
    (void)a_fd;
#endif
}

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
