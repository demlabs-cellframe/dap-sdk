/*
 * FIPS 204 ML-DSA SIMD poly ops — AVX2 assembly.
 * Generated from dap_dilithium_fips204_avx2.S.tpl by dap_tpl.
 *
 * Contains gamma2-specialized use_hint and decompose for ML-DSA verify/sign.
 * Two gamma2 variants:
 *   g32: gamma2 = (Q-1)/32 = 261888   (ML-DSA-65/87)
 *   g88: gamma2 = (Q-1)/88 = 95232    (ML-DSA-44)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#define DIL_N       256
#define DIL_Q       8380417
#define COEFF_BYTES (DIL_N * 4)

.text

/* ================================================================
 * poly_use_hint_p — gamma2 = (Q-1)/32  (ML-DSA-65/87)
 *
 * void dap_dilithium_poly_use_hint_g32_{{ARCH_LOWER}}(
 *     int32_t *r, const int32_t *b, const int32_t *h);
 *
 * Decompose: a1 = ((b+127)>>7 * 1025 + 2^21) >> 22; a1 &= 15
 * Hint:      h==0 → a1; r0>0 → (a1+1)&15; else → (a1-1)&15
 * ================================================================ */
.globl dap_dilithium_poly_use_hint_g32_{{ARCH_LOWER}}
.type  dap_dilithium_poly_use_hint_g32_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_use_hint_g32_{{ARCH_LOWER}}:
    movq    %rdi, %rcx

    movl    $127, %eax
    vmovd   %eax, %xmm15
    vpbroadcastd %xmm15, %ymm15        /* 127 */

    movl    $1025, %eax
    vmovd   %eax, %xmm14
    vpbroadcastd %xmm14, %ymm14        /* magic multiplier */

    movl    $(1 << 21), %eax
    vmovd   %eax, %xmm13
    vpbroadcastd %xmm13, %ymm13        /* rounding constant */

    movl    $15, %eax
    vmovd   %eax, %xmm12
    vpbroadcastd %xmm12, %ymm12        /* 0xF mask */

    movl    $(2 * 261888), %eax
    vmovd   %eax, %xmm11
    vpbroadcastd %xmm11, %ymm11        /* 2*gamma2 = 523776 */

    movl    $((DIL_Q - 1) / 2), %eax
    vmovd   %eax, %xmm10
    vpbroadcastd %xmm10, %ymm10        /* (Q-1)/2 = 4190208 */

    movl    $DIL_Q, %eax
    vmovd   %eax, %xmm9
    vpbroadcastd %xmm9, %ymm9          /* Q */

    movl    $1, %eax
    vmovd   %eax, %xmm8
    vpbroadcastd %xmm8, %ymm8          /* 1 */

    vpxor   %xmm7, %xmm7, %xmm7       /* 0 */

    xorl    %eax, %eax
    .p2align 4
.L_uh_g32_loop:
    vmovdqu (%rsi,%rax), %ymm0         /* val = b[i] */
    vmovdqu (%rdx,%rax), %ymm6         /* hint = h[i] */

    /* a1 = ((val + 127) >> 7 * 1025 + 2^21) >> 22 */
    vpaddd  %ymm15, %ymm0, %ymm1
    vpsrld  $7, %ymm1, %ymm1
    vpmulld %ymm14, %ymm1, %ymm1
    vpaddd  %ymm13, %ymm1, %ymm1
    vpsrld  $22, %ymm1, %ymm1
    vpand   %ymm12, %ymm1, %ymm1       /* a1 &= 15 */

    /* r0 = val - a1 * 2*gamma2 */
    vpmulld %ymm11, %ymm1, %ymm2
    vpsubd  %ymm2, %ymm0, %ymm2        /* r0 */

    /* center: r0 -= (((Q-1)/2 - r0) >> 31) & Q */
    vpsubd  %ymm2, %ymm10, %ymm3
    vpsrad  $31, %ymm3, %ymm3
    vpand   %ymm9, %ymm3, %ymm3
    vpsubd  %ymm3, %ymm2, %ymm2        /* r0 centered */

    /* hint_zero = (hint == 0) */
    vpcmpeqd %ymm7, %ymm6, %ymm5       /* -1 where h==0 */

    /* r0_pos = (r0 > 0) */
    vpcmpgtd %ymm7, %ymm2, %ymm4       /* -1 where r0>0 */

    /* plus1 = (a1 + 1) & 15 */
    vpaddd  %ymm8, %ymm1, %ymm2
    vpand   %ymm12, %ymm2, %ymm2

    /* minus1 = (a1 - 1) & 15 */
    vpsubd  %ymm8, %ymm1, %ymm3
    vpand   %ymm12, %ymm3, %ymm3

    /* hint_result = r0>0 ? plus1 : minus1 */
    vpblendvb %ymm4, %ymm2, %ymm3, %ymm3

    /* result = hint==0 ? a1 : hint_result */
    vpblendvb %ymm5, %ymm1, %ymm3, %ymm0

    vmovdqu %ymm0, (%rcx,%rax)
    addq    $32, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_uh_g32_loop
    vzeroupper
    ret
.size dap_dilithium_poly_use_hint_g32_{{ARCH_LOWER}}, .-dap_dilithium_poly_use_hint_g32_{{ARCH_LOWER}}


/* ================================================================
 * poly_use_hint_p — gamma2 = (Q-1)/88  (ML-DSA-44)
 *
 * void dap_dilithium_poly_use_hint_g88_{{ARCH_LOWER}}(
 *     int32_t *r, const int32_t *b, const int32_t *h);
 *
 * Decompose: a1 = ((b+127)>>7 * 11275 + 2^23) >> 24
 *            a1 ^= ((43-a1)>>31) & a1   (clamp 44→0)
 * Hint:      h==0 → a1; r0>0 → (a1==43?0:a1+1); else → (a1==0?43:a1-1)
 * ================================================================ */
.globl dap_dilithium_poly_use_hint_g88_{{ARCH_LOWER}}
.type  dap_dilithium_poly_use_hint_g88_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_use_hint_g88_{{ARCH_LOWER}}:
    movq    %rdi, %rcx

    movl    $127, %eax
    vmovd   %eax, %xmm15
    vpbroadcastd %xmm15, %ymm15        /* 127 */

    movl    $11275, %eax
    vmovd   %eax, %xmm14
    vpbroadcastd %xmm14, %ymm14        /* magic multiplier */

    movl    $(1 << 23), %eax
    vmovd   %eax, %xmm13
    vpbroadcastd %xmm13, %ymm13        /* rounding constant */

    movl    $43, %eax
    vmovd   %eax, %xmm12
    vpbroadcastd %xmm12, %ymm12        /* 43 (a1_max) */

    movl    $(2 * 95232), %eax
    vmovd   %eax, %xmm11
    vpbroadcastd %xmm11, %ymm11        /* 2*gamma2 = 190464 */

    movl    $((DIL_Q - 1) / 2), %eax
    vmovd   %eax, %xmm10
    vpbroadcastd %xmm10, %ymm10        /* (Q-1)/2 */

    movl    $DIL_Q, %eax
    vmovd   %eax, %xmm9
    vpbroadcastd %xmm9, %ymm9          /* Q */

    movl    $1, %eax
    vmovd   %eax, %xmm8
    vpbroadcastd %xmm8, %ymm8          /* 1 */

    vpxor   %xmm7, %xmm7, %xmm7       /* 0 */

    xorl    %eax, %eax
    .p2align 4
.L_uh_g88_loop:
    vmovdqu (%rsi,%rax), %ymm0         /* val = b[i] */
    vmovdqu (%rdx,%rax), %ymm6         /* hint = h[i] */

    /* a1 = ((val + 127) >> 7 * 11275 + 2^23) >> 24 */
    vpaddd  %ymm15, %ymm0, %ymm1
    vpsrld  $7, %ymm1, %ymm1
    vpmulld %ymm14, %ymm1, %ymm1
    vpaddd  %ymm13, %ymm1, %ymm1
    vpsrld  $24, %ymm1, %ymm1          /* a1_raw */

    /* clamp: a1 ^= ((43 - a1) >> 31) & a1 */
    vpsubd  %ymm1, %ymm12, %ymm2
    vpsrad  $31, %ymm2, %ymm2
    vpand   %ymm1, %ymm2, %ymm2
    vpxor   %ymm2, %ymm1, %ymm1        /* a1 final ∈ [0..43] */

    /* r0 = val - a1 * alpha */
    vpmulld %ymm11, %ymm1, %ymm2
    vpsubd  %ymm2, %ymm0, %ymm2        /* r0 */

    /* center: r0 -= (((Q-1)/2 - r0) >> 31) & Q */
    vpsubd  %ymm2, %ymm10, %ymm3
    vpsrad  $31, %ymm3, %ymm3
    vpand   %ymm9, %ymm3, %ymm3
    vpsubd  %ymm3, %ymm2, %ymm2        /* r0 centered */

    /* hint_zero mask */
    vpcmpeqd %ymm7, %ymm6, %ymm5       /* -1 where h==0 */

    /* r0_pos mask */
    vpcmpgtd %ymm7, %ymm2, %ymm4       /* -1 where r0>0 */

    /* plus1 = (a1 == 43) ? 0 : a1 + 1 */
    vpaddd  %ymm8, %ymm1, %ymm2        /* a1 + 1 */
    vpcmpeqd %ymm12, %ymm1, %ymm3      /* a1 == 43 */
    vpandn  %ymm2, %ymm3, %ymm2        /* ~(a1==43) & (a1+1) */

    /* minus1 = (a1 == 0) ? 43 : a1 - 1 */
    vpsubd  %ymm8, %ymm1, %ymm3        /* a1 - 1 */
    vpcmpeqd %ymm7, %ymm1, %ymm0       /* a1 == 0 */
    vpblendvb %ymm0, %ymm12, %ymm3, %ymm3  /* eq0 ? 43 : a1-1 */

    /* hint_result = r0>0 ? plus1 : minus1 */
    vpblendvb %ymm4, %ymm2, %ymm3, %ymm3

    /* result = hint==0 ? a1 : hint_result */
    vpblendvb %ymm5, %ymm1, %ymm3, %ymm0

    vmovdqu %ymm0, (%rcx,%rax)
    addq    $32, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_uh_g88_loop
    vzeroupper
    ret
.size dap_dilithium_poly_use_hint_g88_{{ARCH_LOWER}}, .-dap_dilithium_poly_use_hint_g88_{{ARCH_LOWER}}


/* ================================================================
 * poly_decompose_p — gamma2 = (Q-1)/32  (ML-DSA-65/87)
 *
 * void dap_dilithium_poly_decompose_g32_{{ARCH_LOWER}}(
 *     int32_t *a1, int32_t *a0, const int32_t *a);
 *
 * a1 = ((a+127)>>7 * 1025 + 2^21) >> 22; a1 &= 15
 * a0 = Q + (a - a1*2*gamma2) centered
 * ================================================================ */
.globl dap_dilithium_poly_decompose_g32_{{ARCH_LOWER}}
.type  dap_dilithium_poly_decompose_g32_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_decompose_g32_{{ARCH_LOWER}}:
    movl    $127, %eax
    vmovd   %eax, %xmm15
    vpbroadcastd %xmm15, %ymm15

    movl    $1025, %eax
    vmovd   %eax, %xmm14
    vpbroadcastd %xmm14, %ymm14

    movl    $(1 << 21), %eax
    vmovd   %eax, %xmm13
    vpbroadcastd %xmm13, %ymm13

    movl    $15, %eax
    vmovd   %eax, %xmm12
    vpbroadcastd %xmm12, %ymm12

    movl    $(2 * 261888), %eax
    vmovd   %eax, %xmm11
    vpbroadcastd %xmm11, %ymm11

    movl    $((DIL_Q - 1) / 2), %eax
    vmovd   %eax, %xmm10
    vpbroadcastd %xmm10, %ymm10

    movl    $DIL_Q, %eax
    vmovd   %eax, %xmm9
    vpbroadcastd %xmm9, %ymm9

    xorl    %eax, %eax
    .p2align 4
.L_dc_g32_loop:
    vmovdqu (%rdx,%rax), %ymm0

    vpaddd  %ymm15, %ymm0, %ymm1
    vpsrld  $7, %ymm1, %ymm1
    vpmulld %ymm14, %ymm1, %ymm1
    vpaddd  %ymm13, %ymm1, %ymm1
    vpsrld  $22, %ymm1, %ymm1
    vpand   %ymm12, %ymm1, %ymm1       /* a1 */

    /* a0 = a - a1 * 2*gamma2, then center and store as Q+r0 */
    vpmulld %ymm11, %ymm1, %ymm2
    vpsubd  %ymm2, %ymm0, %ymm2        /* r0 = a - a1*alpha */
    vpsubd  %ymm2, %ymm10, %ymm3       /* (Q-1)/2 - r0 */
    vpsrad  $31, %ymm3, %ymm3
    vpand   %ymm9, %ymm3, %ymm3
    vpsubd  %ymm3, %ymm2, %ymm2        /* r0 centered */
    vpaddd  %ymm9, %ymm2, %ymm2        /* Q + r0 */

    vmovdqu %ymm1, (%rdi,%rax)          /* a1 */
    vmovdqu %ymm2, (%rsi,%rax)          /* a0 */
    addq    $32, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_dc_g32_loop
    vzeroupper
    ret
.size dap_dilithium_poly_decompose_g32_{{ARCH_LOWER}}, .-dap_dilithium_poly_decompose_g32_{{ARCH_LOWER}}


/* ================================================================
 * poly_decompose_p — gamma2 = (Q-1)/88  (ML-DSA-44)
 * ================================================================ */
.globl dap_dilithium_poly_decompose_g88_{{ARCH_LOWER}}
.type  dap_dilithium_poly_decompose_g88_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_decompose_g88_{{ARCH_LOWER}}:
    movl    $127, %eax
    vmovd   %eax, %xmm15
    vpbroadcastd %xmm15, %ymm15

    movl    $11275, %eax
    vmovd   %eax, %xmm14
    vpbroadcastd %xmm14, %ymm14

    movl    $(1 << 23), %eax
    vmovd   %eax, %xmm13
    vpbroadcastd %xmm13, %ymm13

    movl    $43, %eax
    vmovd   %eax, %xmm12
    vpbroadcastd %xmm12, %ymm12

    movl    $(2 * 95232), %eax
    vmovd   %eax, %xmm11
    vpbroadcastd %xmm11, %ymm11

    movl    $((DIL_Q - 1) / 2), %eax
    vmovd   %eax, %xmm10
    vpbroadcastd %xmm10, %ymm10

    movl    $DIL_Q, %eax
    vmovd   %eax, %xmm9
    vpbroadcastd %xmm9, %ymm9

    xorl    %eax, %eax
    .p2align 4
.L_dc_g88_loop:
    vmovdqu (%rdx,%rax), %ymm0

    vpaddd  %ymm15, %ymm0, %ymm1
    vpsrld  $7, %ymm1, %ymm1
    vpmulld %ymm14, %ymm1, %ymm1
    vpaddd  %ymm13, %ymm1, %ymm1
    vpsrld  $24, %ymm1, %ymm1

    vpsubd  %ymm1, %ymm12, %ymm2
    vpsrad  $31, %ymm2, %ymm2
    vpand   %ymm1, %ymm2, %ymm2
    vpxor   %ymm2, %ymm1, %ymm1        /* a1 clamped */

    vpmulld %ymm11, %ymm1, %ymm2
    vpsubd  %ymm2, %ymm0, %ymm2
    vpsubd  %ymm2, %ymm10, %ymm3
    vpsrad  $31, %ymm3, %ymm3
    vpand   %ymm9, %ymm3, %ymm3
    vpsubd  %ymm3, %ymm2, %ymm2
    vpaddd  %ymm9, %ymm2, %ymm2

    vmovdqu %ymm1, (%rdi,%rax)
    vmovdqu %ymm2, (%rsi,%rax)
    addq    $32, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_dc_g88_loop
    vzeroupper
    ret
.size dap_dilithium_poly_decompose_g88_{{ARCH_LOWER}}, .-dap_dilithium_poly_decompose_g88_{{ARCH_LOWER}}


/* ================================================================
 * polyw1_pack_p — gamma2 = (Q-1)/88  (ML-DSA-44)
 * 6-bit packing: 4 coefficients → 3 bytes
 *
 * void dap_dilithium_polyw1_pack_g88_{{ARCH_LOWER}}(
 *     unsigned char *r, const int32_t *coeffs);
 * ================================================================ */
.globl dap_dilithium_polyw1_pack_g88_{{ARCH_LOWER}}
.type  dap_dilithium_polyw1_pack_g88_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_polyw1_pack_g88_{{ARCH_LOWER}}:
    xorl    %eax, %eax
    xorl    %ecx, %ecx
    .p2align 4
.L_w1p88_loop:
    movl    (%rsi,%rax,4), %r8d
    movl    4(%rsi,%rax,4), %r9d
    movl    8(%rsi,%rax,4), %r10d
    movl    12(%rsi,%rax,4), %r11d

    shll    $6, %r9d
    orl     %r9d, %r8d
    movb    %r8b, (%rdi,%rcx)

    movl    4(%rsi,%rax,4), %r9d
    shrl    $2, %r9d
    shll    $4, %r10d
    orl     %r10d, %r9d
    movb    %r9b, 1(%rdi,%rcx)

    movl    8(%rsi,%rax,4), %r10d
    shrl    $4, %r10d
    shll    $2, %r11d
    orl     %r11d, %r10d
    movb    %r10b, 2(%rdi,%rcx)

    addq    $4, %rax
    addq    $3, %rcx
    cmpq    $DIL_N, %rax
    jne     .L_w1p88_loop
    ret
.size dap_dilithium_polyw1_pack_g88_{{ARCH_LOWER}}, .-dap_dilithium_polyw1_pack_g88_{{ARCH_LOWER}}


/* ================================================================
 * poly_make_hint_p — gamma2 = (Q-1)/32  (ML-DSA-65/87)
 *
 * unsigned dap_dilithium_poly_make_hint_g32_{{ARCH_LOWER}}(
 *     int32_t *h, const int32_t *a, const int32_t *b);
 *
 * hint[i] = (decompose(a).a1 != decompose(b).a1)
 * returns total hint count
 * ================================================================ */
.globl dap_dilithium_poly_make_hint_g32_{{ARCH_LOWER}}
.type  dap_dilithium_poly_make_hint_g32_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_make_hint_g32_{{ARCH_LOWER}}:
    movq    %rdi, %rcx

    movl    $127, %eax
    vmovd   %eax, %xmm15
    vpbroadcastd %xmm15, %ymm15        /* 127 */
    movl    $1025, %eax
    vmovd   %eax, %xmm14
    vpbroadcastd %xmm14, %ymm14        /* magic */
    movl    $(1 << 21), %eax
    vmovd   %eax, %xmm13
    vpbroadcastd %xmm13, %ymm13        /* rounding */
    movl    $15, %eax
    vmovd   %eax, %xmm12
    vpbroadcastd %xmm12, %ymm12        /* 0xF mask */
    movl    $1, %eax
    vmovd   %eax, %xmm11
    vpbroadcastd %xmm11, %ymm11        /* 1 */
    vpxor   %xmm10, %xmm10, %xmm10    /* accumulator */

    xorl    %eax, %eax
    .p2align 4
.L_mh_g32_loop:
    /* decompose(a) → a1_a */
    vmovdqu (%rsi,%rax), %ymm0
    vpaddd  %ymm15, %ymm0, %ymm1
    vpsrld  $7, %ymm1, %ymm1
    vpmulld %ymm14, %ymm1, %ymm1
    vpaddd  %ymm13, %ymm1, %ymm1
    vpsrld  $22, %ymm1, %ymm1
    vpand   %ymm12, %ymm1, %ymm1       /* a1_a */

    /* decompose(b) → a1_b */
    vmovdqu (%rdx,%rax), %ymm0
    vpaddd  %ymm15, %ymm0, %ymm2
    vpsrld  $7, %ymm2, %ymm2
    vpmulld %ymm14, %ymm2, %ymm2
    vpaddd  %ymm13, %ymm2, %ymm2
    vpsrld  $22, %ymm2, %ymm2
    vpand   %ymm12, %ymm2, %ymm2       /* a1_b */

    /* hint = (a1_a != a1_b) ? 1 : 0 */
    vpcmpeqd %ymm2, %ymm1, %ymm3       /* -1 where equal */
    vpandn  %ymm11, %ymm3, %ymm3       /* 1 where NOT equal */
    vmovdqu %ymm3, (%rcx,%rax)

    /* accumulate count */
    vpaddd  %ymm3, %ymm10, %ymm10

    addq    $32, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_mh_g32_loop

    /* horizontal sum of ymm10 */
    vextracti128 $1, %ymm10, %xmm0
    vpaddd  %xmm0, %xmm10, %xmm0
    vpshufd $0x4E, %xmm0, %xmm1        /* swap high/low 64 */
    vpaddd  %xmm1, %xmm0, %xmm0
    vpshufd $0xB1, %xmm0, %xmm1        /* swap adjacent 32 */
    vpaddd  %xmm1, %xmm0, %xmm0
    vmovd   %xmm0, %eax
    vzeroupper
    ret
.size dap_dilithium_poly_make_hint_g32_{{ARCH_LOWER}}, .-dap_dilithium_poly_make_hint_g32_{{ARCH_LOWER}}


/* ================================================================
 * poly_make_hint_p — gamma2 = (Q-1)/88  (ML-DSA-44)
 * ================================================================ */
.globl dap_dilithium_poly_make_hint_g88_{{ARCH_LOWER}}
.type  dap_dilithium_poly_make_hint_g88_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_make_hint_g88_{{ARCH_LOWER}}:
    movq    %rdi, %rcx

    movl    $127, %eax
    vmovd   %eax, %xmm15
    vpbroadcastd %xmm15, %ymm15
    movl    $11275, %eax
    vmovd   %eax, %xmm14
    vpbroadcastd %xmm14, %ymm14
    movl    $(1 << 23), %eax
    vmovd   %eax, %xmm13
    vpbroadcastd %xmm13, %ymm13
    movl    $43, %eax
    vmovd   %eax, %xmm12
    vpbroadcastd %xmm12, %ymm12
    movl    $1, %eax
    vmovd   %eax, %xmm11
    vpbroadcastd %xmm11, %ymm11
    vpxor   %xmm10, %xmm10, %xmm10

    xorl    %eax, %eax
    .p2align 4
.L_mh_g88_loop:
    /* decompose(a) → a1_a (with clamp) */
    vmovdqu (%rsi,%rax), %ymm0
    vpaddd  %ymm15, %ymm0, %ymm1
    vpsrld  $7, %ymm1, %ymm1
    vpmulld %ymm14, %ymm1, %ymm1
    vpaddd  %ymm13, %ymm1, %ymm1
    vpsrld  $24, %ymm1, %ymm1
    vpsubd  %ymm1, %ymm12, %ymm3
    vpsrad  $31, %ymm3, %ymm3
    vpand   %ymm1, %ymm3, %ymm3
    vpxor   %ymm3, %ymm1, %ymm1        /* a1_a clamped */

    /* decompose(b) → a1_b (with clamp) */
    vmovdqu (%rdx,%rax), %ymm0
    vpaddd  %ymm15, %ymm0, %ymm2
    vpsrld  $7, %ymm2, %ymm2
    vpmulld %ymm14, %ymm2, %ymm2
    vpaddd  %ymm13, %ymm2, %ymm2
    vpsrld  $24, %ymm2, %ymm2
    vpsubd  %ymm2, %ymm12, %ymm3
    vpsrad  $31, %ymm3, %ymm3
    vpand   %ymm2, %ymm3, %ymm3
    vpxor   %ymm3, %ymm2, %ymm2        /* a1_b clamped */

    /* hint = (a1_a != a1_b) ? 1 : 0 */
    vpcmpeqd %ymm2, %ymm1, %ymm3
    vpandn  %ymm11, %ymm3, %ymm3
    vmovdqu %ymm3, (%rcx,%rax)
    vpaddd  %ymm3, %ymm10, %ymm10

    addq    $32, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_mh_g88_loop

    vextracti128 $1, %ymm10, %xmm0
    vpaddd  %xmm0, %xmm10, %xmm0
    vpshufd $0x4E, %xmm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm0
    vpshufd $0xB1, %xmm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm0
    vmovd   %xmm0, %eax
    vzeroupper
    ret
.size dap_dilithium_poly_make_hint_g88_{{ARCH_LOWER}}, .-dap_dilithium_poly_make_hint_g88_{{ARCH_LOWER}}
