/**
 * @file reality_io_socket.c
 * @brief Blocking-TCP implementation of reality_io_ops_t (tests / CLI use).
 *        Production adapters will bridge to the SDK event loop instead.
 */

#include "reality/reality.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <net/if.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    int fd;
} sock_user_t;

static int s_read(void *u, uint8_t *buf, size_t len, size_t *out_len)
{
    sock_user_t *su = (sock_user_t *)u;
    ssize_t n = recv(su->fd, buf, len, 0);
    if (n > 0) { *out_len = (size_t)n; return 0; }
    if (n == 0) { *out_len = 0; return 0; } /* EOF */
    if (errno == EINTR) { *out_len = 0; return REALITY_ERR_AGAIN; }
    if (errno == EAGAIN || errno == EWOULDBLOCK) { *out_len = 0; return REALITY_ERR_AGAIN; }
    return REALITY_ERR_IO;
}

static int s_write(void *u, const uint8_t *buf, size_t len)
{
    sock_user_t *su = (sock_user_t *)u;
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(su->fd, buf + off, len - off, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                continue;  /* SNDTIMEO tick / signal: retry until real error */
            return REALITY_ERR_IO;
        }
        off += (size_t)n;
    }
    return 0;
}

static void s_close(void *u)
{
    sock_user_t *su = (sock_user_t *)u;
    if (su->fd >= 0) { close(su->fd); su->fd = -1; }
}

/**
 * Connect TCP to host:port (blocking), allocate an io ops struct.
 * a_iface: optional outgoing interface name (e.g. "en0") to bypass TUN
 *          routes (IP_BOUND_IF on Apple, SO_BINDTODEVICE on Linux); NULL = default.
 * Free with reality_io_socket_free.
 */
reality_io_ops_t *reality_io_socket_connect(const char *host, uint16_t port,
                                            int timeout_sec, const char *a_iface)
{
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return NULL;

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (a_iface && a_iface[0]) {
            unsigned ifidx = if_nametoindex(a_iface);
            if (ifidx) {
#if defined(__APPLE__)
                setsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &ifidx, sizeof(ifidx));
#elif defined(SO_BINDTODEVICE)
                setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, a_iface, strlen(a_iface));
#endif
            }
        }
        struct timeval tv = { timeout_sec > 0 ? timeout_sec : 10, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return NULL;

    sock_user_t *su = (sock_user_t *)calloc(1, sizeof(*su));
    reality_io_ops_t *ops = (reality_io_ops_t *)calloc(1, sizeof(*ops));
    if (!su || !ops) { free(su); free(ops); close(fd); return NULL; }
    su->fd = fd;
    ops->user = su;
    ops->read = s_read;
    ops->write = s_write;
    ops->close = s_close;
    return ops;
}

void reality_io_socket_free(reality_io_ops_t *ops)
{
    if (!ops) return;
    sock_user_t *su = (sock_user_t *)ops->user;
    if (su) { if (su->fd >= 0) close(su->fd); free(su); }
    free(ops);
}

int reality_io_socket_fd(reality_io_ops_t *ops)
{
    if (!ops || !ops->user) return -1;
    return ((sock_user_t *)ops->user)->fd;
}

void reality_io_socket_shutdown(reality_io_ops_t *ops)
{
    if (!ops || !ops->user) return;
    int fd = ((sock_user_t *)ops->user)->fd;
    if (fd >= 0) shutdown(fd, SHUT_RDWR);
}
