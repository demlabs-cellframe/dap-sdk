// ============================================================================
// SSE2 SIMD Primitives for Keccak (Lane Layout)
// ============================================================================

typedef __m128i VTYPE;

// 64-bit rotation (emulated, safe for n=0..63)
static inline uint64_t rol64(uint64_t x, unsigned n) {
    return (n == 0) ? x : ((x << n) | (x >> (64 - n)));
}
#define ROL64(x, n) rol64((x), (n))

// XOR operations
#define XOR128(a, b)     _mm_xor_si128(a, b)

// Chi: a ^ (~b & c)
#define CHI_SCALAR(a, b, c) ((a) ^ (~(b) & (c)))

// ============================================================================
// Theta: Column parity computation with SSE2 acceleration
// ============================================================================

#define THETA_COMPUTE_PARITY() \
    uint64_t C[5]; \
    VTYPE c01 = XOR128(_mm_loadu_si128((const VTYPE*)(A + 0)), \
                       _mm_loadu_si128((const VTYPE*)(A + 5))); \
    c01 = XOR128(c01, _mm_loadu_si128((const VTYPE*)(A + 10))); \
    c01 = XOR128(c01, _mm_loadu_si128((const VTYPE*)(A + 15))); \
    c01 = XOR128(c01, _mm_loadu_si128((const VTYPE*)(A + 20))); \
    \
    VTYPE c23 = XOR128(_mm_loadu_si128((const VTYPE*)(A + 2)), \
                       _mm_loadu_si128((const VTYPE*)(A + 7))); \
    c23 = XOR128(c23, _mm_loadu_si128((const VTYPE*)(A + 12))); \
    c23 = XOR128(c23, _mm_loadu_si128((const VTYPE*)(A + 17))); \
    c23 = XOR128(c23, _mm_loadu_si128((const VTYPE*)(A + 22))); \
    \
    _mm_storeu_si128((VTYPE*)(C + 0), c01); \
    _mm_storeu_si128((VTYPE*)(C + 2), c23); \
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
        VTYPE vD01 = _mm_set_epi64x(D[1], D[0]); \
        VTYPE vD23 = _mm_set_epi64x(D[3], D[2]); \
        for (int y = 0; y < 5; y++) { \
            VTYPE *row01 = (VTYPE*)(A + y * 5); \
            VTYPE *row23 = (VTYPE*)(A + y * 5 + 2); \
            _mm_storeu_si128(row01, XOR128(_mm_loadu_si128(row01), vD01)); \
            _mm_storeu_si128(row23, XOR128(_mm_loadu_si128(row23), vD23)); \
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