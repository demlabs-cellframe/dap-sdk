/*
 * Dilithium polynomial arithmetic ops — AVX2 assembly.
 * Generated from dap_dilithium_poly_ops_avx2.S.tpl by dap_tpl.
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

.text

/* ================================================================
 * poly_reduce: coeffs[i] = coeffs[i] mod Q (Barrett-like)
 *   hi = c >> 23;  lo = c & 0x7FFFFF
 *   coeffs[i] = lo + (hi << 13) - hi  [lazy reduce, result < 2Q]
 * void dap_dilithium_poly_reduce_{{ARCH_LOWER}}(int32_t coeffs[256]);
 * ================================================================ */
.globl dap_dilithium_poly_reduce_{{ARCH_LOWER}}
.type  dap_dilithium_poly_reduce_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_reduce_{{ARCH_LOWER}}:
    movl    $0x7FFFFF, %eax
    leaq    COEFF_BYTES(%rdi), %rdx
    vmovd   %eax, %xmm3
    vpbroadcastd %xmm3, %ymm3
    .p2align 4
.L_reduce_loop:
    vmovdqu (%rdi), %ymm1
    vpsrld  $23, %ymm1, %ymm2
    vpand   %ymm3, %ymm1, %ymm1
    vpslld  $13, %ymm2, %ymm0
    vpsubd  %ymm2, %ymm0, %ymm0
    vpaddd  %ymm1, %ymm0, %ymm0
    vmovdqu %ymm0, (%rdi)
    addq    $32, %rdi
    cmpq    %rdi, %rdx
    jne     .L_reduce_loop
    vzeroupper
    ret
.size dap_dilithium_poly_reduce_{{ARCH_LOWER}}, .-dap_dilithium_poly_reduce_{{ARCH_LOWER}}


/* ================================================================
 * poly_csubq: if (coeffs[i] >= Q) coeffs[i] -= Q
 * void dap_dilithium_poly_csubq_{{ARCH_LOWER}}(int32_t coeffs[256]);
 * ================================================================ */
.globl dap_dilithium_poly_csubq_{{ARCH_LOWER}}
.type  dap_dilithium_poly_csubq_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_csubq_{{ARCH_LOWER}}:
    movl    $(-DIL_Q), %eax
    leaq    COEFF_BYTES(%rdi), %rdx
    vmovd   %eax, %xmm3
    movl    $DIL_Q, %eax
    vmovd   %eax, %xmm2
    vpbroadcastd %xmm3, %ymm3
    vpbroadcastd %xmm2, %ymm2
    .p2align 4
.L_csubq_loop:
    vpaddd  (%rdi), %ymm3, %ymm0
    vpsrad  $31, %ymm0, %ymm1
    vpand   %ymm2, %ymm1, %ymm1
    vpaddd  %ymm1, %ymm0, %ymm0
    vmovdqu %ymm0, (%rdi)
    addq    $32, %rdi
    cmpq    %rdi, %rdx
    jne     .L_csubq_loop
    vzeroupper
    ret
.size dap_dilithium_poly_csubq_{{ARCH_LOWER}}, .-dap_dilithium_poly_csubq_{{ARCH_LOWER}}


/* ================================================================
 * poly_freeze: reduce then csubq
 * void dap_dilithium_poly_freeze_{{ARCH_LOWER}}(int32_t coeffs[256]);
 * ================================================================ */
.globl dap_dilithium_poly_freeze_{{ARCH_LOWER}}
.type  dap_dilithium_poly_freeze_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_freeze_{{ARCH_LOWER}}:
    /* Phase 1: reduce */
    movl    $0x7FFFFF, %ecx
    leaq    COEFF_BYTES(%rdi), %rdx
    movq    %rdi, %rax
    vmovd   %ecx, %xmm3
    vpbroadcastd %xmm3, %ymm3
    .p2align 4
.L_freeze_reduce:
    vmovdqu (%rax), %ymm1
    vpsrld  $23, %ymm1, %ymm2
    vpand   %ymm3, %ymm1, %ymm1
    vpslld  $13, %ymm2, %ymm0
    vpsubd  %ymm2, %ymm0, %ymm0
    vpaddd  %ymm1, %ymm0, %ymm0
    vmovdqu %ymm0, (%rax)
    addq    $32, %rax
    cmpq    %rax, %rdx
    jne     .L_freeze_reduce

    /* Phase 2: csubq */
    movl    $(-DIL_Q), %eax
    vmovd   %eax, %xmm3
    movl    $DIL_Q, %eax
    vmovd   %eax, %xmm2
    vpbroadcastd %xmm3, %ymm3
    vpbroadcastd %xmm2, %ymm2
    .p2align 4
.L_freeze_csubq:
    vpaddd  (%rdi), %ymm3, %ymm0
    vpsrad  $31, %ymm0, %ymm1
    vpand   %ymm2, %ymm1, %ymm1
    vpaddd  %ymm1, %ymm0, %ymm0
    vmovdqu %ymm0, (%rdi)
    addq    $32, %rdi
    cmpq    %rdi, %rdx
    jne     .L_freeze_csubq
    vzeroupper
    ret
.size dap_dilithium_poly_freeze_{{ARCH_LOWER}}, .-dap_dilithium_poly_freeze_{{ARCH_LOWER}}


/* ================================================================
 * poly_add: r[i] = a[i] + b[i]
 * void dap_dilithium_poly_add_{{ARCH_LOWER}}(int32_t *r, const int32_t *a, const int32_t *b);
 * ================================================================ */
.globl dap_dilithium_poly_add_{{ARCH_LOWER}}
.type  dap_dilithium_poly_add_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_add_{{ARCH_LOWER}}:
    xorl    %eax, %eax
    .p2align 4
.L_add_loop:
    vmovdqu (%rdx,%rax), %ymm0
    vpaddd  (%rsi,%rax), %ymm0, %ymm0
    vmovdqu %ymm0, (%rdi,%rax)
    addq    $32, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_add_loop
    vzeroupper
    ret
.size dap_dilithium_poly_add_{{ARCH_LOWER}}, .-dap_dilithium_poly_add_{{ARCH_LOWER}}


/* ================================================================
 * poly_sub: r[i] = 2Q + a[i] - b[i]
 * void dap_dilithium_poly_sub_{{ARCH_LOWER}}(int32_t *r, const int32_t *a, const int32_t *b);
 * ================================================================ */
.globl dap_dilithium_poly_sub_{{ARCH_LOWER}}
.type  dap_dilithium_poly_sub_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_sub_{{ARCH_LOWER}}:
    movl    $(2 * DIL_Q), %ecx
    xorl    %eax, %eax
    vmovd   %ecx, %xmm1
    vpbroadcastd %xmm1, %ymm1
    .p2align 4
.L_sub_loop:
    vpaddd  (%rsi,%rax), %ymm1, %ymm0
    vpsubd  (%rdx,%rax), %ymm0, %ymm0
    vmovdqu %ymm0, (%rdi,%rax)
    addq    $32, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_sub_loop
    vzeroupper
    ret
.size dap_dilithium_poly_sub_{{ARCH_LOWER}}, .-dap_dilithium_poly_sub_{{ARCH_LOWER}}


/* ================================================================
 * poly_neg: coeffs[i] = Q - coeffs[i]
 * void dap_dilithium_poly_neg_{{ARCH_LOWER}}(int32_t coeffs[256]);
 * ================================================================ */
.globl dap_dilithium_poly_neg_{{ARCH_LOWER}}
.type  dap_dilithium_poly_neg_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_neg_{{ARCH_LOWER}}:
    movl    $DIL_Q, %eax
    leaq    COEFF_BYTES(%rdi), %rdx
    vmovd   %eax, %xmm1
    vpbroadcastd %xmm1, %ymm1
    .p2align 4
.L_neg_loop:
    vpsubd  (%rdi), %ymm1, %ymm0
    vmovdqu %ymm0, (%rdi)
    addq    $32, %rdi
    cmpq    %rdi, %rdx
    jne     .L_neg_loop
    vzeroupper
    ret
.size dap_dilithium_poly_neg_{{ARCH_LOWER}}, .-dap_dilithium_poly_neg_{{ARCH_LOWER}}


/* ================================================================
 * poly_shiftl: coeffs[i] <<= d
 * void dap_dilithium_poly_shiftl_{{ARCH_LOWER}}(int32_t coeffs[256], int d);
 * ================================================================ */
.globl dap_dilithium_poly_shiftl_{{ARCH_LOWER}}
.type  dap_dilithium_poly_shiftl_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_shiftl_{{ARCH_LOWER}}:
    leaq    COEFF_BYTES(%rdi), %rax
    vmovd   %esi, %xmm1
    .p2align 4
.L_shiftl_loop:
    vmovdqu (%rdi), %ymm0
    vpslld  %xmm1, %ymm0, %ymm0
    vmovdqu %ymm0, (%rdi)
    addq    $32, %rdi
    cmpq    %rdi, %rax
    jne     .L_shiftl_loop
    vzeroupper
    ret
.size dap_dilithium_poly_shiftl_{{ARCH_LOWER}}, .-dap_dilithium_poly_shiftl_{{ARCH_LOWER}}


/* ================================================================
 * poly_power2round: legacy D=14
 *   a0 = (a & 0x3FFF) + (-8193 >> 31) & 16384
 *   a1 = (a + 8191) >> 14
 * void dap_dilithium_poly_power2round_{{ARCH_LOWER}}(
 *     int32_t *a1, int32_t *a0, const int32_t *a);
 * ================================================================ */
.globl dap_dilithium_poly_power2round_{{ARCH_LOWER}}
.type  dap_dilithium_poly_power2round_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_power2round_{{ARCH_LOWER}}:
    movl    $0x3FFF, %ecx
    xorl    %eax, %eax
    vmovd   %ecx, %xmm7
    movl    $(-8193), %ecx
    vmovd   %ecx, %xmm6
    movl    $16384, %ecx
    vpbroadcastd %xmm7, %ymm7
    vmovd   %ecx, %xmm5
    movl    $(DIL_Q - 8191), %ecx
    vpbroadcastd %xmm6, %ymm6
    vmovd   %ecx, %xmm4
    movl    $8191, %ecx
    vpbroadcastd %xmm5, %ymm5
    vmovd   %ecx, %xmm3
    vpbroadcastd %xmm4, %ymm4
    vpbroadcastd %xmm3, %ymm3
    .p2align 4
.L_p2r_loop:
    vmovdqu (%rdx,%rax), %ymm0
    vpand   %ymm7, %ymm0, %ymm1        /* lo = a & 0x3FFF */
    vpaddd  %ymm3, %ymm0, %ymm0        /* a + 8191 */
    vpaddd  %ymm6, %ymm1, %ymm1        /* lo + (-8193) */
    vpsrad  $31, %ymm1, %ymm2
    vpand   %ymm5, %ymm2, %ymm2        /* (lo-8193)<0 ? 16384 : 0 */
    vpaddd  %ymm2, %ymm1, %ymm1        /* a0 corrected */
    vpsubd  %ymm1, %ymm0, %ymm0
    vpaddd  %ymm4, %ymm1, %ymm2        /* a0 + Q-1-8191 */
    vpsrld  $14, %ymm0, %ymm0          /* a1 */
    vmovdqu %ymm2, (%rsi,%rax)         /* store a0 */
    vmovdqu %ymm0, (%rdi,%rax)         /* store a1 */
    addq    $32, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_p2r_loop
    vzeroupper
    ret
.size dap_dilithium_poly_power2round_{{ARCH_LOWER}}, .-dap_dilithium_poly_power2round_{{ARCH_LOWER}}


/* ================================================================
 * poly_chknorm: check if |coeffs[i]| >= bound
 * int dap_dilithium_poly_chknorm_{{ARCH_LOWER}}(const int32_t *coeffs, int32_t bound);
 * returns 1 if norm violated, 0 otherwise
 * ================================================================ */
.globl dap_dilithium_poly_chknorm_{{ARCH_LOWER}}
.type  dap_dilithium_poly_chknorm_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_chknorm_{{ARCH_LOWER}}:
    subl    $1, %esi
    movl    $((DIL_Q - 1) / 2), %eax
    leaq    COEFF_BYTES(%rdi), %rdx
    vmovd   %esi, %xmm3
    vmovd   %eax, %xmm1
    vpbroadcastd %xmm3, %ymm3          /* bound - 1 */
    vpbroadcastd %xmm1, %ymm1          /* (Q-1)/2 = 4190208 */
    jmp     .L_chknorm_entry

    .p2align 6
    .p2align 4,,10
    .p2align 3
.L_chknorm_next:
    addq    $32, %rdi
    cmpq    %rdi, %rdx
    je      .L_chknorm_pass

.L_chknorm_entry:
    vpsubd  (%rdi), %ymm1, %ymm0       /* t = (Q-1)/2 - coeffs[i] */
    vpsrad  $31, %ymm0, %ymm2          /* sign(t) */
    vpxor   %ymm2, %ymm0, %ymm0        /* ones' complement abs(t) */
    vpsubd  %ymm0, %ymm1, %ymm0        /* (Q-1)/2 - abs_t = min(a, Q-a) */
    vpcmpgtd %ymm3, %ymm0, %ymm0       /* result > (bound-1) → violation */
    vpmovmskb %ymm0, %eax
    testl   %eax, %eax
    je      .L_chknorm_next
    movl    $1, %eax
    vzeroupper
    ret

.L_chknorm_pass:
    xorl    %eax, %eax
    vzeroupper
    ret
.size dap_dilithium_poly_chknorm_{{ARCH_LOWER}}, .-dap_dilithium_poly_chknorm_{{ARCH_LOWER}}


/* ================================================================
 * poly_decompose (legacy, gamma2=(Q-1)/32)
 * void dap_dilithium_poly_decompose_{{ARCH_LOWER}}(
 *     int32_t *a1, int32_t *a0, const int32_t *a);
 *
 * Barrett-like decompose: a = a1*alpha + a0, alpha = 2*gamma2
 * ================================================================ */
.globl dap_dilithium_poly_decompose_{{ARCH_LOWER}}
.type  dap_dilithium_poly_decompose_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_decompose_{{ARCH_LOWER}}:
    movq    %rdi, %rcx
    vpcmpeqd %ymm9, %ymm9, %ymm9      /* all-ones = -1 */
    xorl    %eax, %eax

    movl    $0x7FFFF, %edi
    vmovd   %edi, %xmm8
    vpbroadcastd %xmm8, %ymm8          /* 524287 = (Q-1)/(2*16) - 1? actually mask */

    movl    $(-261889), %edi
    vmovd   %edi, %xmm7
    vpbroadcastd %xmm7, %ymm7

    movl    $523776, %edi
    vmovd   %edi, %xmm6
    vpbroadcastd %xmm6, %ymm6          /* 2*gamma2 */

    movl    $261887, %edi
    vmovd   %edi, %xmm5
    vpbroadcastd %xmm5, %ymm5          /* gamma2-1 */

    movl    $1, %edi
    vmovd   %edi, %xmm2
    vpbroadcastd %xmm2, %ymm2

    movl    $8118530, %edi
    vmovd   %edi, %xmm4
    vpbroadcastd %xmm4, %ymm4          /* Q - gamma2 + 1 */

    movl    $15, %edi
    vmovd   %edi, %xmm3
    vpbroadcastd %xmm3, %ymm3

    .p2align 4
.L_decompose_loop:
    vmovdqu (%rdx,%rax), %ymm0

    vpsrld  $19, %ymm0, %ymm1
    vpand   %ymm8, %ymm0, %ymm10
    vpaddd  %ymm5, %ymm0, %ymm0
    vpslld  $9, %ymm1, %ymm1
    vpaddd  %ymm7, %ymm1, %ymm1
    vpaddd  %ymm10, %ymm1, %ymm1
    vpsrad  $31, %ymm1, %ymm10
    vpand   %ymm6, %ymm10, %ymm10
    vpaddd  %ymm10, %ymm1, %ymm1       /* a0 intermediate */

    vpsubd  %ymm1, %ymm0, %ymm0        /* remainder */

    vpaddd  %ymm9, %ymm0, %ymm10       /* remainder - 1 */
    vpsrld  $19, %ymm0, %ymm0
    vpsrad  $31, %ymm10, %ymm10
    vpaddd  %ymm2, %ymm0, %ymm0
    vpand   %ymm2, %ymm10, %ymm10
    vpsubd  %ymm10, %ymm0, %ymm0

    vpsrld  $4, %ymm0, %ymm10
    vpand   %ymm3, %ymm0, %ymm0        /* a1 */
    vpsubd  %ymm10, %ymm4, %ymm10
    vmovdqu %ymm0, (%rcx,%rax)          /* store a1 */
    vpaddd  %ymm1, %ymm10, %ymm1
    vmovdqu %ymm1, (%rsi,%rax)          /* store a0 */

    addq    $32, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_decompose_loop
    vzeroupper
    ret
.size dap_dilithium_poly_decompose_{{ARCH_LOWER}}, .-dap_dilithium_poly_decompose_{{ARCH_LOWER}}


/* ================================================================
 * poly_make_hint (legacy, gamma2=(Q-1)/32)
 * unsigned dap_dilithium_poly_make_hint_{{ARCH_LOWER}}(
 *     int32_t *h, const int32_t *a, const int32_t *b);
 *
 * Decomposes both a and b, checks if a1 values differ.
 * Returns total hint count.
 * ================================================================ */
.globl dap_dilithium_poly_make_hint_{{ARCH_LOWER}}
.type  dap_dilithium_poly_make_hint_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_make_hint_{{ARCH_LOWER}}:
    movq    %rdi, %rcx
    vpxor   %xmm2, %xmm2, %xmm2       /* hint counter */
    xorl    %eax, %eax

    movl    $0x7FFFF, %edi
    vmovd   %edi, %xmm7
    vpbroadcastd %xmm7, %ymm7
    vpcmpeqd %ymm8, %ymm8, %ymm8      /* -1 */

    movl    $(-261889), %edi
    vmovd   %edi, %xmm6
    vpbroadcastd %xmm6, %ymm6

    movl    $261887, %edi
    vmovd   %edi, %xmm5
    vpbroadcastd %xmm5, %ymm5

    movl    $523776, %edi
    vmovd   %edi, %xmm4
    vpbroadcastd %xmm4, %ymm4

    movl    $1, %edi
    vmovd   %edi, %xmm1
    vpbroadcastd %xmm1, %ymm1

    movl    $15, %edi
    vmovd   %edi, %xmm3
    vpbroadcastd %xmm3, %ymm3

    .p2align 4
.L_mhint_loop:
    /* decompose(a[i]) → a1_a in ymm9 */
    vmovdqu (%rsi,%rax), %ymm9
    vpsrld  $19, %ymm9, %ymm10
    vpand   %ymm7, %ymm9, %ymm11
    vpaddd  %ymm5, %ymm9, %ymm9
    vpslld  $9, %ymm10, %ymm10
    vpaddd  %ymm6, %ymm10, %ymm10
    vpaddd  %ymm11, %ymm10, %ymm10
    vpsrad  $31, %ymm10, %ymm11
    vpand   %ymm4, %ymm11, %ymm11
    vpaddd  %ymm11, %ymm10, %ymm10
    vpsubd  %ymm10, %ymm9, %ymm9
    vpaddd  %ymm8, %ymm9, %ymm11
    vpsrld  $19, %ymm9, %ymm9
    vpsrad  $31, %ymm11, %ymm11
    vpaddd  %ymm1, %ymm9, %ymm9
    vpand   %ymm1, %ymm11, %ymm11
    vpsubd  %ymm11, %ymm9, %ymm9
    vpand   %ymm3, %ymm9, %ymm9        /* a1_a */

    /* decompose(b[i]) → a1_b in ymm0 */
    vmovdqu (%rdx,%rax), %ymm0
    vpsrld  $19, %ymm0, %ymm10
    vpand   %ymm7, %ymm0, %ymm12
    vpaddd  %ymm5, %ymm0, %ymm0
    vpslld  $9, %ymm10, %ymm10
    vpaddd  %ymm6, %ymm10, %ymm10
    vpaddd  %ymm12, %ymm10, %ymm10
    vpsrad  $31, %ymm10, %ymm12
    vpand   %ymm4, %ymm12, %ymm12
    vpaddd  %ymm12, %ymm10, %ymm10
    vpsubd  %ymm10, %ymm0, %ymm0
    vpaddd  %ymm8, %ymm0, %ymm10
    vpsrld  $19, %ymm0, %ymm0
    vpsrad  $31, %ymm10, %ymm10
    vpaddd  %ymm1, %ymm0, %ymm0
    vpand   %ymm1, %ymm10, %ymm10
    vpsubd  %ymm10, %ymm0, %ymm0
    vpand   %ymm3, %ymm0, %ymm0        /* a1_b */

    /* hint = (a1_a != a1_b) */
    vpcmpeqd %ymm0, %ymm9, %ymm9
    vpandn  %ymm1, %ymm9, %ymm9
    vmovdqu %ymm9, (%rcx,%rax)

    /* accumulate: horizontal sum deferred to end */
    vpaddd  %ymm9, %ymm2, %ymm2

    addq    $32, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_mhint_loop

    /* horizontal sum ymm2 → eax */
    vextracti128 $1, %ymm2, %xmm0
    vpaddd  %xmm0, %xmm2, %xmm0
    vpshufd $0x4E, %xmm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm0
    vpshufd $0xB1, %xmm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm0
    vmovd   %xmm0, %eax
    vzeroupper
    ret
.size dap_dilithium_poly_make_hint_{{ARCH_LOWER}}, .-dap_dilithium_poly_make_hint_{{ARCH_LOWER}}


/* ================================================================
 * poly_use_hint (legacy, gamma2=(Q-1)/32)
 * void dap_dilithium_poly_use_hint_{{ARCH_LOWER}}(
 *     int32_t *r, const int32_t *b, const int32_t *h);
 *
 * Decomposes b, then adjusts a1 based on hint and sign of a0.
 * ================================================================ */
.globl dap_dilithium_poly_use_hint_{{ARCH_LOWER}}
.type  dap_dilithium_poly_use_hint_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_use_hint_{{ARCH_LOWER}}:
    movq    %rdi, %rcx
    vpcmpeqd %ymm3, %ymm3, %ymm3      /* -1 */
    vpxor   %xmm10, %xmm10, %xmm10    /* 0 */
    xorl    %eax, %eax

    movl    $0x7FFFF, %edi
    vmovd   %edi, %xmm9
    vpbroadcastd %xmm9, %ymm9

    movl    $(-261889), %edi
    vmovd   %edi, %xmm8
    vpbroadcastd %xmm8, %ymm8

    movl    $523776, %edi
    vmovd   %edi, %xmm7
    vpbroadcastd %xmm7, %ymm7

    movl    $261887, %edi
    vmovd   %edi, %xmm6
    vpbroadcastd %xmm6, %ymm6

    movl    $1, %edi
    vmovd   %edi, %xmm2
    vpbroadcastd %xmm2, %ymm2

    movl    $15, %edi
    vmovd   %edi, %xmm1
    vpbroadcastd %xmm1, %ymm1

    movl    $8118530, %edi
    vmovd   %edi, %xmm5
    vpbroadcastd %xmm5, %ymm5          /* Q - gamma2 + 1 */

    movl    $DIL_Q, %edi
    vmovd   %edi, %xmm4
    vpbroadcastd %xmm4, %ymm4

    .p2align 4
.L_usehint_loop:
    vmovdqu (%rsi,%rax), %ymm0

    /* decompose(b) → a1 in ymm12, a0 in ymm0 */
    vpsrld  $19, %ymm0, %ymm11
    vpand   %ymm9, %ymm0, %ymm12
    vpaddd  %ymm6, %ymm0, %ymm0
    vpslld  $9, %ymm11, %ymm11
    vpaddd  %ymm8, %ymm11, %ymm11
    vpaddd  %ymm12, %ymm11, %ymm11
    vpsrad  $31, %ymm11, %ymm12
    vpand   %ymm7, %ymm12, %ymm12
    vpaddd  %ymm12, %ymm11, %ymm11     /* a0 intermediate */

    vpsubd  %ymm11, %ymm0, %ymm0       /* remainder */

    vpaddd  %ymm3, %ymm0, %ymm12
    vpsrld  $19, %ymm0, %ymm0
    vpsrad  $31, %ymm12, %ymm12
    vpaddd  %ymm2, %ymm0, %ymm0
    vpand   %ymm2, %ymm12, %ymm12
    vpsubd  %ymm12, %ymm0, %ymm0

    vpand   %ymm1, %ymm0, %ymm12       /* a1 = result & 15 */
    vpsrld  $4, %ymm0, %ymm0
    vpsubd  %ymm0, %ymm5, %ymm0
    vpaddd  %ymm11, %ymm0, %ymm0       /* a0 (full) */

    /* plus1 = (a1 + 1) & 15 */
    vpaddd  %ymm12, %ymm2, %ymm13
    vpand   %ymm1, %ymm13, %ymm13

    /* minus1 = (a1 - 1) & 15 (= a1 + (-1) = a1 + 0xFFFFFFFF) */
    vpaddd  %ymm12, %ymm3, %ymm11
    vpand   %ymm1, %ymm11, %ymm11

    /* r0_positive = (a0 > Q) → meaning centered r0 > 0 */
    vpcmpgtd %ymm4, %ymm0, %ymm0

    /* hint_result = r0>Q ? plus1 : minus1 */
    vpblendvb %ymm0, %ymm13, %ymm11, %ymm0

    /* result = hint==0 ? a1 : hint_result */
    vpcmpeqd (%rdx,%rax), %ymm10, %ymm11
    vpblendvb %ymm11, %ymm12, %ymm0, %ymm0

    vmovdqu %ymm0, (%rcx,%rax)
    addq    $32, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_usehint_loop
    vzeroupper
    ret
.size dap_dilithium_poly_use_hint_{{ARCH_LOWER}}, .-dap_dilithium_poly_use_hint_{{ARCH_LOWER}}
