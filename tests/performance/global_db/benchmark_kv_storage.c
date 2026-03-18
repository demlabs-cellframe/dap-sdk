/*
 * Key-Value Storage Benchmark
 *
 * Compares DAP GlobalDB native B-tree engine against:
 * - MDBX (if available)
 * - LMDB (if available)
 * - RocksDB (if available)
 * - LevelDB (if available)
 * - TidesDB (if available)
 * - WiredTiger (if available)
 * - Sophia (if available)
 *
 * Metrics:
 * - Sequential write throughput (ops/sec, MB/sec)
 * - Random write throughput
 * - Sequential read throughput
 * - Random read throughput
 *
 * Build with specific backends:
 *   -DWITH_MDBX=ON        (requires libmdbx)
 *   -DWITH_LMDB=ON        (requires liblmdb)
 *   -DWITH_ROCKSDB=ON     (requires librocksdb)
 *   -DWITH_LEVELDB=ON     (requires libleveldb)
 *   -DWITH_TIDESDB=ON     (requires libtidesdb + libzstd + liblz4 + libsnappy)
 *   -DWITH_WIREDTIGER=ON  (requires libwiredtiger)
 *   -DWITH_SOPHIA=ON      (requires libsophia)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_global_db_btree.h"

// Optional backends
#ifdef WITH_MDBX
#include <mdbx.h>
#endif

#ifdef WITH_LMDB
#include <lmdb.h>
#endif

#ifdef WITH_ROCKSDB
#include <rocksdb/c.h>
#endif

#ifdef WITH_LEVELDB
#include <leveldb/c.h>
#endif

#ifdef WITH_TIDESDB
#include <tidesdb/db.h>
#endif

#ifdef WITH_WIREDTIGER
#include <wiredtiger.h>
#endif

#ifdef WITH_SOPHIA
#include <sophia.h>
#endif

#define LOG_TAG "kv_benchmark"

// ============================================================================
// Configuration
// ============================================================================

#define DEFAULT_NUM_RECORDS     100000
#define DEFAULT_KEY_SIZE        16
#define DEFAULT_VALUE_SIZE      256
#define DEFAULT_BATCH_SIZE      1000
#define DEFAULT_NUM_THREADS     4

typedef struct benchmark_config {
    size_t num_records;
    size_t key_size;
    size_t value_size;
    size_t batch_size;
    size_t num_threads;
    const char *db_path;
    bool sync_writes;
    bool verify;
} benchmark_config_t;

// ============================================================================
// Timing utilities
// ============================================================================

static double s_get_time_sec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

typedef struct benchmark_result {
    const char *name;
    const char *operation;
    size_t num_ops;
    double elapsed_sec;
    double ops_per_sec;
    double mb_per_sec;
} benchmark_result_t;

static void s_print_result(const benchmark_result_t *r)
{
    printf("  %-12s %-20s: %8zu ops in %6.2f sec = %10.0f ops/sec, %7.2f MB/sec\n",
           r->name, r->operation, r->num_ops, r->elapsed_sec, 
           r->ops_per_sec, r->mb_per_sec);
}

// ============================================================================
// Data generation
// ============================================================================

static void s_generate_key(byte_t *key, size_t key_size, uint64_t index)
{
    memset(key, 0, key_size);
    // Big-endian encoding starting from byte 0.
    // btree_key uses bytes [0..15], so the index MUST reside in the first 8 bytes.
    // All other backends compare keys from byte 0 as well, so this is universally correct.
    // For key_size >= 8: bytes [0..7] = big-endian index, rest = 0.
    // For key_size <  8: truncated to lower bytes of big-endian representation.
    int n = 8 < (int)key_size ? 8 : (int)key_size;
    for (int i = 0; i < n; i++) {
        key[i] = (index >> ((n - 1 - i) * 8)) & 0xFF;
    }
}

static void s_generate_value(byte_t *value, size_t value_size, uint64_t seed)
{
    // Pseudo-random fill
    uint64_t state = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    for (size_t i = 0; i < value_size; i++) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        value[i] = (state >> 32) & 0xFF;
    }
}

static uint64_t *s_generate_random_indices(size_t count)
{
    uint64_t *indices = DAP_NEW_SIZE(uint64_t, count * sizeof(uint64_t));
    for (size_t i = 0; i < count; i++)
        indices[i] = i;
    
    // Fisher-Yates shuffle
    for (size_t i = count - 1; i > 0; i--) {
        size_t j = rand() % (i + 1);
        uint64_t tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }
    
    return indices;
}

// ============================================================================
// Map size helper for MDBX/LMDB
// ============================================================================

#if defined(WITH_MDBX) || defined(WITH_LMDB)
// Compute mapsize large enough to hold all records with B-tree overhead.
// Returns at least 1 GB; for large datasets returns 3× the raw data size
// (accounts for B-tree page overhead, free-space fragmentation, etc.).
static size_t s_compute_mapsize(const benchmark_config_t *cfg)
{
    size_t l_data_size = cfg->num_records * (cfg->key_size + cfg->value_size + 64/*per-record overhead*/);
    size_t l_min = 1024ULL * 1024 * 1024;  // 1 GB minimum
    size_t l_needed = l_data_size * 3;       // 3× safety margin
    return l_needed > l_min ? l_needed : l_min;
}
#endif

#ifdef WITH_MDBX
/* Minimum MDBX/LMDB pagesize to fit key_size bytes.
 * MDBX max_key_size = pagesize/4 - 6, so pagesize >= (key_size + 6) * 4.
 * Round up to next power of two, clamped to [512, 65536]. */
static intptr_t s_mdbx_pagesize_for_key(size_t key_size)
{
    size_t needed = (key_size + 6) * 4;
    intptr_t ps = 512;
    while ((size_t)ps < needed && ps < 65536)
        ps <<= 1;
    return ps;
}
#endif

// ============================================================================
// DAP Native B-tree backend
// ============================================================================

static benchmark_result_t s_bench_dap_sequential_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "DAP B-tree", .operation = "seq write" };
    
    char *db_path = dap_strdup_printf("%s/dap_bench.gdb", cfg->db_path);
    dap_global_db_t *btree = dap_global_db_create(db_path);
    DAP_DELETE(db_path);
    
    if (!btree) {
        log_it(L_ERROR, "Failed to create DAP B-tree");
        return result;
    }
    
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    
    double start = s_get_time_sec();
    
    dap_global_db_batch_begin(btree);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        
        dap_global_db_key_t btree_key = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        
        dap_global_db_insert(btree, &btree_key, NULL, 0, 
                                    value, cfg->value_size, NULL, 0, 0);
    }
    dap_global_db_batch_commit(btree);
    
    double elapsed = s_get_time_sec() - start;
    
    DAP_DELETE(key);
    DAP_DELETE(value);
    dap_global_db_close(btree);
    
    result.num_ops = cfg->num_records;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    
    return result;
}

static benchmark_result_t s_bench_dap_random_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "DAP B-tree", .operation = "random write" };
    
    char *db_path = dap_strdup_printf("%s/dap_bench_rw.gdb", cfg->db_path);
    dap_global_db_t *btree = dap_global_db_create(db_path);
    DAP_DELETE(db_path);
    
    if (!btree)
        return result;
    
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    
    double start = s_get_time_sec();
    
    dap_global_db_batch_begin(btree);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        s_generate_value(value, cfg->value_size, indices[i]);
        
        dap_global_db_key_t btree_key = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        
        dap_global_db_insert(btree, &btree_key, NULL, 0,
                                    value, cfg->value_size, NULL, 0, 0);
    }
    dap_global_db_batch_commit(btree);
    
    double elapsed = s_get_time_sec() - start;
    
    DAP_DELETE(key);
    DAP_DELETE(value);
    DAP_DELETE(indices);
    dap_global_db_close(btree);
    
    result.num_ops = cfg->num_records;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    
    return result;
}

static benchmark_result_t s_bench_dap_sequential_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "DAP B-tree", .operation = "seq read" };
    
    // First, populate the database
    char *db_path = dap_strdup_printf("%s/dap_bench_sr.gdb", cfg->db_path);
    dap_global_db_t *btree = dap_global_db_create(db_path);
    
    if (!btree) {
        DAP_DELETE(db_path);
        return result;
    }
    
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    
    // Populate
    dap_global_db_batch_begin(btree);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        
        dap_global_db_key_t btree_key = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        
        dap_global_db_insert(btree, &btree_key, NULL, 0,
                                    value, cfg->value_size, NULL, 0, 0);
    }
    dap_global_db_batch_commit(btree);
    
    // Verify record count
    uint64_t l_count = dap_global_db_count(btree);
    if (l_count != cfg->num_records)
        log_it(L_ERROR, "[FAIL] DAP B-tree: expected %zu records, got %lu",
               cfg->num_records, (unsigned long)l_count);

    // Incremental insertion + verify: insert from scratch in a fresh tree;
    // verify at logarithmic steps (i+1 power of 2) to keep total cost O(n)
    if (cfg->verify) {
        char *idb_path = dap_strdup_printf("%s/dap_bench_incr.gdb", cfg->db_path);
        dap_global_db_t *ibtree = dap_global_db_create(idb_path);
        DAP_DELETE(idb_path);
        if (ibtree) {
            byte_t *ikey = DAP_NEW_SIZE(byte_t, cfg->key_size);
            byte_t *ival = DAP_NEW_SIZE(byte_t, cfg->value_size);
            for (size_t i = 0; i < cfg->num_records; i++) {
                s_generate_key(ikey, cfg->key_size, i);
                s_generate_value(ival, cfg->value_size, i);
                dap_global_db_key_t bk = {
                    .bets = *(uint64_t *)ikey,
                    .becrc = cfg->key_size > 8 ? *(uint64_t *)(ikey + 8) : 0
                };
                dap_global_db_insert(ibtree, &bk, NULL, 0, ival, cfg->value_size, NULL, 0, 0);
                // Verify all keys 0..i only at logarithmic steps (i+1 is power of 2)
                int do_verify = (i == 0) || ((i & (i + 1)) == 0);
                if (do_verify) {
                    for (size_t j = 0; j <= i; j++) {
                        s_generate_key(ikey, cfg->key_size, j);
                        dap_global_db_key_t vk = {
                            .bets = *(uint64_t *)ikey,
                            .becrc = cfg->key_size > 8 ? *(uint64_t *)(ikey + 8) : 0
                        };
                        dap_global_db_ref_t vr;
                        int grc = dap_global_db_get_ref(ibtree, &vk, NULL, &vr, NULL, NULL);
                        if (grc != 0) {
                            log_it(L_ERROR, "[INCR_FAIL] after insert i=%zu, key j=%zu NOT FOUND (rc=%d)", i, j, grc);
                            char tag[64];
                            snprintf(tag, sizeof(tag), "LOST_j%zu_after_i%zu", j, i);
                            dap_global_db_debug_trace(ibtree, &vk, tag);
                            uint64_t real_count = 0;
                            int vrc = dap_global_db_verify(ibtree, &real_count);
                            log_it(L_DEBUG, "[DIAG] header.items=%lu verify_count=%lu verify_rc=%d",
                                   (unsigned long)dap_global_db_count(ibtree),
                                   (unsigned long)real_count, vrc);
                            goto incr_done;
                        }
                    }
                }
            }
            log_it(L_INFO, "[INCR_PASS] %zu incremental inserts verified OK", cfg->num_records);
incr_done:
            DAP_DELETE(ikey);
            DAP_DELETE(ival);
            dap_global_db_close(ibtree);
        }
    }

    // Verification pass (before timed read)
    if (cfg->verify) {
        byte_t *expected = DAP_NEW_SIZE(byte_t, cfg->value_size);
        size_t mismatches = 0;
        for (size_t i = 0; i < cfg->num_records; i++) {
            s_generate_key(key, cfg->key_size, i);
            s_generate_value(expected, cfg->value_size, i);
            dap_global_db_key_t vk = {
                .bets = *(uint64_t *)key,
                .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
            };
            dap_global_db_ref_t vr;
            if (dap_global_db_get_ref(btree, &vk, NULL, &vr, NULL, NULL) == 0) {
                if (vr.len != cfg->value_size ||
                    memcmp(vr.data, expected, cfg->value_size) != 0) {
                    if (mismatches < 5)
                        log_it(L_ERROR, "[MISMATCH] i=%zu: got len=%zu expected=%zu, data_match=%d",
                               i, (size_t)vr.len, cfg->value_size,
                               vr.len == cfg->value_size ? (memcmp(vr.data, expected, cfg->value_size) == 0) : -1);
                    mismatches++;
                }
            } else {
                if (mismatches < 5)
                    log_it(L_ERROR, "[NOT_FOUND] i=%zu: get_ref failed", i);
                mismatches++;
            }
        }
        DAP_DELETE(expected);
        if (mismatches > 0)
            log_it(L_ERROR, "[FAIL] DAP B-tree: %zu/%zu mismatches in verification",
                   mismatches, cfg->num_records);
        else
            log_it(L_INFO, "[PASS] DAP B-tree: %zu records verified OK", cfg->num_records);
    }

    // Now read sequentially (inside a read transaction for leaf cache benefit)
    double start = s_get_time_sec();
    size_t read_count = 0;
    
    dap_global_db_read_begin(btree);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        
        dap_global_db_key_t btree_key = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        
        dap_global_db_ref_t val_ref;
        
        if (dap_global_db_get_ref(btree, &btree_key, NULL, &val_ref,
                                         NULL, NULL) == 0) {
            read_count++;
        }
    }
    dap_global_db_read_end(btree);
    
    double elapsed = s_get_time_sec() - start;
    
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] DAP B-tree seq read: read %zu/%zu records",
               read_count, cfg->num_records);

    DAP_DELETE(key);
    DAP_DELETE(value);
    DAP_DELETE(db_path);
    dap_global_db_close(btree);
    
    result.num_ops = read_count;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    
    return result;
}

static benchmark_result_t s_bench_dap_random_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "DAP B-tree", .operation = "random read" };
    
    char *db_path = dap_strdup_printf("%s/dap_bench_rr.gdb", cfg->db_path);
    dap_global_db_t *btree = dap_global_db_create(db_path);
    
    if (!btree) {
        DAP_DELETE(db_path);
        return result;
    }
    
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    
    // Populate
    dap_global_db_batch_begin(btree);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        
        dap_global_db_key_t btree_key = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        
        dap_global_db_insert(btree, &btree_key, NULL, 0,
                                    value, cfg->value_size, NULL, 0, 0);
    }
    dap_global_db_batch_commit(btree);
    
    // Verify record count
    uint64_t l_rr_count = dap_global_db_count(btree);
    if (l_rr_count != cfg->num_records)
        log_it(L_ERROR, "[FAIL] DAP B-tree: expected %zu records, got %lu",
               cfg->num_records, (unsigned long)l_rr_count);

    // Random read (inside a read transaction for snapshot reuse)
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    
    double start = s_get_time_sec();
    size_t read_count = 0;
    
    dap_global_db_read_begin(btree);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        
        dap_global_db_key_t btree_key = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        
        dap_global_db_ref_t val_ref;
        
        if (dap_global_db_get_ref(btree, &btree_key, NULL, &val_ref,
                                         NULL, NULL) == 0) {
            read_count++;
        }
    }
    dap_global_db_read_end(btree);
    
    double elapsed = s_get_time_sec() - start;
    
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] DAP B-tree rand read: read %zu/%zu records",
               read_count, cfg->num_records);

    DAP_DELETE(key);
    DAP_DELETE(value);
    DAP_DELETE(indices);
    DAP_DELETE(db_path);
    dap_global_db_close(btree);
    
    result.num_ops = read_count;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    
    return result;
}

// ============================================================================
// DAP B-tree multi-threaded benchmarks
// ============================================================================

typedef struct mt_thread_arg {
    dap_global_db_t *btree;
    const benchmark_config_t *cfg;
    size_t start_index;
    size_t count;
    size_t ops_completed;
    char *db_path;
    const uint64_t *indices;
} mt_thread_arg_t;

static void *s_mt_reader_thread(void *a_arg)
{
    mt_thread_arg_t *arg = (mt_thread_arg_t *)a_arg;
    byte_t *key = DAP_NEW_SIZE(byte_t, arg->cfg->key_size);

    dap_global_db_read_begin(arg->btree);
    for (size_t i = arg->start_index; i < arg->start_index + arg->count; i++) {
        s_generate_key(key, arg->cfg->key_size, i);
        dap_global_db_key_t bk = {
            .bets = *(uint64_t *)key,
            .becrc = arg->cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        dap_global_db_ref_t vr;
        if (dap_global_db_get_ref(arg->btree, &bk, NULL, &vr, NULL, NULL) == 0)
            arg->ops_completed++;
    }
    dap_global_db_read_end(arg->btree);

    DAP_DELETE(key);
    return NULL;
}

static void *s_mt_random_reader_thread(void *a_arg)
{
    mt_thread_arg_t *arg = (mt_thread_arg_t *)a_arg;
    byte_t *key = DAP_NEW_SIZE(byte_t, arg->cfg->key_size);

    dap_global_db_read_begin(arg->btree);
    for (size_t i = arg->start_index; i < arg->start_index + arg->count; i++) {
        s_generate_key(key, arg->cfg->key_size, arg->indices[i]);
        dap_global_db_key_t bk = {
            .bets = *(uint64_t *)key,
            .becrc = arg->cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        dap_global_db_ref_t vr;
        if (dap_global_db_get_ref(arg->btree, &bk, NULL, &vr, NULL, NULL) == 0)
            arg->ops_completed++;
    }
    dap_global_db_read_end(arg->btree);

    DAP_DELETE(key);
    return NULL;
}

static void *s_mt_writer_thread(void *a_arg)
{
    mt_thread_arg_t *arg = (mt_thread_arg_t *)a_arg;
    byte_t *key = DAP_NEW_SIZE(byte_t, arg->cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, arg->cfg->value_size);

    for (size_t i = arg->start_index; i < arg->start_index + arg->count; i++) {
        s_generate_key(key, arg->cfg->key_size, i);
        s_generate_value(value, arg->cfg->value_size, i);
        dap_global_db_key_t bk = {
            .bets = *(uint64_t *)key,
            .becrc = arg->cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        if (dap_global_db_insert(arg->btree, &bk, NULL, 0,
                                        value, arg->cfg->value_size, NULL, 0, 0) == 0)
            arg->ops_completed++;
    }

    DAP_DELETE(key);
    DAP_DELETE(value);
    return NULL;
}

static void *s_mt_group_writer_thread(void *a_arg)
{
    mt_thread_arg_t *arg = (mt_thread_arg_t *)a_arg;

    dap_global_db_t *btree = dap_global_db_create(arg->db_path);
    if (!btree)
        return NULL;

    byte_t *key = DAP_NEW_SIZE(byte_t, arg->cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, arg->cfg->value_size);

    dap_global_db_batch_begin(btree);
    for (size_t i = 0; i < arg->count; i++) {
        s_generate_key(key, arg->cfg->key_size, i);
        s_generate_value(value, arg->cfg->value_size, i);
        dap_global_db_key_t bk = {
            .bets = *(uint64_t *)key,
            .becrc = arg->cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        if (dap_global_db_insert(btree, &bk, NULL, 0,
                                        value, arg->cfg->value_size, NULL, 0, 0) == 0)
            arg->ops_completed++;
    }
    dap_global_db_batch_commit(btree);

    DAP_DELETE(key);
    DAP_DELETE(value);
    dap_global_db_close(btree);
    return NULL;
}

static benchmark_result_t s_bench_dap_rdlock_overhead(void)
{
    benchmark_result_t result = { .name = "DAP B-tree", .operation = "rdlock overhead" };

    pthread_rwlock_t lock;
    pthread_rwlock_init(&lock, NULL);

    const size_t iterations = 10000000;
    double start = s_get_time_sec();
    for (size_t i = 0; i < iterations; i++) {
        pthread_rwlock_rdlock(&lock);
        pthread_rwlock_unlock(&lock);
    }
    double elapsed = s_get_time_sec() - start;

    pthread_rwlock_destroy(&lock);

    printf("  rdlock/unlock cycle: %.1f ns/op (%zu M ops in %.3f sec)\n",
           elapsed * 1e9 / (double)iterations, iterations / 1000000, elapsed);

    result.num_ops = iterations;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = (double)iterations / elapsed;
    result.mb_per_sec = 0;
    return result;
}

static benchmark_result_t s_bench_dap_parallel_reads(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "DAP B-tree", .operation = "parallel read" };
    char *db_path = dap_strdup_printf("%s/dap_bench_pr.gdb", cfg->db_path);
    dap_global_db_t *btree = dap_global_db_create(db_path);
    if (!btree) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    dap_global_db_batch_begin(btree);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        dap_global_db_key_t bk = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        dap_global_db_insert(btree, &bk, NULL, 0,
                                    value, cfg->value_size, NULL, 0, 0);
    }
    dap_global_db_batch_commit(btree);
    dap_global_db_sync(btree);
    DAP_DELETE(key);
    DAP_DELETE(value);

    size_t nt = cfg->num_threads;
    size_t per_thread = cfg->num_records / nt;

    pthread_t *threads = DAP_NEW_SIZE(pthread_t, nt * sizeof(pthread_t));
    mt_thread_arg_t *args = DAP_NEW_SIZE(mt_thread_arg_t, nt * sizeof(mt_thread_arg_t));

    for (size_t t = 0; t < nt; t++) {
        args[t] = (mt_thread_arg_t){
            .btree = btree,
            .cfg = cfg,
            .start_index = t * per_thread,
            .count = (t == nt - 1) ? (cfg->num_records - t * per_thread) : per_thread,
            .ops_completed = 0
        };
    }

    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++)
        pthread_create(&threads[t], NULL, s_mt_reader_thread, &args[t]);
    for (size_t t = 0; t < nt; t++)
        pthread_join(threads[t], NULL);
    double elapsed = s_get_time_sec() - start;

    size_t total_ops = 0;
    for (size_t t = 0; t < nt; t++)
        total_ops += args[t].ops_completed;

    DAP_DELETE(threads);
    DAP_DELETE(args);
    DAP_DELETE(db_path);
    dap_global_db_close(btree);

    result.num_ops = total_ops;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = (double)total_ops / elapsed;
    result.mb_per_sec = (double)(total_ops * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_dap_parallel_random_reads(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "DAP B-tree", .operation = "parallel rand read" };
    char *db_path = dap_strdup_printf("%s/dap_bench_prr.gdb", cfg->db_path);
    dap_global_db_t *btree = dap_global_db_create(db_path);
    if (!btree) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    dap_global_db_batch_begin(btree);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        dap_global_db_key_t bk = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        dap_global_db_insert(btree, &bk, NULL, 0,
                                    value, cfg->value_size, NULL, 0, 0);
    }
    dap_global_db_batch_commit(btree);
    dap_global_db_sync(btree);
    DAP_DELETE(key);
    DAP_DELETE(value);

    uint64_t *indices = s_generate_random_indices(cfg->num_records);

    size_t nt = cfg->num_threads;
    size_t per_thread = cfg->num_records / nt;

    pthread_t *threads = DAP_NEW_SIZE(pthread_t, nt * sizeof(pthread_t));
    mt_thread_arg_t *args = DAP_NEW_SIZE(mt_thread_arg_t, nt * sizeof(mt_thread_arg_t));

    for (size_t t = 0; t < nt; t++) {
        args[t] = (mt_thread_arg_t){
            .btree = btree,
            .cfg = cfg,
            .start_index = t * per_thread,
            .count = (t == nt - 1) ? (cfg->num_records - t * per_thread) : per_thread,
            .ops_completed = 0,
            .indices = indices
        };
    }

    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++)
        pthread_create(&threads[t], NULL, s_mt_random_reader_thread, &args[t]);
    for (size_t t = 0; t < nt; t++)
        pthread_join(threads[t], NULL);
    double elapsed = s_get_time_sec() - start;

    size_t total_ops = 0;
    for (size_t t = 0; t < nt; t++)
        total_ops += args[t].ops_completed;

    DAP_DELETE(threads);
    DAP_DELETE(args);
    DAP_DELETE(indices);
    DAP_DELETE(db_path);
    dap_global_db_close(btree);

    result.num_ops = total_ops;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = (double)total_ops / elapsed;
    result.mb_per_sec = (double)(total_ops * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_dap_parallel_group_writes(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "DAP B-tree", .operation = "parallel group write" };

    size_t nt = cfg->num_threads;
    size_t per_thread = cfg->num_records / nt;

    pthread_t *threads = DAP_NEW_SIZE(pthread_t, nt * sizeof(pthread_t));
    mt_thread_arg_t *args = DAP_NEW_SIZE(mt_thread_arg_t, nt * sizeof(mt_thread_arg_t));

    for (size_t t = 0; t < nt; t++) {
        args[t] = (mt_thread_arg_t){
            .cfg = cfg,
            .count = (t == nt - 1) ? (cfg->num_records - t * per_thread) : per_thread,
            .ops_completed = 0,
            .db_path = dap_strdup_printf("%s/dap_bench_pgw_%zu.gdb", cfg->db_path, t)
        };
    }

    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++)
        pthread_create(&threads[t], NULL, s_mt_group_writer_thread, &args[t]);
    for (size_t t = 0; t < nt; t++)
        pthread_join(threads[t], NULL);
    double elapsed = s_get_time_sec() - start;

    size_t total_ops = 0;
    for (size_t t = 0; t < nt; t++) {
        total_ops += args[t].ops_completed;
        DAP_DELETE(args[t].db_path);
    }

    DAP_DELETE(threads);
    DAP_DELETE(args);

    result.num_ops = total_ops;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = (double)total_ops / elapsed;
    result.mb_per_sec = (double)(total_ops * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_dap_mixed_read_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "DAP B-tree", .operation = "mixed R/W" };
    char *db_path = dap_strdup_printf("%s/dap_bench_mrw.gdb", cfg->db_path);
    dap_global_db_t *btree = dap_global_db_create(db_path);
    if (!btree) { DAP_DELETE(db_path); return result; }

    size_t pre_records = cfg->num_records / 2;
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    dap_global_db_batch_begin(btree);
    for (size_t i = 0; i < pre_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        dap_global_db_key_t bk = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        dap_global_db_insert(btree, &bk, NULL, 0,
                                    value, cfg->value_size, NULL, 0, 0);
    }
    dap_global_db_batch_commit(btree);
    dap_global_db_sync(btree);
    DAP_DELETE(key);
    DAP_DELETE(value);

    size_t nt = cfg->num_threads;
    size_t num_readers = nt > 1 ? nt - 1 : 1;
    size_t reader_range = pre_records / num_readers;
    size_t total_threads = (nt > 1) ? nt : 2;

    pthread_t *threads = DAP_NEW_SIZE(pthread_t, total_threads * sizeof(pthread_t));
    mt_thread_arg_t *args = DAP_NEW_SIZE(mt_thread_arg_t, total_threads * sizeof(mt_thread_arg_t));

    args[0] = (mt_thread_arg_t){
        .btree = btree, .cfg = cfg,
        .start_index = pre_records, .count = pre_records,
        .ops_completed = 0
    };

    for (size_t r = 0; r < num_readers; r++) {
        args[1 + r] = (mt_thread_arg_t){
            .btree = btree, .cfg = cfg,
            .start_index = r * reader_range,
            .count = (r == num_readers - 1) ? (pre_records - r * reader_range) : reader_range,
            .ops_completed = 0
        };
    }

    double start = s_get_time_sec();
    pthread_create(&threads[0], NULL, s_mt_writer_thread, &args[0]);
    for (size_t r = 0; r < num_readers; r++)
        pthread_create(&threads[1 + r], NULL, s_mt_reader_thread, &args[1 + r]);
    for (size_t t = 0; t < total_threads; t++)
        pthread_join(threads[t], NULL);
    double elapsed = s_get_time_sec() - start;

    size_t read_ops = 0, write_ops = args[0].ops_completed;
    for (size_t r = 0; r < num_readers; r++)
        read_ops += args[1 + r].ops_completed;
    size_t total_ops = read_ops + write_ops;

    printf("    %zu readers + 1 writer: reads=%zu, writes=%zu\n",
           num_readers, read_ops, write_ops);

    DAP_DELETE(threads);
    DAP_DELETE(args);
    DAP_DELETE(db_path);
    dap_global_db_close(btree);

    result.num_ops = total_ops;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = (double)total_ops / elapsed;
    result.mb_per_sec = (double)(total_ops * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

// ============================================================================
// MDBX backend (optional)
// ============================================================================

#ifdef WITH_MDBX

static benchmark_result_t s_bench_mdbx_sequential_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "MDBX", .operation = "seq write" };
    
    char *db_path = dap_strdup_printf("%s/mdbx_bench", cfg->db_path);
    // MDBX: path = directory (no MDBX_NOSUBDIR); env creates data/lock files inside
    if (dap_mkdir_with_parents(db_path) != 0) {
        DAP_DELETE(db_path);
        return result;
    }
    MDBX_env *env = NULL;
    MDBX_dbi dbi;
    MDBX_txn *txn = NULL;
    
    if (mdbx_env_create(&env) != MDBX_SUCCESS) {
        DAP_DELETE(db_path);
        return result;
    }
    
    mdbx_env_set_geometry(env, 0, 0, s_compute_mapsize(cfg), -1, -1, s_mdbx_pagesize_for_key(cfg->key_size));
    
    int l_rc = mdbx_env_open(env, db_path, MDBX_WRITEMAP, 0644);
    if (l_rc != MDBX_SUCCESS) {
        log_it(L_ERROR, "MDBX seq_write env_open failed: %d (%s) path=%s", l_rc, mdbx_strerror(l_rc), db_path);
        mdbx_env_close(env);
        DAP_DELETE(db_path);
        return result;
    }
    
    if (mdbx_txn_begin(env, NULL, 0, &txn) != MDBX_SUCCESS) {
        mdbx_env_close(env);
        DAP_DELETE(db_path);
        return result;
    }
    
    if (mdbx_dbi_open(txn, NULL, MDBX_CREATE, &dbi) != MDBX_SUCCESS) {
        mdbx_txn_abort(txn);
        mdbx_env_close(env);
        DAP_DELETE(db_path);
        return result;
    }
    
    mdbx_txn_commit(txn);
    txn = NULL;
    
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    
    double start = s_get_time_sec();
    
    for (size_t i = 0; i < cfg->num_records; i++) {
        if (i % cfg->batch_size == 0) {
            if (txn)
                mdbx_txn_commit(txn);
            mdbx_txn_begin(env, NULL, 0, &txn);
        }
        
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        
        MDBX_val mkey = { .iov_base = key, .iov_len = cfg->key_size };
        MDBX_val mval = { .iov_base = value, .iov_len = cfg->value_size };
        
        mdbx_put(txn, dbi, &mkey, &mval, 0);
    }
    
    if (txn)
        mdbx_txn_commit(txn);
    
    double elapsed = s_get_time_sec() - start;
    
    DAP_DELETE(key);
    DAP_DELETE(value);
    mdbx_env_close(env);
    DAP_DELETE(db_path);
    
    result.num_ops = cfg->num_records;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    
    return result;
}

// Helper: open MDBX env+dbi, populate with sequential data, return open env+dbi.
// Caller must pass db_path as directory; we do not use MDBX_NOSUBDIR (path = dir, not file).
static int s_mdbx_open_and_populate(const benchmark_config_t *cfg, const char *db_path,
                                     MDBX_env **out_env, MDBX_dbi *out_dbi)
{
    if (dap_mkdir_with_parents(db_path) != 0)
        return -1;
    if (mdbx_env_create(out_env) != MDBX_SUCCESS)
        return -1;
    mdbx_env_set_geometry(*out_env, 0, 0, s_compute_mapsize(cfg), -1, -1, s_mdbx_pagesize_for_key(cfg->key_size));
    if (mdbx_env_open(*out_env, db_path, MDBX_WRITEMAP, 0644) != MDBX_SUCCESS) {
        mdbx_env_close(*out_env);
        return -1;
    }
    MDBX_txn *txn = NULL;
    if (mdbx_txn_begin(*out_env, NULL, 0, &txn) != MDBX_SUCCESS) {
        mdbx_env_close(*out_env);
        return -1;
    }
    if (mdbx_dbi_open(txn, NULL, MDBX_CREATE, out_dbi) != MDBX_SUCCESS) {
        mdbx_txn_abort(txn);
        mdbx_env_close(*out_env);
        return -1;
    }
    mdbx_txn_commit(txn);
    txn = NULL;
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    for (size_t i = 0; i < cfg->num_records; i++) {
        if (i % cfg->batch_size == 0) {
            if (txn) mdbx_txn_commit(txn);
            mdbx_txn_begin(*out_env, NULL, 0, &txn);
        }
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        MDBX_val mk = { .iov_base = key, .iov_len = cfg->key_size };
        MDBX_val mv = { .iov_base = value, .iov_len = cfg->value_size };
        mdbx_put(txn, *out_dbi, &mk, &mv, 0);
    }
    if (txn) mdbx_txn_commit(txn);
    DAP_DELETE(key);
    DAP_DELETE(value);
    return 0;
}

static benchmark_result_t s_bench_mdbx_random_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "MDBX", .operation = "random write" };
    char *db_path = dap_strdup_printf("%s/mdbx_bench_rw", cfg->db_path);
    if (dap_mkdir_with_parents(db_path) != 0) { DAP_DELETE(db_path); return result; }
    MDBX_env *env = NULL;
    MDBX_dbi dbi;
    MDBX_txn *txn = NULL;
    if (mdbx_env_create(&env) != MDBX_SUCCESS) { DAP_DELETE(db_path); return result; }
    mdbx_env_set_geometry(env, 0, 0, s_compute_mapsize(cfg), -1, -1, s_mdbx_pagesize_for_key(cfg->key_size));
    if (mdbx_env_open(env, db_path, MDBX_WRITEMAP, 0644) != MDBX_SUCCESS) {
        mdbx_env_close(env); DAP_DELETE(db_path); return result;
    }
    if (mdbx_txn_begin(env, NULL, 0, &txn) != MDBX_SUCCESS) {
        mdbx_env_close(env); DAP_DELETE(db_path); return result;
    }
    if (mdbx_dbi_open(txn, NULL, MDBX_CREATE, &dbi) != MDBX_SUCCESS) {
        mdbx_txn_abort(txn); mdbx_env_close(env); DAP_DELETE(db_path); return result;
    }
    mdbx_txn_commit(txn); txn = NULL;

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    double start = s_get_time_sec();
    for (size_t i = 0; i < cfg->num_records; i++) {
        if (i % cfg->batch_size == 0) {
            if (txn) mdbx_txn_commit(txn);
            mdbx_txn_begin(env, NULL, 0, &txn);
        }
        s_generate_key(key, cfg->key_size, indices[i]);
        s_generate_value(value, cfg->value_size, indices[i]);
        MDBX_val mk = { .iov_base = key, .iov_len = cfg->key_size };
        MDBX_val mv = { .iov_base = value, .iov_len = cfg->value_size };
        mdbx_put(txn, dbi, &mk, &mv, 0);
    }
    if (txn) mdbx_txn_commit(txn);
    double elapsed = s_get_time_sec() - start;
    DAP_DELETE(key); DAP_DELETE(value); DAP_DELETE(indices);
    mdbx_env_close(env); DAP_DELETE(db_path);
    result.num_ops = cfg->num_records; result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_mdbx_sequential_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "MDBX", .operation = "seq read" };
    char *db_path = dap_strdup_printf("%s/mdbx_bench_sr", cfg->db_path);
    MDBX_env *env; MDBX_dbi dbi;
    if (s_mdbx_open_and_populate(cfg, db_path, &env, &dbi) != 0) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);

    // Verification pass
    if (cfg->verify) {
        byte_t *expected = DAP_NEW_SIZE(byte_t, cfg->value_size);
        MDBX_txn *vtxn = NULL;
        log_it(L_DEBUG, "MDBX verify: n=%zu dbi=%u env=%p", cfg->num_records, (unsigned)dbi, (void*)env);
        int txn_rc = mdbx_txn_begin(env, NULL, MDBX_TXN_RDONLY, &vtxn);
        if (txn_rc != MDBX_SUCCESS || !vtxn)
            log_it(L_ERROR, "MDBX verify: txn_begin failed rc=%d (%s)", txn_rc, mdbx_strerror(txn_rc));
        size_t mismatches = 0;
        for (size_t i = 0; i < cfg->num_records; i++) {
            s_generate_key(key, cfg->key_size, i);
            s_generate_value(expected, cfg->value_size, i);
            MDBX_val mk = { .iov_base = key, .iov_len = cfg->key_size };
            MDBX_val mv = {0};
            int grc = vtxn ? mdbx_get(vtxn, dbi, &mk, &mv) : MDBX_EINVAL;
            if (grc == MDBX_SUCCESS) {
                if (mv.iov_len != cfg->value_size) {
                    if (mismatches == 0)
                        log_it(L_ERROR, "MDBX verify: size mismatch i=%zu got=%zu want=%zu",
                               i, mv.iov_len, cfg->value_size);
                    mismatches++;
                } else if (memcmp(mv.iov_base, expected, cfg->value_size) != 0) {
                    if (mismatches == 0)
                        log_it(L_ERROR, "MDBX verify: data mismatch i=%zu", i);
                    mismatches++;
                }
            } else {
                if (mismatches == 0)
                    log_it(L_ERROR, "MDBX verify: first miss i=%zu rc=%d (%s)",
                           i, grc, mdbx_strerror(grc));
                mismatches++;
            }
        }
        if (vtxn) mdbx_txn_abort(vtxn);
        DAP_DELETE(expected);
        if (mismatches > 0)
            log_it(L_ERROR, "[FAIL] MDBX: %zu/%zu mismatches in verification",
                   mismatches, cfg->num_records);
        else
            log_it(L_INFO, "[PASS] MDBX: %zu records verified OK", cfg->num_records);
    }

    MDBX_txn *txn = NULL;
    mdbx_txn_begin(env, NULL, MDBX_TXN_RDONLY, &txn);
    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        MDBX_val mk = { .iov_base = key, .iov_len = cfg->key_size };
        MDBX_val mv;
        if (mdbx_get(txn, dbi, &mk, &mv) == MDBX_SUCCESS) read_count++;
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] MDBX seq read: read %zu/%zu records",
               read_count, cfg->num_records);
    mdbx_txn_abort(txn);
    DAP_DELETE(key); mdbx_env_close(env); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_mdbx_random_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "MDBX", .operation = "random read" };
    char *db_path = dap_strdup_printf("%s/mdbx_bench_rr", cfg->db_path);
    MDBX_env *env; MDBX_dbi dbi;
    if (s_mdbx_open_and_populate(cfg, db_path, &env, &dbi) != 0) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    MDBX_txn *txn = NULL;
    mdbx_txn_begin(env, NULL, MDBX_TXN_RDONLY, &txn);
    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        MDBX_val mk = { .iov_base = key, .iov_len = cfg->key_size };
        MDBX_val mv;
        if (mdbx_get(txn, dbi, &mk, &mv) == MDBX_SUCCESS) read_count++;
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] MDBX rand read: read %zu/%zu records",
               read_count, cfg->num_records);
    mdbx_txn_abort(txn);
    DAP_DELETE(key); DAP_DELETE(indices); mdbx_env_close(env); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

#endif // WITH_MDBX

// ============================================================================
// LMDB backend (optional)
// ============================================================================

#ifdef WITH_LMDB

static benchmark_result_t s_bench_lmdb_sequential_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "LMDB", .operation = "seq write" };
    
    char *db_path = dap_strdup_printf("%s/lmdb_bench", cfg->db_path);
    if (dap_mkdir_with_parents(db_path) != 0) { DAP_DELETE(db_path); return result; }
    MDB_env *env = NULL;
    MDB_dbi dbi;
    MDB_txn *txn = NULL;
    
    if (mdb_env_create(&env) != 0) {
        DAP_DELETE(db_path);
        return result;
    }
    
    mdb_env_set_mapsize(env, s_compute_mapsize(cfg));
    
    if (mdb_env_open(env, db_path, MDB_WRITEMAP | MDB_MAPASYNC, 0644) != 0) {
        mdb_env_close(env);
        DAP_DELETE(db_path);
        return result;
    }
    
    if (mdb_txn_begin(env, NULL, 0, &txn) != 0) {
        mdb_env_close(env);
        DAP_DELETE(db_path);
        return result;
    }
    
    if (mdb_dbi_open(txn, NULL, 0, &dbi) != 0) {
        mdb_txn_abort(txn);
        mdb_env_close(env);
        DAP_DELETE(db_path);
        return result;
    }
    
    mdb_txn_commit(txn);
    txn = NULL;
    
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    
    double start = s_get_time_sec();
    
    for (size_t i = 0; i < cfg->num_records; i++) {
        if (i % cfg->batch_size == 0) {
            if (txn)
                mdb_txn_commit(txn);
            mdb_txn_begin(env, NULL, 0, &txn);
        }
        
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        
        MDB_val mkey = { .mv_data = key, .mv_size = cfg->key_size };
        MDB_val mval = { .mv_data = value, .mv_size = cfg->value_size };
        
        mdb_put(txn, dbi, &mkey, &mval, 0);
    }
    
    if (txn)
        mdb_txn_commit(txn);
    
    double elapsed = s_get_time_sec() - start;
    
    DAP_DELETE(key);
    DAP_DELETE(value);
    mdb_env_close(env);
    DAP_DELETE(db_path);
    
    result.num_ops = cfg->num_records;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    
    return result;
}

// Helper: open LMDB env+dbi, populate with sequential data.
// Caller passes db_path as directory (LMDB creates data.mdb/lock.mdb inside); no MDB_NOSUBDIR.
static int s_lmdb_open_and_populate(const benchmark_config_t *cfg, const char *db_path,
                                     MDB_env **out_env, MDB_dbi *out_dbi)
{
    if (dap_mkdir_with_parents(db_path) != 0) return -1;
    if (mdb_env_create(out_env) != 0) return -1;
    mdb_env_set_mapsize(*out_env, s_compute_mapsize(cfg));
    if (mdb_env_open(*out_env, db_path, MDB_WRITEMAP | MDB_MAPASYNC, 0644) != 0) {
        mdb_env_close(*out_env); return -1;
    }
    MDB_txn *txn = NULL;
    if (mdb_txn_begin(*out_env, NULL, 0, &txn) != 0) {
        mdb_env_close(*out_env); return -1;
    }
    if (mdb_dbi_open(txn, NULL, 0, out_dbi) != 0) {
        mdb_txn_abort(txn); mdb_env_close(*out_env); return -1;
    }
    mdb_txn_commit(txn); txn = NULL;
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    for (size_t i = 0; i < cfg->num_records; i++) {
        if (i % cfg->batch_size == 0) {
            if (txn) mdb_txn_commit(txn);
            mdb_txn_begin(*out_env, NULL, 0, &txn);
        }
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        MDB_val mk = { .mv_data = key, .mv_size = cfg->key_size };
        MDB_val mv = { .mv_data = value, .mv_size = cfg->value_size };
        mdb_put(txn, *out_dbi, &mk, &mv, 0);
    }
    if (txn) mdb_txn_commit(txn);
    DAP_DELETE(key); DAP_DELETE(value);
    return 0;
}

static benchmark_result_t s_bench_lmdb_random_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "LMDB", .operation = "random write" };
    char *db_path = dap_strdup_printf("%s/lmdb_bench_rw", cfg->db_path);
    if (dap_mkdir_with_parents(db_path) != 0) { DAP_DELETE(db_path); return result; }
    MDB_env *env = NULL; MDB_dbi dbi; MDB_txn *txn = NULL;
    if (mdb_env_create(&env) != 0) { DAP_DELETE(db_path); return result; }
    mdb_env_set_mapsize(env, s_compute_mapsize(cfg));
    if (mdb_env_open(env, db_path, MDB_WRITEMAP | MDB_MAPASYNC, 0644) != 0) {
        mdb_env_close(env); DAP_DELETE(db_path); return result;
    }
    if (mdb_txn_begin(env, NULL, 0, &txn) != 0) {
        mdb_env_close(env); DAP_DELETE(db_path); return result;
    }
    if (mdb_dbi_open(txn, NULL, 0, &dbi) != 0) {
        mdb_txn_abort(txn); mdb_env_close(env); DAP_DELETE(db_path); return result;
    }
    mdb_txn_commit(txn); txn = NULL;

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    double start = s_get_time_sec();
    for (size_t i = 0; i < cfg->num_records; i++) {
        if (i % cfg->batch_size == 0) {
            if (txn) mdb_txn_commit(txn);
            mdb_txn_begin(env, NULL, 0, &txn);
        }
        s_generate_key(key, cfg->key_size, indices[i]);
        s_generate_value(value, cfg->value_size, indices[i]);
        MDB_val mk = { .mv_data = key, .mv_size = cfg->key_size };
        MDB_val mv = { .mv_data = value, .mv_size = cfg->value_size };
        mdb_put(txn, dbi, &mk, &mv, 0);
    }
    if (txn) mdb_txn_commit(txn);
    double elapsed = s_get_time_sec() - start;
    DAP_DELETE(key); DAP_DELETE(value); DAP_DELETE(indices);
    mdb_env_close(env); DAP_DELETE(db_path);
    result.num_ops = cfg->num_records; result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_lmdb_sequential_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "LMDB", .operation = "seq read" };
    char *db_path = dap_strdup_printf("%s/lmdb_bench_sr", cfg->db_path);
    MDB_env *env; MDB_dbi dbi;
    if (s_lmdb_open_and_populate(cfg, db_path, &env, &dbi) != 0) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);

    // Verification pass
    if (cfg->verify) {
        byte_t *expected = DAP_NEW_SIZE(byte_t, cfg->value_size);
        MDB_txn *vtxn = NULL;
        mdb_txn_begin(env, NULL, MDB_RDONLY, &vtxn);
        size_t mismatches = 0;
        for (size_t i = 0; i < cfg->num_records; i++) {
            s_generate_key(key, cfg->key_size, i);
            s_generate_value(expected, cfg->value_size, i);
            MDB_val mk = { .mv_data = key, .mv_size = cfg->key_size };
            MDB_val mv;
            if (mdb_get(vtxn, dbi, &mk, &mv) == 0) {
                if (mv.mv_size != cfg->value_size ||
                    memcmp(mv.mv_data, expected, cfg->value_size) != 0)
                    mismatches++;
            } else {
                mismatches++;
            }
        }
        mdb_txn_abort(vtxn);
        DAP_DELETE(expected);
        if (mismatches > 0)
            log_it(L_ERROR, "[FAIL] LMDB: %zu/%zu mismatches in verification",
                   mismatches, cfg->num_records);
        else
            log_it(L_INFO, "[PASS] LMDB: %zu records verified OK", cfg->num_records);
    }

    MDB_txn *txn = NULL;
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        MDB_val mk = { .mv_data = key, .mv_size = cfg->key_size };
        MDB_val mv;
        if (mdb_get(txn, dbi, &mk, &mv) == 0) read_count++;
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] LMDB seq read: read %zu/%zu records",
               read_count, cfg->num_records);
    mdb_txn_abort(txn);
    DAP_DELETE(key); mdb_env_close(env); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_lmdb_random_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "LMDB", .operation = "random read" };
    char *db_path = dap_strdup_printf("%s/lmdb_bench_rr", cfg->db_path);
    MDB_env *env; MDB_dbi dbi;
    if (s_lmdb_open_and_populate(cfg, db_path, &env, &dbi) != 0) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    MDB_txn *txn = NULL;
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        MDB_val mk = { .mv_data = key, .mv_size = cfg->key_size };
        MDB_val mv;
        if (mdb_get(txn, dbi, &mk, &mv) == 0) read_count++;
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] LMDB rand read: read %zu/%zu records",
               read_count, cfg->num_records);
    mdb_txn_abort(txn);
    DAP_DELETE(key); DAP_DELETE(indices); mdb_env_close(env); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

#endif // WITH_LMDB

// ============================================================================
// RocksDB backend (optional)
// ============================================================================

#ifdef WITH_ROCKSDB

static benchmark_result_t s_bench_rocksdb_sequential_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "RocksDB", .operation = "seq write" };
    
    char *db_path = dap_strdup_printf("%s/rocksdb_bench", cfg->db_path);
    
    char *err = NULL;
    rocksdb_options_t *options = rocksdb_options_create();
    rocksdb_options_set_create_if_missing(options, 1);
    
    rocksdb_t *db = rocksdb_open(options, db_path, &err);
    if (err) {
        log_it(L_ERROR, "RocksDB open error: %s", err);
        free(err);
        rocksdb_options_destroy(options);
        DAP_DELETE(db_path);
        return result;
    }
    
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    
    rocksdb_writeoptions_t *write_opts = rocksdb_writeoptions_create();
    if (!cfg->sync_writes)
        rocksdb_writeoptions_disable_WAL(write_opts, 1);
    
    double start = s_get_time_sec();
    
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        
        rocksdb_put(db, write_opts, (char *)key, cfg->key_size,
                    (char *)value, cfg->value_size, &err);
        if (err) {
            free(err);
            err = NULL;
        }
    }
    
    double elapsed = s_get_time_sec() - start;
    
    DAP_DELETE(key);
    DAP_DELETE(value);
    rocksdb_writeoptions_destroy(write_opts);
    rocksdb_close(db);
    rocksdb_options_destroy(options);
    DAP_DELETE(db_path);
    
    result.num_ops = cfg->num_records;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    
    return result;
}

// Helper: open RocksDB, populate with sequential data
static rocksdb_t *s_rocksdb_open_and_populate(const benchmark_config_t *cfg, const char *db_path,
                                               rocksdb_options_t **out_opts)
{
    *out_opts = rocksdb_options_create();
    rocksdb_options_set_create_if_missing(*out_opts, 1);
    char *err = NULL;
    rocksdb_t *db = rocksdb_open(*out_opts, db_path, &err);
    if (err) { free(err); rocksdb_options_destroy(*out_opts); return NULL; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    rocksdb_writeoptions_t *wo = rocksdb_writeoptions_create();
    rocksdb_writeoptions_disable_WAL(wo, 1);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        rocksdb_put(db, wo, (char *)key, cfg->key_size, (char *)value, cfg->value_size, &err);
        if (err) { free(err); err = NULL; }
    }
    rocksdb_writeoptions_destroy(wo);
    DAP_DELETE(key); DAP_DELETE(value);
    return db;
}

static benchmark_result_t s_bench_rocksdb_random_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "RocksDB", .operation = "random write" };
    char *db_path = dap_strdup_printf("%s/rocksdb_bench_rw", cfg->db_path);
    char *err = NULL;
    rocksdb_options_t *options = rocksdb_options_create();
    rocksdb_options_set_create_if_missing(options, 1);
    rocksdb_t *db = rocksdb_open(options, db_path, &err);
    if (err) { free(err); rocksdb_options_destroy(options); DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    rocksdb_writeoptions_t *wo = rocksdb_writeoptions_create();
    if (!cfg->sync_writes) rocksdb_writeoptions_disable_WAL(wo, 1);
    double start = s_get_time_sec();
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        s_generate_value(value, cfg->value_size, indices[i]);
        rocksdb_put(db, wo, (char *)key, cfg->key_size, (char *)value, cfg->value_size, &err);
        if (err) { free(err); err = NULL; }
    }
    double elapsed = s_get_time_sec() - start;
    rocksdb_writeoptions_destroy(wo);
    DAP_DELETE(key); DAP_DELETE(value); DAP_DELETE(indices);
    rocksdb_close(db); rocksdb_options_destroy(options); DAP_DELETE(db_path);
    result.num_ops = cfg->num_records; result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_rocksdb_sequential_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "RocksDB", .operation = "seq read" };
    char *db_path = dap_strdup_printf("%s/rocksdb_bench_sr", cfg->db_path);
    rocksdb_options_t *options;
    rocksdb_t *db = s_rocksdb_open_and_populate(cfg, db_path, &options);
    if (!db) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    rocksdb_readoptions_t *ro = rocksdb_readoptions_create();
    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        size_t vlen = 0;
        char *err = NULL;
        char *val = rocksdb_get(db, ro, (char *)key, cfg->key_size, &vlen, &err);
        if (val) { read_count++; free(val); }
        if (err) free(err);
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] RocksDB seq read: read %zu/%zu records",
               read_count, cfg->num_records);
    rocksdb_readoptions_destroy(ro);
    DAP_DELETE(key); rocksdb_close(db); rocksdb_options_destroy(options); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_rocksdb_random_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "RocksDB", .operation = "random read" };
    char *db_path = dap_strdup_printf("%s/rocksdb_bench_rr", cfg->db_path);
    rocksdb_options_t *options;
    rocksdb_t *db = s_rocksdb_open_and_populate(cfg, db_path, &options);
    if (!db) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    rocksdb_readoptions_t *ro = rocksdb_readoptions_create();
    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        size_t vlen = 0;
        char *err = NULL;
        char *val = rocksdb_get(db, ro, (char *)key, cfg->key_size, &vlen, &err);
        if (val) { read_count++; free(val); }
        if (err) free(err);
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] RocksDB rand read: read %zu/%zu records",
               read_count, cfg->num_records);
    rocksdb_readoptions_destroy(ro);
    DAP_DELETE(key); DAP_DELETE(indices); rocksdb_close(db); rocksdb_options_destroy(options); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

#endif // WITH_ROCKSDB

// ============================================================================
// LevelDB backend (optional)
// ============================================================================

#ifdef WITH_LEVELDB

static benchmark_result_t s_bench_leveldb_sequential_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "LevelDB", .operation = "seq write" };
    char *db_path = dap_strdup_printf("%s/leveldb_bench_sw", cfg->db_path);

    leveldb_options_t *opt = leveldb_options_create();
    leveldb_options_set_create_if_missing(opt, 1);
    char *err = NULL;
    leveldb_t *db = leveldb_open(opt, db_path, &err);
    if (err) { log_it(L_ERROR, "LevelDB open: %s", err); free(err);
               leveldb_options_destroy(opt); DAP_DELETE(db_path); return result; }

    leveldb_writeoptions_t *wo = leveldb_writeoptions_create();
    leveldb_writeoptions_set_sync(wo, cfg->sync_writes ? 1 : 0);
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);

    double start = s_get_time_sec();
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        err = NULL;
        leveldb_put(db, wo, (char *)key, cfg->key_size, (char *)value, cfg->value_size, &err);
        if (err) { free(err); err = NULL; }
    }
    double elapsed = s_get_time_sec() - start;

    DAP_DELETE(key); DAP_DELETE(value);
    leveldb_writeoptions_destroy(wo); leveldb_close(db);
    leveldb_options_destroy(opt); DAP_DELETE(db_path);
    result.num_ops = cfg->num_records; result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static leveldb_t *s_leveldb_open_and_populate(const benchmark_config_t *cfg, const char *db_path,
                                               leveldb_options_t **out_opt)
{
    *out_opt = leveldb_options_create();
    leveldb_options_set_create_if_missing(*out_opt, 1);
    char *err = NULL;
    leveldb_t *db = leveldb_open(*out_opt, db_path, &err);
    if (err) { free(err); leveldb_options_destroy(*out_opt); return NULL; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    leveldb_writeoptions_t *wo = leveldb_writeoptions_create();
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        err = NULL;
        leveldb_put(db, wo, (char *)key, cfg->key_size, (char *)value, cfg->value_size, &err);
        if (err) { free(err); err = NULL; }
    }
    leveldb_writeoptions_destroy(wo);
    DAP_DELETE(key); DAP_DELETE(value);
    return db;
}

static benchmark_result_t s_bench_leveldb_random_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "LevelDB", .operation = "random write" };
    char *db_path = dap_strdup_printf("%s/leveldb_bench_rw", cfg->db_path);

    leveldb_options_t *opt = leveldb_options_create();
    leveldb_options_set_create_if_missing(opt, 1);
    char *err = NULL;
    leveldb_t *db = leveldb_open(opt, db_path, &err);
    if (err) { free(err); leveldb_options_destroy(opt); DAP_DELETE(db_path); return result; }

    leveldb_writeoptions_t *wo = leveldb_writeoptions_create();
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);

    double start = s_get_time_sec();
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        s_generate_value(value, cfg->value_size, indices[i]);
        err = NULL;
        leveldb_put(db, wo, (char *)key, cfg->key_size, (char *)value, cfg->value_size, &err);
        if (err) { free(err); err = NULL; }
    }
    double elapsed = s_get_time_sec() - start;

    DAP_DELETE(key); DAP_DELETE(value); DAP_DELETE(indices);
    leveldb_writeoptions_destroy(wo); leveldb_close(db);
    leveldb_options_destroy(opt); DAP_DELETE(db_path);
    result.num_ops = cfg->num_records; result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_leveldb_sequential_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "LevelDB", .operation = "seq read" };
    char *db_path = dap_strdup_printf("%s/leveldb_bench_sr", cfg->db_path);
    leveldb_options_t *opt;
    leveldb_t *db = s_leveldb_open_and_populate(cfg, db_path, &opt);
    if (!db) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    leveldb_readoptions_t *ro = leveldb_readoptions_create();
    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        size_t vlen = 0; char *err = NULL;
        char *val = leveldb_get(db, ro, (char *)key, cfg->key_size, &vlen, &err);
        if (val) { read_count++; free(val); }
        if (err) free(err);
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] LevelDB seq read: read %zu/%zu records",
               read_count, cfg->num_records);
    leveldb_readoptions_destroy(ro); DAP_DELETE(key);
    leveldb_close(db); leveldb_options_destroy(opt); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_leveldb_random_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "LevelDB", .operation = "random read" };
    char *db_path = dap_strdup_printf("%s/leveldb_bench_rr", cfg->db_path);
    leveldb_options_t *opt;
    leveldb_t *db = s_leveldb_open_and_populate(cfg, db_path, &opt);
    if (!db) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    leveldb_readoptions_t *ro = leveldb_readoptions_create();
    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        size_t vlen = 0; char *err = NULL;
        char *val = leveldb_get(db, ro, (char *)key, cfg->key_size, &vlen, &err);
        if (val) { read_count++; free(val); }
        if (err) free(err);
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] LevelDB rand read: read %zu/%zu records",
               read_count, cfg->num_records);
    leveldb_readoptions_destroy(ro); DAP_DELETE(key); DAP_DELETE(indices);
    leveldb_close(db); leveldb_options_destroy(opt); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

#endif // WITH_LEVELDB

// ============================================================================
// TidesDB backend (optional)
// ============================================================================

#ifdef WITH_TIDESDB

// Helper: open TidesDB, create "bench" column family
static int s_tidesdb_open_db(const char *db_path, tidesdb_t **out_db,
                              tidesdb_column_family_t **out_cf)
{
    #ifdef DAP_OS_WINDOWS
    mkdir(db_path);
#else
    mkdir(db_path, 0755);
#endif
    tidesdb_init(NULL, NULL, NULL, NULL);

    tidesdb_config_t tdb_cfg = tidesdb_default_config();
    tdb_cfg.db_path = (char *)db_path;
    tdb_cfg.log_level = TDB_LOG_NONE;

    if (tidesdb_open(&tdb_cfg, out_db) != TDB_SUCCESS) {
        tidesdb_finalize(); return -1;
    }

    tidesdb_column_family_config_t cf_cfg = tidesdb_default_column_family_config();
    cf_cfg.compression_algorithm = TDB_COMPRESS_LZ4;
    if (tidesdb_create_column_family(*out_db, "bench", &cf_cfg) != TDB_SUCCESS) {
        tidesdb_close(*out_db); tidesdb_finalize(); return -1;
    }

    *out_cf = tidesdb_get_column_family(*out_db, "bench");
    if (!*out_cf) {
        tidesdb_close(*out_db); tidesdb_finalize(); return -1;
    }
    return 0;
}

static void s_tidesdb_close_db(tidesdb_t *db)
{
    tidesdb_close(db);
    tidesdb_finalize();
}

// Helper: open + populate with sequential data
static int s_tidesdb_open_and_populate(const benchmark_config_t *cfg, const char *db_path,
                                        tidesdb_t **out_db, tidesdb_column_family_t **out_cf)
{
    if (s_tidesdb_open_db(db_path, out_db, out_cf) != 0) return -1;

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);

    tidesdb_txn_t *txn = NULL;
    tidesdb_txn_begin(*out_db, &txn);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        tidesdb_txn_put(txn, *out_cf, key, cfg->key_size, value, cfg->value_size, -1);
    }
    tidesdb_txn_commit(txn);
    tidesdb_txn_free(txn);
    DAP_DELETE(key); DAP_DELETE(value);
    return 0;
}

static benchmark_result_t s_bench_tidesdb_sequential_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "TidesDB", .operation = "seq write" };
    char *db_path = dap_strdup_printf("%s/tidesdb_bench_sw", cfg->db_path);

    tidesdb_t *tdb = NULL; tidesdb_column_family_t *cf = NULL;
    if (s_tidesdb_open_db(db_path, &tdb, &cf) != 0) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);

    double start = s_get_time_sec();
    tidesdb_txn_t *txn = NULL;
    tidesdb_txn_begin(tdb, &txn);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        tidesdb_txn_put(txn, cf, key, cfg->key_size, value, cfg->value_size, -1);
    }
    tidesdb_txn_commit(txn);
    tidesdb_txn_free(txn);
    double elapsed = s_get_time_sec() - start;

    DAP_DELETE(key); DAP_DELETE(value);
    s_tidesdb_close_db(tdb); DAP_DELETE(db_path);
    result.num_ops = cfg->num_records; result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_tidesdb_random_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "TidesDB", .operation = "random write" };
    char *db_path = dap_strdup_printf("%s/tidesdb_bench_rw", cfg->db_path);

    tidesdb_t *tdb = NULL; tidesdb_column_family_t *cf = NULL;
    if (s_tidesdb_open_db(db_path, &tdb, &cf) != 0) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);

    double start = s_get_time_sec();
    tidesdb_txn_t *txn = NULL;
    tidesdb_txn_begin(tdb, &txn);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        s_generate_value(value, cfg->value_size, indices[i]);
        tidesdb_txn_put(txn, cf, key, cfg->key_size, value, cfg->value_size, -1);
    }
    tidesdb_txn_commit(txn);
    tidesdb_txn_free(txn);
    double elapsed = s_get_time_sec() - start;

    DAP_DELETE(key); DAP_DELETE(value); DAP_DELETE(indices);
    s_tidesdb_close_db(tdb); DAP_DELETE(db_path);
    result.num_ops = cfg->num_records; result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_tidesdb_sequential_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "TidesDB", .operation = "seq read" };
    char *db_path = dap_strdup_printf("%s/tidesdb_bench_sr", cfg->db_path);

    tidesdb_t *tdb = NULL; tidesdb_column_family_t *cf = NULL;
    if (s_tidesdb_open_and_populate(cfg, db_path, &tdb, &cf) != 0) {
        DAP_DELETE(db_path); return result;
    }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    tidesdb_txn_t *txn = NULL;
    tidesdb_txn_begin(tdb, &txn);

    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        size_t vlen = 0;
        uint8_t *val = NULL;
        if (tidesdb_txn_get(txn, cf, key, cfg->key_size, &val, &vlen) == TDB_SUCCESS && val) {
            read_count++;
            tidesdb_free(val);
        }
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] TidesDB seq read: read %zu/%zu records",
               read_count, cfg->num_records);
    tidesdb_txn_rollback(txn);
    tidesdb_txn_free(txn);
    DAP_DELETE(key); s_tidesdb_close_db(tdb); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_tidesdb_random_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "TidesDB", .operation = "random read" };
    char *db_path = dap_strdup_printf("%s/tidesdb_bench_rr", cfg->db_path);

    tidesdb_t *tdb = NULL; tidesdb_column_family_t *cf = NULL;
    if (s_tidesdb_open_and_populate(cfg, db_path, &tdb, &cf) != 0) {
        DAP_DELETE(db_path); return result;
    }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    tidesdb_txn_t *txn = NULL;
    tidesdb_txn_begin(tdb, &txn);

    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        size_t vlen = 0;
        uint8_t *val = NULL;
        if (tidesdb_txn_get(txn, cf, key, cfg->key_size, &val, &vlen) == TDB_SUCCESS && val) {
            read_count++;
            tidesdb_free(val);
        }
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] TidesDB rand read: read %zu/%zu records",
               read_count, cfg->num_records);
    tidesdb_txn_rollback(txn);
    tidesdb_txn_free(txn);
    DAP_DELETE(key); DAP_DELETE(indices);
    s_tidesdb_close_db(tdb); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

#endif // WITH_TIDESDB

// ============================================================================
// WiredTiger backend (optional)
// ============================================================================

#ifdef WITH_WIREDTIGER

static benchmark_result_t s_bench_wiredtiger_sequential_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "WiredTiger", .operation = "seq write" };
    char *db_path = dap_strdup_printf("%s/wt_bench_sw", cfg->db_path);
    #ifdef DAP_OS_WINDOWS
    mkdir(db_path);
#else
    mkdir(db_path, 0755);
#endif

    WT_CONNECTION *conn = NULL;
    if (wiredtiger_open(db_path, NULL, "create,cache_size=256M", &conn) != 0) {
        DAP_DELETE(db_path); return result; }
    WT_SESSION *session = NULL;
    conn->open_session(conn, NULL, NULL, &session);
    session->create(session, "table:bench", "key_format=u,value_format=u");

    WT_CURSOR *cursor = NULL;
    session->open_cursor(session, "table:bench", NULL, NULL, &cursor);

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    WT_ITEM wk, wv;

    double start = s_get_time_sec();
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        wk.data = key; wk.size = cfg->key_size;
        wv.data = value; wv.size = cfg->value_size;
        cursor->set_key(cursor, &wk);
        cursor->set_value(cursor, &wv);
        cursor->insert(cursor);
    }
    double elapsed = s_get_time_sec() - start;

    DAP_DELETE(key); DAP_DELETE(value);
    cursor->close(cursor); session->close(session, NULL);
    conn->close(conn, NULL); DAP_DELETE(db_path);
    result.num_ops = cfg->num_records; result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static int s_wt_open_and_populate(const benchmark_config_t *cfg, const char *db_path,
                                   WT_CONNECTION **out_conn)
{
    #ifdef DAP_OS_WINDOWS
    mkdir(db_path);
#else
    mkdir(db_path, 0755);
#endif
    if (wiredtiger_open(db_path, NULL, "create,cache_size=256M", out_conn) != 0) return -1;
    WT_SESSION *session = NULL;
    (*out_conn)->open_session(*out_conn, NULL, NULL, &session);
    session->create(session, "table:bench", "key_format=u,value_format=u");
    WT_CURSOR *cursor = NULL;
    session->open_cursor(session, "table:bench", NULL, NULL, &cursor);

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    WT_ITEM wk, wv;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        wk.data = key; wk.size = cfg->key_size;
        wv.data = value; wv.size = cfg->value_size;
        cursor->set_key(cursor, &wk);
        cursor->set_value(cursor, &wv);
        cursor->insert(cursor);
    }
    DAP_DELETE(key); DAP_DELETE(value);
    cursor->close(cursor); session->close(session, NULL);
    return 0;
}

static benchmark_result_t s_bench_wiredtiger_random_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "WiredTiger", .operation = "random write" };
    char *db_path = dap_strdup_printf("%s/wt_bench_rw", cfg->db_path);
    #ifdef DAP_OS_WINDOWS
    mkdir(db_path);
#else
    mkdir(db_path, 0755);
#endif

    WT_CONNECTION *conn = NULL;
    if (wiredtiger_open(db_path, NULL, "create,cache_size=256M", &conn) != 0) {
        DAP_DELETE(db_path); return result; }
    WT_SESSION *session = NULL;
    conn->open_session(conn, NULL, NULL, &session);
    session->create(session, "table:bench", "key_format=u,value_format=u");
    WT_CURSOR *cursor = NULL;
    session->open_cursor(session, "table:bench", NULL, NULL, &cursor);

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    WT_ITEM wk, wv;

    double start = s_get_time_sec();
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        s_generate_value(value, cfg->value_size, indices[i]);
        wk.data = key; wk.size = cfg->key_size;
        wv.data = value; wv.size = cfg->value_size;
        cursor->set_key(cursor, &wk);
        cursor->set_value(cursor, &wv);
        cursor->insert(cursor);
    }
    double elapsed = s_get_time_sec() - start;

    DAP_DELETE(key); DAP_DELETE(value); DAP_DELETE(indices);
    cursor->close(cursor); session->close(session, NULL);
    conn->close(conn, NULL); DAP_DELETE(db_path);
    result.num_ops = cfg->num_records; result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_wiredtiger_sequential_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "WiredTiger", .operation = "seq read" };
    char *db_path = dap_strdup_printf("%s/wt_bench_sr", cfg->db_path);
    WT_CONNECTION *conn;
    if (s_wt_open_and_populate(cfg, db_path, &conn) != 0) { DAP_DELETE(db_path); return result; }

    WT_SESSION *session = NULL;
    conn->open_session(conn, NULL, NULL, &session);
    WT_CURSOR *cursor = NULL;
    session->open_cursor(session, "table:bench", NULL, NULL, &cursor);

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    WT_ITEM wk, wv;
    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        wk.data = key; wk.size = cfg->key_size;
        cursor->set_key(cursor, &wk);
        if (cursor->search(cursor) == 0) {
            cursor->get_value(cursor, &wv);
            read_count++;
        }
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] WiredTiger seq read: read %zu/%zu records",
               read_count, cfg->num_records);
    DAP_DELETE(key); cursor->close(cursor); session->close(session, NULL);
    conn->close(conn, NULL); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_wiredtiger_random_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "WiredTiger", .operation = "random read" };
    char *db_path = dap_strdup_printf("%s/wt_bench_rr", cfg->db_path);
    WT_CONNECTION *conn;
    if (s_wt_open_and_populate(cfg, db_path, &conn) != 0) { DAP_DELETE(db_path); return result; }

    WT_SESSION *session = NULL;
    conn->open_session(conn, NULL, NULL, &session);
    WT_CURSOR *cursor = NULL;
    session->open_cursor(session, "table:bench", NULL, NULL, &cursor);

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    WT_ITEM wk, wv;
    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        wk.data = key; wk.size = cfg->key_size;
        cursor->set_key(cursor, &wk);
        if (cursor->search(cursor) == 0) {
            cursor->get_value(cursor, &wv);
            read_count++;
        }
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] WiredTiger rand read: read %zu/%zu records",
               read_count, cfg->num_records);
    DAP_DELETE(key); DAP_DELETE(indices);
    cursor->close(cursor); session->close(session, NULL);
    conn->close(conn, NULL); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

#endif // WITH_WIREDTIGER

// ============================================================================
// Sophia backend (optional)
// ============================================================================

#ifdef WITH_SOPHIA

static benchmark_result_t s_bench_sophia_sequential_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "Sophia", .operation = "seq write" };
    char *db_path = dap_strdup_printf("%s/sophia_bench_sw", cfg->db_path);
    #ifdef DAP_OS_WINDOWS
    mkdir(db_path);
#else
    mkdir(db_path, 0755);
#endif

    void *env = sp_env();
    sp_setstring(env, "sophia.path", db_path, 0);
    sp_setstring(env, "db", "bench", 0);
    if (sp_open(env) != 0) {
        sp_destroy(env); DAP_DELETE(db_path); return result; }
    void *db = sp_getobject(env, "db.bench");
    if (!db) { sp_destroy(env); DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);

    double start = s_get_time_sec();
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        void *doc = sp_document(db);
        sp_setstring(doc, "key", key, cfg->key_size);
        sp_setstring(doc, "value", value, cfg->value_size);
        sp_set(db, doc);
    }
    double elapsed = s_get_time_sec() - start;

    DAP_DELETE(key); DAP_DELETE(value);
    sp_destroy(env); DAP_DELETE(db_path);
    result.num_ops = cfg->num_records; result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static int s_sophia_open_and_populate(const benchmark_config_t *cfg, const char *db_path,
                                       void **out_env, void **out_db)
{
    #ifdef DAP_OS_WINDOWS
    mkdir(db_path);
#else
    mkdir(db_path, 0755);
#endif
    *out_env = sp_env();
    sp_setstring(*out_env, "sophia.path", db_path, 0);
    sp_setstring(*out_env, "db", "bench", 0);
    if (sp_open(*out_env) != 0) { sp_destroy(*out_env); return -1; }
    *out_db = sp_getobject(*out_env, "db.bench");
    if (!*out_db) { sp_destroy(*out_env); return -1; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        void *doc = sp_document(*out_db);
        sp_setstring(doc, "key", key, cfg->key_size);
        sp_setstring(doc, "value", value, cfg->value_size);
        sp_set(*out_db, doc);
    }
    DAP_DELETE(key); DAP_DELETE(value);
    return 0;
}

static benchmark_result_t s_bench_sophia_random_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "Sophia", .operation = "random write" };
    char *db_path = dap_strdup_printf("%s/sophia_bench_rw", cfg->db_path);
    #ifdef DAP_OS_WINDOWS
    mkdir(db_path);
#else
    mkdir(db_path, 0755);
#endif

    void *env = sp_env();
    sp_setstring(env, "sophia.path", db_path, 0);
    sp_setstring(env, "db", "bench", 0);
    if (sp_open(env) != 0) { sp_destroy(env); DAP_DELETE(db_path); return result; }
    void *db = sp_getobject(env, "db.bench");
    if (!db) { sp_destroy(env); DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);

    double start = s_get_time_sec();
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        s_generate_value(value, cfg->value_size, indices[i]);
        void *doc = sp_document(db);
        sp_setstring(doc, "key", key, cfg->key_size);
        sp_setstring(doc, "value", value, cfg->value_size);
        sp_set(db, doc);
    }
    double elapsed = s_get_time_sec() - start;

    DAP_DELETE(key); DAP_DELETE(value); DAP_DELETE(indices);
    sp_destroy(env); DAP_DELETE(db_path);
    result.num_ops = cfg->num_records; result.elapsed_sec = elapsed;
    result.ops_per_sec = cfg->num_records / elapsed;
    result.mb_per_sec = (cfg->num_records * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_sophia_sequential_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "Sophia", .operation = "seq read" };
    char *db_path = dap_strdup_printf("%s/sophia_bench_sr", cfg->db_path);
    void *env, *db;
    if (s_sophia_open_and_populate(cfg, db_path, &env, &db) != 0) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        void *doc = sp_document(db);
        sp_setstring(doc, "key", key, cfg->key_size);
        void *r = sp_get(db, doc);
        if (r) { read_count++; sp_destroy(r); }
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] Sophia seq read: read %zu/%zu records",
               read_count, cfg->num_records);
    DAP_DELETE(key); sp_destroy(env); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

static benchmark_result_t s_bench_sophia_random_read(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "Sophia", .operation = "random read" };
    char *db_path = dap_strdup_printf("%s/sophia_bench_rr", cfg->db_path);
    void *env, *db;
    if (s_sophia_open_and_populate(cfg, db_path, &env, &db) != 0) { DAP_DELETE(db_path); return result; }

    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    double start = s_get_time_sec();
    size_t read_count = 0;
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        void *doc = sp_document(db);
        sp_setstring(doc, "key", key, cfg->key_size);
        void *r = sp_get(db, doc);
        if (r) { read_count++; sp_destroy(r); }
    }
    double elapsed = s_get_time_sec() - start;
    if (read_count != cfg->num_records)
        log_it(L_WARNING, "[WARN] Sophia rand read: read %zu/%zu records",
               read_count, cfg->num_records);
    DAP_DELETE(key); DAP_DELETE(indices);
    sp_destroy(env); DAP_DELETE(db_path);
    result.num_ops = read_count; result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    return result;
}

#endif // WITH_SOPHIA

// ============================================================================
// Multi-threaded parallel read benchmarks for all backends
// ============================================================================

typedef struct mt_generic_arg {
    const benchmark_config_t *cfg;
    size_t start_index;
    size_t count;
    size_t ops_completed;
    const uint64_t *indices;
    void *h1, *h2;
    unsigned int h3;
} mt_generic_arg_t;

#define MT_BENCH_SETUP(BACKEND_LABEL, OP_LABEL, SUFFIX, OPEN_CALL) \
    benchmark_result_t result = { .name = BACKEND_LABEL, .operation = OP_LABEL }; \
    char *db_path = dap_strdup_printf("%s/" SUFFIX, cfg->db_path);

#define MT_BENCH_SPAWN(THREAD_FN) \
    size_t nt = cfg->num_threads; \
    size_t per_thread = cfg->num_records / nt; \
    pthread_t *threads = DAP_NEW_SIZE(pthread_t, nt * sizeof(pthread_t)); \
    mt_generic_arg_t *args = DAP_NEW_SIZE(mt_generic_arg_t, nt * sizeof(mt_generic_arg_t)); \
    for (size_t t = 0; t < nt; t++) { \
        args[t] = (mt_generic_arg_t){ \
            .cfg = cfg, .start_index = t * per_thread, \
            .count = (t == nt - 1) ? (cfg->num_records - t * per_thread) : per_thread, \
            .ops_completed = 0 }; \
    }

#define MT_BENCH_COLLECT \
    double elapsed = s_get_time_sec() - start; \
    size_t total_ops = 0; \
    for (size_t t = 0; t < nt; t++) total_ops += args[t].ops_completed; \
    DAP_DELETE(threads); DAP_DELETE(args); \
    result.num_ops = total_ops; result.elapsed_sec = elapsed; \
    result.ops_per_sec = (double)total_ops / elapsed; \
    result.mb_per_sec = (double)(total_ops * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;

// --- LMDB MT ---
#ifdef WITH_LMDB
static void *s_mt_lmdb_reader(void *a) {
    mt_generic_arg_t *arg = (mt_generic_arg_t *)a;
    MDB_env *env = (MDB_env *)arg->h1;
    MDB_dbi dbi = (MDB_dbi)arg->h3;
    byte_t *key = DAP_NEW_SIZE(byte_t, arg->cfg->key_size);
    MDB_txn *txn = NULL;
    mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
    for (size_t i = arg->start_index; i < arg->start_index + arg->count; i++) {
        size_t idx = arg->indices ? arg->indices[i] : i;
        s_generate_key(key, arg->cfg->key_size, idx);
        MDB_val mk = { .mv_data = key, .mv_size = arg->cfg->key_size }, mv;
        if (mdb_get(txn, dbi, &mk, &mv) == 0) arg->ops_completed++;
    }
    mdb_txn_abort(txn);
    DAP_DELETE(key);
    return NULL;
}
static benchmark_result_t s_bench_lmdb_parallel_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("LMDB", "parallel read", "lmdb_mt_pr", 0)
    MDB_env *env; MDB_dbi dbi;
    if (s_lmdb_open_and_populate(cfg, db_path, &env, &dbi) != 0) { DAP_DELETE(db_path); return result; }
    MT_BENCH_SPAWN(s_mt_lmdb_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = env; args[t].h3 = dbi; args[t].indices = NULL; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_lmdb_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    DAP_DELETE(db_path); mdb_env_close(env);
    return result;
}
static benchmark_result_t s_bench_lmdb_parallel_random_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("LMDB", "parallel rand read", "lmdb_mt_prr", 0)
    MDB_env *env; MDB_dbi dbi;
    if (s_lmdb_open_and_populate(cfg, db_path, &env, &dbi) != 0) { DAP_DELETE(db_path); return result; }
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    MT_BENCH_SPAWN(s_mt_lmdb_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = env; args[t].h3 = dbi; args[t].indices = indices; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_lmdb_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    DAP_DELETE(indices); DAP_DELETE(db_path); mdb_env_close(env);
    return result;
}
#endif

// --- MDBX MT ---
#ifdef WITH_MDBX
static void *s_mt_mdbx_reader(void *a) {
    mt_generic_arg_t *arg = (mt_generic_arg_t *)a;
    MDBX_env *env = (MDBX_env *)arg->h1;
    MDBX_dbi dbi = (MDBX_dbi)(uintptr_t)arg->h2;
    byte_t *key = DAP_NEW_SIZE(byte_t, arg->cfg->key_size);
    MDBX_txn *txn = NULL;
    mdbx_txn_begin(env, NULL, MDBX_TXN_RDONLY, &txn);
    for (size_t i = arg->start_index; i < arg->start_index + arg->count; i++) {
        size_t idx = arg->indices ? arg->indices[i] : i;
        s_generate_key(key, arg->cfg->key_size, idx);
        MDBX_val mk = { .iov_base = key, .iov_len = arg->cfg->key_size }, mv;
        if (mdbx_get(txn, dbi, &mk, &mv) == MDBX_SUCCESS) arg->ops_completed++;
    }
    mdbx_txn_abort(txn);
    DAP_DELETE(key);
    return NULL;
}
static benchmark_result_t s_bench_mdbx_parallel_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("MDBX", "parallel read", "mdbx_mt_pr", 0)
    MDBX_env *env; MDBX_dbi dbi;
    if (s_mdbx_open_and_populate(cfg, db_path, &env, &dbi) != 0) { DAP_DELETE(db_path); return result; }
    MT_BENCH_SPAWN(s_mt_mdbx_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = env; args[t].h2 = (void *)(uintptr_t)dbi; args[t].indices = NULL; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_mdbx_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    DAP_DELETE(db_path); mdbx_env_close(env);
    return result;
}
static benchmark_result_t s_bench_mdbx_parallel_random_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("MDBX", "parallel rand read", "mdbx_mt_prr", 0)
    MDBX_env *env; MDBX_dbi dbi;
    if (s_mdbx_open_and_populate(cfg, db_path, &env, &dbi) != 0) { DAP_DELETE(db_path); return result; }
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    MT_BENCH_SPAWN(s_mt_mdbx_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = env; args[t].h2 = (void *)(uintptr_t)dbi; args[t].indices = indices; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_mdbx_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    DAP_DELETE(indices); DAP_DELETE(db_path); mdbx_env_close(env);
    return result;
}
#endif

// --- RocksDB MT ---
#ifdef WITH_ROCKSDB
static void *s_mt_rocksdb_reader(void *a) {
    mt_generic_arg_t *arg = (mt_generic_arg_t *)a;
    rocksdb_t *db = (rocksdb_t *)arg->h1;
    byte_t *key = DAP_NEW_SIZE(byte_t, arg->cfg->key_size);
    rocksdb_readoptions_t *ro = rocksdb_readoptions_create();
    for (size_t i = arg->start_index; i < arg->start_index + arg->count; i++) {
        size_t idx = arg->indices ? arg->indices[i] : i;
        s_generate_key(key, arg->cfg->key_size, idx);
        size_t vlen = 0; char *err = NULL;
        char *val = rocksdb_get(db, ro, (char *)key, arg->cfg->key_size, &vlen, &err);
        if (val) { arg->ops_completed++; free(val); }
        if (err) free(err);
    }
    rocksdb_readoptions_destroy(ro);
    DAP_DELETE(key);
    return NULL;
}
static benchmark_result_t s_bench_rocksdb_parallel_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("RocksDB", "parallel read", "rocksdb_mt_pr", 0)
    rocksdb_options_t *opts;
    rocksdb_t *db = s_rocksdb_open_and_populate(cfg, db_path, &opts);
    if (!db) { DAP_DELETE(db_path); return result; }
    MT_BENCH_SPAWN(s_mt_rocksdb_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = db; args[t].indices = NULL; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_rocksdb_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    rocksdb_close(db); rocksdb_options_destroy(opts); DAP_DELETE(db_path);
    return result;
}
static benchmark_result_t s_bench_rocksdb_parallel_random_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("RocksDB", "parallel rand read", "rocksdb_mt_prr", 0)
    rocksdb_options_t *opts;
    rocksdb_t *db = s_rocksdb_open_and_populate(cfg, db_path, &opts);
    if (!db) { DAP_DELETE(db_path); return result; }
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    MT_BENCH_SPAWN(s_mt_rocksdb_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = db; args[t].indices = indices; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_rocksdb_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    DAP_DELETE(indices); rocksdb_close(db); rocksdb_options_destroy(opts); DAP_DELETE(db_path);
    return result;
}
#endif

// --- LevelDB MT ---
#ifdef WITH_LEVELDB
static void *s_mt_leveldb_reader(void *a) {
    mt_generic_arg_t *arg = (mt_generic_arg_t *)a;
    leveldb_t *db = (leveldb_t *)arg->h1;
    byte_t *key = DAP_NEW_SIZE(byte_t, arg->cfg->key_size);
    leveldb_readoptions_t *ro = leveldb_readoptions_create();
    for (size_t i = arg->start_index; i < arg->start_index + arg->count; i++) {
        size_t idx = arg->indices ? arg->indices[i] : i;
        s_generate_key(key, arg->cfg->key_size, idx);
        size_t vlen = 0; char *err = NULL;
        char *val = leveldb_get(db, ro, (char *)key, arg->cfg->key_size, &vlen, &err);
        if (val) { arg->ops_completed++; free(val); }
        if (err) free(err);
    }
    leveldb_readoptions_destroy(ro);
    DAP_DELETE(key);
    return NULL;
}
static benchmark_result_t s_bench_leveldb_parallel_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("LevelDB", "parallel read", "leveldb_mt_pr", 0)
    leveldb_options_t *opt;
    leveldb_t *db = s_leveldb_open_and_populate(cfg, db_path, &opt);
    if (!db) { DAP_DELETE(db_path); return result; }
    MT_BENCH_SPAWN(s_mt_leveldb_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = db; args[t].indices = NULL; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_leveldb_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    leveldb_close(db); leveldb_options_destroy(opt); DAP_DELETE(db_path);
    return result;
}
static benchmark_result_t s_bench_leveldb_parallel_random_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("LevelDB", "parallel rand read", "leveldb_mt_prr", 0)
    leveldb_options_t *opt;
    leveldb_t *db = s_leveldb_open_and_populate(cfg, db_path, &opt);
    if (!db) { DAP_DELETE(db_path); return result; }
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    MT_BENCH_SPAWN(s_mt_leveldb_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = db; args[t].indices = indices; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_leveldb_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    DAP_DELETE(indices); leveldb_close(db); leveldb_options_destroy(opt); DAP_DELETE(db_path);
    return result;
}
#endif

// --- WiredTiger MT ---
#ifdef WITH_WIREDTIGER
static void *s_mt_wt_reader(void *a) {
    mt_generic_arg_t *arg = (mt_generic_arg_t *)a;
    WT_CONNECTION *conn = (WT_CONNECTION *)arg->h1;
    WT_SESSION *session = NULL;
    conn->open_session(conn, NULL, NULL, &session);
    WT_CURSOR *cursor = NULL;
    session->open_cursor(session, "table:bench", NULL, NULL, &cursor);
    byte_t *key = DAP_NEW_SIZE(byte_t, arg->cfg->key_size);
    WT_ITEM wk, wv;
    for (size_t i = arg->start_index; i < arg->start_index + arg->count; i++) {
        size_t idx = arg->indices ? arg->indices[i] : i;
        s_generate_key(key, arg->cfg->key_size, idx);
        wk.data = key; wk.size = arg->cfg->key_size;
        cursor->set_key(cursor, &wk);
        if (cursor->search(cursor) == 0) {
            cursor->get_value(cursor, &wv);
            arg->ops_completed++;
        }
    }
    DAP_DELETE(key);
    cursor->close(cursor);
    session->close(session, NULL);
    return NULL;
}
static benchmark_result_t s_bench_wiredtiger_parallel_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("WiredTiger", "parallel read", "wt_mt_pr", 0)
    WT_CONNECTION *conn;
    if (s_wt_open_and_populate(cfg, db_path, &conn) != 0) { DAP_DELETE(db_path); return result; }
    MT_BENCH_SPAWN(s_mt_wt_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = conn; args[t].indices = NULL; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_wt_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    conn->close(conn, NULL); DAP_DELETE(db_path);
    return result;
}
static benchmark_result_t s_bench_wiredtiger_parallel_random_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("WiredTiger", "parallel rand read", "wt_mt_prr", 0)
    WT_CONNECTION *conn;
    if (s_wt_open_and_populate(cfg, db_path, &conn) != 0) { DAP_DELETE(db_path); return result; }
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    MT_BENCH_SPAWN(s_mt_wt_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = conn; args[t].indices = indices; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_wt_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    DAP_DELETE(indices); conn->close(conn, NULL); DAP_DELETE(db_path);
    return result;
}
#endif

// --- TidesDB MT ---
#ifdef WITH_TIDESDB
static void *s_mt_tidesdb_reader(void *a) {
    mt_generic_arg_t *arg = (mt_generic_arg_t *)a;
    tidesdb_t *tdb = (tidesdb_t *)arg->h1;
    tidesdb_column_family_t *cf = (tidesdb_column_family_t *)arg->h2;
    byte_t *key = DAP_NEW_SIZE(byte_t, arg->cfg->key_size);
    tidesdb_txn_t *txn = NULL;
    tidesdb_txn_begin(tdb, &txn);
    for (size_t i = arg->start_index; i < arg->start_index + arg->count; i++) {
        size_t idx = arg->indices ? arg->indices[i] : i;
        s_generate_key(key, arg->cfg->key_size, idx);
        size_t vlen = 0; uint8_t *val = NULL;
        if (tidesdb_txn_get(txn, cf, key, arg->cfg->key_size, &val, &vlen) == TDB_SUCCESS && val) {
            arg->ops_completed++;
            tidesdb_free(val);
        }
    }
    tidesdb_txn_rollback(txn);
    tidesdb_txn_free(txn);
    DAP_DELETE(key);
    return NULL;
}
static benchmark_result_t s_bench_tidesdb_parallel_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("TidesDB", "parallel read", "tidesdb_mt_pr", 0)
    tidesdb_t *tdb = NULL; tidesdb_column_family_t *cf = NULL;
    if (s_tidesdb_open_and_populate(cfg, db_path, &tdb, &cf) != 0) { DAP_DELETE(db_path); return result; }
    MT_BENCH_SPAWN(s_mt_tidesdb_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = tdb; args[t].h2 = cf; args[t].indices = NULL; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_tidesdb_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    DAP_DELETE(db_path); s_tidesdb_close_db(tdb);
    return result;
}
static benchmark_result_t s_bench_tidesdb_parallel_random_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("TidesDB", "parallel rand read", "tidesdb_mt_prr", 0)
    tidesdb_t *tdb = NULL; tidesdb_column_family_t *cf = NULL;
    if (s_tidesdb_open_and_populate(cfg, db_path, &tdb, &cf) != 0) { DAP_DELETE(db_path); return result; }
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    MT_BENCH_SPAWN(s_mt_tidesdb_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = tdb; args[t].h2 = cf; args[t].indices = indices; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_tidesdb_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    DAP_DELETE(indices); DAP_DELETE(db_path); s_tidesdb_close_db(tdb);
    return result;
}
#endif

// --- Sophia MT ---
#ifdef WITH_SOPHIA
static void *s_mt_sophia_reader(void *a) {
    mt_generic_arg_t *arg = (mt_generic_arg_t *)a;
    void *db = arg->h1;
    byte_t *key = DAP_NEW_SIZE(byte_t, arg->cfg->key_size);
    for (size_t i = arg->start_index; i < arg->start_index + arg->count; i++) {
        size_t idx = arg->indices ? arg->indices[i] : i;
        s_generate_key(key, arg->cfg->key_size, idx);
        void *doc = sp_document(db);
        sp_setstring(doc, "key", key, arg->cfg->key_size);
        void *r = sp_get(db, doc);
        if (r) { arg->ops_completed++; sp_destroy(r); }
    }
    DAP_DELETE(key);
    return NULL;
}
static benchmark_result_t s_bench_sophia_parallel_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("Sophia", "parallel read", "sophia_mt_pr", 0)
    void *env, *db;
    if (s_sophia_open_and_populate(cfg, db_path, &env, &db) != 0) { DAP_DELETE(db_path); return result; }
    MT_BENCH_SPAWN(s_mt_sophia_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = db; args[t].indices = NULL; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_sophia_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    DAP_DELETE(db_path); sp_destroy(env);
    return result;
}
static benchmark_result_t s_bench_sophia_parallel_random_reads(const benchmark_config_t *cfg) {
    MT_BENCH_SETUP("Sophia", "parallel rand read", "sophia_mt_prr", 0)
    void *env, *db;
    if (s_sophia_open_and_populate(cfg, db_path, &env, &db) != 0) { DAP_DELETE(db_path); return result; }
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    MT_BENCH_SPAWN(s_mt_sophia_reader)
    for (size_t t = 0; t < nt; t++) { args[t].h1 = db; args[t].indices = indices; }
    double start = s_get_time_sec();
    for (size_t t = 0; t < nt; t++) pthread_create(&threads[t], NULL, s_mt_sophia_reader, &args[t]);
    for (size_t t = 0; t < nt; t++) pthread_join(threads[t], NULL);
    MT_BENCH_COLLECT
    DAP_DELETE(indices); DAP_DELETE(db_path); sp_destroy(env);
    return result;
}
#endif

// ============================================================================
// Main benchmark runner
// ============================================================================

static void s_cleanup_db_dir(const char *path)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "rm -rf %s/*", path);
    system(cmd);
}

static void s_run_benchmarks(const benchmark_config_t *cfg)
{
    printf("\n");
    printf("=================================================================\n");
    printf("Key-Value Storage Benchmark\n");
    printf("=================================================================\n");
    printf("Records: %zu, Key size: %zu bytes, Value size: %zu bytes\n",
           cfg->num_records, cfg->key_size, cfg->value_size);
    printf("Total data: %.2f MB, Threads: %zu\n",
           (double)(cfg->num_records * (cfg->key_size + cfg->value_size)) / 1024 / 1024,
           cfg->num_threads);
    printf("=================================================================\n\n");
    
    benchmark_result_t results[128];
    int result_count = 0;
    
    // DAP Native B-tree benchmarks
    printf("DAP Native B-tree:\n");
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_dap_sequential_write(cfg);
    s_print_result(&results[result_count++]);
    
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_dap_random_write(cfg);
    s_print_result(&results[result_count++]);
    
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_dap_sequential_read(cfg);
    s_print_result(&results[result_count++]);
    
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_dap_random_read(cfg);
    s_print_result(&results[result_count++]);

    int dap_seq_read_idx = result_count - 2;
    int dap_rand_read_idx = result_count - 1;

    if (cfg->num_threads > 1) {
        printf("\nDAP B-tree multi-threaded (%zu threads):\n", cfg->num_threads);

        s_bench_dap_rdlock_overhead();

        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_dap_parallel_reads(cfg);
        s_print_result(&results[result_count]);
        if (results[dap_seq_read_idx].ops_per_sec > 0)
            printf("    scaling vs ST seq read: %.2fx\n",
                   results[result_count].ops_per_sec / results[dap_seq_read_idx].ops_per_sec);
        result_count++;

        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_dap_parallel_random_reads(cfg);
        s_print_result(&results[result_count]);
        if (results[dap_rand_read_idx].ops_per_sec > 0)
            printf("    scaling vs ST rand read: %.2fx\n",
                   results[result_count].ops_per_sec / results[dap_rand_read_idx].ops_per_sec);
        result_count++;

        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_dap_parallel_group_writes(cfg);
        s_print_result(&results[result_count++]);

        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_dap_mixed_read_write(cfg);
        s_print_result(&results[result_count++]);
    }

#ifdef WITH_MDBX
    printf("\nMDBX:\n");
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_mdbx_sequential_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_mdbx_random_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_mdbx_sequential_read(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_mdbx_random_read(cfg);
    s_print_result(&results[result_count++]);
#endif
    
#ifdef WITH_LMDB
    printf("\nLMDB:\n");
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_lmdb_sequential_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_lmdb_random_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_lmdb_sequential_read(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_lmdb_random_read(cfg);
    s_print_result(&results[result_count++]);
#endif
    
#ifdef WITH_ROCKSDB
    printf("\nRocksDB:\n");
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_rocksdb_sequential_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_rocksdb_random_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_rocksdb_sequential_read(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_rocksdb_random_read(cfg);
    s_print_result(&results[result_count++]);
#endif

#ifdef WITH_LEVELDB
    printf("\nLevelDB:\n");
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_leveldb_sequential_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_leveldb_random_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_leveldb_sequential_read(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_leveldb_random_read(cfg);
    s_print_result(&results[result_count++]);
#endif

#ifdef WITH_TIDESDB
    printf("\nTidesDB:\n");
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_tidesdb_sequential_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_tidesdb_random_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_tidesdb_sequential_read(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_tidesdb_random_read(cfg);
    s_print_result(&results[result_count++]);
#endif

#ifdef WITH_WIREDTIGER
    printf("\nWiredTiger:\n");
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_wiredtiger_sequential_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_wiredtiger_random_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_wiredtiger_sequential_read(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_wiredtiger_random_read(cfg);
    s_print_result(&results[result_count++]);
#endif

#ifdef WITH_SOPHIA
    printf("\nSophia:\n");
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_sophia_sequential_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_sophia_random_write(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_sophia_sequential_read(cfg);
    s_print_result(&results[result_count++]);
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_sophia_random_read(cfg);
    s_print_result(&results[result_count++]);
#endif

    // ------------------------------------------------------------------
    // Multi-threaded parallel reads for all backends
    // ------------------------------------------------------------------
    if (cfg->num_threads > 1) {
#ifdef WITH_LMDB
        printf("\nLMDB multi-threaded (%zu threads):\n", cfg->num_threads);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_lmdb_parallel_reads(cfg);
        s_print_result(&results[result_count++]);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_lmdb_parallel_random_reads(cfg);
        s_print_result(&results[result_count++]);
#endif
#ifdef WITH_MDBX
        printf("\nMDBX multi-threaded (%zu threads):\n", cfg->num_threads);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_mdbx_parallel_reads(cfg);
        s_print_result(&results[result_count++]);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_mdbx_parallel_random_reads(cfg);
        s_print_result(&results[result_count++]);
#endif
#ifdef WITH_ROCKSDB
        printf("\nRocksDB multi-threaded (%zu threads):\n", cfg->num_threads);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_rocksdb_parallel_reads(cfg);
        s_print_result(&results[result_count++]);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_rocksdb_parallel_random_reads(cfg);
        s_print_result(&results[result_count++]);
#endif
#ifdef WITH_LEVELDB
        printf("\nLevelDB multi-threaded (%zu threads):\n", cfg->num_threads);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_leveldb_parallel_reads(cfg);
        s_print_result(&results[result_count++]);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_leveldb_parallel_random_reads(cfg);
        s_print_result(&results[result_count++]);
#endif
#ifdef WITH_WIREDTIGER
        printf("\nWiredTiger multi-threaded (%zu threads):\n", cfg->num_threads);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_wiredtiger_parallel_reads(cfg);
        s_print_result(&results[result_count++]);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_wiredtiger_parallel_random_reads(cfg);
        s_print_result(&results[result_count++]);
#endif
#ifdef WITH_TIDESDB
        printf("\nTidesDB multi-threaded (%zu threads):\n", cfg->num_threads);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_tidesdb_parallel_reads(cfg);
        s_print_result(&results[result_count++]);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_tidesdb_parallel_random_reads(cfg);
        s_print_result(&results[result_count++]);
#endif
#ifdef WITH_SOPHIA
        printf("\nSophia multi-threaded (%zu threads):\n", cfg->num_threads);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_sophia_parallel_reads(cfg);
        s_print_result(&results[result_count++]);
        s_cleanup_db_dir(cfg->db_path);
        results[result_count] = s_bench_sophia_parallel_random_reads(cfg);
        s_print_result(&results[result_count++]);
#endif
    }

    printf("\n=================================================================\n");
    printf("Benchmark complete\n");
    printf("=================================================================\n\n");

    // ------------------------------------------------------------------
    // Summary table with ops/sec
    // ------------------------------------------------------------------
    const char *backends[] = {
        "DAP B-tree", "MDBX", "LMDB", "RocksDB", "LevelDB",
        "TidesDB", "WiredTiger", "Sophia"
    };
    const int num_backends = (int)(sizeof(backends) / sizeof(backends[0]));
    const char *ops[] = { "seq write", "random write", "seq read", "random read",
                           "parallel read", "parallel rand read" };
    const char *ops_short[] = { "Seq Write", "Rand Write", "Seq Read", "Rand Read",
                                "MT Read", "MT Rand Read" };
    const int num_ops = (int)(sizeof(ops) / sizeof(ops[0]));

    // Build lookup matrix: perf[backend][op] = ops_per_sec (0 = not tested)
    double perf[8][6] = {{0}};
    for (int b = 0; b < num_backends; b++) {
        for (int o = 0; o < num_ops; o++) {
            for (int i = 0; i < result_count; i++) {
                if (strcmp(results[i].name, backends[b]) == 0 &&
                    strcmp(results[i].operation, ops[o]) == 0) {
                    perf[b][o] = results[i].ops_per_sec;
                    break;
                }
            }
        }
    }

    printf("Summary (ops/sec):\n");
    printf("%-16s", "Backend");
    for (int o = 0; o < num_ops; o++)
        printf("%-16s", ops_short[o]);
    printf("\n-------------------------------------------------------------------------\n");

    for (int b = 0; b < num_backends; b++) {
        bool has = false;
        for (int o = 0; o < num_ops; o++)
            if (perf[b][o] > 0) { has = true; break; }
        if (!has) continue;

        printf("%-16s", backends[b]);
        for (int o = 0; o < num_ops; o++) {
            if (perf[b][o] > 0)
                printf("%-16.0f", perf[b][o]);
            else
                printf("%-16s", "-");
        }
        printf("\n");
    }

    // ------------------------------------------------------------------
    // Winner analysis table
    // ------------------------------------------------------------------
    printf("\n=================================================================\n");
    printf("Winner Analysis\n");
    printf("=================================================================\n\n");
    printf("%-15s %-15s %-15s %-10s  %s\n",
           "Operation", "Winner", "ops/sec", "Factor", "Details");
    printf("-----------------------------------------------------------------------\n");

    const int dap_idx = 0;  // DAP B-tree is always index 0
    for (int o = 0; o < num_ops; o++) {
        // Find winner (highest ops/sec) and runner-up
        int winner_idx = -1;
        double winner_val = 0;
        int runner_idx = -1;
        double runner_val = 0;

        for (int b = 0; b < num_backends; b++) {
            if (perf[b][o] <= 0) continue;
            if (perf[b][o] > winner_val) {
                runner_idx = winner_idx;
                runner_val = winner_val;
                winner_idx = b;
                winner_val = perf[b][o];
            } else if (perf[b][o] > runner_val) {
                runner_idx = b;
                runner_val = perf[b][o];
            }
        }
        if (winner_idx < 0) continue;

        double dap_val = perf[dap_idx][o];
        char factor_str[32];
        char detail_str[128];

        if (winner_idx == dap_idx) {
            // DAP SDK wins — show factor over closest competitor
            if (runner_idx >= 0 && runner_val > 0) {
                double factor = winner_val / runner_val;
                snprintf(factor_str, sizeof(factor_str), "%.2fx", factor);
                snprintf(detail_str, sizeof(detail_str),
                         "\033[32mDAP wins\033[0m vs %s (%.0f ops/s)",
                         backends[runner_idx], runner_val);
            } else {
                snprintf(factor_str, sizeof(factor_str), "-");
                snprintf(detail_str, sizeof(detail_str), "\033[32mDAP wins\033[0m (no competitors)");
            }
        } else {
            // Another backend wins — show how much faster it is than DAP
            if (dap_val > 0) {
                double factor = winner_val / dap_val;
                snprintf(factor_str, sizeof(factor_str), "%.2fx", factor);
                snprintf(detail_str, sizeof(detail_str),
                         "\033[31m%s faster\033[0m than DAP (%.0f ops/s)",
                         backends[winner_idx], dap_val);
            } else {
                snprintf(factor_str, sizeof(factor_str), "-");
                snprintf(detail_str, sizeof(detail_str), "DAP not tested");
            }
        }

        printf("%-15s %-15s %-15.0f %-10s  %s\n",
               ops_short[o], backends[winner_idx], winner_val, factor_str, detail_str);
    }

    // Final scoreboard
    int dap_wins = 0, total_ops = 0;
    for (int o = 0; o < num_ops; o++) {
        double best = 0;
        int best_idx = -1;
        for (int b = 0; b < num_backends; b++) {
            if (perf[b][o] > best) {
                best = perf[b][o];
                best_idx = b;
            }
        }
        if (best_idx >= 0) total_ops++;
        if (best_idx == dap_idx) dap_wins++;
    }

    printf("\n=================================================================\n");
    if (dap_wins == total_ops)
        printf("\033[1;32m  DAP B-tree WINS ALL %d/%d categories!\033[0m\n", dap_wins, total_ops);
    else if (dap_wins > total_ops / 2)
        printf("\033[1;33m  DAP B-tree wins %d/%d categories\033[0m\n", dap_wins, total_ops);
    else
        printf("\033[1;31m  DAP B-tree wins %d/%d categories\033[0m\n", dap_wins, total_ops);
    printf("=================================================================\n");
}

int main(int argc, char **argv)
{
    benchmark_config_t cfg = {
        .num_records = DEFAULT_NUM_RECORDS,
        .key_size = DEFAULT_KEY_SIZE,
        .value_size = DEFAULT_VALUE_SIZE,
        .batch_size = DEFAULT_BATCH_SIZE,
        .num_threads = DEFAULT_NUM_THREADS,
        .db_path = "/tmp/kv_benchmark",
        .sync_writes = false
    };
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            cfg.num_records = atol(argv[++i]);
        else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc)
            cfg.key_size = atol(argv[++i]);
        else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc)
            cfg.value_size = atol(argv[++i]);
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
            cfg.num_threads = atol(argv[++i]);
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
            cfg.num_threads = atol(argv[++i]);
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            cfg.db_path = argv[++i];
        else if (strcmp(argv[i], "--sync") == 0)
            cfg.sync_writes = true;
        else if (strcmp(argv[i], "--verify") == 0)
            cfg.verify = true;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -n NUM      Number of records (default: %d)\n", DEFAULT_NUM_RECORDS);
            printf("  -k SIZE     Key size in bytes (default: %d)\n", DEFAULT_KEY_SIZE);
            printf("  -v SIZE     Value size in bytes (default: %d)\n", DEFAULT_VALUE_SIZE);
            printf("  -t, --threads N  Number of threads for MT benchmarks (default: %d)\n",
                   DEFAULT_NUM_THREADS);
            printf("  -p PATH     Database directory (default: /tmp/kv_benchmark)\n");
            printf("  --sync      Enable sync writes\n");
            printf("  --verify    Verify data correctness after each populate\n");
            printf("  -h, --help  Show this help\n");
            return 0;
        }
    }
    
    // Initialize DAP common
    dap_common_init("kv_benchmark", NULL);
    
    // Create benchmark directory (and parents if -p path is nested)
    if (dap_mkdir_with_parents(cfg.db_path) != 0) {
        log_it(L_ERROR, "Failed to create benchmark directory: %s", cfg.db_path);
        return 1;
    }

    // Seed random
    srand(time(NULL));
    
    // Run benchmarks
    s_run_benchmarks(&cfg);
    
    // Cleanup
    s_cleanup_db_dir(cfg.db_path);
    rmdir(cfg.db_path);
    
    dap_common_deinit();
    
    return 0;
}
