/*
 * Dilithium polynomial arithmetic ops — AVX-512 assembly.
 * Generated from dap_dilithium_poly_ops_avx512.S.tpl by dap_tpl.
 *
 * 16 int32_t coefficients per ZMM iteration.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#define DIL_N       256
#define DIL_Q       8380417
#define COEFF_BYTES (DIL_N * 4)

.text

/* ================================================================
 * poly_reduce
 * ================================================================ */
.globl dap_dilithium_poly_reduce_{{ARCH_LOWER}}
.type  dap_dilithium_poly_reduce_{{ARCH_LOWER}}, @function
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
.size dap_dilithium_poly_reduce_{{ARCH_LOWER}}, .-dap_dilithium_poly_reduce_{{ARCH_LOWER}}


/* ================================================================
 * poly_csubq
 * ================================================================ */
.globl dap_dilithium_poly_csubq_{{ARCH_LOWER}}
.type  dap_dilithium_poly_csubq_{{ARCH_LOWER}}, @function
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
.size dap_dilithium_poly_csubq_{{ARCH_LOWER}}, .-dap_dilithium_poly_csubq_{{ARCH_LOWER}}


/* ================================================================
 * poly_freeze: reduce then csubq
 * ================================================================ */
.globl dap_dilithium_poly_freeze_{{ARCH_LOWER}}
.type  dap_dilithium_poly_freeze_{{ARCH_LOWER}}, @function
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
.size dap_dilithium_poly_freeze_{{ARCH_LOWER}}, .-dap_dilithium_poly_freeze_{{ARCH_LOWER}}


/* ================================================================
 * poly_add
 * ================================================================ */
.globl dap_dilithium_poly_add_{{ARCH_LOWER}}
.type  dap_dilithium_poly_add_{{ARCH_LOWER}}, @function
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
.size dap_dilithium_poly_add_{{ARCH_LOWER}}, .-dap_dilithium_poly_add_{{ARCH_LOWER}}


/* ================================================================
 * poly_sub
 * ================================================================ */
.globl dap_dilithium_poly_sub_{{ARCH_LOWER}}
.type  dap_dilithium_poly_sub_{{ARCH_LOWER}}, @function
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
.size dap_dilithium_poly_sub_{{ARCH_LOWER}}, .-dap_dilithium_poly_sub_{{ARCH_LOWER}}


/* ================================================================
 * poly_neg
 * ================================================================ */
.globl dap_dilithium_poly_neg_{{ARCH_LOWER}}
.type  dap_dilithium_poly_neg_{{ARCH_LOWER}}, @function
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
.size dap_dilithium_poly_neg_{{ARCH_LOWER}}, .-dap_dilithium_poly_neg_{{ARCH_LOWER}}


/* ================================================================
 * poly_shiftl
 * ================================================================ */
.globl dap_dilithium_poly_shiftl_{{ARCH_LOWER}}
.type  dap_dilithium_poly_shiftl_{{ARCH_LOWER}}, @function
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
.size dap_dilithium_poly_shiftl_{{ARCH_LOWER}}, .-dap_dilithium_poly_shiftl_{{ARCH_LOWER}}


/* ================================================================
 * poly_power2round (legacy D=14)
 * ================================================================ */
.globl dap_dilithium_poly_power2round_{{ARCH_LOWER}}
.type  dap_dilithium_poly_power2round_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_power2round_{{ARCH_LOWER}}:
    movl    $0x3FFF, %ecx
    xorl    %eax, %eax
    vpbroadcastd %ecx, %zmm7
    movl    $(-8193), %ecx
    vpbroadcastd %ecx, %zmm6
    movl    $16384, %ecx
    vpbroadcastd %ecx, %zmm5
    movl    $(DIL_Q - 1 - 8191), %ecx
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
.size dap_dilithium_poly_power2round_{{ARCH_LOWER}}, .-dap_dilithium_poly_power2round_{{ARCH_LOWER}}


/* ================================================================
 * poly_chknorm
 * int dap_dilithium_poly_chknorm_{{ARCH_LOWER}}(const int32_t *, int32_t bound);
 * ================================================================ */
.globl dap_dilithium_poly_chknorm_{{ARCH_LOWER}}
.type  dap_dilithium_poly_chknorm_{{ARCH_LOWER}}, @function
.p2align 4
dap_dilithium_poly_chknorm_{{ARCH_LOWER}}:
    movl    $((DIL_Q - 1) / 2), %eax
    vpbroadcastd %eax, %zmm3
    vpbroadcastd %esi, %zmm4
    vpxord  %zmm5, %zmm5, %zmm5

    xorl    %eax, %eax
    .p2align 4
.L_chknorm512_loop:
    vmovdqu64 (%rdi,%rax), %zmm0
    vpsubd  %zmm0, %zmm3, %zmm1        /* center */
    vpabsd  %zmm1, %zmm1
    vpcmpd  $5, %zmm4, %zmm1, %k1      /* k1 = (|centered| >= bound) */
    kortestw %k1, %k1
    jnz     .L_chknorm512_fail
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
.size dap_dilithium_poly_chknorm_{{ARCH_LOWER}}, .-dap_dilithium_poly_chknorm_{{ARCH_LOWER}}
