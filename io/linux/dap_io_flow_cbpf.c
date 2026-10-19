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

static bool s_debug_more = false;
// Classic BPF availability flag
static bool s_cbpf_available = false;
static bool s_cbpf_checked = false;

/**
 * @brief Classic BPF program for SO_REUSEPORT consistent hashing
 *
 * For cBPF with SO_ATTACH_REUSEPORT_CBPF:
 * - Kernel calls pskb_pull(skb, sizeof(struct udphdr)) → data at UDP payload
 * - Return value MUST be a valid socket index. Out-of-range values fall back
 *   to the kernel selector; the kernel does NOT apply modulo.
 *
 * Hash the source IP, then explicitly reduce it to the bound group size.
 * Clients behind one NAT share a worker: this trades distribution granularity
 * for stable ownership across changes in RX hash metadata.
 */

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
int dap_io_flow_cbpf_attach_socket(int socket_fd, unsigned int socket_count)
{
    if (!socket_count) return -1;
    if (!dap_io_flow_cbpf_is_available()) {
        log_it(L_ERROR, "Classic BPF not available, cannot attach");
        return -1;
    }
    
#ifdef SO_ATTACH_REUSEPORT_CBPF
    /* IPv4 source address is stable across NIC/RPS hash changes and IP
     * fragmentation. IPv6 uses the final source-address word. Do not hash
     * payload bytes: handshake and encrypted data have different layouts. */
    struct sock_filter l_filter[] = {
        BPF_STMT(BPF_LD | BPF_B | BPF_ABS, SKF_NET_OFF),
        BPF_STMT(BPF_ALU | BPF_AND | BPF_K, 0xf0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x60, 2, 0),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, SKF_NET_OFF + 12),
        BPF_STMT(BPF_JMP | BPF_JA, 1),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, SKF_NET_OFF + 20),
        BPF_STMT(BPF_ALU | BPF_MOD | BPF_K, socket_count),
        BPF_STMT(BPF_RET | BPF_A, 0),
    };
    struct sock_fprog prog = {
        .len = sizeof(l_filter) / sizeof(struct sock_filter),
        .filter = l_filter,
    };
    
    if (setsockopt(socket_fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF, 
                   &prog, sizeof(prog)) < 0) {
        log_it(L_ERROR, "Failed to attach classic BPF to SO_REUSEPORT socket %d: %s", 
               socket_fd, strerror(errno));
        return -1;
    }
    
    log_it(L_NOTICE, "✅ Classic BPF attached to SO_REUSEPORT group (socket=%d)", socket_fd);
    log_it(L_NOTICE, "Sticky sessions enabled: source IP modulo %u listeners", socket_count);
    
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
    
    debug_if(s_debug_more, L_DEBUG, "Classic BPF detached from socket %d", socket_fd);
    return 0;
#else
    log_it(L_WARNING, "SO_DETACH_REUSEPORT_BPF not available");
    return -1;
#endif
}
