/*
 * Dilithium polynomial arithmetic ops — AVX-512 assembly.
 * Generated from dap_dilithium_poly_ops_avx512.S.tpl by dap_tpl.
 *
 * 16 int32_t coefficients per ZMM iteration.
 *
 * Contains: reduce, csubq, freeze, add, sub, neg, shiftl,
 *           power2round, chknorm, decompose, make_hint, use_hint
 *           (legacy gamma2=(Q-1)/32).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#define DIL_N       256
#define DIL_Q       8380417
#define COEFF_BYTES (DIL_N * 4)

{{#include ASM_MACROS}}

.text

/* ================================================================
 * poly_reduce
 * ================================================================ */
.globl dap_dilithium_poly_reduce_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_poly_reduce_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_poly_reduce_{{ARCH_LOWER}}:
    movl    $0x7FFFFF, %eax
    leaq    COEFF_BYTES(%rdi), %rdx
    vpbroadcastd %eax, %zmm3
    .p2align 4
.L_reduce512_loop:
    vmovdqu64 (%rdi), %zmm1
    vpsrld  $23, %zmm1, %zmm2
    vpandd  %zmm3, %zmm1, %zmm1
    vpslld  $13, %zmm2, %zmm0
    vpsubd  %zmm2, %zmm0, %zmm0
    vpaddd  %zmm1, %zmm0, %zmm0
    vmovdqu64 %zmm0, (%rdi)
    addq    $64, %rdi
    cmpq    %rdi, %rdx
    jne     .L_reduce512_loop
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_poly_reduce_{{ARCH_LOWER}})


/* ================================================================
 * poly_csubq
 * ================================================================ */
.globl dap_dilithium_poly_csubq_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_poly_csubq_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_poly_csubq_{{ARCH_LOWER}}:
    movl    $(-DIL_Q), %eax
    leaq    COEFF_BYTES(%rdi), %rdx
    vpbroadcastd %eax, %zmm3
    movl    $DIL_Q, %eax
    vpbroadcastd %eax, %zmm2
    .p2align 4
.L_csubq512_loop:
    vpaddd  (%rdi), %zmm3, %zmm0
    vpsrad  $31, %zmm0, %zmm1
    vpandd  %zmm2, %zmm1, %zmm1
    vpaddd  %zmm1, %zmm0, %zmm0
    vmovdqu64 %zmm0, (%rdi)
    addq    $64, %rdi
    cmpq    %rdi, %rdx
    jne     .L_csubq512_loop
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_poly_csubq_{{ARCH_LOWER}})


/* ================================================================
 * poly_freeze: reduce then csubq
 * ================================================================ */
.globl dap_dilithium_poly_freeze_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_poly_freeze_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_poly_freeze_{{ARCH_LOWER}}:
    movl    $0x7FFFFF, %ecx
    leaq    COEFF_BYTES(%rdi), %rdx
    movq    %rdi, %rax
    vpbroadcastd %ecx, %zmm3
    .p2align 4
.L_freeze512_reduce:
    vmovdqu64 (%rax), %zmm1
    vpsrld  $23, %zmm1, %zmm2
    vpandd  %zmm3, %zmm1, %zmm1
    vpslld  $13, %zmm2, %zmm0
    vpsubd  %zmm2, %zmm0, %zmm0
    vpaddd  %zmm1, %zmm0, %zmm0
    vmovdqu64 %zmm0, (%rax)
    addq    $64, %rax
    cmpq    %rax, %rdx
    jne     .L_freeze512_reduce

    movl    $(-DIL_Q), %eax
    vpbroadcastd %eax, %zmm3
    movl    $DIL_Q, %eax
    vpbroadcastd %eax, %zmm2
    .p2align 4
.L_freeze512_csubq:
    vpaddd  (%rdi), %zmm3, %zmm0
    vpsrad  $31, %zmm0, %zmm1
    vpandd  %zmm2, %zmm1, %zmm1
    vpaddd  %zmm1, %zmm0, %zmm0
    vmovdqu64 %zmm0, (%rdi)
    addq    $64, %rdi
    cmpq    %rdi, %rdx
    jne     .L_freeze512_csubq
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_poly_freeze_{{ARCH_LOWER}})


/* ================================================================
 * poly_add
 * ================================================================ */
.globl dap_dilithium_poly_add_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_poly_add_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_poly_add_{{ARCH_LOWER}}:
    xorl    %eax, %eax
    .p2align 4
.L_add512_loop:
    vmovdqu64 (%rdx,%rax), %zmm0
    vpaddd  (%rsi,%rax), %zmm0, %zmm0
    vmovdqu64 %zmm0, (%rdi,%rax)
    addq    $64, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_add512_loop
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_poly_add_{{ARCH_LOWER}})


/* ================================================================
 * poly_sub
 * ================================================================ */
.globl dap_dilithium_poly_sub_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_poly_sub_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_poly_sub_{{ARCH_LOWER}}:
    movl    $(2 * DIL_Q), %ecx
    xorl    %eax, %eax
    vpbroadcastd %ecx, %zmm1
    .p2align 4
.L_sub512_loop:
    vpaddd  (%rsi,%rax), %zmm1, %zmm0
    vpsubd  (%rdx,%rax), %zmm0, %zmm0
    vmovdqu64 %zmm0, (%rdi,%rax)
    addq    $64, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_sub512_loop
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_poly_sub_{{ARCH_LOWER}})


/* ================================================================
 * poly_neg
 * ================================================================ */
.globl dap_dilithium_poly_neg_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_poly_neg_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_poly_neg_{{ARCH_LOWER}}:
    movl    $DIL_Q, %eax
    leaq    COEFF_BYTES(%rdi), %rdx
    vpbroadcastd %eax, %zmm1
    .p2align 4
.L_neg512_loop:
    vpsubd  (%rdi), %zmm1, %zmm0
    vmovdqu64 %zmm0, (%rdi)
    addq    $64, %rdi
    cmpq    %rdi, %rdx
    jne     .L_neg512_loop
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_poly_neg_{{ARCH_LOWER}})


/* ================================================================
 * poly_shiftl
 * ================================================================ */
.globl dap_dilithium_poly_shiftl_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_poly_shiftl_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_poly_shiftl_{{ARCH_LOWER}}:
    leaq    COEFF_BYTES(%rdi), %rax
    vmovd   %esi, %xmm1
    .p2align 4
.L_shiftl512_loop:
    vmovdqu64 (%rdi), %zmm0
    vpslld  %xmm1, %zmm0, %zmm0
    vmovdqu64 %zmm0, (%rdi)
    addq    $64, %rdi
    cmpq    %rdi, %rax
    jne     .L_shiftl512_loop
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_poly_shiftl_{{ARCH_LOWER}})


/* ================================================================
 * poly_power2round (legacy D=14)
 * ================================================================ */
.globl dap_dilithium_poly_power2round_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_poly_power2round_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_poly_power2round_{{ARCH_LOWER}}:
    movl    $0x3FFF, %ecx
    xorl    %eax, %eax
    vpbroadcastd %ecx, %zmm7
    movl    $(-8193), %ecx
    vpbroadcastd %ecx, %zmm6
    movl    $16384, %ecx
    vpbroadcastd %ecx, %zmm5
    movl    $(DIL_Q - 8191), %ecx
    vpbroadcastd %ecx, %zmm4
    movl    $8191, %ecx
    vpbroadcastd %ecx, %zmm3
    .p2align 4
.L_p2r512_loop:
    vmovdqu64 (%rdx,%rax), %zmm0
    vpandd  %zmm7, %zmm0, %zmm1
    vpaddd  %zmm3, %zmm0, %zmm0
    vpaddd  %zmm6, %zmm1, %zmm1
    vpsrad  $31, %zmm1, %zmm2
    vpandd  %zmm5, %zmm2, %zmm2
    vpaddd  %zmm2, %zmm1, %zmm1
    vpsubd  %zmm1, %zmm0, %zmm0
    vpaddd  %zmm4, %zmm1, %zmm2
    vpsrld  $14, %zmm0, %zmm0
    vmovdqu64 %zmm2, (%rsi,%rax)
    vmovdqu64 %zmm0, (%rdi,%rax)
    addq    $64, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_p2r512_loop
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_poly_power2round_{{ARCH_LOWER}})


/* ================================================================
 * poly_chknorm
 * int dap_dilithium_poly_chknorm_{{ARCH_LOWER}}(const int32_t *, int32_t bound);
 * ================================================================ */
.globl dap_dilithium_poly_chknorm_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_poly_chknorm_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_poly_chknorm_{{ARCH_LOWER}}:
    movl    $((DIL_Q - 1) / 2), %edx
    subl    $1, %esi
    vpbroadcastd %edx, %zmm0
    vpbroadcastd %esi, %zmm1           /* bound - 1 */

    xorl    %eax, %eax
    .p2align 4
.L_chknorm512_loop:
    vpsubd  (%rdi,%rax), %zmm0, %zmm3  /* t = (Q-1)/2 - coeffs */
    vpsrad  $31, %zmm3, %zmm2          /* sign(t) */
    vpxord  %zmm3, %zmm2, %zmm2        /* ones' complement abs */
    vpsubd  %zmm2, %zmm0, %zmm2        /* (Q-1)/2 - abs_t */
    vpcmpd  $6, %zmm1, %zmm2, %k0      /* result > (bound-1) → NLE */
    vpmovm2d %k0, %zmm2
    vptestmd %zmm2, %zmm2, %k0
    kortestw %k0, %k0
    jne     .L_chknorm512_fail
    addq    $64, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_chknorm512_loop

    xorl    %eax, %eax
    vzeroupper
    ret

.L_chknorm512_fail:
    movl    $1, %eax
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_poly_chknorm_{{ARCH_LOWER}})


/* ================================================================
 * poly_decompose (legacy, gamma2=(Q-1)/32)
 * void dap_dilithium_poly_decompose_{{ARCH_LOWER}}(
 *     int32_t *a1, int32_t *a0, const int32_t *a);
 * ================================================================ */
.globl dap_dilithium_poly_decompose_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_poly_decompose_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_poly_decompose_{{ARCH_LOWER}}:
    xorl    %eax, %eax
    vpternlogd $0xFF, %zmm10, %zmm10, %zmm10   /* all-ones */

    movl    $0x7FFFF, %ecx
    vpbroadcastd %ecx, %zmm9
    movl    $(-261889), %ecx
    vpbroadcastd %ecx, %zmm8
    movl    $523776, %ecx
    vpbroadcastd %ecx, %zmm7
    movl    $261887, %ecx
    vpbroadcastd %ecx, %zmm6
    movl    $1, %ecx
    vpbroadcastd %ecx, %zmm5
    movl    $8118530, %ecx
    vpbroadcastd %ecx, %zmm4
    movl    $15, %ecx
    vpbroadcastd %ecx, %zmm3

    .p2align 4
.L_decompose512_loop:
    vmovdqu64 (%rdx,%rax), %zmm0
    vpsrld  $19, %zmm0, %zmm1
    vpandd  %zmm0, %zmm9, %zmm2
    vpaddd  %zmm0, %zmm6, %zmm0
    vpslld  $9, %zmm1, %zmm1
    vpaddd  %zmm8, %zmm1, %zmm1
    vpaddd  %zmm1, %zmm2, %zmm1
    vpsrad  $31, %zmm1, %zmm2
    vpandd  %zmm7, %zmm2, %zmm2
    vpaddd  %zmm1, %zmm2, %zmm2        /* a0 intermediate */

    vpsubd  %zmm2, %zmm0, %zmm1        /* remainder */
    vpaddd  %zmm10, %zmm1, %zmm0
    vpsrld  $19, %zmm1, %zmm1
    vpsrad  $31, %zmm0, %zmm0
    vpandnd %zmm5, %zmm0, %zmm0
    vpaddd  %zmm1, %zmm0, %zmm0

    vpsrld  $4, %zmm0, %zmm1
    vpandd  %zmm3, %zmm0, %zmm0
    vpsubd  %zmm1, %zmm4, %zmm1
    vmovdqu64 %zmm0, (%rdi,%rax)       /* store a1 */
    vpaddd  %zmm2, %zmm1, %zmm1
    vmovdqu64 %zmm1, (%rsi,%rax)       /* store a0 */

    addq    $64, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_decompose512_loop
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_poly_decompose_{{ARCH_LOWER}})


/* ================================================================
 * poly_make_hint (legacy, gamma2=(Q-1)/32)
 * unsigned dap_dilithium_poly_make_hint_{{ARCH_LOWER}}(
 *     int32_t *h, const int32_t *a, const int32_t *b);
 * ================================================================ */
.globl dap_dilithium_poly_make_hint_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_poly_make_hint_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_poly_make_hint_{{ARCH_LOWER}}:
    movq    %rdi, %r8
    movq    %rsi, %rdi
    movq    %rdx, %rsi
    xorl    %ecx, %ecx
    xorl    %edx, %edx

    vpternlogd $0xFF, %zmm10, %zmm10, %zmm10

    movl    $0x7FFFF, %eax
    vpbroadcastd %eax, %zmm9
    movl    $(-261889), %eax
    vpbroadcastd %eax, %zmm8
    movl    $261887, %eax
    vpbroadcastd %eax, %zmm7
    movl    $523776, %eax
    vpbroadcastd %eax, %zmm6
    movl    $1, %eax
    vpbroadcastd %eax, %zmm4
    movl    $15, %eax
    vpbroadcastd %eax, %zmm5

    vpxor   %xmm14, %xmm14, %xmm14    /* hint accumulator */

    .p2align 4
.L_mhint512_loop:
    /* decompose(a[i]) → a1_a in zmm1 */
    vmovdqu64 (%rdi,%rdx), %zmm1
    vpsrld  $19, %zmm1, %zmm2
    vpandd  %zmm1, %zmm9, %zmm3
    vpaddd  %zmm1, %zmm7, %zmm1
    vpslld  $9, %zmm2, %zmm2
    vpaddd  %zmm8, %zmm2, %zmm2
    vpaddd  %zmm2, %zmm3, %zmm3
    vpsrad  $31, %zmm3, %zmm2
    vpandd  %zmm6, %zmm2, %zmm2
    vpaddd  %zmm3, %zmm2, %zmm2
    vpsubd  %zmm2, %zmm1, %zmm1
    vpaddd  %zmm10, %zmm1, %zmm2
    vpsrld  $19, %zmm1, %zmm1
    vpsrad  $31, %zmm2, %zmm2
    vpandnd %zmm4, %zmm2, %zmm2
    vpaddd  %zmm1, %zmm2, %zmm1
    vpandd  %zmm5, %zmm1, %zmm1        /* a1_a */

    /* decompose(b[i]) → a1_b in zmm0 */
    vmovdqu64 (%rsi,%rdx), %zmm0
    vpsrld  $19, %zmm0, %zmm2
    vpandd  %zmm0, %zmm9, %zmm11
    vpaddd  %zmm0, %zmm7, %zmm0
    vpslld  $9, %zmm2, %zmm2
    vpaddd  %zmm8, %zmm2, %zmm2
    vpaddd  %zmm2, %zmm11, %zmm11
    vpsrad  $31, %zmm11, %zmm2
    vpandd  %zmm6, %zmm2, %zmm2
    vpaddd  %zmm11, %zmm2, %zmm2
    vpsubd  %zmm2, %zmm0, %zmm0
    vpaddd  %zmm10, %zmm0, %zmm3
    vpsrld  $19, %zmm0, %zmm0
    vpsrad  $31, %zmm3, %zmm3
    vpandnd %zmm4, %zmm3, %zmm3
    vpaddd  %zmm0, %zmm3, %zmm0
    vpandd  %zmm5, %zmm0, %zmm0        /* a1_b */

    /* hint = (a1_a != a1_b) ? 1 : 0 */
    vpcmpd  $0, %zmm0, %zmm1, %k0
    vpmovm2d %k0, %zmm0
    vpandnd %zmm4, %zmm0, %zmm0
    vmovdqu64 %zmm0, (%r8,%rdx)

    vpaddd  %zmm0, %zmm14, %zmm14

    addq    $64, %rdx
    cmpq    $COEFF_BYTES, %rdx
    jne     .L_mhint512_loop

    /* horizontal sum zmm14 → ecx */
    vextracti64x4 $1, %zmm14, %ymm0
    vpaddd  %ymm0, %ymm14, %ymm0
    vextracti128  $1, %ymm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm0
    vpshufd $0x4E, %xmm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm0
    vpshufd $0xB1, %xmm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm0
    vmovd   %xmm0, %eax
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_poly_make_hint_{{ARCH_LOWER}})


/* ================================================================
 * poly_use_hint (legacy, gamma2=(Q-1)/32)
 * void dap_dilithium_poly_use_hint_{{ARCH_LOWER}}(
 *     int32_t *r, const int32_t *b, const int32_t *h);
 * ================================================================ */
.globl dap_dilithium_poly_use_hint_{{ARCH_LOWER}}
FUNC_TYPE(dap_dilithium_poly_use_hint_{{ARCH_LOWER}})
.p2align 4
dap_dilithium_poly_use_hint_{{ARCH_LOWER}}:
    movq    %rsi, %rcx
    xorl    %eax, %eax
    vpternlogd $0xFF, %zmm5, %zmm5, %zmm5

    movl    $0x7FFFF, %esi
    vpbroadcastd %esi, %zmm11
    movl    $(-261889), %esi
    vpbroadcastd %esi, %zmm10
    movl    $523776, %esi
    vpbroadcastd %esi, %zmm9
    movl    $261887, %esi
    vpbroadcastd %esi, %zmm8
    movl    $1, %esi
    vpbroadcastd %esi, %zmm4
    movl    $15, %esi
    vpbroadcastd %esi, %zmm3
    movl    $8118530, %esi
    vpbroadcastd %esi, %zmm7
    movl    $DIL_Q, %esi
    vpbroadcastd %esi, %zmm6

    .p2align 4
.L_usehint512_loop:
    vmovdqu64 (%rcx,%rax), %zmm0
    vpxor   %xmm13, %xmm13, %xmm13
    vpcmpeqd (%rdx,%rax), %zmm13, %k0
    vpmovm2d %k0, %zmm12               /* hint==0 as vector */

    /* decompose(b) → a1 in zmm1, a0_full in zmm0 */
    vpsrld  $19, %zmm0, %zmm1
    vpandd  %zmm0, %zmm11, %zmm2
    vpaddd  %zmm0, %zmm8, %zmm0
    vpslld  $9, %zmm1, %zmm1
    vpaddd  %zmm10, %zmm1, %zmm1
    vpaddd  %zmm1, %zmm2, %zmm1
    vpsrad  $31, %zmm1, %zmm2
    vpandd  %zmm9, %zmm2, %zmm2
    vpaddd  %zmm1, %zmm2, %zmm2

    vpsubd  %zmm2, %zmm0, %zmm1
    vpaddd  %zmm5, %zmm1, %zmm0
    vpsrld  $19, %zmm1, %zmm1
    vpsrad  $31, %zmm0, %zmm0
    vpandnd %zmm4, %zmm0, %zmm0
    vpaddd  %zmm1, %zmm0, %zmm0

    vpandd  %zmm3, %zmm0, %zmm1        /* a1 */
    vpsrld  $4, %zmm0, %zmm0
    vpsubd  %zmm0, %zmm7, %zmm0
    vpaddd  %zmm2, %zmm0, %zmm0        /* a0_full */

    vpaddd  %zmm4, %zmm1, %zmm2        /* plus1 = a1+1 (before &15) */
    vpcmpd  $6, %zmm6, %zmm0, %k1      /* a0_full > Q */
    vpaddd  %zmm5, %zmm1, %zmm0        /* minus1 = a1-1 (before &15) */
    vpandd  %zmm3, %zmm0, %zmm0        /* minus1 &= 15 */
    vpandd  %zmm3, %zmm2, %zmm0{%k1}   /* merge plus1&15 where a0>Q */
    vpmovd2m %zmm12, %k1
    vmovdqa32 %zmm1, %zmm0{%k1}        /* merge a1 where hint==0 */

    vmovdqu64 %zmm0, (%rdi,%rax)
    addq    $64, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_usehint512_loop
    vzeroupper
    ret
FUNC_SIZE(dap_dilithium_poly_use_hint_{{ARCH_LOWER}})
