/**
 * @file dap_map.h
 * @brief DAP Map — string-keyed ordered map (left-leaning red-black tree)
 *
 * Compact ordered map optimized for small collections (tens of elements).
 * Uses a left-leaning red-black tree — zero pre-allocation overhead,
 * O(log N) lookup/insert/delete, naturally sorted iteration.
 *
 * Keys are NUL-terminated strings (copied on insert).
 * Values are opaque void* pointers — caller owns the memory.
 *
 * Usage:
 *   dap_map_t l_map = {0};
 *
 *   dap_map_put(&l_map, "CELL", l_price_ptr);
 *   void *l_found = dap_map_get(&l_map, "CELL");
 *
 *   dap_map_iter_t l_it;
 *   for (dap_map_begin(&l_map, &l_it); dap_map_iter_valid(&l_it); dap_map_iter_next(&l_it))
 *       printf("%s -> %p\n", l_it.key, l_it.val);
 *
 *   dap_map_purge(&l_map);               // free nodes only, NOT values
 *   dap_map_purge_all(&l_map);           // free nodes AND values via DAP_DELETE
 *   dap_map_purge_cb(&l_map, my_free);   // free nodes AND values via callback
 *
 * Copyright (c) 2026 Demlabs
 * License: GNU GPL v3
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dap_map_node {
    char               *key;
    void               *val;
    int                 color;  /* 0 = black, 1 = red */
    struct dap_map_node *left;
    struct dap_map_node *right;
    struct dap_map_node *parent;
} dap_map_node_t;

typedef struct dap_map {
    dap_map_node_t *root;
    unsigned        count;
} dap_map_t;

typedef struct dap_map_iter {
    dap_map_node_t *node;
    const char     *key;
    void           *val;
} dap_map_iter_t;

void  *dap_map_put       (dap_map_t *a_map, const char *a_key, void *a_val);
void  *dap_map_get       (const dap_map_t *a_map, const char *a_key);
bool   dap_map_has       (const dap_map_t *a_map, const char *a_key);
void  *dap_map_remove    (dap_map_t *a_map, const char *a_key);
unsigned dap_map_count   (const dap_map_t *a_map);

void   dap_map_begin     (const dap_map_t *a_map, dap_map_iter_t *a_it);
bool   dap_map_iter_valid(const dap_map_iter_t *a_it);
void   dap_map_iter_next (dap_map_iter_t *a_it);

void   dap_map_walk      (const dap_map_t *a_map,
                           void (*a_cb)(const char *a_key, void *a_val, void *a_arg),
                           void *a_arg);

void   dap_map_purge     (dap_map_t *a_map);
void   dap_map_purge_all (dap_map_t *a_map);
void   dap_map_purge_cb  (dap_map_t *a_map, void (*a_free_val)(void *));

#ifdef __cplusplus
}
#endif
