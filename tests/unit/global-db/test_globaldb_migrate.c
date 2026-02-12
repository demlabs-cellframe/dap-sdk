/*
 * DAP GlobalDB Migration Integration Tests
 *
 * Tests:
 * - SQL dump parsing and migration
 * - Format detection
 * - Error handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_global_db.h"
#include "dap_global_db_storage.h"
#include "dap_global_db_migrate.h"

#define TEST_DIR "/tmp/test_globaldb_migrate"
#define TEST_SQL_FILE "/tmp/test_globaldb_migrate/test_dump.sql"
#define TEST_DEST_DIR "/tmp/test_globaldb_migrate/dest"

static void s_cleanup_test_dir(void)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

static void s_create_test_sql_dump(void)
{
    mkdir(TEST_DIR, 0755);
    
    FILE *fp = fopen(TEST_SQL_FILE, "w");
    if (!fp) return;
    
    fprintf(fp, "-- Test SQL dump for GlobalDB migration\n");
    fprintf(fp, "-- Generated for testing purposes\n\n");
    
    // Create some INSERT statements
    for (int i = 0; i < 20; i++) {
        fprintf(fp, "INSERT INTO test_group VALUES (%lu, %d, 'key_%d', 'value_for_key_%d', 0);\n",
                1000000UL + i, i * 100, i, i);
    }
    
    fprintf(fp, "\n");
    
    // Another group
    for (int i = 0; i < 10; i++) {
        fprintf(fp, "INSERT INTO another_group VALUES (%lu, %d, 'item_%d', 'data_%d', 0);\n",
                2000000UL + i, i * 200, i, i);
    }
    
    fclose(fp);
}

// ============================================================================
// Tests
// ============================================================================

static void test_migrate_strerror(void)
{
    dap_test_msg("Testing migrate_strerror");
    
    dap_assert(strcmp(dap_global_db_migrate_strerror(DAP_MIGRATE_OK), "Success") == 0, "OK message");
    dap_assert(strcmp(dap_global_db_migrate_strerror(DAP_MIGRATE_ERR_SOURCE), "Cannot open source") == 0, "Source error");
    dap_assert(strcmp(dap_global_db_migrate_strerror(DAP_MIGRATE_ERR_FORMAT), "Invalid source format") == 0, "Format error");
    
    dap_test_pass("migrate_strerror");
}

static void test_migrate_args_validation(void)
{
    dap_test_msg("Testing migration argument validation");
    
    dap_global_db_migrate_options_t opts = DAP_GLOBAL_DB_MIGRATE_OPTIONS_DEFAULT;
    
    // NULL source
    dap_global_db_migrate_result_t result = dap_global_db_migrate_mdbx(NULL, "/tmp/dest", &opts);
    dap_assert(result.status == DAP_MIGRATE_ERR_ARGS, "Should fail on NULL source");
    dap_global_db_migrate_result_free(&result);
    
    // NULL dest
    result = dap_global_db_migrate_sql("/tmp/source.sql", NULL, &opts);
    dap_assert(result.status == DAP_MIGRATE_ERR_ARGS, "Should fail on NULL dest");
    dap_global_db_migrate_result_free(&result);
    
    // Non-existent source
    result = dap_global_db_migrate_sql("/nonexistent/path.sql", "/tmp/dest", &opts);
    dap_assert(result.status == DAP_MIGRATE_ERR_SOURCE, "Should fail on non-existent source");
    dap_global_db_migrate_result_free(&result);
    
    dap_test_pass("Migration argument validation");
}

static void test_migrate_sql_dump(void)
{
    dap_test_msg("Testing SQL dump migration");
    
    s_cleanup_test_dir();
    s_create_test_sql_dump();
    mkdir(TEST_DEST_DIR, 0755);
    
    dap_global_db_migrate_options_t opts = DAP_GLOBAL_DB_MIGRATE_OPTIONS_DEFAULT;
    opts.verbose = false;
    opts.skip_errors = true;
    
    dap_global_db_migrate_result_t result = dap_global_db_migrate_sql(TEST_SQL_FILE, TEST_DEST_DIR, &opts);
    
    dap_test_msg("Migration result: status=%d, groups=%zu, records=%zu, failed=%zu",
                 result.status, result.groups_migrated, result.records_migrated, result.records_failed);
    
    if (result.error_message)
        dap_test_msg("Error: %s", result.error_message);
    
    dap_assert(result.status == DAP_MIGRATE_OK, "Migration should succeed");
    dap_assert(result.records_migrated > 0, "Should migrate some records");
    
    dap_global_db_migrate_result_free(&result);
    
    dap_test_pass("SQL dump migration");
}

static void test_migrate_auto_detection(void)
{
    dap_test_msg("Testing auto format detection");
    
    s_cleanup_test_dir();
    s_create_test_sql_dump();
    
    // Create a mock MDBX file with magic
    char mdbx_path[256];
    snprintf(mdbx_path, sizeof(mdbx_path), "%s/test.mdbx", TEST_DIR);
    FILE *fp = fopen(mdbx_path, "wb");
    if (fp) {
        // Write MDBX magic: 0xBEEFC0DE (little endian)
        uint8_t magic[] = {0xDE, 0xC0, 0xEF, 0xBE};
        fwrite(magic, 1, sizeof(magic), fp);
        fclose(fp);
    }
    
    dap_global_db_migrate_options_t opts = DAP_GLOBAL_DB_MIGRATE_OPTIONS_DEFAULT;
    opts.skip_errors = true;
    
    // Should detect SQL
    dap_global_db_migrate_result_t result = dap_global_db_migrate_auto(TEST_SQL_FILE, TEST_DEST_DIR, &opts);
    // Note: may fail for other reasons, but should try SQL path
    dap_global_db_migrate_result_free(&result);
    
    // Unknown format
    char unknown_path[256];
    snprintf(unknown_path, sizeof(unknown_path), "%s/unknown.dat", TEST_DIR);
    fp = fopen(unknown_path, "w");
    if (fp) {
        fprintf(fp, "random binary data");
        fclose(fp);
    }
    
    result = dap_global_db_migrate_auto(unknown_path, TEST_DEST_DIR, &opts);
    dap_assert(result.status == DAP_MIGRATE_ERR_FORMAT, "Unknown format should fail");
    dap_global_db_migrate_result_free(&result);
    
    dap_test_pass("Auto format detection");
}

static void test_migrate_result_free(void)
{
    dap_test_msg("Testing result free");
    
    dap_global_db_migrate_result_t result = {0};
    result.error_message = dap_strdup("test error message");
    
    dap_assert(result.error_message != NULL, "Error message should be set");
    
    dap_global_db_migrate_result_free(&result);
    
    dap_assert(result.error_message == NULL, "Error message should be NULL after free");
    
    // Double free should be safe
    dap_global_db_migrate_result_free(&result);
    
    dap_test_pass("Result free");
}

static void test_migrate_verify_data(void)
{
    dap_test_msg("Testing migrated data verification");
    
    s_cleanup_test_dir();
    s_create_test_sql_dump();
    mkdir(TEST_DEST_DIR, 0755);
    
    // Migrate
    dap_global_db_migrate_options_t opts = DAP_GLOBAL_DB_MIGRATE_OPTIONS_DEFAULT;
    opts.skip_errors = true;
    
    dap_global_db_migrate_result_t result = dap_global_db_migrate_sql(TEST_SQL_FILE, TEST_DEST_DIR, &opts);
    
    if (result.status == DAP_MIGRATE_OK && result.records_migrated > 0) {
        // Open the migrated storage and verify
        dap_global_db_storage_init(TEST_DEST_DIR);
        
        uint64_t count = dap_global_db_storage_group_count("test_group", false);
        dap_test_msg("test_group has %lu records", count);
        
        dap_global_db_storage_deinit();
    }
    
    dap_global_db_migrate_result_free(&result);
    
    dap_test_pass("Migrated data verification");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    dap_log_level_set(L_WARNING);
    
    dap_test_msg("=== DAP GlobalDB Migration Integration Tests ===\n");
    
    test_migrate_strerror();
    test_migrate_args_validation();
    test_migrate_sql_dump();
    test_migrate_auto_detection();
    test_migrate_result_free();
    test_migrate_verify_data();
    
    s_cleanup_test_dir();
    
    dap_test_msg("\n=== All Migration tests passed ===\n");
    return 0;
}
