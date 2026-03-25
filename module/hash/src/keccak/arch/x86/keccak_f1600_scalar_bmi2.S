/*
 * Scalar GPR+BMI2 Keccak-f[1600] — two-buffer fused Theta+Rho+Pi+Chi
 *
 * Fix for RAW hazards through Pi permutation: rows store to a
 * SEPARATE output buffer so reads from the input are never clobbered.
 * Even rounds: A (rdi) → B (stack).  Odd rounds: B → A.
 * After 24 rounds the result is back in A.
 *
 * Register map per round:
 *   \in             input state base (rdi or rsp)
 *   \out            output state base (rsp or rdi)
 *   \rc             round-constant pointer (rcx)
 *   rax,rbx,rdx,r8,r9     Ba..Bu (5 row values for Chi)
 *   r10,r11,r12,r13,r14   D0..D4 (Theta column diffs)
 *   r15                    Chi save0 (Ba)
 *   rbp                    Chi save1 (Be)
 *   rsi                    Chi temp  (free after Iota)
 *
 * Stack layout (after prologue):
 *   [rsp+0 .. rsp+199]    state B  (25 × 8 bytes)
 *   [rsp+200 .. rsp+207]  loop counter save slot
 */

.text

/* ========================================================================= */
/* THETA_PARITY: column parity → D0..D4 in r10..r14                          */
/* \base = input state base register                                         */
/* Clobbers rax, rbx, rdx, r8, r9.                                          */
/* ========================================================================= */
.macro THETA_PARITY base
    movq    0*8(\base), %rax
    xorq    5*8(\base), %rax
    xorq   10*8(\base), %rax
    xorq   15*8(\base), %rax
    xorq   20*8(\base), %rax

    movq    1*8(\base), %rbx
    xorq    6*8(\base), %rbx
    xorq   11*8(\base), %rbx
    xorq   16*8(\base), %rbx
    xorq   21*8(\base), %rbx

    movq    2*8(\base), %rdx
    xorq    7*8(\base), %rdx
    xorq   12*8(\base), %rdx
    xorq   17*8(\base), %rdx
    xorq   22*8(\base), %rdx

    movq    3*8(\base), %r8
    xorq    8*8(\base), %r8
    xorq   13*8(\base), %r8
    xorq   18*8(\base), %r8
    xorq   23*8(\base), %r8

    movq    4*8(\base), %r9
    xorq    9*8(\base), %r9
    xorq   14*8(\base), %r9
    xorq   19*8(\base), %r9
    xorq   24*8(\base), %r9

    rorxq  $63, %rbx, %r10
    xorq   %r9,  %r10
    rorxq  $63, %rdx, %r11
    xorq   %rax, %r11
    rorxq  $63, %r8,  %r12
    xorq   %rbx, %r12
    rorxq  $63, %r9,  %r13
    xorq   %rdx, %r13
    rorxq  $63, %rax, %r14
    xorq   %r8,  %r14
.endm

/* ========================================================================= */
/* CHI_ROW: Chi for rax,rbx,rdx,r8,r9.  Uses r15,rbp,rsi as temps.         */
/* ========================================================================= */
.macro CHI_ROW
    movq   %rax, %r15
    movq   %rbx, %rbp

    andnq  %rdx, %rbx, %rbx
    xorq   %rbx, %rax

    andnq  %r8,  %rdx, %rbx
    xorq   %rbp, %rbx

    andnq  %r9,  %r8,  %rsi
    xorq   %rsi, %rdx

    andnq  %r15, %r9,  %rsi
    xorq   %rsi, %r8

    andnq  %rbp, %r15, %rsi
    xorq   %rsi, %r9
.endm

/* ========================================================================= */
/* KECCAK_ROUND \in, \out, \rc                                               */
/* One full round: reads from \in, writes to \out.  \rc → Iota constant.    */
/* ========================================================================= */
.macro KECCAK_ROUND in, out, rc

    THETA_PARITY \in

    /* Row 0: lanes 0,6,12,18,24  D: D0,D1,D2,D3,D4  ρ: 0,44,43,21,14 */
    movq    0*8(\in), %rax
    xorq   %r10, %rax
    movq    6*8(\in), %rbx
    xorq   %r11, %rbx
    rorxq  $20, %rbx, %rbx
    movq   12*8(\in), %rdx
    xorq   %r12, %rdx
    rorxq  $21, %rdx, %rdx
    movq   18*8(\in), %r8
    xorq   %r13, %r8
    rorxq  $43, %r8, %r8
    movq   24*8(\in), %r9
    xorq   %r14, %r9
    rorxq  $50, %r9, %r9

    movq   %rax, %r15
    movq   %rbx, %rbp
    andnq  %rdx, %rbx, %rbx
    xorq   %rbx, %rax
    xorq   (\rc), %rax
    andnq  %r8,  %rdx, %rbx
    xorq   %rbp, %rbx
    andnq  %r9,  %r8,  %rsi
    xorq   %rsi, %rdx
    andnq  %r15, %r9,  %rsi
    xorq   %rsi, %r8
    andnq  %rbp, %r15, %rsi
    xorq   %rsi, %r9

    movq   %rax,  0*8(\out)
    movq   %rbx,  1*8(\out)
    movq   %rdx,  2*8(\out)
    movq   %r8,   3*8(\out)
    movq   %r9,   4*8(\out)

    /* Row 1: lanes 3,9,10,16,22  D: D3,D4,D0,D1,D2  ρ: 28,20,3,45,61 */
    movq    3*8(\in), %rax
    xorq   %r13, %rax
    rorxq  $36, %rax, %rax
    movq    9*8(\in), %rbx
    xorq   %r14, %rbx
    rorxq  $44, %rbx, %rbx
    movq   10*8(\in), %rdx
    xorq   %r10, %rdx
    rorxq  $61, %rdx, %rdx
    movq   16*8(\in), %r8
    xorq   %r11, %r8
    rorxq  $19, %r8, %r8
    movq   22*8(\in), %r9
    xorq   %r12, %r9
    rorxq  $3,  %r9, %r9

    CHI_ROW

    movq   %rax,  5*8(\out)
    movq   %rbx,  6*8(\out)
    movq   %rdx,  7*8(\out)
    movq   %r8,   8*8(\out)
    movq   %r9,   9*8(\out)

    /* Row 2: lanes 1,7,13,19,20  D: D1,D2,D3,D4,D0  ρ: 1,6,25,8,18 */
    movq    1*8(\in), %rax
    xorq   %r11, %rax
    rorxq  $63, %rax, %rax
    movq    7*8(\in), %rbx
    xorq   %r12, %rbx
    rorxq  $58, %rbx, %rbx
    movq   13*8(\in), %rdx
    xorq   %r13, %rdx
    rorxq  $39, %rdx, %rdx
    movq   19*8(\in), %r8
    xorq   %r14, %r8
    rorxq  $56, %r8, %r8
    movq   20*8(\in), %r9
    xorq   %r10, %r9
    rorxq  $46, %r9, %r9

    CHI_ROW

    movq   %rax, 10*8(\out)
    movq   %rbx, 11*8(\out)
    movq   %rdx, 12*8(\out)
    movq   %r8,  13*8(\out)
    movq   %r9,  14*8(\out)

    /* Row 3: lanes 4,5,11,17,23  D: D4,D0,D1,D2,D3  ρ: 27,36,10,15,56 */
    movq    4*8(\in), %rax
    xorq   %r14, %rax
    rorxq  $37, %rax, %rax
    movq    5*8(\in), %rbx
    xorq   %r10, %rbx
    rorxq  $28, %rbx, %rbx
    movq   11*8(\in), %rdx
    xorq   %r11, %rdx
    rorxq  $54, %rdx, %rdx
    movq   17*8(\in), %r8
    xorq   %r12, %r8
    rorxq  $49, %r8, %r8
    movq   23*8(\in), %r9
    xorq   %r13, %r9
    rorxq  $8,  %r9, %r9

    CHI_ROW

    movq   %rax, 15*8(\out)
    movq   %rbx, 16*8(\out)
    movq   %rdx, 17*8(\out)
    movq   %r8,  18*8(\out)
    movq   %r9,  19*8(\out)

    /* Row 4: lanes 2,8,14,15,21  D: D2,D3,D4,D0,D1  ρ: 62,55,39,41,2 */
    movq    2*8(\in), %rax
    xorq   %r12, %rax
    rorxq  $2,  %rax, %rax
    movq    8*8(\in), %rbx
    xorq   %r13, %rbx
    rorxq  $9,  %rbx, %rbx
    movq   14*8(\in), %rdx
    xorq   %r14, %rdx
    rorxq  $25, %rdx, %rdx
    movq   15*8(\in), %r8
    xorq   %r10, %r8
    rorxq  $23, %r8, %r8
    movq   21*8(\in), %r9
    xorq   %r11, %r9
    rorxq  $62, %r9, %r9

    CHI_ROW

    movq   %rax, 20*8(\out)
    movq   %rbx, 21*8(\out)
    movq   %rdx, 22*8(\out)
    movq   %r8,  23*8(\out)
    movq   %r9,  24*8(\out)

.endm  /* KECCAK_ROUND */


/* ========================================================================= */
/* dap_hash_keccak_permute_scalar_bmi2(state)                                */
/* ========================================================================= */
.globl  dap_hash_keccak_permute_scalar_bmi2
.type   dap_hash_keccak_permute_scalar_bmi2, @function
.balign 64
dap_hash_keccak_permute_scalar_bmi2:
    pushq  %rbx
    pushq  %rbp
    pushq  %r12
    pushq  %r13
    pushq  %r14
    pushq  %r15
    subq   $208, %rsp

    leaq   .Lscalar_rc(%rip), %rsi
    movl   $12, %ecx

.balign 32
.Lscalar_loop:
    movl   %ecx, 200(%rsp)
    movq   %rsi, %rcx

    KECCAK_ROUND %rdi, %rsp, %rcx
    addq   $8, %rcx

    KECCAK_ROUND %rsp, %rdi, %rcx
    leaq   8(%rcx), %rsi

    movl   200(%rsp), %ecx
    decl   %ecx
    jnz    .Lscalar_loop

    addq   $208, %rsp
    popq   %r15
    popq   %r14
    popq   %r13
    popq   %r12
    popq   %rbp
    popq   %rbx
    ret
.size   dap_hash_keccak_permute_scalar_bmi2, .-dap_hash_keccak_permute_scalar_bmi2


/* ========================================================================= */
/* Round constants                                                            */
/* ========================================================================= */
.section .rodata
.balign 64
.Lscalar_rc:
    .quad 0x0000000000000001
    .quad 0x0000000000008082
    .quad 0x800000000000808a
    .quad 0x8000000080008000
    .quad 0x000000000000808b
    .quad 0x0000000080000001
    .quad 0x8000000080008081
    .quad 0x8000000000008009
    .quad 0x000000000000008a
    .quad 0x0000000000000088
    .quad 0x0000000080008009
    .quad 0x000000008000000a
    .quad 0x000000008000808b
    .quad 0x800000000000008b
    .quad 0x8000000000008089
    .quad 0x8000000000008003
    .quad 0x8000000000008002
    .quad 0x8000000000000080
    .quad 0x000000000000800a
    .quad 0x800000008000000a
    .quad 0x8000000080008081
    .quad 0x8000000000008080
    .quad 0x0000000080000001
    .quad 0x8000000080008008

.section .note.GNU-stack,"",@progbits
