// ============================================================================
// ARM SVE SIMD Primitives for Keccak (Lane Layout)
// SVE provides scalable vectors (128-2048 bits), predicated operations
// Unlike SVE2, no EOR3/BCAX - use standard operations
// ============================================================================

typedef svuint64_t VTYPE;

// 64-bit rotation (emulated)
// 64-bit rotation (safe for n=0..63)
static inline uint64_t rol64(uint64_t x, unsigned n) {
    return (n == 0) ? x : ((x << n) | (x >> (64 - n)));
}
#define ROL64(x, n) rol64((x), (n))

// Chi: a ^ (~b & c)
#define CHI_SCALAR(a, b, c) ((a) ^ (~(b) & (c)))

// ============================================================================
// Theta: Column parity computation with SVE acceleration
// ============================================================================

#define THETA_COMPUTE_PARITY() \
    uint64_t C[5]; \
    svbool_t pg2 = svwhilelt_b64(0, 2); \
    \
    svuint64_t c01 = svld1_u64(pg2, A + 0); \
    c01 = sveor_u64_z(pg2, c01, svld1_u64(pg2, A + 5)); \
    c01 = sveor_u64_z(pg2, c01, svld1_u64(pg2, A + 10)); \
    c01 = sveor_u64_z(pg2, c01, svld1_u64(pg2, A + 15)); \
    c01 = sveor_u64_z(pg2, c01, svld1_u64(pg2, A + 20)); \
    \
    svuint64_t c23 = svld1_u64(pg2, A + 2); \
    c23 = sveor_u64_z(pg2, c23, svld1_u64(pg2, A + 7)); \
    c23 = sveor_u64_z(pg2, c23, svld1_u64(pg2, A + 12)); \
    c23 = sveor_u64_z(pg2, c23, svld1_u64(pg2, A + 17)); \
    c23 = sveor_u64_z(pg2, c23, svld1_u64(pg2, A + 22)); \
    \
    svst1_u64(pg2, C + 0, c01); \
    svst1_u64(pg2, C + 2, c23); \
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
        svbool_t pg2 = svwhilelt_b64(0, 2); \
        svuint64_t vD01 = svld1_u64(pg2, D); \
        svuint64_t vD23 = svld1_u64(pg2, D + 2); \
        for (int y = 0; y < 5; y++) { \
            svuint64_t row01 = svld1_u64(pg2, A + y * 5); \
            svuint64_t row23 = svld1_u64(pg2, A + y * 5 + 2); \
            row01 = sveor_u64_z(pg2, row01, vD01); \
            row23 = sveor_u64_z(pg2, row23, vD23); \
            svst1_u64(pg2, A + y * 5, row01); \
            svst1_u64(pg2, A + y * 5 + 2, row23); \
            A[y * 5 + 4] ^= D[4]; \
        } \
    } while(0)

// ============================================================================
// Chi: Non-linear mixing
// SVE has BIC but wrap-around in row complicates vectorization
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