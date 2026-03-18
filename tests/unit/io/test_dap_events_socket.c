/**
 * @file test_dap_events_socket.c
 * @brief Unit tests for DAP events socket module
 * @details Tests socket creation, lifecycle, buffer operations, and edge cases
 * @date 2025-11-21
 * @copyright (c) 2025 Cellframe Network
 */

#include "dap_common.h"
#include "dap_test.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_context.h"
#include "dap_mock.h"
#include <limits.h>

#define LOG_TAG "test_dap_events_socket"

// Mock adjacent DAP SDK modules to isolate dap_events_socket
DAP_MOCK(dap_worker_add_events_socket_auto);
DAP_MOCK(dap_worker_exec_callback_on);
DAP_MOCK(dap_worker_get_current);
DAP_MOCK(dap_context_current);
DAP_MOCK(dap_context_add);
DAP_MOCK(dap_context_remove);
DAP_MOCK(dap_context_poll_update);
DAP_MOCK(dap_context_find);

static bool s_callback_called = false;
static int s_callback_count = 0;
static void *s_callback_arg = NULL;

/**
 * @brief Test callback for socket operations
 */
static void s_test_callback(dap_events_socket_t *a_es, void *a_arg)
{
    UNUSED(a_es);
    s_callback_called = true;
    s_callback_count++;
    s_callback_arg = a_arg;
}

/**
 * @brief Reset callback state
 */
static void s_reset_callback_state(void)
{
    s_callback_called = false;
    s_callback_count = 0;
    s_callback_arg = NULL;
}

#ifdef DAP_EVENTS_CAPS_IOCP
static void s_iocp_queue_flush_and_free(PSLIST_HEADER a_list)
{
    for (PSLIST_ENTRY l_cur = InterlockedPopEntrySList(a_list); l_cur; l_cur = InterlockedPopEntrySList(a_list)) {
        queue_entry_t *l_entry = (queue_entry_t *)l_cur;
        if (l_entry->size)
            DAP_DELETE(l_entry->data);
        DAP_ALFREE(l_entry);
    }
}

static void s_test_iocp_queue_send_contract(void)
{
    log_it(L_INFO, "Testing IOCP queue send contract");

    // API compatibility check:
    // - legacy queue_data_send keeps int return type
    // - size_t-safe queue_data_send_size is additive API
    int (*l_queue_data_send_compat)(dap_events_socket_t*, const void*, size_t) = dap_events_socket_queue_data_send;
    size_t (*l_queue_data_send_size)(dap_events_socket_t*, const void*, size_t) = dap_events_socket_queue_data_send_size;
    dap_assert(l_queue_data_send_compat != NULL, "legacy queue_data_send(int) API is available");
    dap_assert(l_queue_data_send_size != NULL, "queue_data_send_size(size_t) API is available");

    SLIST_HEADER l_slist = { 0 };
    InitializeSListHead(&l_slist);

    // Keep queue non-empty to avoid IOCP signaling dependency in success path
    queue_entry_t *l_seed = DAP_ALMALLOC(MEMORY_ALLOCATION_ALIGNMENT, sizeof(queue_entry_t));
    dap_assert(l_seed != NULL, "Seed queue entry allocation");
    if (!l_seed)
        return;
    *l_seed = (queue_entry_t){ .size = 0, .data = NULL };
    InterlockedPushEntrySList(&l_slist, &l_seed->entry);

    dap_events_socket_t l_es = { 0 };
    l_es.buf_out = &l_slist;
    l_es.uuid = 1;

    const char l_payload[] = "iocp-payload";
    int l_written_compat = dap_events_socket_queue_data_send(&l_es, l_payload, sizeof(l_payload));
    size_t l_written = dap_events_socket_queue_data_send_size(&l_es, l_payload, sizeof(l_payload));
    dap_assert(l_written_compat == (int)sizeof(l_payload), "legacy queue_data_send returns payload length");
    dap_assert(l_written == sizeof(l_payload), "queue_data_send_size returns exact size_t payload length");

    int l_ptr_send_ret = dap_events_socket_queue_ptr_send(&l_es, (void *)l_payload);
    dap_assert(l_ptr_send_ret == 0, "queue_ptr_send returns status=0 on success");

    // Boundary checks from regression ticket: INT_MAX / INT_MAX+1
    int l_boundary_compat_int_max = dap_events_socket_queue_data_send(&l_es, NULL, (size_t)INT_MAX);
    int l_boundary_compat_int_max_plus_1 = dap_events_socket_queue_data_send(&l_es, NULL, (size_t)INT_MAX + 1U);
    size_t l_boundary_int_max = dap_events_socket_queue_data_send_size(&l_es, NULL, (size_t)INT_MAX);
    size_t l_boundary_int_max_plus_1 = dap_events_socket_queue_data_send_size(&l_es, NULL, (size_t)INT_MAX + 1U);
    dap_assert(l_boundary_compat_int_max == 0, "legacy queue_data_send handles INT_MAX boundary");
    dap_assert(l_boundary_compat_int_max_plus_1 == 0, "legacy queue_data_send handles INT_MAX+1 boundary");
    dap_assert(l_boundary_int_max == 0, "queue_data_send_size handles INT_MAX boundary");
    dap_assert(l_boundary_int_max_plus_1 == 0, "queue_data_send_size handles INT_MAX+1 boundary");

    // For empty queue without context IOCP ptr send should report an error code, not false success.
    SLIST_HEADER l_empty_slist = { 0 };
    InitializeSListHead(&l_empty_slist);
    dap_events_socket_t l_es_empty = { 0 };
    l_es_empty.buf_out = &l_empty_slist;
    l_es_empty.uuid = 2;
    int l_ptr_send_fail = dap_events_socket_queue_ptr_send(&l_es_empty, (void *)l_payload);
    dap_assert(l_ptr_send_fail != 0, "queue_ptr_send returns non-zero status on failure");

    s_iocp_queue_flush_and_free(&l_slist);
    s_iocp_queue_flush_and_free(&l_empty_slist);
}
#endif

/**
 * @brief Test: Initialize and deinitialize events socket system
 */
static void s_test_events_socket_init_deinit(void)
{
    log_it(L_INFO, "Testing events socket init/deinit");
    
    int l_ret = dap_events_socket_init();
    dap_assert(l_ret == 0, "Events socket initialization");
    
    dap_events_socket_deinit();
    dap_pass_msg("Events socket deinitialization");
}

/**
 * @brief Test: Create events socket with different types
 */
static void s_test_events_socket_create(void)
{
    log_it(L_INFO, "Testing events socket creation");
    
    int l_ret = dap_events_socket_init();
    dap_assert(l_ret == 0, "Events socket initialization");
    
    // Create callbacks structure
    dap_events_socket_callbacks_t l_callbacks = {0};
    l_callbacks.read_callback = s_test_callback;
    
    // Test creating queue type socket
    dap_events_socket_t *l_es_queue = dap_events_socket_create(
        DESCRIPTOR_TYPE_QUEUE, &l_callbacks);
    dap_assert(l_es_queue != NULL, "Create queue socket");
    
    if (l_es_queue) {
        dap_assert(l_es_queue->type == DESCRIPTOR_TYPE_QUEUE, "Socket type is queue");
        dap_assert(l_es_queue->callbacks.read_callback == s_test_callback, 
                   "Callback properly assigned");
        dap_events_socket_delete_unsafe(l_es_queue, false);
    }
    
    // Test creating event type socket
    dap_events_socket_t *l_es_event = dap_events_socket_create(
        DESCRIPTOR_TYPE_EVENT, &l_callbacks);
    dap_assert(l_es_event != NULL, "Create event socket");
    
    if (l_es_event) {
        dap_assert(l_es_event->type == DESCRIPTOR_TYPE_EVENT, "Socket type is event");
        dap_events_socket_delete_unsafe(l_es_event, false);
    }
    
    // Test creating pipe type socket
    dap_events_socket_t *l_es_pipe = dap_events_socket_create(
        DESCRIPTOR_TYPE_PIPE, &l_callbacks);
    dap_assert(l_es_pipe != NULL, "Create pipe socket");
    
    if (l_es_pipe) {
        dap_assert(l_es_pipe->type == DESCRIPTOR_TYPE_PIPE, "Socket type is pipe");
        dap_events_socket_delete_unsafe(l_es_pipe, false);
    }
    
    dap_events_socket_deinit();
}

/**
 * @brief Test: Events socket UUID generation and uniqueness
 */
static void s_test_events_socket_uuid(void)
{
    log_it(L_INFO, "Testing events socket UUID generation");
    
    int l_ret = dap_events_socket_init();
    dap_assert(l_ret == 0, "Events socket initialization");
    
    // Test UUID generation and uniqueness
    const int UUID_COUNT = 100;
    unsigned int l_uuids[UUID_COUNT];
    
    for (int i = 0; i < UUID_COUNT; i++) {
        l_uuids[i] = dap_new_es_id();
    }
    
    // Check all UUIDs are unique
    int l_duplicates = 0;
    for (int i = 0; i < UUID_COUNT - 1; i++) {
        for (int j = i + 1; j < UUID_COUNT; j++) {
            if (l_uuids[i] == l_uuids[j]) {
                l_duplicates++;
                log_it(L_ERROR, "Duplicate UUID found: %u at indices %d and %d",
                       l_uuids[i], i, j);
            }
        }
    }
    
    dap_assert(l_duplicates == 0, "All UUIDs are unique");
    log_it(L_DEBUG, "Generated %d unique UUIDs", UUID_COUNT);
    
    // Test UUID monotonicity (should generally increase)
    int l_increases = 0;
    for (int i = 0; i < UUID_COUNT - 1; i++) {
        if (l_uuids[i + 1] > l_uuids[i]) {
            l_increases++;
        }
    }
    log_it(L_DEBUG, "UUID increases: %d out of %d", l_increases, UUID_COUNT - 1);
    
    dap_events_socket_deinit();
}

/**
 * @brief Test: Socket buffer operations with real data
 */
static void s_test_events_socket_buffers(void)
{
    log_it(L_INFO, "Testing events socket buffer operations");
    
    int l_ret = dap_events_socket_init();
    dap_assert(l_ret == 0, "Events socket initialization");
    
    dap_events_socket_callbacks_t l_callbacks = {0};
    dap_events_socket_t *l_es = dap_events_socket_create(
        DESCRIPTOR_TYPE_PIPE, &l_callbacks);
    dap_assert(l_es != NULL, "Create pipe socket");
    
    if (l_es) {
        // Test initial buffer state
        size_t l_initial_free = dap_events_socket_get_free_buf_size(l_es);
        log_it(L_DEBUG, "Initial free buffer size: %zu", l_initial_free);
        dap_assert(l_initial_free > 0, "Initial buffer has free space");
        
        // Simulate filling the input buffer
        const char *l_test_data = "Test data for buffer operations";
        size_t l_data_len = strlen(l_test_data);
        
        if (l_es->buf_in) {
            // Fill buffer with test data
            size_t l_available = l_es->buf_in_size_max > l_es->buf_in_size ? 
                                 l_es->buf_in_size_max - l_es->buf_in_size : 0;
            size_t l_bytes_to_write = l_data_len < l_available ? l_data_len : l_available;
            
            if (l_bytes_to_write > 0) {
                memcpy(l_es->buf_in + l_es->buf_in_size, l_test_data, l_bytes_to_write);
                l_es->buf_in_size += l_bytes_to_write;
                
                log_it(L_DEBUG, "Filled buffer with %zu bytes, used: %zu/%zu",
                       l_bytes_to_write, l_es->buf_in_size, l_es->buf_in_size_max);
                
                // Test shrink with data
                size_t l_shrink_size = l_bytes_to_write / 2;
                dap_events_socket_shrink_buf_in(l_es, l_shrink_size);
                dap_assert(l_es->buf_in_size == l_bytes_to_write - l_shrink_size,
                           "Buffer shrunk correctly");
                
                log_it(L_DEBUG, "After shrink(%zu): used=%zu",
                       l_shrink_size, l_es->buf_in_size);
                
                // Test shrink entire buffer
                dap_events_socket_shrink_buf_in(l_es, l_es->buf_in_size);
                dap_assert(l_es->buf_in_size == 0, "Buffer completely cleared");
            }
        }
        
        // Test pop from empty buffer
        char l_dummy_buf[10];
        dap_events_socket_pop_from_buf_in(l_es, l_dummy_buf, sizeof(l_dummy_buf));
        dap_pass_msg("Pop from empty buffer handled");
        
        dap_events_socket_delete_unsafe(l_es, false);
    }
    
    dap_events_socket_deinit();
}

/**
 * @brief Test: Buffer shrink variations
 */
static void s_test_buffer_shrink_variations(void)
{
    log_it(L_INFO, "Testing buffer shrink variations");
    
    int l_ret = dap_events_socket_init();
    dap_assert(l_ret == 0, "Events socket initialization");
    
    dap_events_socket_callbacks_t l_callbacks = {0};
    dap_events_socket_t *l_es = dap_events_socket_create(
        DESCRIPTOR_TYPE_PIPE, &l_callbacks);
    dap_assert(l_es != NULL, "Create pipe socket");
    
    if (l_es && l_es->buf_in) {
        // Fill buffer with test pattern
        const char *l_pattern = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        size_t l_pattern_len = strlen(l_pattern);
        size_t l_fill_size = l_pattern_len < l_es->buf_in_size_max ? 
                             l_pattern_len : l_es->buf_in_size_max;
        
        memcpy(l_es->buf_in, l_pattern, l_fill_size);
        l_es->buf_in_size = l_fill_size;
        log_it(L_DEBUG, "Filled buffer with %zu bytes", l_fill_size);
        
        // Test 1: Shrink zero bytes
        size_t l_before = l_es->buf_in_size;
        dap_events_socket_shrink_buf_in(l_es, 0);
        dap_assert(l_es->buf_in_size == l_before, "Shrink 0 bytes does nothing");
        
        // Test 2: Shrink partial buffer
        dap_events_socket_shrink_buf_in(l_es, 10);
        dap_assert(l_es->buf_in_size == l_before - 10, "Shrink 10 bytes");
        log_it(L_DEBUG, "After shrink 10: used=%zu", l_es->buf_in_size);
        
        // Verify data shifted correctly
        if (l_es->buf_in_size > 0 && l_es->buf_in[0] == l_pattern[10]) {
            dap_pass_msg("Data shifted correctly after shrink");
        }
        
        // Test 3: Shrink more than available (edge case)
        size_t l_huge_shrink = l_es->buf_in_size + 1000;
        dap_events_socket_shrink_buf_in(l_es, l_huge_shrink);
        log_it(L_DEBUG, "After huge shrink: used=%zu", l_es->buf_in_size);
        dap_pass_msg("Oversized shrink handled");
        
        // Test 4: Shrink on empty buffer
        l_es->buf_in_size = 0;
        dap_events_socket_shrink_buf_in(l_es, 10);
        dap_assert(l_es->buf_in_size == 0, "Shrink empty buffer safe");
        
        dap_events_socket_delete_unsafe(l_es, false);
    }
    
    dap_events_socket_deinit();
}

/**
 * @brief Test: Buffer insert operations
 */
static void s_test_buffer_insert_operations(void)
{
    log_it(L_INFO, "Testing buffer insert operations");
    
    int l_ret = dap_events_socket_init();
    dap_assert(l_ret == 0, "Events socket initialization");
    
    dap_events_socket_callbacks_t l_callbacks = {0};
    dap_events_socket_t *l_es = dap_events_socket_create(
        DESCRIPTOR_TYPE_PIPE, &l_callbacks);
    dap_assert(l_es != NULL, "Create pipe socket");
    
    if (l_es) {
        // Test inserting data into output buffer
        const char *l_msg1 = "First message";
        const char *l_msg2 = "Second message";
        const char *l_msg3 = "Third message";
        
        size_t l_ret1 = dap_events_socket_insert_buf_out(l_es, (void *)l_msg1, strlen(l_msg1));
        log_it(L_DEBUG, "Insert msg1: %zu bytes", l_ret1);
        
        size_t l_ret2 = dap_events_socket_insert_buf_out(l_es, (void *)l_msg2, strlen(l_msg2));
        log_it(L_DEBUG, "Insert msg2: %zu bytes", l_ret2);
        
        size_t l_ret3 = dap_events_socket_insert_buf_out(l_es, (void *)l_msg3, strlen(l_msg3));
        log_it(L_DEBUG, "Insert msg3: %zu bytes", l_ret3);
        
        // Check if buffer tracking is correct
        if (l_es->buf_out_size > 0) {
            log_it(L_DEBUG, "Output buffer: %zu bytes buffered", l_es->buf_out_size);
            dap_pass_msg("Multiple inserts tracked");
        }
        
        // Test insert NULL data (edge case)
        size_t l_ret_null = dap_events_socket_insert_buf_out(l_es, NULL, 100);
        log_it(L_DEBUG, "Insert NULL: %zu bytes", l_ret_null);
        
        // Test insert zero bytes
        size_t l_ret_zero = dap_events_socket_insert_buf_out(l_es, (void *)l_msg1, 0);
        log_it(L_DEBUG, "Insert 0 bytes: %zu bytes", l_ret_zero);
        
        dap_events_socket_delete_unsafe(l_es, false);
    }
    
    dap_events_socket_deinit();
}

/**
 * @brief Test: Socket event signaling
 */
static void s_test_socket_event_signal(void)
{
    log_it(L_INFO, "Testing socket event signaling");
    
    int l_ret = dap_events_socket_init();
    dap_assert(l_ret == 0, "Events socket initialization");
    
    dap_events_socket_callbacks_t l_callbacks = {0};
    l_callbacks.event_callback = NULL;
    
    dap_events_socket_t *l_es = dap_events_socket_create(
        DESCRIPTOR_TYPE_EVENT, &l_callbacks);
    dap_assert(l_es != NULL, "Create event socket");
    
    if (l_es) {
        // Test signaling with different values
        int l_signal_values[] = {0, 1, 42, 100, -1, INT_MAX, INT_MIN};
        
        for (size_t i = 0; i < sizeof(l_signal_values) / sizeof(l_signal_values[0]); i++) {
            int l_signal_ret = dap_events_socket_event_signal(l_es, l_signal_values[i]);
            log_it(L_DEBUG, "Event signal(%d) returned: %d", 
                   l_signal_values[i], l_signal_ret);
        }
        
        dap_pass_msg("Event signaling with various values tested");
        
        dap_events_socket_delete_unsafe(l_es, false);
    }
    
    dap_events_socket_deinit();
}

/**
 * @brief Test: Socket lifecycle and cleanup
 */
static void s_test_socket_lifecycle(void)
{
    log_it(L_INFO, "Testing socket lifecycle");
    
    int l_ret = dap_events_socket_init();
    dap_assert(l_ret == 0, "Events socket initialization");
    
    // Create and immediately delete
    dap_events_socket_callbacks_t l_callbacks = {0};
    dap_events_socket_t *l_es = dap_events_socket_create(
        DESCRIPTOR_TYPE_QUEUE, &l_callbacks);
    dap_assert(l_es != NULL, "Create socket");
    
    if (l_es) {
        uint64_t l_uuid = l_es->uuid;
        log_it(L_DEBUG, "Created socket with UUID: %lu", (unsigned long)l_uuid);
        
        dap_events_socket_delete_unsafe(l_es, false);
        dap_pass_msg("Socket deleted successfully");
    }
    
    // Create multiple sockets and delete in reverse order
    const int SOCKET_COUNT = 10;
    dap_events_socket_t *l_sockets[SOCKET_COUNT];
    
    for (int i = 0; i < SOCKET_COUNT; i++) {
        l_sockets[i] = dap_events_socket_create(DESCRIPTOR_TYPE_PIPE, &l_callbacks);
        if (l_sockets[i]) {
            log_it(L_DEBUG, "Socket[%d] UUID: %lu", i, (unsigned long)l_sockets[i]->uuid);
        }
    }
    
    for (int i = SOCKET_COUNT - 1; i >= 0; i--) {
        if (l_sockets[i]) {
            dap_events_socket_delete_unsafe(l_sockets[i], false);
        }
    }
    
    dap_pass_msg("Multiple socket lifecycle tested");
    
    dap_events_socket_deinit();
}

/**
 * @brief Test: Edge cases and NULL checks
 */
static void s_test_events_socket_edge_cases(void)
{
    log_it(L_INFO, "Testing events socket edge cases");
    
    int l_ret = dap_events_socket_init();
    dap_assert(l_ret == 0, "Events socket initialization");
    
    // Test create with NULL callbacks
    dap_events_socket_t *l_es_null = dap_events_socket_create(
        DESCRIPTOR_TYPE_PIPE, NULL);
    log_it(L_DEBUG, "Socket with NULL callbacks: %p", l_es_null);
    
    if (l_es_null) {
        dap_events_socket_delete_unsafe(l_es_null, false);
    }
    
    // Test delete with NULL
    dap_events_socket_delete_unsafe(NULL, false);
    dap_pass_msg("Delete NULL socket handled gracefully");
    
    // Test shrink with NULL
    dap_events_socket_shrink_buf_in(NULL, 0);
    dap_pass_msg("Shrink NULL socket handled gracefully");
    
    // Test insert with NULL socket
    dap_events_socket_insert_buf_out(NULL, "test", 4);
    dap_pass_msg("Insert to NULL socket handled");
    
    // Test get_free_buf_size with NULL
    size_t l_size = dap_events_socket_get_free_buf_size(NULL);
    log_it(L_DEBUG, "Free buffer size of NULL socket: %zu", l_size);
    
    dap_events_socket_deinit();
}

/**
 * @brief Test: Buffer boundary conditions
 */
static void s_test_buffer_boundaries(void)
{
    log_it(L_INFO, "Testing buffer boundary conditions");
    
    int l_ret = dap_events_socket_init();
    dap_assert(l_ret == 0, "Events socket initialization");
    
    dap_events_socket_callbacks_t l_callbacks = {0};
    dap_events_socket_t *l_es = dap_events_socket_create(
        DESCRIPTOR_TYPE_PIPE, &l_callbacks);
    dap_assert(l_es != NULL, "Create pipe socket");
    
    if (l_es && l_es->buf_in) {
        // Fill to maximum
        size_t l_max_size = l_es->buf_in_size;
        l_es->buf_in_size = l_max_size;
        log_it(L_DEBUG, "Buffer filled to maximum: %zu", l_max_size);
        
        // Try to get free size when full
        size_t l_free = dap_events_socket_get_free_buf_size(l_es);
        log_it(L_DEBUG, "Free size when full: %zu", l_free);
        dap_assert(l_free == 0, "No free space when buffer full");
        
        // Try to insert when full
        size_t l_inserted = dap_events_socket_insert_buf_out(l_es, "test", 4);
        log_it(L_DEBUG, "Insert when full: %zu bytes", l_inserted);
        
        // Shrink to empty
        dap_events_socket_shrink_buf_in(l_es, l_max_size);
        dap_assert(l_es->buf_in_size == 0, "Buffer emptied");
        
        // Get free size when empty
        l_free = dap_events_socket_get_free_buf_size(l_es);
        log_it(L_DEBUG, "Free size when empty: %zu", l_free);
        dap_assert(l_free == l_max_size, "Full space when buffer empty");
        
        dap_events_socket_delete_unsafe(l_es, false);
    }
    
    dap_events_socket_deinit();
}

/**
 * @brief Main test function
 */
int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    // Initialize DAP SDK
    int l_ret = dap_common_init("test_dap_events_socket", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }
    
    // Initialize mock framework
    dap_mock_init();
    
    log_it(L_INFO, "=== DAP Events Socket - Unit Tests ===");
    
    // Run tests
    s_test_events_socket_init_deinit();
    s_test_events_socket_create();
    s_test_events_socket_uuid();
    s_test_events_socket_buffers();
    s_test_buffer_shrink_variations();
    s_test_buffer_insert_operations();
    s_test_socket_event_signal();
    s_test_socket_lifecycle();
    s_test_events_socket_edge_cases();
    s_test_buffer_boundaries();
#ifdef DAP_EVENTS_CAPS_IOCP
    s_test_iocp_queue_send_contract();
#endif
    
    log_it(L_INFO, "=== All Events Socket Tests PASSED! ===");
    
    // Cleanup
    dap_mock_deinit();
    dap_common_deinit();
    
    return 0;
}
