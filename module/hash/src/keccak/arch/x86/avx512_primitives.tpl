// ============================================================================
// AVX-512 SIMD Primitives for Keccak (Plane Layout)
// ============================================================================

{{#include PRIM_LIB}}

#define VTYPE VEC_T

#define XOR(a, b)       VEC_XOR(a, b)
#define XOR3(a, b, c)   VEC_XOR3(a, b, c)
#define XOR5(a,b,c,d,e) XOR3(XOR3(a, b, c), d, e)
#define CHI(a, b, c)    VEC_CHI(a, b, c)

#define LOAD_PLANE(a)   VEC_MASKZ_LOAD_64(0x1F, a)
#define STORE_PLANE(a, v) VEC_MASK_STORE_64(a, 0x1F, v)
#define LOAD_RC(i)      VEC_MASKZ_LOAD_64(0x01, s_round_constants + (i))

#define ROL1(x)         VEC_ROL64(x, 1)

// Permutation indices for Theta step
static const VTYPE s_theta_prev = {4, 0, 1, 2, 3, 5, 6, 7};
static const VTYPE s_theta_next = {1, 2, 3, 4, 0, 5, 6, 7};

#define PERMUTE_THETA_PREV(x) VEC_PERMUTEXVAR_64(s_theta_prev, x)
#define PERMUTE_THETA_NEXT(x) VEC_PERMUTEXVAR_64(s_theta_next, x)

// Rho rotation offsets per plane
static const VTYPE s_rho_B = { 0,  1, 62, 28, 27, 0, 0, 0};
static const VTYPE s_rho_G = {36, 44,  6, 55, 20, 0, 0, 0};
static const VTYPE s_rho_K = { 3, 10, 43, 25, 39, 0, 0, 0};
static const VTYPE s_rho_M = {41, 45, 15, 21,  8, 0, 0, 0};
static const VTYPE s_rho_S = {18,  2, 61, 56, 14, 0, 0, 0};

#define ROLV_B(x) VEC_ROLV64(x, s_rho_B)
#define ROLV_G(x) VEC_ROLV64(x, s_rho_G)
#define ROLV_K(x) VEC_ROLV64(x, s_rho_K)
#define ROLV_M(x) VEC_ROLV64(x, s_rho_M)
#define ROLV_S(x) VEC_ROLV64(x, s_rho_S)

// Pi step 1: within-plane permutation indices
static const VTYPE s_pi1_B = {0, 3, 1, 4, 2, 5, 6, 7};
static const VTYPE s_pi1_G = {1, 4, 2, 0, 3, 5, 6, 7};
static const VTYPE s_pi1_K = {2, 0, 3, 1, 4, 5, 6, 7};
static const VTYPE s_pi1_M = {3, 1, 4, 2, 0, 5, 6, 7};
static const VTYPE s_pi1_S = {4, 2, 0, 3, 1, 5, 6, 7};

#define PERMUTE_PI1_B(x) VEC_PERMUTEXVAR_64(s_pi1_B, x)
#define PERMUTE_PI1_G(x) VEC_PERMUTEXVAR_64(s_pi1_G, x)
#define PERMUTE_PI1_K(x) VEC_PERMUTEXVAR_64(s_pi1_K, x)
#define PERMUTE_PI1_M(x) VEC_PERMUTEXVAR_64(s_pi1_M, x)
#define PERMUTE_PI1_S(x) VEC_PERMUTEXVAR_64(s_pi1_S, x)

// Pi step 2: cross-plane permutation indices
static const VTYPE s_pi2_S1 = {0, 1, 2, 3, 4, 5, 0+8, 2+8};
static const VTYPE s_pi2_S2 = {0, 1, 2, 3, 4, 5, 1+8, 3+8};
static const VTYPE s_pi2_BG = {0, 1, 0+8, 1+8, 6, 5, 6, 7};
static const VTYPE s_pi2_KM = {2, 3, 2+8, 3+8, 7, 5, 6, 7};
static const VTYPE s_pi2_S3 = {4, 5, 4+8, 5+8, 4, 5, 6, 7};

#define PI2_PERMUTE(B, G, K, M, S, t0, t1, t2, t3, t4) \
do { \
    t0 = VEC_UNPACKLO_64(B, G); t1 = VEC_UNPACKLO_64(K, M); \
    t0 = VEC_PERMUTEX2VAR_64(t0, s_pi2_S1, S); \
    t2 = VEC_UNPACKHI_64(B, G); t3 = VEC_UNPACKHI_64(K, M); \
    t2 = VEC_PERMUTEX2VAR_64(t2, s_pi2_S2, S); \
    B = VEC_PERMUTEX2VAR_64(t0, s_pi2_BG, t1); \
    G = VEC_PERMUTEX2VAR_64(t2, s_pi2_BG, t3); \
    K = VEC_PERMUTEX2VAR_64(t0, s_pi2_KM, t1); \
    M = VEC_PERMUTEX2VAR_64(t2, s_pi2_KM, t3); \
    t0 = VEC_PERMUTEX2VAR_64(t0, s_pi2_S3, t1); \
    S = VEC_MASK_BLEND_64(0x10, t0, S); \
} while(0)
