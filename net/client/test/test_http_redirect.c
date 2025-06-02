/**
 * Example of using dap_client_http with redirect handling and full callback
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dap_client_http.h"
#include "dap_worker.h"
#include "dap_events.h"
#include "dap_http_header.h"

// Error callback
static void http_error_callback(int a_error_code, void *a_arg)
{
    const char *l_url = (const char *)a_arg;
    
    // Check for custom error codes
    switch(a_error_code) {
        case -301:
            printf("Error: Too many redirects for URL: %s\n", l_url);
            break;
        case -302:
            printf("Error: Redirect without Location header for URL: %s\n", l_url);
            break;
        case -413:
            printf("Error: Response too large (>10MB) for URL: %s\n", l_url);
            break;
        default:
            printf("Error %d for URL %s: %s\n", a_error_code, l_url, strerror(a_error_code));
    }
}

// Full response callback with headers
static void http_response_full_callback(void *a_body, size_t a_body_size, 
                                       struct dap_http_header *a_headers, 
                                       void *a_arg, http_status_code_t a_status_code)
{
    const char *l_url = (const char *)a_arg;
    
    printf("\n=== Response for URL: %s ===\n", l_url);
    printf("Status Code: %d\n", a_status_code);
    printf("Body Size: %zu bytes\n", a_body_size);
    
    // Print headers
    printf("\nHeaders:\n");
    struct dap_http_header *l_header = a_headers;
    while(l_header) {
        printf("  %s: %s\n", l_header->name, l_header->value);
        l_header = l_header->next;
    }
    
    // Print body (first 200 chars if large)
    printf("\nBody:\n");
    if(a_body_size > 0) {
        size_t l_print_size = a_body_size > 200 ? 200 : a_body_size;
        printf("%.*s", (int)l_print_size, (char *)a_body);
        if(a_body_size > 200) {
            printf("\n... (truncated, total %zu bytes)\n", a_body_size);
        }
    }
    printf("\n=========================\n");
}

// Test function
void test_http_redirect()
{
    dap_common_init(NULL, "log.txt");
    dap_log_level_set(L_DEBUG);
    // Initialize
    dap_events_init(1, 0);
    dap_events_start();
    dap_client_http_init();
    
    // Test URLs
    const char *test_urls[][3] = {
        // {host, port, path}
        {"httpbin.org", "80", "/redirect/3"},  // Will redirect 3 times
        {"httpbin.org", "80", "/bytes/10000000"}, // Large response (100KB)
        {"httpbin.org", "80", "/redirect/15"},  // Too many redirects (will fail)
        {"httpbin.org", "80", "/status/301"},   // 301 without Location (will fail)
        {NULL, NULL, NULL}
    };
    
    for(int i = 0; test_urls[i][0] != NULL; i++) {
        const char *host = test_urls[i][0];
        const char *port_str = test_urls[i][1];
        const char *path = test_urls[i][2];
        
        // Parse port safely
        char *endptr = NULL;
        long port_long = strtol(port_str, &endptr, 10);
        if(endptr == port_str || *endptr != '\0' || port_long <= 0 || port_long > 65535) {
            printf("Invalid port: %s\n", port_str);
            continue;
        }
        uint16_t port = (uint16_t)port_long;
        
        char url[256];
        snprintf(url, sizeof(url), "http://%s:%d%s", host, port, path);
        
        printf("\n>>> Testing URL: %s\n", url);
        dap_client_http_request_async(NULL,                      // worker (auto)
            host,                      // host
            port,                      // port
            "GET",                     // method
            NULL,                      // content type
            path,                      // path
            NULL,                      // request body
            0,                         // request size
            NULL,                      // cookie
            http_response_full_callback, // full callback
            http_error_callback,       // error callback
            NULL, NULL,
            (void *)strdup(url),               // callback arg
            NULL                       // custom headers
            );
        // Make request with full callback
        /*dap_client_http_t *l_client = dap_client_http_request_full(
            NULL,                      // worker (auto)
            host,                      // host
            port,                      // port
            "GET",                     // method
            NULL,                      // content type
            path,                      // path
            NULL,                      // request body
            0,                         // request size
            NULL,                      // cookie
            http_response_full_callback, // full callback
            http_error_callback,       // error callback
            (void *)strdup(url),               // callback arg
            NULL                       // custom headers
        );*/
        
        /*if(!l_client) {
            printf("Failed to create HTTP client for %s\n", url);
            continue;
        }*/
        
        // Wait for response (simplified - in real app use proper event loop)
        sleep(1);
    }
    
    // Cleanup
    dap_client_http_deinit();
    dap_events_deinit();
}

int main()
{
    test_http_redirect();
    return 0;
} 