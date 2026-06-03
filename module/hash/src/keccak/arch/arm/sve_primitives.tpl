// ============================================================================
// ARM SVE SIMD Primitives for Keccak (Lane Layout)
// ============================================================================

{{#include PRIM_LIB}}

typedef svuint64_t VTYPE;

static inline uint64_t rol64(uint64_t x, unsigned n) {
    return (n == 0) ? x : ((x << n) | (x >> (64 - n)));
}
#define ROL64(x, n) rol64((x), (n))

#define CHI_SCALAR(a, b, c) ((a) ^ (~(b) & (c)))

// ============================================================================
// Theta: Column parity computation with SVE acceleration
// ============================================================================

#define THETA_COMPUTE_PARITY() \
    uint64_t C[5]; \
    svbool_t pg2 = VEC_WHILELT_64(0, 2); \
    \
    svuint64_t c01 = VEC_LOAD_U64_Z(pg2, A + 0); \
    c01 = VEC_XOR_U64_Z(pg2, c01, VEC_LOAD_U64_Z(pg2, A + 5)); \
    c01 = VEC_XOR_U64_Z(pg2, c01, VEC_LOAD_U64_Z(pg2, A + 10)); \
    c01 = VEC_XOR_U64_Z(pg2, c01, VEC_LOAD_U64_Z(pg2, A + 15)); \
    c01 = VEC_XOR_U64_Z(pg2, c01, VEC_LOAD_U64_Z(pg2, A + 20)); \
    \
    svuint64_t c23 = VEC_LOAD_U64_Z(pg2, A + 2); \
    c23 = VEC_XOR_U64_Z(pg2, c23, VEC_LOAD_U64_Z(pg2, A + 7)); \
    c23 = VEC_XOR_U64_Z(pg2, c23, VEC_LOAD_U64_Z(pg2, A + 12)); \
    c23 = VEC_XOR_U64_Z(pg2, c23, VEC_LOAD_U64_Z(pg2, A + 17)); \
    c23 = VEC_XOR_U64_Z(pg2, c23, VEC_LOAD_U64_Z(pg2, A + 22)); \
    \
    VEC_STORE_U64(pg2, C + 0, c01); \
    VEC_STORE_U64(pg2, C + 2, c23); \
    C[4] = A[4] ^ A[9] ^ A[14] ^ A[19] ^ A[24]; \
    \
    uint64_t D[5]; \
    D[0] = C[4] ^ ROL64(C[1], 1); \
    D[1] = C[0] ^ ROL64(C[2], 1); \
    D[2] = C[1] ^ ROL64(C[3], 1); \
    D[3] = C[2] ^ ROL64(C[4], 1); \
    D[4] = C[3] ^ ROL64(C[0], 1)

#define THETA_APPLY_D() \
    do { \
        svbool_t pg2 = VEC_WHILELT_64(0, 2); \
        svuint64_t vD01 = VEC_LOAD_U64_Z(pg2, D); \
        svuint64_t vD23 = VEC_LOAD_U64_Z(pg2, D + 2); \
        for (int y = 0; y < 5; y++) { \
            svuint64_t row01 = VEC_LOAD_U64_Z(pg2, A + y * 5); \
            svuint64_t row23 = VEC_LOAD_U64_Z(pg2, A + y * 5 + 2); \
            row01 = VEC_XOR_U64_Z(pg2, row01, vD01); \
            row23 = VEC_XOR_U64_Z(pg2, row23, vD23); \
            VEC_STORE_U64(pg2, A + y * 5, row01); \
            VEC_STORE_U64(pg2, A + y * 5 + 2, row23); \
            A[y * 5 + 4] ^= D[4]; \
        } \
    } while(0)

// ============================================================================
// Chi: Non-linear mixing
// ============================================================================

#define CHI_ROWS() \
    do { \
        for (int y = 0; y < 5; y++) { \
            int base = y * 5; \
            A[base + 0] = CHI_SCALAR(B[base + 0], B[base + 1], B[base + 2]); \
            A[base + 1] = CHI_SCALAR(B[base + 1], B[base + 2], B[base + 3]); \
            A[base + 2] = CHI_SCALAR(B[base + 2], B[base + 3], B[base + 4]); \
            A[base + 3] = CHI_SCALAR(B[base + 3], B[base + 4], B[base + 0]); \
            A[base + 4] = CHI_SCALAR(B[base + 4], B[base + 0], B[base + 1]); \
        } \
    } while(0)
