/**
 * @file dap_hash_fast.h
 * @brief Fast hash functions for hash tables and general use
 *
 * All functions are static inline for maximum performance.
 * These functions are designed to be:
 * 1. Fast - no function call overhead
 * 2. Portable - work on all platforms
 * 3. Reusable - can be used in hash tables, checksums, etc.
 *
 * Available hash functions:
 * - dap_hash_fast() - default fast hash (Jenkins one-at-a-time)
 * - dap_hash_jenkins() - Jenkins lookup3 hash
 * - dap_hash_fnv1a() - FNV-1a hash (good for strings)
 * - dap_hash_murmur3() - MurmurHash3 (fast, good distribution)
 * - dap_hash_xxh32() - xxHash 32-bit (very fast)
 *
 * Note: These are NON-CRYPTOGRAPHIC hashes for hash tables.
 * For cryptographic hashes, use dap_hash.h (SHA3, etc.)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Utility macros
// ============================================================================

#define DAP_HASH_ROTL32(x, r) (((x) << (r)) | ((x) >> (32 - (r))))
#define DAP_HASH_ROTR32(x, r) (((x) >> (r)) | ((x) << (32 - (r))))

// Get unaligned 32-bit value (little-endian)
static inline uint32_t dap_hash_get_unaligned32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ============================================================================
// FNV-1a Hash - simple, fast, good for strings
// ============================================================================

#define DAP_FNV1A_32_INIT  0x811c9dc5U
#define DAP_FNV1A_32_PRIME 0x01000193U

/**
 * @brief FNV-1a 32-bit hash
 * @param key Pointer to data
 * @param len Length in bytes
 * @return 32-bit hash value
 */
static inline uint32_t dap_hash_fnv1a(const void *key, size_t len) {
    const uint8_t *data = (const uint8_t *)key;
    uint32_t hash = DAP_FNV1A_32_INIT;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= DAP_FNV1A_32_PRIME;
    }
    return hash;
}

/**
 * @brief FNV-1a hash for null-terminated strings
 */
static inline uint32_t dap_hash_fnv1a_str(const char *str) {
    uint32_t hash = DAP_FNV1A_32_INIT;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= DAP_FNV1A_32_PRIME;
    }
    return hash;
}

// ============================================================================
// Jenkins One-at-a-time Hash - good balance of speed and distribution
// ============================================================================

/**
 * @brief Jenkins one-at-a-time hash
 * @param key Pointer to data
 * @param len Length in bytes
 * @return 32-bit hash value
 */
static inline uint32_t dap_hash_jenkins_oat(const void *key, size_t len) {
    const uint8_t *data = (const uint8_t *)key;
    uint32_t hash = 0;
    for (size_t i = 0; i < len; i++) {
        hash += data[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

// ============================================================================
// Jenkins lookup3 Hash - fast, high quality (used by uthash default)
// ============================================================================

/**
 * @brief Jenkins lookup3 mix function
 */
#define DAP_HASH_JEN_MIX(a, b, c) do { \
    a -= b; a -= c; a ^= (c >> 13); \
    b -= c; b -= a; b ^= (a << 8);  \
    c -= a; c -= b; c ^= (b >> 13); \
    a -= b; a -= c; a ^= (c >> 12); \
    b -= c; b -= a; b ^= (a << 16); \
    c -= a; c -= b; c ^= (b >> 5);  \
    a -= b; a -= c; a ^= (c >> 3);  \
    b -= c; b -= a; b ^= (a << 10); \
    c -= a; c -= b; c ^= (b >> 15); \
} while (0)

/**
 * @brief Jenkins lookup3 hash (uthash default)
 * @param key Pointer to data
 * @param len Length in bytes
 * @return 32-bit hash value
 */
static inline uint32_t dap_hash_jenkins(const void *key, size_t len) {
    const uint8_t *k = (const uint8_t *)key;
    uint32_t a, b, c;
    size_t length = len;

    // Set up internal state
    a = b = 0x9e3779b9;  // Golden ratio
    c = 0xdeadbeef;      // Initial value

    // Process 12-byte chunks
    while (length >= 12) {
        a += k[0] + ((uint32_t)k[1] << 8) + ((uint32_t)k[2] << 16) + ((uint32_t)k[3] << 24);
        b += k[4] + ((uint32_t)k[5] << 8) + ((uint32_t)k[6] << 16) + ((uint32_t)k[7] << 24);
        c += k[8] + ((uint32_t)k[9] << 8) + ((uint32_t)k[10] << 16) + ((uint32_t)k[11] << 24);
        DAP_HASH_JEN_MIX(a, b, c);
        k += 12;
        length -= 12;
    }

    // Handle remaining bytes
    c += (uint32_t)len;
    switch (length) {
        case 11: c += ((uint32_t)k[10] << 24); /* fall through */
        case 10: c += ((uint32_t)k[9] << 16);  /* fall through */
        case 9:  c += ((uint32_t)k[8] << 8);   /* fall through */
        case 8:  b += ((uint32_t)k[7] << 24);  /* fall through */
        case 7:  b += ((uint32_t)k[6] << 16);  /* fall through */
        case 6:  b += ((uint32_t)k[5] << 8);   /* fall through */
        case 5:  b += k[4];                    /* fall through */
        case 4:  a += ((uint32_t)k[3] << 24);  /* fall through */
        case 3:  a += ((uint32_t)k[2] << 16);  /* fall through */
        case 2:  a += ((uint32_t)k[1] << 8);   /* fall through */
        case 1:  a += k[0];
        default: break;
    }
    DAP_HASH_JEN_MIX(a, b, c);
    return c;
}

// ============================================================================
// MurmurHash3 - very fast, excellent distribution
// ============================================================================

/**
 * @brief MurmurHash3 32-bit
 * @param key Pointer to data
 * @param len Length in bytes
 * @param seed Seed value (use 0 for default)
 * @return 32-bit hash value
 */
static inline uint32_t dap_hash_murmur3(const void *key, size_t len, uint32_t seed) {
    const uint8_t *data = (const uint8_t *)key;
    const size_t nblocks = len / 4;

    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    // Body
    const uint8_t *blocks = data;
    for (size_t i = 0; i < nblocks; i++) {
        uint32_t k1 = dap_hash_get_unaligned32(blocks + i * 4);
        k1 *= c1;
        k1 = DAP_HASH_ROTL32(k1, 15);
        k1 *= c2;
        h1 ^= k1;
        h1 = DAP_HASH_ROTL32(h1, 13);
        h1 = h1 * 5 + 0xe6546b64;
    }

    // Tail
    const uint8_t *tail = data + nblocks * 4;
    uint32_t k1 = 0;
    switch (len & 3) {
        case 3: k1 ^= (uint32_t)tail[2] << 16; /* fall through */
        case 2: k1 ^= (uint32_t)tail[1] << 8;  /* fall through */
        case 1: k1 ^= tail[0];
                k1 *= c1;
                k1 = DAP_HASH_ROTL32(k1, 15);
                k1 *= c2;
                h1 ^= k1;
    }

    // Finalization
    h1 ^= (uint32_t)len;
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;

    return h1;
}

// ============================================================================
// xxHash32 - extremely fast
// ============================================================================

#define DAP_XXH_PRIME32_1 0x9E3779B1U
#define DAP_XXH_PRIME32_2 0x85EBCA77U
#define DAP_XXH_PRIME32_3 0xC2B2AE3DU
#define DAP_XXH_PRIME32_4 0x27D4EB2FU
#define DAP_XXH_PRIME32_5 0x165667B1U

static inline uint32_t dap_xxh32_round(uint32_t acc, uint32_t input) {
    acc += input * DAP_XXH_PRIME32_2;
    acc = DAP_HASH_ROTL32(acc, 13);
    acc *= DAP_XXH_PRIME32_1;
    return acc;
}

/**
 * @brief xxHash 32-bit - extremely fast hash
 * @param key Pointer to data
 * @param len Length in bytes
 * @param seed Seed value (use 0 for default)
 * @return 32-bit hash value
 */
static inline uint32_t dap_hash_xxh32(const void *key, size_t len, uint32_t seed) {
    const uint8_t *p = (const uint8_t *)key;
    const uint8_t *const end = p + len;
    uint32_t h32;

    if (len >= 16) {
        const uint8_t *const limit = end - 16;
        uint32_t v1 = seed + DAP_XXH_PRIME32_1 + DAP_XXH_PRIME32_2;
        uint32_t v2 = seed + DAP_XXH_PRIME32_2;
        uint32_t v3 = seed + 0;
        uint32_t v4 = seed - DAP_XXH_PRIME32_1;

        do {
            v1 = dap_xxh32_round(v1, dap_hash_get_unaligned32(p)); p += 4;
            v2 = dap_xxh32_round(v2, dap_hash_get_unaligned32(p)); p += 4;
            v3 = dap_xxh32_round(v3, dap_hash_get_unaligned32(p)); p += 4;
            v4 = dap_xxh32_round(v4, dap_hash_get_unaligned32(p)); p += 4;
        } while (p <= limit);

        h32 = DAP_HASH_ROTL32(v1, 1) + DAP_HASH_ROTL32(v2, 7) +
              DAP_HASH_ROTL32(v3, 12) + DAP_HASH_ROTL32(v4, 18);
    } else {
        h32 = seed + DAP_XXH_PRIME32_5;
    }

    h32 += (uint32_t)len;

    while (p + 4 <= end) {
        h32 += dap_hash_get_unaligned32(p) * DAP_XXH_PRIME32_3;
        h32 = DAP_HASH_ROTL32(h32, 17) * DAP_XXH_PRIME32_4;
        p += 4;
    }

    while (p < end) {
        h32 += (*p++) * DAP_XXH_PRIME32_5;
        h32 = DAP_HASH_ROTL32(h32, 11) * DAP_XXH_PRIME32_1;
    }

    h32 ^= h32 >> 15;
    h32 *= DAP_XXH_PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= DAP_XXH_PRIME32_3;
    h32 ^= h32 >> 16;

    return h32;
}

// ============================================================================
// Default fast hash (use xxHash for best performance)
// ============================================================================

/**
 * @brief Default fast hash function
 * Uses xxHash32 which is the fastest for general use
 */
static inline uint32_t dap_hash_fast(const void *key, size_t len) {
    return dap_hash_xxh32(key, len, 0);
}

/**
 * @brief Fast hash for null-terminated strings
 */
static inline uint32_t dap_hash_fast_str(const char *str) {
    return dap_hash_fast(str, strlen(str));
}

/**
 * @brief Fast hash for integers (32-bit)
 */
static inline uint32_t dap_hash_fast_int(int32_t value) {
    // Use finalizer from MurmurHash3 for integers
    uint32_t h = (uint32_t)value;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

/**
 * @brief Fast hash for pointers
 */
static inline uint32_t dap_hash_fast_ptr(const void *ptr) {
    uintptr_t p = (uintptr_t)ptr;
#if UINTPTR_MAX == UINT64_MAX
    // 64-bit pointer - mix both halves
    uint32_t h = (uint32_t)(p ^ (p >> 32));
#else
    uint32_t h = (uint32_t)p;
#endif
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

#ifdef __cplusplus
}
#endif
