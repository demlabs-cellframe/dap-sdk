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
#include <string.h>
#include "dap_common.h"
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

// Lower load factor = shorter chains = faster lookup
#ifndef DAP_HT_LOAD_FACTOR
#define DAP_HT_LOAD_FACTOR 50
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
    dap_ht_table_t *tbl = DAP_NEW_Z(dap_ht_table_t);
    if (!tbl) return NULL;
    tbl->buckets = DAP_NEW_Z_COUNT(dap_ht_bucket_t, DAP_HT_INITIAL_BUCKETS);
    if (!tbl->buckets) { DAP_DELETE(tbl); return NULL; }
    tbl->num_buckets = DAP_HT_INITIAL_BUCKETS;
    tbl->hho = hho;
    return tbl;
}

static inline void dap_ht_expand(dap_ht_table_t *tbl) {
    unsigned new_num = tbl->num_buckets * 2;
    dap_ht_bucket_t *new_bkts = DAP_NEW_Z_COUNT(dap_ht_bucket_t, new_num);
    if (!new_bkts) return;
    
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
    DAP_DELETE(tbl->buckets);
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

// Prefetch hint for better cache performance
#if defined(__GNUC__) || defined(__clang__)
#define DAP_HT_PREFETCH(addr) __builtin_prefetch(addr, 0, 1)
#else
#define DAP_HT_PREFETCH(addr) (void)(addr)
#endif

/**
 * OPTIMIZED find:
 * - Compare hash first (fast integer comparison for rejection)
 * - Prefetch next element to hide memory latency
 */
static inline void* dap_ht_find_impl(dap_ht_table_t *tbl, const void *key, unsigned keylen) {
    if (!tbl) return NULL;
    uint32_t hashv = DAP_HT_HASH(key, keylen);
    unsigned bkt = DAP_HT_TO_BKT(hashv, tbl->num_buckets);
    for (dap_ht_handle_t *hh = tbl->buckets[bkt].head; hh; hh = hh->hh_next) {
        // Prefetch next element while comparing current
        if (hh->hh_next) DAP_HT_PREFETCH(hh->hh_next);
        // Fast path: compare hash first (single 32-bit comparison)
        // Then keylen, then full key - avoids expensive memcmp on misses
        if (hh->hashv == hashv && hh->keylen == keylen && memcmp(hh->key, key, keylen) == 0)
            return DAP_HT_FROM_HH(tbl, hh);
    }
    return NULL;
}

static inline void dap_ht_del_impl(void **head, void *del UNUSED_ARG, dap_ht_handle_t *del_hh) {
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
        DAP_DELETE(tbl->buckets);
        DAP_DELETE(tbl);
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
        DAP_DELETE((head)->hh.tbl->buckets); \
        DAP_DELETE((head)->hh.tbl); \
    } \
    (head) = NULL; \
} while (0)

/**
 * @brief Clear table with explicit handle name (does NOT free items)
 */
#define dap_ht_clear_hh(hhname, head) do { \
    if ((head) && (head)->hhname.tbl) { \
        DAP_DELETE((head)->hhname.tbl->buckets); \
        DAP_DELETE((head)->hhname.tbl); \
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

/**
 * @brief Add item with integer key
 */
#define dap_ht_add_int(head, field, add) \
    dap_ht_add_impl((void**)&(head), (add), &((add)->hh), \
        &((add)->field), sizeof((add)->field), \
        (ptrdiff_t)((char*)&((add)->hh) - (char*)(add)))

/**
 * @brief Compute hash value for key (for use with _by_hashvalue functions)
 */
#define dap_ht_hash_value(key, keylen) DAP_HT_HASH((key), (keylen))

#define dap_ht_last(head) \
    ( (head) && (head)->hh.tbl ? (typeof(head))((head)->hh.tbl->tail) : NULL )

// ============================================================================
// By-hashvalue functions (pre-computed hash for performance)
// ============================================================================

static inline void dap_ht_add_by_hashvalue_impl(void **head, void *add, dap_ht_handle_t *add_hh,
                                                 const void *key, unsigned keylen, uint32_t hashv,
                                                 ptrdiff_t hho) {
    add_hh->key = key;
    add_hh->keylen = keylen;
    add_hh->hashv = hashv;
    add_hh->prev = NULL;
    add_hh->next = NULL;
    add_hh->hh_prev = NULL;
    add_hh->hh_next = NULL;
    
    if (!*head) {
        add_hh->tbl = dap_ht_new_tbl(hho);
        if (!add_hh->tbl) return;
        *head = add;
        add_hh->tbl->tail = add;
    } else {
        dap_ht_handle_t *head_hh = (dap_ht_handle_t*)((char*)*head + hho);
        add_hh->tbl = head_hh->tbl;
    }
    
    dap_ht_table_t *tbl = add_hh->tbl;
    
    if (tbl->num_items >= tbl->num_buckets * DAP_HT_LOAD_FACTOR / 100)
        dap_ht_expand(tbl);
    
    unsigned bkt = DAP_HT_TO_BKT(hashv, tbl->num_buckets);
    add_hh->hh_next = tbl->buckets[bkt].head;
    if (tbl->buckets[bkt].head) tbl->buckets[bkt].head->hh_prev = add_hh;
    tbl->buckets[bkt].head = add_hh;
    tbl->buckets[bkt].count++;
    
    if (tbl->tail && tbl->tail != add) {
        dap_ht_handle_t *tail_hh = (dap_ht_handle_t*)((char*)tbl->tail + hho);
        tail_hh->next = add;
        add_hh->prev = tbl->tail;
    }
    tbl->tail = add;
    tbl->num_items++;
}

static inline void* dap_ht_find_by_hashvalue_impl(dap_ht_table_t *tbl, const void *key, unsigned keylen, uint32_t hashv) {
    if (!tbl) return NULL;
    unsigned bkt = DAP_HT_TO_BKT(hashv, tbl->num_buckets);
    for (dap_ht_handle_t *hh = tbl->buckets[bkt].head; hh; hh = hh->hh_next) {
        if (hh->hh_next) DAP_HT_PREFETCH(hh->hh_next);
        // Hash already matches (pre-computed), just check keylen and content
        if (hh->hashv == hashv && hh->keylen == keylen && memcmp(hh->key, key, keylen) == 0)
            return DAP_HT_FROM_HH(tbl, hh);
    }
    return NULL;
}

/**
 * @brief Add item with pre-computed hash value
 */
#define dap_ht_add_by_hashvalue(head, field, keylen, hashv, add) \
    dap_ht_add_by_hashvalue_impl((void**)&(head), (add), &((add)->hh), \
        &((add)->field), (unsigned)(keylen), (hashv), \
        (ptrdiff_t)((char*)&((add)->hh) - (char*)(add)))

/**
 * @brief Find item with pre-computed hash value
 */
#define dap_ht_find_by_hashvalue(head, keyptr, keylen, hashv, out) \
    (out) = (head) ? (DAP_HT_TYPEOF(head))dap_ht_find_by_hashvalue_impl((head)->hh.tbl, (keyptr), (unsigned)(keylen), (hashv)) : NULL

/**
 * @brief Delete item (same as dap_ht_del, hash value not needed for deletion)
 */
#define dap_ht_del_by_hashvalue(head, del) dap_ht_del(head, del)

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
 * @brief Add item with explicit handle name and key pointer
 */
#define dap_ht_add_keyptr_hh(hhname, head, keyptr, keylen, add) \
    dap_ht_add_impl((void**)&(head), (add), &((add)->hhname), \
        (keyptr), (unsigned)(keylen), \
        (ptrdiff_t)((char*)&((add)->hhname) - (char*)(add)))

/**
 * @brief Find item by string key with explicit handle name
 */
#define dap_ht_find_str_hh(hhname, head, findstr, out) \
    dap_ht_find_hh(hhname, head, findstr, strlen(findstr), out)

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

/**
 * @brief Add item with explicit handle name and pre-computed hash
 */
#define dap_ht_add_by_hashvalue_hh(hhname, head, field, keylen, hashv, add) \
    dap_ht_add_by_hashvalue_impl((void**)&(head), (add), &((add)->hhname), \
        &((add)->field), (unsigned)(keylen), (hashv), \
        (ptrdiff_t)((char*)&((add)->hhname) - (char*)(add)))

/**
 * @brief Find item with explicit handle name and pre-computed hash
 */
#define dap_ht_find_by_hashvalue_hh(hhname, head, keyptr, keylen, hashv, out) \
    (out) = (head) ? (DAP_HT_TYPEOF(head))dap_ht_find_by_hashvalue_impl((head)->hhname.tbl, (keyptr), (unsigned)(keylen), (hashv)) : NULL

/**
 * @brief Delete item with explicit handle name
 */
#define dap_ht_del_by_hashvalue_hh(hhname, head, del) dap_ht_del_hh(hhname, head, del)

// ============================================================================
// Sort hash table iteration order
// ============================================================================

static inline void dap_ht_sort_impl(void **head, int (*cmp)(void*, void*), ptrdiff_t hho) {
    if (!*head) return;
    dap_ht_handle_t *head_hh = (dap_ht_handle_t*)((char*)*head + hho);
    dap_ht_table_t *tbl = head_hh->tbl;
    if (!tbl) return;

    int insize = 1;
    void *list = *head;
    while (1) {
        void *p = list, *tail = NULL;
        list = NULL;
        int nmerges = 0;
        while (p) {
            nmerges++;
            void *q = p;
            int psize = 0;
            for (int i = 0; i < insize; i++) {
                psize++;
                q = ((dap_ht_handle_t*)((char*)q + hho))->next;
                if (!q) break;
            }
            int qsize = insize;
            while (psize > 0 || (qsize > 0 && q)) {
                void *e;
                if (psize == 0) {
                    e = q; q = ((dap_ht_handle_t*)((char*)q + hho))->next; qsize--;
                } else if (qsize == 0 || !q) {
                    e = p; p = ((dap_ht_handle_t*)((char*)p + hho))->next; psize--;
                } else if (cmp(p, q) <= 0) {
                    e = p; p = ((dap_ht_handle_t*)((char*)p + hho))->next; psize--;
                } else {
                    e = q; q = ((dap_ht_handle_t*)((char*)q + hho))->next; qsize--;
                }
                dap_ht_handle_t *e_hh = (dap_ht_handle_t*)((char*)e + hho);
                if (tail) {
                    ((dap_ht_handle_t*)((char*)tail + hho))->next = e;
                } else {
                    list = e;
                }
                e_hh->prev = tail;
                tail = e;
            }
            p = q;
        }
        if (tail) ((dap_ht_handle_t*)((char*)tail + hho))->next = NULL;
        if (nmerges <= 1) {
            *head = list;
            tbl->tail = tail;
            break;
        }
        insize *= 2;
    }
}

#define dap_ht_sort(head, cmp) do { \
    if (head) \
        dap_ht_sort_impl((void**)&(head), (int(*)(void*,void*))(cmp), \
            (ptrdiff_t)((char*)&((head)->hh) - (char*)(head))); \
} while (0)

#define dap_ht_sort_hh(hhname, head, cmp) do { \
    if (head) \
        dap_ht_sort_impl((void**)&(head), (int(*)(void*,void*))(cmp), \
            (ptrdiff_t)((char*)&((head)->hhname) - (char*)(head))); \
} while (0)

// ============================================================================
// Add to hash table in sorted order
// ============================================================================

static inline void dap_ht_add_inorder_impl(void **head, void *add, dap_ht_handle_t *add_hh,
                                             const void *key, unsigned keylen,
                                             int (*cmp)(void*, void*), ptrdiff_t hho) {
    add_hh->key = key;
    add_hh->keylen = keylen;
    add_hh->hashv = DAP_HT_HASH(key, keylen);
    add_hh->prev = NULL;
    add_hh->next = NULL;
    add_hh->hh_prev = NULL;
    add_hh->hh_next = NULL;

    if (!*head) {
        dap_ht_table_t *tbl = (dap_ht_table_t*)DAP_CALLOC(1, sizeof(dap_ht_table_t));
        if (!tbl) return;
        tbl->num_items = 1;
        tbl->hho = hho;
        tbl->num_buckets = DAP_HT_INITIAL_BUCKETS;
        tbl->buckets = (dap_ht_bucket_t*)DAP_CALLOC(tbl->num_buckets, sizeof(dap_ht_bucket_t));
        if (!tbl->buckets) { DAP_DELETE(tbl); return; }
        add_hh->tbl = tbl;
        *head = add;
        tbl->tail = add;
        unsigned bkt = DAP_HT_TO_BKT(add_hh->hashv, tbl->num_buckets);
        tbl->buckets[bkt].head = add_hh;
        tbl->buckets[bkt].count = 1;
        return;
    }

    dap_ht_handle_t *head_hh = (dap_ht_handle_t*)((char*)*head + hho);
    add_hh->tbl = head_hh->tbl;

    dap_ht_table_t *tbl = add_hh->tbl;
    tbl->num_items++;

    unsigned bkt = DAP_HT_TO_BKT(add_hh->hashv, tbl->num_buckets);
    add_hh->hh_next = tbl->buckets[bkt].head;
    if (tbl->buckets[bkt].head) tbl->buckets[bkt].head->hh_prev = add_hh;
    tbl->buckets[bkt].head = add_hh;
    tbl->buckets[bkt].count++;

    void *pos = NULL;
    void *el = *head;
    while (el) {
        if (cmp(add, el) <= 0) { pos = el; break; }
        el = ((dap_ht_handle_t*)((char*)el + hho))->next;
    }

    if (pos) {
        dap_ht_handle_t *pos_hh = (dap_ht_handle_t*)((char*)pos + hho);
        add_hh->next = pos;
        add_hh->prev = pos_hh->prev;
        if (pos == *head) {
            *head = add;
        } else if (pos_hh->prev) {
            ((dap_ht_handle_t*)((char*)pos_hh->prev + hho))->next = add;
        }
        pos_hh->prev = add;
    } else {
        if (tbl->tail && tbl->tail != add) {
            dap_ht_handle_t *tail_hh = (dap_ht_handle_t*)((char*)tbl->tail + hho);
            tail_hh->next = add;
            add_hh->prev = tbl->tail;
        }
        tbl->tail = add;
    }

    if (tbl->num_items >= tbl->num_buckets * 10)
        dap_ht_expand(tbl);
}

#define dap_ht_add_inorder(head, field, add, cmp) \
    dap_ht_add_inorder_impl((void**)&(head), (add), &((add)->hh), \
        &((add)->field), sizeof((add)->field), (int(*)(void*,void*))(cmp), \
        (ptrdiff_t)((char*)&((add)->hh) - (char*)(add)))

#define dap_ht_add_inorder_hh(hhname, head, field, add, cmp) \
    dap_ht_add_inorder_impl((void**)&(head), (add), &((add)->hhname), \
        &((add)->field), sizeof((add)->field), (int(*)(void*,void*))(cmp), \
        (ptrdiff_t)((char*)&((add)->hhname) - (char*)(add)))

#ifdef __cplusplus
}
#endif
