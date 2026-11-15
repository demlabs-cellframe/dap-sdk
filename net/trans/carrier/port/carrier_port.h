/**
 * @file carrier_port.h
 * @brief Platform compatibility for the carrier layer (POSIX + Windows/MinGW).
 *        No SDK dependencies.
 *
 * Closes the Windows gaps of the REALITY template (plan §2.6):
 *   - socketpair -> carrier_pipe_pair (TCP loopback on Windows)
 *   - nonblock   -> fcntl / ioctlsocket
 *   - SIGPIPE    -> MSG_NOSIGNAL / SO_NOSIGPIPE / no-op on Windows
 *   - poll       -> poll / WSAPoll
 *   - errno      -> errno / WSAGetLastError
 */

#ifndef CARRIER_PORT_H
#define CARRIER_PORT_H

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
typedef SOCKET carrier_fd_t;
#  define CARRIER_BAD_FD INVALID_SOCKET
#  define CARRIER_SEND_FLAGS 0
#  define CARRIER_EAGAIN WSAEWOULDBLOCK
#  define CARRIER_EWOULDBLOCK WSAEWOULDBLOCK
#  define CARRIER_EINTR WSAEINTR
static inline int carrier_errno(void) { return WSAGetLastError(); }
static inline int carrier_poll(struct pollfd *a_fds, unsigned long a_n, int a_ms)
{ return WSAPoll(a_fds, a_n, a_ms); }
static inline int carrier_close_fd(int a_fd) { return closesocket((SOCKET)a_fd); }
static inline int carrier_set_nonblock(int a_fd)
{
    u_long l_mode = 1;
    return ioctlsocket((SOCKET)a_fd, FIONBIO, &l_mode) == 0 ? 0 : -1;
}
static inline void carrier_no_sigpipe(int a_fd) { (void)a_fd; /* no SIGPIPE on Windows */ }
#else
#  include <sys/socket.h>
#  include <poll.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
typedef int carrier_fd_t;
#  define CARRIER_BAD_FD (-1)
#  ifdef MSG_NOSIGNAL
#    define CARRIER_SEND_FLAGS MSG_NOSIGNAL
#  else
#    define CARRIER_SEND_FLAGS 0
#  endif
#  define CARRIER_EAGAIN EAGAIN
#  define CARRIER_EWOULDBLOCK EWOULDBLOCK
#  define CARRIER_EINTR EINTR
static inline int carrier_errno(void) { return errno; }
static inline int carrier_poll(struct pollfd *a_fds, unsigned long a_n, int a_ms)
{ return poll(a_fds, (nfds_t)a_n, a_ms); }
static inline int carrier_close_fd(int a_fd) { return close(a_fd); }
static inline int carrier_set_nonblock(int a_fd)
{
    int l_fl = fcntl(a_fd, F_GETFL, 0);
    if (l_fl < 0) return -1;
    return fcntl(a_fd, F_SETFL, l_fl | O_NONBLOCK);
}
static inline void carrier_no_sigpipe(int a_fd)
{
#  ifdef SO_NOSIGPIPE
    int l_on = 1;
    (void)setsockopt(a_fd, SOL_SOCKET, SO_NOSIGPIPE, &l_on, sizeof(l_on));
#  else
    (void)a_fd;
#  endif
}
#endif

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Implemented in carrier_pipe.c */
int  carrier_pipe_pair(int a_fds[2]);
void carrier_pipe_close(int a_fd);

#ifdef __cplusplus
}
#endif

#endif /* CARRIER_PORT_H */