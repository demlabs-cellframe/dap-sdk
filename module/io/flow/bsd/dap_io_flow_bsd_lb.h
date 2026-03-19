/**
 * @file dap_io_flow_bsd_lb.h
 * @brief FreeBSD SO_REUSEPORT_LB load balancing
 * 
 * FreeBSD provides native kernel-level load balancing via SO_REUSEPORT_LB.
 * Available since FreeBSD 12.0 (2017).
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if BSD SO_REUSEPORT_LB is available
 * 
 * FreeBSD 12.0+ provides SO_REUSEPORT_LB for kernel-level load balancing.
 * 
 * @return true if available, false otherwise
 */
bool dap_io_flow_bsd_lb_is_available(void);

/**
 * @brief Enable SO_REUSEPORT_LB on socket (before bind)
 * 
 * Must be called before bind(). Creates load-balancing group where
 * kernel distributes packets evenly across all sockets.
 * 
 * @param socket_fd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int dap_io_flow_bsd_lb_enable(int socket_fd);

#ifdef __cplusplus
}
#endif
