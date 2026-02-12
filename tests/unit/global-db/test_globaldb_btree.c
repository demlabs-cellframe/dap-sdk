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
    
    dap_pass_msg("B-tree creation");
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
    btree = dap_global_db_btree_open(path, false);
    dap_assert(btree != NULL, "B-tree should be opened");
    dap_global_db_btree_close(btree);
    
    dap_pass_msg("B-tree open");
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
    
    dap_pass_msg("B-tree insert");
}

static void test_btree_cursor(void)
{
    dap_test_msg("Testing B-tree cursor iteration");
    
    char path[256];
    snprintf(path, sizeof(path), "%s/insert_test.gdb", TEST_DIR);
    
    dap_global_db_btree_t *btree = dap_global_db_btree_open(path, false);
    dap_assert(btree != NULL, "B-tree should be opened");
    
    dap_global_db_btree_cursor_t *cursor = dap_global_db_btree_cursor_create(btree);
    dap_assert(cursor != NULL, "Cursor should be created");
    
    int rc = dap_global_db_btree_cursor_move(cursor, DAP_GLOBAL_DB_BTREE_FIRST, NULL);
    dap_assert(rc == 0, "Cursor should move to first");
    
    size_t count = 0;
    while (1) {
        dap_global_db_btree_key_t key;
        char *text_key;
        void *value_data, *sign_data;
        uint32_t value_len, sign_len;
        uint8_t flags;
        
        rc = dap_global_db_btree_cursor_get(cursor, &key,
                                             &text_key,
                                             &value_data, &value_len,
                                             &sign_data, &sign_len, &flags);
        if (rc != 0)
            break;
        
        count++;
        
        if (dap_global_db_btree_cursor_move(cursor, DAP_GLOBAL_DB_BTREE_NEXT, NULL) != 0)
            break;
    }
    
    dap_global_db_btree_cursor_close(cursor);
    dap_global_db_btree_close(btree);
    
    dap_assert(count == 100, "Should iterate all 100 records");
    
    dap_pass_msg("B-tree cursor iteration");
}

static void test_btree_lookup(void)
{
    dap_test_msg("Testing B-tree lookup");
    
    char path[256];
    snprintf(path, sizeof(path), "%s/insert_test.gdb", TEST_DIR);
    
    dap_global_db_btree_t *btree = dap_global_db_btree_open(path, false);
    dap_assert(btree != NULL, "B-tree should be opened");
    
    // Lookup specific key
    dap_global_db_btree_key_t key = s_make_key(1050, 50 * 12345);
    
    char *text_key;
    void *value_data, *sign_data;
    uint32_t value_len, sign_len;
    uint8_t flags;
    
    int rc = dap_global_db_btree_get(btree, &key,
                                      &text_key,
                                      &value_data, &value_len,
                                      &sign_data, &sign_len, &flags);
    
    if (rc == 0) {
        dap_assert(value_len > 0, "Value data should be present");
        if (text_key)
            dap_test_msg("Found: key=%s, value=%s", text_key, (char*)value_data);
    }
    
    dap_global_db_btree_close(btree);
    
    dap_pass_msg("B-tree lookup");
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
        dap_global_db_btree_t *btree = dap_global_db_btree_open(path, false);
        dap_assert(btree != NULL, "B-tree should be reopened");
        
        dap_global_db_btree_cursor_t *cursor = dap_global_db_btree_cursor_create(btree);
        dap_assert(cursor != NULL, "Cursor should be created");
        dap_global_db_btree_cursor_move(cursor, DAP_GLOBAL_DB_BTREE_FIRST, NULL);
        
        size_t count = 0;
        while (1) {
            dap_global_db_btree_key_t k;
            char *tk;
            void *vd, *sd;
            uint32_t vl, sl;
            uint8_t f;
            
            if (dap_global_db_btree_cursor_get(cursor, &k, &tk, &vd, &vl, &sd, &sl, &f) != 0)
                break;
            count++;
            if (dap_global_db_btree_cursor_move(cursor, DAP_GLOBAL_DB_BTREE_NEXT, NULL) != 0)
                break;
        }
        
        dap_global_db_btree_cursor_close(cursor);
        dap_global_db_btree_close(btree);
        
        dap_assert(count == 50, "Should have 50 records after reopen");
    }
    
    dap_pass_msg("B-tree persistence");
}

static void test_btree_delete(void)
{
    dap_test_msg("Testing B-tree delete with rebalancing");
    
    char path[256];
    snprintf(path, sizeof(path), "%s/delete_test.gdb", TEST_DIR);
    
    // Create fresh tree and populate it
    dap_global_db_btree_t *btree = dap_global_db_btree_create(path);
    dap_assert(btree != NULL, "B-tree should be created for delete test");

    const int NUM_ENTRIES = 500;
    char val_buf[64];

    for (int i = 0; i < NUM_ENTRIES; i++) {
        dap_global_db_btree_key_t key = s_make_key(1000, (uint64_t)i);
        snprintf(val_buf, sizeof(val_buf), "val_%d", i);
        int rc = dap_global_db_btree_insert(btree, &key, val_buf, strlen(val_buf) + 1,
                                             val_buf, strlen(val_buf) + 1, NULL, 0, 0);
        dap_assert(rc == 0, "Insert for delete test should succeed");
    }
    dap_global_db_btree_sync(btree);
    dap_assert(dap_global_db_btree_count(btree) == (uint64_t)NUM_ENTRIES, "Count should match NUM_ENTRIES");

    // --- Test 1: Basic delete ---
    {
        dap_global_db_btree_key_t key = s_make_key(1000, 42);
        int rc = dap_global_db_btree_delete(btree, &key);
        dap_assert(rc == 0, "Delete existing key should succeed");
        dap_assert(!dap_global_db_btree_exists(btree, &key), "Deleted key should not exist");
        dap_assert(dap_global_db_btree_count(btree) == (uint64_t)(NUM_ENTRIES - 1),
                   "Count should be N-1 after one delete");
    }

    // --- Test 2: Delete non-existent key ---
    {
        dap_global_db_btree_key_t key = s_make_key(9999, 9999);
        int rc = dap_global_db_btree_delete(btree, &key);
        dap_assert(rc == 1, "Delete non-existent key should return 1 (not found)");
    }

    // --- Test 3: Mass delete to trigger leaf merges ---
    {
        int deleted = 1;  // We already deleted one above
        for (int i = 0; i < NUM_ENTRIES; i += 2) {
            dap_global_db_btree_key_t key = s_make_key(1000, (uint64_t)i);
            int rc = dap_global_db_btree_delete(btree, &key);
            if (rc == 0)
                deleted++;
        }
        uint64_t expected = NUM_ENTRIES - deleted;
        dap_assert(dap_global_db_btree_count(btree) == expected,
                   "Count should match after mass delete");

        // Verify remaining entries are still accessible
        for (int i = 1; i < NUM_ENTRIES; i += 2) {
            dap_global_db_btree_key_t key = s_make_key(1000, (uint64_t)i);
            dap_assert(dap_global_db_btree_exists(btree, &key),
                       "Odd keys should still exist after mass delete");
        }
    }

    // --- Test 4: Cursor forward+backward iteration after deletes ---
    {
        dap_global_db_btree_cursor_t *cur = dap_global_db_btree_cursor_create(btree);
        dap_assert(cur != NULL, "Cursor should be created");

        // Forward scan
        int count_fwd = 0;
        int rc = dap_global_db_btree_cursor_move(cur, DAP_GLOBAL_DB_BTREE_FIRST, NULL);
        dap_assert(rc == 0, "Cursor FIRST should succeed");
        while (dap_global_db_btree_cursor_valid(cur)) {
            count_fwd++;
            rc = dap_global_db_btree_cursor_move(cur, DAP_GLOBAL_DB_BTREE_NEXT, NULL);
        }
        dap_assert(count_fwd == (int)dap_global_db_btree_count(btree),
                   "Forward cursor count should match tree count");

        // Backward scan
        int count_bwd = 0;
        rc = dap_global_db_btree_cursor_move(cur, DAP_GLOBAL_DB_BTREE_LAST, NULL);
        dap_assert(rc == 0, "Cursor LAST should succeed");
        while (dap_global_db_btree_cursor_valid(cur)) {
            count_bwd++;
            rc = dap_global_db_btree_cursor_move(cur, DAP_GLOBAL_DB_BTREE_PREV, NULL);
        }
        dap_assert(count_bwd == count_fwd,
                   "Backward cursor count should match forward cursor count");

        dap_global_db_btree_cursor_close(cur);
    }

    // --- Test 5: Delete all remaining entries ---
    {
        for (int i = 1; i < NUM_ENTRIES; i += 2) {
            dap_global_db_btree_key_t key = s_make_key(1000, (uint64_t)i);
            dap_global_db_btree_delete(btree, &key);
        }
        dap_assert(dap_global_db_btree_count(btree) == 0,
                   "Count should be 0 after deleting all entries");
    }

    dap_global_db_btree_close(btree);
    dap_pass_msg("B-tree delete with rebalancing");
}

/**
 * @brief Regression test: SIGSEGV in s_leaf_insert_entry after leaf split
 *
 * Bug: s_split_child() truncates entries_count and restores free_space for the
 * child page, but does NOT compact the physical entry data. When the first half
 * (by key order) has the lowest physical offsets (entries packed from the end in
 * insertion order), subsequent inserts compute l_new_offset = l_min_offset -
 * l_entry_size, which underflows uint16_t → huge offset → write outside the
 * page buffer → SIGSEGV.
 *
 * Trigger sequence with entry_size=288 (value=256 + header=32):
 * - Page holds ~12 entries (PAGE_DATA_SIZE=4064)
 * - Insert 12 entries with DECREASING keys → offsets: 3776, 3488, ..., 608
 * - 13th insert triggers root split at mid=6:
 *     child keeps entries 0-5 (smallest keys = lowest offsets {608..2048})
 *     min_offset=608, free_space≈2324
 * - Insert 14th: offset=608-288=320, OK
 * - Insert 15th: offset=320-288=32, OK
 * - Insert 16th: free_space check passes (2324-3*290=1454 >> 308),
 *   but l_new_offset = 32 - 288 = uint16_t(65280) → SIGSEGV
 */
static void test_btree_split_compaction_sigsegv(void)
{
    dap_test_msg("Regression: leaf split + decreasing keys → SIGSEGV");

    s_cleanup_test_dir();
    mkdir(TEST_DIR, 0755);

    char path[256];
    snprintf(path, sizeof(path), "%s/regression_split.gdb", TEST_DIR);

    dap_global_db_btree_t *btree = dap_global_db_btree_create(path);
    dap_assert(btree != NULL, "B-tree should be created");

    // 256-byte value → entry_size = sizeof(leaf_entry_t) + 256 ≈ 288
    // With entry_size=288, page holds ~12 entries before split
    const size_t VALUE_SIZE = 256;
    uint8_t value[256];
    memset(value, 0xAB, VALUE_SIZE);

    // Insert keys in STRICTLY DECREASING order to trigger worst-case split:
    // After split, kept entries (smallest keys) have the lowest offsets.
    // With entry_size=288 a page holds 13 entries. Split at mid=6:
    //   child keeps entries 0-5 (lowest offsets: 320,608,...,1760)
    //   min_offset=320 after split
    // 14th insert: offset=320-288=32 (OK)
    // 15th insert: offset=32-288 → uint16_t underflow → heap buffer overflow
    //
    // We insert 500 records to also expose cascaded corruption (the write at
    // data+65280 may silently corrupt heap instead of faulting immediately).
    const int NUM_RECORDS = 500;
    int insert_ok = 0;

    for (int i = NUM_RECORDS; i > 0; i--) {
        dap_global_db_btree_key_t key = s_make_key((uint64_t)i, (uint64_t)(i * 7919));

        int rc = dap_global_db_btree_insert(btree, &key, NULL, 0,
                                             value, VALUE_SIZE, NULL, 0, 0);
        dap_assert(rc == 0, "Insert should succeed without SIGSEGV");
        insert_ok++;
    }

    // Verify all records via individual lookups (cursor has a separate bug)
    dap_global_db_btree_sync(btree);

    int verified = 0;
    for (int i = NUM_RECORDS; i > 0; i--) {
        dap_global_db_btree_key_t key = s_make_key((uint64_t)i, (uint64_t)(i * 7919));

        char *text_key = NULL;
        void *out_value = NULL, *out_sign = NULL;
        uint32_t out_value_len = 0, out_sign_len = 0;
        uint8_t out_flags = 0;

        int rc = dap_global_db_btree_get(btree, &key, &text_key,
                                          &out_value, &out_value_len,
                                          &out_sign, &out_sign_len, &out_flags);
        dap_assert(rc == 0, "Record lookup should succeed after split");
        dap_assert(out_value_len == VALUE_SIZE,
                   "Value length should match (data corruption check)");

        // Verify value integrity — if heap was corrupted, values will be wrong
        uint8_t *vp = (uint8_t *)out_value;
        int l_ok = 1;
        for (size_t j = 0; j < VALUE_SIZE; j++) {
            if (vp[j] != 0xAB) {
                l_ok = 0;
                break;
            }
        }
        dap_assert(l_ok, "Value data should be intact (no heap corruption)");

        DAP_DEL_Z(out_value);
        DAP_DEL_Z(out_sign);
        DAP_DEL_Z(text_key);
        verified++;
    }

    dap_test_msg("Verified %d of %d records via lookup", verified, NUM_RECORDS);
    dap_assert(verified == NUM_RECORDS,
               "All records should be verifiable (no data corruption)");

    dap_global_db_btree_close(btree);
    dap_pass_msg("Regression: leaf split + decreasing keys");
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
    
    test_btree_split_compaction_sigsegv();
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
