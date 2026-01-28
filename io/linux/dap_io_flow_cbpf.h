/**
 * @file dap_io_flow_cbpf.h
 * @brief Classic BPF-based SO_REUSEPORT sticky sessions
 * 
 * Provides kernel-level consistent hashing for UDP flows using classic BPF.
 * Fallback from eBPF when SO_ATTACH_REUSEPORT_EBPF not supported.
 */

#pragma once

#include <stdbool.h>
#include "dap_io_flow.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if classic BPF is available
 * 
 * Checks for SO_ATTACH_REUSEPORT_CBPF support (Linux 3.9+)
 * 
 * @return true if classic BPF available, false otherwise
 */
bool dap_io_flow_cbpf_is_available(void);

/**
 * @brief Attach classic BPF program to SO_REUSEPORT socket group
 * 
 * Loads and attaches classic BPF program for consistent hashing.
 * Uses simpler BPF bytecode than eBPF but still provides sticky sessions.
 * 
 * @param socket_fd One of the SO_REUSEPORT sockets (program attaches to entire group)
 * @return 0 on success, -1 on error
 */
int dap_io_flow_cbpf_attach_socket(int socket_fd);

/**
 * @brief Detach classic BPF program from socket
 * 
 * @param socket_fd Socket to detach from
 * @return 0 on success, -1 on error
 */
int dap_io_flow_cbpf_detach_socket(int socket_fd);

#ifdef __cplusplus
}
#endif
