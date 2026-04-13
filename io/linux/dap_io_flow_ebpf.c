/**
 * @file dap_io_flow_ebpf.c
 * @brief eBPF-based SO_REUSEPORT consistent hashing for UDP flows
 * 
 * Implements kernel-level sticky sessions using BPF_PROG_TYPE_SK_REUSEPORT.
 * 
 * Requires: 
 * - Linux kernel 4.15+ (for SO_ATTACH_REUSEPORT_EBPF)
 * - Modern kernel headers with BPF support
 * - CAP_BPF or CAP_NET_ADMIN capability
 * 
 * Falls back to: Application-level load balancing if eBPF unavailable
 * 
 * CRITICAL: Kernel's default SO_REUSEPORT hashing is NOT consistent for UDP!
 *           This eBPF program ensures sticky sessions: same client → same worker.
 */

#include <sys/syscall.h>
#include <sys/socket.h>
#include <linux/bpf.h>
#include <linux/bpf_common.h>
#include <linux/filter.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_io_flow_ebpf.h"

#define LOG_TAG "dap_io_flow_ebpf"

static bool s_debug_more = false;
// eBPF availability flag
static bool s_ebpf_available = false;
static bool s_ebpf_checked = false;

/**
 * @brief eBPF program for SO_REUSEPORT hash-based socket selection
 * 
 * PROBLEM: sk_reuseport_md->hash is 0 for loopback/local traffic!
 * SOLUTION: Read source port from UDP header (via data pointer) and use it as hash.
 * 
 * sk_reuseport_md structure (from linux/bpf.h):
 * - offset 0:  data (void*)     - points to UDP header start
 * - offset 8:  data_end (void*)
 * - offset 16: len (u32)
 * - offset 20: eth_protocol (u32)
 * - offset 24: ip_protocol (u32)
 * - offset 28: bind_inany (u32)
 * - offset 32: hash (u32)       - kernel hash (0 on loopback!)
 * 
 * UDP header layout (data points here):
 * - offset 0: source port (2 bytes, network byte order)
 * - offset 2: dest port (2 bytes)
 * - offset 4: length (2 bytes)
 * - offset 6: checksum (2 bytes)
 * 
 * Algorithm:
 * 1. Load kernel hash - if non-zero (real network), use it for full 4-tuple sticky
 * 2. If hash == 0 (loopback), read source port from UDP header
 * 3. Return hash value, kernel does socket_index = hash % num_sockets
 * 
 * Result: STICKY SESSIONS on both real network AND localhost!
 * - Real network: full 4-tuple hash
 * - Localhost: source port hash (same client port → same worker)
 */

// eBPF instruction macros (prefixed to avoid conflicts with linux/bpf.h)
#define EBPF_LD_MEM(SIZE, DST, SRC, OFF) \
    ((struct bpf_insn){.code = BPF_LDX | BPF_MEM | (SIZE), .dst_reg = DST, .src_reg = SRC, .off = OFF, .imm = 0})
#define EBPF_MOV64_REG(DST, SRC) \
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MOV | BPF_X, .dst_reg = DST, .src_reg = SRC, .off = 0, .imm = 0})
#define EBPF_JNE_IMM(DST, IMM, OFF) \
    ((struct bpf_insn){.code = BPF_JMP | BPF_JNE | BPF_K, .dst_reg = DST, .src_reg = 0, .off = OFF, .imm = IMM})
#define EBPF_EXIT_INSN() \
    ((struct bpf_insn){.code = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = 0})

static struct bpf_insn s_reuseport_ebpf_prog[] = {
    // r6 = ctx (save context pointer)
    EBPF_MOV64_REG(6, 1),
    
    // r0 = *(u32*)(ctx + 32) - load kernel hash
    EBPF_LD_MEM(BPF_W, 0, 6, 32),
    
    // if (r0 != 0) goto exit - use kernel hash if available
    // Jump offset: target = PC+1+offset = 2+1+3 = 6 (exit instruction)
    EBPF_JNE_IMM(0, 0, 3),
    
    // Kernel hash is 0 (loopback) - read source port from UDP header
    // r2 = *(u64*)(ctx + 0) - load data pointer (points to UDP header)
    EBPF_LD_MEM(BPF_DW, 2, 6, 0),
    
    // r0 = *(u16*)(data + 0) - load source port (first 2 bytes of UDP header)
    // Note: This is in network byte order, but that's fine for hashing
    EBPF_LD_MEM(BPF_H, 0, 2, 0),
    
    // Multiply by prime for better distribution: r0 = r0 * 2654435761
    // Using ALU64 with immediate multiplication
    ((struct bpf_insn){.code = BPF_ALU64 | BPF_MUL | BPF_K, .dst_reg = 0, .src_reg = 0, .off = 0, .imm = (int)2654435761U}),
    
    // exit - return r0 (hash value)
    // Kernel does: socket_index = r0 % num_sockets
    EBPF_EXIT_INSN(),
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
    char log_buf[8192] = {0};
    
    attr.prog_type = BPF_PROG_TYPE_SK_REUSEPORT;
    attr.insns = (uint64_t)(uintptr_t)s_reuseport_ebpf_prog;
    attr.insn_cnt = sizeof(s_reuseport_ebpf_prog) / sizeof(struct bpf_insn);
    attr.license = (uint64_t)(uintptr_t)"GPL";
    attr.log_buf = (uint64_t)(uintptr_t)log_buf;
    attr.log_size = sizeof(log_buf);
    attr.log_level = 1;
    
    int prog_fd = bpf_syscall(BPF_PROG_LOAD, &attr, sizeof(attr));
    
    if (prog_fd < 0) {
        log_it(L_NOTICE, "Failed to load eBPF program: %s (errno=%d)",
               strerror(errno), errno);
        if (log_buf[0]) {
            log_it(L_DEBUG, "eBPF verifier log:\n%s", log_buf);
        }
        
        if (errno == EPERM || errno == EACCES) {
            log_it(L_NOTICE, "Permission denied - need CAP_BPF or CAP_NET_ADMIN");
            log_it(L_NOTICE, "Run as root or: sudo setcap cap_bpf,cap_net_admin=ep <binary>");
        } else if (errno == ENOSYS) {
            log_it(L_NOTICE, "BPF syscall not available - kernel too old (need 4.15+)");
        }
    } else {
        log_it(L_NOTICE, "✅ eBPF program loaded successfully (fd=%d, %u instructions)",
               prog_fd, attr.insn_cnt);
        log_it(L_NOTICE, "Kernel hash-based sticky sessions enabled (same client → same worker)");
    }
    
    return prog_fd;
}

/**
 * @brief Check if eBPF is available on this system
 * 
 * Tests BOTH program loading AND SO_ATTACH_REUSEPORT_EBPF support.
 * Some kernels support BPF syscall but NOT SO_ATTACH_REUSEPORT_EBPF.
 */
bool dap_io_flow_ebpf_is_available(void)
{
    if (s_ebpf_checked) {
        return s_ebpf_available;
    }
    
    s_ebpf_checked = true;
    
    // Step 1: Try to load eBPF program
    int prog_fd = dap_io_flow_ebpf_load_prog();
    if (prog_fd < 0) {
        s_ebpf_available = false;
        log_it(L_NOTICE, "❌ eBPF program load failed");
        return false;
    }
    
    // Step 2: Test SO_ATTACH_REUSEPORT_EBPF support with dummy socket
    // CRITICAL: attach MUST happen BEFORE bind (kernel requires sk_unhashed())
    int test_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (test_sock < 0) {
        close(prog_fd);
        s_ebpf_available = false;
        return false;
    }
    
    int opt = 1;
    setsockopt(test_sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    
    // Attach eBPF BEFORE bind - kernel requirement for reuseport_attach_prog()
    int attach_ret = setsockopt(test_sock, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF,
                                 &prog_fd, sizeof(prog_fd));
    
    if (attach_ret < 0) {
        int l_errno = errno;
        close(test_sock);
        close(prog_fd);
        s_ebpf_available = false;
        log_it(L_WARNING, "SO_ATTACH_REUSEPORT_EBPF not supported: %s (errno=%d)",
               strerror(l_errno), l_errno);
        return false;
    }
    
    // Verify bind works after attach
    struct sockaddr_in test_addr = {
        .sin_family = AF_INET,
        .sin_port = 0,  // Kernel picks port
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
    };
    
    if (bind(test_sock, (struct sockaddr*)&test_addr, sizeof(test_addr)) < 0) {
        log_it(L_WARNING, "Test socket bind after eBPF attach failed: %s", strerror(errno));
        close(test_sock);
        close(prog_fd);
        s_ebpf_available = false;
        return false;
    }
    
    close(test_sock);
    close(prog_fd);
    
    // Both attach and bind succeeded
    s_ebpf_available = true;
    log_it(L_NOTICE, "eBPF sticky sessions: AVAILABLE (attach before bind verified)");
    return true;
}

/**
 * @brief Attach eBPF program to SO_REUSEPORT socket group
 * 
 * @param socket_fd One of the SO_REUSEPORT sockets (program attaches to entire group)
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
        log_it(L_ERROR, "Failed to load eBPF program for attach");
        return -1;
    }
    
    // Attach to SO_REUSEPORT socket group
    if (setsockopt(socket_fd, SOL_SOCKET, SO_ATTACH_REUSEPORT_EBPF, 
                   &prog_fd, sizeof(prog_fd)) < 0) {
        log_it(L_ERROR, "Failed to attach eBPF to SO_REUSEPORT socket %d: %s", 
               socket_fd, strerror(errno));
        close(prog_fd);
        return -1;
    }
    
    log_it(L_NOTICE, "✅ eBPF program attached to SO_REUSEPORT group (socket=%d)", socket_fd);
    log_it(L_NOTICE, "Sticky sessions enabled: kernel hash → consistent worker selection");
    
    // Socket now owns the program
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

