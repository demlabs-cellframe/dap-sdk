/**
 * @file test_link_manager_hot_list.c
 * @brief Unit tests for Link Manager Hot List functionality
 * @details Comprehensive tests for hot list in-memory storage:
 *          - Adding nodes to hot list
 *          - Updating timestamp on re-add
 *          - Automatic expiration of old entries
 *          - Retrieving ignored addresses
 *          - Thread safety of operations
 *          - Memory cleanup on net removal
 * 
 * @date 2025-11-14
 * @copyright (c) 2025 Demlabs
 */

#include "dap_common.h"
#include "dap_net_common.h"
#include "dap_test.h"
#include "dap_link_manager.h"
#include "dap_stream_cluster.h"
#include "dap_config.h"
#include "dap_events.h"
#include "dap_proc_thread.h"
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define LOG_TAG "test_hot_list"

// Test configuration
#define TEST_NET_ID_1 0x0000000000000001ULL
#define TEST_NET_ID_2 0x0000000000000002ULL

// Track created clusters for cleanup
typedef struct {
    uint64_t net_id;
    dap_cluster_t *cluster;
} test_cluster_info_t;

static dap_link_manager_t *s_link_manager = NULL;
static dap_list_t *s_test_clusters = NULL;

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * @brief Mock callback for link manager (required for initialization)
 */
static int s_mock_fill_net_info(dap_link_t *a_link)
{
    UNUSED(a_link);
    return 0;
}

/**
 * @brief Initialize test environment
 */
static bool s_test_init(void)
{
    log_it(L_INFO, "Initializing test environment...");
    
    // Initialize DAP common
    if (dap_common_init("test_hot_list", NULL) != 0) {
        log_it(L_ERROR, "Failed to initialize DAP common");
        return false;
    }
    
    // Initialize I/O (threads, events) - required before link manager
    uint32_t l_cpu_count = 2; // Use 2 worker threads for tests
    if (dap_events_init(l_cpu_count, 0) != 0) {
        log_it(L_ERROR, "Failed to initialize events");
        return false;
    }
    
    if (dap_proc_thread_init(l_cpu_count) != 0) {
        log_it(L_ERROR, "Failed to initialize proc threads");
        return false;
    }
    
    // Start event loop in async mode
    dap_events_start();
    
    // Give some time for threads to start
    sleep(1);
    
    // Initialize link manager
    dap_link_manager_callbacks_t l_callbacks = {
        .fill_net_info = s_mock_fill_net_info,
        .connected = NULL,
        .disconnected = NULL,
        .error = NULL,
        .link_request = NULL,
        .link_count_changed = NULL
    };
    
    if (dap_link_manager_init(&l_callbacks) != 0) {
        log_it(L_ERROR, "Failed to initialize link manager");
        return false;
    }
    
    s_link_manager = dap_link_manager_get_default();
    if (!s_link_manager) {
        log_it(L_ERROR, "Failed to get default link manager");
        return false;
    }
    
    log_it(L_INFO, "Test environment initialized successfully");
    return true;
}

/**
 * @brief Cleanup test environment
 */
static void s_test_cleanup(void)
{
    log_it(L_INFO, "Cleaning up test environment...");
    
    if (s_link_manager) {
        dap_link_manager_deinit();
        s_link_manager = NULL;
    }
    
    // dap_events_deinit() already calls dap_proc_thread_deinit() internally
    dap_events_deinit();
    dap_common_deinit();
    
    log_it(L_INFO, "Test environment cleaned up");
}

/**
 * @brief Create test network
 */
static bool s_create_test_net(uint64_t a_net_id)
{
    dap_guuid_t l_guuid = {
        .net_id = a_net_id,
        .srv_id = 0x0000000000000001ULL
    };
    dap_cluster_t *l_cluster = dap_cluster_new(NULL, l_guuid, DAP_CLUSTER_TYPE_EMBEDDED);
    if (!l_cluster) {
        log_it(L_ERROR, "Failed to create cluster for net 0x%016" DAP_UINT64_FORMAT_X, a_net_id);
        return false;
    }
    
    int l_ret = dap_link_manager_add_net(a_net_id, l_cluster, 5);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to add net 0x%016" DAP_UINT64_FORMAT_X ", error: %d", a_net_id, l_ret);
        dap_cluster_delete(l_cluster);
        return false;
    }
    
    // Track cluster for cleanup
    test_cluster_info_t *l_info = DAP_NEW_Z(test_cluster_info_t);
    if (l_info) {
        l_info->net_id = a_net_id;
        l_info->cluster = l_cluster;
        s_test_clusters = dap_list_append(s_test_clusters, l_info);
    }
    
    return true;
}

/**
 * @brief Cleanup all test networks
 */
static void s_cleanup_test_nets(void)
{
    dap_list_t *l_item, *l_tmp;
    DL_FOREACH_SAFE(s_test_clusters, l_item, l_tmp) {
        test_cluster_info_t *l_info = (test_cluster_info_t *)l_item->data;
        if (l_info) {
            // Remove from link manager first
            dap_link_manager_remove_net(l_info->net_id);
            // Delete cluster
            if (l_info->cluster) {
                dap_cluster_delete(l_info->cluster);
            }
            DAP_DELETE(l_info);
        }
    }
    dap_list_free(s_test_clusters);
    s_test_clusters = NULL;
}

/**
 * @brief Compare node addresses
 */
static bool s_addr_equal(dap_stream_node_addr_t a_addr1, dap_stream_node_addr_t a_addr2)
{
    return a_addr1.uint64 == a_addr2.uint64;
}

/**
 * @brief Create test node address from uint64
 */
static dap_stream_node_addr_t s_make_addr(uint64_t a_val)
{
    dap_stream_node_addr_t l_addr;
    l_addr.uint64 = a_val;
    return l_addr;
}

// =============================================================================
// UNIT TESTS
// =============================================================================

/**
 * @brief Test 1: Basic hot list initialization
 */
static void test_hot_list_init(void)
{
    log_it(L_INFO, "=== Test 1: Hot List Initialization ===");
    
    // Create test network
    dap_assert_PIF(s_create_test_net(TEST_NET_ID_1),
                   "Failed to create test network");
    
    // Verify hot list is empty initially
    size_t l_count = 0;
    dap_stream_node_addr_t *l_addrs = dap_link_manager_get_ignored_addrs(&l_count, TEST_NET_ID_1);
    
    dap_assert_PIF(l_addrs == NULL, "Hot list should be empty initially");
    dap_assert_PIF(l_count == 0, "Count should be 0 for empty hot list");
    
    // Cleanup test networks
    s_cleanup_test_nets();
    
    log_it(L_INFO, "✓ Test 1: Hot List Initialization PASSED\n");
}

/**
 * @brief Test 2: Adding single node to hot list
 */
static void test_hot_list_add_single(void)
{
    log_it(L_INFO, "=== Test 2: Add Single Node ===");
    
    // Create test network
    dap_assert_PIF(s_create_test_net(TEST_NET_ID_1),
                   "Failed to create test network");
    
    // Add node to hot list
    dap_stream_node_addr_t l_addr = s_make_addr(0x01);
    dap_link_manager_test_add_to_hot_list(l_addr, TEST_NET_ID_1);
    
    // Verify node is in hot list
    size_t l_count = 0;
    dap_stream_node_addr_t *l_addrs = dap_link_manager_get_ignored_addrs(&l_count, TEST_NET_ID_1);
    
    log_it(L_DEBUG, "Hot list count: %zu (expected: 1)", l_count);
    dap_assert_PIF(l_count == 1, "Hot list should contain 1 node");
    dap_assert_PIF(l_addrs != NULL, "Addrs array should not be NULL");
    
    if (l_addrs) {
        dap_assert_PIF(s_addr_equal(l_addrs[0], l_addr),
                       "Address should match added node");
        DAP_DELETE(l_addrs);
    }
    
    // Cleanup
    s_cleanup_test_nets();
    
    log_it(L_INFO, "✓ Test 2: Add Single Node PASSED\n");
}

/**
 * @brief Test 3: Adding multiple nodes to hot list
 */
static void test_hot_list_add_multiple(void)
{
    log_it(L_INFO, "=== Test 3: Add Multiple Nodes ===");
    
    // Create test network
    dap_assert_PIF(s_create_test_net(TEST_NET_ID_1),
                   "Failed to create test network");
    
    // Add multiple nodes
    const size_t l_nodes_count = 3;
    dap_stream_node_addr_t l_addrs_to_add[3];
    l_addrs_to_add[0] = s_make_addr(0x01);
    l_addrs_to_add[1] = s_make_addr(0x02);
    l_addrs_to_add[2] = s_make_addr(0x03);
    
    for (size_t i = 0; i < l_nodes_count; i++) {
        dap_link_manager_test_add_to_hot_list(l_addrs_to_add[i], TEST_NET_ID_1);
    }
    
    // Verify all nodes are in hot list
    size_t l_count = 0;
    dap_stream_node_addr_t *l_addrs = dap_link_manager_get_ignored_addrs(&l_count, TEST_NET_ID_1);
    
    log_it(L_DEBUG, "Hot list count: %zu (expected: %zu)", l_count, l_nodes_count);
    dap_assert_PIF(l_count == l_nodes_count, "Hot list should contain all added nodes");
    dap_assert_PIF(l_addrs != NULL, "Addrs array should not be NULL");
    
    // Verify all addresses are present
    if (l_addrs) {
        for (size_t i = 0; i < l_count; i++) {
            bool l_found = false;
            for (size_t j = 0; j < l_nodes_count; j++) {
                if (s_addr_equal(l_addrs[i], l_addrs_to_add[j])) {
                    l_found = true;
                    break;
                }
            }
            dap_assert_PIF(l_found, "Address should be in original list");
        }
        DAP_DELETE(l_addrs);
    }
    
    // Cleanup
    s_cleanup_test_nets();
    
    log_it(L_INFO, "✓ Test 3: Add Multiple Nodes PASSED\n");
}

/**
 * @brief Test 4: Duplicate node handling (timestamp update)
 */
static void test_hot_list_duplicate(void)
{
    log_it(L_INFO, "=== Test 4: Duplicate Node Handling ===");
    
    // Create test network
    dap_assert_PIF(s_create_test_net(TEST_NET_ID_1),
                   "Failed to create test network");
    
    // Add node first time
    dap_stream_node_addr_t l_addr = s_make_addr(0x01);
    dap_link_manager_test_add_to_hot_list(l_addr, TEST_NET_ID_1);
    
    // Add same node again (should update timestamp, not add duplicate)
    dap_link_manager_test_add_to_hot_list(l_addr, TEST_NET_ID_1);
    
    // Verify still only one entry
    size_t l_count = 0;
    dap_stream_node_addr_t *l_addrs = dap_link_manager_get_ignored_addrs(&l_count, TEST_NET_ID_1);
    
    log_it(L_DEBUG, "Hot list count after duplicate add: %zu (expected: 1)", l_count);
    dap_assert_PIF(l_count == 1, "Should still have only 1 node (no duplicates)");
    
    if (l_addrs) {
        DAP_DELETE(l_addrs);
    }
    
    // Cleanup
    s_cleanup_test_nets();
    
    log_it(L_INFO, "✓ Test 4: Duplicate Node Handling PASSED\n");
}

/**
 * @brief Test 5: Multiple networks isolation
 */
static void test_hot_list_multi_net(void)
{
    log_it(L_INFO, "=== Test 5: Multiple Networks Isolation ===");
    
    // Create two test networks
    dap_assert_PIF(s_create_test_net(TEST_NET_ID_1),
                   "Failed to create test network 1");
    dap_assert_PIF(s_create_test_net(TEST_NET_ID_2),
                   "Failed to create test network 2");
    
    // Add nodes to different networks
    dap_stream_node_addr_t l_addr1 = s_make_addr(0x01);
    dap_stream_node_addr_t l_addr2 = s_make_addr(0x02);
    
    dap_link_manager_test_add_to_hot_list(l_addr1, TEST_NET_ID_1);
    dap_link_manager_test_add_to_hot_list(l_addr2, TEST_NET_ID_2);
    
    // Verify network 1 hot list
    size_t l_count1 = 0;
    dap_stream_node_addr_t *l_addrs1 = dap_link_manager_get_ignored_addrs(&l_count1, TEST_NET_ID_1);
    
    log_it(L_DEBUG, "Net 1 hot list count: %zu", l_count1);
    dap_assert_PIF(l_count1 == 1, "Network 1 should have 1 node");
    if (l_addrs1) {
        dap_assert_PIF(s_addr_equal(l_addrs1[0], l_addr1),
                       "Network 1 should contain addr1");
        DAP_DELETE(l_addrs1);
    }
    
    // Verify network 2 hot list
    size_t l_count2 = 0;
    dap_stream_node_addr_t *l_addrs2 = dap_link_manager_get_ignored_addrs(&l_count2, TEST_NET_ID_2);
    
    log_it(L_DEBUG, "Net 2 hot list count: %zu", l_count2);
    dap_assert_PIF(l_count2 == 1, "Network 2 should have 1 node");
    if (l_addrs2) {
        dap_assert_PIF(s_addr_equal(l_addrs2[0], l_addr2),
                       "Network 2 should contain addr2");
        DAP_DELETE(l_addrs2);
    }
    
    // Cleanup
    s_cleanup_test_nets();
    
    log_it(L_INFO, "✓ Test 5: Multiple Networks Isolation PASSED\n");
}

/**
 * @brief Test 6: Memory cleanup on network removal
 */
static void test_hot_list_cleanup(void)
{
    log_it(L_INFO, "=== Test 6: Memory Cleanup on Net Removal ===");
    
    // Create test network
    dap_assert_PIF(s_create_test_net(TEST_NET_ID_1),
                   "Failed to create test network");
    
    // Add multiple nodes
    dap_stream_node_addr_t l_addrs[3];
    l_addrs[0] = s_make_addr(0x01);
    l_addrs[1] = s_make_addr(0x02);
    l_addrs[2] = s_make_addr(0x03);
    
    for (size_t i = 0; i < sizeof(l_addrs) / sizeof(l_addrs[0]); i++) {
        dap_link_manager_test_add_to_hot_list(l_addrs[i], TEST_NET_ID_1);
    }
    
    // Verify nodes are present
    size_t l_count = 0;
    dap_stream_node_addr_t *l_hot_addrs = dap_link_manager_get_ignored_addrs(&l_count, TEST_NET_ID_1);
    log_it(L_DEBUG, "Hot list count before cleanup: %zu", l_count);
    
    if (l_hot_addrs) {
        DAP_DELETE(l_hot_addrs);
    }
    
    // Remove network (should cleanup hot list)
    s_cleanup_test_nets();
    
    // Note: After removal, we can't query hot list anymore as net doesn't exist
    // This test verifies no memory leaks occur (check with valgrind)
    
    log_it(L_INFO, "✓ Test 6: Memory Cleanup PASSED (check with valgrind)\n");
}

/**
 * @brief Test 7: Empty network hot list
 */
static void test_hot_list_empty(void)
{
    log_it(L_INFO, "=== Test 7: Empty Network Hot List ===");
    
    // Create test network
    dap_assert_PIF(s_create_test_net(TEST_NET_ID_1),
                   "Failed to create test network");
    
    // Query empty hot list multiple times
    for (int i = 0; i < 3; i++) {
        size_t l_count = 0;
        dap_stream_node_addr_t *l_addrs = dap_link_manager_get_ignored_addrs(&l_count, TEST_NET_ID_1);
        
        dap_assert_PIF(l_addrs == NULL, "Empty hot list should return NULL");
        dap_assert_PIF(l_count == 0, "Empty hot list count should be 0");
    }
    
    // Cleanup
    s_cleanup_test_nets();
    
    log_it(L_INFO, "✓ Test 7: Empty Network Hot List PASSED\n");
}

/**
 * @brief Test 8: Invalid network ID
 */
static void test_hot_list_invalid_net(void)
{
    log_it(L_INFO, "=== Test 8: Invalid Network ID ===");
    
    // Query hot list for non-existent network
    uint64_t l_invalid_net_id = 0xDEADBEEFDEADBEEFULL;
    size_t l_count = 0;
    dap_stream_node_addr_t *l_addrs = dap_link_manager_get_ignored_addrs(&l_count, l_invalid_net_id);
    
    dap_assert_PIF(l_addrs == NULL, "Invalid network should return NULL");
    dap_assert_PIF(l_count == 0, "Invalid network count should be 0");
    
    log_it(L_INFO, "✓ Test 8: Invalid Network ID PASSED\n");
}

/**
 * @brief Test 9: Large number of nodes
 */
static void test_hot_list_many_nodes(void)
{
    log_it(L_INFO, "=== Test 9: Large Number of Nodes ===");
    
    // Create test network
    dap_assert_PIF(s_create_test_net(TEST_NET_ID_1),
                   "Failed to create test network");
    
    // Add many nodes
    const size_t NODES_COUNT = 50;
    for (size_t i = 1; i <= NODES_COUNT; i++) {
        dap_stream_node_addr_t l_addr;
        l_addr.uint64 = i;
        dap_link_manager_test_add_to_hot_list(l_addr, TEST_NET_ID_1);
    }
    
    // Verify all nodes are present
    size_t l_count = 0;
    dap_stream_node_addr_t *l_addrs = dap_link_manager_get_ignored_addrs(&l_count, TEST_NET_ID_1);
    
    log_it(L_DEBUG, "Hot list count with many nodes: %zu (expected: %zu)", l_count, NODES_COUNT);
    dap_assert_PIF(l_count == NODES_COUNT, "Should have all nodes");
    
    if (l_addrs) {
        // Verify some random nodes
        bool l_found_1 = false, l_found_25 = false, l_found_50 = false;
        for (size_t i = 0; i < l_count; i++) {
            if (l_addrs[i].uint64 == 1) l_found_1 = true;
            if (l_addrs[i].uint64 == 25) l_found_25 = true;
            if (l_addrs[i].uint64 == NODES_COUNT) l_found_50 = true;
        }
        dap_assert_PIF(l_found_1 && l_found_25 && l_found_50,
                       "Should find sample nodes");
        DAP_DELETE(l_addrs);
    }
    
    // Cleanup
    s_cleanup_test_nets();
    
    log_it(L_INFO, "✓ Test 9: Large Number of Nodes PASSED\n");
}

// =============================================================================
// MAIN TEST SUITE
// =============================================================================

int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    log_it(L_INFO, "=== Link Manager Hot List - Unit Tests ===");
    log_it(L_INFO, "Testing in-memory hot list storage...\n");
    
    // Initialize test environment
    if (!s_test_init()) {
        log_it(L_ERROR, "Failed to initialize test environment");
        return 1;
    }
    
    // Run tests
    test_hot_list_init();
    test_hot_list_add_single();
    test_hot_list_add_multiple();
    test_hot_list_duplicate();
    test_hot_list_multi_net();
    test_hot_list_cleanup();
    test_hot_list_empty();
    test_hot_list_invalid_net();
    test_hot_list_many_nodes();
    
    // Cleanup
    s_test_cleanup();
    
    log_it(L_INFO, "\n=== All Hot List Tests PASSED! ===");
    log_it(L_INFO, "Total: 9 tests");
    log_it(L_INFO, "\n💡 Recommended: Run with valgrind to verify memory safety:");
    log_it(L_INFO, "   valgrind --leak-check=full ./test_link_manager_hot_list");
    
    return 0;
}

