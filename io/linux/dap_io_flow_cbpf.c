/**
 * @file dap_io_flow_cbpf.c
 * @brief Classic BPF (cBPF) for SO_REUSEPORT sticky sessions
 * 
 * Implements kernel-level load balancing using classic BPF (SO_ATTACH_REUSEPORT_CBPF).
 * Simpler and more portable than eBPF, available since Linux 3.9.
 * 
 * Requires: Linux kernel 3.9+ (for SO_ATTACH_REUSEPORT_CBPF)
 * 
 * Falls back to: Application-level load balancing if classic BPF unavailable
 */

#include <sys/socket.h>
#include <linux/filter.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "dap_common.h"
#include "dap_io_flow_cbpf.h"

#define LOG_TAG "dap_io_flow_cbpf"

// Classic BPF availability flag
static bool s_cbpf_available = false;
static bool s_cbpf_checked = false;

/**
 * @brief Classic BPF program for SO_REUSEPORT consistent hashing
 * 
 * Classic BPF context for SO_REUSEPORT (simplified):
 * - A[0] = hash value from skb (for SO_REUSEPORT context)
 * - Program returns hash value
 * - Kernel does: socket_index = hash % num_sockets
 * 
 * Classic BPF instruction format:
 * - code: operation code
 * - jt: jump if true offset
 * - jf: jump if false offset
 * - k: generic multi-use field (constant/offset)
 * 
 * Classic BPF opcodes:
 * - 0x20: LD W (abs) - load word from absolute offset
 * - 0x06: RET A - return accumulator value
 * - 0x15: JEQ K - jump if accumulator == constant
 * 
 * Our program:
 * 1. Load hash from context (A[0] for REUSEPORT)
 * 2. Return it
 * 3. Kernel distributes: socket = sockets[hash % N]
 */
static struct sock_filter s_cbpf_reuseport_prog[] = {
    // ld [0]  - Load word from offset 0 (hash value in REUSEPORT context)
    // For SO_REUSEPORT context, offset 0 contains the hash
    { .code = 0x20, .jt = 0, .jf = 0, .k = 0 },
    
    // ret a   - Return accumulator (hash value)
    // Kernel will do: socket_index = return_value % num_sockets
    { .code = 0x06, .jt = 0, .jf = 0, .k = 0 },
};

/**
 * @brief Check if classic BPF is available
 */
bool dap_io_flow_cbpf_is_available(void)
{
    if (s_cbpf_checked) {
        return s_cbpf_available;
    }
    
    s_cbpf_checked = true;
    
#ifdef SO_ATTACH_REUSEPORT_CBPF
    s_cbpf_available = true;
    log_it(L_NOTICE, "✅ Classic BPF sticky sessions: AVAILABLE (kernel 3.9+)");
#else
    s_cbpf_available = false;
    log_it(L_CRITICAL, "❌ Classic BPF: NOT AVAILABLE (SO_ATTACH_REUSEPORT_CBPF not defined)");
#endif
    
    return s_cbpf_available;
}

/**
 * @brief Attach classic BPF program to SO_REUSEPORT socket group
 * 
 * @param socket_fd One of the SO_REUSEPORT sockets (program attaches to entire group)
 * @return 0 on success, -1 on error
 */
int dap_io_flow_cbpf_attach_socket(int socket_fd)
{
    if (!dap_io_flow_cbpf_is_available()) {
        log_it(L_ERROR, "Classic BPF not available, cannot attach");
        return -1;
    }
    
#ifdef SO_ATTACH_REUSEPORT_CBPF
    struct sock_fprog prog = {
        .len = sizeof(s_cbpf_reuseport_prog) / sizeof(struct sock_filter),
        .filter = s_cbpf_reuseport_prog,
    };
    
    if (setsockopt(socket_fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF, 
                   &prog, sizeof(prog)) < 0) {
        log_it(L_ERROR, "Failed to attach classic BPF to SO_REUSEPORT socket %d: %s", 
               socket_fd, strerror(errno));
        return -1;
    }
    
    log_it(L_NOTICE, "✅ Classic BPF attached to SO_REUSEPORT group (socket=%d)", socket_fd);
    log_it(L_NOTICE, "Sticky sessions enabled: kernel hash → consistent worker selection");
    
    return 0;
#else
    log_it(L_ERROR, "SO_ATTACH_REUSEPORT_CBPF not available on this platform");
    return -1;
#endif
}

/**
 * @brief Detach classic BPF program from socket
 * 
 * @param socket_fd Socket to detach from
 * @return 0 on success, -1 on error
 */
int dap_io_flow_cbpf_detach_socket(int socket_fd)
{
#ifdef SO_DETACH_REUSEPORT_BPF
    if (setsockopt(socket_fd, SOL_SOCKET, SO_DETACH_REUSEPORT_BPF, 
                   NULL, 0) < 0) {
        log_it(L_WARNING, "Failed to detach classic BPF from socket %d: %s", 
               socket_fd, strerror(errno));
        return -1;
    }
    
    log_it(L_DEBUG, "Classic BPF detached from socket %d", socket_fd);
    return 0;
#else
    log_it(L_WARNING, "SO_DETACH_REUSEPORT_BPF not available");
    return -1;
#endif
}
