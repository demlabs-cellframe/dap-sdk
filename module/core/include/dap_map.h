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
#include <string.h>
#include "dap_common.h"
#include "dap_strfuncs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Node / tree types
 * ================================================================ */

typedef enum { DAP_MAP_BLACK = 0, DAP_MAP_RED = 1 } dap_map_color_t;

typedef struct dap_map_node {
    char               *key;
    void               *val;
    dap_map_color_t     color;
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

/* ================================================================
 * Internal RB-tree helpers
 * ================================================================ */

DAP_STATIC_INLINE bool s_dap_map_is_red(const dap_map_node_t *a_node)
{
    return a_node && a_node->color == DAP_MAP_RED;
}

DAP_STATIC_INLINE dap_map_node_t *s_dap_map_rotate_left(dap_map_node_t *a_node)
{
    dap_map_node_t *l_pivot = a_node->right;
    a_node->right = l_pivot->left;
    if (l_pivot->left) l_pivot->left->parent = a_node;
    l_pivot->left   = a_node;
    l_pivot->color  = a_node->color;
    a_node->color   = DAP_MAP_RED;
    l_pivot->parent = a_node->parent;
    a_node->parent  = l_pivot;
    return l_pivot;
}

DAP_STATIC_INLINE dap_map_node_t *s_dap_map_rotate_right(dap_map_node_t *a_node)
{
    dap_map_node_t *l_pivot = a_node->left;
    a_node->left = l_pivot->right;
    if (l_pivot->right) l_pivot->right->parent = a_node;
    l_pivot->right  = a_node;
    l_pivot->color  = a_node->color;
    a_node->color   = DAP_MAP_RED;
    l_pivot->parent = a_node->parent;
    a_node->parent  = l_pivot;
    return l_pivot;
}

DAP_STATIC_INLINE void s_dap_map_flip_colors(dap_map_node_t *a_node)
{
    a_node->color = (a_node->color == DAP_MAP_RED) ? DAP_MAP_BLACK : DAP_MAP_RED;
    if (a_node->left)
        a_node->left->color  = (a_node->left->color  == DAP_MAP_RED) ? DAP_MAP_BLACK : DAP_MAP_RED;
    if (a_node->right)
        a_node->right->color = (a_node->right->color == DAP_MAP_RED) ? DAP_MAP_BLACK : DAP_MAP_RED;
}

DAP_STATIC_INLINE dap_map_node_t *s_dap_map_fixup(dap_map_node_t *a_node)
{
    if (s_dap_map_is_red(a_node->right) && !s_dap_map_is_red(a_node->left))
        a_node = s_dap_map_rotate_left(a_node);
    if (s_dap_map_is_red(a_node->left) && s_dap_map_is_red(a_node->left->left))
        a_node = s_dap_map_rotate_right(a_node);
    if (s_dap_map_is_red(a_node->left) && s_dap_map_is_red(a_node->right))
        s_dap_map_flip_colors(a_node);
    return a_node;
}

DAP_STATIC_INLINE dap_map_node_t *s_dap_map_new_node(const char *a_key, void *a_val,
                                                       dap_map_node_t *a_parent)
{
    dap_map_node_t *l_node = DAP_NEW_Z(dap_map_node_t);
    dap_return_val_if_fail(l_node, NULL);
    l_node->key    = dap_strdup(a_key);
    l_node->val    = a_val;
    l_node->color  = DAP_MAP_RED;
    l_node->parent = a_parent;
    return l_node;
}

DAP_STATIC_INLINE void s_dap_map_free_node(dap_map_node_t *a_node)
{
    DAP_DELETE(a_node->key);
    DAP_DELETE(a_node);
}

DAP_STATIC_INLINE dap_map_node_t *s_dap_map_insert(dap_map_node_t *a_node, const char *a_key,
                                                     void *a_val, dap_map_node_t *a_parent,
                                                     bool *a_replaced, unsigned *a_count)
{
    if (!a_node) {
        (*a_count)++;
        return s_dap_map_new_node(a_key, a_val, a_parent);
    }
    int l_cmp = strcmp(a_key, a_node->key);
    if (l_cmp < 0)
        a_node->left  = s_dap_map_insert(a_node->left,  a_key, a_val, a_node, a_replaced, a_count);
    else if (l_cmp > 0)
        a_node->right = s_dap_map_insert(a_node->right, a_key, a_val, a_node, a_replaced, a_count);
    else {
        a_node->val = a_val;
        *a_replaced = true;
    }
    return s_dap_map_fixup(a_node);
}

DAP_STATIC_INLINE dap_map_node_t *s_dap_map_min(dap_map_node_t *a_node)
{
    while (a_node && a_node->left)
        a_node = a_node->left;
    return a_node;
}

DAP_STATIC_INLINE dap_map_node_t *s_dap_map_move_red_left(dap_map_node_t *a_node)
{
    s_dap_map_flip_colors(a_node);
    if (a_node->right && s_dap_map_is_red(a_node->right->left)) {
        a_node->right = s_dap_map_rotate_right(a_node->right);
        if (a_node->right) a_node->right->parent = a_node;
        a_node = s_dap_map_rotate_left(a_node);
        s_dap_map_flip_colors(a_node);
    }
    return a_node;
}

DAP_STATIC_INLINE dap_map_node_t *s_dap_map_move_red_right(dap_map_node_t *a_node)
{
    s_dap_map_flip_colors(a_node);
    if (a_node->left && s_dap_map_is_red(a_node->left->left)) {
        a_node = s_dap_map_rotate_right(a_node);
        s_dap_map_flip_colors(a_node);
    }
    return a_node;
}

DAP_STATIC_INLINE dap_map_node_t *s_dap_map_delete_min(dap_map_node_t *a_node)
{
    if (!a_node->left) {
        s_dap_map_free_node(a_node);
        return NULL;
    }
    if (!s_dap_map_is_red(a_node->left) && !s_dap_map_is_red(a_node->left->left))
        a_node = s_dap_map_move_red_left(a_node);
    a_node->left = s_dap_map_delete_min(a_node->left);
    if (a_node->left) a_node->left->parent = a_node;
    return s_dap_map_fixup(a_node);
}

DAP_STATIC_INLINE dap_map_node_t *s_dap_map_delete(dap_map_node_t *a_node, const char *a_key,
                                                     void **a_out_val, unsigned *a_count)
{
    if (!a_node) return NULL;

    if (strcmp(a_key, a_node->key) < 0) {
        if (a_node->left && !s_dap_map_is_red(a_node->left) && !s_dap_map_is_red(a_node->left->left))
            a_node = s_dap_map_move_red_left(a_node);
        a_node->left = s_dap_map_delete(a_node->left, a_key, a_out_val, a_count);
        if (a_node->left) a_node->left->parent = a_node;
    } else {
        if (s_dap_map_is_red(a_node->left))
            a_node = s_dap_map_rotate_right(a_node);

        if (strcmp(a_key, a_node->key) == 0 && !a_node->right) {
            if (a_out_val) *a_out_val = a_node->val;
            (*a_count)--;
            s_dap_map_free_node(a_node);
            return NULL;
        }

        if (a_node->right && !s_dap_map_is_red(a_node->right) && !s_dap_map_is_red(a_node->right->left))
            a_node = s_dap_map_move_red_right(a_node);

        if (strcmp(a_key, a_node->key) == 0) {
            if (a_out_val) *a_out_val = a_node->val;
            dap_map_node_t *l_min = s_dap_map_min(a_node->right);
            DAP_DELETE(a_node->key);
            a_node->key = dap_strdup(l_min->key);
            a_node->val = l_min->val;
            a_node->right = s_dap_map_delete_min(a_node->right);
            if (a_node->right) a_node->right->parent = a_node;
            (*a_count)--;
        } else {
            a_node->right = s_dap_map_delete(a_node->right, a_key, a_out_val, a_count);
            if (a_node->right) a_node->right->parent = a_node;
        }
    }
    return s_dap_map_fixup(a_node);
}

DAP_STATIC_INLINE void s_dap_map_purge(dap_map_node_t *a_node, void (*a_free_val)(void *))
{
    if (!a_node) return;
    s_dap_map_purge(a_node->left, a_free_val);
    s_dap_map_purge(a_node->right, a_free_val);
    if (a_free_val && a_node->val)
        a_free_val(a_node->val);
    s_dap_map_free_node(a_node);
}

DAP_STATIC_INLINE void s_dap_map_purge_delete(dap_map_node_t *a_node)
{
    if (!a_node) return;
    s_dap_map_purge_delete(a_node->left);
    s_dap_map_purge_delete(a_node->right);
    if (a_node->val) DAP_DELETE(a_node->val);
    s_dap_map_free_node(a_node);
}

/* ================================================================
 * Public API
 * ================================================================ */

/**
 * @brief Insert or replace a key-value pair.
 * @return previous value if key existed, NULL otherwise
 */
DAP_STATIC_INLINE void *dap_map_put(dap_map_t *a_map, const char *a_key, void *a_val)
{
    bool l_replaced = false;
    a_map->root = s_dap_map_insert(a_map->root, a_key, a_val, NULL, &l_replaced, &a_map->count);
    a_map->root->color  = DAP_MAP_BLACK;
    a_map->root->parent = NULL;
    return l_replaced ? a_val : NULL;
}

/**
 * @brief O(log N) lookup by key.
 */
DAP_STATIC_INLINE void *dap_map_get(const dap_map_t *a_map, const char *a_key)
{
    dap_map_node_t *l_node = a_map ? a_map->root : NULL;
    while (l_node) {
        int l_cmp = strcmp(a_key, l_node->key);
        if (l_cmp < 0)       l_node = l_node->left;
        else if (l_cmp > 0)  l_node = l_node->right;
        else                  return l_node->val;
    }
    return NULL;
}

/**
 * @brief Check if key exists.
 */
DAP_STATIC_INLINE bool dap_map_has(const dap_map_t *a_map, const char *a_key)
{
    return dap_map_get(a_map, a_key) != NULL;
}

/**
 * @brief Remove by key. Does NOT free the value.
 * @return removed value, or NULL if not found
 */
DAP_STATIC_INLINE void *dap_map_remove(dap_map_t *a_map, const char *a_key)
{
    void *l_val = NULL;
    if (!a_map->root) return NULL;
    a_map->root = s_dap_map_delete(a_map->root, a_key, &l_val, &a_map->count);
    if (a_map->root) {
        a_map->root->color  = DAP_MAP_BLACK;
        a_map->root->parent = NULL;
    }
    return l_val;
}

/**
 * @brief Number of entries.
 */
DAP_STATIC_INLINE unsigned dap_map_count(const dap_map_t *a_map)
{
    return a_map ? a_map->count : 0;
}

/* ── Iteration (in-order) ────────────────────────────────────── */

DAP_STATIC_INLINE void dap_map_begin(const dap_map_t *a_map, dap_map_iter_t *a_it)
{
    dap_map_node_t *l_node = a_map ? a_map->root : NULL;
    while (l_node && l_node->left) l_node = l_node->left;
    a_it->node = l_node;
    a_it->key  = l_node ? l_node->key : NULL;
    a_it->val  = l_node ? l_node->val : NULL;
}

DAP_STATIC_INLINE bool dap_map_iter_valid(const dap_map_iter_t *a_it)
{
    return a_it->node != NULL;
}

DAP_STATIC_INLINE void dap_map_iter_next(dap_map_iter_t *a_it)
{
    dap_map_node_t *l_node = a_it->node;
    if (!l_node) return;
    if (l_node->right) {
        l_node = l_node->right;
        while (l_node->left) l_node = l_node->left;
    } else {
        dap_map_node_t *l_parent = l_node->parent;
        while (l_parent && l_node == l_parent->right) {
            l_node   = l_parent;
            l_parent = l_parent->parent;
        }
        l_node = l_parent;
    }
    a_it->node = l_node;
    a_it->key  = l_node ? l_node->key : NULL;
    a_it->val  = l_node ? l_node->val : NULL;
}

/**
 * @brief Walk all entries with a callback.
 */
DAP_STATIC_INLINE void dap_map_walk(const dap_map_t *a_map,
                                      void (*a_cb)(const char *a_key, void *a_val, void *a_arg),
                                      void *a_arg)
{
    dap_map_iter_t l_it;
    for (dap_map_begin(a_map, &l_it); dap_map_iter_valid(&l_it); dap_map_iter_next(&l_it))
        a_cb(l_it.key, l_it.val, a_arg);
}

/* ── Cleanup ─────────────────────────────────────────────────── */

/**
 * @brief Free all nodes. Does NOT free values.
 */
DAP_STATIC_INLINE void dap_map_purge(dap_map_t *a_map)
{
    s_dap_map_purge(a_map->root, NULL);
    a_map->root  = NULL;
    a_map->count = 0;
}

/**
 * @brief Free all nodes AND values via DAP_DELETE.
 */
DAP_STATIC_INLINE void dap_map_purge_all(dap_map_t *a_map)
{
    s_dap_map_purge_delete(a_map->root);
    a_map->root  = NULL;
    a_map->count = 0;
}

/**
 * @brief Free all nodes AND values via custom callback.
 */
DAP_STATIC_INLINE void dap_map_purge_cb(dap_map_t *a_map, void (*a_free_val)(void *))
{
    s_dap_map_purge(a_map->root, a_free_val);
    a_map->root  = NULL;
    a_map->count = 0;
}

#ifdef __cplusplus
}
#endif
