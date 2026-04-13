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
 * - Return value = socket index (kernel does index % num_sockets)
 *
 * Algorithm:
 * 1. Load SKF_AD_RXHASH (kernel 4-tuple hash from skb->hash)
 * 2. If non-zero (real network) → return it for sticky sessions
 * 3. If zero (loopback — rxhash not computed) → fallback:
 *    read UDP source port via SKF_NET_OFF (accesses the original network
 *    header regardless of pskb_pull) + IP header length (BPF_MSH)
 * 4. Return source port → kernel does % num_sockets → sticky per-client
 *
 * Result: sticky sessions on BOTH real network AND localhost.
 */
static struct sock_filter s_cbpf_reuseport_prog[] = {
    /* [0] A = skb->hash (rxhash) */
    BPF_STMT(BPF_LD  | BPF_W | BPF_ABS, SKF_AD_OFF + SKF_AD_RXHASH),
    /* [1] if (A != 0) goto [4] — use kernel hash when available */
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, 2),
    /* [2] X = (ip_hdr[0] & 0xf) * 4 — IP header length via SKF_NET_OFF */
    BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, SKF_NET_OFF),
    /* [3] A = *(u16*)(net_hdr + X) — UDP source port (first field after IP hdr) */
    BPF_STMT(BPF_LD  | BPF_H | BPF_IND, SKF_NET_OFF),
    /* [4] return A */
    BPF_STMT(BPF_RET | BPF_A, 0),
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
    
    debug_if(s_debug_more, L_DEBUG, "Classic BPF detached from socket %d", socket_fd);
    return 0;
#else
    log_it(L_WARNING, "SO_DETACH_REUSEPORT_BPF not available");
    return -1;
#endif
}
