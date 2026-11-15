/**
 * @file carrier_pipe.c
 * @brief Portable bidirectional byte pipe: socketpair on POSIX, TCP loopback
 *        pair on Windows (which lacks socketpair). No SDK dependencies.
 */

#include "carrier_port.h"
#include "carrier/carrier.h"

#include <string.h>

#ifdef _WIN32

int carrier_pipe_pair(int a_fds[2])
{
    a_fds[0] = a_fds[1] = -1;
    SOCKET l_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (l_listen == INVALID_SOCKET) return -1;

    struct sockaddr_in l_addr;
    memset(&l_addr, 0, sizeof(l_addr));
    l_addr.sin_family = AF_INET;
    l_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    l_addr.sin_port = 0; /* ephemeral */
    if (bind(l_listen, (struct sockaddr *)&l_addr, sizeof(l_addr)) != 0) {
        closesocket(l_listen); return -1;
    }
    int l_alen = sizeof(l_addr);
    if (getsockname(l_listen, (struct sockaddr *)&l_addr, &l_alen) != 0 ||
        listen(l_listen, 1) != 0) {
        closesocket(l_listen); return -1;
    }

    SOCKET l_cli = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (l_cli == INVALID_SOCKET) { closesocket(l_listen); return -1; }
    if (connect(l_cli, (struct sockaddr *)&l_addr, sizeof(l_addr)) != 0) {
        closesocket(l_cli); closesocket(l_listen); return -1;
    }
    SOCKET l_srv = accept(l_listen, NULL, NULL);
    closesocket(l_listen);
    if (l_srv == INVALID_SOCKET) { closesocket(l_cli); return -1; }

    int l_nodelay = 1;
    setsockopt(l_cli, IPPROTO_TCP, TCP_NODELAY, (const char *)&l_nodelay, sizeof(l_nodelay));
    setsockopt(l_srv, IPPROTO_TCP, TCP_NODELAY, (const char *)&l_nodelay, sizeof(l_nodelay));

    a_fds[0] = (int)l_cli;
    a_fds[1] = (int)l_srv;
    return 0;
}

void carrier_pipe_close(int a_fd)
{
    if (a_fd >= 0) closesocket((SOCKET)a_fd);
}

#else /* POSIX */

int carrier_pipe_pair(int a_fds[2])
{
    a_fds[0] = a_fds[1] = -1;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, a_fds) != 0) return -1;
    return 0;
}

void carrier_pipe_close(int a_fd)
{
    if (a_fd >= 0) close(a_fd);
}

#endif