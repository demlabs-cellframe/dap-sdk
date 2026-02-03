// ============================================================================
// ARM SVE2 SIMD Primitives for Keccak (Plane Layout)
// SVE2 provides scalable vectors (128-2048 bits), predicated operations,
// and EOR3 for 3-way XOR in single instruction
// ============================================================================

typedef svuint64_t VTYPE;

// Predicate for 5-lane operations (active lanes 0-4)
#define PG5 svwhilelt_b64(0, 5)

// XOR operations - SVE2 has EOR3 for 3-way XOR
#define XOR(a, b)       sveor_u64_z(svptrue_b64(), a, b)
#define XOR3(a, b, c)   sveor3_u64(a, b, c)
#define XOR5(a,b,c,d,e) XOR3(XOR3(a, b, c), d, e)

// Chi: a ^ (~b & c) - use BCAX (bit clear and xor) if available, else decompose
// BCAX: result = a ^ ((~b) & c) in single instruction
#define CHI(a, b, c)    svbcax_u64(a, c, b)

// Load/Store with 5-lane predicate
#define LOAD_PLANE(a)   svld1_u64(PG5, a)
#define STORE_PLANE(a, v) svst1_u64(PG5, a, v)
#define LOAD_RC(i)      svld1_u64(svwhilelt_b64(0, 1), s_round_constants + (i))

// Rotation - SVE2 provides variable rotation
#define ROL1(x)         svlsl_n_u64_z(svptrue_b64(), x, 1) | svshr_n_u64_z(svptrue_b64(), x, 63)

// Theta permutation indices
static const uint64_t s_theta_prev_idx[8] = {4, 0, 1, 2, 3, 5, 6, 7};
static const uint64_t s_theta_next_idx[8] = {1, 2, 3, 4, 0, 5, 6, 7};

#define PERMUTE_THETA_PREV(x) svtbl_u64(x, svld1_u64(svptrue_b64(), s_theta_prev_idx))
#define PERMUTE_THETA_NEXT(x) svtbl_u64(x, svld1_u64(svptrue_b64(), s_theta_next_idx))

// Rho rotation offsets per plane
static const uint64_t s_rho_B_off[8] = { 0,  1, 62, 28, 27, 0, 0, 0};
static const uint64_t s_rho_G_off[8] = {36, 44,  6, 55, 20, 0, 0, 0};
static const uint64_t s_rho_K_off[8] = { 3, 10, 43, 25, 39, 0, 0, 0};
static const uint64_t s_rho_M_off[8] = {41, 45, 15, 21,  8, 0, 0, 0};
static const uint64_t s_rho_S_off[8] = {18,  2, 61, 56, 14, 0, 0, 0};

// Variable rotation using SVE2
static inline VTYPE rolv_sve2(VTYPE x, const uint64_t *offsets) {
    svuint64_t off = svld1_u64(svptrue_b64(), offsets);
    svuint64_t off_neg = svsub_u64_z(svptrue_b64(), svdup_u64(64), off);
    return svorr_u64_z(svptrue_b64(),
        svlsl_u64_z(svptrue_b64(), x, off),
        svlsr_u64_z(svptrue_b64(), x, off_neg));
}

#define ROLV_B(x) rolv_sve2(x, s_rho_B_off)
#define ROLV_G(x) rolv_sve2(x, s_rho_G_off)
#define ROLV_K(x) rolv_sve2(x, s_rho_K_off)
#define ROLV_M(x) rolv_sve2(x, s_rho_M_off)
#define ROLV_S(x) rolv_sve2(x, s_rho_S_off)

// Pi step 1: within-plane permutation indices
static const uint64_t s_pi1_B_idx[8] = {0, 3, 1, 4, 2, 5, 6, 7};
static const uint64_t s_pi1_G_idx[8] = {1, 4, 2, 0, 3, 5, 6, 7};
static const uint64_t s_pi1_K_idx[8] = {2, 0, 3, 1, 4, 5, 6, 7};
static const uint64_t s_pi1_M_idx[8] = {3, 1, 4, 2, 0, 5, 6, 7};
static const uint64_t s_pi1_S_idx[8] = {4, 2, 0, 3, 1, 5, 6, 7};

#define PERMUTE_PI1_B(x) svtbl_u64(x, svld1_u64(svptrue_b64(), s_pi1_B_idx))
#define PERMUTE_PI1_G(x) svtbl_u64(x, svld1_u64(svptrue_b64(), s_pi1_G_idx))
#define PERMUTE_PI1_K(x) svtbl_u64(x, svld1_u64(svptrue_b64(), s_pi1_K_idx))
#define PERMUTE_PI1_M(x) svtbl_u64(x, svld1_u64(svptrue_b64(), s_pi1_M_idx))
#define PERMUTE_PI1_S(x) svtbl_u64(x, svld1_u64(svptrue_b64(), s_pi1_S_idx))

// Pi step 2: cross-plane permutation (complex, using zip/unzip)
#define PI2_PERMUTE(B, G, K, M, S, t0, t1, t2, t3, t4) \
do { \
    /* SVE2 cross-plane permutation using ZIP/UZP */ \
    t0 = svzip1_u64(B, G); \
    t1 = svzip1_u64(K, M); \
    t2 = svzip2_u64(B, G); \
    t3 = svzip2_u64(K, M); \
    \
    /* Recompose planes */ \
    VTYPE newB = svuzp1_u64(t0, t1); \
    VTYPE newG = svuzp1_u64(t2, t3); \
    VTYPE newK = svuzp2_u64(t0, t1); \
    VTYPE newM = svuzp2_u64(t2, t3); \
    \
    /* S plane needs special handling */ \
    t4 = svext_u64(S, S, 4); \
    \
    B = newB; G = newG; K = newK; M = newM; \
    /* S remains mostly unchanged with element 4 swapped */ \
} while(0)