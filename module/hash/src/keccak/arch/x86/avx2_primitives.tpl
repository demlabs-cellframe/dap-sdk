// ============================================================================
// AVX2 SIMD Primitives for Keccak (Lane Layout)
// ============================================================================

{{#include PRIM_LIB}}

#define VTYPE VEC_T

static inline uint64_t rol64(uint64_t x, unsigned n) {
    return (n == 0) ? x : ((x << n) | (x >> (64 - n)));
}
#define ROL64(x, n) rol64((x), (n))

#define XOR256(a, b)     VEC_XOR(a, b)
#define CHI_SCALAR(a, b, c) ((a) ^ (~(b) & (c)))

// ============================================================================
// Theta: Column parity computation with AVX2 acceleration
// ============================================================================

#define THETA_COMPUTE_PARITY() \
    uint64_t C[5]; \
    VTYPE col01 = XOR256(VEC_LOAD(A + 0), VEC_LOAD(A + 5)); \
    col01 = XOR256(col01, VEC_LOAD(A + 10)); \
    col01 = XOR256(col01, VEC_LOAD(A + 15)); \
    col01 = XOR256(col01, VEC_LOAD(A + 20)); \
    VEC_STORE(C, col01); \
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
        VTYPE vD01 = VEC_SET_64(D[3], D[2], D[1], D[0]); \
        for (int y = 0; y < 5; y++) { \
            VTYPE row = VEC_LOAD(A + y * 5); \
            row = XOR256(row, vD01); \
            VEC_STORE(A + y * 5, row); \
            A[y * 5 + 4] ^= D[4]; \
        } \
    } while(0)

// ============================================================================
// Chi: Non-linear mixing with SIMD ANDN where beneficial
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
