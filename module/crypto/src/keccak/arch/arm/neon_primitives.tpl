// ============================================================================
// ARM NEON SIMD Primitives for Keccak (Lane Layout)
// ============================================================================

typedef uint64x2_t VTYPE;

// 64-bit rotation (emulated on NEON, native on ARMv8.1+)
// 64-bit rotation (safe for n=0..63)
static inline uint64_t rol64(uint64_t x, unsigned n) {
    return (n == 0) ? x : ((x << n) | (x >> (64 - n)));
}
#define ROL64(x, n) rol64((x), (n))

// XOR operations
#define XOR_NEON(a, b)  veorq_u64(a, b)

// Chi: a ^ (~b & c) using BIC (bit clear: ~b & c in single instruction)
#define CHI_SCALAR(a, b, c) ((a) ^ (~(b) & (c)))

// ============================================================================
// Theta: Column parity computation with NEON acceleration
// ============================================================================

#define THETA_COMPUTE_PARITY() \
    uint64_t C[5]; \
    VTYPE c01 = vld1q_u64(A + 0); \
    c01 = XOR_NEON(c01, vld1q_u64(A + 5)); \
    c01 = XOR_NEON(c01, vld1q_u64(A + 10)); \
    c01 = XOR_NEON(c01, vld1q_u64(A + 15)); \
    c01 = XOR_NEON(c01, vld1q_u64(A + 20)); \
    \
    VTYPE c23 = vld1q_u64(A + 2); \
    c23 = XOR_NEON(c23, vld1q_u64(A + 7)); \
    c23 = XOR_NEON(c23, vld1q_u64(A + 12)); \
    c23 = XOR_NEON(c23, vld1q_u64(A + 17)); \
    c23 = XOR_NEON(c23, vld1q_u64(A + 22)); \
    \
    vst1q_u64(C + 0, c01); \
    vst1q_u64(C + 2, c23); \
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
        VTYPE vD01 = vcombine_u64(vdup_n_u64(D[0]), vdup_n_u64(D[1])); \
        VTYPE vD23 = vcombine_u64(vdup_n_u64(D[2]), vdup_n_u64(D[3])); \
        for (int y = 0; y < 5; y++) { \
            VTYPE row01 = vld1q_u64(A + y * 5); \
            VTYPE row23 = vld1q_u64(A + y * 5 + 2); \
            row01 = XOR_NEON(row01, vD01); \
            row23 = XOR_NEON(row23, vD23); \
            vst1q_u64(A + y * 5, row01); \
            vst1q_u64(A + y * 5 + 2, row23); \
            A[y * 5 + 4] ^= D[4]; \
        } \
    } while(0)

// ============================================================================
// Chi: Non-linear mixing (BIC available but wrap-around complicates SIMD)
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