#ifndef DAP_OS_WINDOWS
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
#define poll WSAPoll
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#endif

#include <errno.h>
#include <string.h>
#include "dap_net.h"
#include "dap_strfuncs.h"
#define LOG_TAG "dap_net"

int dap_net_resolve_host(const char *a_host, const char *a_port, bool a_numeric_only, struct sockaddr_storage *a_addr_out, int *a_family)
{
    dap_return_val_if_fail_err(a_addr_out, -1, "Required storage is not provided");
    *a_addr_out = (struct sockaddr_storage){ };

    
    int l_ret = 0;
    #ifdef ANDROID //on android  AI_MASK is  (AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV )
        int l_ai_flags = a_numeric_only ? AI_NUMERICHOST : AI_CANONNAME ;
    #else
        int l_ai_flags = a_numeric_only ? AI_NUMERICHOST : AI_CANONNAME | AI_V4MAPPED | AI_ADDRCONFIG;
    #endif
    if ( !a_host )
        l_ai_flags |= AI_PASSIVE;
    
    if ( a_port )
        l_ai_flags |= AI_NUMERICSERV;

    struct addrinfo *l_res = NULL, l_hints = { .ai_flags = l_ai_flags, .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
    if (a_family)
        *a_family = AF_UNSPEC;
    if (( l_ret = getaddrinfo(a_host, a_port, &l_hints, &l_res) ) || !l_res ) {
#ifdef DAP_OS_WINDOWS
        _set_errno( WSAGetLastError() );
        log_it( L_ERROR, "getaddrinfo() failed, error %d: \"%s\"", errno, dap_strerror(errno) );
#else
        log_it( L_ERROR, "getaddrinfo() failed, error %d: \"%s\"", l_ret, gai_strerror(l_ret) );
#endif
        l_ret = -2;
    } else {
        if (a_family)
            *a_family = l_res->ai_family;
        l_ret = l_res->ai_addrlen;
        memcpy(a_addr_out, l_res->ai_addr, l_res->ai_addrlen);
        freeaddrinfo(l_res);
    }
    return l_ret;
}

int dap_net_parse_config_address(const char *a_src, char *a_addr, uint16_t *a_port, struct sockaddr_storage *a_saddr, int *a_family) {
    dap_return_val_if_fail_err( !!a_src && ( !!a_addr || !!a_port ), -1, "Required args are not provided");
    int l_len = 0, l_type = 0;
    char *l_bpos = NULL, *l_cpos = NULL;
    /*  
        type 4,5 - possibly hostname or IPv4 (no port, with port)
        type 6,7 - possibly IPv6 (no port, with port)
    */
    if ((l_cpos = strrchr(a_src, ':') )) {
        l_type = strchr(a_src, ':') == l_cpos ? 5 : 6;
    } else
        l_type = 4;

    if (*a_src == '[') {   // It's likely an IPv6 with port, see https://www.ietf.org/rfc/rfc2732.txt
        if ( l_type != 6 || !(l_bpos = strrchr(a_src, ']')) || l_cpos < l_bpos )
            return -1;
        a_src++;
        l_type = 7;
    } else if ( (l_bpos = strrchr(a_src, ']')) )
        return -2;
    
    switch (l_type) {
    case 4:
    case 6:
        l_len = strlen(a_src);
        if (a_port)
            *a_port = 0;
        break;
    case 5:
        l_bpos = l_cpos;
    case 7:
        if (a_port)
            *a_port = strtoul(l_cpos + 1, NULL, 10);
        l_len = l_bpos - a_src;
        break;
    default:
        log_it(L_ERROR, "Couldn't define address \"%s\" type", a_src);
        return 0;
    }
    if ( l_len > DAP_HOSTADDR_STRLEN || !l_len )
        return log_it(L_ERROR, "Can't parse config string \"%s\", invalid address size %d", a_src, l_len), -3;
    char *a_addr2 = a_addr ? a_addr : a_saddr ? DAP_NEW_STACK_SIZE(char, l_len + 1) : NULL;
    if ( !a_addr2 )
        return l_len;
    dap_strncpy(a_addr2, a_src, l_len + 1);
    return a_saddr ? dap_net_resolve_host(a_addr2, a_port ? dap_itoa(*a_port) : NULL, true, a_saddr, a_family) : l_len;
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
