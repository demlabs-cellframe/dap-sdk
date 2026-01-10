/**
 * @file dap_io_flow_ebpf.h
 * @brief eBPF-based SO_REUSEPORT sticky sessions
 * 
 * Provides kernel-level consistent hashing for UDP flows to eliminate
 * duplicate flow creation across worker threads.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if eBPF sticky sessions are available
 * 
 * Checks for:
 * - Linux kernel 4.15+ (SO_ATTACH_REUSEPORT_EBPF support)
 * - CAP_BPF or CAP_NET_ADMIN capability
 * 
 * @return true if eBPF available, false otherwise
 */
bool dap_io_flow_ebpf_is_available(void);

/**
 * @brief Attach eBPF sticky session program to SO_REUSEPORT socket
 * 
 * Loads and attaches eBPF program that implements consistent hashing
 * based on (src_ip, src_port) to ensure packets from the same client
 * always arrive on the same worker thread.
 * 
 * @param socket_fd One of the SO_REUSEPORT sockets (any from the group)
 * @return 0 on success, -1 on error
 */
int dap_io_flow_ebpf_attach_socket(int socket_fd);

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

