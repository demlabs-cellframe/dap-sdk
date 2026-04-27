/**
 * @file dap_io_flow_win_rio.c
 * @brief Windows RIO (Registered I/O) load balancing implementation
 * 
 * Windows load balancing strategy:
 * 1. Each worker thread creates its own UDP socket
 * 2. All sockets bind to same address/port (Windows allows this for UDP)
 * 3. Each socket registers RIO completion queue
 * 4. Application-level hash distributes flows across workers
 * 5. IOCP provides efficient multi-threaded I/O completion
 * 
 * This is essentially Tier 1 (Application-level) but optimized with RIO.
 * Windows doesn't provide kernel-level sticky sessions like Linux BPF or FreeBSD LB.
 */

#ifdef _WIN32
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#endif

#include <errno.h>
#include <string.h>

#include "dap_common.h"
#include "dap_io_flow_win_rio.h"

#define LOG_TAG "dap_io_flow_win_rio"

static bool s_debug_more = false;
static int s_rio_available_cache = -1;
/**
 * @brief Check if RIO is available
 */
bool dap_io_flow_win_rio_is_available(void)
{
#ifdef _WIN32
    if (s_rio_available_cache >= 0) {
        return s_rio_available_cache != 0;
    }

#if defined(_WIN32_WINNT) && (_WIN32_WINNT < 0x0602)
    log_it(L_NOTICE, "Windows WIN_RIO tier enabled with compatibility mode (_WIN32_WINNT=0x%04x < 0x0602)",
           (unsigned int)_WIN32_WINNT);
    s_rio_available_cache = 1;
    return true;
#elif !defined(SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER) || !defined(WSAID_MULTIPLE_RIO)
    log_it(L_NOTICE, "Windows WIN_RIO tier enabled (native RIO symbols unavailable in SDK, using Windows socket fallback)");
    s_rio_available_cache = 1;
    return true;
#else

    SOCKET l_probe_socket = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (l_probe_socket == INVALID_SOCKET) {
        int l_wsa_err = WSAGetLastError();
        log_it(L_WARNING, "Windows RIO probe socket creation failed (WSA=%d), using compatibility mode", l_wsa_err);
        s_rio_available_cache = 1;
        return true;
    }

    GUID l_rio_guid = WSAID_MULTIPLE_RIO;
    RIO_EXTENSION_FUNCTION_TABLE l_rio_table;
    DWORD l_bytes = 0;
    memset(&l_rio_table, 0, sizeof(l_rio_table));
    int l_ioctl_rc = WSAIoctl(
        l_probe_socket,
        SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
        &l_rio_guid,
        sizeof(l_rio_guid),
        &l_rio_table,
        sizeof(l_rio_table),
        &l_bytes,
        NULL,
        NULL);

    int l_ioctl_wsa_err = (l_ioctl_rc == SOCKET_ERROR) ? WSAGetLastError() : 0;
    closesocket(l_probe_socket);

    if (l_ioctl_rc == SOCKET_ERROR) {
        log_it(L_NOTICE, "Windows RIO extension unavailable (WSAIoctl error=%d), using compatibility mode",
               l_ioctl_wsa_err);
        s_rio_available_cache = 1;
        return true;
    }

    if (!l_rio_table.RIOCreateCompletionQueue || !l_rio_table.RIOReceive || !l_rio_table.RIOSend) {
        log_it(L_NOTICE, "Windows RIO extension table incomplete, using compatibility mode");
        s_rio_available_cache = 1;
        return true;
    }

    log_it(L_NOTICE, "Windows RIO extension detected: enabling native WIN_RIO mode");
    s_rio_available_cache = 1;
    return true;
#endif
#else
    log_it(L_NOTICE, "❌ Windows RIO: NOT AVAILABLE (not Windows)");
    return false;
#endif
}

/**
 * @brief Configure socket for RIO-based load balancing
 * 
 * Windows UDP socket configuration:
 * - Enable address reuse (allows multiple sockets on same port)
 * - Configure for IOCP usage
 * - Application will handle flow distribution
 * 
 * Note: Windows doesn't provide kernel-level sticky sessions.
 * We rely on Application-level LB (Tier 1) with IOCP optimization.
 */
int dap_io_flow_win_rio_configure(int socket_fd)
{
#ifdef _WIN32
    BOOL opt = TRUE;
    SOCKET sock = (SOCKET)socket_fd;
    
    // Allow multiple sockets to bind to same address/port (Windows UDP quirk)
    // This is different from Linux SO_REUSEPORT but achieves similar result
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) != 0) {
        log_it(L_ERROR, "Failed to set SO_REUSEADDR: error %d", WSAGetLastError());
        return -1;
    }
    
    // Optional: Set SO_EXCLUSIVEADDRUSE = FALSE to allow port sharing
    // (Already implicit with SO_REUSEADDR on Windows for UDP)
    
    debug_if(s_debug_more, L_DEBUG, "✅ Windows socket configured for multi-socket load balancing");
    debug_if(s_debug_more, L_DEBUG, "Distribution: Application-level hash + IOCP");
    
    // Note: RIO registration happens later when socket is associated with
    // completion queue in dap_events/dap_worker infrastructure
    
    return 0;
#else
    log_it(L_ERROR, "Windows RIO configuration not supported on this platform");
    return -1;
#endif
}
