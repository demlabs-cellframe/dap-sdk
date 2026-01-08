/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform) the open source project
 *
 *    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "dap_string_pool.h"
#include "dap_arena.h"
#include "dap_common.h"

#define LOG_TAG "dap_string_pool"

// Default initial capacity
#define DAP_STRING_POOL_DEFAULT_CAPACITY 256

// Load factor before rehashing (75%)
#define DAP_STRING_POOL_LOAD_FACTOR 0.75

// Minimum capacity
#define DAP_STRING_POOL_MIN_CAPACITY 16

/**
 * @brief String pool entry (hash table node)
 */
typedef struct dap_string_pool_entry {
    const char *string;                      // Interned string
    size_t length;                           // String length
    uint32_t hash;                           // Cached hash value
    struct dap_string_pool_entry *next;      // Next in collision chain
} dap_string_pool_entry_t;

/**
 * @brief String pool structure
 */
struct dap_string_pool {
    dap_string_pool_entry_t **table;         // Hash table
    size_t capacity;                         // Table capacity
    size_t count;                            // Number of strings
    dap_arena_t *arena;                      // Arena for string storage
    bool thread_safe;                        // Thread-safe flag
    pthread_mutex_t mutex;                   // Mutex for thread safety
    
    // Statistics
    size_t lookup_count;
    size_t hit_count;
    size_t collision_count;
};

/* ========================================================================== */
/*                         HASH FUNCTIONS                                     */
/* ========================================================================== */

/**
 * @brief FNV-1a hash function
 * 
 * Fast and good distribution for short strings.
 */
static inline uint32_t s_hash_fnv1a(const char *a_data, size_t a_len)
{
    uint32_t l_hash = 2166136261u;
    
    for (size_t i = 0; i < a_len; i++) {
        l_hash ^= (uint8_t)a_data[i];
        l_hash *= 16777619u;
    }
    
    return l_hash;
}

/**
 * @brief Get hash table index
 */
static inline size_t s_hash_to_index(uint32_t a_hash, size_t a_capacity)
{
    // Simple modulo for power-of-2 capacity (fast bitwise AND)
    return a_hash & (a_capacity - 1);
}

/* ========================================================================== */
/*                         INTERNAL FUNCTIONS                                 */
/* ========================================================================== */

/**
 * @brief Find string in pool (internal)
 */
static dap_string_pool_entry_t *s_find_entry(
    const dap_string_pool_t *a_pool,
    const char *a_str,
    size_t a_len,
    uint32_t a_hash
)
{
    size_t l_index = s_hash_to_index(a_hash, a_pool->capacity);
    
    for (dap_string_pool_entry_t *l_entry = a_pool->table[l_index];
         l_entry != NULL;
         l_entry = l_entry->next)
    {
        if (l_entry->hash == a_hash &&
            l_entry->length == a_len &&
            memcmp(l_entry->string, a_str, a_len) == 0)
        {
            return l_entry;
        }
    }
    
    return NULL;
}

/**
 * @brief Rehash table (grow capacity)
 */
static bool s_rehash(dap_string_pool_t *a_pool)
{
    size_t l_new_capacity = a_pool->capacity * 2;
    
    dap_string_pool_entry_t **l_new_table = 
        (dap_string_pool_entry_t **)DAP_NEW_Z_SIZE(dap_string_pool_entry_t*, 
                                                     l_new_capacity * sizeof(dap_string_pool_entry_t*));
    
    if (!l_new_table) {
        log_it(L_ERROR, "Failed to allocate new hash table (%zu entries)", l_new_capacity);
        return false;
    }
    
    // Rehash all entries
    for (size_t i = 0; i < a_pool->capacity; i++) {
        dap_string_pool_entry_t *l_entry = a_pool->table[i];
        
        while (l_entry) {
            dap_string_pool_entry_t *l_next = l_entry->next;
            
            // Reinsert into new table
            size_t l_new_index = s_hash_to_index(l_entry->hash, l_new_capacity);
            l_entry->next = l_new_table[l_new_index];
            l_new_table[l_new_index] = l_entry;
            
            l_entry = l_next;
        }
    }
    
    // Replace old table
    DAP_DELETE(a_pool->table);
    a_pool->table = l_new_table;
    a_pool->capacity = l_new_capacity;
    
    log_it(L_DEBUG, "String pool rehashed to %zu entries", l_new_capacity);
    
    return true;
}

/* ========================================================================== */
/*                         PUBLIC API                                         */
/* ========================================================================== */

/**
 * @brief Create new string pool
 */
dap_string_pool_t *dap_string_pool_new(size_t a_initial_capacity)
{
    if (a_initial_capacity == 0) {
        a_initial_capacity = DAP_STRING_POOL_DEFAULT_CAPACITY;
    }
    
    if (a_initial_capacity < DAP_STRING_POOL_MIN_CAPACITY) {
        a_initial_capacity = DAP_STRING_POOL_MIN_CAPACITY;
    }
    
    // Round up to power of 2
    a_initial_capacity = 1u << (32 - __builtin_clz(a_initial_capacity - 1));
    
    // Allocate pool structure
    dap_string_pool_t *l_pool = DAP_NEW_Z(dap_string_pool_t);
    if (!l_pool) {
        log_it(L_ERROR, "Failed to allocate string pool");
        return NULL;
    }
    
    // Allocate hash table
    l_pool->table = (dap_string_pool_entry_t **)DAP_NEW_Z_SIZE(
        dap_string_pool_entry_t*, 
        a_initial_capacity * sizeof(dap_string_pool_entry_t*)
    );
    
    if (!l_pool->table) {
        log_it(L_ERROR, "Failed to allocate hash table");
        DAP_DELETE(l_pool);
        return NULL;
    }
    
    // Create arena for string storage
    l_pool->arena = dap_arena_new(4096);
    if (!l_pool->arena) {
        log_it(L_ERROR, "Failed to create arena");
        DAP_DELETE(l_pool->table);
        DAP_DELETE(l_pool);
        return NULL;
    }
    
    l_pool->capacity = a_initial_capacity;
    l_pool->count = 0;
    l_pool->thread_safe = false;
    l_pool->lookup_count = 0;
    l_pool->hit_count = 0;
    l_pool->collision_count = 0;
    
    log_it(L_DEBUG, "String pool created (capacity: %zu)", a_initial_capacity);
    
    return l_pool;
}

/**
 * @brief Create thread-safe string pool
 */
dap_string_pool_t *dap_string_pool_new_thread_safe(size_t a_initial_capacity)
{
    dap_string_pool_t *l_pool = dap_string_pool_new(a_initial_capacity);
    if (!l_pool) {
        return NULL;
    }
    
    l_pool->thread_safe = true;
    pthread_mutex_init(&l_pool->mutex, NULL);
    
    log_it(L_DEBUG, "Thread-safe string pool created");
    
    return l_pool;
}

/**
 * @brief Intern string with known length
 */
const char *dap_string_pool_intern_n(dap_string_pool_t *a_pool, const char *a_str, size_t a_len)
{
    if (!a_pool || !a_str) {
        return NULL;
    }
    
    if (a_pool->thread_safe) {
        pthread_mutex_lock(&a_pool->mutex);
    }
    
    // Calculate hash
    uint32_t l_hash = s_hash_fnv1a(a_str, a_len);
    
    // Statistics
    a_pool->lookup_count++;
    
    // Check if string already exists
    dap_string_pool_entry_t *l_existing = s_find_entry(a_pool, a_str, a_len, l_hash);
    if (l_existing) {
        a_pool->hit_count++;
        
        if (a_pool->thread_safe) {
            pthread_mutex_unlock(&a_pool->mutex);
        }
        
        return l_existing->string;
    }
    
    // Check load factor and rehash if needed
    if ((double)a_pool->count / a_pool->capacity > DAP_STRING_POOL_LOAD_FACTOR) {
        if (!s_rehash(a_pool)) {
            if (a_pool->thread_safe) {
                pthread_mutex_unlock(&a_pool->mutex);
            }
            return NULL;
        }
    }
    
    // Allocate new entry
    dap_string_pool_entry_t *l_entry = (dap_string_pool_entry_t *)dap_arena_alloc(
        a_pool->arena,
        sizeof(dap_string_pool_entry_t)
    );
    
    if (!l_entry) {
        log_it(L_ERROR, "Failed to allocate pool entry");
        if (a_pool->thread_safe) {
            pthread_mutex_unlock(&a_pool->mutex);
        }
        return NULL;
    }
    
    // Allocate string copy
    char *l_str_copy = dap_arena_strndup(a_pool->arena, a_str, a_len);
    if (!l_str_copy) {
        log_it(L_ERROR, "Failed to allocate string copy");
        if (a_pool->thread_safe) {
            pthread_mutex_unlock(&a_pool->mutex);
        }
        return NULL;
    }
    
    // Initialize entry
    l_entry->string = l_str_copy;
    l_entry->length = a_len;
    l_entry->hash = l_hash;
    
    // Insert into hash table
    size_t l_index = s_hash_to_index(l_hash, a_pool->capacity);
    
    if (a_pool->table[l_index] != NULL) {
        a_pool->collision_count++;
    }
    
    l_entry->next = a_pool->table[l_index];
    a_pool->table[l_index] = l_entry;
    a_pool->count++;
    
    if (a_pool->thread_safe) {
        pthread_mutex_unlock(&a_pool->mutex);
    }
    
    return l_entry->string;
}

/**
 * @brief Intern string
 */
const char *dap_string_pool_intern(dap_string_pool_t *a_pool, const char *a_str)
{
    if (!a_str) {
        return NULL;
    }
    
    return dap_string_pool_intern_n(a_pool, a_str, strlen(a_str));
}

/**
 * @brief Check if string with length exists
 */
const char *dap_string_pool_contains_n(const dap_string_pool_t *a_pool, const char *a_str, size_t a_len)
{
    if (!a_pool || !a_str) {
        return NULL;
    }
    
    if (a_pool->thread_safe) {
        pthread_mutex_lock((pthread_mutex_t*)&a_pool->mutex);
    }
    
    uint32_t l_hash = s_hash_fnv1a(a_str, a_len);
    dap_string_pool_entry_t *l_entry = s_find_entry(a_pool, a_str, a_len, l_hash);
    
    if (a_pool->thread_safe) {
        pthread_mutex_unlock((pthread_mutex_t*)&a_pool->mutex);
    }
    
    return l_entry ? l_entry->string : NULL;
}

/**
 * @brief Check if string exists
 */
const char *dap_string_pool_contains(const dap_string_pool_t *a_pool, const char *a_str)
{
    if (!a_str) {
        return NULL;
    }
    
    return dap_string_pool_contains_n(a_pool, a_str, strlen(a_str));
}

/**
 * @brief Clear string pool
 */
void dap_string_pool_clear(dap_string_pool_t *a_pool)
{
    if (!a_pool) {
        return;
    }
    
    if (a_pool->thread_safe) {
        pthread_mutex_lock(&a_pool->mutex);
    }
    
    // Clear hash table
    memset(a_pool->table, 0, a_pool->capacity * sizeof(dap_string_pool_entry_t*));
    
    // Reset arena (frees all strings and entries)
    dap_arena_reset(a_pool->arena);
    
    a_pool->count = 0;
    a_pool->lookup_count = 0;
    a_pool->hit_count = 0;
    a_pool->collision_count = 0;
    
    if (a_pool->thread_safe) {
        pthread_mutex_unlock(&a_pool->mutex);
    }
    
    log_it(L_DEBUG, "String pool cleared");
}

/**
 * @brief Get statistics
 */
void dap_string_pool_get_stats(const dap_string_pool_t *a_pool, dap_string_pool_stats_t *a_stats)
{
    if (!a_pool || !a_stats) {
        return;
    }
    
    if (a_pool->thread_safe) {
        pthread_mutex_lock((pthread_mutex_t*)&a_pool->mutex);
    }
    
    memset(a_stats, 0, sizeof(dap_string_pool_stats_t));
    
    a_stats->string_count = a_pool->count;
    a_stats->lookup_count = a_pool->lookup_count;
    a_stats->hit_count = a_pool->hit_count;
    a_stats->collision_count = a_pool->collision_count;
    
    // Calculate total length and allocated
    dap_arena_stats_t l_arena_stats;
    dap_arena_get_stats(a_pool->arena, &l_arena_stats);
    
    a_stats->total_allocated = l_arena_stats.total_allocated;
    a_stats->total_length = l_arena_stats.total_used;
    
    if (a_pool->thread_safe) {
        pthread_mutex_unlock((pthread_mutex_t*)&a_pool->mutex);
    }
}

/**
 * @brief Free string pool
 */
void dap_string_pool_free(dap_string_pool_t *a_pool)
{
    if (!a_pool) {
        return;
    }
    
    if (a_pool->thread_safe) {
        pthread_mutex_destroy(&a_pool->mutex);
    }
    
    dap_arena_free(a_pool->arena);
    DAP_DELETE(a_pool->table);
    DAP_DELETE(a_pool);
    
    log_it(L_DEBUG, "String pool freed");
}

