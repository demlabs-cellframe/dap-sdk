// Quick test for integer fast path
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "dap_common.h"
#include "dap_json.h"

static char* gen_int_json(size_t n, size_t *len) {
    size_t cap = n * 20;
    char *j = malloc(cap);
    size_t p = sprintf(j, "{\"nums\":[");
    for (size_t i = 0; i < n; i++) {
        if (i > 0) p += sprintf(j + p, ",");
        p += sprintf(j + p, "%zu", i); // Pure integers!
        if (p + 30 > cap) { cap *= 2; j = realloc(j, cap); }
    }
    p += sprintf(j + p, "]}");
    *len = p;
    return j;
}

int main() {
    dap_log_level_set(L_CRITICAL);
    
    printf("=== INTEGER FAST PATH BENCHMARK ===\n\n");
    
    size_t len;
    char *json = gen_int_json(10000, &len);
    printf("JSON: %zu KB (10K pure integers)\n\n", len/1024);
    
    // Warmup
    for (int i = 0; i < 10; i++) {
        dap_json_t *o = dap_json_parse_buffer((uint8_t*)json, len);
        if (o) dap_json_object_free(o);
    }
    
    // Benchmark
    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    
    const int iter = 100;
    for (int i = 0; i < iter; i++) {
        dap_json_t *o = dap_json_parse_buffer((uint8_t*)json, len);
        if (!o) { printf("Parse failed\n"); return 1; }
        dap_json_object_free(o);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &t2);
    double sec = (t2.tv_sec - t1.tv_sec) + (t2.tv_nsec - t1.tv_nsec)/1e9;
    double mb = (len * iter) / (1024.0 * 1024.0);
    double throughput = mb / sec;
    
    printf("📊 RESULTS:\n");
    printf("Throughput: %.2f MB/s\n", throughput);
    printf("Latency:    %.3f ms\n\n", (sec/iter)*1000);
    
    if (throughput > 150) {
        printf("✅ EXCELLENT! Integer fast path working! (+%.0f%% vs baseline)\n", 
               (throughput/87-1)*100);
    } else {
        printf("⚠️  Slower than expected\n");
    }
    
    free(json);
    return 0;
}
