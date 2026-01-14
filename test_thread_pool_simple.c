/*
 * Simple test for thread pool + UDP handshake offload
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "dap_common.h"
#include "dap_thread_pool.h"

static int s_task_counter = 0;

static void* s_test_task(void *a_arg)
{
    int l_id = *(int*)a_arg;
    printf("[Task %d] Started in thread pool\n", l_id);
    usleep(100000);  // 100ms
    __sync_fetch_and_add(&s_task_counter, 1);
    return (void*)(intptr_t)l_id;
}

static void s_test_callback(void *a_result, void *a_arg)
{
    int l_result = (intptr_t)a_result;
    int l_expected = *(int*)a_arg;
    printf("[Callback %d] Result: %d\n", l_expected, l_result);
}

int main(void)
{
    printf("=== Thread Pool Test ===\n");
    
    // Create pool with auto CPU count
    dap_thread_pool_t *l_pool = dap_thread_pool_create(0, 100);
    if (!l_pool) {
        printf("ERROR: Failed to create thread pool\n");
        return 1;
    }
    
    printf("Thread pool created (auto CPU count with affinity)\n");
    
    // Submit 10 tasks
    int l_args[10];
    for (int i = 0; i < 10; i++) {
        l_args[i] = i;
        int l_ret = dap_thread_pool_submit(l_pool, s_test_task, &l_args[i],
                                          s_test_callback, &l_args[i]);
        if (l_ret != 0) {
            printf("ERROR: Failed to submit task %d: %d\n", i, l_ret);
        }
    }
    
    printf("Submitted 10 tasks, waiting for completion...\n");
    
    // Wait for completion
    sleep(2);
    
    printf("Tasks completed: %d/10\n", s_task_counter);
    
    // Shutdown
    dap_thread_pool_shutdown(l_pool, 5000);
    dap_thread_pool_delete(l_pool);
    
    printf("Thread pool shut down\n");
    printf("=== Test %s ===\n", (s_task_counter == 10) ? "PASSED" : "FAILED");
    
    return (s_task_counter == 10) ? 0 : 1;
}
