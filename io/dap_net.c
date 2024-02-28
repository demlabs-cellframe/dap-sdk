#ifndef _WIN32
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
//#include <unistd.h> // for close
#include <fcntl.h>
//#include <sys/poll.h>
//#include <sys/select.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/stat.h>
//#define closesocket close
//typedef int SOCKET;
//#define SOCKET_ERROR    -1  // for win32 =  (-1)
//#define INVALID_SOCKET  -1  // for win32 =  (SOCKET)(~0)
// for Windows
#else
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#endif

#include <errno.h>
#include <string.h>
#include "dap_net.h"

#define LOG_TAG "dap_net"

#ifdef _WIN32
  #define poll WSAPoll
#endif

int dap_net_resolve_host(const char *a_host, const char *a_port, struct sockaddr_storage *a_addr_out, bool a_passive_flag)
{
    dap_return_val_if_fail(a_addr_out && a_host, -1);
    int l_ret = 0;
    struct addrinfo l_hints = {
        .ai_flags   = a_passive_flag ? AI_CANONNAME | AI_PASSIVE : AI_CANONNAME,
        .ai_family  = AF_UNSPEC,
        .ai_socktype= SOCK_STREAM,
    }, *l_res;

    if ( l_ret = getaddrinfo(a_host, a_port, &l_hints, &l_res) )
        return l_ret;

    // for (struct addrinfo *l_res_it = l_res; l_res_it; l_res_it = l_res_it->ai_next) // What shall we do?
    {
        memset(a_addr_out, 0, sizeof(*a_addr_out));
        memcpy(a_addr_out, l_res->ai_addr, l_res->ai_addrlen);
    }
    freeaddrinfo(l_res);
    return 0;
}

/**
 * @brief s_recv
 * timeout in milliseconds
 * return the number of read bytes (-1 err or -2 timeout)
 * @param sock
 * @param buf
 * @param bufsize
 * @param timeout
 * @return long
 */
long dap_net_recv(SOCKET sd, unsigned char *buf, size_t bufsize, int timeout)
{
struct pollfd fds = {.fd = sd, .events = POLLIN};
int res;

    if ( !(res = poll(&fds, 1, timeout)) )
        return -2;

    if ( (res == 1) && !(fds.revents & POLLIN))
        return -1;

    if(res < 1)
        return -1;

    if ( 0 >= (res = recv(sd, (char *)buf, bufsize, 0)) )
        printf("[s_recv] recv()->%d, errno: %d\n", res, errno);

    return res;
}
