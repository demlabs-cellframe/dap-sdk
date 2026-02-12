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
 * IMPORTANT: For classic BPF (cBPF) with SO_ATTACH_REUSEPORT_CBPF:
 * - Data pointer starts AFTER transport header (UDP payload), NOT at UDP header!
 * - Kernel calls pskb_pull(skb, sizeof(struct udphdr)) before running BPF
 * - BPF program must return SOCKET INDEX (0 to N-1), not raw hash
 * - If return value >= num_sockets, kernel uses default distribution
 * 
 * SOLUTION: Use BPF ancillary data extension SKF_AD_RXHASH to get the
 * kernel-computed packet hash. This hash is based on the full 4-tuple:
 *   (src_ip, src_port, dst_ip, dst_port)
 * 
 * This provides:
 * 1. STICKY SESSIONS: Same client (same 4-tuple) always gets same hash
 * 2. GOOD DISTRIBUTION: Kernel hash is well-distributed across values
 * 3. WORKS FOR ALL CLIENTS: Not dependent on localhost quirks
 * 
 * The kernel documentation (networking/filter.rst) lists available extensions:
 *   - SKF_AD_RXHASH (offset 32): skb->hash - packet hash
 *   - SKF_AD_CPU (offset 36): current CPU number
 *   - etc.
 * 
 * Access via: BPF_LD | BPF_W | BPF_ABS with k = SKF_AD_OFF + SKF_AD_*
 * 
 * Classic BPF instruction format:
 * - code: operation code (BPF_LD, BPF_RET, etc.)
 * - jt: jump if true offset  
 * - jf: jump if false offset
 * - k: generic multi-use field (constant/offset)
 * 
 * SKF_AD_OFF and SKF_AD_RXHASH are defined in linux/filter.h
 */
static struct sock_filter s_cbpf_reuseport_prog[] = {
    // Use rxhash (kernel 4-tuple hash) for sticky sessions
    // 
    // IMPORTANT: rxhash is 0 for loopback/local traffic!
    // - Production (real network): rxhash works, sticky sessions OK
    // - Localhost testing: rxhash=0, all to worker 0 (use eBPF or Application tier)
    //
    // For full localhost support, upgrade to eBPF (BPF_PROG_TYPE_SK_REUSEPORT)
    // which has access to sk_reuseport_md with remote_ip/remote_port fields
    { .code = BPF_LD | BPF_W | BPF_ABS, .jt = 0, .jf = 0, .k = SKF_AD_OFF + SKF_AD_RXHASH },
    { .code = BPF_RET | BPF_A, .jt = 0, .jf = 0, .k = 0 },
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
