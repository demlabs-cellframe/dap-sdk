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
 * @file test_dap_dl.c
 * @brief Unit tests for DAP Doubly-Linked List (dap_dl.h)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "dap_common.h"
#include "dap_dl.h"
#include "dap_test.h"

#define LOG_TAG "test_dap_dl"

// ============================================================================
// Test Structure
// ============================================================================

typedef struct test_item {
    int id;
    char name[32];
    struct test_item *prev, *next;
} test_item_t;

static test_item_t *s_list = NULL;

// ============================================================================
// Helper Functions
// ============================================================================

static test_item_t *create_item(int id, const char *name) {
    test_item_t *item = DAP_NEW_Z(test_item_t);
    if (item) {
        item->id = id;
        snprintf(item->name, sizeof(item->name), "%s", name);
    }
    return item;
}

static void free_list(test_item_t **head) {
    test_item_t *el, *tmp;
    dap_dl_foreach_safe(*head, el, tmp) {
        dap_dl_delete(*head, el);
        DAP_DELETE(el);
    }
    *head = NULL;
}

// ============================================================================
// Tests: Basic Operations
// ============================================================================

static void test_dl_append(void) {
    dap_print_module_name("dap_dl_append");
    
    s_list = NULL;
    
    // Append to empty list
    test_item_t *item1 = create_item(1, "first");
    dap_dl_append(s_list, item1);
    dap_assert(s_list == item1, "Append to empty list sets head");
    dap_assert(s_list->prev == item1, "Single item prev points to itself (tail)");
    dap_assert(s_list->next == NULL, "Single item next is NULL");
    
    // Append second item
    test_item_t *item2 = create_item(2, "second");
    dap_dl_append(s_list, item2);
    dap_assert(s_list == item1, "Head unchanged after append");
    dap_assert(item1->next == item2, "First->next points to second");
    dap_assert(item2->prev == item1, "Second->prev points to first");
    dap_assert(s_list->prev == item2, "Head->prev is tail (second)");
    
    // Append third item
    test_item_t *item3 = create_item(3, "third");
    dap_dl_append(s_list, item3);
    dap_assert(item2->next == item3, "Second->next points to third");
    dap_assert(item3->prev == item2, "Third->prev points to second");
    dap_assert(s_list->prev == item3, "Head->prev is new tail (third)");
    
    free_list(&s_list);
    dap_pass_msg("dap_dl_append works correctly");
}

static void test_dl_prepend(void) {
    dap_print_module_name("dap_dl_prepend");
    
    s_list = NULL;
    
    // Prepend to empty list
    test_item_t *item1 = create_item(1, "first");
    dap_dl_prepend(s_list, item1);
    dap_assert(s_list == item1, "Prepend to empty list sets head");
    
    // Prepend second (becomes new head)
    test_item_t *item2 = create_item(2, "second");
    dap_dl_prepend(s_list, item2);
    dap_assert(s_list == item2, "Prepend changes head");
    dap_assert(item2->next == item1, "New head->next points to old head");
    dap_assert(item1->prev == item2, "Old head->prev points to new head");
    
    free_list(&s_list);
    dap_pass_msg("dap_dl_prepend works correctly");
}

static void test_dl_delete(void) {
    dap_print_module_name("dap_dl_delete");
    
    s_list = NULL;
    
    // Setup: create list of 3 items
    test_item_t *item1 = create_item(1, "first");
    test_item_t *item2 = create_item(2, "second");
    test_item_t *item3 = create_item(3, "third");
    dap_dl_append(s_list, item1);
    dap_dl_append(s_list, item2);
    dap_dl_append(s_list, item3);
    
    // Delete middle
    dap_dl_delete(s_list, item2);
    dap_assert(item1->next == item3, "After delete middle: first->next is third");
    dap_assert(item3->prev == item1, "After delete middle: third->prev is first");
    DAP_DELETE(item2);
    
    // Delete tail
    dap_dl_delete(s_list, item3);
    dap_assert(item1->next == NULL, "After delete tail: first->next is NULL");
    dap_assert(s_list->prev == item1, "After delete tail: head->prev is first (itself)");
    DAP_DELETE(item3);
    
    // Delete head (last item)
    dap_dl_delete(s_list, item1);
    dap_assert(s_list == NULL, "After delete last: list is NULL");
    DAP_DELETE(item1);
    
    dap_pass_msg("dap_dl_delete works correctly");
}

static void test_dl_foreach(void) {
    dap_print_module_name("dap_dl_foreach");
    
    s_list = NULL;
    
    // Setup
    for (int i = 1; i <= 5; i++) {
        char name[16];
        snprintf(name, sizeof(name), "item%d", i);
        test_item_t *item = create_item(i, name);
        dap_dl_append(s_list, item);
    }
    
    // Count with foreach
    int count = 0;
    test_item_t *el;
    dap_dl_foreach(s_list, el) {
        count++;
        dap_assert(el->id == count, "Element id matches iteration order");
    }
    dap_assert(count == 5, "Foreach iterated over all 5 elements");
    
    // Count with dap_dl_count
    size_t counted = 0;
    dap_dl_count(s_list, counted);
    dap_assert(counted == 5, "dap_dl_count returns 5");
    
    free_list(&s_list);
    dap_pass_msg("dap_dl_foreach and dap_dl_count work correctly");
}

static void test_dl_foreach_safe(void) {
    dap_print_module_name("dap_dl_foreach_safe");
    
    s_list = NULL;
    
    // Setup
    for (int i = 1; i <= 5; i++) {
        test_item_t *item = create_item(i, "item");
        dap_dl_append(s_list, item);
    }
    
    // Delete even items during iteration
    test_item_t *el, *tmp;
    dap_dl_foreach_safe(s_list, el, tmp) {
        if (el->id % 2 == 0) {
            dap_dl_delete(s_list, el);
            DAP_DELETE(el);
        }
    }
    
    // Verify only odd remain
    size_t count = 0;
    dap_dl_count(s_list, count);
    dap_assert(count == 3, "After deleting evens: 3 items remain");
    
    int expected_id = 1;
    dap_dl_foreach(s_list, el) {
        dap_assert(el->id == expected_id, "Remaining items are odd");
        expected_id += 2;
    }
    
    free_list(&s_list);
    dap_pass_msg("dap_dl_foreach_safe works correctly for deletion during iteration");
}

static void test_dl_search(void) {
    dap_print_module_name("dap_dl_search");
    
    s_list = NULL;
    
    // Setup
    for (int i = 1; i <= 5; i++) {
        test_item_t *item = create_item(i * 10, "item");
        dap_dl_append(s_list, item);
    }
    
    // Search by scalar
    test_item_t *found = NULL;
    dap_dl_search_scalar(s_list, found, id, 30);
    dap_assert(found != NULL, "Found item with id=30");
    dap_assert(found->id == 30, "Found item has correct id");
    
    // Search non-existent
    found = NULL;
    dap_dl_search_scalar(s_list, found, id, 999);
    dap_assert(found == NULL, "Search for non-existent returns NULL");
    
    free_list(&s_list);
    dap_pass_msg("dap_dl_search works correctly");
}

static int compare_by_id(test_item_t *a, test_item_t *b) {
    return a->id - b->id;
}

static void test_dl_sort(void) {
    dap_print_module_name("dap_dl_sort");
    
    s_list = NULL;
    
    // Add items in reverse order
    for (int i = 5; i >= 1; i--) {
        test_item_t *item = create_item(i, "item");
        dap_dl_append(s_list, item);
    }
    
    // Verify unsorted
    dap_assert(s_list->id == 5, "Before sort: first id is 5");
    
    // Sort
    dap_dl_sort(s_list, compare_by_id);
    
    // Verify sorted
    dap_assert(s_list->id == 1, "After sort: first id is 1");
    
    int prev_id = 0;
    test_item_t *el;
    dap_dl_foreach(s_list, el) {
        dap_assert(el->id > prev_id, "Items are in ascending order");
        prev_id = el->id;
    }
    
    free_list(&s_list);
    dap_pass_msg("dap_dl_sort works correctly");
}

static void test_dl_insert_inorder(void) {
    dap_print_module_name("dap_dl_insert_inorder");
    
    s_list = NULL;
    
    // Insert in random order, should maintain sorted
    int ids[] = {30, 10, 50, 20, 40};
    for (int i = 0; i < 5; i++) {
        test_item_t *item = create_item(ids[i], "item");
        dap_dl_insert_inorder(s_list, item, compare_by_id);
    }
    
    // Verify sorted
    int expected[] = {10, 20, 30, 40, 50};
    int idx = 0;
    test_item_t *el;
    dap_dl_foreach(s_list, el) {
        dap_assert(el->id == expected[idx], "Insert inorder maintains sort");
        idx++;
    }
    
    free_list(&s_list);
    dap_pass_msg("dap_dl_insert_inorder works correctly");
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    
    dap_set_appname("test_dap_dl");
    
    printf("\n=== DAP Doubly-Linked List (dap_dl) Tests ===\n\n");
    
    test_dl_append();
    test_dl_prepend();
    test_dl_delete();
    test_dl_foreach();
    test_dl_foreach_safe();
    test_dl_search();
    test_dl_sort();
    test_dl_insert_inorder();
    
    printf("\n=== All dap_dl tests passed! ===\n\n");
    
    return 0;
}
