{{! ================================================================ }}
{{! ML-KEM shared SIMD reduction primitives                         }}
{{! ================================================================ }}
{{! Montgomery fqmul and Barrett reduction for VEC_T and HVEC_T.    }}
{{! Included by both NTT and poly SIMD templates.                   }}
{{! Expects: VEC_T, VEC_MULLO16, VEC_MULHI16, VEC_SUB16, VEC_SRAI16}}
{{!          VEC_SET1_16 already defined via PRIMITIVES_FILE.        }}
{{! ================================================================ }}

#ifndef MLKEM_Q
#define MLKEM_Q     3329
#endif
#ifndef MLKEM_QINV
#define MLKEM_QINV  ((int16_t)-3327)
#endif
#ifndef MLKEM_N
#define MLKEM_N     256
#endif
#ifndef MLKEM_BARRETT_V
#define MLKEM_BARRETT_V 20159
#endif

{{TARGET_ATTR}}
static inline VEC_T s_fqmul(VEC_T a_a, VEC_T a_b)
{
    const VEC_T l_qinv = VEC_SET1_16(MLKEM_QINV);
    const VEC_T l_q    = VEC_SET1_16(MLKEM_Q);
    VEC_T l_lo  = VEC_MULLO16(a_a, a_b);
    VEC_T l_hi  = VEC_MULHI16(a_a, a_b);
    VEC_T l_u   = VEC_MULLO16(l_lo, l_qinv);
    VEC_T l_uq  = VEC_MULHI16(l_u, l_q);
    return VEC_SUB16(l_hi, l_uq);
}

{{TARGET_ATTR}}
static inline VEC_T s_fqmul_ext(VEC_T a_a, VEC_T a_b, VEC_T a_qinv, VEC_T a_q)
{
    VEC_T l_lo = VEC_MULLO16(a_a, a_b);
    VEC_T l_hi = VEC_MULHI16(a_a, a_b);
    VEC_T l_u  = VEC_MULLO16(l_lo, a_qinv);
    VEC_T l_uq = VEC_MULHI16(l_u, a_q);
    return VEC_SUB16(l_hi, l_uq);
}

{{TARGET_ATTR}}
static inline VEC_T s_barrett_reduce(VEC_T a_val)
{
    const VEC_T l_v = VEC_SET1_16(MLKEM_BARRETT_V);
    const VEC_T l_q = VEC_SET1_16(MLKEM_Q);
    VEC_T l_bt = VEC_MULHI16(l_v, a_val);
    l_bt = VEC_SRAI16(l_bt, 10);
    l_bt = VEC_MULLO16(l_bt, l_q);
    return VEC_SUB16(a_val, l_bt);
}

static inline int16_t s_fqmul_scalar(int16_t a, int16_t b)
{
    int32_t t = (int32_t)a * b;
    int16_t u = (int16_t)t * MLKEM_QINV;
    return (int16_t)((t - (int32_t)u * MLKEM_Q) >> 16);
}

static inline int16_t s_barrett_reduce_scalar(int16_t a)
{
    int16_t t = (int16_t)((int32_t)MLKEM_BARRETT_V * a >> 26);
    return a - t * MLKEM_Q;
}

#ifdef HVEC_LANES
{{TARGET_ATTR}}
static inline HVEC_T s_fqmul_hvec(HVEC_T a_a, HVEC_T a_b)
{
    const HVEC_T l_qinv = HVEC_SET1_16(MLKEM_QINV);
    const HVEC_T l_q    = HVEC_SET1_16(MLKEM_Q);
    HVEC_T l_lo = HVEC_MULLO16(a_a, a_b);
    HVEC_T l_hi = HVEC_MULHI16(a_a, a_b);
    HVEC_T l_u  = HVEC_MULLO16(l_lo, l_qinv);
    HVEC_T l_uq = HVEC_MULHI16(l_u, l_q);
    return HVEC_SUB16(l_hi, l_uq);
}

{{TARGET_ATTR}}
static inline HVEC_T s_barrett_reduce_hvec(HVEC_T a_val)
{
    const HVEC_T l_v = HVEC_SET1_16(MLKEM_BARRETT_V);
    const HVEC_T l_q = HVEC_SET1_16(MLKEM_Q);
    HVEC_T l_bt = HVEC_MULHI16(l_v, a_val);
    l_bt = HVEC_SRAI16(l_bt, 10);
    l_bt = HVEC_MULLO16(l_bt, l_q);
    return HVEC_SUB16(a_val, l_bt);
}
#endif
