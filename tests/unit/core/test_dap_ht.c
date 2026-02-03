/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform) the open source project
 *
 *    DAP is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 */

/**
 * @file test_dap_ht.c
 * @brief Unit tests for DAP Hash Table (dap_ht.h)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "dap_common.h"
#include "dap_ht.h"
#include "dap_test.h"

#define LOG_TAG "test_dap_ht"

// ============================================================================
// Test Structures
// ============================================================================

// Item with string key
typedef struct str_item {
    char *name;
    int value;
    dap_ht_handle_t hh;
} str_item_t;

// Item with integer key
typedef struct int_item {
    int id;
    char data[32];
    dap_ht_handle_t hh;
} int_item_t;

// Item with pointer key
typedef struct ptr_item {
    void *ptr;
    int value;
    dap_ht_handle_t hh;
} ptr_item_t;

static str_item_t *s_str_table = NULL;
static int_item_t *s_int_table = NULL;
static ptr_item_t *s_ptr_table = NULL;

// ============================================================================
// Helper Functions
// ============================================================================

static str_item_t *create_str_item(const char *name, int value) {
    str_item_t *item = DAP_NEW_Z(str_item_t);
    if (item) {
        item->name = strdup(name);
        item->value = value;
    }
    return item;
}

static void free_str_table(str_item_t **head) {
    str_item_t *el, *tmp;
    dap_ht_foreach(*head, el, tmp) {
        dap_ht_del(*head, el);
        free(el->name);
        DAP_DELETE(el);
    }
    *head = NULL;
}

static int_item_t *create_int_item(int id, const char *data) {
    int_item_t *item = DAP_NEW_Z(int_item_t);
    if (item) {
        item->id = id;
        snprintf(item->data, sizeof(item->data), "%s", data);
    }
    return item;
}

static void free_int_table(int_item_t **head) {
    int_item_t *el, *tmp;
    dap_ht_foreach(*head, el, tmp) {
        dap_ht_del(*head, el);
        DAP_DELETE(el);
    }
    *head = NULL;
}

// ============================================================================
// Tests: String Key Operations
// ============================================================================

static void test_ht_add_str(void) {
    dap_print_module_name("dap_ht_add_str");
    
    s_str_table = NULL;
    
    // Add first item
    str_item_t *item1 = create_str_item("alpha", 100);
    dap_ht_add_str(s_str_table, name, item1);
    dap_assert(s_str_table != NULL, "Table not NULL after add");
    dap_assert(dap_ht_count(s_str_table) == 1, "Count is 1 after first add");
    
    // Add more items
    str_item_t *item2 = create_str_item("beta", 200);
    str_item_t *item3 = create_str_item("gamma", 300);
    dap_ht_add_str(s_str_table, name, item2);
    dap_ht_add_str(s_str_table, name, item3);
    dap_assert(dap_ht_count(s_str_table) == 3, "Count is 3 after adding 3 items");
    
    free_str_table(&s_str_table);
    dap_pass_msg("dap_ht_add_str works correctly");
}

static void test_ht_find_str(void) {
    dap_print_module_name("dap_ht_find_str");
    
    s_str_table = NULL;
    
    // Setup
    const char *names[] = {"one", "two", "three", "four", "five"};
    for (int i = 0; i < 5; i++) {
        str_item_t *item = create_str_item(names[i], (i + 1) * 10);
        dap_ht_add_str(s_str_table, name, item);
    }
    
    // Find existing
    str_item_t *found = NULL;
    dap_ht_find_str(s_str_table, "three", found);
    dap_assert(found != NULL, "Found 'three'");
    dap_assert(found->value == 30, "Found item has correct value");
    
    // Find first
    found = NULL;
    dap_ht_find_str(s_str_table, "one", found);
    dap_assert(found != NULL && found->value == 10, "Found 'one' with value 10");
    
    // Find last
    found = NULL;
    dap_ht_find_str(s_str_table, "five", found);
    dap_assert(found != NULL && found->value == 50, "Found 'five' with value 50");
    
    // Find non-existent
    found = NULL;
    dap_ht_find_str(s_str_table, "nonexistent", found);
    dap_assert(found == NULL, "Non-existent key returns NULL");
    
    free_str_table(&s_str_table);
    dap_pass_msg("dap_ht_find_str works correctly");
}

static void test_ht_del(void) {
    dap_print_module_name("dap_ht_del");
    
    s_str_table = NULL;
    
    // Setup
    str_item_t *item1 = create_str_item("delete_me", 100);
    str_item_t *item2 = create_str_item("keep_me", 200);
    str_item_t *item3 = create_str_item("also_keep", 300);
    dap_ht_add_str(s_str_table, name, item1);
    dap_ht_add_str(s_str_table, name, item2);
    dap_ht_add_str(s_str_table, name, item3);
    dap_assert(dap_ht_count(s_str_table) == 3, "Initial count is 3");
    
    // Delete middle item
    dap_ht_del(s_str_table, item1);
    free(item1->name);
    DAP_DELETE(item1);
    dap_assert(dap_ht_count(s_str_table) == 2, "Count is 2 after delete");
    
    // Verify deleted item not found
    str_item_t *found = NULL;
    dap_ht_find_str(s_str_table, "delete_me", found);
    dap_assert(found == NULL, "Deleted item not found");
    
    // Verify other items still exist
    found = NULL;
    dap_ht_find_str(s_str_table, "keep_me", found);
    dap_assert(found != NULL, "Other items still exist");
    
    free_str_table(&s_str_table);
    dap_pass_msg("dap_ht_del works correctly");
}

static void test_ht_foreach(void) {
    dap_print_module_name("dap_ht_foreach");
    
    s_str_table = NULL;
    
    // Setup
    for (int i = 0; i < 10; i++) {
        char name[16];
        snprintf(name, sizeof(name), "item%d", i);
        str_item_t *item = create_str_item(name, i * 10);
        dap_ht_add_str(s_str_table, name, item);
    }
    
    // Iterate and count
    int count = 0;
    int sum = 0;
    str_item_t *el, *tmp;
    dap_ht_foreach(s_str_table, el, tmp) {
        count++;
        sum += el->value;
    }
    dap_assert(count == 10, "Foreach iterated 10 times");
    dap_assert(sum == 450, "Sum of values is correct (0+10+20+...+90 = 450)");
    
    free_str_table(&s_str_table);
    dap_pass_msg("dap_ht_foreach works correctly");
}

// ============================================================================
// Tests: Integer Key Operations
// ============================================================================

static void test_ht_int_key(void) {
    dap_print_module_name("dap_ht integer keys");
    
    s_int_table = NULL;
    
    // Add items with integer keys
    for (int i = 1; i <= 100; i++) {
        int_item_t *item = create_int_item(i, "data");
        dap_ht_add(s_int_table, id, item);
    }
    dap_assert(dap_ht_count(s_int_table) == 100, "Added 100 items");
    
    // Find by integer key
    int_item_t *found = NULL;
    int key = 50;
    dap_ht_find_int(s_int_table, key, found);
    dap_assert(found != NULL, "Found item with key 50");
    dap_assert(found->id == 50, "Found item has correct id");
    
    // Find non-existent
    key = 999;
    found = NULL;
    dap_ht_find_int(s_int_table, key, found);
    dap_assert(found == NULL, "Non-existent key returns NULL");
    
    free_int_table(&s_int_table);
    dap_pass_msg("Integer key operations work correctly");
}

// ============================================================================
// Tests: Stress/Performance
// ============================================================================

static void test_ht_many_items(void) {
    dap_print_module_name("dap_ht stress test");
    
    s_int_table = NULL;
    const int N = 10000;
    
    // Add many items
    for (int i = 0; i < N; i++) {
        int_item_t *item = create_int_item(i, "stress");
        dap_ht_add(s_int_table, id, item);
    }
    dap_assert(dap_ht_count(s_int_table) == (unsigned)N, "Added 10000 items");
    
    // Find random items
    for (int i = 0; i < 100; i++) {
        int key = rand() % N;
        int_item_t *found = NULL;
        dap_ht_find_int(s_int_table, key, found);
        dap_assert(found != NULL && found->id == key, "Random lookup works");
    }
    
    // Delete half
    int_item_t *el, *tmp;
    int deleted = 0;
    dap_ht_foreach(s_int_table, el, tmp) {
        if (el->id % 2 == 0) {
            dap_ht_del(s_int_table, el);
            DAP_DELETE(el);
            deleted++;
        }
    }
    dap_assert(dap_ht_count(s_int_table) == (unsigned)(N - deleted), "Count correct after deletions");
    
    // Verify odd items still findable
    for (int i = 1; i < N; i += 2) {
        int_item_t *found = NULL;
        dap_ht_find_int(s_int_table, i, found);
        dap_assert(found != NULL, "Odd items still findable");
    }
    
    free_int_table(&s_int_table);
    dap_pass_msg("Stress test with 10000 items passed");
}

// ============================================================================
// Tests: Edge Cases
// ============================================================================

static void test_ht_empty(void) {
    dap_print_module_name("dap_ht empty table");
    
    s_str_table = NULL;
    
    // Operations on empty table
    dap_assert(dap_ht_count(s_str_table) == 0, "Empty table count is 0");
    
    str_item_t *found = NULL;
    dap_ht_find_str(s_str_table, "anything", found);
    dap_assert(found == NULL, "Find in empty table returns NULL");
    
    // Foreach on empty should not crash
    str_item_t *el, *tmp;
    int count = 0;
    dap_ht_foreach(s_str_table, el, tmp) {
        count++;
    }
    dap_assert(count == 0, "Foreach on empty iterates 0 times");
    
    dap_pass_msg("Empty table operations work correctly");
}

static void test_ht_single(void) {
    dap_print_module_name("dap_ht single item");
    
    s_str_table = NULL;
    
    // Single item
    str_item_t *item = create_str_item("only_one", 42);
    dap_ht_add_str(s_str_table, name, item);
    dap_assert(dap_ht_count(s_str_table) == 1, "Count is 1");
    
    str_item_t *found = NULL;
    dap_ht_find_str(s_str_table, "only_one", found);
    dap_assert(found == item, "Found the single item");
    
    // Delete single item
    dap_ht_del(s_str_table, item);
    free(item->name);
    DAP_DELETE(item);
    
    // Table should be NULL or empty now
    dap_assert(s_str_table == NULL || dap_ht_count(s_str_table) == 0, "Table empty after deleting single item");
    
    dap_pass_msg("Single item operations work correctly");
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    
    dap_set_appname("test_dap_ht");
    
    printf("\n=== DAP Hash Table (dap_ht) Tests ===\n\n");
    
    // String key tests
    test_ht_add_str();
    test_ht_find_str();
    test_ht_del();
    test_ht_foreach();
    
    // Integer key tests
    test_ht_int_key();
    
    // Stress tests
    test_ht_many_items();
    
    // Edge cases
    test_ht_empty();
    test_ht_single();
    
    printf("\n=== All dap_ht tests passed! ===\n\n");
    
    return 0;
}
