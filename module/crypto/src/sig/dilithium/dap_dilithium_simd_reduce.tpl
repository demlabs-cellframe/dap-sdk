{{! ================================================================ }}
{{! Dilithium/ML-DSA shared SIMD reduction primitives               }}
{{! ================================================================ }}
{{! Barrett-like reduction, csubq, freeze for int32 Dilithium Q.    }}
{{! Included by poly_simd and NTT SIMD templates.                   }}
{{! Expects: VEC_T, VEC_SET1_32, VEC_AND, VEC_SRL32, VEC_SLL32,    }}
{{!          VEC_ADD32, VEC_SUB32, VEC_SRAI32, VEC_LOAD, VEC_STORE  }}
{{!          already defined via PRIMITIVES_FILE.                    }}
{{! ================================================================ }}

#define DIL_N    256
#define DIL_Q    8380417
#define DIL_QINV 4236238847U
#define DIL_MONT 4193792U

{{TARGET_ATTR}}
static inline void s_poly_reduce_vec(int32_t *a_coeffs)
{
    const VEC_T l_mask23 = VEC_SET1_32(0x7FFFFF);
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_T lo = VEC_AND(v, l_mask23);
        VEC_T hi = VEC_SRLI32(v, 23);
        VEC_STORE(a_coeffs + i,
            VEC_ADD32(lo, VEC_SUB32(VEC_SLLI32(hi, 13), hi)));
    }
}

{{TARGET_ATTR}}
static inline void s_poly_csubq_vec(int32_t *a_coeffs)
{
    const VEC_T l_q = VEC_SET1_32(DIL_Q);
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_T t = VEC_SUB32(v, l_q);
        VEC_T m = VEC_SRAI32(t, 31);
        VEC_STORE(a_coeffs + i, VEC_ADD32(t, VEC_AND(m, l_q)));
    }
}

{{TARGET_ATTR}}
static inline void s_poly_freeze_vec(int32_t *a_coeffs)
{
    s_poly_reduce_vec(a_coeffs);
    s_poly_csubq_vec(a_coeffs);
}

{{TARGET_ATTR}}
static inline void s_poly_add_vec(int32_t * restrict a_r,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T vb = VEC_LOAD(a_b + i);
        VEC_STORE(a_r + i, VEC_ADD32(va, vb));
    }
}

{{TARGET_ATTR}}
static inline void s_poly_sub_vec(int32_t * restrict a_r,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    const VEC_T l_2q = VEC_SET1_32(2 * DIL_Q);
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T vb = VEC_LOAD(a_b + i);
        VEC_STORE(a_r + i, VEC_SUB32(VEC_ADD32(va, l_2q), vb));
    }
}
