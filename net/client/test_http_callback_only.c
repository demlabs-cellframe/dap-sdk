/**
 * Example of callback-only HTTP client usage
 * Thread-safe, fire-and-forget pattern
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include "dap_client_http.h"
#include "dap_worker.h"
#include "dap_events.h"
#include "dap_http_header.h"

// Request context for tracking
typedef struct {
    const char *url;
    int thread_id;
    sem_t *done_sem;
} request_context_t;

// Response callback
static void http_response_callback(void *a_body, size_t a_body_size, 
                                  struct dap_http_header *a_headers, 
                                  void *a_arg, http_status_code_t a_status_code)
{
    request_context_t *ctx = (request_context_t *)a_arg;
    
    printf("[Thread %d] Response for %s:\n", ctx->thread_id, ctx->url);
    printf("  Status: %d\n", a_status_code);
    printf("  Body size: %zu\n", a_body_size);
    
    // Print some headers
    struct dap_http_header *h = a_headers;
    int header_count = 0;
    while(h && header_count < 3) {
        printf("  Header: %s = %s\n", h->name, h->value);
        h = h->next;
        header_count++;
    }
    
    // Signal completion
    sem_post(ctx->done_sem);
    free(ctx);
}

// Error callback
static void http_error_callback(int a_error_code, void *a_arg)
{
    request_context_t *ctx = (request_context_t *)a_arg;
    
    printf("[Thread %d] Error %d for %s: %s\n", 
           ctx->thread_id, a_error_code, ctx->url, strerror(a_error_code));
    
    // Signal completion
    sem_post(ctx->done_sem);
    free(ctx);
}

// Started callback
static void http_started_callback(void *a_arg)
{
    request_context_t *ctx = (request_context_t *)a_arg;
    printf("[Thread %d] Request started for %s\n", ctx->thread_id, ctx->url);
}

// Progress callback (not implemented in current version)
static void http_progress_callback(size_t a_downloaded, size_t a_total, void *a_arg)
{
    request_context_t *ctx = (request_context_t *)a_arg;
    if(a_total > 0) {
        printf("[Thread %d] Progress: %zu/%zu bytes (%.1f%%)\n", 
               ctx->thread_id, a_downloaded, a_total, 
               (double)a_downloaded / a_total * 100.0);
    }
}

// Worker thread function
static void *worker_thread(void *arg)
{
    int thread_id = *(int*)arg;
    free(arg);
    
    // URLs to test
    const char *urls[] = {
        "/json",
        "/headers",
        "/delay/1",
        "/status/404"
    };
    
    // Semaphore to track completion
    sem_t done_sem;
    sem_init(&done_sem, 0, 0);
    
    // Make multiple requests
    for(int i = 0; i < 4; i++) {
        request_context_t *ctx = malloc(sizeof(request_context_t));
        ctx->url = urls[i];
        ctx->thread_id = thread_id;
        ctx->done_sem = &done_sem;
        
        printf("[Thread %d] Starting request %d to %s\n", thread_id, i, urls[i]);
        
        // Fire and forget - no return value
        dap_client_http_request_async(
            NULL,                      // auto worker
            "httpbin.org",            // host
            80,                       // port
            "GET",                    // method
            NULL,                     // content type
            urls[i],                  // path
            NULL,                     // request body
            0,                        // request size
            NULL,                     // cookie
            http_response_callback,   // response callback
            http_error_callback,      // error callback
            http_started_callback,    // started callback
            http_progress_callback,   // progress callback
            ctx,                      // context
            NULL                      // custom headers
        );
        
        // Small delay between requests
        usleep(100000);
    }
    
    // Wait for all requests to complete
    for(int i = 0; i < 4; i++) {
        sem_wait(&done_sem);
    }
    
    sem_destroy(&done_sem);
    printf("[Thread %d] All requests completed\n", thread_id);
    
    return NULL;
}

int main()
{
    // Initialize
    dap_events_init(0, 0);
    dap_client_http_init();
    
    const int num_threads = 3;
    pthread_t threads[num_threads];
    
    printf("Starting %d threads with callback-only API\n", num_threads);
    
    // Start threads
    for(int i = 0; i < num_threads; i++) {
        int *thread_id = malloc(sizeof(int));
        *thread_id = i;
        
        if(pthread_create(&threads[i], NULL, worker_thread, thread_id) != 0) {
            printf("Failed to create thread %d\n", i);
            free(thread_id);
        }
    }
    
    // Wait for all threads
    for(int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\nAll threads completed\n");
    
    // Give some time for cleanup
    sleep(1);
    
    // Cleanup
    dap_client_http_deinit();
    dap_events_deinit();
    
    return 0;
} 