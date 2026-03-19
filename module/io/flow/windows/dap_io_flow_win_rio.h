/**
 * @file dap_io_flow_win_rio.h
 * @brief Windows RIO (Registered I/O) load balancing
 * 
 * Windows doesn't have SO_REUSEPORT. Instead, use:
 * - RIO (Registered I/O) for high-performance UDP
 * - IOCP (I/O Completion Port) with multiple threads
 * - Application-level flow distribution across worker sockets
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if Windows RIO is available
 * 
 * RIO (Registered I/O) available on Windows 8+ / Server 2012+.
 * Provides high-performance UDP I/O with pre-registered buffers.
 * 
 * @return true if RIO available, false otherwise
 */
bool dap_io_flow_win_rio_is_available(void);

/**
 * @brief Configure socket for RIO-based load balancing
 * 
 * On Windows, multiple sockets can share the same IOCP.
 * Each worker thread manages its own socket, application distributes flows.
 * 
 * Strategy:
 * - Each worker: dedicated socket + RIO completion queue
 * - Application-level hash determines target worker
 * - IOCP ensures efficient cross-thread wake-up
 * 
 * @param socket_fd Socket handle (SOCKET)
 * @return 0 on success, -1 on error
 */
int dap_io_flow_win_rio_configure(int socket_fd);

#ifdef __cplusplus
}
#endif
