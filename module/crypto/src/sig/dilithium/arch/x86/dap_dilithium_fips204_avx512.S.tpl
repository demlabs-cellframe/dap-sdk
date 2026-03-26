/*
 * FIPS 204 ML-DSA SIMD poly ops — AVX-512 assembly.
 * Generated from dap_dilithium_fips204_avx512.S.tpl by dap_tpl.
 *
 * 16 int32_t coefficients per ZMM iteration, mask-register branchless logic.
 *
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
 * ================================================================ */
.globl dap_dilithium_poly_use_hint_g32_{{ARCH_LOWER}}
.type  dap_dilithium_poly_use_hint_g32_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_use_hint_g32_{{ARCH_LOWER}}:
    movq    %rdi, %rcx

    movl    $127, %eax
    vpbroadcastd %eax, %zmm15          /* 127 */
    movl    $1025, %eax
    vpbroadcastd %eax, %zmm14          /* magic multiplier */
    movl    $(1 << 21), %eax
    vpbroadcastd %eax, %zmm13          /* rounding constant */
    movl    $15, %eax
    vpbroadcastd %eax, %zmm12          /* 0xF mask */
    movl    $(2 * 261888), %eax
    vpbroadcastd %eax, %zmm11          /* 2*gamma2 = 523776 */
    movl    $((DIL_Q - 1) / 2), %eax
    vpbroadcastd %eax, %zmm10          /* (Q-1)/2 */
    movl    $DIL_Q, %eax
    vpbroadcastd %eax, %zmm9           /* Q */
    movl    $1, %eax
    vpbroadcastd %eax, %zmm8           /* 1 */

    xorl    %eax, %eax
    .p2align 4
.L_uh512_g32_loop:
    vmovdqu64 (%rsi,%rax), %zmm0       /* val = b[i..i+15] */

    /* a1 = ((val + 127) >> 7 * 1025 + 2^21) >> 22; a1 &= 15 */
    vpaddd  %zmm15, %zmm0, %zmm1
    vpsrld  $7, %zmm1, %zmm1
    vpmulld %zmm14, %zmm1, %zmm1
    vpaddd  %zmm13, %zmm1, %zmm1
    vpsrld  $22, %zmm1, %zmm1
    vpandd  %zmm12, %zmm1, %zmm1       /* a1 */

    /* r0 = val - a1 * 2*gamma2, centered */
    vpmulld %zmm11, %zmm1, %zmm2
    vpsubd  %zmm2, %zmm0, %zmm2
    vpsubd  %zmm2, %zmm10, %zmm3
    vpsrad  $31, %zmm3, %zmm3
    vpandd  %zmm9, %zmm3, %zmm3
    vpsubd  %zmm3, %zmm2, %zmm2        /* r0 centered */

    /* plus1 = (a1 + 1) & 15 */
    vpaddd  %zmm8, %zmm1, %zmm3
    vpandd  %zmm12, %zmm3, %zmm3       /* plus1 */

    /* minus1 = (a1 - 1) & 15 */
    vpsubd  %zmm8, %zmm1, %zmm4
    vpandd  %zmm12, %zmm4, %zmm4       /* minus1 */

    /* hint_result = r0>0 ? plus1 : minus1 */
    vpxord  %zmm5, %zmm5, %zmm5
    vpcmpd  $6, %zmm5, %zmm2, %k1      /* k1 = (r0 > 0) NLE=6 */
    vpblendmd %zmm3, %zmm4, %zmm4{%k1} /* r0>0 ? plus1 : minus1 */

    /* result = hint==0 ? a1 : hint_result */
    vmovdqu64 (%rdx,%rax), %zmm6       /* h[i..i+15] */
    vpcmpeqd %zmm5, %zmm6, %k2         /* k2 = (h == 0) */
    vpblendmd %zmm1, %zmm4, %zmm0{%k2} /* h==0 ? a1 : hint_result */

    vmovdqu64 %zmm0, (%rcx,%rax)
    addq    $64, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_uh512_g32_loop
    vzeroupper
    ret
.size dap_dilithium_poly_use_hint_g32_{{ARCH_LOWER}}, .-dap_dilithium_poly_use_hint_g32_{{ARCH_LOWER}}


/* ================================================================
 * poly_use_hint_p — gamma2 = (Q-1)/88  (ML-DSA-44)
 *
 * void dap_dilithium_poly_use_hint_g88_{{ARCH_LOWER}}(
 *     int32_t *r, const int32_t *b, const int32_t *h);
 * ================================================================ */
.globl dap_dilithium_poly_use_hint_g88_{{ARCH_LOWER}}
.type  dap_dilithium_poly_use_hint_g88_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_use_hint_g88_{{ARCH_LOWER}}:
    movq    %rdi, %rcx

    movl    $127, %eax
    vpbroadcastd %eax, %zmm15          /* 127 */
    movl    $11275, %eax
    vpbroadcastd %eax, %zmm14          /* magic multiplier */
    movl    $(1 << 23), %eax
    vpbroadcastd %eax, %zmm13          /* rounding constant */
    movl    $43, %eax
    vpbroadcastd %eax, %zmm12          /* a1_max = 43 */
    movl    $((DIL_Q - 1) / 2), %eax
    vpbroadcastd %eax, %zmm10          /* (Q-1)/2 */
    movl    $DIL_Q, %eax
    vpbroadcastd %eax, %zmm9           /* Q */
    movl    $1, %eax
    vpbroadcastd %eax, %zmm8           /* 1 */

    /* 2*gamma2 via strength reduction: 190464 = 93 * 2048 */
    /* We precompute in register */
    movl    $(2 * 95232), %eax
    vpbroadcastd %eax, %zmm11          /* 2*gamma2 = 190464 */

    xorl    %eax, %eax
    .p2align 4
.L_uh512_g88_loop:
    vmovdqu64 (%rsi,%rax), %zmm0       /* val = b[i..i+15] */
    vmovdqu64 (%rdx,%rax), %zmm6       /* hint = h[i..i+15] */

    /* a1 = ((val + 127) >> 7 * 11275 + 2^23) >> 24 */
    vpaddd  %zmm15, %zmm0, %zmm1
    vpsrld  $7, %zmm1, %zmm1
    vpmulld %zmm14, %zmm1, %zmm1
    vpaddd  %zmm13, %zmm1, %zmm1
    vpsrld  $24, %zmm1, %zmm1          /* a1_raw */

    /* clamp: a1 ^= ((43 - a1) >> 31) & a1 → if a1>=44, a1=0 */
    vpsubd  %zmm1, %zmm12, %zmm2
    vpsrad  $31, %zmm2, %zmm2
    vpandd  %zmm1, %zmm2, %zmm2
    vpxord  %zmm2, %zmm1, %zmm1        /* a1 clamped */

    /* r0 = val - a1 * 2*gamma2, centered */
    vpmulld %zmm11, %zmm1, %zmm2
    vpsubd  %zmm2, %zmm0, %zmm2
    vpsubd  %zmm2, %zmm10, %zmm3
    vpsrad  $31, %zmm3, %zmm3
    vpandd  %zmm9, %zmm3, %zmm3
    vpsubd  %zmm3, %zmm2, %zmm2        /* r0 centered */

    vpxord  %zmm5, %zmm5, %zmm5

    /* plus1 = (a1 == 43) ? 0 : a1 + 1 */
    vpaddd  %zmm8, %zmm1, %zmm3        /* a1 + 1 */
    vpcmpeqd %zmm12, %zmm1, %k3        /* k3 = (a1 == 43) */
    vpblendmd %zmm5, %zmm3, %zmm3{%k3} /* a1==43 ? 0 : a1+1 */

    /* minus1 = (a1 == 0) ? 43 : a1 - 1 */
    vpsubd  %zmm8, %zmm1, %zmm4        /* a1 - 1 */
    vpcmpeqd %zmm5, %zmm1, %k4         /* k4 = (a1 == 0) */
    vpblendmd %zmm12, %zmm4, %zmm4{%k4} /* a1==0 ? 43 : a1-1 */

    /* hint_result = r0>0 ? plus1 : minus1 */
    vpcmpd  $6, %zmm5, %zmm2, %k1      /* k1 = (r0 > 0) */
    vpblendmd %zmm3, %zmm4, %zmm4{%k1} /* r0>0 ? plus1 : minus1 */

    /* result = hint==0 ? a1 : hint_result */
    vpcmpeqd %zmm5, %zmm6, %k2         /* k2 = (hint == 0) */
    vpblendmd %zmm1, %zmm4, %zmm0{%k2} /* hint==0 ? a1 : hint_result */

    vmovdqu64 %zmm0, (%rcx,%rax)
    addq    $64, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_uh512_g88_loop
    vzeroupper
    ret
.size dap_dilithium_poly_use_hint_g88_{{ARCH_LOWER}}, .-dap_dilithium_poly_use_hint_g88_{{ARCH_LOWER}}


/* ================================================================
 * poly_decompose_p — gamma2 = (Q-1)/32  (ML-DSA-65/87)
 *
 * void dap_dilithium_poly_decompose_g32_{{ARCH_LOWER}}(
 *     int32_t *a1, int32_t *a0, const int32_t *a);
 * ================================================================ */
.globl dap_dilithium_poly_decompose_g32_{{ARCH_LOWER}}
.type  dap_dilithium_poly_decompose_g32_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_decompose_g32_{{ARCH_LOWER}}:
    movl    $127, %eax
    vpbroadcastd %eax, %zmm15
    movl    $1025, %eax
    vpbroadcastd %eax, %zmm14
    movl    $(1 << 21), %eax
    vpbroadcastd %eax, %zmm13
    movl    $15, %eax
    vpbroadcastd %eax, %zmm12
    movl    $(2 * 261888), %eax
    vpbroadcastd %eax, %zmm11
    movl    $((DIL_Q - 1) / 2), %eax
    vpbroadcastd %eax, %zmm10
    movl    $DIL_Q, %eax
    vpbroadcastd %eax, %zmm9

    xorl    %eax, %eax
    .p2align 4
.L_dc512_g32_loop:
    vmovdqu64 (%rdx,%rax), %zmm0

    vpaddd  %zmm15, %zmm0, %zmm1
    vpsrld  $7, %zmm1, %zmm1
    vpmulld %zmm14, %zmm1, %zmm1
    vpaddd  %zmm13, %zmm1, %zmm1
    vpsrld  $22, %zmm1, %zmm1
    vpandd  %zmm12, %zmm1, %zmm1       /* a1 */

    vpmulld %zmm11, %zmm1, %zmm2
    vpsubd  %zmm2, %zmm0, %zmm2
    vpsubd  %zmm2, %zmm10, %zmm3
    vpsrad  $31, %zmm3, %zmm3
    vpandd  %zmm9, %zmm3, %zmm3
    vpsubd  %zmm3, %zmm2, %zmm2
    vpaddd  %zmm9, %zmm2, %zmm2        /* Q + r0 */

    vmovdqu64 %zmm1, (%rdi,%rax)       /* a1 */
    vmovdqu64 %zmm2, (%rsi,%rax)       /* a0 */
    addq    $64, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_dc512_g32_loop
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
    vpbroadcastd %eax, %zmm15
    movl    $11275, %eax
    vpbroadcastd %eax, %zmm14
    movl    $(1 << 23), %eax
    vpbroadcastd %eax, %zmm13
    movl    $43, %eax
    vpbroadcastd %eax, %zmm12
    movl    $(2 * 95232), %eax
    vpbroadcastd %eax, %zmm11
    movl    $((DIL_Q - 1) / 2), %eax
    vpbroadcastd %eax, %zmm10
    movl    $DIL_Q, %eax
    vpbroadcastd %eax, %zmm9

    xorl    %eax, %eax
    .p2align 4
.L_dc512_g88_loop:
    vmovdqu64 (%rdx,%rax), %zmm0

    vpaddd  %zmm15, %zmm0, %zmm1
    vpsrld  $7, %zmm1, %zmm1
    vpmulld %zmm14, %zmm1, %zmm1
    vpaddd  %zmm13, %zmm1, %zmm1
    vpsrld  $24, %zmm1, %zmm1

    vpsubd  %zmm1, %zmm12, %zmm2
    vpsrad  $31, %zmm2, %zmm2
    vpandd  %zmm1, %zmm2, %zmm2
    vpxord  %zmm2, %zmm1, %zmm1        /* a1 clamped */

    vpmulld %zmm11, %zmm1, %zmm2
    vpsubd  %zmm2, %zmm0, %zmm2
    vpsubd  %zmm2, %zmm10, %zmm3
    vpsrad  $31, %zmm3, %zmm3
    vpandd  %zmm9, %zmm3, %zmm3
    vpsubd  %zmm3, %zmm2, %zmm2
    vpaddd  %zmm9, %zmm2, %zmm2        /* Q + r0 */

    vmovdqu64 %zmm1, (%rdi,%rax)
    vmovdqu64 %zmm2, (%rsi,%rax)
    addq    $64, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_dc512_g88_loop
    vzeroupper
    ret
.size dap_dilithium_poly_decompose_g88_{{ARCH_LOWER}}, .-dap_dilithium_poly_decompose_g88_{{ARCH_LOWER}}


/* ================================================================
 * polyw1_pack_p — gamma2 = (Q-1)/88  (ML-DSA-44)
 * 6-bit packing: 4 coefficients → 3 bytes
 * Scalar loop (packing is byte-oriented, not SIMD-friendly)
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
.L_w1p512_88_loop:
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
    jne     .L_w1p512_88_loop
    ret
.size dap_dilithium_polyw1_pack_g88_{{ARCH_LOWER}}, .-dap_dilithium_polyw1_pack_g88_{{ARCH_LOWER}}


/* ================================================================
 * poly_make_hint_p — gamma2 = (Q-1)/32  (ML-DSA-65/87)
 * unsigned dap_dilithium_poly_make_hint_g32_{{ARCH_LOWER}}(
 *     int32_t *h, const int32_t *a, const int32_t *b);
 * ================================================================ */
.globl dap_dilithium_poly_make_hint_g32_{{ARCH_LOWER}}
.type  dap_dilithium_poly_make_hint_g32_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_make_hint_g32_{{ARCH_LOWER}}:
    movq    %rdi, %rcx

    movl    $127, %eax
    vpbroadcastd %eax, %zmm15
    movl    $1025, %eax
    vpbroadcastd %eax, %zmm14
    movl    $(1 << 21), %eax
    vpbroadcastd %eax, %zmm13
    movl    $15, %eax
    vpbroadcastd %eax, %zmm12
    movl    $1, %eax
    vpbroadcastd %eax, %zmm11
    vpxord  %zmm10, %zmm10, %zmm10     /* accumulator */

    xorl    %eax, %eax
    .p2align 4
.L_mh512_g32_loop:
    vmovdqu64 (%rsi,%rax), %zmm0
    vpaddd  %zmm15, %zmm0, %zmm1
    vpsrld  $7, %zmm1, %zmm1
    vpmulld %zmm14, %zmm1, %zmm1
    vpaddd  %zmm13, %zmm1, %zmm1
    vpsrld  $22, %zmm1, %zmm1
    vpandd  %zmm12, %zmm1, %zmm1       /* a1_a */

    vmovdqu64 (%rdx,%rax), %zmm0
    vpaddd  %zmm15, %zmm0, %zmm2
    vpsrld  $7, %zmm2, %zmm2
    vpmulld %zmm14, %zmm2, %zmm2
    vpaddd  %zmm13, %zmm2, %zmm2
    vpsrld  $22, %zmm2, %zmm2
    vpandd  %zmm12, %zmm2, %zmm2       /* a1_b */

    vpcmpd  $4, %zmm2, %zmm1, %k1      /* k1 = (a1_a != a1_b) */
    vpxord  %zmm3, %zmm3, %zmm3
    vpblendmd %zmm11, %zmm3, %zmm3{%k1} /* hint: k1 ? 1 : 0 */
    vmovdqu64 %zmm3, (%rcx,%rax)
    vpaddd  %zmm3, %zmm10, %zmm10

    addq    $64, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_mh512_g32_loop

    /* horizontal sum zmm10 → eax */
    vextracti64x4 $1, %zmm10, %ymm0
    vpaddd  %ymm0, %ymm10, %ymm0
    vextracti128 $1, %ymm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm0
    vpshufd $0x4E, %xmm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm0
    vpshufd $0xB1, %xmm0, %xmm1
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
    vpbroadcastd %eax, %zmm15
    movl    $11275, %eax
    vpbroadcastd %eax, %zmm14
    movl    $(1 << 23), %eax
    vpbroadcastd %eax, %zmm13
    movl    $43, %eax
    vpbroadcastd %eax, %zmm12
    movl    $1, %eax
    vpbroadcastd %eax, %zmm11
    vpxord  %zmm10, %zmm10, %zmm10

    xorl    %eax, %eax
    .p2align 4
.L_mh512_g88_loop:
    vmovdqu64 (%rsi,%rax), %zmm0
    vpaddd  %zmm15, %zmm0, %zmm1
    vpsrld  $7, %zmm1, %zmm1
    vpmulld %zmm14, %zmm1, %zmm1
    vpaddd  %zmm13, %zmm1, %zmm1
    vpsrld  $24, %zmm1, %zmm1
    vpsubd  %zmm1, %zmm12, %zmm3
    vpsrad  $31, %zmm3, %zmm3
    vpandd  %zmm1, %zmm3, %zmm3
    vpxord  %zmm3, %zmm1, %zmm1

    vmovdqu64 (%rdx,%rax), %zmm0
    vpaddd  %zmm15, %zmm0, %zmm2
    vpsrld  $7, %zmm2, %zmm2
    vpmulld %zmm14, %zmm2, %zmm2
    vpaddd  %zmm13, %zmm2, %zmm2
    vpsrld  $24, %zmm2, %zmm2
    vpsubd  %zmm2, %zmm12, %zmm3
    vpsrad  $31, %zmm3, %zmm3
    vpandd  %zmm2, %zmm3, %zmm3
    vpxord  %zmm3, %zmm2, %zmm2

    vpcmpd  $4, %zmm2, %zmm1, %k1
    vpxord  %zmm3, %zmm3, %zmm3
    vpblendmd %zmm11, %zmm3, %zmm3{%k1}
    vmovdqu64 %zmm3, (%rcx,%rax)
    vpaddd  %zmm3, %zmm10, %zmm10

    addq    $64, %rax
    cmpq    $COEFF_BYTES, %rax
    jne     .L_mh512_g88_loop

    vextracti64x4 $1, %zmm10, %ymm0
    vpaddd  %ymm0, %ymm10, %ymm0
    vextracti128 $1, %ymm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm0
    vpshufd $0x4E, %xmm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm0
    vpshufd $0xB1, %xmm0, %xmm1
    vpaddd  %xmm1, %xmm0, %xmm0
    vmovd   %xmm0, %eax
    vzeroupper
    ret
.size dap_dilithium_poly_make_hint_g88_{{ARCH_LOWER}}, .-dap_dilithium_poly_make_hint_g88_{{ARCH_LOWER}}
