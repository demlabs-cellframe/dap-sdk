/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * This file is part of DAP SDK the open source project
 *
 *    DAP SDK is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "dap_ring_buffer.h"
#include "dap_common.h"
#include "dap_test.h"

#define LOG_TAG "test_dap_ring_buffer"

static int s_dummy_values[256];

static void test_create_delete(void)
{
    dap_test_msg("test_create_delete");

    // Normal creation
    dap_ring_buffer_t *l_rb = dap_ring_buffer_create(16);
    dap_assert_PIF(l_rb != NULL, "Create ring buffer (16)");
    dap_assert_PIF(l_rb->capacity == 16, "Capacity is 16 (power of 2)");
    dap_assert_PIF(l_rb->mask == 15, "Mask is 15");
    dap_ring_buffer_delete(l_rb);

    // Non-power-of-2: must be rounded up
    l_rb = dap_ring_buffer_create(10);
    dap_assert_PIF(l_rb != NULL, "Create ring buffer (10)");
    dap_assert_PIF(l_rb->capacity == 16, "Capacity rounded up to 16");
    dap_ring_buffer_delete(l_rb);

    // Minimum capacity
    l_rb = dap_ring_buffer_create(1);
    dap_assert_PIF(l_rb != NULL, "Create ring buffer (1)");
    dap_assert_PIF(l_rb->capacity >= 1, "Capacity >= 1");
    dap_ring_buffer_delete(l_rb);

    // Zero capacity -> NULL
    l_rb = dap_ring_buffer_create(0);
    dap_assert_PIF(l_rb == NULL, "Zero capacity returns NULL");

    // Delete NULL is safe
    dap_ring_buffer_delete(NULL);
}

static void test_push_pop_single(void)
{
    dap_test_msg("test_push_pop_single");

    dap_ring_buffer_t *l_rb = dap_ring_buffer_create(8);
    dap_assert_PIF(l_rb != NULL, "Create");

    int l_val = 42;
    bool l_ok = dap_ring_buffer_push(l_rb, &l_val);
    dap_assert_PIF(l_ok, "Push single element");
    dap_assert_PIF(!dap_ring_buffer_is_empty(l_rb), "Not empty after push");
    dap_assert_PIF(dap_ring_buffer_size(l_rb) == 1, "Size is 1");

    void *l_popped = dap_ring_buffer_pop(l_rb);
    dap_assert_PIF(l_popped == &l_val, "Popped value matches pushed");
    dap_assert_PIF(dap_ring_buffer_is_empty(l_rb), "Empty after pop");
    dap_assert_PIF(dap_ring_buffer_size(l_rb) == 0, "Size is 0");

    dap_ring_buffer_delete(l_rb);
}

static void test_push_pop_multiple(void)
{
    dap_test_msg("test_push_pop_multiple");

    dap_ring_buffer_t *l_rb = dap_ring_buffer_create(16);
    dap_assert_PIF(l_rb != NULL, "Create");

    const size_t l_count = 10;
    for(size_t i = 0; i < l_count; i++)
    {
        s_dummy_values[i] = (int)i;
        bool l_ok = dap_ring_buffer_push(l_rb, &s_dummy_values[i]);
        dap_assert_PIF(l_ok, "Push element");
    }

    dap_assert_PIF(dap_ring_buffer_size(l_rb) == l_count, "Size matches count");

    // FIFO order
    for(size_t i = 0; i < l_count; i++)
    {
        void *l_p = dap_ring_buffer_pop(l_rb);
        dap_assert_PIF(l_p == &s_dummy_values[i], "FIFO order preserved");
    }

    dap_assert_PIF(dap_ring_buffer_is_empty(l_rb), "Empty after popping all");

    dap_ring_buffer_delete(l_rb);
}

static void test_empty_pop(void)
{
    dap_test_msg("test_empty_pop");

    dap_ring_buffer_t *l_rb = dap_ring_buffer_create(4);
    dap_assert_PIF(l_rb != NULL, "Create");

    void *l_val = dap_ring_buffer_pop(l_rb);
    dap_assert_PIF(l_val == NULL, "Pop from empty returns NULL");
    dap_assert_PIF(dap_ring_buffer_is_empty(l_rb), "Still empty");

    // Pop from NULL ring buffer
    l_val = dap_ring_buffer_pop(NULL);
    dap_assert_PIF(l_val == NULL, "Pop from NULL returns NULL");

    dap_ring_buffer_delete(l_rb);
}

static void test_full_buffer(void)
{
    dap_test_msg("test_full_buffer");

    // Capacity 4 means usable slots = capacity - 1 = 3 (one sentinel slot)
    dap_ring_buffer_t *l_rb = dap_ring_buffer_create(4);
    dap_assert_PIF(l_rb != NULL, "Create");
    dap_assert_PIF(l_rb->capacity == 4, "Capacity is 4");

    // Fill up
    size_t l_pushed = 0;
    for(size_t i = 0; i < 4; i++)
    {
        s_dummy_values[i] = (int)(i + 100);
        if(dap_ring_buffer_push(l_rb, &s_dummy_values[i]))
            l_pushed++;
    }

    // With mask-based ring buffer, usable capacity is capacity - 1
    dap_assert_PIF(l_pushed == 3, "Pushed 3 elements (cap-1)");
    dap_assert_PIF(dap_ring_buffer_is_full(l_rb), "Buffer is full");

    // Extra push must fail
    int l_extra = 999;
    bool l_ok = dap_ring_buffer_push(l_rb, &l_extra);
    dap_assert_PIF(!l_ok, "Push to full buffer fails");

    // Pop one and push again
    void *l_p = dap_ring_buffer_pop(l_rb);
    dap_assert_PIF(l_p == &s_dummy_values[0], "Pop oldest");
    dap_assert_PIF(!dap_ring_buffer_is_full(l_rb), "No longer full");

    l_ok = dap_ring_buffer_push(l_rb, &l_extra);
    dap_assert_PIF(l_ok, "Push after pop succeeds");

    dap_ring_buffer_delete(l_rb);
}

static void test_null_push(void)
{
    dap_test_msg("test_null_push");

    dap_ring_buffer_t *l_rb = dap_ring_buffer_create(4);
    dap_assert_PIF(l_rb != NULL, "Create");

    // NULL pointer push must fail
    bool l_ok = dap_ring_buffer_push(l_rb, NULL);
    dap_assert_PIF(!l_ok, "Push NULL fails");
    dap_assert_PIF(dap_ring_buffer_is_empty(l_rb), "Still empty");

    // Push to NULL ring buffer
    int l_val = 1;
    l_ok = dap_ring_buffer_push(NULL, &l_val);
    dap_assert_PIF(!l_ok, "Push to NULL rb fails");

    dap_ring_buffer_delete(l_rb);
}

static void test_wrap_around(void)
{
    dap_test_msg("test_wrap_around");

    dap_ring_buffer_t *l_rb = dap_ring_buffer_create(4);
    dap_assert_PIF(l_rb != NULL, "Create");

    // Push 3, pop 3, push 3 again — forces wrap-around
    for(int round = 0; round < 5; round++)
    {
        for(size_t i = 0; i < 3; i++)
        {
            s_dummy_values[i] = round * 10 + (int)i;
            bool l_ok = dap_ring_buffer_push(l_rb, &s_dummy_values[i]);
            dap_assert_PIF(l_ok, "Push wrap-around");
        }
        for(size_t i = 0; i < 3; i++)
        {
            void *l_p = dap_ring_buffer_pop(l_rb);
            dap_assert_PIF(l_p == &s_dummy_values[i], "Pop wrap-around");
        }
    }
    dap_assert_PIF(dap_ring_buffer_is_empty(l_rb), "Empty after wrap-around");

    dap_ring_buffer_delete(l_rb);
}

static void test_stats(void)
{
    dap_test_msg("test_stats");

    dap_ring_buffer_t *l_rb = dap_ring_buffer_create(4);
    dap_assert_PIF(l_rb != NULL, "Create");

    int l_a = 1, l_b = 2, l_c = 3, l_extra = 4;

    dap_ring_buffer_push(l_rb, &l_a);
    dap_ring_buffer_push(l_rb, &l_b);
    dap_ring_buffer_push(l_rb, &l_c);
    dap_ring_buffer_push(l_rb, &l_extra); // should fail (full)

    dap_ring_buffer_pop(l_rb);
    dap_ring_buffer_pop(l_rb);
    dap_ring_buffer_pop(l_rb);
    dap_ring_buffer_pop(l_rb); // should fail (empty)

    uint64_t l_pushes = 0, l_pops = 0, l_full = 0, l_empty = 0;
    dap_ring_buffer_get_stats(l_rb, &l_pushes, &l_pops, &l_full, &l_empty);

    dap_assert_PIF(l_pushes == 3, "Total pushes == 3");
    dap_assert_PIF(l_pops == 3, "Total pops == 3");
    dap_assert_PIF(l_full == 1, "Total full == 1");
    dap_assert_PIF(l_empty == 1, "Total empty == 1");

    // Reset stats
    dap_ring_buffer_reset_stats(l_rb);
    dap_ring_buffer_get_stats(l_rb, &l_pushes, &l_pops, &l_full, &l_empty);
    dap_assert_PIF(l_pushes == 0, "Pushes reset to 0");
    dap_assert_PIF(l_pops == 0, "Pops reset to 0");

    // NULL stats pointers are safe
    dap_ring_buffer_get_stats(l_rb, NULL, NULL, NULL, NULL);
    dap_ring_buffer_get_stats(NULL, &l_pushes, &l_pops, NULL, NULL);
    dap_ring_buffer_reset_stats(NULL);

    dap_ring_buffer_delete(l_rb);
}

static void test_is_empty_is_full(void)
{
    dap_test_msg("test_is_empty_is_full");

    dap_ring_buffer_t *l_rb = dap_ring_buffer_create(4);
    dap_assert_PIF(l_rb != NULL, "Create");

    dap_assert_PIF(dap_ring_buffer_is_empty(l_rb), "Initially empty");
    dap_assert_PIF(!dap_ring_buffer_is_full(l_rb), "Initially not full");
    dap_assert_PIF(dap_ring_buffer_size(l_rb) == 0, "Initial size 0");

    int l_vals[3] = {10, 20, 30};
    dap_ring_buffer_push(l_rb, &l_vals[0]);

    dap_assert_PIF(!dap_ring_buffer_is_empty(l_rb), "Not empty after 1 push");
    dap_assert_PIF(!dap_ring_buffer_is_full(l_rb), "Not full after 1 push");
    dap_assert_PIF(dap_ring_buffer_size(l_rb) == 1, "Size 1");

    dap_ring_buffer_push(l_rb, &l_vals[1]);
    dap_ring_buffer_push(l_rb, &l_vals[2]);

    dap_assert_PIF(dap_ring_buffer_is_full(l_rb), "Full after 3 pushes (cap=4, usable=3)");
    dap_assert_PIF(!dap_ring_buffer_is_empty(l_rb), "Not empty when full");
    dap_assert_PIF(dap_ring_buffer_size(l_rb) == 3, "Size 3");

    dap_ring_buffer_delete(l_rb);
}

int main(void)
{
    dap_common_init("test_dap_ring_buffer", NULL);
    dap_log_level_set(L_DEBUG);

    dap_print_module_name("dap_ring_buffer");

    test_create_delete();
    test_push_pop_single();
    test_push_pop_multiple();
    test_empty_pop();
    test_full_buffer();
    test_null_push();
    test_wrap_around();
    test_stats();
    test_is_empty_is_full();

    log_it(L_INFO, "All dap_ring_buffer tests passed");

    dap_common_deinit();
    return 0;
}
