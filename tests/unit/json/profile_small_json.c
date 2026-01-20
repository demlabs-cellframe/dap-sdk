/**
 * @brief Micro-benchmark для профилирования Small JSON parsing
 * @details Запускает ТОЛЬКО Small JSON test много раз для perf analysis
 */

#include "dap_common.h"
#include "dap_json.h"
#include <stdio.h>
#include <time.h>

#define ITERATIONS 1000000  // 1M iterations для perf

static const char *s_small_json = 
    "{\"name\":\"John\",\"age\":30,\"city\":\"New York\","
    "\"hobbies\":[\"reading\",\"coding\"],\"active\":true}";

int main(int argc, char **argv)
{
    dap_log_level_set(L_CRITICAL);  // Отключаем логирование
    
    printf("Starting profiling: %d iterations of Small JSON parsing\n", ITERATIONS);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    size_t total_parsed = 0;
    
    // Hot loop для профилирования
    for (size_t i = 0; i < ITERATIONS; i++) {
        dap_json_t *l_json = dap_json_parse_buffer(s_small_json, strlen(s_small_json));
        if (l_json) {
            total_parsed++;
            dap_json_object_free(l_json);
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed = (end.tv_sec - start.tv_sec) + 
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("Completed: %zu/%d successful parses\n", total_parsed, ITERATIONS);
    printf("Total time: %.3f seconds\n", elapsed);
    printf("Average: %.2f ns/parse (%.2f Mops/sec)\n", 
           (elapsed * 1e9) / ITERATIONS,
           ITERATIONS / (elapsed * 1e6));
    
    return (total_parsed == ITERATIONS) ? 0 : 1;
}
