/**
 * @brief Micro-benchmark для профилирования Small JSON parsing
 * @details Запускает ТОЛЬКО Small JSON test много раз для perf analysis
 */

#include "dap_common.h"
#include "dap_time.h"
#include "dap_json.h"
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define ITERATIONS 1000000  // 1M iterations для perf

static const char *s_small_json = 
    "{\"name\":\"John\",\"age\":30,\"city\":\"New York\","
    "\"hobbies\":[\"reading\",\"coding\"],\"active\":true}";

int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    dap_log_level_set(L_CRITICAL);  // Отключаем логирование
    
    printf("Starting profiling: %d iterations of Small JSON parsing\n", ITERATIONS);
    
    dap_nanotime_t start_ns = dap_nanotime_now();
    
    size_t total_parsed = 0;
    
    // Hot loop для профилирования
    for (size_t i = 0; i < ITERATIONS; i++) {
        dap_json_t *l_json = dap_json_parse_buffer(s_small_json, strlen(s_small_json));
        if (l_json) {
            total_parsed++;
            dap_json_object_free(l_json);
        }
    }
    
    dap_nanotime_t end_ns = dap_nanotime_now();
    double elapsed = (end_ns - start_ns) / 1e9;
    
    printf("Completed: %zu/%d successful parses\n", total_parsed, ITERATIONS);
    printf("Total time: %.3f seconds\n", elapsed);
    printf("Average: %.2f ns/parse (%.2f Mops/sec)\n", 
           (elapsed * 1e9) / ITERATIONS,
           ITERATIONS / (elapsed * 1e6));
    
    return (total_parsed == ITERATIONS) ? 0 : 1;
}
