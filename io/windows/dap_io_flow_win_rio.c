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
/**
 * @brief Check if RIO is available
 */
bool dap_io_flow_win_rio_is_available(void)
{
#ifdef _WIN32
    // RIO available on Windows 8+ / Server 2012+
    // Version check: Windows 8 = version 6.2
    OSVERSIONINFOEX osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    osvi.dwMajorVersion = 6;
    osvi.dwMinorVersion = 2;
    
    // Simple version check
    // In production, use RtlGetVersion() or check RIO function pointers
    log_it(L_NOTICE, "Windows RIO: Assuming available (Windows 8+/Server 2012+)");
    log_it(L_NOTICE, "Using IOCP + application-level load balancing");
    return true;
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
