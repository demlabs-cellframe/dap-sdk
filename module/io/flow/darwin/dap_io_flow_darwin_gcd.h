/**
 * @file dap_io_flow_darwin_gcd.h
 * @brief macOS Grand Central Dispatch (GCD) load balancing for UDP
 * 
 * macOS SO_REUSEPORT has different semantics than Linux.
 * Optimal solution: use GCD dispatch sources for efficient multi-socket handling.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if macOS GCD load balancing is available
 * 
 * GCD (Grand Central Dispatch) is available on all modern macOS versions.
 * Provides efficient kernel queue (kqueue) based I/O with automatic thread management.
 * 
 * @return true if available (always true on macOS), false otherwise
 */
bool dap_io_flow_darwin_gcd_is_available(void);

/**
 * @brief Configure socket for GCD-based load balancing
 * 
 * Sets SO_REUSEPORT (macOS variant) and prepares socket for GCD dispatch source.
 * macOS SO_REUSEPORT allows multiple sockets on same port but distribution
 * is managed by application via GCD, not kernel.
 * 
 * @param socket_fd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int dap_io_flow_darwin_gcd_configure(int socket_fd);

#ifdef __cplusplus
}
#endif
