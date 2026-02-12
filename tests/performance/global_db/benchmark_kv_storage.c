/*
 * Key-Value Storage Benchmark
 *
 * Compares DAP GlobalDB native B-tree engine against:
 * - MDBX (if available)
 * - LMDB (if available)
 * - RocksDB (if available)
 * - LevelDB (if available)
 *
 * Metrics:
 * - Sequential write throughput (ops/sec, MB/sec)
 * - Random write throughput
 * - Sequential read throughput
 * - Random read throughput
 * - Range scan throughput
 * - Mixed workload (50% read, 50% write)
 *
 * Build with specific backends:
 *   -DWITH_MDBX=ON    (requires libmdbx)
 *   -DWITH_LMDB=ON    (requires liblmdb)
 *   -DWITH_ROCKSDB=ON (requires librocksdb)
 *   -DWITH_LEVELDB=ON (requires libleveldb)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

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

#define LOG_TAG "kv_benchmark"

// ============================================================================
// Configuration
// ============================================================================

#define DEFAULT_NUM_RECORDS     100000
#define DEFAULT_KEY_SIZE        16
#define DEFAULT_VALUE_SIZE      256
#define DEFAULT_BATCH_SIZE      1000

typedef struct benchmark_config {
    size_t num_records;
    size_t key_size;
    size_t value_size;
    size_t batch_size;
    const char *db_path;
    bool sync_writes;
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
    // Big-endian for proper sorting
    for (int i = 0; i < 8 && i < (int)key_size; i++) {
        key[key_size - 1 - i] = (index >> (i * 8)) & 0xFF;
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
// DAP Native B-tree backend
// ============================================================================

static benchmark_result_t s_bench_dap_sequential_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "DAP B-tree", .operation = "seq write" };
    
    char *db_path = dap_strdup_printf("%s/dap_bench.gdb", cfg->db_path);
    dap_global_db_btree_t *btree = dap_global_db_btree_create(db_path);
    DAP_DELETE(db_path);
    
    if (!btree) {
        log_it(L_ERROR, "Failed to create DAP B-tree");
        return result;
    }
    
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    
    double start = s_get_time_sec();
    
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        
        dap_global_db_btree_key_t btree_key = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        
        dap_global_db_btree_insert(btree, &btree_key, NULL, 0, 
                                    value, cfg->value_size, NULL, 0, 0);
    }
    
    double elapsed = s_get_time_sec() - start;
    
    DAP_DELETE(key);
    DAP_DELETE(value);
    dap_global_db_btree_close(btree);
    
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
    dap_global_db_btree_t *btree = dap_global_db_btree_create(db_path);
    DAP_DELETE(db_path);
    
    if (!btree)
        return result;
    
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    
    double start = s_get_time_sec();
    
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        s_generate_value(value, cfg->value_size, indices[i]);
        
        dap_global_db_btree_key_t btree_key = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        
        dap_global_db_btree_insert(btree, &btree_key, NULL, 0,
                                    value, cfg->value_size, NULL, 0, 0);
    }
    
    double elapsed = s_get_time_sec() - start;
    
    DAP_DELETE(key);
    DAP_DELETE(value);
    DAP_DELETE(indices);
    dap_global_db_btree_close(btree);
    
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
    dap_global_db_btree_t *btree = dap_global_db_btree_create(db_path);
    
    if (!btree) {
        DAP_DELETE(db_path);
        return result;
    }
    
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    
    // Populate
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        
        dap_global_db_btree_key_t btree_key = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        
        dap_global_db_btree_insert(btree, &btree_key, NULL, 0,
                                    value, cfg->value_size, NULL, 0, 0);
    }
    
    // Now read sequentially
    double start = s_get_time_sec();
    size_t read_count = 0;
    
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        
        dap_global_db_btree_key_t btree_key = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        
        void *read_value = NULL;
        uint32_t read_len = 0;
        
        if (dap_global_db_btree_get(btree, &btree_key, NULL, &read_value, &read_len,
                                     NULL, NULL, NULL) == 0) {
            read_count++;
            DAP_DELETE(read_value);
        }
    }
    
    double elapsed = s_get_time_sec() - start;
    
    DAP_DELETE(key);
    DAP_DELETE(value);
    DAP_DELETE(db_path);
    dap_global_db_btree_close(btree);
    
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
    dap_global_db_btree_t *btree = dap_global_db_btree_create(db_path);
    
    if (!btree) {
        DAP_DELETE(db_path);
        return result;
    }
    
    byte_t *key = DAP_NEW_SIZE(byte_t, cfg->key_size);
    byte_t *value = DAP_NEW_SIZE(byte_t, cfg->value_size);
    
    // Populate
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, i);
        s_generate_value(value, cfg->value_size, i);
        
        dap_global_db_btree_key_t btree_key = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        
        dap_global_db_btree_insert(btree, &btree_key, NULL, 0,
                                    value, cfg->value_size, NULL, 0, 0);
    }
    
    // Random read
    uint64_t *indices = s_generate_random_indices(cfg->num_records);
    
    double start = s_get_time_sec();
    size_t read_count = 0;
    
    for (size_t i = 0; i < cfg->num_records; i++) {
        s_generate_key(key, cfg->key_size, indices[i]);
        
        dap_global_db_btree_key_t btree_key = {
            .bets = *(uint64_t *)key,
            .becrc = cfg->key_size > 8 ? *(uint64_t *)(key + 8) : 0
        };
        
        void *read_value = NULL;
        uint32_t read_len = 0;
        
        if (dap_global_db_btree_get(btree, &btree_key, NULL, &read_value, &read_len,
                                     NULL, NULL, NULL) == 0) {
            read_count++;
            DAP_DELETE(read_value);
        }
    }
    
    double elapsed = s_get_time_sec() - start;
    
    DAP_DELETE(key);
    DAP_DELETE(value);
    DAP_DELETE(indices);
    DAP_DELETE(db_path);
    dap_global_db_btree_close(btree);
    
    result.num_ops = read_count;
    result.elapsed_sec = elapsed;
    result.ops_per_sec = read_count / elapsed;
    result.mb_per_sec = (read_count * (cfg->key_size + cfg->value_size)) / elapsed / 1024 / 1024;
    
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
    mkdir(db_path, 0755);
    
    MDBX_env *env = NULL;
    MDBX_dbi dbi;
    MDBX_txn *txn = NULL;
    
    if (mdbx_env_create(&env) != MDBX_SUCCESS) {
        DAP_DELETE(db_path);
        return result;
    }
    
    mdbx_env_set_geometry(env, 0, 0, 1024ULL * 1024 * 1024, -1, -1, -1);
    
    if (mdbx_env_open(env, db_path, MDBX_NOSUBDIR | MDBX_WRITEMAP, 0644) != MDBX_SUCCESS) {
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

// Add more MDBX benchmarks here...

#endif // WITH_MDBX

// ============================================================================
// LMDB backend (optional)
// ============================================================================

#ifdef WITH_LMDB

static benchmark_result_t s_bench_lmdb_sequential_write(const benchmark_config_t *cfg)
{
    benchmark_result_t result = { .name = "LMDB", .operation = "seq write" };
    
    char *db_path = dap_strdup_printf("%s/lmdb_bench", cfg->db_path);
    mkdir(db_path, 0755);
    
    MDB_env *env = NULL;
    MDB_dbi dbi;
    MDB_txn *txn = NULL;
    
    if (mdb_env_create(&env) != 0) {
        DAP_DELETE(db_path);
        return result;
    }
    
    mdb_env_set_mapsize(env, 1024ULL * 1024 * 1024);
    
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

#endif // WITH_ROCKSDB

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
    printf("Total data: %.2f MB\n",
           (double)(cfg->num_records * (cfg->key_size + cfg->value_size)) / 1024 / 1024);
    printf("=================================================================\n\n");
    
    benchmark_result_t results[32];
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
    
#ifdef WITH_MDBX
    printf("\nMDBX:\n");
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_mdbx_sequential_write(cfg);
    s_print_result(&results[result_count++]);
#endif
    
#ifdef WITH_LMDB
    printf("\nLMDB:\n");
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_lmdb_sequential_write(cfg);
    s_print_result(&results[result_count++]);
#endif
    
#ifdef WITH_ROCKSDB
    printf("\nRocksDB:\n");
    s_cleanup_db_dir(cfg->db_path);
    results[result_count] = s_bench_rocksdb_sequential_write(cfg);
    s_print_result(&results[result_count++]);
#endif
    
    printf("\n=================================================================\n");
    printf("Benchmark complete\n");
    printf("=================================================================\n\n");
    
    // Summary table
    printf("Summary (ops/sec):\n");
    printf("%-15s", "Backend");
    printf("%-15s", "Seq Write");
    printf("%-15s", "Rand Write");
    printf("%-15s", "Seq Read");
    printf("%-15s", "Rand Read");
    printf("\n");
    printf("---------------------------------------------------------------\n");
    
    // Group results by backend
    const char *backends[] = { "DAP B-tree", "MDBX", "LMDB", "RocksDB", "LevelDB" };
    const char *ops[] = { "seq write", "random write", "seq read", "random read" };
    
    for (size_t b = 0; b < sizeof(backends)/sizeof(backends[0]); b++) {
        bool has_results = false;
        for (int i = 0; i < result_count; i++) {
            if (strcmp(results[i].name, backends[b]) == 0) {
                has_results = true;
                break;
            }
        }
        if (!has_results)
            continue;
        
        printf("%-15s", backends[b]);
        for (size_t o = 0; o < sizeof(ops)/sizeof(ops[0]); o++) {
            bool found = false;
            for (int i = 0; i < result_count; i++) {
                if (strcmp(results[i].name, backends[b]) == 0 &&
                    strcmp(results[i].operation, ops[o]) == 0) {
                    printf("%-15.0f", results[i].ops_per_sec);
                    found = true;
                    break;
                }
            }
            if (!found)
                printf("%-15s", "-");
        }
        printf("\n");
    }
}

int main(int argc, char **argv)
{
    benchmark_config_t cfg = {
        .num_records = DEFAULT_NUM_RECORDS,
        .key_size = DEFAULT_KEY_SIZE,
        .value_size = DEFAULT_VALUE_SIZE,
        .batch_size = DEFAULT_BATCH_SIZE,
        .db_path = "/tmp/kv_benchmark",
        .sync_writes = false
    };
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            cfg.num_records = atol(argv[++i]);
        else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc)
            cfg.key_size = atol(argv[++i]);
        else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc)
            cfg.value_size = atol(argv[++i]);
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            cfg.db_path = argv[++i];
        else if (strcmp(argv[i], "--sync") == 0)
            cfg.sync_writes = true;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -n NUM      Number of records (default: %d)\n", DEFAULT_NUM_RECORDS);
            printf("  -k SIZE     Key size in bytes (default: %d)\n", DEFAULT_KEY_SIZE);
            printf("  -v SIZE     Value size in bytes (default: %d)\n", DEFAULT_VALUE_SIZE);
            printf("  -p PATH     Database directory (default: /tmp/kv_benchmark)\n");
            printf("  --sync      Enable sync writes\n");
            printf("  -h, --help  Show this help\n");
            return 0;
        }
    }
    
    // Initialize DAP common
    dap_common_init("kv_benchmark", NULL);
    
    // Create benchmark directory
    mkdir(cfg.db_path, 0755);
    
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
