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
#include "dap_file_utils.h"
#include "dap_test.h"
#include "dap_global_db.h"
#include "dap_global_db_btree.h"

#define TEST_DIR "/tmp/test_globaldb_btree"

// Helpers
static void s_cleanup_test_dir(void)
{
    dap_rm_rf(TEST_DIR);
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

static void test_btree_create(void)
{
    dap_test_msg("Testing B-tree creation");
    
    s_cleanup_test_dir();
#ifdef DAP_OS_WINDOWS
    mkdir(TEST_DIR);
#else
    mkdir(TEST_DIR, 0755);
#endif
    
    char path[256];
    snprintf(path, sizeof(path), "%s/test.gdb", TEST_DIR);
    
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "B-tree should be created");
    
    dap_global_db_close(btree);
    
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
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "B-tree should be created");
    dap_global_db_close(btree);
    
    // Open existing
    btree = dap_global_db_open(path, false);
    dap_assert(btree != NULL, "B-tree should be opened");
    dap_global_db_close(btree);
    
    dap_pass_msg("B-tree open");
}

static void test_btree_insert(void)
{
    dap_test_msg("Testing B-tree insert");
    
    char path[256];
    snprintf(path, sizeof(path), "%s/insert_test.gdb", TEST_DIR);
    
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "B-tree should be created");
    
    // Insert records
    for (int i = 0; i < 100; i++) {
        dap_global_db_key_t key = s_make_key(1000 + i, i * 12345);
        
        char key_str[64];
        snprintf(key_str, sizeof(key_str), "key_%d", i);
        
        char value[128];
        snprintf(value, sizeof(value), "value_for_key_%d", i);
        
        int rc = dap_global_db_insert(btree, &key, 
                                             key_str, strlen(key_str) + 1,
                                             value, strlen(value) + 1,
                                             NULL, 0, 0);
        dap_assert_PIF(rc >= 0, "Insert should succeed");
    }
    
    dap_global_db_sync(btree);
    dap_global_db_close(btree);
    
    dap_pass_msg("B-tree insert");
}

static void test_btree_cursor(void)
{
    dap_test_msg("Testing B-tree cursor iteration");
    
    char path[256];
    snprintf(path, sizeof(path), "%s/insert_test.gdb", TEST_DIR);
    
    dap_global_db_t *btree = dap_global_db_open(path, false);
    dap_assert(btree != NULL, "B-tree should be opened");
    
    dap_global_db_cursor_t *cursor = dap_global_db_cursor_create(btree);
    dap_assert(cursor != NULL, "Cursor should be created");
    
    int rc = dap_global_db_cursor_move(cursor, DAP_GLOBAL_DB_FIRST, NULL);
    dap_assert(rc == 0, "Cursor should move to first");
    
    size_t count = 0;
    while (1) {
        dap_global_db_key_t key;
        char *text_key;
        void *value_data, *sign_data;
        uint32_t value_len, sign_len;
        uint8_t flags;
        
        rc = dap_global_db_cursor_get(cursor, &key,
                                             &text_key,
                                             &value_data, &value_len,
                                             &sign_data, &sign_len, &flags);
        if (rc != 0)
            break;
        
        count++;
        DAP_DELETE(text_key);
        DAP_DELETE(value_data);
        DAP_DELETE(sign_data);
        
        if (dap_global_db_cursor_move(cursor, DAP_GLOBAL_DB_NEXT, NULL) != 0)
            break;
    }
    
    dap_global_db_cursor_close(cursor);
    dap_global_db_close(btree);
    
    dap_assert(count == 100, "Should iterate all 100 records");
    
    dap_pass_msg("B-tree cursor iteration");
}

static void test_btree_lookup(void)
{
    dap_test_msg("Testing B-tree lookup");
    
    char path[256];
    snprintf(path, sizeof(path), "%s/insert_test.gdb", TEST_DIR);
    
    dap_global_db_t *btree = dap_global_db_open(path, false);
    dap_assert(btree != NULL, "B-tree should be opened");
    
    // Lookup specific key
    dap_global_db_key_t key = s_make_key(1050, 50 * 12345);
    
    char *text_key;
    void *value_data, *sign_data;
    uint32_t value_len, sign_len;
    uint8_t flags;
    
    int rc = dap_global_db_fetch(btree, &key,
                                      &text_key,
                                      &value_data, &value_len,
                                      &sign_data, &sign_len, &flags);
    
    if (rc == 0) {
        dap_assert(value_len > 0, "Value data should be present");
        if (text_key)
            dap_test_msg("Found: key=%s, value=%s", text_key, (char*)value_data);
        DAP_DELETE(text_key);
        DAP_DELETE(value_data);
        DAP_DELETE(sign_data);
    }
    
    dap_global_db_close(btree);
    
    dap_pass_msg("B-tree lookup");
}

static void test_btree_persistence(void)
{
    dap_test_msg("Testing B-tree persistence");
    
    s_cleanup_test_dir();
#ifdef DAP_OS_WINDOWS
    mkdir(TEST_DIR);
#else
    mkdir(TEST_DIR, 0755);
#endif
    
    char path[256];
    snprintf(path, sizeof(path), "%s/persist.gdb", TEST_DIR);
    
    // Phase 1: Create and insert
    {
        dap_global_db_t *btree = dap_global_db_create(path);
        dap_assert(btree != NULL, "B-tree should be created");
        
        for (int i = 0; i < 50; i++) {
            dap_global_db_key_t key = s_make_key(2000 + i, i);
            char kstr[32], vstr[64];
            snprintf(kstr, sizeof(kstr), "persist_key_%d", i);
            snprintf(vstr, sizeof(vstr), "persist_value_%d", i);
            
            dap_global_db_insert(btree, &key, kstr, strlen(kstr)+1, vstr, strlen(vstr)+1, NULL, 0, 0);
        }
        
        dap_global_db_sync(btree);
        dap_global_db_close(btree);
    }
    
    // Phase 2: Reopen and verify
    {
        dap_global_db_t *btree = dap_global_db_open(path, false);
        dap_assert(btree != NULL, "B-tree should be reopened");
        
        dap_global_db_cursor_t *cursor = dap_global_db_cursor_create(btree);
        dap_assert(cursor != NULL, "Cursor should be created");
        dap_global_db_cursor_move(cursor, DAP_GLOBAL_DB_FIRST, NULL);
        
        size_t count = 0;
        while (1) {
            dap_global_db_key_t k;
            char *tk;
            void *vd, *sd;
            uint32_t vl, sl;
            uint8_t f;
            
            if (dap_global_db_cursor_get(cursor, &k, &tk, &vd, &vl, &sd, &sl, &f) != 0)
                break;
            count++;
            DAP_DELETE(tk);
            DAP_DELETE(vd);
            DAP_DELETE(sd);
            if (dap_global_db_cursor_move(cursor, DAP_GLOBAL_DB_NEXT, NULL) != 0)
                break;
        }
        
        dap_global_db_cursor_close(cursor);
        dap_global_db_close(btree);
        
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
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "B-tree should be created for delete test");

    const int NUM_ENTRIES = 500;
    char val_buf[64];

    for (int i = 0; i < NUM_ENTRIES; i++) {
        dap_global_db_key_t key = s_make_key(1000, (uint64_t)i);
        snprintf(val_buf, sizeof(val_buf), "val_%d", i);
        int rc = dap_global_db_insert(btree, &key, val_buf, strlen(val_buf) + 1,
                                             val_buf, strlen(val_buf) + 1, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Insert for delete test should succeed");
    }
    dap_global_db_sync(btree);
    dap_assert(dap_global_db_count(btree) == (uint64_t)NUM_ENTRIES, "Count should match NUM_ENTRIES");

    // --- Test 1: Basic delete ---
    {
        dap_global_db_key_t key = s_make_key(1000, 42);
        int rc = dap_global_db_delete(btree, &key);
        dap_assert(rc == 0, "Delete existing key should succeed");
        dap_assert(!dap_global_db_exists(btree, &key), "Deleted key should not exist");
        dap_assert(dap_global_db_count(btree) == (uint64_t)(NUM_ENTRIES - 1),
                   "Count should be N-1 after one delete");
    }

    // --- Test 2: Delete non-existent key ---
    {
        dap_global_db_key_t key = s_make_key(9999, 9999);
        int rc = dap_global_db_delete(btree, &key);
        dap_assert(rc == 1, "Delete non-existent key should return 1 (not found)");
    }

    // --- Test 3: Mass delete to trigger leaf merges ---
    {
        int deleted = 1;  // We already deleted one above
        for (int i = 0; i < NUM_ENTRIES; i += 2) {
            dap_global_db_key_t key = s_make_key(1000, (uint64_t)i);
            int rc = dap_global_db_delete(btree, &key);
            if (rc == 0)
                deleted++;
        }
        uint64_t expected = NUM_ENTRIES - deleted;
        dap_assert(dap_global_db_count(btree) == expected,
                   "Count should match after mass delete");

        // Verify remaining entries are still accessible
        for (int i = 1; i < NUM_ENTRIES; i += 2) {
            dap_global_db_key_t key = s_make_key(1000, (uint64_t)i);
            dap_assert_PIF(dap_global_db_exists(btree, &key),
                           "Odd keys should still exist after mass delete");
        }
    }

    // --- Test 4: Cursor forward+backward iteration after deletes ---
    {
        dap_global_db_cursor_t *cur = dap_global_db_cursor_create(btree);
        dap_assert(cur != NULL, "Cursor should be created");

        // Forward scan
        int count_fwd = 0;
        int rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_FIRST, NULL);
        dap_assert(rc == 0, "Cursor FIRST should succeed");
        while (dap_global_db_cursor_valid(cur)) {
            count_fwd++;
            rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_NEXT, NULL);
        }
        dap_assert(count_fwd == (int)dap_global_db_count(btree),
                   "Forward cursor count should match tree count");

        // Backward scan
        int count_bwd = 0;
        rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_LAST, NULL);
        dap_assert(rc == 0, "Cursor LAST should succeed");
        while (dap_global_db_cursor_valid(cur)) {
            count_bwd++;
            rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_PREV, NULL);
        }
        dap_assert(count_bwd == count_fwd,
                   "Backward cursor count should match forward cursor count");

        dap_global_db_cursor_close(cur);
    }

    // --- Test 5: Delete all remaining entries ---
    {
        for (int i = 1; i < NUM_ENTRIES; i += 2) {
            dap_global_db_key_t key = s_make_key(1000, (uint64_t)i);
            dap_global_db_delete(btree, &key);
        }
        dap_assert(dap_global_db_count(btree) == 0,
                   "Count should be 0 after deleting all entries");
    }

    dap_global_db_close(btree);
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
#ifdef DAP_OS_WINDOWS
    mkdir(TEST_DIR);
#else
    mkdir(TEST_DIR, 0755);
#endif

    char path[256];
    snprintf(path, sizeof(path), "%s/regression_split.gdb", TEST_DIR);

    dap_global_db_t *btree = dap_global_db_create(path);
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

    for (int i = NUM_RECORDS; i > 0; i--) {
        dap_global_db_key_t key = s_make_key((uint64_t)i, (uint64_t)(i * 7919));

        int rc = dap_global_db_insert(btree, &key, NULL, 0,
                                             value, VALUE_SIZE, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Insert should succeed without SIGSEGV");
    }

    dap_global_db_sync(btree);

    int verified = 0;
    for (int i = NUM_RECORDS; i > 0; i--) {
        dap_global_db_key_t key = s_make_key((uint64_t)i, (uint64_t)(i * 7919));

        char *text_key = NULL;
        void *out_value = NULL, *out_sign = NULL;
        uint32_t out_value_len = 0, out_sign_len = 0;
        uint8_t out_flags = 0;

        int rc = dap_global_db_fetch(btree, &key, &text_key,
                                          &out_value, &out_value_len,
                                          &out_sign, &out_sign_len, &out_flags);
        dap_assert_PIF(rc == 0, "Record lookup should succeed after split");
        dap_assert_PIF(out_value_len == VALUE_SIZE,
                       "Value length should match (data corruption check)");

        uint8_t *vp = (uint8_t *)out_value;
        int l_ok = 1;
        for (size_t j = 0; j < VALUE_SIZE; j++) {
            if (vp[j] != 0xAB) {
                l_ok = 0;
                break;
            }
        }
        dap_assert_PIF(l_ok, "Value data should be intact (no heap corruption)");

        DAP_DEL_Z(out_value);
        DAP_DEL_Z(out_sign);
        DAP_DEL_Z(text_key);
        verified++;
    }

    dap_test_msg("Verified %d of %d records via lookup", verified, NUM_RECORDS);
    dap_assert(verified == NUM_RECORDS,
               "All records should be verifiable (no data corruption)");

    dap_global_db_close(btree);
    dap_pass_msg("Regression: leaf split + decreasing keys");
}

// ============================================================================
// Edge-Case & Boundary Tests
// ============================================================================

/**
 * @brief Operations on a completely empty tree.
 * Covers: count, exists, get, delete, cursor FIRST/LAST on empty tree.
 */
static void test_btree_empty_tree(void)
{
    dap_test_msg("Testing operations on empty tree");

    s_cleanup_test_dir();
#ifdef DAP_OS_WINDOWS
    mkdir(TEST_DIR);
#else
    mkdir(TEST_DIR, 0755);
#endif

    char path[256];
    snprintf(path, sizeof(path), "%s/empty.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Empty tree should be created");

    // Count
    dap_assert(dap_global_db_count(btree) == 0,
               "Empty tree count should be 0");

    // Exists
    dap_global_db_key_t key = s_make_key(1, 1);
    dap_assert(!dap_global_db_exists(btree, &key),
               "Exists on empty tree should return false");

    // Get
    char *tk = NULL; void *vd = NULL, *sd = NULL;
    uint32_t vl = 0, sl = 0; uint8_t fl = 0;
    int rc = dap_global_db_fetch(btree, &key, &tk, &vd, &vl, &sd, &sl, &fl);
    dap_assert(rc != 0, "Get on empty tree should fail");

    // Delete
    rc = dap_global_db_delete(btree, &key);
    dap_assert(rc != 0, "Delete on empty tree should fail (not found)");

    // Cursor FIRST — should position but not be valid (no entries)
    dap_global_db_cursor_t *cur = dap_global_db_cursor_create(btree);
    dap_assert(cur != NULL, "Cursor should be created on empty tree");
    dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_FIRST, NULL);
    dap_assert(!dap_global_db_cursor_valid(cur),
               "Cursor should not be valid on empty tree after FIRST");

    // Cursor LAST — same: not valid
    dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_LAST, NULL);
    dap_assert(!dap_global_db_cursor_valid(cur),
               "Cursor should not be valid on empty tree after LAST");

    dap_global_db_cursor_close(cur);
    dap_global_db_close(btree);
    dap_pass_msg("Empty tree operations");
}

/**
 * @brief Single-entry tree: insert one, verify all ops, delete, verify empty.
 */
static void test_btree_single_entry(void)
{
    dap_test_msg("Testing single-entry tree");

    char path[256];
    snprintf(path, sizeof(path), "%s/single.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    dap_global_db_key_t key = s_make_key(42, 42);
    int rc = dap_global_db_insert(btree, &key, "k", 2, "v", 2, NULL, 0, 0);
    dap_assert(rc == 0, "Single insert should succeed");
    dap_assert(dap_global_db_count(btree) == 1, "Count should be 1");
    dap_assert(dap_global_db_exists(btree, &key), "Key should exist");

    // Cursor: FIRST == LAST, NEXT fails, PREV fails
    dap_global_db_cursor_t *cur = dap_global_db_cursor_create(btree);
    rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_FIRST, NULL);
    dap_assert(rc == 0, "FIRST should succeed with 1 entry");
    dap_assert(dap_global_db_cursor_valid(cur), "Cursor should be valid");

    rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_NEXT, NULL);
    dap_assert(!dap_global_db_cursor_valid(cur),
               "NEXT after single entry should invalidate cursor");

    rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_LAST, NULL);
    dap_assert(rc == 0, "LAST should succeed with 1 entry");

    rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_PREV, NULL);
    dap_assert(!dap_global_db_cursor_valid(cur),
               "PREV after single entry should invalidate cursor");

    dap_global_db_cursor_close(cur);

    // Delete the only entry
    rc = dap_global_db_delete(btree, &key);
    dap_assert(rc == 0, "Delete single entry should succeed");
    dap_assert(dap_global_db_count(btree) == 0, "Count should be 0 after delete");
    dap_assert(!dap_global_db_exists(btree, &key), "Key should not exist after delete");

    // Cursor on now-empty tree
    cur = dap_global_db_cursor_create(btree);
    dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_FIRST, NULL);
    dap_assert(!dap_global_db_cursor_valid(cur),
               "Cursor should not be valid on emptied tree");
    dap_global_db_cursor_close(cur);

    dap_global_db_close(btree);
    dap_pass_msg("Single-entry tree");
}

/**
 * @brief Update: insert same key twice -> value updated, count unchanged.
 */
static void test_btree_update_overwrite(void)
{
    dap_test_msg("Testing key update/overwrite");

    char path[256];
    snprintf(path, sizeof(path), "%s/update.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    dap_global_db_key_t key = s_make_key(100, 200);

    // First insert
    int rc = dap_global_db_insert(btree, &key, "key1", 5,
                                         "old_value", 10, NULL, 0, 0);
    dap_assert(rc == 0, "First insert should succeed");
    dap_assert(dap_global_db_count(btree) == 1, "Count should be 1");

    // Second insert with same key (update)
    rc = dap_global_db_insert(btree, &key, "key1", 5,
                                     "new_value!!", 12, NULL, 0, 0);
    dap_assert(rc == 0, "Update insert should succeed");
    dap_assert(dap_global_db_count(btree) == 1,
               "Count should remain 1 after update");

    // Verify updated value
    char *tk = NULL; void *vd = NULL, *sd = NULL;
    uint32_t vl = 0, sl = 0; uint8_t fl = 0;
    rc = dap_global_db_fetch(btree, &key, &tk, &vd, &vl, &sd, &sl, &fl);
    dap_assert(rc == 0, "Get after update should succeed");
    dap_assert(vl == 12, "Value length should match updated value");
    dap_assert(memcmp(vd, "new_value!!", 12) == 0,
               "Value data should be the updated value");
    DAP_DEL_Z(tk); DAP_DEL_Z(vd); DAP_DEL_Z(sd);

    // Multiple updates: insert 50 entries, update all of them
    for (int i = 0; i < 50; i++) {
        dap_global_db_key_t k = s_make_key(500, (uint64_t)i);
        char v[32];
        snprintf(v, sizeof(v), "orig_%d", i);
        dap_global_db_insert(btree, &k, v, (uint32_t)strlen(v) + 1,
                                    v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
    }
    dap_assert(dap_global_db_count(btree) == 51, "Count should be 51");

    for (int i = 0; i < 50; i++) {
        dap_global_db_key_t k = s_make_key(500, (uint64_t)i);
        char v[32];
        snprintf(v, sizeof(v), "updated_%d", i);
        dap_global_db_insert(btree, &k, v, (uint32_t)strlen(v) + 1,
                                    v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
    }
    dap_assert(dap_global_db_count(btree) == 51,
               "Count should still be 51 after mass update");

    for (int i = 0; i < 50; i++) {
        dap_global_db_key_t k = s_make_key(500, (uint64_t)i);
        char expected[32];
        snprintf(expected, sizeof(expected), "updated_%d", i);
        vd = NULL; vl = 0; tk = NULL; sd = NULL;
        rc = dap_global_db_fetch(btree, &k, &tk, &vd, &vl, &sd, &sl, &fl);
        dap_assert_PIF(rc == 0, "Get updated entry should succeed");
        dap_assert_PIF(vl == strlen(expected) + 1, "Updated value length should match");
        dap_assert_PIF(memcmp(vd, expected, vl) == 0, "Updated value data should match");
        DAP_DEL_Z(tk); DAP_DEL_Z(vd); DAP_DEL_Z(sd);
    }

    dap_global_db_close(btree);
    dap_pass_msg("Update/overwrite");
}

/**
 * @brief Clear: populate, clear, verify empty, re-insert, verify.
 */
static void test_btree_clear(void)
{
    dap_test_msg("Testing clear operation");

    char path[256];
    snprintf(path, sizeof(path), "%s/clear.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    // Populate
    for (int i = 0; i < 200; i++) {
        dap_global_db_key_t key = s_make_key(1000, (uint64_t)i);
        char v[32]; snprintf(v, sizeof(v), "v_%d", i);
        dap_assert_PIF(dap_global_db_insert(btree, &key, v, (uint32_t)strlen(v) + 1,
                                    v, (uint32_t)strlen(v) + 1, NULL, 0, 0) == 0,
                       "Insert should succeed");
    }
    dap_assert(dap_global_db_count(btree) == 200, "Count should be 200");

    // Clear
    int rc = dap_global_db_clear(btree);
    dap_assert(rc == 0, "Clear should succeed");
    dap_assert(dap_global_db_count(btree) == 0, "Count should be 0 after clear");

    // Cursor on cleared tree — not valid
    dap_global_db_cursor_t *cur = dap_global_db_cursor_create(btree);
    dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_FIRST, NULL);
    dap_assert(!dap_global_db_cursor_valid(cur),
               "Cursor should not be valid on cleared tree");
    dap_global_db_cursor_close(cur);

    // Exists on cleared tree
    dap_global_db_key_t key = s_make_key(1000, 100);
    dap_assert(!dap_global_db_exists(btree, &key),
               "Previously existing key should not exist after clear");

    // Re-insert after clear
    for (int i = 0; i < 100; i++) {
        dap_global_db_key_t k = s_make_key(2000, (uint64_t)i);
        char v[32]; snprintf(v, sizeof(v), "new_%d", i);
        rc = dap_global_db_insert(btree, &k, v, (uint32_t)strlen(v) + 1,
                                         v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Insert after clear should succeed");
    }
    dap_assert(dap_global_db_count(btree) == 100,
               "Count should be 100 after re-insert");

    for (int i = 0; i < 100; i++) {
        dap_global_db_key_t k = s_make_key(2000, (uint64_t)i);
        dap_assert_PIF(dap_global_db_exists(btree, &k),
                       "Re-inserted entry should exist");
    }

    dap_global_db_close(btree);
    dap_pass_msg("Clear operation");
}

/**
 * @brief Cursor SEEK (SET): exact match and non-existent key.
 */
static void test_btree_cursor_seek(void)
{
    dap_test_msg("Testing cursor SEEK (SET) operation");

    char path[256];
    snprintf(path, sizeof(path), "%s/seek.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    // Insert entries with gaps: 10, 20, 30, ..., 500
    for (int i = 1; i <= 50; i++) {
        dap_global_db_key_t key = s_make_key(1000, (uint64_t)(i * 10));
        char v[16]; snprintf(v, sizeof(v), "v%d", i * 10);
        dap_global_db_insert(btree, &key, v, (uint32_t)strlen(v) + 1,
                                    v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
    }
    dap_global_db_sync(btree);

    dap_global_db_cursor_t *cur = dap_global_db_cursor_create(btree);
    dap_assert(cur != NULL, "Cursor should be created");

    // Seek to exact key (exists)
    dap_global_db_key_t seek_key = s_make_key(1000, 250);
    int rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_SET, &seek_key);
    dap_assert(rc == 0, "SEEK to existing key should succeed");
    dap_assert(dap_global_db_cursor_valid(cur), "Cursor should be valid after SEEK");

    dap_global_db_key_t got_key;
    char *tk = NULL; void *vd = NULL, *sd = NULL;
    uint32_t vl = 0, sl = 0; uint8_t fl = 0;
    rc = dap_global_db_cursor_get(cur, &got_key, &tk, &vd, &vl, &sd, &sl, &fl);
    dap_assert(rc == 0, "cursor_get after SEEK should succeed");
    dap_assert(dap_global_db_key_compare(&got_key, &seek_key) == 0,
               "Sought key should match");
    DAP_DEL_Z(tk); DAP_DEL_Z(vd); DAP_DEL_Z(sd);

    // NEXT after SEEK
    rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_NEXT, NULL);
    dap_assert(rc == 0, "NEXT after SEEK should succeed");
    rc = dap_global_db_cursor_get(cur, &got_key, &tk, &vd, &vl, &sd, &sl, &fl);
    dap_assert(rc == 0, "Get next entry after seek should succeed");
    dap_global_db_key_t expected_next = s_make_key(1000, 260);
    dap_assert(dap_global_db_key_compare(&got_key, &expected_next) == 0,
               "Next key after 250 should be 260");
    DAP_DEL_Z(tk); DAP_DEL_Z(vd); DAP_DEL_Z(sd);

    // Seek to first key
    dap_global_db_key_t first_key = s_make_key(1000, 10);
    rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_SET, &first_key);
    dap_assert(rc == 0, "SEEK to first key should succeed");

    // Seek to last key
    dap_global_db_key_t last_key = s_make_key(1000, 500);
    rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_SET, &last_key);
    dap_assert(rc == 0, "SEEK to last key should succeed");

    // Seek to non-existent key
    dap_global_db_key_t missing = s_make_key(1000, 15);
    rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_SET, &missing);
    dap_assert(rc != 0, "SEEK to non-existent key should fail");

    dap_global_db_cursor_close(cur);
    dap_global_db_close(btree);
    dap_pass_msg("Cursor SEEK");
}

/**
 * @brief Reverse iteration with full data verification.
 */
static void test_btree_cursor_reverse(void)
{
    dap_test_msg("Testing reverse iteration with data verification");

    char path[256];
    snprintf(path, sizeof(path), "%s/reverse.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    const int N = 300;
    for (int i = 0; i < N; i++) {
        dap_global_db_key_t key = s_make_key(1000, (uint64_t)i);
        char v[32]; snprintf(v, sizeof(v), "rev_%d", i);
        dap_global_db_insert(btree, &key, v, (uint32_t)strlen(v) + 1,
                                    v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
    }
    dap_global_db_sync(btree);

    // Reverse iterate and verify descending key order
    dap_global_db_cursor_t *cur = dap_global_db_cursor_create(btree);
    int rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_LAST, NULL);
    dap_assert(rc == 0, "LAST should succeed");

    int count = 0;
    dap_global_db_key_t prev_key = {0};
    bool first = true;
    while (dap_global_db_cursor_valid(cur)) {
        dap_global_db_key_t k;
        char *tk = NULL; void *vd = NULL, *sd = NULL;
        uint32_t vl = 0, sl = 0; uint8_t fl = 0;
        rc = dap_global_db_cursor_get(cur, &k, &tk, &vd, &vl, &sd, &sl, &fl);
        dap_assert_PIF(rc == 0, "Cursor get in reverse should succeed");

        if (!first) {
            dap_assert_PIF(dap_global_db_key_compare(&k, &prev_key) < 0,
                           "Keys should be in descending order during reverse iteration");
        }
        prev_key = k;
        first = false;
        count++;
        DAP_DEL_Z(tk); DAP_DEL_Z(vd); DAP_DEL_Z(sd);
        dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_PREV, NULL);
    }
    dap_assert(count == N, "Reverse iteration should visit all entries");

    dap_global_db_cursor_close(cur);
    dap_global_db_close(btree);
    dap_pass_msg("Reverse iteration");
}

/**
 * @brief Random-order insert -> sorted cursor output.
 * Uses a simple LCG to generate a permutation.
 */
static void test_btree_key_ordering(void)
{
    dap_test_msg("Testing key ordering: random insert -> sorted cursor");

    char path[256];
    snprintf(path, sizeof(path), "%s/ordering.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    const int N = 1000;
    // Generate a pseudo-random permutation using a simple shuffle
    int indices[1000];
    for (int i = 0; i < N; i++) indices[i] = i;
    uint32_t seed = 12345;
    for (int i = N - 1; i > 0; i--) {
        seed = seed * 1103515245 + 12345;
        int j = (int)(seed % (uint32_t)(i + 1));
        int tmp = indices[i]; indices[i] = indices[j]; indices[j] = tmp;
    }

    for (int i = 0; i < N; i++) {
        dap_global_db_key_t key = s_make_key(1000, (uint64_t)indices[i]);
        char v[16]; snprintf(v, sizeof(v), "%d", indices[i]);
        int rc = dap_global_db_insert(btree, &key, v, (uint32_t)strlen(v) + 1,
                                             v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Random-order insert should succeed");
    }
    dap_global_db_sync(btree);

    dap_global_db_cursor_t *cur = dap_global_db_cursor_create(btree);
    int rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_FIRST, NULL);
    dap_assert(rc == 0, "FIRST should succeed");

    int count = 0;
    dap_global_db_key_t prev_key = {0};
    bool first = true;
    while (dap_global_db_cursor_valid(cur)) {
        dap_global_db_key_t k;
        char *tk = NULL; void *vd = NULL, *sd = NULL;
        uint32_t vl = 0, sl = 0; uint8_t fl = 0;
        dap_global_db_cursor_get(cur, &k, &tk, &vd, &vl, &sd, &sl, &fl);

        if (!first) {
            dap_assert_PIF(dap_global_db_key_compare(&k, &prev_key) > 0,
                           "Keys should be in ascending order after random insert");
        }
        prev_key = k;
        first = false;
        count++;
        DAP_DEL_Z(tk); DAP_DEL_Z(vd); DAP_DEL_Z(sd);
        dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_NEXT, NULL);
    }
    dap_assert(count == N, "Should iterate all entries after random insert");

    dap_global_db_cursor_close(cur);
    dap_global_db_close(btree);
    dap_pass_msg("Key ordering");
}

/** Benchmark-style key: first 8 bytes = big-endian index, bytes 8..15 = 0 for becrc */
static void s_bench_key(uint8_t *key_buf, size_t key_size, uint64_t index,
                        dap_global_db_key_t *out_key)
{
    memset(key_buf, 0, key_size);
    int n = 8 < (int)key_size ? 8 : (int)key_size;
    for (int i = 0; i < n; i++)
        key_buf[i] = (uint8_t)((index >> ((n - 1 - i) * 8)) & 0xFF);
    out_key->bets = *(uint64_t *)key_buf;
    out_key->becrc = key_size > 8 ? *(uint64_t *)(key_buf + 8) : 0;
}

/**
 * @brief Exact benchmark scenario: n=2, key_size 2345, value 4978 (overflow), then verify.
 * Reproduces "2/2 mismatches" and "read 1/2" if there is a bug.
 */
static void test_btree_overflow_benchmark_n2(void)
{
    dap_test_msg("Testing overflow benchmark scenario (n=2, key 2345, value 4978)");

    char path[256];
    snprintf(path, sizeof(path), "%s/overflow_bench_n2.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    const size_t KEY_SIZE = 2345;
    const size_t VAL_SIZE = 4978;
    uint8_t *key = (uint8_t *)malloc(KEY_SIZE);
    uint8_t *value = (uint8_t *)malloc(VAL_SIZE);
    dap_assert(key != NULL && value != NULL, "Allocation should succeed");

    for (size_t i = 0; i < 2; i++) {
        dap_global_db_key_t btree_key;
        s_bench_key(key, KEY_SIZE, i, &btree_key);
        memset(value, (int)(i & 0xFF), VAL_SIZE);
        int rc = dap_global_db_insert(btree, &btree_key, NULL, 0,
                                           value, (uint32_t)VAL_SIZE, NULL, 0, 0);
        dap_assert(rc == 0, "Insert overflow (bench n=2) should succeed");
    }
    dap_assert(dap_global_db_count(btree) == 2, "Count should be 2");

    for (size_t i = 0; i < 2; i++) {
        dap_global_db_key_t vk;
        s_bench_key(key, KEY_SIZE, i, &vk);
        dap_global_db_ref_t vr;
        int rc = dap_global_db_get_ref(btree, &vk, NULL, &vr, NULL, NULL);
        dap_assert(rc == 0, "Get overflow (bench n=2) should find key");
        dap_assert(vr.len == VAL_SIZE, "Value length should match");
        uint8_t expected = (uint8_t)(i & 0xFF);
        bool intact = true;
        for (size_t j = 0; j < VAL_SIZE && intact; j++) {
            if (((const uint8_t *)vr.data)[j] != expected) intact = false;
        }
        dap_assert(intact, "Overflow value data should match");
    }

    free(key);
    free(value);
    dap_global_db_close(btree);
    dap_pass_msg("Overflow benchmark n=2");
}

/**
 * @brief Overflow values: value size > page data size (stored in overflow chain).
 * Same scenario as benchmark -v 4978.
 */
static void test_btree_overflow_values(void)
{
    dap_test_msg("Testing overflow values (> page data size)");

    char path[256];
    snprintf(path, sizeof(path), "%s/overflow_val.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    /* Value larger than PAGE_DATA_SIZE (~4064) to trigger overflow chain */
    const size_t VAL_SIZE = 4978;
    uint8_t *big_val = (uint8_t *)calloc(VAL_SIZE, 1);
    dap_assert(big_val != NULL, "Allocation should succeed");

    const int N = 5;
    for (int i = 0; i < N; i++) {
        memset(big_val, (uint8_t)(i & 0xFF), VAL_SIZE);
        dap_global_db_key_t key = s_make_key(4000, (uint64_t)i);
        int rc = dap_global_db_insert(btree, &key, NULL, 0,
                                            big_val, (uint32_t)VAL_SIZE, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Insert overflow value should succeed");
    }
    dap_assert(dap_global_db_count(btree) == (uint64_t)N, "Count should match");

    for (int i = 0; i < N; i++) {
        dap_global_db_key_t key = s_make_key(4000, (uint64_t)i);
        dap_global_db_ref_t vr;
        int rc = dap_global_db_get_ref(btree, &key, NULL, &vr, NULL, NULL);
        dap_assert_PIF(rc == 0, "Get overflow value should succeed");
        dap_assert_PIF(vr.len == VAL_SIZE, "Value length should match");
        uint8_t expected = (uint8_t)(i & 0xFF);
        bool intact = true;
        for (size_t j = 0; j < VAL_SIZE && intact; j++) {
            if (((const uint8_t *)vr.data)[j] != expected) intact = false;
        }
        dap_assert(intact, "Overflow value data should be intact");
    }

    free(big_val);
    dap_global_db_close(btree);
    dap_pass_msg("Overflow values");
}

/**
 * @brief Large values near page-data size.
 * Tests leaf pages with very few entries, deep trees, splits with large payloads.
 */
static void test_btree_large_values(void)
{
    dap_test_msg("Testing large values (near page size)");

    char path[256];
    snprintf(path, sizeof(path), "%s/large_val.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    // ~1000 byte value: 3-4 entries per leaf, many splits
    const size_t VAL_SIZE = 1000;
    uint8_t *big_val = calloc(VAL_SIZE, 1);
    dap_assert(big_val != NULL, "Allocation should succeed");

    const int N = 100;
    for (int i = 0; i < N; i++) {
        memset(big_val, (uint8_t)(i & 0xFF), VAL_SIZE);
        dap_global_db_key_t key = s_make_key(3000, (uint64_t)i);
        char kstr[16]; snprintf(kstr, sizeof(kstr), "big_%d", i);
        int rc = dap_global_db_insert(btree, &key, kstr, (uint32_t)strlen(kstr) + 1,
                                             big_val, (uint32_t)VAL_SIZE, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Insert large value should succeed");
    }
    dap_assert(dap_global_db_count(btree) == (uint64_t)N, "Count should match");
    dap_global_db_sync(btree);

    for (int i = 0; i < N; i++) {
        dap_global_db_key_t key = s_make_key(3000, (uint64_t)i);
        char *tk = NULL; void *vd = NULL, *sd = NULL;
        uint32_t vl = 0, sl = 0; uint8_t fl = 0;
        int rc = dap_global_db_fetch(btree, &key, &tk, &vd, &vl, &sd, &sl, &fl);
        dap_assert_PIF(rc == 0, "Get large value should succeed");
        dap_assert_PIF(vl == (uint32_t)VAL_SIZE, "Value length should match");

        uint8_t expected = (uint8_t)(i & 0xFF);
        uint8_t *p = (uint8_t *)vd;
        bool intact = true;
        for (size_t j = 0; j < VAL_SIZE; j++) {
            if (p[j] != expected) { intact = false; break; }
        }
        dap_assert_PIF(intact, "Large value data should be intact");
        DAP_DEL_Z(tk); DAP_DEL_Z(vd); DAP_DEL_Z(sd);
    }

    // Cursor iteration with large values
    dap_global_db_cursor_t *cur = dap_global_db_cursor_create(btree);
    int rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_FIRST, NULL);
    dap_assert(rc == 0, "FIRST should succeed for large values");
    int count = 0;
    while (dap_global_db_cursor_valid(cur)) {
        count++;
        dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_NEXT, NULL);
    }
    dap_assert(count == N, "Cursor should iterate all large-value entries");
    dap_global_db_cursor_close(cur);

    free(big_val);
    dap_global_db_close(btree);
    dap_pass_msg("Large values");
}

/**
 * @brief Persistence after delete: insert, delete some, close, reopen, verify.
 */
static void test_btree_persistence_after_delete(void)
{
    dap_test_msg("Testing persistence after delete");

    s_cleanup_test_dir();
#ifdef DAP_OS_WINDOWS
    mkdir(TEST_DIR);
#else
    mkdir(TEST_DIR, 0755);
#endif

    char path[256];
    snprintf(path, sizeof(path), "%s/persist_del.gdb", TEST_DIR);

    // Phase 1: Create, insert 200, delete even keys
    {
        dap_global_db_t *btree = dap_global_db_create(path);
        dap_assert(btree != NULL, "Tree should be created");

        for (int i = 0; i < 200; i++) {
            dap_global_db_key_t key = s_make_key(5000, (uint64_t)i);
            char v[32]; snprintf(v, sizeof(v), "pv_%d", i);
            dap_global_db_insert(btree, &key, v, (uint32_t)strlen(v) + 1,
                                        v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
        }
        for (int i = 0; i < 200; i += 2) {
            dap_global_db_key_t key = s_make_key(5000, (uint64_t)i);
            dap_global_db_delete(btree, &key);
        }
        dap_assert(dap_global_db_count(btree) == 100,
                   "Count should be 100 after deleting even keys");
        dap_global_db_sync(btree);
        dap_global_db_close(btree);
    }

    // Phase 2: Reopen and verify only odd keys remain
    {
        dap_global_db_t *btree = dap_global_db_open(path, false);
        dap_assert(btree != NULL, "Tree should reopen");
        dap_assert(dap_global_db_count(btree) == 100,
                   "Count should be 100 after reopen");

        for (int i = 0; i < 200; i++) {
            dap_global_db_key_t key = s_make_key(5000, (uint64_t)i);
            bool exists = dap_global_db_exists(btree, &key);
            if (i % 2 == 0) {
                dap_assert_PIF(!exists, "Even key should not exist after reopen");
            } else {
                dap_assert_PIF(exists, "Odd key should exist after reopen");
            }
        }

        dap_global_db_close(btree);
    }

    dap_pass_msg("Persistence after delete");
}

/**
 * @brief Root collapse: grow tree tall (many levels), delete until tree
 * shrinks back to a single-level root leaf.
 */
static void test_btree_root_collapse(void)
{
    dap_test_msg("Testing root collapse (tree height reduction)");

    char path[256];
    snprintf(path, sizeof(path), "%s/collapse.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    const int N = 2000;
    for (int i = 0; i < N; i++) {
        dap_global_db_key_t key = s_make_key(6000, (uint64_t)i);
        char v[32]; snprintf(v, sizeof(v), "c_%d", i);
        int rc = dap_global_db_insert(btree, &key, v, (uint32_t)strlen(v) + 1,
                                             v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Insert for collapse test should succeed");
    }
    dap_global_db_sync(btree);

    for (int i = 5; i < N; i++) {
        dap_global_db_key_t key = s_make_key(6000, (uint64_t)i);
        int rc = dap_global_db_delete(btree, &key);
        dap_assert_PIF(rc == 0, "Delete for collapse should succeed");
    }
    dap_assert(dap_global_db_count(btree) == 5,
               "Count should be 5 after mass delete");

    for (int i = 0; i < 5; i++) {
        dap_global_db_key_t key = s_make_key(6000, (uint64_t)i);
        dap_assert_PIF(dap_global_db_exists(btree, &key),
                       "Surviving entry should exist after collapse");
    }

    // Cursor check
    dap_global_db_cursor_t *cur = dap_global_db_cursor_create(btree);
    int rc = dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_FIRST, NULL);
    dap_assert(rc == 0, "FIRST after collapse should succeed");
    int count = 0;
    while (dap_global_db_cursor_valid(cur)) {
        count++;
        dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_NEXT, NULL);
    }
    dap_assert(count == 5, "Cursor should iterate 5 entries after collapse");
    dap_global_db_cursor_close(cur);

    for (int i = 0; i < 5; i++) {
        dap_global_db_key_t key = s_make_key(6000, (uint64_t)i);
        rc = dap_global_db_delete(btree, &key);
        dap_assert_PIF(rc == 0, "Delete last entries should succeed");
    }
    dap_assert(dap_global_db_count(btree) == 0,
               "Tree should be empty after deleting everything");

    dap_global_db_close(btree);
    dap_pass_msg("Root collapse");
}

/**
 * @brief Insert after delete: delete entries, insert new ones reusing freed
 * pages. Verifies free list integrity and page reuse correctness.
 */
static void test_btree_insert_after_delete(void)
{
    dap_test_msg("Testing insert after delete (page reuse)");

    char path[256];
    snprintf(path, sizeof(path), "%s/reuse.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    const int REUSE_N = 500;

    // Phase 1: Insert entries
    for (int i = 0; i < REUSE_N; i++) {
        dap_global_db_key_t key = s_make_key(7000, (uint64_t)i);
        char v[32]; snprintf(v, sizeof(v), "old_%d", i);
        dap_global_db_insert(btree, &key, v, (uint32_t)strlen(v) + 1,
                                    v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
    }

    // Phase 2: Delete all
    for (int i = 0; i < REUSE_N; i++) {
        dap_global_db_key_t key = s_make_key(7000, (uint64_t)i);
        dap_global_db_delete(btree, &key);
    }
    dap_assert(dap_global_db_count(btree) == 0,
               "Should be empty after deleting all");

    for (int i = 0; i < REUSE_N; i++) {
        dap_global_db_key_t key = s_make_key(8000, (uint64_t)i);
        char v[32]; snprintf(v, sizeof(v), "new_%d", i);
        int rc = dap_global_db_insert(btree, &key, v, (uint32_t)strlen(v) + 1,
                                             v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Re-insert should succeed");
        dap_assert_PIF(dap_global_db_exists(btree, &key),
                       "Just-inserted key must be immediately readable");
    }
    dap_assert(dap_global_db_count(btree) == (uint64_t)REUSE_N,
               "Count should match after re-insert");

    for (int i = 0; i < REUSE_N; i++) {
        dap_global_db_key_t key = s_make_key(7000, (uint64_t)i);
        dap_assert_PIF(!dap_global_db_exists(btree, &key),
                       "Old key should not exist after re-insert");
    }

    for (int i = 0; i < REUSE_N; i++) {
        dap_global_db_key_t key = s_make_key(8000, (uint64_t)i);
        char expected[32]; snprintf(expected, sizeof(expected), "new_%d", i);
        char *tk = NULL; void *vd = NULL, *sd = NULL;
        uint32_t vl = 0, sl = 0; uint8_t fl = 0;
        int rc = dap_global_db_fetch(btree, &key, &tk, &vd, &vl, &sd, &sl, &fl);
        dap_assert_PIF(rc == 0, "All re-inserted entries must be found");
        dap_assert_PIF(vl == strlen(expected) + 1, "Value length should match");
        dap_assert_PIF(memcmp(vd, expected, vl) == 0, "Value data should match");
        DAP_DEL_Z(tk); DAP_DEL_Z(vd); DAP_DEL_Z(sd);
    }

    dap_global_db_close(btree);
    dap_pass_msg("Insert after delete");
}

/**
 * @brief B-tree structural invariant verification test.
 *
 * Exercises dap_global_db_verify() across multiple mutation patterns:
 *   - Empty tree
 *   - Single entry
 *   - Sequential inserts (triggers append-only split)
 *   - Random-pattern deletes (triggers merge/rebalance)
 *   - Interleaved insert/delete cycles
 *   - Full delete back to empty
 *
 * Each phase asserts verify() returns 0 and entry count matches expectations.
 */
static void test_btree_invariants(void)
{
    dap_test_msg("B-tree invariant verification test");

    char path[256];
    snprintf(path, sizeof(path), "%s/invariants.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created for invariant test");

    uint64_t l_count = 0;
    int l_rc;

    // Phase 1: Empty tree
    l_rc = dap_global_db_verify(btree, &l_count);
    dap_assert(l_rc == 0, "Empty tree should pass verify");
    dap_assert(l_count == 0, "Empty tree verify count should be 0");

    // Phase 2: Single entry
    {
        dap_global_db_key_t key = s_make_key(100, 1);
        dap_global_db_insert(btree, &key, "k", 2, "v", 2, NULL, 0, 0);
        l_rc = dap_global_db_verify(btree, &l_count);
        dap_assert(l_rc == 0, "Single entry should pass verify");
        dap_assert(l_count == 1, "Single entry verify count should be 1");
        dap_global_db_delete(btree, &key);
        l_rc = dap_global_db_verify(btree, &l_count);
        dap_assert(l_rc == 0, "After deleting single entry should pass verify");
        dap_assert(l_count == 0, "After deleting single entry count should be 0");
    }

    // Phase 3: Sequential inserts (2000 entries, triggers many splits)
    const int N = 2000;
    for (int i = 0; i < N; i++) {
        dap_global_db_key_t key = s_make_key(200, (uint64_t)i);
        char v[32]; snprintf(v, sizeof(v), "inv_%d", i);
        int rc = dap_global_db_insert(btree, &key, v, (uint32_t)strlen(v) + 1,
                                             v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Invariant test insert should succeed");
    }
    dap_global_db_sync(btree);
    l_rc = dap_global_db_verify(btree, &l_count);
    dap_assert(l_rc == 0, "After 2000 inserts should pass verify");
    dap_assert(l_count == (uint64_t)N, "After 2000 inserts verify count should match");

    // Phase 4: Delete every 2nd entry (aggressive rebalancing)
    int l_deleted = 0;
    for (int i = 0; i < N; i += 2) {
        dap_global_db_key_t key = s_make_key(200, (uint64_t)i);
        if (dap_global_db_delete(btree, &key) == 0)
            l_deleted++;
    }
    int l_remaining = N - l_deleted;
    l_rc = dap_global_db_verify(btree, &l_count);
    dap_assert(l_rc == 0, "After deleting every 2nd entry should pass verify");
    dap_assert(l_count == (uint64_t)l_remaining,
               "After deleting every 2nd entry count should match");

    // Phase 5: Interleaved insert/delete — insert new keys into gaps
    int l_inserted = 0;
    for (int i = 0; i < N; i += 2) {
        dap_global_db_key_t key = s_make_key(300, (uint64_t)i);
        char v[32]; snprintf(v, sizeof(v), "new_%d", i);
        if (dap_global_db_insert(btree, &key, v, (uint32_t)strlen(v) + 1,
                                        v, (uint32_t)strlen(v) + 1, NULL, 0, 0) == 0) {
            l_inserted++;
        }
    }
    l_rc = dap_global_db_verify(btree, &l_count);
    dap_assert(l_rc == 0, "After interleaved insert/delete should pass verify");
    dap_assert(l_count == (uint64_t)(l_remaining + l_inserted),
               "After interleaved insert/delete count should match");

    // Phase 6: Delete everything
    for (int i = 0; i < N; i++) {
        dap_global_db_key_t k1 = s_make_key(200, (uint64_t)i);
        dap_global_db_delete(btree, &k1);
        dap_global_db_key_t k2 = s_make_key(300, (uint64_t)i);
        dap_global_db_delete(btree, &k2);
    }
    l_rc = dap_global_db_verify(btree, &l_count);
    dap_assert(l_rc == 0, "After deleting everything should pass verify");
    dap_assert(l_count == 0, "After deleting everything count should be 0");

    dap_global_db_close(btree);
    dap_pass_msg("B-tree invariant verification");
}

/**
 * @brief Stress test: 10000 records — full lifecycle.
 * Insert all, verify all, delete half, verify remainder, delete rest.
 */
static void test_btree_stress(void)
{
    dap_test_msg("Stress test: 10000 records full lifecycle");

    char path[256];
    snprintf(path, sizeof(path), "%s/stress.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    const int N = 10000;

    for (int i = 0; i < N; i++) {
        dap_global_db_key_t key = s_make_key(9000, (uint64_t)i);
        char v[32]; snprintf(v, sizeof(v), "s_%d", i);
        int rc = dap_global_db_insert(btree, &key, v, (uint32_t)strlen(v) + 1,
                                             v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Stress insert should succeed");
    }
    dap_global_db_sync(btree);
    dap_assert(dap_global_db_count(btree) == (uint64_t)N,
               "Count should be N after stress insert");

    // Verify tree integrity after mass insert
    {
        uint64_t l_verify_count = 0;
        int l_rc = dap_global_db_verify(btree, &l_verify_count);
        dap_assert(l_rc == 0, "Tree integrity after insert should be OK");
        dap_assert(l_verify_count == (uint64_t)N, "Verify count should match N after insert");
    }

    // Cursor count verification
    dap_global_db_cursor_t *cur = dap_global_db_cursor_create(btree);
    dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_FIRST, NULL);
    int fwd_count = 0;
    while (dap_global_db_cursor_valid(cur)) {
        fwd_count++;
        dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_NEXT, NULL);
    }
    dap_assert(fwd_count == N, "Forward cursor count should match N");

    // Backward count
    dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_LAST, NULL);
    int bwd_count = 0;
    while (dap_global_db_cursor_valid(cur)) {
        bwd_count++;
        dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_PREV, NULL);
    }
    dap_assert(bwd_count == N, "Backward cursor count should match N");
    dap_global_db_cursor_close(cur);

    // Delete every 3rd entry
    int deleted = 0;
    for (int i = 0; i < N; i += 3) {
        dap_global_db_key_t key = s_make_key(9000, (uint64_t)i);
        if (dap_global_db_delete(btree, &key) == 0)
            deleted++;
    }
    int remaining = N - deleted;
    dap_assert(dap_global_db_count(btree) == (uint64_t)remaining,
               "Count should match after partial delete");

    // Verify tree integrity after partial delete
    {
        uint64_t l_verify_count = 0;
        int l_rc = dap_global_db_verify(btree, &l_verify_count);
        dap_assert(l_rc == 0, "Tree integrity after partial delete should be OK");
        dap_assert(l_verify_count == (uint64_t)remaining,
                   "Verify count should match remaining after partial delete");
    }

    for (int i = 0; i < N; i++) {
        dap_global_db_key_t key = s_make_key(9000, (uint64_t)i);
        bool exists = dap_global_db_exists(btree, &key);
        if (i % 3 == 0) {
            dap_assert_PIF(!exists, "Deleted key should not exist");
        } else {
            dap_assert_PIF(exists, "Surviving key should exist");
        }
    }

    // Delete all remaining
    for (int i = 0; i < N; i++) {
        dap_global_db_key_t key = s_make_key(9000, (uint64_t)i);
        dap_global_db_delete(btree, &key);
    }
    dap_assert(dap_global_db_count(btree) == 0,
               "Count should be 0 after deleting everything");

    // Verify tree integrity after full delete
    {
        uint64_t l_verify_count = 0;
        int l_rc = dap_global_db_verify(btree, &l_verify_count);
        dap_assert(l_rc == 0, "Tree integrity after full delete should be OK");
        dap_assert(l_verify_count == 0, "Verify count should be 0 after full delete");
    }

    // Verify tree is truly empty
    cur = dap_global_db_cursor_create(btree);
    dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_FIRST, NULL);
    dap_assert(!dap_global_db_cursor_valid(cur),
               "Cursor should not be valid after stress-deleting all");
    dap_global_db_cursor_close(cur);

    dap_global_db_close(btree);
    dap_pass_msg("Stress test 10000 records");
}

// ============================================================================
// Concurrent Access Tests (Phase 3.4 + 3.5)
// ============================================================================

#include <pthread.h>

#define CONCURRENT_RECORDS 10000
#define NUM_READER_THREADS 4

typedef struct {
    dap_global_db_t *tree;
    int thread_id;
    int records_read;
    int mismatches;
} reader_thread_arg_t;

typedef struct {
    dap_global_db_t *tree;
    int start_index;
    int count;
    int records_written;
} writer_thread_arg_t;

/**
 * @brief Reader thread: reads all pre-populated records and verifies data.
 */
static void *s_reader_thread(void *a_arg)
{
    reader_thread_arg_t *arg = (reader_thread_arg_t *)a_arg;
    arg->records_read = 0;
    arg->mismatches = 0;

    for (int i = 0; i < CONCURRENT_RECORDS; i++) {
        dap_global_db_key_t key = s_make_key((uint64_t)i, (uint64_t)(i * 17));
        dap_global_db_ref_t val_ref = {0};
        int rc = dap_global_db_get_ref(arg->tree, &key, NULL, &val_ref, NULL, NULL);
        if (rc == 0) {
            arg->records_read++;
            // Verify value content: first 8 bytes should be big-endian index
            if (val_ref.len >= 8) {
                uint64_t expected_be = htobe64((uint64_t)i);
                if (memcmp(val_ref.data, &expected_be, 8) != 0)
                    arg->mismatches++;
            }
        }
    }
    return NULL;
}

/**
 * @brief Test: N threads read concurrently from a populated tree.
 * All threads should see consistent data, no crashes, no corruption.
 */
static void test_btree_concurrent_reads(void)
{
    s_cleanup_test_dir();
#ifdef DAP_OS_WINDOWS
    mkdir(TEST_DIR);
#else
    mkdir(TEST_DIR, 0755);
#endif

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/concurrent_reads.gdb", TEST_DIR);

    // Populate tree
    dap_global_db_t *tree = dap_global_db_create(filepath);
    dap_assert(tree != NULL, "Tree should be created for concurrent reads test");

    for (int i = 0; i < CONCURRENT_RECORDS; i++) {
        dap_global_db_key_t key = s_make_key((uint64_t)i, (uint64_t)(i * 17));
        uint8_t value[64];
        memset(value, 0, sizeof(value));
        uint64_t be_i = htobe64((uint64_t)i);
        memcpy(value, &be_i, 8);

        char text_key[32];
        snprintf(text_key, sizeof(text_key), "key_%d", i);
        int rc = dap_global_db_insert(tree, &key, text_key, (uint32_t)strlen(text_key) + 1,
                                             value, sizeof(value), NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Insert should succeed during populate");
    }

    dap_global_db_sync(tree);

    pthread_t threads[NUM_READER_THREADS];
    reader_thread_arg_t args[NUM_READER_THREADS];

    for (int t = 0; t < NUM_READER_THREADS; t++) {
        args[t].tree = tree;
        args[t].thread_id = t;
        int rc = pthread_create(&threads[t], NULL, s_reader_thread, &args[t]);
        dap_assert(rc == 0, "Reader thread should be created");
    }

    // Wait for all readers
    for (int t = 0; t < NUM_READER_THREADS; t++) {
        pthread_join(threads[t], NULL);
    }

    // Verify results
    for (int t = 0; t < NUM_READER_THREADS; t++) {
        dap_test_msg("Thread %d: read %d/%d records, %d mismatches",
                     t, args[t].records_read, CONCURRENT_RECORDS, args[t].mismatches);
        dap_assert(args[t].records_read == CONCURRENT_RECORDS,
                   "All records should be found by each reader thread");
        dap_assert(args[t].mismatches == 0,
                   "No data corruption in concurrent reads");
    }

    dap_global_db_close(tree);
    dap_pass_msg("Concurrent reads passed");
}

/**
 * @brief Writer thread: inserts new records while readers are active.
 */
static void *s_writer_thread(void *a_arg)
{
    writer_thread_arg_t *arg = (writer_thread_arg_t *)a_arg;
    arg->records_written = 0;

    for (int i = arg->start_index; i < arg->start_index + arg->count; i++) {
        dap_global_db_key_t key = s_make_key((uint64_t)i, (uint64_t)(i * 17));
        uint8_t value[64];
        memset(value, 0, sizeof(value));
        uint64_t be_i = htobe64((uint64_t)i);
        memcpy(value, &be_i, 8);

        char text_key[32];
        snprintf(text_key, sizeof(text_key), "key_%d", i);
        int rc = dap_global_db_insert(arg->tree, &key, text_key,
                                             (uint32_t)strlen(text_key) + 1,
                                             value, sizeof(value), NULL, 0, 0);
        if (rc == 0)
            arg->records_written++;
    }
    return NULL;
}

/**
 * @brief Test: 1 writer + N readers operating concurrently.
 * Writer inserts new records [CONCURRENT_RECORDS .. 2*CONCURRENT_RECORDS).
 * Readers read existing records [0 .. CONCURRENT_RECORDS).
 * Readers should never see corrupted data.
 */
static void test_btree_concurrent_read_write(void)
{
    s_cleanup_test_dir();
#ifdef DAP_OS_WINDOWS
    mkdir(TEST_DIR);
#else
    mkdir(TEST_DIR, 0755);
#endif

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/concurrent_rw.gdb", TEST_DIR);

    // Populate tree with initial records
    dap_global_db_t *tree = dap_global_db_create(filepath);
    dap_assert(tree != NULL, "Tree should be created for concurrent r/w test");

    for (int i = 0; i < CONCURRENT_RECORDS; i++) {
        dap_global_db_key_t key = s_make_key((uint64_t)i, (uint64_t)(i * 17));
        uint8_t value[64];
        memset(value, 0, sizeof(value));
        uint64_t be_i = htobe64((uint64_t)i);
        memcpy(value, &be_i, 8);

        char text_key[32];
        snprintf(text_key, sizeof(text_key), "key_%d", i);
        dap_global_db_insert(tree, &key, text_key, (uint32_t)strlen(text_key) + 1,
                                    value, sizeof(value), NULL, 0, 0);
    }

    dap_global_db_sync(tree);

    // Launch writer + readers concurrently
    pthread_t reader_threads[NUM_READER_THREADS];
    reader_thread_arg_t reader_args[NUM_READER_THREADS];
    pthread_t writer_tid;
    writer_thread_arg_t writer_arg;

    writer_arg.tree = tree;
    writer_arg.start_index = CONCURRENT_RECORDS;
    writer_arg.count = CONCURRENT_RECORDS / 2;  // Write 5000 new records

    // Start writer first
    int rc = pthread_create(&writer_tid, NULL, s_writer_thread, &writer_arg);
    dap_assert(rc == 0, "Writer thread should be created");

    // Start readers
    for (int t = 0; t < NUM_READER_THREADS; t++) {
        reader_args[t].tree = tree;
        reader_args[t].thread_id = t;
        rc = pthread_create(&reader_threads[t], NULL, s_reader_thread, &reader_args[t]);
        dap_assert(rc == 0, "Reader thread should be created");
    }

    // Wait for all
    pthread_join(writer_tid, NULL);
    for (int t = 0; t < NUM_READER_THREADS; t++) {
        pthread_join(reader_threads[t], NULL);
    }

    // Verify reader results — all pre-existing records should be readable
    for (int t = 0; t < NUM_READER_THREADS; t++) {
        dap_test_msg("Reader %d: read %d/%d, mismatches %d",
                     t, reader_args[t].records_read, CONCURRENT_RECORDS, reader_args[t].mismatches);
        dap_assert(reader_args[t].records_read == CONCURRENT_RECORDS,
                   "All pre-existing records should be found during concurrent r/w");
        dap_assert(reader_args[t].mismatches == 0,
                   "No data corruption during concurrent r/w");
    }

    dap_test_msg("Writer: wrote %d/%d records", writer_arg.records_written, writer_arg.count);
    dap_assert(writer_arg.records_written == writer_arg.count,
               "All new records should be written during concurrent r/w");

    // Verify total count
    uint64_t total = dap_global_db_count(tree);
    dap_test_msg("Total records after concurrent r/w: %llu", (unsigned long long)total);
    dap_assert(total == (uint64_t)(CONCURRENT_RECORDS + writer_arg.count),
               "Total count should include both pre-existing and new records");

    dap_global_db_close(tree);
    dap_pass_msg("Concurrent read+write passed");
}

// ============================================================================
// MVCC Snapshot Isolation Tests (Phase 5.6)
// ============================================================================

/**
 * @brief Test: cursor snapshot isolation — cursor sees tree state at creation time.
 *
 * 1. Populate with N records
 * 2. Open cursor (acquires snapshot)
 * 3. Delete half the records through the public API
 * 4. Iterate cursor — should still see ALL N records (snapshot isolation)
 * 5. Close cursor (releases snapshot)
 * 6. Open new cursor — should see N/2 records
 */
static void test_btree_snapshot_isolation(void)
{
    dap_test_msg("MVCC snapshot isolation test");

    s_cleanup_test_dir();
#ifdef DAP_OS_WINDOWS
    mkdir(TEST_DIR);
#else
    mkdir(TEST_DIR, 0755);
#endif

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/snapshot_iso.gdb", TEST_DIR);

    dap_global_db_t *tree = dap_global_db_create(filepath);
    dap_assert(tree != NULL, "Tree should be created for snapshot isolation test");

    const int N = 1000;

    // Populate
    for (int i = 0; i < N; i++) {
        dap_global_db_key_t key = s_make_key(500, (uint64_t)i);
        char v[32]; snprintf(v, sizeof(v), "snap_%d", i);
        int rc = dap_global_db_insert(tree, &key, v, (uint32_t)strlen(v) + 1,
                                             v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Snapshot isolation insert should succeed");
    }
    dap_global_db_sync(tree);
    dap_assert(dap_global_db_count(tree) == (uint64_t)N,
               "Should have N records before snapshot");

    // Open cursor — acquires snapshot of the current state (N records)
    dap_global_db_cursor_t *snap_cur = dap_global_db_cursor_create(tree);
    dap_assert(snap_cur != NULL, "Snapshot cursor should be created");

    // Delete half the records AFTER cursor creation
    for (int i = 0; i < N; i += 2) {
        dap_global_db_key_t key = s_make_key(500, (uint64_t)i);
        dap_global_db_delete(tree, &key);
    }
    dap_assert(dap_global_db_count(tree) == (uint64_t)(N / 2),
               "Should have N/2 records after deletes");

    // Iterate snapshot cursor — should still see ALL N records
    dap_global_db_cursor_move(snap_cur, DAP_GLOBAL_DB_FIRST, NULL);
    int snap_count = 0;
    dap_global_db_key_t prev_key = {0};
    bool ordering_ok = true;
    while (dap_global_db_cursor_valid(snap_cur)) {
        dap_global_db_key_t cur_key;
        char *tk = NULL; void *vd = NULL, *sd = NULL;
        uint32_t vl = 0, sl = 0; uint8_t fl = 0;
        if (dap_global_db_cursor_get(snap_cur, &cur_key, &tk, &vd, &vl, &sd, &sl, &fl) == 0) {
            if (snap_count > 0 && dap_global_db_key_compare(&cur_key, &prev_key) <= 0)
                ordering_ok = false;
            prev_key = cur_key;
            snap_count++;
            DAP_DEL_Z(tk); DAP_DEL_Z(vd); DAP_DEL_Z(sd);
        }
        dap_global_db_cursor_move(snap_cur, DAP_GLOBAL_DB_NEXT, NULL);
    }
    dap_global_db_cursor_close(snap_cur);

    dap_test_msg("Snapshot cursor saw %d records (expected %d)", snap_count, N);
    dap_assert(snap_count == N, "Snapshot cursor should see ALL N records despite concurrent deletes");
    dap_assert(ordering_ok, "Snapshot cursor keys should be in ascending order");

    // Open NEW cursor — should see current state (N/2 records)
    dap_global_db_cursor_t *new_cur = dap_global_db_cursor_create(tree);
    dap_global_db_cursor_move(new_cur, DAP_GLOBAL_DB_FIRST, NULL);
    int new_count = 0;
    while (dap_global_db_cursor_valid(new_cur)) {
        new_count++;
        dap_global_db_cursor_move(new_cur, DAP_GLOBAL_DB_NEXT, NULL);
    }
    dap_global_db_cursor_close(new_cur);

    dap_test_msg("New cursor saw %d records (expected %d)", new_count, N / 2);
    dap_assert(new_count == N / 2, "New cursor should see N/2 records after deletes");

    dap_global_db_close(tree);
    dap_pass_msg("MVCC snapshot isolation");
}

/**
 * @brief Thread argument for snapshot consistency test.
 */
typedef struct {
    dap_global_db_t *tree;
    int thread_id;
    int iterations;          // Number of cursor scan iterations
    int inconsistencies;     // Number of scans with non-monotonic counts
    int ordering_errors;     // Key ordering violations
    _Atomic int *writer_done;
} snapshot_reader_arg_t;

/**
 * @brief Reader thread: repeatedly opens cursors and scans, verifying consistency.
 *
 * Each scan MUST see a consistent count (non-decreasing between scans since
 * writer only inserts) and keys MUST be in sorted order within each scan.
 */
static void *s_snapshot_reader_thread(void *a_arg)
{
    snapshot_reader_arg_t *arg = (snapshot_reader_arg_t *)a_arg;
    arg->iterations = 0;
    arg->inconsistencies = 0;
    arg->ordering_errors = 0;

    int prev_scan_count = 0;

    while (!atomic_load(arg->writer_done) || arg->iterations < 10) {
        dap_global_db_cursor_t *cur = dap_global_db_cursor_create(arg->tree);
        if (!cur) break;

        dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_FIRST, NULL);
        int scan_count = 0;
        dap_global_db_key_t prev_key = {0};
        bool order_ok = true;

        while (dap_global_db_cursor_valid(cur)) {
            dap_global_db_key_t cur_key;
            if (dap_global_db_cursor_get(cur, &cur_key, NULL, NULL, NULL, NULL, NULL, NULL) == 0) {
                if (scan_count > 0 && dap_global_db_key_compare(&cur_key, &prev_key) <= 0)
                    order_ok = false;
                prev_key = cur_key;
            }
            scan_count++;
            dap_global_db_cursor_move(cur, DAP_GLOBAL_DB_NEXT, NULL);
        }

        uint64_t l_snap_root = cur->snapshot_root;
        uint64_t l_snap_txn = cur->snapshot_txn;
        uint64_t l_snap_count = cur->snapshot_count;

        // Verify count BEFORE closing cursor (snapshot still active, pages protected)
        uint64_t l_tree_count = dap_global_db_count_at_root(arg->tree, l_snap_root);

        dap_global_db_cursor_close(cur);

        if (!order_ok)
            arg->ordering_errors++;

        if (scan_count < prev_scan_count) {
            dap_test_msg("Thread %d: DECREASED %d -> %d (root=%llu txn=%llu snap_count=%llu tree_count=%llu)",
                         arg->thread_id, prev_scan_count, scan_count,
                         (unsigned long long)l_snap_root, (unsigned long long)l_snap_txn,
                         (unsigned long long)l_snap_count, (unsigned long long)l_tree_count);
            arg->inconsistencies++;
        }
        prev_scan_count = scan_count;
        arg->iterations++;
    }
    return NULL;
}

/**
 * @brief Test: concurrent writer + readers with snapshot consistency verification.
 *
 * Writer inserts 5000 records. Readers repeatedly open cursors and scan.
 * Each reader scan must produce:
 *   - Sorted keys (no ordering violations)
 *   - Non-decreasing count between scans (writer only inserts)
 */
static void test_btree_snapshot_concurrent(void)
{
    dap_test_msg("MVCC concurrent snapshot consistency test");

    s_cleanup_test_dir();
#ifdef DAP_OS_WINDOWS
    mkdir(TEST_DIR);
#else
    mkdir(TEST_DIR, 0755);
#endif

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/snapshot_concurrent.gdb", TEST_DIR);

    dap_global_db_t *tree = dap_global_db_create(filepath);
    dap_assert(tree != NULL, "Tree should be created for snapshot concurrent test");

    _Atomic int writer_done = 0;
    const int WRITE_COUNT = 5000;

    // Start reader threads
    pthread_t reader_tids[NUM_READER_THREADS];
    snapshot_reader_arg_t reader_args[NUM_READER_THREADS];
    for (int t = 0; t < NUM_READER_THREADS; t++) {
        reader_args[t].tree = tree;
        reader_args[t].thread_id = t;
        reader_args[t].writer_done = &writer_done;
        int rc = pthread_create(&reader_tids[t], NULL, s_snapshot_reader_thread, &reader_args[t]);
        dap_assert(rc == 0, "Snapshot reader thread should be created");
    }

    // Writer: insert records
    for (int i = 0; i < WRITE_COUNT; i++) {
        dap_global_db_key_t key = s_make_key(600, (uint64_t)i);
        char v[32]; snprintf(v, sizeof(v), "sc_%d", i);
        dap_global_db_insert(tree, &key, v, (uint32_t)strlen(v) + 1,
                                    v, (uint32_t)strlen(v) + 1, NULL, 0, 0);
    }
    atomic_store(&writer_done, 1);

    // Wait for readers
    for (int t = 0; t < NUM_READER_THREADS; t++)
        pthread_join(reader_tids[t], NULL);

    // Verify results
    int total_iterations = 0;
    int total_inconsistencies = 0;
    int total_ordering_errors = 0;
    for (int t = 0; t < NUM_READER_THREADS; t++) {
        dap_test_msg("Snapshot reader %d: %d scans, %d inconsistencies, %d ordering errors",
                     t, reader_args[t].iterations, reader_args[t].inconsistencies,
                     reader_args[t].ordering_errors);
        total_iterations += reader_args[t].iterations;
        total_inconsistencies += reader_args[t].inconsistencies;
        total_ordering_errors += reader_args[t].ordering_errors;
    }
    dap_test_msg("Total: %d scans across %d threads", total_iterations, NUM_READER_THREADS);
    dap_assert(total_iterations > 0, "Readers should have completed at least 1 scan");
    dap_assert(total_inconsistencies == 0,
               "No snapshot count inconsistencies (non-decreasing with insert-only writer)");
    dap_assert(total_ordering_errors == 0,
               "No key ordering errors in snapshot scans");

    // Final verify
    uint64_t l_count = 0;
    int l_rc = dap_global_db_verify(tree, &l_count);
    dap_assert(l_rc == 0, "Tree integrity after concurrent snapshot test should be OK");
    dap_assert(l_count == (uint64_t)WRITE_COUNT,
               "Tree should have all written records after concurrent test");

    dap_global_db_close(tree);
    dap_pass_msg("MVCC concurrent snapshot consistency");
}

// ============================================================================
// Phase 6.6: Cross-parent sibling boundary test
// ============================================================================

static void test_btree_cross_parent_cursor(void)
{
    dap_test_msg("Cross-parent sibling: insert/delete at branch boundaries + cursor scan");

    char path[256];
    snprintf(path, sizeof(path), "%s/cross_parent.gdb", TEST_DIR);
    dap_global_db_t *btree = dap_global_db_create(path);
    dap_assert(btree != NULL, "Tree should be created");

    const int N = 3000;
    char val[64];

    for (int i = N; i > 0; i--) {
        dap_global_db_key_t key = s_make_key(500, (uint64_t)i);
        snprintf(val, sizeof(val), "cross_%d", i);
        int rc = dap_global_db_insert(btree, &key, val, (uint32_t)strlen(val) + 1,
                                             val, (uint32_t)strlen(val) + 1, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Insert decreasing key should succeed");
    }
    dap_global_db_sync(btree);

    // Verify tree integrity
    uint64_t count = 0;
    int rc = dap_global_db_verify(btree, &count);
    dap_assert(rc == 0, "Tree should pass verify after decreasing inserts");
    dap_assert(count == (uint64_t)N, "Tree should have N entries");

    // Forward cursor scan: count must equal N
    dap_global_db_cursor_t *cursor = dap_global_db_cursor_create(btree);
    dap_assert(cursor != NULL, "Cursor should be created");
    rc = dap_global_db_cursor_move(cursor, DAP_GLOBAL_DB_FIRST, NULL);
    dap_assert(rc == 0, "Cursor should move to first");

    size_t fwd_count = 0;
    dap_global_db_key_t prev_key = {0};
    do {
        dap_global_db_key_t key;
        char *text_key;
        void *vdata, *sdata;
        uint32_t vlen, slen;
        uint8_t flags;
        rc = dap_global_db_cursor_get(cursor, &key, &text_key, &vdata, &vlen, &sdata, &slen, &flags);
        if (rc != 0) break;
        if (fwd_count > 0)
            dap_assert_PIF(memcmp(&prev_key, &key, sizeof(key)) < 0, "Keys must be strictly ascending");
        prev_key = key;
        fwd_count++;
        DAP_DELETE(text_key);
        DAP_DELETE(vdata);
        DAP_DELETE(sdata);
    } while (dap_global_db_cursor_move(cursor, DAP_GLOBAL_DB_NEXT, NULL) == 0);

    dap_assert(fwd_count == (size_t)N, "Forward scan must visit all N records");

    rc = dap_global_db_cursor_move(cursor, DAP_GLOBAL_DB_LAST, NULL);
    dap_assert(rc == 0, "Cursor should move to last");

    size_t rev_count = 0;
    memset(&prev_key, 0xff, sizeof(prev_key));
    do {
        dap_global_db_key_t key;
        char *text_key;
        void *vdata, *sdata;
        uint32_t vlen, slen;
        uint8_t flags;
        rc = dap_global_db_cursor_get(cursor, &key, &text_key, &vdata, &vlen, &sdata, &slen, &flags);
        if (rc != 0) break;
        if (rev_count > 0)
            dap_assert_PIF(memcmp(&prev_key, &key, sizeof(key)) > 0, "Keys must be strictly descending in reverse");
        prev_key = key;
        rev_count++;
        DAP_DELETE(text_key);
        DAP_DELETE(vdata);
        DAP_DELETE(sdata);
    } while (dap_global_db_cursor_move(cursor, DAP_GLOBAL_DB_PREV, NULL) == 0);

    dap_assert(rev_count == (size_t)N, "Reverse scan must visit all N records");
    dap_global_db_cursor_close(cursor);

    // Delete every 3rd entry (triggers merges across branch boundaries)
    int deleted = 0;
    for (int i = 1; i <= N; i += 3) {
        dap_global_db_key_t key = s_make_key(500, (uint64_t)i);
        rc = dap_global_db_delete(btree, &key);
        if (rc == 0) deleted++;
    }
    dap_global_db_sync(btree);

    // Verify after deletes
    count = 0;
    rc = dap_global_db_verify(btree, &count);
    dap_assert(rc == 0, "Tree should pass verify after cross-boundary deletes");
    dap_assert(count == (uint64_t)(N - deleted), "Entry count should match after deletes");

    for (int i = 1; i <= N; i += 3) {
        dap_global_db_key_t key = s_make_key(500, (uint64_t)i);
        snprintf(val, sizeof(val), "reinsert_%d", i);
        rc = dap_global_db_insert(btree, &key, val, (uint32_t)strlen(val) + 1,
                                         val, (uint32_t)strlen(val) + 1, NULL, 0, 0);
        dap_assert_PIF(rc == 0, "Re-insert should succeed");
    }
    dap_global_db_sync(btree);

    // Final verify + cursor scan
    count = 0;
    rc = dap_global_db_verify(btree, &count);
    dap_assert(rc == 0, "Tree should pass verify after re-inserts");
    dap_assert(count == (uint64_t)N, "Tree should have N entries after re-inserts");

    cursor = dap_global_db_cursor_create(btree);
    rc = dap_global_db_cursor_move(cursor, DAP_GLOBAL_DB_FIRST, NULL);
    dap_assert(rc == 0, "Final cursor should start");
    fwd_count = 0;
    memset(&prev_key, 0, sizeof(prev_key));
    do {
        dap_global_db_key_t key;
        char *text_key;
        void *vdata, *sdata;
        uint32_t vlen, slen;
        uint8_t flags;
        rc = dap_global_db_cursor_get(cursor, &key, &text_key, &vdata, &vlen, &sdata, &slen, &flags);
        if (rc != 0) break;
        if (fwd_count > 0)
            dap_assert_PIF(memcmp(&prev_key, &key, sizeof(key)) < 0, "Final scan keys must be ascending");
        prev_key = key;
        fwd_count++;
        DAP_DELETE(text_key);
        DAP_DELETE(vdata);
        DAP_DELETE(sdata);
    } while (dap_global_db_cursor_move(cursor, DAP_GLOBAL_DB_NEXT, NULL) == 0);

    dap_assert(fwd_count == (size_t)N, "Final forward scan must visit all N records");
    dap_global_db_cursor_close(cursor);

    dap_global_db_close(btree);
    dap_pass_msg("Cross-parent sibling boundary test");
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

    dap_test_msg("\n=== Edge-Case & Boundary Tests ===\n");
    test_btree_empty_tree();
    test_btree_single_entry();
    test_btree_update_overwrite();
    test_btree_clear();
    test_btree_cursor_seek();
    test_btree_cursor_reverse();
    test_btree_key_ordering();
    test_btree_overflow_benchmark_n2();
    test_btree_overflow_values();
    test_btree_large_values();
    test_btree_persistence_after_delete();
    test_btree_root_collapse();
    test_btree_insert_after_delete();
    test_btree_invariants();
    test_btree_stress();

    dap_test_msg("\n=== Thread Safety Tests (Phase 3) ===\n");
    test_btree_concurrent_reads();
    test_btree_concurrent_read_write();

    dap_test_msg("\n=== MVCC Snapshot Tests (Phase 5.6) ===\n");
    test_btree_snapshot_isolation();
    test_btree_snapshot_concurrent();

    dap_test_msg("\n=== COW Sibling Boundary Tests (Phase 6.6) ===\n");
    test_btree_cross_parent_cursor();

    s_cleanup_test_dir();
    
    dap_test_msg("\n=== All B-tree tests passed ===\n");
    return 0;
}
