/*
 * DAP GlobalDB Storage Layer Unit Tests
 *
 * Tests the public API for storage group operations (after refactoring
 * where dap_global_db_storage.c was merged into dap_global_db.c):
 * - groups_init/deinit/flush
 * - group_get_or_create
 * - get_groups_by_mask
 * - group_count (with btree_insert for data)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_test.h"
#include "dap_global_db.h"
#include "dap_global_db_btree.h"

#define TEST_DIR "/tmp/test_globaldb_storage"

static void s_cleanup_test_dir(void)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR);
    system(cmd);
}

static dap_global_db_key_t s_make_key(uint64_t ts, uint64_t crc)
{
    dap_global_db_key_t key = {
        .bets = htobe64(ts),
        .becrc = htobe64(crc)
    };
    return key;
}

// ============================================================================
// Tests
// ============================================================================

static void test_storage_init_deinit(void)
{
    dap_test_msg("Testing groups init/deinit");

    s_cleanup_test_dir();

    int rc = dap_global_db_groups_init(TEST_DIR);
    dap_assert(rc == 0, "Groups should initialize");

    // Verify directory created
    struct stat st;
    dap_assert(stat(TEST_DIR, &st) == 0, "Storage directory should exist");
    dap_assert(S_ISDIR(st.st_mode), "Should be a directory");

    dap_global_db_groups_deinit();

    dap_pass_msg("Groups init/deinit");
}

static void test_storage_group_create(void)
{
    dap_test_msg("Testing group creation");

    s_cleanup_test_dir();
    dap_global_db_groups_init(TEST_DIR);

    // Create group
    dap_global_db_t *btree = dap_global_db_group_get_or_create("test_group");
    dap_assert(btree != NULL, "Group B-tree should be created");

    // Get same group again (get_or_create returns same handle)
    dap_global_db_t *btree2 = dap_global_db_group_get_or_create("test_group");
    dap_assert(btree2 == btree, "Should return same B-tree handle");

    dap_global_db_groups_deinit();

    dap_pass_msg("Group creation");
}

static void test_storage_count(void)
{
    dap_test_msg("Testing group count");

    s_cleanup_test_dir();
    dap_global_db_groups_init(TEST_DIR);

    dap_global_db_t *btree = dap_global_db_group_get_or_create("count_group");
    dap_assert(btree != NULL, "Group should be created");

    // Insert multiple records via btree API
    for (int i = 0; i < 50; i++) {
        char *key_str = dap_strdup_printf("key_%d", i);
        char *val_str = dap_strdup_printf("value_%d", i);
        dap_global_db_key_t key = s_make_key(dap_nanotime_now() + i, (uint64_t)i);

        int rc = dap_global_db_insert(btree, &key,
                                            key_str, strlen(key_str) + 1,
                                            val_str, strlen(val_str) + 1,
                                            NULL, 0, 0);
        dap_assert(rc == 0, "Insert should succeed");

        DAP_DEL_MULTY(key_str, val_str);
    }

    uint64_t count = dap_global_db_count(btree);
    dap_assert(count == 50, "Should have 50 records");

    dap_global_db_groups_deinit();

    dap_pass_msg("Group count");
}

static void test_storage_groups_by_mask(void)
{
    dap_test_msg("Testing get groups by mask");

    s_cleanup_test_dir();
    dap_global_db_groups_init(TEST_DIR);

    // Create multiple groups
    const char *groups[] = {
        "local.settings",
        "local.cache",
        "network.peers",
        "network.nodes",
        "global.state"
    };

    for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
        dap_global_db_group_get_or_create(groups[i]);
    }

    // Get all groups
    dap_list_t *all = dap_global_db_get_groups_by_mask("*");
    size_t all_count = dap_list_length(all);
    dap_assert(all_count == 5, "Should have 5 groups");
    dap_list_free_full(all, NULL);

    // Get local.* groups
    dap_list_t *local = dap_global_db_get_groups_by_mask("local.*");
    size_t local_count = dap_list_length(local);
    dap_assert(local_count == 2, "Should have 2 local.* groups");
    dap_list_free_full(local, NULL);

    dap_global_db_groups_deinit();

    dap_pass_msg("Get groups by mask");
}

static void test_storage_persistence(void)
{
    dap_test_msg("Testing storage persistence");

    s_cleanup_test_dir();

    // Phase 1: Create and populate
    {
        dap_global_db_groups_init(TEST_DIR);

        dap_global_db_t *btree = dap_global_db_group_get_or_create("persist_group");
        dap_assert(btree != NULL, "Group should be created");

        for (int i = 0; i < 30; i++) {
            char *key_str = dap_strdup_printf("persist_key_%d", i);
            char *val_str = dap_strdup_printf("persist_value_%d", i);
            dap_global_db_key_t key = s_make_key(1000000 + i, (uint64_t)(i * 111));

            int rc = dap_global_db_insert(btree, &key,
                                                key_str, strlen(key_str) + 1,
                                                val_str, strlen(val_str) + 1,
                                                NULL, 0, 0);
            dap_assert(rc == 0, "Insert should succeed");

            DAP_DEL_MULTY(key_str, val_str);
        }

        dap_global_db_groups_flush();
        dap_global_db_groups_deinit();
    }

    // Phase 2: Reopen and verify
    {
        dap_global_db_groups_init(TEST_DIR);

        uint64_t count = dap_global_db_group_count("persist_group", false);
        dap_assert(count == 30, "Should have 30 records after reopen");

        // Verify specific record via btree API
        dap_global_db_t *btree = dap_global_db_group_get_or_create("persist_group");
        dap_global_db_key_t key15 = s_make_key(1000015, 15 * 111);

        char *out_key = NULL;
        void *out_value = NULL;
        uint32_t out_value_len = 0;
        void *out_sign = NULL;
        uint32_t out_sign_len = 0;
        uint8_t out_flags = 0;

        int rc = dap_global_db_fetch(btree, &key15,
                                         &out_key, &out_value, &out_value_len,
                                         &out_sign, &out_sign_len, &out_flags);
        dap_assert(rc == 0, "Should find persist_key_15");
        dap_assert(out_value != NULL && out_value_len > 0, "Value should be present");
        dap_assert(strcmp((char *)out_value, "persist_value_15") == 0, "Value should match");

        DAP_DEL_MULTY(out_key, out_value, out_sign);

        dap_global_db_groups_deinit();
    }

    dap_pass_msg("Storage persistence");
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
    test_storage_count();
    test_storage_groups_by_mask();
    test_storage_persistence();

    s_cleanup_test_dir();

    dap_test_msg("\n=== All Storage tests passed ===\n");
    return 0;
}
