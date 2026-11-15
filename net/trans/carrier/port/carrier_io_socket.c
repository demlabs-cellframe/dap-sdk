/**
 * @file carrier_io_socket.c
 * @brief Portable blocking-TCP implementation of carrier_io_ops_t.
 *        Generalized from reality_io_socket with Windows support (plan §2.6):
 *        iface bind via IP_BOUND_IF (Apple) / SO_BINDTODEVICE (Linux) /
 *        IP_UNICAST_IF (Windows); timeouts via SO_RCVTIMEO/SO_SNDTIMEO.
 *        No SDK dependencies. Returns canonical CARRIER_ERR_* codes.
 */

#include "carrier/carrier.h"
#include "carrier_port.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#  include <iphlpapi.h>
#else
#  include <sys/types.h>
#  include <netdb.h>
#  include <net/if.h>
#  include <unistd.h>
#endif

typedef struct {
    int fd;
} carrier_sock_user_t;

static int s_read(void *u, uint8_t *buf, size_t len, size_t *out_len)
{
    carrier_sock_user_t *su = (carrier_sock_user_t *)u;
    ssize_t n = recv((carrier_fd_t)su->fd, (char *)buf, (int)len, 0);
    if (n > 0) { *out_len = (size_t)n; return CARRIER_OK; }
    if (n == 0) { *out_len = 0; return CARRIER_OK; } /* EOF */
    int e = carrier_errno();
    if (e == CARRIER_EINTR || e == CARRIER_EAGAIN || e == CARRIER_EWOULDBLOCK) {
        *out_len = 0; return CARRIER_ERR_AGAIN;
    }
    return CARRIER_ERR_IO;
}

static int s_write(void *u, const uint8_t *buf, size_t len)
{
    carrier_sock_user_t *su = (carrier_sock_user_t *)u;
    size_t off = 0;
    int again_tries = 0;
    while (off < len) {
        ssize_t n = send((carrier_fd_t)su->fd, (const char *)buf + off,
                         (int)(len - off), CARRIER_SEND_FLAGS);
        if (n <= 0) {
            int e = carrier_errno();
            if (n < 0 && e == CARRIER_EINTR)
                continue;
            if (n < 0 && (e == CARRIER_EAGAIN || e == CARRIER_EWOULDBLOCK)) {
                /* SNDTIMEO tick: bounded retries, then report the peer stuck. */
                if (++again_tries >= 4) return CARRIER_ERR_IO;
                continue;
            }
            return CARRIER_ERR_IO;
        }
        again_tries = 0;
        off += (size_t)n;
    }
    return CARRIER_OK;
}

static void s_close(void *u)
{
    carrier_sock_user_t *su = (carrier_sock_user_t *)u;
    if (su->fd >= 0) { carrier_close_fd(su->fd); su->fd = -1; }
}

static int s_fd(void *u)
{
    return ((carrier_sock_user_t *)u)->fd;
}

static void s_shutdown(void *u)
{
    int fd = ((carrier_sock_user_t *)u)->fd;
    if (fd >= 0) shutdown((carrier_fd_t)fd, SHUT_RDWR);
}

static void s_bind_iface(int fd, const char *a_iface)
{
    if (!a_iface || !a_iface[0]) return;
#if defined(__APPLE__)
    unsigned ifidx = if_nametoindex(a_iface);
    if (ifidx) setsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &ifidx, sizeof(ifidx));
#elif defined(SO_BINDTODEVICE)
    setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, a_iface, strlen(a_iface));
#elif defined(_WIN32)
    /* Windows: bind by interface index via IP_UNICAST_IF (Vista+). */
    NET_LUID l_luid;
    if (ConvertInterfaceAliasToLuid(a_iface, &l_luid) == NO_ERROR) {
        NET_IFINDEX l_idx = 0;
        if (ConvertInterfaceLuidToIndex(&l_luid, &l_idx) == NO_ERROR && l_idx) {
            DWORD l_be = htonl((DWORD)l_idx);
            setsockopt(fd, IPPROTO_IP, IP_UNICAST_IF, (const char *)&l_be, sizeof(l_be));
        }
    }
#else
    (void)fd;
#endif
}

carrier_io_ops_t *carrier_io_socket_connect(const char *host, uint16_t port,
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
    int to = timeout_sec > 0 ? timeout_sec : 10;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = (int)socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        s_bind_iface(fd, a_iface);
#ifdef _WIN32
        DWORD l_ms = (DWORD)to * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&l_ms, sizeof(l_ms));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&l_ms, sizeof(l_ms));
#else
        struct timeval tv = { to, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
        if (connect(fd, ai->ai_addr, (socklen_t)ai->ai_addrlen) == 0) {
            carrier_no_sigpipe(fd);
            break;
        }
        carrier_close_fd(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return NULL;

    carrier_sock_user_t *su = (carrier_sock_user_t *)calloc(1, sizeof(*su));
    carrier_io_ops_t *ops = (carrier_io_ops_t *)calloc(1, sizeof(*ops));
    if (!su || !ops) { free(su); free(ops); carrier_close_fd(fd); return NULL; }
    su->fd = fd;
    ops->user = su;
    ops->read = s_read;
    ops->write = s_write;
    ops->close = s_close;
    ops->fd = s_fd;
    ops->shutdown = s_shutdown;
    return ops;
}

void carrier_io_socket_free(carrier_io_ops_t *ops)
{
    if (!ops) return;
    carrier_sock_user_t *su = (carrier_sock_user_t *)ops->user;
    if (su) { if (su->fd >= 0) carrier_close_fd(su->fd); free(su); }
    free(ops);
}

int carrier_io_socket_fd(carrier_io_ops_t *ops)
{
    if (!ops || !ops->user) return -1;
    return ((carrier_sock_user_t *)ops->user)->fd;
}

void carrier_io_socket_shutdown(carrier_io_ops_t *ops)
{
    if (!ops || !ops->user) return;
    int fd = ((carrier_sock_user_t *)ops->user)->fd;
    if (fd >= 0) shutdown((carrier_fd_t)fd, SHUT_RDWR);
}