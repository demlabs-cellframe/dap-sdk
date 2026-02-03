/**
 * @file benchmark_hashtable.c
 * @brief Performance benchmark comparing dap_ht with other hash table implementations
 *
 * Compares:
 * - dap_ht (DAP SDK native implementation)
 * - uthash (original implementation we replaced)
 * - khash (klib - very fast)
 * - stb_ds (stb libraries)
 *
 * Copyright (c) 2024-2026 Demlabs
 * License: GNU GPL v3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

#include "dap_common.h"
#include "dap_ht.h"

// Competitor implementations - conditionally included
#ifdef BENCHMARK_UTHASH
#include "competitors/uthash.h"
#endif

#ifdef BENCHMARK_KHASH
#include "competitors/khash.h"
KHASH_MAP_INIT_STR(str_int, int)
#endif

#ifdef BENCHMARK_STB_DS
#define STB_DS_IMPLEMENTATION
#include "competitors/stb_ds.h"
#endif

// ============================================================================
// Benchmark Configuration
// ============================================================================

#define SMALL_SIZE      1000
#define MEDIUM_SIZE     10000
#define LARGE_SIZE      100000
#define XLARGE_SIZE     1000000

#define WARMUP_ITERATIONS   3
#define BENCHMARK_ITERATIONS 10

// ============================================================================
// Timing Utilities
// ============================================================================

static inline double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
}

typedef struct {
    double insert_time_us;
    double lookup_time_us;
    double delete_time_us;
    double total_time_us;
    size_t memory_bytes;
} benchmark_result_t;

// ============================================================================
// Test Data Generation
// ============================================================================

static char** generate_string_keys(int count) {
    char **keys = DAP_NEW_Z_COUNT(char*, count);
    if (!keys) return NULL;
    
    for (int i = 0; i < count; i++) {
        keys[i] = DAP_NEW_Z_COUNT(char, 32);
        if (keys[i])
            snprintf(keys[i], 32, "key_%d_%08x", i, (unsigned)rand());
    }
    return keys;
}

static void free_string_keys(char **keys, int count) {
    if (!keys) return;
    for (int i = 0; i < count; i++)
        DAP_DELETE(keys[i]);
    DAP_DELETE(keys);
}

// ============================================================================
// DAP_HT Benchmark
// ============================================================================

typedef struct dap_ht_test_item {
    char *key;
    int value;
    dap_ht_handle_t hh;
} dap_ht_test_item_t;

static benchmark_result_t benchmark_dap_ht(char **keys, int count) {
    benchmark_result_t result = {0};
    dap_ht_test_item_t *table = NULL;
    double start, end;
    
    // INSERT
    start = get_time_us();
    for (int i = 0; i < count; i++) {
        dap_ht_test_item_t *item = DAP_NEW_Z(dap_ht_test_item_t);
        item->key = keys[i];
        item->value = i;
        dap_ht_add_str(table, key, item);
    }
    end = get_time_us();
    result.insert_time_us = end - start;
    
    // LOOKUP
    start = get_time_us();
    for (int i = 0; i < count; i++) {
        dap_ht_test_item_t *found = NULL;
        dap_ht_find_str(table, keys[i], found);
        if (!found) {
            fprintf(stderr, "ERROR: dap_ht lookup failed for key %s\n", keys[i]);
        }
    }
    end = get_time_us();
    result.lookup_time_us = end - start;
    
    // DELETE
    start = get_time_us();
    dap_ht_test_item_t *el, *tmp;
    dap_ht_foreach(table, el, tmp) {
        dap_ht_del(table, el);
        DAP_DELETE(el);
    }
    end = get_time_us();
    result.delete_time_us = end - start;
    
    result.total_time_us = result.insert_time_us + result.lookup_time_us + result.delete_time_us;
    result.memory_bytes = count * sizeof(dap_ht_test_item_t);
    
    return result;
}

// ============================================================================
// UTHASH Benchmark
// ============================================================================

#ifdef BENCHMARK_UTHASH

typedef struct uthash_test_item {
    char *key;
    int value;
    UT_hash_handle hh;
} uthash_test_item_t;

static benchmark_result_t benchmark_uthash(char **keys, int count) {
    benchmark_result_t result = {0};
    uthash_test_item_t *table = NULL;
    double start, end;
    
    // INSERT
    start = get_time_us();
    for (int i = 0; i < count; i++) {
        uthash_test_item_t *item = calloc(1, sizeof(uthash_test_item_t));
        item->key = keys[i];
        item->value = i;
        HASH_ADD_KEYPTR(hh, table, item->key, strlen(item->key), item);
    }
    end = get_time_us();
    result.insert_time_us = end - start;
    
    // LOOKUP
    start = get_time_us();
    for (int i = 0; i < count; i++) {
        uthash_test_item_t *found = NULL;
        HASH_FIND_STR(table, keys[i], found);
        if (!found) {
            fprintf(stderr, "ERROR: uthash lookup failed for key %s\n", keys[i]);
        }
    }
    end = get_time_us();
    result.lookup_time_us = end - start;
    
    // DELETE
    start = get_time_us();
    uthash_test_item_t *el, *tmp;
    HASH_ITER(hh, table, el, tmp) {
        HASH_DEL(table, el);
        free(el);
    }
    end = get_time_us();
    result.delete_time_us = end - start;
    
    result.total_time_us = result.insert_time_us + result.lookup_time_us + result.delete_time_us;
    result.memory_bytes = count * sizeof(uthash_test_item_t);
    
    return result;
}

#endif // BENCHMARK_UTHASH

// ============================================================================
// KHASH Benchmark
// ============================================================================

#ifdef BENCHMARK_KHASH

static benchmark_result_t benchmark_khash(char **keys, int count) {
    benchmark_result_t result = {0};
    khash_t(str_int) *table = kh_init(str_int);
    double start, end;
    int ret;
    khiter_t k;
    
    // INSERT
    start = get_time_us();
    for (int i = 0; i < count; i++) {
        k = kh_put(str_int, table, keys[i], &ret);
        kh_value(table, k) = i;
    }
    end = get_time_us();
    result.insert_time_us = end - start;
    
    // LOOKUP
    start = get_time_us();
    for (int i = 0; i < count; i++) {
        k = kh_get(str_int, table, keys[i]);
        if (k == kh_end(table)) {
            fprintf(stderr, "ERROR: khash lookup failed for key %s\n", keys[i]);
        }
    }
    end = get_time_us();
    result.lookup_time_us = end - start;
    
    // DELETE
    start = get_time_us();
    for (k = kh_begin(table); k != kh_end(table); ++k) {
        if (kh_exist(table, k)) {
            kh_del(str_int, table, k);
        }
    }
    kh_destroy(str_int, table);
    end = get_time_us();
    result.delete_time_us = end - start;
    
    result.total_time_us = result.insert_time_us + result.lookup_time_us + result.delete_time_us;
    result.memory_bytes = 0; // khash doesn't expose internal memory easily
    
    return result;
}

#endif // BENCHMARK_KHASH

// ============================================================================
// STB_DS Benchmark
// ============================================================================

#ifdef BENCHMARK_STB_DS

typedef struct stb_test_item {
    char *key;
    int value;
} stb_test_item_t;

static benchmark_result_t benchmark_stb_ds(char **keys, int count) {
    benchmark_result_t result = {0};
    stb_test_item_t *table = NULL;
    double start, end;
    
    // Initialize
    sh_new_arena(table);
    
    // INSERT
    start = get_time_us();
    for (int i = 0; i < count; i++) {
        shput(table, keys[i], i);
    }
    end = get_time_us();
    result.insert_time_us = end - start;
    
    // LOOKUP
    start = get_time_us();
    for (int i = 0; i < count; i++) {
        int val = shget(table, keys[i]);
        (void)val;
    }
    end = get_time_us();
    result.lookup_time_us = end - start;
    
    // DELETE
    start = get_time_us();
    shfree(table);
    end = get_time_us();
    result.delete_time_us = end - start;
    
    result.total_time_us = result.insert_time_us + result.lookup_time_us + result.delete_time_us;
    result.memory_bytes = 0;
    
    return result;
}

#endif // BENCHMARK_STB_DS

// ============================================================================
// Benchmark Runner
// ============================================================================

static void print_result(const char *name, benchmark_result_t *result, int count) {
    printf("  %-12s | %10.2f | %10.2f | %10.2f | %10.2f | %.2f ops/us\n",
           name,
           result->insert_time_us,
           result->lookup_time_us,
           result->delete_time_us,
           result->total_time_us,
           (double)(count * 3) / result->total_time_us);
}

static void run_benchmark_suite(int count, const char *size_name) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║ Hash Table Benchmark: %s (%d items)                                        \n", size_name, count);
    printf("╠════════════════════════════════════════════════════════════════════════════╣\n");
    printf("║  Library     |  Insert µs |  Lookup µs |  Delete µs |   Total µs |   Perf  ║\n");
    printf("╠════════════════════════════════════════════════════════════════════════════╣\n");
    
    char **keys = generate_string_keys(count);
    if (!keys) {
        printf("║ ERROR: Failed to generate test keys!                                      ║\n");
        return;
    }
    
    benchmark_result_t result;
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        result = benchmark_dap_ht(keys, count);
    }
    
    // DAP_HT
    benchmark_result_t dap_best = {.total_time_us = 1e20};
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        result = benchmark_dap_ht(keys, count);
        if (result.total_time_us < dap_best.total_time_us)
            dap_best = result;
    }
    printf("║");
    print_result("dap_ht", &dap_best, count);
    printf("║\n");
    
#ifdef BENCHMARK_UTHASH
    // UTHASH
    benchmark_result_t uthash_best = {.total_time_us = 1e20};
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        result = benchmark_uthash(keys, count);
        if (result.total_time_us < uthash_best.total_time_us)
            uthash_best = result;
    }
    printf("║");
    print_result("uthash", &uthash_best, count);
    printf("║\n");
#endif

#ifdef BENCHMARK_KHASH
    // KHASH
    benchmark_result_t khash_best = {.total_time_us = 1e20};
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        result = benchmark_khash(keys, count);
        if (result.total_time_us < khash_best.total_time_us)
            khash_best = result;
    }
    printf("║");
    print_result("khash", &khash_best, count);
    printf("║\n");
#endif

#ifdef BENCHMARK_STB_DS
    // STB_DS
    benchmark_result_t stb_best = {.total_time_us = 1e20};
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        result = benchmark_stb_ds(keys, count);
        if (result.total_time_us < stb_best.total_time_us)
            stb_best = result;
    }
    printf("║");
    print_result("stb_ds", &stb_best, count);
    printf("║\n");
#endif
    
    printf("╚════════════════════════════════════════════════════════════════════════════╝\n");
    
    // Comparison summary
    printf("\n--- Performance Comparison ---\n");
    printf("dap_ht baseline: %.2f ops/µs\n", (double)(count * 3) / dap_best.total_time_us);
    
#ifdef BENCHMARK_UTHASH
    double uthash_ratio = dap_best.total_time_us / uthash_best.total_time_us;
    printf("vs uthash: dap_ht is %.1fx %s\n", 
           uthash_ratio > 1.0 ? uthash_ratio : 1.0/uthash_ratio,
           uthash_ratio > 1.0 ? "SLOWER" : "FASTER");
#endif

#ifdef BENCHMARK_KHASH
    double khash_ratio = dap_best.total_time_us / khash_best.total_time_us;
    printf("vs khash:  dap_ht is %.1fx %s\n",
           khash_ratio > 1.0 ? khash_ratio : 1.0/khash_ratio,
           khash_ratio > 1.0 ? "SLOWER" : "FASTER");
#endif

#ifdef BENCHMARK_STB_DS
    double stb_ratio = dap_best.total_time_us / stb_best.total_time_us;
    printf("vs stb_ds: dap_ht is %.1fx %s\n",
           stb_ratio > 1.0 ? stb_ratio : 1.0/stb_ratio,
           stb_ratio > 1.0 ? "SLOWER" : "FASTER");
#endif
    
    free_string_keys(keys, count);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    printf("╔════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║           DAP SDK Hash Table Performance Benchmark                         ║\n");
    printf("║                                                                            ║\n");
    printf("║  Comparing: dap_ht");
#ifdef BENCHMARK_UTHASH
    printf(", uthash");
#endif
#ifdef BENCHMARK_KHASH
    printf(", khash");
#endif
#ifdef BENCHMARK_STB_DS
    printf(", stb_ds");
#endif
    printf("\n");
    printf("╚════════════════════════════════════════════════════════════════════════════╝\n");
    
    srand(42);  // Reproducible results
    
    // Run benchmarks at different sizes
    run_benchmark_suite(SMALL_SIZE, "SMALL");
    run_benchmark_suite(MEDIUM_SIZE, "MEDIUM");
    run_benchmark_suite(LARGE_SIZE, "LARGE");
    
    // Optional: Very large test
    if (argc > 1 && strcmp(argv[1], "--xlarge") == 0) {
        run_benchmark_suite(XLARGE_SIZE, "XLARGE");
    }
    
    printf("\n");
    printf("Benchmark complete.\n");
    printf("To compare with competitors, download them first:\n");
    printf("  ./download_competitors.sh\n");
    printf("Then rebuild with: cmake -DBENCHMARK_COMPETITORS=ON ..\n");
    
    return 0;
}
