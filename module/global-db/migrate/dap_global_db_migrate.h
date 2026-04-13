/*
 * DAP Global DB Migration Module
 *
 * Converts data from legacy storage formats to native B-tree format:
 * - MDBX files (direct binary migration)
 * - SQL dumps (SQLite/PostgreSQL format)
 *
 * Usage:
 *   dap_global_db_migrate_mdbx("/path/to/old.mdbx", "/path/to/new_storage/");
 *   dap_global_db_migrate_sql("/path/to/dump.sql", "/path/to/new_storage/");
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Migration result
// ============================================================================

typedef struct dap_global_db_migrate_result {
    int status;                 // 0 = success, negative = error
    size_t groups_migrated;     // Number of groups processed
    size_t records_migrated;    // Total records migrated
    size_t records_failed;      // Records that failed to migrate
    size_t bytes_migrated;      // Total data bytes migrated
    char *error_message;        // Error description (caller frees)
} dap_global_db_migrate_result_t;

// ============================================================================
// Progress callback
// ============================================================================

typedef void (*dap_global_db_migrate_progress_cb_t)(
    const char *a_group,        // Current group being migrated
    size_t a_current,           // Current record in group
    size_t a_total,             // Total records in group
    void *a_arg
);

// ============================================================================
// Migration options
// ============================================================================

typedef struct dap_global_db_migrate_options {
    bool verbose;                               // Print progress to stdout
    bool skip_errors;                           // Continue on individual record errors
    bool verify_after;                          // Verify data after migration
    size_t batch_size;                          // Records per transaction (0 = auto)
    dap_global_db_migrate_progress_cb_t progress_cb;
    void *progress_arg;
} dap_global_db_migrate_options_t;

// Default options
#define DAP_GLOBAL_DB_MIGRATE_OPTIONS_DEFAULT { \
    .verbose = false, \
    .skip_errors = false, \
    .verify_after = true, \
    .batch_size = 1000, \
    .progress_cb = NULL, \
    .progress_arg = NULL \
}

// ============================================================================
// Migration API
// ============================================================================

/**
 * @brief Migrate from MDBX database file
 * @param a_mdbx_path Path to source MDBX database
 * @param a_dest_path Destination storage directory
 * @param a_opts Migration options (NULL for defaults)
 * @return Migration result (caller must free error_message if set)
 */
dap_global_db_migrate_result_t dap_global_db_migrate_mdbx(
    const char *a_mdbx_path,
    const char *a_dest_path,
    const dap_global_db_migrate_options_t *a_opts
);

/**
 * @brief Migrate from SQL dump file
 * @param a_sql_path Path to SQL dump file
 * @param a_dest_path Destination storage directory
 * @param a_opts Migration options (NULL for defaults)
 * @return Migration result (caller must free error_message if set)
 *
 * Supported SQL formats:
 * - SQLite dump (from .dump command)
 * - PostgreSQL dump (plain format from pg_dump)
 */
dap_global_db_migrate_result_t dap_global_db_migrate_sql(
    const char *a_sql_path,
    const char *a_dest_path,
    const dap_global_db_migrate_options_t *a_opts
);

/**
 * @brief Auto-detect source format and migrate
 * @param a_source_path Path to source (MDBX file or SQL dump)
 * @param a_dest_path Destination storage directory
 * @param a_opts Migration options (NULL for defaults)
 * @return Migration result
 */
dap_global_db_migrate_result_t dap_global_db_migrate_auto(
    const char *a_source_path,
    const char *a_dest_path,
    const dap_global_db_migrate_options_t *a_opts
);

/**
 * @brief Free migration result resources
 * @param a_result Result to free
 */
void dap_global_db_migrate_result_free(dap_global_db_migrate_result_t *a_result);

/**
 * @brief Get human-readable error description
 * @param a_status Error status code
 * @return Static error string
 */
const char *dap_global_db_migrate_strerror(int a_status);

// Error codes
#define DAP_MIGRATE_OK              0
#define DAP_MIGRATE_ERR_ARGS       -1
#define DAP_MIGRATE_ERR_SOURCE     -2
#define DAP_MIGRATE_ERR_DEST       -3
#define DAP_MIGRATE_ERR_FORMAT     -4
#define DAP_MIGRATE_ERR_READ       -5
#define DAP_MIGRATE_ERR_WRITE      -6
#define DAP_MIGRATE_ERR_MEMORY     -7
#define DAP_MIGRATE_ERR_VERIFY     -8
#define DAP_MIGRATE_ERR_UNSUPPORTED -9

#ifdef __cplusplus
}
#endif
