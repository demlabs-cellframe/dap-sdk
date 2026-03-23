/*
 * dap_chacha20_avx512vl.S — Hand-tuned ChaCha20 for x86-64 AVX-512VL
 *
 * 2×8 dual-block (1024 bytes/iter), 32 YMM registers, ZERO QR spills.
 * Compiler C-intrinsics version: 44 register spills per QR iteration.
 * This version: 0 spills → ~400 fewer memory ops per dual-block batch.
 *
 * Register plan (QR loop):
 *   ymm0..15:  block A state (s[0]..s[15], each broadcast across 8 lanes)
 *   ymm16..31: block B state (same, counter offset +8)
 *   eax:       double-round counter (10 → 0)
 *
 * Stack layout (rsp-relative, 64-byte aligned):
 *   [0..14]*32:   initial state s[0..11], s[13..15] = 15 broadcast vectors
 *   [15]*32:      a12_init (block A counter vector)
 *   [16]*32:      b12_init (block B counter vector)
 *   [17..32]*32:  block B save area (16 vectors)
 *   [33]*32:      key pointer (8 bytes) + nonce pointer (8 bytes)
 *   Total: 34*32 = 1088 bytes
 *
 * Copyright: DeM Labs Inc. 2026
 * License: GNU GPL v3
 */

#define STKOFF_S0       (0*32)
#define STKOFF_S1       (1*32)
#define STKOFF_S2       (2*32)
#define STKOFF_S3       (3*32)
#define STKOFF_S4       (4*32)
#define STKOFF_S5       (5*32)
#define STKOFF_S6       (6*32)
#define STKOFF_S7       (7*32)
#define STKOFF_S8       (8*32)
#define STKOFF_S9       (9*32)
#define STKOFF_S10      (10*32)
#define STKOFF_S11      (11*32)
#define STKOFF_S13      (12*32)
#define STKOFF_S14      (13*32)
#define STKOFF_S15      (14*32)
#define STKOFF_A12      (15*32)
#define STKOFF_B12      (16*32)
#define STKOFF_BSAVE    (17*32)
#define STKOFF_KEYPTR   (33*32)
#define STKOFF_NONCEPTR (33*32+8)
#define STACK_SIZE      (34*32)

.section .rodata
.balign 64
.Lsigma:
    .long 0x61707865, 0x3320646e, 0x79622d32, 0x6b206574
.Lctr_inc:
    .long 0, 1, 2, 3, 4, 5, 6, 7

.text

.globl  dap_chacha20_encrypt_asm
.type   dap_chacha20_encrypt_asm, @function
.balign 64
dap_chacha20_encrypt_asm:
    /* ── Prologue ── */
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    pushq   %rbp
    movq    %rsp, %rbp
    andq    $-64, %rsp
    subq    $STACK_SIZE, %rsp

    /* Save args for tail-call */
    movq    %rdi, %r12              /* out */
    movq    %rsi, %r13              /* in */
    movq    %rdx, %r14              /* len */
    movl    %r9d, %r15d             /* counter */
    movq    %rcx, STKOFF_KEYPTR(%rsp)
    movq    %r8,  STKOFF_NONCEPTR(%rsp)

    /* Build broadcast initial state on stack */
    vpbroadcastd    .Lsigma+0(%rip), %ymm0
    vpbroadcastd    .Lsigma+4(%rip), %ymm1
    vpbroadcastd    .Lsigma+8(%rip), %ymm2
    vpbroadcastd    .Lsigma+12(%rip), %ymm3
    vmovdqa64   %ymm0,  STKOFF_S0(%rsp)
    vmovdqa64   %ymm1,  STKOFF_S1(%rsp)
    vmovdqa64   %ymm2,  STKOFF_S2(%rsp)
    vmovdqa64   %ymm3,  STKOFF_S3(%rsp)

    vpbroadcastd     0(%rcx), %ymm0
    vpbroadcastd     4(%rcx), %ymm1
    vpbroadcastd     8(%rcx), %ymm2
    vpbroadcastd    12(%rcx), %ymm3
    vpbroadcastd    16(%rcx), %ymm4
    vpbroadcastd    20(%rcx), %ymm5
    vpbroadcastd    24(%rcx), %ymm6
    vpbroadcastd    28(%rcx), %ymm7
    vmovdqa64   %ymm0,  STKOFF_S4(%rsp)
    vmovdqa64   %ymm1,  STKOFF_S5(%rsp)
    vmovdqa64   %ymm2,  STKOFF_S6(%rsp)
    vmovdqa64   %ymm3,  STKOFF_S7(%rsp)
    vmovdqa64   %ymm4,  STKOFF_S8(%rsp)
    vmovdqa64   %ymm5,  STKOFF_S9(%rsp)
    vmovdqa64   %ymm6,  STKOFF_S10(%rsp)
    vmovdqa64   %ymm7,  STKOFF_S11(%rsp)

    vpbroadcastd    0(%r8), %ymm0
    vpbroadcastd    4(%r8), %ymm1
    vpbroadcastd    8(%r8), %ymm2
    vmovdqa64   %ymm0,  STKOFF_S13(%rsp)
    vmovdqa64   %ymm1,  STKOFF_S14(%rsp)
    vmovdqa64   %ymm2,  STKOFF_S15(%rsp)

    cmpq    $1024, %r14
    jb      .Ltail

/* ══════════════════════════════════════════════════════════════════ */
/* Dual-block main loop: 2×8 blocks = 1024 bytes per iteration      */
/* ══════════════════════════════════════════════════════════════════ */
.balign 64
.Ldual_loop:
    /* ── Load block A state (ymm0..15) ── */
    vmovdqa64   STKOFF_S0(%rsp),  %ymm0
    vmovdqa64   STKOFF_S1(%rsp),  %ymm1
    vmovdqa64   STKOFF_S2(%rsp),  %ymm2
    vmovdqa64   STKOFF_S3(%rsp),  %ymm3
    vmovdqa64   STKOFF_S4(%rsp),  %ymm4
    vmovdqa64   STKOFF_S5(%rsp),  %ymm5
    vmovdqa64   STKOFF_S6(%rsp),  %ymm6
    vmovdqa64   STKOFF_S7(%rsp),  %ymm7
    vmovdqa64   STKOFF_S8(%rsp),  %ymm8
    vmovdqa64   STKOFF_S9(%rsp),  %ymm9
    vmovdqa64   STKOFF_S10(%rsp), %ymm10
    vmovdqa64   STKOFF_S11(%rsp), %ymm11
    vpbroadcastd    %r15d, %ymm12
    vpaddd      .Lctr_inc(%rip), %ymm12, %ymm12
    vmovdqa64   %ymm12, STKOFF_A12(%rsp)       /* save a12_init */
    vmovdqa64   STKOFF_S13(%rsp), %ymm13
    vmovdqa64   STKOFF_S14(%rsp), %ymm14
    vmovdqa64   STKOFF_S15(%rsp), %ymm15

    /* ── Load block B state (ymm16..31) ── */
    vmovdqa64   STKOFF_S0(%rsp),  %ymm16
    vmovdqa64   STKOFF_S1(%rsp),  %ymm17
    vmovdqa64   STKOFF_S2(%rsp),  %ymm18
    vmovdqa64   STKOFF_S3(%rsp),  %ymm19
    vmovdqa64   STKOFF_S4(%rsp),  %ymm20
    vmovdqa64   STKOFF_S5(%rsp),  %ymm21
    vmovdqa64   STKOFF_S6(%rsp),  %ymm22
    vmovdqa64   STKOFF_S7(%rsp),  %ymm23
    vmovdqa64   STKOFF_S8(%rsp),  %ymm24
    vmovdqa64   STKOFF_S9(%rsp),  %ymm25
    vmovdqa64   STKOFF_S10(%rsp), %ymm26
    vmovdqa64   STKOFF_S11(%rsp), %ymm27
    leal    8(%r15d), %eax
    vpbroadcastd    %eax, %ymm28
    vpaddd      .Lctr_inc(%rip), %ymm28, %ymm28
    vmovdqa64   %ymm28, STKOFF_B12(%rsp)       /* save b12_init */
    vmovdqa64   STKOFF_S13(%rsp), %ymm29
    vmovdqa64   STKOFF_S14(%rsp), %ymm30
    vmovdqa64   STKOFF_S15(%rsp), %ymm31

    /* ═══════════════════════════════════════════════════════════ */
    /* QR loop: 10 double-rounds, ZERO register spills.           */
    /* All 32 YMM registers hold live state — nothing on stack.   */
    /* ═══════════════════════════════════════════════════════════ */
    movl    $10, %eax
.balign 32
.Lqr_loop:

    /* Column round: QR(0,4,8,12) QR(1,5,9,13) QR(2,6,10,14) QR(3,7,11,15) */
    /* Interleaved: block A instruction, then block B — maximizes ILP */

    /* QR col 0: A(0,4,8,12) + B(16,20,24,28) */
    vpaddd  %ymm4,  %ymm0,  %ymm0
    vpaddd  %ymm20, %ymm16, %ymm16
    vpxord  %ymm0,  %ymm12, %ymm12
    vpxord  %ymm16, %ymm28, %ymm28
    vprold  $16, %ymm12, %ymm12
    vprold  $16, %ymm28, %ymm28
    vpaddd  %ymm12, %ymm8,  %ymm8
    vpaddd  %ymm28, %ymm24, %ymm24
    vpxord  %ymm8,  %ymm4,  %ymm4
    vpxord  %ymm24, %ymm20, %ymm20
    vprold  $12, %ymm4,  %ymm4
    vprold  $12, %ymm20, %ymm20
    vpaddd  %ymm4,  %ymm0,  %ymm0
    vpaddd  %ymm20, %ymm16, %ymm16
    vpxord  %ymm0,  %ymm12, %ymm12
    vpxord  %ymm16, %ymm28, %ymm28
    vprold  $8, %ymm12, %ymm12
    vprold  $8, %ymm28, %ymm28
    vpaddd  %ymm12, %ymm8,  %ymm8
    vpaddd  %ymm28, %ymm24, %ymm24
    vpxord  %ymm8,  %ymm4,  %ymm4
    vpxord  %ymm24, %ymm20, %ymm20
    vprold  $7, %ymm4,  %ymm4
    vprold  $7, %ymm20, %ymm20

    /* QR col 1: A(1,5,9,13) + B(17,21,25,29) */
    vpaddd  %ymm5,  %ymm1,  %ymm1
    vpaddd  %ymm21, %ymm17, %ymm17
    vpxord  %ymm1,  %ymm13, %ymm13
    vpxord  %ymm17, %ymm29, %ymm29
    vprold  $16, %ymm13, %ymm13
    vprold  $16, %ymm29, %ymm29
    vpaddd  %ymm13, %ymm9,  %ymm9
    vpaddd  %ymm29, %ymm25, %ymm25
    vpxord  %ymm9,  %ymm5,  %ymm5
    vpxord  %ymm25, %ymm21, %ymm21
    vprold  $12, %ymm5,  %ymm5
    vprold  $12, %ymm21, %ymm21
    vpaddd  %ymm5,  %ymm1,  %ymm1
    vpaddd  %ymm21, %ymm17, %ymm17
    vpxord  %ymm1,  %ymm13, %ymm13
    vpxord  %ymm17, %ymm29, %ymm29
    vprold  $8, %ymm13, %ymm13
    vprold  $8, %ymm29, %ymm29
    vpaddd  %ymm13, %ymm9,  %ymm9
    vpaddd  %ymm29, %ymm25, %ymm25
    vpxord  %ymm9,  %ymm5,  %ymm5
    vpxord  %ymm25, %ymm21, %ymm21
    vprold  $7, %ymm5,  %ymm5
    vprold  $7, %ymm21, %ymm21

    /* QR col 2: A(2,6,10,14) + B(18,22,26,30) */
    vpaddd  %ymm6,  %ymm2,  %ymm2
    vpaddd  %ymm22, %ymm18, %ymm18
    vpxord  %ymm2,  %ymm14, %ymm14
    vpxord  %ymm18, %ymm30, %ymm30
    vprold  $16, %ymm14, %ymm14
    vprold  $16, %ymm30, %ymm30
    vpaddd  %ymm14, %ymm10, %ymm10
    vpaddd  %ymm30, %ymm26, %ymm26
    vpxord  %ymm10, %ymm6,  %ymm6
    vpxord  %ymm26, %ymm22, %ymm22
    vprold  $12, %ymm6,  %ymm6
    vprold  $12, %ymm22, %ymm22
    vpaddd  %ymm6,  %ymm2,  %ymm2
    vpaddd  %ymm22, %ymm18, %ymm18
    vpxord  %ymm2,  %ymm14, %ymm14
    vpxord  %ymm18, %ymm30, %ymm30
    vprold  $8, %ymm14, %ymm14
    vprold  $8, %ymm30, %ymm30
    vpaddd  %ymm14, %ymm10, %ymm10
    vpaddd  %ymm30, %ymm26, %ymm26
    vpxord  %ymm10, %ymm6,  %ymm6
    vpxord  %ymm26, %ymm22, %ymm22
    vprold  $7, %ymm6,  %ymm6
    vprold  $7, %ymm22, %ymm22

    /* QR col 3: A(3,7,11,15) + B(19,23,27,31) */
    vpaddd  %ymm7,  %ymm3,  %ymm3
    vpaddd  %ymm23, %ymm19, %ymm19
    vpxord  %ymm3,  %ymm15, %ymm15
    vpxord  %ymm19, %ymm31, %ymm31
    vprold  $16, %ymm15, %ymm15
    vprold  $16, %ymm31, %ymm31
    vpaddd  %ymm15, %ymm11, %ymm11
    vpaddd  %ymm31, %ymm27, %ymm27
    vpxord  %ymm11, %ymm7,  %ymm7
    vpxord  %ymm27, %ymm23, %ymm23
    vprold  $12, %ymm7,  %ymm7
    vprold  $12, %ymm23, %ymm23
    vpaddd  %ymm7,  %ymm3,  %ymm3
    vpaddd  %ymm23, %ymm19, %ymm19
    vpxord  %ymm3,  %ymm15, %ymm15
    vpxord  %ymm19, %ymm31, %ymm31
    vprold  $8, %ymm15, %ymm15
    vprold  $8, %ymm31, %ymm31
    vpaddd  %ymm15, %ymm11, %ymm11
    vpaddd  %ymm31, %ymm27, %ymm27
    vpxord  %ymm11, %ymm7,  %ymm7
    vpxord  %ymm27, %ymm23, %ymm23
    vprold  $7, %ymm7,  %ymm7
    vprold  $7, %ymm23, %ymm23

    /* Diagonal round: QR(0,5,10,15) QR(1,6,11,12) QR(2,7,8,13) QR(3,4,9,14) */

    /* QR diag 0: A(0,5,10,15) + B(16,21,26,31) */
    vpaddd  %ymm5,  %ymm0,  %ymm0
    vpaddd  %ymm21, %ymm16, %ymm16
    vpxord  %ymm0,  %ymm15, %ymm15
    vpxord  %ymm16, %ymm31, %ymm31
    vprold  $16, %ymm15, %ymm15
    vprold  $16, %ymm31, %ymm31
    vpaddd  %ymm15, %ymm10, %ymm10
    vpaddd  %ymm31, %ymm26, %ymm26
    vpxord  %ymm10, %ymm5,  %ymm5
    vpxord  %ymm26, %ymm21, %ymm21
    vprold  $12, %ymm5,  %ymm5
    vprold  $12, %ymm21, %ymm21
    vpaddd  %ymm5,  %ymm0,  %ymm0
    vpaddd  %ymm21, %ymm16, %ymm16
    vpxord  %ymm0,  %ymm15, %ymm15
    vpxord  %ymm16, %ymm31, %ymm31
    vprold  $8, %ymm15, %ymm15
    vprold  $8, %ymm31, %ymm31
    vpaddd  %ymm15, %ymm10, %ymm10
    vpaddd  %ymm31, %ymm26, %ymm26
    vpxord  %ymm10, %ymm5,  %ymm5
    vpxord  %ymm26, %ymm21, %ymm21
    vprold  $7, %ymm5,  %ymm5
    vprold  $7, %ymm21, %ymm21

    /* QR diag 1: A(1,6,11,12) + B(17,22,27,28) */
    vpaddd  %ymm6,  %ymm1,  %ymm1
    vpaddd  %ymm22, %ymm17, %ymm17
    vpxord  %ymm1,  %ymm12, %ymm12
    vpxord  %ymm17, %ymm28, %ymm28
    vprold  $16, %ymm12, %ymm12
    vprold  $16, %ymm28, %ymm28
    vpaddd  %ymm12, %ymm11, %ymm11
    vpaddd  %ymm28, %ymm27, %ymm27
    vpxord  %ymm11, %ymm6,  %ymm6
    vpxord  %ymm27, %ymm22, %ymm22
    vprold  $12, %ymm6,  %ymm6
    vprold  $12, %ymm22, %ymm22
    vpaddd  %ymm6,  %ymm1,  %ymm1
    vpaddd  %ymm22, %ymm17, %ymm17
    vpxord  %ymm1,  %ymm12, %ymm12
    vpxord  %ymm17, %ymm28, %ymm28
    vprold  $8, %ymm12, %ymm12
    vprold  $8, %ymm28, %ymm28
    vpaddd  %ymm12, %ymm11, %ymm11
    vpaddd  %ymm28, %ymm27, %ymm27
    vpxord  %ymm11, %ymm6,  %ymm6
    vpxord  %ymm27, %ymm22, %ymm22
    vprold  $7, %ymm6,  %ymm6
    vprold  $7, %ymm22, %ymm22

    /* QR diag 2: A(2,7,8,13) + B(18,23,24,29) */
    vpaddd  %ymm7,  %ymm2,  %ymm2
    vpaddd  %ymm23, %ymm18, %ymm18
    vpxord  %ymm2,  %ymm13, %ymm13
    vpxord  %ymm18, %ymm29, %ymm29
    vprold  $16, %ymm13, %ymm13
    vprold  $16, %ymm29, %ymm29
    vpaddd  %ymm13, %ymm8,  %ymm8
    vpaddd  %ymm29, %ymm24, %ymm24
    vpxord  %ymm8,  %ymm7,  %ymm7
    vpxord  %ymm24, %ymm23, %ymm23
    vprold  $12, %ymm7,  %ymm7
    vprold  $12, %ymm23, %ymm23
    vpaddd  %ymm7,  %ymm2,  %ymm2
    vpaddd  %ymm23, %ymm18, %ymm18
    vpxord  %ymm2,  %ymm13, %ymm13
    vpxord  %ymm18, %ymm29, %ymm29
    vprold  $8, %ymm13, %ymm13
    vprold  $8, %ymm29, %ymm29
    vpaddd  %ymm13, %ymm8,  %ymm8
    vpaddd  %ymm29, %ymm24, %ymm24
    vpxord  %ymm8,  %ymm7,  %ymm7
    vpxord  %ymm24, %ymm23, %ymm23
    vprold  $7, %ymm7,  %ymm7
    vprold  $7, %ymm23, %ymm23

    /* QR diag 3: A(3,4,9,14) + B(19,20,25,30) */
    vpaddd  %ymm4,  %ymm3,  %ymm3
    vpaddd  %ymm20, %ymm19, %ymm19
    vpxord  %ymm3,  %ymm14, %ymm14
    vpxord  %ymm19, %ymm30, %ymm30
    vprold  $16, %ymm14, %ymm14
    vprold  $16, %ymm30, %ymm30
    vpaddd  %ymm14, %ymm9,  %ymm9
    vpaddd  %ymm30, %ymm25, %ymm25
    vpxord  %ymm9,  %ymm4,  %ymm4
    vpxord  %ymm25, %ymm20, %ymm20
    vprold  $12, %ymm4,  %ymm4
    vprold  $12, %ymm20, %ymm20
    vpaddd  %ymm4,  %ymm3,  %ymm3
    vpaddd  %ymm20, %ymm19, %ymm19
    vpxord  %ymm3,  %ymm14, %ymm14
    vpxord  %ymm19, %ymm30, %ymm30
    vprold  $8, %ymm14, %ymm14
    vprold  $8, %ymm30, %ymm30
    vpaddd  %ymm14, %ymm9,  %ymm9
    vpaddd  %ymm30, %ymm25, %ymm25
    vpxord  %ymm9,  %ymm4,  %ymm4
    vpxord  %ymm25, %ymm20, %ymm20
    vprold  $7, %ymm4,  %ymm4
    vprold  $7, %ymm20, %ymm20

    decl    %eax
    jnz     .Lqr_loop

    /* ═══════════════════════════════════════════════════════════ */
    /* Phase 2: Save block B → finalize A → transpose+XOR+store  */
    /*          Then load B → finalize → transpose+XOR+store     */
    /* ═══════════════════════════════════════════════════════════ */

    /* Save block B QR results to stack */
    vmovdqa64   %ymm16, (STKOFF_BSAVE + 0*32)(%rsp)
    vmovdqa64   %ymm17, (STKOFF_BSAVE + 1*32)(%rsp)
    vmovdqa64   %ymm18, (STKOFF_BSAVE + 2*32)(%rsp)
    vmovdqa64   %ymm19, (STKOFF_BSAVE + 3*32)(%rsp)
    vmovdqa64   %ymm20, (STKOFF_BSAVE + 4*32)(%rsp)
    vmovdqa64   %ymm21, (STKOFF_BSAVE + 5*32)(%rsp)
    vmovdqa64   %ymm22, (STKOFF_BSAVE + 6*32)(%rsp)
    vmovdqa64   %ymm23, (STKOFF_BSAVE + 7*32)(%rsp)
    vmovdqa64   %ymm24, (STKOFF_BSAVE + 8*32)(%rsp)
    vmovdqa64   %ymm25, (STKOFF_BSAVE + 9*32)(%rsp)
    vmovdqa64   %ymm26, (STKOFF_BSAVE + 10*32)(%rsp)
    vmovdqa64   %ymm27, (STKOFF_BSAVE + 11*32)(%rsp)
    vmovdqa64   %ymm28, (STKOFF_BSAVE + 12*32)(%rsp)
    vmovdqa64   %ymm29, (STKOFF_BSAVE + 13*32)(%rsp)
    vmovdqa64   %ymm30, (STKOFF_BSAVE + 14*32)(%rsp)
    vmovdqa64   %ymm31, (STKOFF_BSAVE + 15*32)(%rsp)

    /* ── Finalize block A: add initial state ── */
    vpaddd  STKOFF_S0(%rsp),  %ymm0,  %ymm0
    vpaddd  STKOFF_S1(%rsp),  %ymm1,  %ymm1
    vpaddd  STKOFF_S2(%rsp),  %ymm2,  %ymm2
    vpaddd  STKOFF_S3(%rsp),  %ymm3,  %ymm3
    vpaddd  STKOFF_S4(%rsp),  %ymm4,  %ymm4
    vpaddd  STKOFF_S5(%rsp),  %ymm5,  %ymm5
    vpaddd  STKOFF_S6(%rsp),  %ymm6,  %ymm6
    vpaddd  STKOFF_S7(%rsp),  %ymm7,  %ymm7
    vpaddd  STKOFF_S8(%rsp),  %ymm8,  %ymm8
    vpaddd  STKOFF_S9(%rsp),  %ymm9,  %ymm9
    vpaddd  STKOFF_S10(%rsp), %ymm10, %ymm10
    vpaddd  STKOFF_S11(%rsp), %ymm11, %ymm11
    vpaddd  STKOFF_A12(%rsp), %ymm12, %ymm12
    vpaddd  STKOFF_S13(%rsp), %ymm13, %ymm13
    vpaddd  STKOFF_S14(%rsp), %ymm14, %ymm14
    vpaddd  STKOFF_S15(%rsp), %ymm15, %ymm15

    /* ── Transpose block A words 0-7 + XOR + store ── */
    /* Input: ymm0..7  Scratch: ymm16..23  Output base: %r12/%r13 */

    /* Stage 1: 32-bit interleave */
    vpunpckldq  %ymm1, %ymm0, %ymm16
    vpunpckhdq  %ymm1, %ymm0, %ymm17
    vpunpckldq  %ymm3, %ymm2, %ymm18
    vpunpckhdq  %ymm3, %ymm2, %ymm19
    vpunpckldq  %ymm5, %ymm4, %ymm20
    vpunpckhdq  %ymm5, %ymm4, %ymm21
    vpunpckldq  %ymm7, %ymm6, %ymm22
    vpunpckhdq  %ymm7, %ymm6, %ymm23

    /* Stage 2: 64-bit interleave */
    vpunpcklqdq %ymm18, %ymm16, %ymm0
    vpunpckhqdq %ymm18, %ymm16, %ymm1
    vpunpcklqdq %ymm19, %ymm17, %ymm2
    vpunpckhqdq %ymm19, %ymm17, %ymm3
    vpunpcklqdq %ymm22, %ymm20, %ymm4
    vpunpckhqdq %ymm22, %ymm20, %ymm5
    vpunpcklqdq %ymm23, %ymm21, %ymm6
    vpunpckhqdq %ymm23, %ymm21, %ymm7

    /* Stage 3: 128-bit cross-lane + XOR plaintext + store */
    vshufi32x4  $0, %ymm4, %ymm0, %ymm16
    vpxord      0(%r13), %ymm16, %ymm16
    vmovdqu64   %ymm16, 0(%r12)

    vshufi32x4  $0, %ymm5, %ymm1, %ymm17
    vpxord      64(%r13), %ymm17, %ymm17
    vmovdqu64   %ymm17, 64(%r12)

    vshufi32x4  $0, %ymm6, %ymm2, %ymm18
    vpxord      128(%r13), %ymm18, %ymm18
    vmovdqu64   %ymm18, 128(%r12)

    vshufi32x4  $0, %ymm7, %ymm3, %ymm19
    vpxord      192(%r13), %ymm19, %ymm19
    vmovdqu64   %ymm19, 192(%r12)

    vshufi32x4  $3, %ymm4, %ymm0, %ymm20
    vpxord      256(%r13), %ymm20, %ymm20
    vmovdqu64   %ymm20, 256(%r12)

    vshufi32x4  $3, %ymm5, %ymm1, %ymm21
    vpxord      320(%r13), %ymm21, %ymm21
    vmovdqu64   %ymm21, 320(%r12)

    vshufi32x4  $3, %ymm6, %ymm2, %ymm22
    vpxord      384(%r13), %ymm22, %ymm22
    vmovdqu64   %ymm22, 384(%r12)

    vshufi32x4  $3, %ymm7, %ymm3, %ymm23
    vpxord      448(%r13), %ymm23, %ymm23
    vmovdqu64   %ymm23, 448(%r12)

    /* ── Transpose block A words 8-15 + XOR + store ── */
    /* Input: ymm8..15  Scratch: ymm0..7  byte_off=32 */

    vpunpckldq  %ymm9,  %ymm8,  %ymm0
    vpunpckhdq  %ymm9,  %ymm8,  %ymm1
    vpunpckldq  %ymm11, %ymm10, %ymm2
    vpunpckhdq  %ymm11, %ymm10, %ymm3
    vpunpckldq  %ymm13, %ymm12, %ymm4
    vpunpckhdq  %ymm13, %ymm12, %ymm5
    vpunpckldq  %ymm15, %ymm14, %ymm6
    vpunpckhdq  %ymm15, %ymm14, %ymm7

    vpunpcklqdq %ymm2, %ymm0, %ymm8
    vpunpckhqdq %ymm2, %ymm0, %ymm9
    vpunpcklqdq %ymm3, %ymm1, %ymm10
    vpunpckhqdq %ymm3, %ymm1, %ymm11
    vpunpcklqdq %ymm6, %ymm4, %ymm12
    vpunpckhqdq %ymm6, %ymm4, %ymm13
    vpunpcklqdq %ymm7, %ymm5, %ymm14
    vpunpckhqdq %ymm7, %ymm5, %ymm15

    vshufi32x4  $0, %ymm12, %ymm8,  %ymm0
    vpxord      32(%r13), %ymm0, %ymm0
    vmovdqu64   %ymm0, 32(%r12)

    vshufi32x4  $0, %ymm13, %ymm9,  %ymm1
    vpxord      96(%r13), %ymm1, %ymm1
    vmovdqu64   %ymm1, 96(%r12)

    vshufi32x4  $0, %ymm14, %ymm10, %ymm2
    vpxord      160(%r13), %ymm2, %ymm2
    vmovdqu64   %ymm2, 160(%r12)

    vshufi32x4  $0, %ymm15, %ymm11, %ymm3
    vpxord      224(%r13), %ymm3, %ymm3
    vmovdqu64   %ymm3, 224(%r12)

    vshufi32x4  $3, %ymm12, %ymm8,  %ymm4
    vpxord      288(%r13), %ymm4, %ymm4
    vmovdqu64   %ymm4, 288(%r12)

    vshufi32x4  $3, %ymm13, %ymm9,  %ymm5
    vpxord      352(%r13), %ymm5, %ymm5
    vmovdqu64   %ymm5, 352(%r12)

    vshufi32x4  $3, %ymm14, %ymm10, %ymm6
    vpxord      416(%r13), %ymm6, %ymm6
    vmovdqu64   %ymm6, 416(%r12)

    vshufi32x4  $3, %ymm15, %ymm11, %ymm7
    vpxord      480(%r13), %ymm7, %ymm7
    vmovdqu64   %ymm7, 480(%r12)

    /* ═══════════════════════════════════════════════════ */
    /* Block B: load from stack, finalize, transpose+store */
    /* ═══════════════════════════════════════════════════ */

    vmovdqa64   (STKOFF_BSAVE + 0*32)(%rsp),  %ymm0
    vmovdqa64   (STKOFF_BSAVE + 1*32)(%rsp),  %ymm1
    vmovdqa64   (STKOFF_BSAVE + 2*32)(%rsp),  %ymm2
    vmovdqa64   (STKOFF_BSAVE + 3*32)(%rsp),  %ymm3
    vmovdqa64   (STKOFF_BSAVE + 4*32)(%rsp),  %ymm4
    vmovdqa64   (STKOFF_BSAVE + 5*32)(%rsp),  %ymm5
    vmovdqa64   (STKOFF_BSAVE + 6*32)(%rsp),  %ymm6
    vmovdqa64   (STKOFF_BSAVE + 7*32)(%rsp),  %ymm7
    vmovdqa64   (STKOFF_BSAVE + 8*32)(%rsp),  %ymm8
    vmovdqa64   (STKOFF_BSAVE + 9*32)(%rsp),  %ymm9
    vmovdqa64   (STKOFF_BSAVE + 10*32)(%rsp), %ymm10
    vmovdqa64   (STKOFF_BSAVE + 11*32)(%rsp), %ymm11
    vmovdqa64   (STKOFF_BSAVE + 12*32)(%rsp), %ymm12
    vmovdqa64   (STKOFF_BSAVE + 13*32)(%rsp), %ymm13
    vmovdqa64   (STKOFF_BSAVE + 14*32)(%rsp), %ymm14
    vmovdqa64   (STKOFF_BSAVE + 15*32)(%rsp), %ymm15

    vpaddd  STKOFF_S0(%rsp),  %ymm0,  %ymm0
    vpaddd  STKOFF_S1(%rsp),  %ymm1,  %ymm1
    vpaddd  STKOFF_S2(%rsp),  %ymm2,  %ymm2
    vpaddd  STKOFF_S3(%rsp),  %ymm3,  %ymm3
    vpaddd  STKOFF_S4(%rsp),  %ymm4,  %ymm4
    vpaddd  STKOFF_S5(%rsp),  %ymm5,  %ymm5
    vpaddd  STKOFF_S6(%rsp),  %ymm6,  %ymm6
    vpaddd  STKOFF_S7(%rsp),  %ymm7,  %ymm7
    vpaddd  STKOFF_S8(%rsp),  %ymm8,  %ymm8
    vpaddd  STKOFF_S9(%rsp),  %ymm9,  %ymm9
    vpaddd  STKOFF_S10(%rsp), %ymm10, %ymm10
    vpaddd  STKOFF_S11(%rsp), %ymm11, %ymm11
    vpaddd  STKOFF_B12(%rsp), %ymm12, %ymm12
    vpaddd  STKOFF_S13(%rsp), %ymm13, %ymm13
    vpaddd  STKOFF_S14(%rsp), %ymm14, %ymm14
    vpaddd  STKOFF_S15(%rsp), %ymm15, %ymm15

    /* Transpose block B words 0-7 + XOR + store (output at +512) */
    vpunpckldq  %ymm1, %ymm0, %ymm16
    vpunpckhdq  %ymm1, %ymm0, %ymm17
    vpunpckldq  %ymm3, %ymm2, %ymm18
    vpunpckhdq  %ymm3, %ymm2, %ymm19
    vpunpckldq  %ymm5, %ymm4, %ymm20
    vpunpckhdq  %ymm5, %ymm4, %ymm21
    vpunpckldq  %ymm7, %ymm6, %ymm22
    vpunpckhdq  %ymm7, %ymm6, %ymm23

    vpunpcklqdq %ymm18, %ymm16, %ymm0
    vpunpckhqdq %ymm18, %ymm16, %ymm1
    vpunpcklqdq %ymm19, %ymm17, %ymm2
    vpunpckhqdq %ymm19, %ymm17, %ymm3
    vpunpcklqdq %ymm22, %ymm20, %ymm4
    vpunpckhqdq %ymm22, %ymm20, %ymm5
    vpunpcklqdq %ymm23, %ymm21, %ymm6
    vpunpckhqdq %ymm23, %ymm21, %ymm7

    vshufi32x4  $0, %ymm4, %ymm0, %ymm16
    vpxord      512(%r13), %ymm16, %ymm16
    vmovdqu64   %ymm16, 512(%r12)

    vshufi32x4  $0, %ymm5, %ymm1, %ymm17
    vpxord      576(%r13), %ymm17, %ymm17
    vmovdqu64   %ymm17, 576(%r12)

    vshufi32x4  $0, %ymm6, %ymm2, %ymm18
    vpxord      640(%r13), %ymm18, %ymm18
    vmovdqu64   %ymm18, 640(%r12)

    vshufi32x4  $0, %ymm7, %ymm3, %ymm19
    vpxord      704(%r13), %ymm19, %ymm19
    vmovdqu64   %ymm19, 704(%r12)

    vshufi32x4  $3, %ymm4, %ymm0, %ymm20
    vpxord      768(%r13), %ymm20, %ymm20
    vmovdqu64   %ymm20, 768(%r12)

    vshufi32x4  $3, %ymm5, %ymm1, %ymm21
    vpxord      832(%r13), %ymm21, %ymm21
    vmovdqu64   %ymm21, 832(%r12)

    vshufi32x4  $3, %ymm6, %ymm2, %ymm22
    vpxord      896(%r13), %ymm22, %ymm22
    vmovdqu64   %ymm22, 896(%r12)

    vshufi32x4  $3, %ymm7, %ymm3, %ymm23
    vpxord      960(%r13), %ymm23, %ymm23
    vmovdqu64   %ymm23, 960(%r12)

    /* Transpose block B words 8-15 + XOR + store */
    vpunpckldq  %ymm9,  %ymm8,  %ymm0
    vpunpckhdq  %ymm9,  %ymm8,  %ymm1
    vpunpckldq  %ymm11, %ymm10, %ymm2
    vpunpckhdq  %ymm11, %ymm10, %ymm3
    vpunpckldq  %ymm13, %ymm12, %ymm4
    vpunpckhdq  %ymm13, %ymm12, %ymm5
    vpunpckldq  %ymm15, %ymm14, %ymm6
    vpunpckhdq  %ymm15, %ymm14, %ymm7

    vpunpcklqdq %ymm2, %ymm0, %ymm8
    vpunpckhqdq %ymm2, %ymm0, %ymm9
    vpunpcklqdq %ymm3, %ymm1, %ymm10
    vpunpckhqdq %ymm3, %ymm1, %ymm11
    vpunpcklqdq %ymm6, %ymm4, %ymm12
    vpunpckhqdq %ymm6, %ymm4, %ymm13
    vpunpcklqdq %ymm7, %ymm5, %ymm14
    vpunpckhqdq %ymm7, %ymm5, %ymm15

    vshufi32x4  $0, %ymm12, %ymm8,  %ymm0
    vpxord      544(%r13), %ymm0, %ymm0
    vmovdqu64   %ymm0, 544(%r12)

    vshufi32x4  $0, %ymm13, %ymm9,  %ymm1
    vpxord      608(%r13), %ymm1, %ymm1
    vmovdqu64   %ymm1, 608(%r12)

    vshufi32x4  $0, %ymm14, %ymm10, %ymm2
    vpxord      672(%r13), %ymm2, %ymm2
    vmovdqu64   %ymm2, 672(%r12)

    vshufi32x4  $0, %ymm15, %ymm11, %ymm3
    vpxord      736(%r13), %ymm3, %ymm3
    vmovdqu64   %ymm3, 736(%r12)

    vshufi32x4  $3, %ymm12, %ymm8,  %ymm4
    vpxord      800(%r13), %ymm4, %ymm4
    vmovdqu64   %ymm4, 800(%r12)

    vshufi32x4  $3, %ymm13, %ymm9,  %ymm5
    vpxord      864(%r13), %ymm5, %ymm5
    vmovdqu64   %ymm5, 864(%r12)

    vshufi32x4  $3, %ymm14, %ymm10, %ymm6
    vpxord      928(%r13), %ymm6, %ymm6
    vmovdqu64   %ymm6, 928(%r12)

    vshufi32x4  $3, %ymm15, %ymm11, %ymm7
    vpxord      992(%r13), %ymm7, %ymm7
    vmovdqu64   %ymm7, 992(%r12)

    /* ── Advance pointers and loop ── */
    addq    $1024, %r12
    addq    $1024, %r13
    subq    $1024, %r14
    addl    $16,   %r15d

    cmpq    $1024, %r14
    jae     .Ldual_loop

/* ══════════════════════════════════════════════════════════════════ */
/* Tail: delegate remaining bytes to C intrinsics                    */
/* ══════════════════════════════════════════════════════════════════ */
.Ltail:
    testq   %r14, %r14
    jz      .Lexit

    /* Set up args for dap_chacha20_encrypt_avx2_512vl */
    movq    %r12, %rdi
    movq    %r13, %rsi
    movq    %r14, %rdx
    movq    STKOFF_KEYPTR(%rsp), %rcx
    movq    STKOFF_NONCEPTR(%rsp), %r8
    movl    %r15d, %r9d

    vzeroupper
    movq    %rbp, %rsp
    popq    %rbp
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    jmp     dap_chacha20_encrypt_avx2_512vl

.Lexit:
    vzeroupper
    movq    %rbp, %rsp
    popq    %rbp
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    ret

.size   dap_chacha20_encrypt_asm, .-dap_chacha20_encrypt_asm

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",@progbits
#endif
