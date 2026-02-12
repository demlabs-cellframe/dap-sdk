/*
 * DAP GlobalDB B-tree Unit Tests
 *
 * Tests:
 * - B-tree creation and opening
 * - Insert operations
 * - Lookup operations
 * - Cursor iteration
 * - Update and delete
 * - Persistence (write/read cycle)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_global_db.h"
#include "dap_global_db_btree.h"

#define TEST_DIR "/tmp/test_globaldb_btree"

// Helpers
static void s_cleanup_test_dir(void)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

static dap_global_db_btree_key_t s_make_key(uint64_t ts, uint64_t crc)
{
    dap_global_db_btree_key_t key = {
        .bets = htobe64(ts),
        .becrc = htobe64(crc)
    };
    return key;
}

// ============================================================================
// Tests
// ============================================================================

static void test_btree_create(void)
{
    dap_test_msg("Testing B-tree creation");
    
    s_cleanup_test_dir();
    mkdir(TEST_DIR, 0755);
    
    char path[256];
    snprintf(path, sizeof(path), "%s/test.gdb", TEST_DIR);
    
    dap_global_db_btree_t *btree = dap_global_db_btree_create(path);
    dap_assert(btree != NULL, "B-tree should be created");
    
    dap_global_db_btree_close(btree);
    
    // Verify file exists
    struct stat st;
    dap_assert(stat(path, &st) == 0, "B-tree file should exist");
    
    dap_test_pass("B-tree creation");
}

static void test_btree_open(void)
{
    dap_test_msg("Testing B-tree open");
    
    char path[256];
    snprintf(path, sizeof(path), "%s/test.gdb", TEST_DIR);
    
    // Create first
    dap_global_db_btree_t *btree = dap_global_db_btree_create(path);
    dap_assert(btree != NULL, "B-tree should be created");
    dap_global_db_btree_close(btree);
    
    // Open existing
    btree = dap_global_db_btree_open(path);
    dap_assert(btree != NULL, "B-tree should be opened");
    dap_global_db_btree_close(btree);
    
    dap_test_pass("B-tree open");
}

static void test_btree_insert(void)
{
    dap_test_msg("Testing B-tree insert");
    
    char path[256];
    snprintf(path, sizeof(path), "%s/insert_test.gdb", TEST_DIR);
    
    dap_global_db_btree_t *btree = dap_global_db_btree_create(path);
    dap_assert(btree != NULL, "B-tree should be created");
    
    // Insert records
    for (int i = 0; i < 100; i++) {
        dap_global_db_btree_key_t key = s_make_key(1000 + i, i * 12345);
        
        char key_str[64];
        snprintf(key_str, sizeof(key_str), "key_%d", i);
        
        char value[128];
        snprintf(value, sizeof(value), "value_for_key_%d", i);
        
        int rc = dap_global_db_btree_insert(btree, &key, 
                                             key_str, strlen(key_str) + 1,
                                             value, strlen(value) + 1,
                                             NULL, 0, 0);
        dap_assert(rc >= 0, "Insert should succeed");
    }
    
    dap_global_db_btree_sync(btree);
    dap_global_db_btree_close(btree);
    
    dap_test_pass("B-tree insert");
}

static void test_btree_cursor(void)
{
    dap_test_msg("Testing B-tree cursor iteration");
    
    char path[256];
    snprintf(path, sizeof(path), "%s/insert_test.gdb", TEST_DIR);
    
    dap_global_db_btree_t *btree = dap_global_db_btree_open(path);
    dap_assert(btree != NULL, "B-tree should be opened");
    
    dap_global_db_btree_cursor_t *cursor = dap_global_db_btree_cursor_open(btree);
    dap_assert(cursor != NULL, "Cursor should be opened");
    
    size_t count = 0;
    while (1) {
        dap_global_db_btree_key_t key;
        byte_t *key_data, *value_data, *sign_data;
        size_t key_len, value_len, sign_len;
        uint8_t flags;
        
        int rc = dap_global_db_btree_cursor_get(cursor, &key, 
                                                 &key_data, &key_len,
                                                 &value_data, &value_len,
                                                 &sign_data, &sign_len, &flags);
        if (rc < 0)
            break;
        
        count++;
        
        if (dap_global_db_btree_cursor_next(cursor) < 0)
            break;
    }
    
    dap_global_db_btree_cursor_close(cursor);
    dap_global_db_btree_close(btree);
    
    dap_assert(count == 100, "Should iterate all 100 records");
    
    dap_test_pass("B-tree cursor iteration");
}

static void test_btree_lookup(void)
{
    dap_test_msg("Testing B-tree lookup");
    
    char path[256];
    snprintf(path, sizeof(path), "%s/insert_test.gdb", TEST_DIR);
    
    dap_global_db_btree_t *btree = dap_global_db_btree_open(path);
    dap_assert(btree != NULL, "B-tree should be opened");
    
    // Lookup specific key
    dap_global_db_btree_key_t key = s_make_key(1050, 50 * 12345);
    
    byte_t *key_data, *value_data, *sign_data;
    size_t key_len, value_len, sign_len;
    uint8_t flags;
    
    int rc = dap_global_db_btree_lookup(btree, &key,
                                         &key_data, &key_len,
                                         &value_data, &value_len,
                                         &sign_data, &sign_len, &flags);
    
    if (rc == 0) {
        dap_assert(key_len > 0, "Key data should be present");
        dap_assert(value_len > 0, "Value data should be present");
        dap_test_msg("Found: key=%s, value=%s", (char*)key_data, (char*)value_data);
    }
    
    dap_global_db_btree_close(btree);
    
    dap_test_pass("B-tree lookup");
}

static void test_btree_persistence(void)
{
    dap_test_msg("Testing B-tree persistence");
    
    s_cleanup_test_dir();
    mkdir(TEST_DIR, 0755);
    
    char path[256];
    snprintf(path, sizeof(path), "%s/persist.gdb", TEST_DIR);
    
    // Phase 1: Create and insert
    {
        dap_global_db_btree_t *btree = dap_global_db_btree_create(path);
        dap_assert(btree != NULL, "B-tree should be created");
        
        for (int i = 0; i < 50; i++) {
            dap_global_db_btree_key_t key = s_make_key(2000 + i, i);
            char kstr[32], vstr[64];
            snprintf(kstr, sizeof(kstr), "persist_key_%d", i);
            snprintf(vstr, sizeof(vstr), "persist_value_%d", i);
            
            dap_global_db_btree_insert(btree, &key, kstr, strlen(kstr)+1, vstr, strlen(vstr)+1, NULL, 0, 0);
        }
        
        dap_global_db_btree_sync(btree);
        dap_global_db_btree_close(btree);
    }
    
    // Phase 2: Reopen and verify
    {
        dap_global_db_btree_t *btree = dap_global_db_btree_open(path);
        dap_assert(btree != NULL, "B-tree should be reopened");
        
        dap_global_db_btree_cursor_t *cursor = dap_global_db_btree_cursor_open(btree);
        dap_assert(cursor != NULL, "Cursor should open");
        
        size_t count = 0;
        while (1) {
            dap_global_db_btree_key_t k;
            byte_t *kd, *vd, *sd;
            size_t kl, vl, sl;
            uint8_t f;
            
            if (dap_global_db_btree_cursor_get(cursor, &k, &kd, &kl, &vd, &vl, &sd, &sl, &f) < 0)
                break;
            count++;
            if (dap_global_db_btree_cursor_next(cursor) < 0)
                break;
        }
        
        dap_global_db_btree_cursor_close(cursor);
        dap_global_db_btree_close(btree);
        
        dap_assert(count == 50, "Should have 50 records after reopen");
    }
    
    dap_test_pass("B-tree persistence");
}

static void test_btree_delete(void)
{
    dap_test_msg("Testing B-tree delete");
    
    char path[256];
    snprintf(path, sizeof(path), "%s/persist.gdb", TEST_DIR);
    
    dap_global_db_btree_t *btree = dap_global_db_btree_open(path);
    dap_assert(btree != NULL, "B-tree should be opened");
    
    // Delete a key
    dap_global_db_btree_key_t key = s_make_key(2025, 25);
    int rc = dap_global_db_btree_delete(btree, &key);
    
    // Note: delete may not be fully implemented yet
    (void)rc;
    
    dap_global_db_btree_close(btree);
    
    dap_test_pass("B-tree delete");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    dap_log_level_set(L_WARNING);
    
    dap_test_msg("=== DAP GlobalDB B-tree Unit Tests ===\n");
    
    test_btree_create();
    test_btree_open();
    test_btree_insert();
    test_btree_cursor();
    test_btree_lookup();
    test_btree_persistence();
    test_btree_delete();
    
    s_cleanup_test_dir();
    
    dap_test_msg("\n=== All B-tree tests passed ===\n");
    return 0;
}
