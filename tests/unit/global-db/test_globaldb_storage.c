/*
 * DAP GlobalDB Storage Layer Unit Tests
 *
 * Tests:
 * - Storage initialization
 * - Group creation and lookup
 * - Record CRUD operations
 * - Hash-based operations
 * - Multi-record operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_hash.h"
#include "dap_global_db.h"
#include "dap_global_db_storage.h"

#define TEST_DIR "/tmp/test_globaldb_storage"

static void s_cleanup_test_dir(void)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

// ============================================================================
// Tests
// ============================================================================

static void test_storage_init_deinit(void)
{
    dap_test_msg("Testing storage init/deinit");
    
    s_cleanup_test_dir();
    
    int rc = dap_global_db_storage_init(TEST_DIR);
    dap_assert(rc == 0, "Storage should initialize");
    
    // Verify directory created
    struct stat st;
    dap_assert(stat(TEST_DIR, &st) == 0, "Storage directory should exist");
    dap_assert(S_ISDIR(st.st_mode), "Should be a directory");
    
    dap_global_db_storage_deinit();
    
    dap_test_pass("Storage init/deinit");
}

static void test_storage_group_create(void)
{
    dap_test_msg("Testing group creation");
    
    s_cleanup_test_dir();
    dap_global_db_storage_init(TEST_DIR);
    
    // Create group
    dap_global_db_btree_t *btree = dap_global_db_storage_group_get_or_create("test_group");
    dap_assert(btree != NULL, "Group B-tree should be created");
    
    // Get same group again
    dap_global_db_btree_t *btree2 = dap_global_db_storage_group_get("test_group");
    dap_assert(btree2 == btree, "Should return same B-tree handle");
    
    // Get non-existent group
    dap_global_db_btree_t *btree3 = dap_global_db_storage_group_get("nonexistent");
    dap_assert(btree3 == NULL, "Non-existent group should return NULL");
    
    dap_global_db_storage_deinit();
    
    dap_test_pass("Group creation");
}

static void test_storage_write_read(void)
{
    dap_test_msg("Testing storage write/read");
    
    s_cleanup_test_dir();
    dap_global_db_storage_init(TEST_DIR);
    
    // Create test record
    dap_global_db_store_obj_t obj = {0};
    obj.group = dap_strdup("test_group");
    obj.key = dap_strdup("test_key");
    obj.value = (byte_t *)dap_strdup("test_value");
    obj.value_len = strlen((char *)obj.value) + 1;
    obj.timestamp = dap_nanotime_now();
    obj.crc = dap_hash_fast64(obj.value, obj.value_len);
    
    // Write
    int rc = dap_global_db_storage_write(&obj);
    dap_assert(rc == 0, "Write should succeed");
    
    // Read back
    dap_global_db_store_obj_t *read_obj = dap_global_db_storage_read_by_key("test_group", "test_key", false);
    dap_assert(read_obj != NULL, "Read should succeed");
    dap_assert(strcmp(read_obj->key, "test_key") == 0, "Key should match");
    dap_assert(read_obj->value_len == obj.value_len, "Value length should match");
    dap_assert(memcmp(read_obj->value, obj.value, obj.value_len) == 0, "Value should match");
    
    dap_global_db_store_obj_free(read_obj, 1);
    
    // Cleanup obj
    DAP_DEL_MULTY(obj.group, obj.key, obj.value);
    
    dap_global_db_storage_deinit();
    
    dap_test_pass("Storage write/read");
}

static void test_storage_count(void)
{
    dap_test_msg("Testing storage count");
    
    s_cleanup_test_dir();
    dap_global_db_storage_init(TEST_DIR);
    
    // Insert multiple records
    for (int i = 0; i < 50; i++) {
        dap_global_db_store_obj_t obj = {0};
        obj.group = dap_strdup("count_group");
        obj.key = dap_strdup_printf("key_%d", i);
        obj.value = (byte_t *)dap_strdup_printf("value_%d", i);
        obj.value_len = strlen((char *)obj.value) + 1;
        obj.timestamp = dap_nanotime_now() + i;
        obj.crc = i;
        
        dap_global_db_storage_write(&obj);
        
        DAP_DEL_MULTY(obj.group, obj.key, obj.value);
    }
    
    uint64_t count = dap_global_db_storage_group_count("count_group", false);
    dap_assert(count == 50, "Should have 50 records");
    
    dap_global_db_storage_deinit();
    
    dap_test_pass("Storage count");
}

static void test_storage_read_all(void)
{
    dap_test_msg("Testing storage read all");
    
    s_cleanup_test_dir();
    dap_global_db_storage_init(TEST_DIR);
    
    // Insert records
    for (int i = 0; i < 25; i++) {
        dap_global_db_store_obj_t obj = {0};
        obj.group = dap_strdup("readall_group");
        obj.key = dap_strdup_printf("item_%02d", i);
        obj.value = (byte_t *)dap_strdup_printf("data_%d", i);
        obj.value_len = strlen((char *)obj.value) + 1;
        obj.timestamp = dap_nanotime_now() + i;
        obj.crc = i * 100;
        
        dap_global_db_storage_write(&obj);
        
        DAP_DEL_MULTY(obj.group, obj.key, obj.value);
    }
    
    // Read all
    size_t count = 0;
    dap_global_db_store_obj_t *objs = dap_global_db_storage_read_all("readall_group", &count, false);
    
    dap_assert(objs != NULL, "Read all should return data");
    dap_assert(count == 25, "Should return 25 records");
    
    dap_global_db_store_obj_free(objs, count);
    
    dap_global_db_storage_deinit();
    
    dap_test_pass("Storage read all");
}

static void test_storage_exists(void)
{
    dap_test_msg("Testing storage exists check");
    
    s_cleanup_test_dir();
    dap_global_db_storage_init(TEST_DIR);
    
    // Insert a record
    dap_global_db_store_obj_t obj = {0};
    obj.group = dap_strdup("exists_group");
    obj.key = dap_strdup("existing_key");
    obj.value = (byte_t *)dap_strdup("data");
    obj.value_len = 5;
    obj.timestamp = dap_nanotime_now();
    obj.crc = 12345;
    
    dap_global_db_storage_write(&obj);
    
    // Check exists
    bool exists = dap_global_db_storage_exists_key("exists_group", "existing_key");
    dap_assert(exists == true, "Existing key should be found");
    
    bool not_exists = dap_global_db_storage_exists_key("exists_group", "nonexistent_key");
    dap_assert(not_exists == false, "Non-existent key should not be found");
    
    DAP_DEL_MULTY(obj.group, obj.key, obj.value);
    
    dap_global_db_storage_deinit();
    
    dap_test_pass("Storage exists check");
}

static void test_storage_groups_by_mask(void)
{
    dap_test_msg("Testing get groups by mask");
    
    s_cleanup_test_dir();
    dap_global_db_storage_init(TEST_DIR);
    
    // Create multiple groups
    const char *groups[] = {
        "local.settings",
        "local.cache",
        "network.peers",
        "network.nodes",
        "global.state"
    };
    
    for (size_t i = 0; i < sizeof(groups)/sizeof(groups[0]); i++) {
        dap_global_db_storage_group_get_or_create(groups[i]);
    }
    
    // Get all groups
    dap_list_t *all = dap_global_db_storage_get_groups_by_mask("*");
    size_t all_count = dap_list_length(all);
    dap_assert(all_count == 5, "Should have 5 groups");
    dap_list_free_full(all, NULL);
    
    // Get local.* groups
    dap_list_t *local = dap_global_db_storage_get_groups_by_mask("local.*");
    size_t local_count = dap_list_length(local);
    dap_assert(local_count == 2, "Should have 2 local.* groups");
    dap_list_free_full(local, NULL);
    
    dap_global_db_storage_deinit();
    
    dap_test_pass("Get groups by mask");
}

static void test_storage_persistence(void)
{
    dap_test_msg("Testing storage persistence");
    
    s_cleanup_test_dir();
    
    // Phase 1: Create and populate
    {
        dap_global_db_storage_init(TEST_DIR);
        
        for (int i = 0; i < 30; i++) {
            dap_global_db_store_obj_t obj = {0};
            obj.group = dap_strdup("persist_group");
            obj.key = dap_strdup_printf("persist_key_%d", i);
            obj.value = (byte_t *)dap_strdup_printf("persist_value_%d", i);
            obj.value_len = strlen((char *)obj.value) + 1;
            obj.timestamp = 1000000 + i;
            obj.crc = i * 111;
            
            dap_global_db_storage_write(&obj);
            
            DAP_DEL_MULTY(obj.group, obj.key, obj.value);
        }
        
        dap_global_db_storage_flush();
        dap_global_db_storage_deinit();
    }
    
    // Phase 2: Reopen and verify
    {
        dap_global_db_storage_init(TEST_DIR);
        
        uint64_t count = dap_global_db_storage_group_count("persist_group", false);
        dap_assert(count == 30, "Should have 30 records after reopen");
        
        // Verify specific record
        dap_global_db_store_obj_t *obj = dap_global_db_storage_read_by_key("persist_group", "persist_key_15", false);
        dap_assert(obj != NULL, "Should find persist_key_15");
        dap_assert(strcmp((char *)obj->value, "persist_value_15") == 0, "Value should match");
        
        dap_global_db_store_obj_free(obj, 1);
        
        dap_global_db_storage_deinit();
    }
    
    dap_test_pass("Storage persistence");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    dap_log_level_set(L_WARNING);
    
    dap_test_msg("=== DAP GlobalDB Storage Unit Tests ===\n");
    
    test_storage_init_deinit();
    test_storage_group_create();
    test_storage_write_read();
    test_storage_count();
    test_storage_read_all();
    test_storage_exists();
    test_storage_groups_by_mask();
    test_storage_persistence();
    
    s_cleanup_test_dir();
    
    dap_test_msg("\n=== All Storage tests passed ===\n");
    return 0;
}
