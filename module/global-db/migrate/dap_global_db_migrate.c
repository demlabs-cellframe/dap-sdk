/*
 * DAP Global DB Migration - Core Implementation
 */

#include <string.h>
#include <stdio.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_global_db_migrate.h"

#define LOG_TAG "dap_global_db_migrate"

// External implementations
extern dap_global_db_migrate_result_t dap_global_db_migrate_mdbx_impl(
    const char *a_mdbx_path,
    const char *a_dest_path,
    const dap_global_db_migrate_options_t *a_opts
);

extern dap_global_db_migrate_result_t dap_global_db_migrate_sql_impl(
    const char *a_sql_path,
    const char *a_dest_path,
    const dap_global_db_migrate_options_t *a_opts
);

// ============================================================================
// Error handling
// ============================================================================

const char *dap_global_db_migrate_strerror(int a_status)
{
    switch (a_status) {
        case DAP_MIGRATE_OK:            return "Success";
        case DAP_MIGRATE_ERR_ARGS:      return "Invalid arguments";
        case DAP_MIGRATE_ERR_SOURCE:    return "Cannot open source";
        case DAP_MIGRATE_ERR_DEST:      return "Cannot create destination";
        case DAP_MIGRATE_ERR_FORMAT:    return "Invalid source format";
        case DAP_MIGRATE_ERR_READ:      return "Read error";
        case DAP_MIGRATE_ERR_WRITE:     return "Write error";
        case DAP_MIGRATE_ERR_MEMORY:    return "Memory allocation failed";
        case DAP_MIGRATE_ERR_VERIFY:    return "Verification failed";
        case DAP_MIGRATE_ERR_UNSUPPORTED: return "Unsupported feature";
        default:                        return "Unknown error";
    }
}

void dap_global_db_migrate_result_free(dap_global_db_migrate_result_t *a_result)
{
    if (a_result && a_result->error_message) {
        DAP_DELETE(a_result->error_message);
        a_result->error_message = NULL;
    }
}

// ============================================================================
// Format detection
// ============================================================================

typedef enum {
    MIGRATE_FORMAT_UNKNOWN,
    MIGRATE_FORMAT_MDBX,
    MIGRATE_FORMAT_SQL,
} migrate_format_t;

static migrate_format_t s_detect_format(const char *a_path)
{
    if (!a_path || !dap_file_test(a_path))
        return MIGRATE_FORMAT_UNKNOWN;
    
    // Check file extension
    const char *l_ext = strrchr(a_path, '.');
    if (l_ext) {
        if (strcmp(l_ext, ".mdbx") == 0 || strcmp(l_ext, ".lmdb") == 0)
            return MIGRATE_FORMAT_MDBX;
        if (strcmp(l_ext, ".sql") == 0)
            return MIGRATE_FORMAT_SQL;
    }
    
    // Check file signature
    FILE *l_fp = fopen(a_path, "rb");
    if (!l_fp)
        return MIGRATE_FORMAT_UNKNOWN;
    
    uint8_t l_magic[16];
    size_t l_read = fread(l_magic, 1, sizeof(l_magic), l_fp);
    fclose(l_fp);
    
    if (l_read >= 4) {
        // MDBX magic: 0xBEEFC0DE
        if (l_magic[0] == 0xDE && l_magic[1] == 0xC0 && 
            l_magic[2] == 0xEF && l_magic[3] == 0xBE)
            return MIGRATE_FORMAT_MDBX;
        
        // SQL dump usually starts with -- or CREATE/INSERT
        if (l_magic[0] == '-' && l_magic[1] == '-')
            return MIGRATE_FORMAT_SQL;
        if (memcmp(l_magic, "CREATE", 6) == 0 || memcmp(l_magic, "INSERT", 6) == 0)
            return MIGRATE_FORMAT_SQL;
    }
    
    return MIGRATE_FORMAT_UNKNOWN;
}

// ============================================================================
// Public API
// ============================================================================

dap_global_db_migrate_result_t dap_global_db_migrate_mdbx(
    const char *a_mdbx_path,
    const char *a_dest_path,
    const dap_global_db_migrate_options_t *a_opts)
{
    dap_global_db_migrate_result_t l_result = {0};
    
    if (!a_mdbx_path || !a_dest_path) {
        l_result.status = DAP_MIGRATE_ERR_ARGS;
        l_result.error_message = dap_strdup("Source and destination paths required");
        return l_result;
    }
    
    if (!dap_file_test(a_mdbx_path)) {
        l_result.status = DAP_MIGRATE_ERR_SOURCE;
        l_result.error_message = dap_strdup_printf("Source not found: %s", a_mdbx_path);
        return l_result;
    }
    
    // Create destination directory
    if (!dap_dir_test(a_dest_path)) {
        if (dap_mkdir_with_parents(a_dest_path) != 0) {
            l_result.status = DAP_MIGRATE_ERR_DEST;
            l_result.error_message = dap_strdup_printf("Cannot create destination: %s", a_dest_path);
            return l_result;
        }
    }
    
    dap_global_db_migrate_options_t l_opts = DAP_GLOBAL_DB_MIGRATE_OPTIONS_DEFAULT;
    if (a_opts)
        l_opts = *a_opts;
    
    if (l_opts.verbose)
        log_it(L_INFO, "Migrating MDBX: %s -> %s", a_mdbx_path, a_dest_path);
    
    return dap_global_db_migrate_mdbx_impl(a_mdbx_path, a_dest_path, &l_opts);
}

dap_global_db_migrate_result_t dap_global_db_migrate_sql(
    const char *a_sql_path,
    const char *a_dest_path,
    const dap_global_db_migrate_options_t *a_opts)
{
    dap_global_db_migrate_result_t l_result = {0};
    
    if (!a_sql_path || !a_dest_path) {
        l_result.status = DAP_MIGRATE_ERR_ARGS;
        l_result.error_message = dap_strdup("Source and destination paths required");
        return l_result;
    }
    
    if (!dap_file_test(a_sql_path)) {
        l_result.status = DAP_MIGRATE_ERR_SOURCE;
        l_result.error_message = dap_strdup_printf("Source not found: %s", a_sql_path);
        return l_result;
    }
    
    // Create destination directory
    if (!dap_dir_test(a_dest_path)) {
        if (dap_mkdir_with_parents(a_dest_path) != 0) {
            l_result.status = DAP_MIGRATE_ERR_DEST;
            l_result.error_message = dap_strdup_printf("Cannot create destination: %s", a_dest_path);
            return l_result;
        }
    }
    
    dap_global_db_migrate_options_t l_opts = DAP_GLOBAL_DB_MIGRATE_OPTIONS_DEFAULT;
    if (a_opts)
        l_opts = *a_opts;
    
    if (l_opts.verbose)
        log_it(L_INFO, "Migrating SQL dump: %s -> %s", a_sql_path, a_dest_path);
    
    return dap_global_db_migrate_sql_impl(a_sql_path, a_dest_path, &l_opts);
}

dap_global_db_migrate_result_t dap_global_db_migrate_auto(
    const char *a_source_path,
    const char *a_dest_path,
    const dap_global_db_migrate_options_t *a_opts)
{
    dap_global_db_migrate_result_t l_result = {0};
    
    migrate_format_t l_format = s_detect_format(a_source_path);
    
    switch (l_format) {
        case MIGRATE_FORMAT_MDBX:
            return dap_global_db_migrate_mdbx(a_source_path, a_dest_path, a_opts);
        
        case MIGRATE_FORMAT_SQL:
            return dap_global_db_migrate_sql(a_source_path, a_dest_path, a_opts);
        
        default:
            l_result.status = DAP_MIGRATE_ERR_FORMAT;
            l_result.error_message = dap_strdup_printf(
                "Cannot detect format of %s (use explicit mdbx/sql function)", a_source_path);
            return l_result;
    }
}
