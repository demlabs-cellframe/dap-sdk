/**
 * @file dap_io_flow_darwin_gcd.c
 * @brief macOS GCD (Grand Central Dispatch) load balancing implementation
 * 
 * macOS approach for multi-socket UDP load balancing:
 * 1. SO_REUSEPORT allows multiple sockets on same port (requires SO_REUSEADDR too)
 * 2. GCD dispatch sources provide efficient kqueue-based I/O with automatic threading
 * 3. Application-level hash-based distribution (similar to Linux Tier 1)
 * 
 * NOTE: macOS SO_REUSEPORT semantics differ from Linux:
 * - Requires both SO_REUSEADDR and SO_REUSEPORT
 * - No kernel-level hash distribution like Linux
 * - Best performance via GCD dispatch sources (Apple's recommended approach)
 */

#include <sys/socket.h>
#include <errno.h>
#include <string.h>

#include "dap_common.h"
#include "dap_io_flow_darwin_gcd.h"

#define LOG_TAG "dap_io_flow_darwin_gcd"

/**
 * @brief Check if GCD load balancing is available
 */
bool dap_io_flow_darwin_gcd_is_available(void)
{
#ifdef __APPLE__
    // GCD is available on all macOS versions we support
    log_it(L_NOTICE, "✅ macOS GCD load balancing: AVAILABLE");
    log_it(L_NOTICE, "Using SO_REUSEPORT + application-level hash distribution");
    return true;
#else
    log_it(L_NOTICE, "❌ macOS GCD: NOT AVAILABLE (not macOS)");
    return false;
#endif
}

/**
 * @brief Configure socket for GCD-based load balancing
 * 
 * Sets both SO_REUSEADDR and SO_REUSEPORT (macOS requirement).
 * Sockets will be managed via existing dap_events (kqueue-based) infrastructure.
 * 
 * Load distribution strategy:
 * - Each worker thread has its own socket bound to same address/port
 * - Application-level hash (Tier 1) distributes flows across workers
 * - GCD/kqueue ensures efficient event delivery per worker
 */
int dap_io_flow_darwin_gcd_configure(int socket_fd)
{
#ifdef __APPLE__
    int opt = 1;
    
    // macOS requires BOTH SO_REUSEADDR and SO_REUSEPORT for multi-socket binding
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_it(L_ERROR, "Failed to set SO_REUSEADDR: %s", strerror(errno));
        return -1;
    }
    
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        log_it(L_ERROR, "Failed to set SO_REUSEPORT: %s", strerror(errno));
        return -1;
    }
    
    log_it(L_DEBUG, "✅ macOS socket configured: SO_REUSEADDR + SO_REUSEPORT");
    log_it(L_DEBUG, "Load distribution via application-level hash (GCD manages I/O)");
    
    return 0;
#else
    log_it(L_ERROR, "macOS GCD configuration not supported on this platform");
    return -1;
#endif
}
