/**
 * @file dap_io_flow_ebpf.c
 * @brief eBPF-based SO_REUSEPORT consistent hashing for UDP flows
 * 
 * Implements PROPER sticky sessions using FNV-1a hash on (src_ip, src_port)
 * to ensure packets from the same client ALWAYS arrive on the same worker thread.
 * 
 * Requires: Linux kernel 4.15+ (for SO_ATTACH_REUSEPORT_EBPF) + CAP_BPF/CAP_NET_ADMIN
 * Falls back to: DISABLING sharding (single socket) if eBPF unavailable
 * 
 * CRITICAL: Kernel's default SO_REUSEPORT hashing is NOT consistent for UDP!
 *           Without this eBPF program, multiple flows will be created for same client.
 */

#include <sys/syscall.h>
#include <sys/socket.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "dap_common.h"
#include "dap_io_flow_ebpf.h"

#define LOG_TAG "dap_io_flow_ebpf"

// Fallback definitions for older kernel headers
#ifndef BPF_PROG_TYPE_SK_REUSEPORT
#define BPF_PROG_TYPE_SK_REUSEPORT 14
#endif

#ifndef SO_ATTACH_REUSEPORT_EBPF
#define SO_ATTACH_REUSEPORT_EBPF 51
#endif

#ifndef SO_DETACH_REUSEPORT_BPF
#define SO_DETACH_REUSEPORT_BPF 52
#endif

// eBPF availability flag
static bool s_ebpf_available = false;
static bool s_ebpf_checked = false;

/**
 * @brief eBPF program for SO_REUSEPORT sticky sessions using FNV-1a hash
 * 
 * This program implements CONSISTENT HASHING based on (src_ip, src_port):
 * 1. Извлекает src_ip и src_port из пакета
 * 2. Вычисляет FNV-1a hash: hash = ((hash ^ byte) * FNV_PRIME) для каждого байта
 * 3. Возвращает hash % num_workers
 * 
 * FNV-1a constants:
 * - FNV_OFFSET_BASIS = 2166136261 (0x811c9dc5)
 * - FNV_PRIME = 16777619 (0x01000193)
 * 
 * Instructions are raw struct bpf_insn because:
 * - Kernel headers don't provide BPF_MOV64_REG and similar macros
 * - We need portable code that works across different systems
 * - Manual encoding ensures exact semantics we need
 */
static struct bpf_insn s_reuseport_ebpf_prog[] = {
    // r6 = ctx (struct sk_reuseport_md*)
    // code=0xbf (BPF_ALU64 | BPF_MOV | BPF_X), dst_reg=6, src_reg=1
    {.code = 0xbf, .dst_reg = 6, .src_reg = 1, .off = 0, .imm = 0},
    
    // Load eth_protocol (offset 12 in sk_reuseport_md)
    // r7 = ctx->eth_protocol
    // code=0x61 (BPF_LDX | BPF_W | BPF_MEM)
    {.code = 0x61, .dst_reg = 7, .src_reg = 6, .off = 12, .imm = 0},
    
    // Check if IPv4 (ETH_P_IP = 0x0800, in network byte order = 0x0008)
    // r8 = 0x0008
    // code=0xb7 (BPF_ALU | BPF_MOV | BPF_K)
    {.code = 0xb7, .dst_reg = 8, .src_reg = 0, .off = 0, .imm = 0x0008},
    
    // if (r7 != r8) goto +17 (skip IPv4 PATH, go to IPv6 PATH)
    // code=0x5d (BPF_JMP | BPF_JNE | BPF_X)
    {.code = 0x5d, .dst_reg = 7, .src_reg = 8, .off = 17, .imm = 0},  // FIXED: 17 инструкций до IPv6_PATH
    
    // === IPv4 PATH ===
    // Load data pointer (offset 0 in sk_reuseport_md)
    // r2 = ctx->data
    {.code = 0x79, .dst_reg = 2, .src_reg = 6, .off = 0, .imm = 0},
    
    // Load data_end pointer (offset 8 in sk_reuseport_md)
    // r3 = ctx->data_end
    {.code = 0x79, .dst_reg = 3, .src_reg = 6, .off = 8, .imm = 0},
    
    // r4 = r2 + 20 (end of IPv4 header, no options)
    // code=0x07 (BPF_ALU64 | BPF_ADD | BPF_K)
    {.code = 0x07, .dst_reg = 4, .src_reg = 0, .off = 0, .imm = 20},
    
    // if (r4 > r3) goto error (packet too small)
    // code=0x2d (BPF_JMP | BPF_JGT | BPF_X)
    {.code = 0x2d, .dst_reg = 4, .src_reg = 3, .off = 32, .imm = 0},  // FIXED: 32 до ERROR
    
    // Load src_ip (offset 12 in IPv4 header)
    // r2 = *(u32*)(r2 + 12)
    {.code = 0x61, .dst_reg = 2, .src_reg = 2, .off = 12, .imm = 0},
    
    // === FNV-1a HASH BEGIN ===
    // r0 = FNV_OFFSET_BASIS = 0x811c9dc5
    {.code = 0xb7, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0x811c9dc5},
    
    // hash ^= src_ip (byte 0-3)
    // r0 ^= r2
    // code=0xaf (BPF_ALU | BPF_XOR | BPF_X)
    {.code = 0xaf, .dst_reg = 0, .src_reg = 2, .off = 0, .imm = 0},
    
    // hash *= FNV_PRIME = 0x01000193
    // r0 *= 0x01000193
    // code=0x27 (BPF_ALU | BPF_MUL | BPF_K)
    {.code = 0x27, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0x01000193},
    
    // Now load src_port from UDP header
    // r4 = ctx->data
    {.code = 0x79, .dst_reg = 4, .src_reg = 6, .off = 0, .imm = 0},
    
    // r4 += 20 (skip IPv4 header)
    {.code = 0x07, .dst_reg = 4, .src_reg = 0, .off = 0, .imm = 20},
    
    // r5 = r4 + 2 (end of src_port)
    {.code = 0xbf, .dst_reg = 5, .src_reg = 4, .off = 0, .imm = 0},
    {.code = 0x07, .dst_reg = 5, .src_reg = 0, .off = 0, .imm = 2},
    
    // if (r5 > r3) goto error
    {.code = 0x2d, .dst_reg = 5, .src_reg = 3, .off = 23, .imm = 0},  // FIXED: 23 до ERROR
    
    // Load src_port (offset 0 in UDP header, 16-bit)
    // r4 = *(u16*)(r4 + 0)
    // code=0x69 (BPF_LDX | BPF_H | BPF_MEM)
    {.code = 0x69, .dst_reg = 4, .src_reg = 4, .off = 0, .imm = 0},
    
    // hash ^= src_port
    {.code = 0xaf, .dst_reg = 0, .src_reg = 4, .off = 0, .imm = 0},
    
    // hash *= FNV_PRIME
    {.code = 0x27, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0x01000193},
    
    // Jump to return
    // code=0x05 (BPF_JMP | BPF_JA)
    {.code = 0x05, .dst_reg = 0, .src_reg = 0, .off = 18, .imm = 0},  // FIXED: 18 до RETURN
    
    // === IPv6 PATH ===
    // Check if IPv6 (ETH_P_IPV6 = 0x86DD, in network byte order = 0xDD86)
    // r8 = 0xDD86
    {.code = 0xb7, .dst_reg = 8, .src_reg = 0, .off = 0, .imm = 0xDD86},
    
    // if (r7 != r8) goto error (not IPv4, not IPv6)
    {.code = 0x5d, .dst_reg = 7, .src_reg = 8, .off = 17, .imm = 0},  // FIXED: 17 до ERROR
    
    // Load data pointer
    // r2 = ctx->data
    {.code = 0x79, .dst_reg = 2, .src_reg = 6, .off = 0, .imm = 0},
    
    // Load data_end
    // r3 = ctx->data_end
    {.code = 0x79, .dst_reg = 3, .src_reg = 6, .off = 8, .imm = 0},
    
    // r4 = r2 + 40 (end of IPv6 header)
    {.code = 0x07, .dst_reg = 4, .src_reg = 0, .off = 0, .imm = 40},
    
    // if (r4 > r3) goto error
    {.code = 0x2d, .dst_reg = 4, .src_reg = 3, .off = 13, .imm = 0},  // FIXED: 13 до ERROR
    
    // Load first 4 bytes of src_ipv6 (offset 8 in IPv6 header)
    // r2 = *(u32*)(r2 + 8)
    {.code = 0x61, .dst_reg = 2, .src_reg = 2, .off = 8, .imm = 0},
    
    // FNV-1a hash (same as IPv4)
    // r0 = FNV_OFFSET_BASIS
    {.code = 0xb7, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0x811c9dc5},
    
    // hash ^= src_ipv6[0:3]
    {.code = 0xaf, .dst_reg = 0, .src_reg = 2, .off = 0, .imm = 0},
    
    // hash *= FNV_PRIME
    {.code = 0x27, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0x01000193},
    
    // Load src_port from UDP header (IPv6 header is 40 bytes)
    // r4 = ctx->data + 40
    {.code = 0x79, .dst_reg = 4, .src_reg = 6, .off = 0, .imm = 0},
    {.code = 0x07, .dst_reg = 4, .src_reg = 0, .off = 0, .imm = 40},
    
    // r5 = r4 + 2
    {.code = 0xbf, .dst_reg = 5, .src_reg = 4, .off = 0, .imm = 0},
    {.code = 0x07, .dst_reg = 5, .src_reg = 0, .off = 0, .imm = 2},
    
    // if (r5 > r3) goto error
    {.code = 0x2d, .dst_reg = 5, .src_reg = 3, .off = 4, .imm = 0},  // FIXED: 4 до ERROR
    
    // Load src_port
    {.code = 0x69, .dst_reg = 4, .src_reg = 4, .off = 0, .imm = 0},
    
    // hash ^= src_port
    {.code = 0xaf, .dst_reg = 0, .src_reg = 4, .off = 0, .imm = 0},
    
    // hash *= FNV_PRIME
    {.code = 0x27, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0x01000193},
    
    // === RETURN HASH ===
    // Exit (return r0 = hash)
    // Kernel automatically does: socket_index = return_value % num_sockets
    // code=0x95 (BPF_JMP | BPF_EXIT)
    {.code = 0x95, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0},
    
    // === ERROR PATH ===
    // Return 0 (let kernel decide)
    {.code = 0xb7, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0},
    {.code = 0x95, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0},
};

/**
 * @brief Wrapper for BPF syscall
 */
static int bpf_syscall(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

/**
 * @brief Load eBPF program into kernel
 * 
 * @return BPF program fd or -1 on error
 */
static int dap_io_flow_ebpf_load_prog(void)
{
    union bpf_attr attr = {0};
    char log_buf[8192];
    
    attr.prog_type = BPF_PROG_TYPE_SK_REUSEPORT;
    attr.insns = (uint64_t)(uintptr_t)s_reuseport_ebpf_prog;
    attr.insn_cnt = sizeof(s_reuseport_ebpf_prog) / sizeof(struct bpf_insn);
    attr.license = (uint64_t)(uintptr_t)"GPL";
    attr.log_buf = (uint64_t)(uintptr_t)log_buf;
    attr.log_size = sizeof(log_buf);
    attr.log_level = 1;
    attr.kern_version = 0;  // Not required for SK_REUSEPORT
    
    int prog_fd = bpf_syscall(BPF_PROG_LOAD, &attr, sizeof(attr));
    
    if (prog_fd < 0) {
        log_it(L_ERROR, "Failed to load eBPF program: %s (errno=%d)", 
               strerror(errno), errno);
        if (log_buf[0]) {
            log_it(L_WARNING, "eBPF verifier log:\n%s", log_buf);
        }
        
        // Specific error messages for common issues
        if (errno == EPERM) {
            log_it(L_ERROR, "Permission denied - need CAP_BPF or CAP_NET_ADMIN capability");
            log_it(L_NOTICE, "Run as root or grant capability: setcap cap_bpf,cap_net_admin=ep <binary>");
        } else if (errno == ENOSYS) {
            log_it(L_ERROR, "BPF syscall not available - kernel too old (need 4.15+)");
        }
    } else {
        log_it(L_NOTICE, "eBPF consistent hashing program loaded successfully (fd=%d)", prog_fd);
        log_it(L_DEBUG, "Program: FNV-1a hash on (src_ip, src_port), %zu instructions",
               sizeof(s_reuseport_ebpf_prog) / sizeof(struct bpf_insn));
    }
    
    return prog_fd;
}

/**
 * @brief Check if eBPF is available on this system
 */
bool dap_io_flow_ebpf_is_available(void)
{
    if (s_ebpf_checked) {
        return s_ebpf_available;
    }
    
    s_ebpf_checked = true;
    
    // Try to load eBPF program to check availability
    int prog_fd = dap_io_flow_ebpf_load_prog();
    
    if (prog_fd >= 0) {
        close(prog_fd);
        s_ebpf_available = true;
        log_it(L_NOTICE, "eBPF sticky sessions: AVAILABLE (kernel 4.15+, CAP_BPF)");
    } else {
        s_ebpf_available = false;
        log_it(L_CRITICAL, "eBPF sticky sessions: NOT AVAILABLE");
        log_it(L_CRITICAL, "UDP sharding will be DISABLED to prevent duplicate flows");
    }
    
    return s_ebpf_available;
}

/**
 * @brief Attach eBPF program to SO_REUSEPORT socket group
 * 
 * @param socket_fd One of the SO_REUSEPORT sockets (any from the group)
 * @return 0 on success, -1 on error
 */
int dap_io_flow_ebpf_attach_socket(int socket_fd)
{
    if (!dap_io_flow_ebpf_is_available()) {
        log_it(L_ERROR, "eBPF not available, cannot attach");
        return -1;
    }
    
    // Load eBPF program
    int prog_fd = dap_io_flow_ebpf_load_prog();
    if (prog_fd < 0) {
        return -1;
    }
    
    // Attach to socket via SO_ATTACH_REUSEPORT_EBPF
    if (setsockopt(socket_fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF, 
                   &prog_fd, sizeof(prog_fd)) < 0) {
        log_it(L_ERROR, "Failed to attach eBPF to SO_REUSEPORT socket: %s", 
               strerror(errno));
        close(prog_fd);
        return -1;
    }
    
    log_it(L_NOTICE, "eBPF consistent hashing attached to socket %d", socket_fd);
    log_it(L_NOTICE, "Packets from same (src_ip, src_port) -> same worker (guaranteed)");
    
    // Socket now owns the program, we can close our fd
    close(prog_fd);
    
    return 0;
}

/**
 * @brief Detach eBPF program from socket (cleanup)
 * 
 * @param socket_fd Socket to detach from
 * @return 0 on success, -1 on error
 */
int dap_io_flow_ebpf_detach_socket(int socket_fd)
{
    if (setsockopt(socket_fd, SOL_SOCKET, SO_DETACH_REUSEPORT_BPF, 
                   NULL, 0) < 0) {
        log_it(L_WARNING, "Failed to detach eBPF from socket %d: %s", 
               socket_fd, strerror(errno));
        return -1;
    }
    
    log_it(L_DEBUG, "eBPF program detached from socket %d", socket_fd);
    return 0;
}

/**
 * @brief Check if classic BPF is available
 * 
 * Classic BPF (SO_ATTACH_BPF) available since Linux 3.9
 */
bool dap_io_flow_classic_bpf_is_available(void)
{
#ifdef SO_ATTACH_BPF
    // Classic BPF with SO_ATTACH_BPF available
    // This is older API but still works for REUSEPORT load balancing
    return true;
#else
    return false;
#endif
}

/**
 * @brief Classic BPF program for SO_REUSEPORT load balancing
 * 
 * Simpler than eBPF - just uses raw_smp_processor_id() hash
 * Not as good as eBPF's FNV-1a but better than kernel default
 */
int dap_io_flow_classic_bpf_attach_socket(int socket_fd)
{
#ifdef SO_ATTACH_BPF
    // Classic BPF program: simple hash on src_port
    // Note: Classic BPF is limited compared to eBPF but works for basic load balancing
    struct sock_filter code[] = {
        // Load src_port from skb (offset 0 = src_port for REUSEPORT context)
        { 0x20, 0, 0, 0x00000000 },  // ld [0]  (load src_port)
        // Return src_port as hash (kernel will mod by num_sockets)
        { 0x06, 0, 0, 0x00000000 },  // ret a
    };
    
    struct sock_fprog prog = {
        .len = sizeof(code) / sizeof(code[0]),
        .filter = code,
    };
    
    // Attach via SO_ATTACH_BPF (older API, but works)
    if (setsockopt(socket_fd, SOL_SOCKET, SO_ATTACH_BPF, &prog, sizeof(prog)) < 0) {
        log_it(L_ERROR, "Failed to attach classic BPF: %s", strerror(errno));
        return -1;
    }
    
    log_it(L_NOTICE, "Classic BPF load balancing attached to socket %d", socket_fd);
    log_it(L_NOTICE, "Using src_port hash for distribution (basic sticky sessions)");
    return 0;
#else
    log_it(L_ERROR, "SO_ATTACH_BPF not available on this platform");
    return -1;
#endif
}

/**
 * @brief Detect best available load balancing tier
 * 
 * Note: Classic BPF (SO_ATTACH_BPF) does NOT support SO_REUSEPORT!
 * Only eBPF (SO_ATTACH_REUSEPORT_EBPF) provides kernel-level sticky sessions.
 */
dap_io_flow_lb_tier_t dap_io_flow_detect_lb_tier(void)
{
    // Try eBPF (SO_ATTACH_REUSEPORT_EBPF - the ONLY kernel-level option for SO_REUSEPORT)
    if (dap_io_flow_ebpf_is_available()) {
        log_it(L_NOTICE, "🚀 Load balancing: Tier 2 (eBPF) - Kernel sticky sessions with FNV-1a");
        return DAP_IO_FLOW_LB_TIER_EBPF;
    }
    
    // Fallback to application-level (manual distribution via queues)
    log_it(L_NOTICE, "📦 Load balancing: Tier 1 (Application-level) - Queue-based distribution");
    return DAP_IO_FLOW_LB_TIER_APPLICATION;
}

