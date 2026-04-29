/**
 * @file dap_io_flow_ebpf.h
 * @brief eBPF-based SO_REUSEPORT sticky sessions
 * 
 * Provides kernel-level consistent hashing for UDP flows to eliminate
 * duplicate flow creation across worker threads.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "dap_io_flow.h"  // For dap_io_flow_lb_tier_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if eBPF sticky sessions are available
 * 
 * Tests:
 * - Program loading (BPF syscall)
 * - SO_ATTACH_REUSEPORT_EBPF support (attach before bind)
 * 
 * @return true if eBPF fully available, false otherwise
 */
bool dap_io_flow_ebpf_is_available(void);

/**
 * @brief Attach eBPF sticky session program to SO_REUSEPORT socket
 * 
 * Loads and attaches eBPF program that returns a valid reuseport socket index.
 * 
 * @param socket_fd SO_REUSEPORT socket in the target group
 * @return 0 on success, -1 on error
 */
int dap_io_flow_ebpf_attach_socket(int socket_fd);

/**
 * @brief Attach eBPF sticky session program with an explicit listener count
 *
 * @param socket_fd SO_REUSEPORT socket in the target group
 * @param listener_count Number of sockets in the reuseport group
 * @return 0 on success, -1 on error
 */
int dap_io_flow_ebpf_attach_socket_count(int socket_fd, uint32_t listener_count);

/**
 * @brief Detach eBPF program from socket (cleanup)
 * 
 * @param socket_fd Socket to detach from
 * @return 0 on success, -1 on error
 */
int dap_io_flow_ebpf_detach_socket(int socket_fd);

#ifdef __cplusplus
}
#endif
