/**
 * @file dap_ht.h
 * @brief DAP Hash Table - intrusive hash table implementation
 *
 * Fast intrusive hash table. "Intrusive" means the hash handle
 * is embedded directly in your structure.
 *
 * Usage:
 *   struct my_item {
 *       char *name;           // Key
 *       int value;
 *       dap_ht_handle_t hh;   // Hash handle (MUST be named 'hh')
 *   };
 *
 *   struct my_item *table = NULL;
 *   struct my_item *item = malloc(sizeof(*item));
 *   item->name = strdup("key1");
 *   item->value = 42;
 *   dap_ht_add_str(table, name, item);
 *
 *   struct my_item *found;
 *   dap_ht_find_str(table, "key1", found);
 *
 * Copyright (c) 2024-2026 Demlabs
 * License: GNU GPL v3
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "dap_hash_fast.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration
// ============================================================================

#ifndef DAP_HT_INITIAL_BUCKETS
#define DAP_HT_INITIAL_BUCKETS 32
#endif

#ifndef DAP_HT_LOAD_FACTOR
#define DAP_HT_LOAD_FACTOR 75
#endif

#ifndef dap_ht_malloc
#define dap_ht_malloc(sz) malloc(sz)
#endif

#ifndef dap_ht_free
#define dap_ht_free(ptr, sz) free(ptr)
#endif

// Default hash function - xxHash (fastest)
#ifndef DAP_HT_HASH
#define DAP_HT_HASH(key, len) dap_hash_fast_ht((key), (len))
#endif

// ============================================================================
// Structures
// ============================================================================

typedef struct dap_ht_handle {
    struct dap_ht_table *tbl;
    void *prev;                     // Previous item in iteration order
    void *next;                     // Next item in iteration order
    struct dap_ht_handle *hh_prev;  // Previous in bucket
    struct dap_ht_handle *hh_next;  // Next in bucket
    const void *key;
    unsigned keylen;
    uint32_t hashv;
} dap_ht_handle_t;

typedef struct dap_ht_bucket {
    dap_ht_handle_t *head;
    unsigned count;
} dap_ht_bucket_t;

typedef struct dap_ht_table {
    dap_ht_bucket_t *buckets;
    unsigned num_buckets;
    unsigned num_items;
    void *tail;
    ptrdiff_t hho;  // Handle offset
} dap_ht_table_t;

// ============================================================================
// Type helpers
// ============================================================================

#if defined(__GNUC__) || defined(__clang__)
#define DAP_HT_TYPEOF(x) __typeof(x)
#else
#define DAP_HT_TYPEOF(x) void*
#endif

#define DAP_HT_TO_BKT(hashv, num) ((hashv) & ((num) - 1U))
#define DAP_HT_FROM_HH(tbl, hh) ((void*)(((char*)(hh)) - ((tbl)->hho)))
#define DAP_HT_TO_HH(tbl, el) ((dap_ht_handle_t*)(((char*)(el)) + ((tbl)->hho)))

// ============================================================================
// Internal functions (static inline for speed)
// ============================================================================

static inline dap_ht_table_t* dap_ht_new_tbl(ptrdiff_t hho) {
    dap_ht_table_t *tbl = (dap_ht_table_t*)dap_ht_malloc(sizeof(*tbl));
    if (!tbl) return NULL;
    memset(tbl, 0, sizeof(*tbl));
    tbl->buckets = (dap_ht_bucket_t*)dap_ht_malloc(DAP_HT_INITIAL_BUCKETS * sizeof(dap_ht_bucket_t));
    if (!tbl->buckets) { dap_ht_free(tbl, sizeof(*tbl)); return NULL; }
    memset(tbl->buckets, 0, DAP_HT_INITIAL_BUCKETS * sizeof(dap_ht_bucket_t));
    tbl->num_buckets = DAP_HT_INITIAL_BUCKETS;
    tbl->hho = hho;
    return tbl;
}

static inline void dap_ht_expand(dap_ht_table_t *tbl) {
    unsigned new_num = tbl->num_buckets * 2;
    dap_ht_bucket_t *new_bkts = (dap_ht_bucket_t*)dap_ht_malloc(new_num * sizeof(dap_ht_bucket_t));
    if (!new_bkts) return;
    memset(new_bkts, 0, new_num * sizeof(dap_ht_bucket_t));
    
    for (unsigned i = 0; i < tbl->num_buckets; i++) {
        dap_ht_handle_t *hh = tbl->buckets[i].head;
        while (hh) {
            dap_ht_handle_t *next = hh->hh_next;
            unsigned bkt = DAP_HT_TO_BKT(hh->hashv, new_num);
            hh->hh_next = new_bkts[bkt].head;
            hh->hh_prev = NULL;
            if (new_bkts[bkt].head) new_bkts[bkt].head->hh_prev = hh;
            new_bkts[bkt].head = hh;
            new_bkts[bkt].count++;
            hh = next;
        }
    }
    dap_ht_free(tbl->buckets, tbl->num_buckets * sizeof(dap_ht_bucket_t));
    tbl->buckets = new_bkts;
    tbl->num_buckets = new_num;
}

static inline void dap_ht_add_impl(void **head, void *add, dap_ht_handle_t *add_hh,
                                    const void *key, unsigned keylen, ptrdiff_t hho) {
    add_hh->key = key;
    add_hh->keylen = keylen;
    add_hh->hashv = DAP_HT_HASH(key, keylen);
    add_hh->prev = NULL;
    add_hh->next = NULL;
    add_hh->hh_prev = NULL;
    add_hh->hh_next = NULL;
    
    // First item - create table
    if (!*head) {
        add_hh->tbl = dap_ht_new_tbl(hho);
        if (!add_hh->tbl) return;
        *head = add;
        add_hh->tbl->tail = add;
    } else {
        // Get table from head
        dap_ht_handle_t *head_hh = (dap_ht_handle_t*)((char*)*head + hho);
        add_hh->tbl = head_hh->tbl;
    }
    
    dap_ht_table_t *tbl = add_hh->tbl;
    
    if (tbl->num_items >= tbl->num_buckets * DAP_HT_LOAD_FACTOR / 100)
        dap_ht_expand(tbl);
    
    // Add to bucket
    unsigned bkt = DAP_HT_TO_BKT(add_hh->hashv, tbl->num_buckets);
    add_hh->hh_next = tbl->buckets[bkt].head;
    if (tbl->buckets[bkt].head) tbl->buckets[bkt].head->hh_prev = add_hh;
    tbl->buckets[bkt].head = add_hh;
    tbl->buckets[bkt].count++;
    
    // Add to iteration list (append to tail)
    if (tbl->tail && tbl->tail != add) {
        dap_ht_handle_t *tail_hh = (dap_ht_handle_t*)((char*)tbl->tail + hho);
        tail_hh->next = add;
        add_hh->prev = tbl->tail;
    }
    tbl->tail = add;
    tbl->num_items++;
}

static inline void* dap_ht_find_impl(dap_ht_table_t *tbl, const void *key, unsigned keylen) {
    if (!tbl) return NULL;
    uint32_t hashv = DAP_HT_HASH(key, keylen);
    unsigned bkt = DAP_HT_TO_BKT(hashv, tbl->num_buckets);
    for (dap_ht_handle_t *hh = tbl->buckets[bkt].head; hh; hh = hh->hh_next) {
        if (hh->keylen == keylen && memcmp(hh->key, key, keylen) == 0)
            return DAP_HT_FROM_HH(tbl, hh);
    }
    return NULL;
}

static inline void dap_ht_del_impl(void **head, void *del, dap_ht_handle_t *del_hh) {
    dap_ht_table_t *tbl = del_hh->tbl;
    if (!tbl) return;
    
    unsigned bkt = DAP_HT_TO_BKT(del_hh->hashv, tbl->num_buckets);
    if (del_hh->hh_prev) del_hh->hh_prev->hh_next = del_hh->hh_next;
    else tbl->buckets[bkt].head = del_hh->hh_next;
    if (del_hh->hh_next) del_hh->hh_next->hh_prev = del_hh->hh_prev;
    tbl->buckets[bkt].count--;
    
    if (del_hh->prev) {
        dap_ht_handle_t *prev_hh = DAP_HT_TO_HH(tbl, del_hh->prev);
        prev_hh->next = del_hh->next;
    } else {
        *head = del_hh->next;
    }
    if (del_hh->next) {
        dap_ht_handle_t *next_hh = DAP_HT_TO_HH(tbl, del_hh->next);
        next_hh->prev = del_hh->prev;
    } else {
        tbl->tail = del_hh->prev;
    }
    
    tbl->num_items--;
    if (tbl->num_items == 0) {
        ptrdiff_t hho = tbl->hho;  // Save before freeing
        dap_ht_free(tbl->buckets, tbl->num_buckets * sizeof(dap_ht_bucket_t));
        dap_ht_free(tbl, sizeof(*tbl));
        if (*head) ((dap_ht_handle_t*)((char*)*head + hho))->tbl = NULL;
    }
}

// ============================================================================
// Public API Macros
// ============================================================================

/**
 * @brief Add item with key from field
 * @param head Table head pointer
 * @param field Key field name
 * @param add Item to add
 */
#define dap_ht_add(head, field, add) \
    dap_ht_add_impl((void**)&(head), (add), &((add)->hh), \
        &((add)->field), sizeof((add)->field), \
        (ptrdiff_t)((char*)&((add)->hh) - (char*)(add)))

/**
 * @brief Add item with string key
 */
#define dap_ht_add_str(head, field, add) \
    dap_ht_add_impl((void**)&(head), (add), &((add)->hh), \
        (add)->field, (unsigned)strlen((add)->field), \
        (ptrdiff_t)((char*)&((add)->hh) - (char*)(add)))

/**
 * @brief Add item with explicit key pointer
 */
#define dap_ht_add_keyptr(head, keyptr, keylen, add) \
    dap_ht_add_impl((void**)&(head), (add), &((add)->hh), \
        (keyptr), (unsigned)(keylen), \
        (ptrdiff_t)((char*)&((add)->hh) - (char*)(add)))

/**
 * @brief Find item by key field
 */
#define dap_ht_find(head, keyptr, keylen, out) \
    (out) = (head) ? (DAP_HT_TYPEOF(head))dap_ht_find_impl((head)->hh.tbl, (keyptr), (unsigned)(keylen)) : NULL

/**
 * @brief Find item by string key
 */
#define dap_ht_find_str(head, findstr, out) \
    dap_ht_find(head, findstr, strlen(findstr), out)

/**
 * @brief Find item by integer key
 */
#define dap_ht_find_int(head, findint, out) \
    dap_ht_find(head, &(findint), sizeof(findint), out)

/**
 * @brief Delete item
 */
#define dap_ht_del(head, del) \
    dap_ht_del_impl((void**)&(head), (del), &((del)->hh))

/**
 * @brief Get item count
 */
#define dap_ht_count(head) \
    ((head) && (head)->hh.tbl ? (head)->hh.tbl->num_items : ((head) ? 1 : 0))

/**
 * @brief Iterate (safe for deletion)
 */
#define dap_ht_foreach(head, el, tmp) \
    for ((el) = (head), (tmp) = (el) ? (DAP_HT_TYPEOF(el))((el)->hh.next) : NULL; \
         (el); \
         (el) = (tmp), (tmp) = (el) ? (DAP_HT_TYPEOF(el))((el)->hh.next) : NULL)

/**
 * @brief Clear table (does NOT free items)
 */
#define dap_ht_clear(head) do { \
    if ((head) && (head)->hh.tbl) { \
        dap_ht_free((head)->hh.tbl->buckets, (head)->hh.tbl->num_buckets * sizeof(dap_ht_bucket_t)); \
        dap_ht_free((head)->hh.tbl, sizeof(dap_ht_table_t)); \
    } \
    (head) = NULL; \
} while (0)

/**
 * @brief Add item with pointer key (key is the address of field)
 */
#define dap_ht_add_ptr(head, field, add) \
    dap_ht_add_impl((void**)&(head), (add), &((add)->hh), \
        &((add)->field), sizeof((add)->field), \
        (ptrdiff_t)((char*)&((add)->hh) - (char*)(add)))

/**
 * @brief Find item by pointer key
 */
#define dap_ht_find_ptr(head, findptr, out) do { \
    void *_ptr = (void*)(findptr); \
    dap_ht_find(head, &_ptr, sizeof(_ptr), out); \
} while (0)

// ============================================================================
// Named Handle Versions (for multiple hash tables per item)
// ============================================================================

/**
 * @brief Add item with explicit handle name and key field
 * For items that belong to multiple hash tables
 */
#define dap_ht_add_hh(hhname, head, field, add) \
    dap_ht_add_impl((void**)&(head), (add), &((add)->hhname), \
        &((add)->field), sizeof((add)->field), \
        (ptrdiff_t)((char*)&((add)->hhname) - (char*)(add)))

/**
 * @brief Find item with explicit handle name
 */
#define dap_ht_find_hh(hhname, head, keyptr, keylen, out) \
    (out) = (head) ? (DAP_HT_TYPEOF(head))dap_ht_find_impl((head)->hhname.tbl, (keyptr), (unsigned)(keylen)) : NULL

/**
 * @brief Delete item with explicit handle name
 */
#define dap_ht_del_hh(hhname, head, del) \
    dap_ht_del_impl((void**)&(head), (del), &((del)->hhname))

/**
 * @brief Count with explicit handle name
 */
#define dap_ht_count_hh(hhname, head) \
    ((head) && (head)->hhname.tbl ? (head)->hhname.tbl->num_items : ((head) ? 1 : 0))

/**
 * @brief Iterate with explicit handle name
 */
#define dap_ht_foreach_hh(hhname, head, el, tmp) \
    for ((el) = (head), (tmp) = (el) ? (DAP_HT_TYPEOF(el))((el)->hhname.next) : NULL; \
         (el); \
         (el) = (tmp), (tmp) = (el) ? (DAP_HT_TYPEOF(el))((el)->hhname.next) : NULL)

#ifdef __cplusplus
}
#endif
