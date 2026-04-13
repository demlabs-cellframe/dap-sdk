// ============================================================================
// ARM SVE2 SIMD Primitives for Keccak (Plane Layout)
// ============================================================================

{{#include PRIM_LIB}}

typedef svuint64_t VTYPE;

#define PG5 svwhilelt_b64(0, 5)

#define XOR(a, b)       VEC_XOR_U64_Z(VEC_PTRUE_64(), a, b)
#define XOR3(a, b, c)   VEC_XOR3_U64(a, b, c)
#define XOR5(a,b,c,d,e) XOR3(XOR3(a, b, c), d, e)
#define CHI(a, b, c)    VEC_BCAX_U64(a, b, c)

#define LOAD_PLANE(a)   VEC_LOAD_U64_Z(PG5, a)
#define STORE_PLANE(a, v) VEC_STORE_U64(PG5, a, v)
#define LOAD_RC(i)      VEC_LOAD_U64_Z(svwhilelt_b64(0, 1), s_round_constants + (i))

#define ROL1(x)         svorr_u64_z(VEC_PTRUE_64(), VEC_SHL_U64_Z(VEC_PTRUE_64(), x, 1), VEC_SHR_U64_Z(VEC_PTRUE_64(), x, 63))

// Theta permutation indices
static const uint64_t s_theta_prev_idx[8] = {4, 0, 1, 2, 3, 5, 6, 7};
static const uint64_t s_theta_next_idx[8] = {1, 2, 3, 4, 0, 5, 6, 7};

#define PERMUTE_THETA_PREV(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_theta_prev_idx))
#define PERMUTE_THETA_NEXT(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_theta_next_idx))

// Rho rotation offsets per plane
static const uint64_t s_rho_B_off[8] = { 0,  1, 62, 28, 27, 0, 0, 0};
static const uint64_t s_rho_G_off[8] = {36, 44,  6, 55, 20, 0, 0, 0};
static const uint64_t s_rho_K_off[8] = { 3, 10, 43, 25, 39, 0, 0, 0};
static const uint64_t s_rho_M_off[8] = {41, 45, 15, 21,  8, 0, 0, 0};
static const uint64_t s_rho_S_off[8] = {18,  2, 61, 56, 14, 0, 0, 0};

static inline VTYPE rolv_sve2(VTYPE x, const uint64_t *offsets) {
    svuint64_t off = VEC_LOAD_U64_Z(VEC_PTRUE_64(), offsets);
    svuint64_t off_neg = VEC_SUB_U64_Z(VEC_PTRUE_64(), VEC_SET1_U64(64), off);
    return svorr_u64_z(VEC_PTRUE_64(),
        svlsl_u64_z(VEC_PTRUE_64(), x, off),
        svlsr_u64_z(VEC_PTRUE_64(), x, off_neg));
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

#define PERMUTE_PI1_B(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_pi1_B_idx))
#define PERMUTE_PI1_G(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_pi1_G_idx))
#define PERMUTE_PI1_K(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_pi1_K_idx))
#define PERMUTE_PI1_M(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_pi1_M_idx))
#define PERMUTE_PI1_S(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_pi1_S_idx))

// Pi step 2: cross-plane permutation (complex, using zip/unzip)
#define PI2_PERMUTE(B, G, K, M, S, t0, t1, t2, t3, t4) \
do { \
    t0 = VEC_ZIP1_U64(B, G); \
    t1 = VEC_ZIP1_U64(K, M); \
    t2 = VEC_ZIP2_U64(B, G); \
    t3 = VEC_ZIP2_U64(K, M); \
    \
    VTYPE newB = VEC_UZP1_U64(t0, t1); \
    VTYPE newG = VEC_UZP1_U64(t2, t3); \
    VTYPE newK = VEC_UZP2_U64(t0, t1); \
    VTYPE newM = VEC_UZP2_U64(t2, t3); \
    \
    t4 = VEC_EXT_U64(S, S, 4); \
    \
    B = newB; G = newG; K = newK; M = newM; \
} while(0)
