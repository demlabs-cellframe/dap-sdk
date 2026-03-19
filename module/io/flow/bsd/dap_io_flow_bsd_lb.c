/**
 * @file dap_io_flow_bsd_lb.c
 * @brief FreeBSD SO_REUSEPORT_LB implementation
 * 
 * Provides kernel-level load balancing for UDP sockets using FreeBSD's
 * native SO_REUSEPORT_LB option (available since FreeBSD 12.0).
 * 
 * Benefits:
 * - Kernel-level distribution (no application queues needed)
 * - Even load distribution across sockets
 * - NUMA-aware with TCP_REUSPORT_LB_NUMA
 * - Works for both TCP and UDP
 */

#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>

#include "dap_common.h"
#include "dap_io_flow_bsd_lb.h"

#define LOG_TAG "dap_io_flow_bsd_lb"

// SO_REUSEPORT_LB constant (FreeBSD 12.0+)
#ifndef SO_REUSEPORT_LB
#define SO_REUSEPORT_LB 0x00010000
#endif

/**
 * @brief Check if SO_REUSEPORT_LB is available
 */
bool dap_io_flow_bsd_lb_is_available(void)
{
#if defined(__FreeBSD__) || defined(__DragonFly__)
    // SO_REUSEPORT_LB available on FreeBSD 12.0+ and DragonFly BSD
    log_it(L_NOTICE, "✅ FreeBSD SO_REUSEPORT_LB: AVAILABLE");
    return true;
#else
    log_it(L_NOTICE, "❌ FreeBSD SO_REUSEPORT_LB: NOT AVAILABLE (not FreeBSD)");
    return false;
#endif
}

/**
 * @brief Enable SO_REUSEPORT_LB on socket
 * 
 * Must be called BEFORE bind(). All sockets in the group must have
 * SO_REUSEPORT_LB set before binding to the same address/port.
 * 
 * Kernel will distribute incoming packets evenly across all sockets
 * in the load-balancing group, providing sticky sessions automatically.
 */
int dap_io_flow_bsd_lb_enable(int socket_fd)
{
#if defined(__FreeBSD__) || defined(__DragonFly__)
    int opt = 1;
    
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(opt)) < 0) {
        log_it(L_ERROR, "Failed to set SO_REUSEPORT_LB on socket %d: %s",
               socket_fd, strerror(errno));
        return -1;
    }
    
    log_it(L_DEBUG, "✅ SO_REUSEPORT_LB enabled on socket %d", socket_fd);
    return 0;
#else
    log_it(L_ERROR, "SO_REUSEPORT_LB not supported on this BSD variant");
    return -1;
#endif
}
