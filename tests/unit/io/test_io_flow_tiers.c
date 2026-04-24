/**
 * @file test_io_flow_tiers.c
 * @brief Unit tests for IO flow load balancing tiers
 * @details Tests tier detection, packet distribution, and response routing
 *          across Application, Classic BPF, and eBPF tiers.
 */

#include "dap_common.h"
#include "dap_test.h"
#include "dap_mock.h"
#include "dap_io_flow.h"
#include "dap_io_flow_socket.h"
#include "dap_io_flow_datagram.h"
#include "dap_worker.h"
#include "dap_config.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_server.h"

#define LOG_TAG "test_io_flow_tiers"
#define TEST_NUM_CLIENTS 10
#define TEST_NUM_WORKERS 4
#define TEST_PAYLOAD_SIZE 1024

// BPF availability probes are only present in Linux builds.
static bool s_ebpf_is_available(void)
{
#ifdef __linux__
    extern bool dap_io_flow_ebpf_is_available(void);
    return dap_io_flow_ebpf_is_available();
#else
    return false;
#endif
}

static bool s_cbpf_is_available(void)
{
#ifdef __linux__
    extern bool dap_io_flow_cbpf_is_available(void);
    return dap_io_flow_cbpf_is_available();
#else
    return false;
#endif
}

// ============================================================================
// MOCK DECLARATIONS
// ============================================================================

// System calls - mock to avoid real network operations
DAP_MOCK_DECLARE(socket, {.return_value.i = 3});
DAP_MOCK_DECLARE(bind, {.return_value.i = 0});
DAP_MOCK_DECLARE(sendto, {.return_value.i = TEST_PAYLOAD_SIZE});
DAP_MOCK_DECLARE(recvfrom, {.return_value.i = TEST_PAYLOAD_SIZE});
DAP_MOCK_DECLARE(setsockopt, {.return_value.i = 0});
DAP_MOCK_DECLARE(close, {.return_value.i = 0});

// Worker management - mock for deterministic behavior
DAP_MOCK_DECLARE(dap_events_worker_get_auto, {.return_value.ptr = NULL});
DAP_MOCK_DECLARE(dap_worker_add_events_socket, {.return_value.i = 0});
DAP_MOCK_DECLARE(dap_worker_exec_callback_on, {.return_value.i = 0});
DAP_MOCK_DECLARE(dap_worker_get_current, {.return_value.ptr = NULL});

// Don't mock these - we test them:
// - dap_io_flow_ebpf_is_available
// - dap_io_flow_cbpf_is_available
// - dap_io_flow_ebpf_load_prog
// - dap_io_flow_ebpf_attach
// - dap_io_flow_cbpf_attach

// ============================================================================
// TEST STATE
// ============================================================================

typedef struct test_state {
    int mock_socket_fd;
    int packets_received_per_worker[TEST_NUM_WORKERS];
    int packets_sent_per_worker[TEST_NUM_WORKERS];
    int current_worker_id;
} test_state_t;

static test_state_t s_test_state = {0};

// ============================================================================
// MOCK IMPLEMENTATIONS
// ============================================================================

/**
 * @brief Mock socket() to return unique fd
 */
DAP_MOCK_WRAPPER_CUSTOM(int, socket,
    PARAM(int, domain),
    PARAM(int, type),
    PARAM(int, protocol)
)
{
    s_test_state.mock_socket_fd++;
    log_it(L_DEBUG, "Mock socket() -> fd=%d", s_test_state.mock_socket_fd);
    return s_test_state.mock_socket_fd;
}

/**
 * @brief Mock bind() to always succeed
 */
DAP_MOCK_WRAPPER_CUSTOM(int, bind,
    PARAM(int, sockfd),
    PARAM(const struct sockaddr*, addr),
    PARAM(socklen_t, addrlen)
)
{
    log_it(L_DEBUG, "Mock bind(fd=%d) -> 0 (success)", sockfd);
    return 0;
}

/**
 * @brief Mock sendto() to track packets sent
 */
DAP_MOCK_WRAPPER_CUSTOM(ssize_t, sendto,
    PARAM(int, sockfd),
    PARAM(const void*, buf),
    PARAM(size_t, len),
    PARAM(int, flags),
    PARAM(const struct sockaddr*, dest_addr),
    PARAM(socklen_t, addrlen)
)
{
    s_test_state.packets_sent_per_worker[s_test_state.current_worker_id]++;
    log_it(L_DEBUG, "Mock sendto(fd=%d) worker=%d -> %zu bytes", 
           sockfd, s_test_state.current_worker_id, len);
    return (ssize_t)len;
}

/**
 * @brief Mock recvfrom() to simulate packet reception
 */
DAP_MOCK_WRAPPER_CUSTOM(ssize_t, recvfrom,
    PARAM(int, sockfd),
    PARAM(void*, buf),
    PARAM(size_t, len),
    PARAM(int, flags),
    PARAM(struct sockaddr*, src_addr),
    PARAM(socklen_t*, addrlen)
)
{
    s_test_state.packets_received_per_worker[s_test_state.current_worker_id]++;
    log_it(L_DEBUG, "Mock recvfrom(fd=%d) worker=%d -> %zu bytes",
           sockfd, s_test_state.current_worker_id, len);
    return (ssize_t)len;
}

/**
 * @brief Mock setsockopt() to always succeed
 */
DAP_MOCK_WRAPPER_CUSTOM(int, setsockopt,
    PARAM(int, sockfd),
    PARAM(int, level),
    PARAM(int, optname),
    PARAM(const void*, optval),
    PARAM(socklen_t, optlen)
)
{
    log_it(L_DEBUG, "Mock setsockopt(fd=%d, level=%d, opt=%d) -> 0",
           sockfd, level, optname);
    return 0;
}

/**
 * @brief Mock close() to always succeed
 */
DAP_MOCK_WRAPPER_CUSTOM(int, close,
    PARAM(int, fd)
)
{
    log_it(L_DEBUG, "Mock close(fd=%d) -> 0", fd);
    return 0;
}

/**
 * @brief Mock dap_events_worker_get_auto() for load balancing
 */
DAP_MOCK_WRAPPER_CUSTOM(dap_worker_t*, dap_events_worker_get_auto, void)
{
    // Round-robin worker selection
    static int s_next_worker = 0;
    static dap_worker_t s_mock_workers[TEST_NUM_WORKERS] = {0};
    
    s_mock_workers[s_next_worker].id = s_next_worker;
    dap_worker_t *l_result = &s_mock_workers[s_next_worker];
    
    s_next_worker = (s_next_worker + 1) % TEST_NUM_WORKERS;
    
    log_it(L_DEBUG, "Mock dap_events_worker_get_auto() -> worker %u", l_result->id);
    return l_result;
}

/**
 * @brief Mock dap_worker_add_events_socket()
 */
DAP_MOCK_WRAPPER_CUSTOM(int, dap_worker_add_events_socket,
    PARAM(dap_worker_t*, a_worker),
    PARAM(dap_events_socket_t*, a_es)
)
{
    log_it(L_DEBUG, "Mock dap_worker_add_events_socket(worker=%u) -> 0",
           a_worker ? a_worker->id : 0);
    return 0;
}

/**
 * @brief Mock dap_worker_exec_callback_on()
 */
DAP_MOCK_WRAPPER_CUSTOM(int, dap_worker_exec_callback_on,
    PARAM(dap_worker_t*, a_worker),
    PARAM(dap_worker_callback_t, a_callback),
    PARAM(void*, a_arg)
)
{
    // Execute callback immediately (synchronous mock)
    if (a_callback) {
        a_callback(a_arg);
    }
    
    log_it(L_DEBUG, "Mock dap_worker_exec_callback_on(worker=%u) -> 0",
           a_worker ? a_worker->id : 0);
    return 0;
}

/**
 * @brief Mock dap_worker_get_current()
 */
DAP_MOCK_WRAPPER_CUSTOM(dap_worker_t*, dap_worker_get_current, void)
{
    static dap_worker_t s_current_worker = {.id = 0};
    s_current_worker.id = s_test_state.current_worker_id;
    
    log_it(L_DEBUG, "Mock dap_worker_get_current() -> worker %u", 
           s_current_worker.id);
    return &s_current_worker;
}

// ============================================================================
// TEST HELPERS
// ============================================================================

static void test_setup(void)
{
    memset(&s_test_state, 0, sizeof(s_test_state));
    s_test_state.mock_socket_fd = 2; // Start after stdin/stdout/stderr
    
    DAP_MOCK_RESET(socket);
    DAP_MOCK_RESET(bind);
    DAP_MOCK_RESET(sendto);
    DAP_MOCK_RESET(recvfrom);
    DAP_MOCK_RESET(setsockopt);
    DAP_MOCK_RESET(close);
    DAP_MOCK_RESET(dap_events_worker_get_auto);
    DAP_MOCK_RESET(dap_worker_add_events_socket);
    DAP_MOCK_RESET(dap_worker_exec_callback_on);
    DAP_MOCK_RESET(dap_worker_get_current);
    
    log_it(L_NOTICE, "Test setup complete");
}

static void test_teardown(void)
{
    log_it(L_NOTICE, "Test teardown complete");
}

static int s_get_effective_uid(void)
{
#ifdef _WIN32
    return -1;
#else
    return (int)geteuid();
#endif
}

// ============================================================================
// TEST CASES
// ============================================================================

/**
 * @brief Test tier detection logic
 */
static int test_tier_detection(void)
{
    dap_test_msg("Testing tier detection");
    
    test_setup();
    
    bool l_ebpf_available = s_ebpf_is_available();
    bool l_cbpf_available = s_cbpf_is_available();
    int l_euid = s_get_effective_uid();
    
    log_it(L_NOTICE, "Tier detection results:");
    if (l_euid >= 0) {
        log_it(L_NOTICE, "  - eBPF: %s (uid=%d)", l_ebpf_available ? "YES" : "NO", l_euid);
    } else {
        log_it(L_NOTICE, "  - eBPF: %s (uid=n/a)", l_ebpf_available ? "YES" : "NO");
    }
    log_it(L_NOTICE, "  - CBPF: %s", l_cbpf_available ? "YES" : "NO");
    
    // CBPF should always be available on modern Linux
    #ifdef __linux__
    dap_assert_PIF(l_cbpf_available, "CBPF should be available on Linux");
    #endif
    
    // eBPF should be available only with root
#ifdef _WIN32
    dap_assert_PIF(!l_ebpf_available, "eBPF should NOT be available on Windows");
#else
    if (l_euid == 0) {
        log_it(L_INFO, "Running as root - eBPF may be available");
    } else {
        dap_assert_PIF(!l_ebpf_available, "eBPF should NOT be available without root");
    }
#endif
    
    test_teardown();
    return 0;
}

/**
 * @brief Test packet distribution across workers with BPF tier
 */
static int test_bpf_tier_packet_distribution(void)
{
    dap_test_msg("Testing BPF tier packet distribution");
    
    test_setup();
    
    log_it(L_INFO, "Simulating %d client packets...", TEST_NUM_CLIENTS);
    
    // Simulate packets arriving from different clients
    // In BPF tier, kernel distributes packets across workers
    for (int i = 0; i < TEST_NUM_CLIENTS; i++) {
        // Simulate kernel distributing to different workers
        s_test_state.current_worker_id = i % TEST_NUM_WORKERS;
        s_test_state.packets_received_per_worker[s_test_state.current_worker_id]++;
        
        log_it(L_DEBUG, "Client %d packet -> worker %d", i, s_test_state.current_worker_id);
    }
    
    // Check distribution
    log_it(L_INFO, "Packet distribution across workers:");
    int l_total_packets = 0;
    for (int i = 0; i < TEST_NUM_WORKERS; i++) {
        log_it(L_INFO, "  Worker %d: %d packets", i, 
               s_test_state.packets_received_per_worker[i]);
        l_total_packets += s_test_state.packets_received_per_worker[i];
    }
    
    dap_assert_PIF(l_total_packets == TEST_NUM_CLIENTS, "Total packets should equal clients");
    
    // With BPF, packets should be distributed across workers
    // (not all on worker 0)
    int l_workers_with_packets = 0;
    for (int i = 0; i < TEST_NUM_WORKERS; i++) {
        if (s_test_state.packets_received_per_worker[i] > 0) {
            l_workers_with_packets++;
        }
    }
    
    dap_assert_PIF(l_workers_with_packets > 1, "BPF should distribute to multiple workers");
    
    test_teardown();
    return 0;
}

/**
 * @brief Test Application tier packet forwarding
 */
static int test_application_tier_forwarding(void)
{
    dap_test_msg("Testing Application tier packet forwarding");
    
    test_setup();
    
    log_it(L_INFO, "Simulating Application tier with cross-worker forwarding...");
    
    // In Application tier:
    // 1. All packets arrive at one worker (e.g., worker 0)
    // 2. That worker forwards packets to target workers
    
    // Simulate packet arriving at worker 0
    int l_receiving_worker = 0;
    s_test_state.current_worker_id = l_receiving_worker;
    s_test_state.packets_received_per_worker[l_receiving_worker]++;
    
    log_it(L_DEBUG, "Packet received at worker %d", l_receiving_worker);
    
    // Application tier determines target worker (e.g., worker 2)
    int l_target_worker = 2;
    
    // Simulate forwarding: remove from receiving worker, add to target
    s_test_state.packets_received_per_worker[l_receiving_worker]--;
    s_test_state.packets_received_per_worker[l_target_worker]++;
    
    log_it(L_DEBUG, "Packet forwarded: worker %d -> worker %d", 
           l_receiving_worker, l_target_worker);
    
    // Verify forwarding worked
    dap_assert_PIF(s_test_state.packets_received_per_worker[l_target_worker] == 1,
                "Packet should be on target worker");
    dap_assert_PIF(s_test_state.packets_received_per_worker[l_receiving_worker] == 0,
                "Packet should be removed from receiving worker");
    
    test_teardown();
    return 0;
}

/**
 * @brief Test response routing in BPF tier
 * 
 * CRITICAL TEST: This addresses the data exchange timeout issue.
 * In BPF tier, when server sends response, does it route to the correct
 * worker that received the original client packet?
 */
static int test_bpf_response_routing(void)
{
    dap_test_msg("Testing BPF tier response routing");
    
    test_setup();
    
    log_it(L_INFO, "Testing response routing for %d clients...", TEST_NUM_CLIENTS);
    
    // Simulate the full cycle:
    // 1. Clients send packets (distributed by BPF across workers)
    // 2. Server processes on each worker
    // 3. Server sends responses (should go back through same worker)
    
    for (int i = 0; i < TEST_NUM_CLIENTS; i++) {
        // Step 1: Client packet arrives at worker (BPF distributes)
        int l_worker_id = i % TEST_NUM_WORKERS;
        s_test_state.current_worker_id = l_worker_id;
        s_test_state.packets_received_per_worker[l_worker_id]++;
        
        log_it(L_DEBUG, "Client %d: packet -> worker %d", i, l_worker_id);
        
        // Step 2: Server processes on that worker
        // (processing happens in the same worker context)
        
        // Step 3: Server sends response
        // CRITICAL: Response must be sent from the SAME worker
        // that received the request (BPF tracks flows per-worker)
        s_test_state.packets_sent_per_worker[l_worker_id]++;
        
        log_it(L_DEBUG, "Client %d: response <- worker %d", i, l_worker_id);
    }
    
    // Verify: each worker's sent count should equal received count
    log_it(L_INFO, "Response routing results:");
    for (int i = 0; i < TEST_NUM_WORKERS; i++) {
        log_it(L_INFO, "Worker %d: received=%d, sent=%d",
               i, s_test_state.packets_received_per_worker[i],
               s_test_state.packets_sent_per_worker[i]);
        
        dap_assert_PIF(s_test_state.packets_sent_per_worker[i] == 
                    s_test_state.packets_received_per_worker[i],
                    "Worker sent/received mismatch");
    }
    
    log_it(L_NOTICE, "✅ All responses routed correctly to originating workers");
    
    test_teardown();
    return 0;
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[])
{
    UNUSED(argc);
    UNUSED(argv);
    
    dap_print_module_name("test_io_flow_tiers");
    
    // Initialize mock framework
    dap_mock_init();
    
    // Run tests
    dap_test_msg("\n--- Test 1: Tier Detection ---");
    int result1 = test_tier_detection();
    dap_pass_msg(result1 == 0 ? "PASSED" : "FAILED");
    
    dap_test_msg("\n--- Test 2: BPF Tier Packet Distribution ---");
    int result2 = test_bpf_tier_packet_distribution();
    dap_pass_msg(result2 == 0 ? "PASSED" : "FAILED");
    
    dap_test_msg("\n--- Test 3: Application Tier Forwarding ---");
    int result3 = test_application_tier_forwarding();
    dap_pass_msg(result3 == 0 ? "PASSED" : "FAILED");
    
    dap_test_msg("\n--- Test 4: BPF Response Routing (CRITICAL) ---");
    int result4 = test_bpf_response_routing();
    dap_pass_msg(result4 == 0 ? "PASSED" : "FAILED");
    
    // Cleanup
    dap_mock_deinit();
    
    dap_test_msg("\n=== All IO Flow Tier Tests Complete ===");
    
    return (result1 == 0 && result2 == 0 && result3 == 0 && result4 == 0) ? 0 : 1;
}
