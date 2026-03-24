/*
 * Scalar GPR+BMI2 Keccak-f[1600] — fused Theta+Rho+Pi+Chi
 *
 * Key optimization: each output row computed in a SINGLE pass.
 * For each row, load 5 scattered input lanes, apply D (Theta),
 * rotate (Rho), and compute Chi — all in registers. Only 25 loads
 * + 25 stores per round (vs 75+ in naive 3-pass approach).
 *
 * Register map:
 *   rdi            state pointer (preserved)
 *   rsi            round constant table / Chi temp (after iota)
 *   rax,rbx,rdx,r8,r9     Ba..Bu (5 row values for Chi)
 *   r10,r11,r12,r13,r14   D0..D4 (Theta column diffs)
 *   r15                    Chi save0 (Ba)
 *   rbp                    Chi save1 (Be)
 *   rcx                    loop counter
 */

.text

/* ========================================================================= */
/* THETA_PARITY: compute column parity C[x] and D[x]                        */
/* Clobbers rax,rbx,rdx,r8,r9. Results in r10..r14 = D0..D4.               */
/* ========================================================================= */
.macro THETA_PARITY
    movq   0*8(%rdi), %rax
    xorq   5*8(%rdi), %rax
    xorq  10*8(%rdi), %rax
    xorq  15*8(%rdi), %rax
    xorq  20*8(%rdi), %rax          /* C0 */

    movq   1*8(%rdi), %rbx
    xorq   6*8(%rdi), %rbx
    xorq  11*8(%rdi), %rbx
    xorq  16*8(%rdi), %rbx
    xorq  21*8(%rdi), %rbx          /* C1 */

    movq   2*8(%rdi), %rdx
    xorq   7*8(%rdi), %rdx
    xorq  12*8(%rdi), %rdx
    xorq  17*8(%rdi), %rdx
    xorq  22*8(%rdi), %rdx          /* C2 */

    movq   3*8(%rdi), %r8
    xorq   8*8(%rdi), %r8
    xorq  13*8(%rdi), %r8
    xorq  18*8(%rdi), %r8
    xorq  23*8(%rdi), %r8           /* C3 */

    movq   4*8(%rdi), %r9
    xorq   9*8(%rdi), %r9
    xorq  14*8(%rdi), %r9
    xorq  19*8(%rdi), %r9
    xorq  24*8(%rdi), %r9           /* C4 */

    rorxq  $63, %rbx, %r10
    xorq   %r9,  %r10               /* D0 = C4 ^ ROL(C1,1) */
    rorxq  $63, %rdx, %r11
    xorq   %rax, %r11               /* D1 = C0 ^ ROL(C2,1) */
    rorxq  $63, %r8,  %r12
    xorq   %rbx, %r12               /* D2 = C1 ^ ROL(C3,1) */
    rorxq  $63, %r9,  %r13
    xorq   %rdx, %r13               /* D3 = C2 ^ ROL(C4,1) */
    rorxq  $63, %rax, %r14
    xorq   %r8,  %r14               /* D4 = C3 ^ ROL(C0,1) */
.endm

/* ========================================================================= */
/* CHI_ROW: compute Chi for 5 values in rax,rbx,rdx,r8,r9                  */
/* Uses rsi as temp (must be free — used after iota in row 0).              */
/* Saves Ba→r15, Be→rbp. Results in rax,rbx,rdx,r8,r9 = E0..E4.           */
/* ========================================================================= */
.macro CHI_ROW
    movq   %rax, %r15
    movq   %rbx, %rbp

    andnq  %rdx, %rbx, %rbx         /* ~Be & Bi → rbx */
    xorq   %rbx, %rax               /* E0 = Ba ^ (~Be & Bi) */

    andnq  %r8,  %rdx, %rbx         /* ~Bi & Bo */
    xorq   %rbp, %rbx               /* E1 = Be ^ (~Bi & Bo) → rbx */

    andnq  %r9,  %r8,  %rsi         /* ~Bo & Bu */
    xorq   %rsi, %rdx               /* E2 = Bi ^ (~Bo & Bu) */

    andnq  %r15, %r9,  %rsi         /* ~Bu & Ba_orig */
    xorq   %rsi, %r8                /* E3 = Bo ^ (~Bu & Ba) */

    andnq  %rbp, %r15, %rsi         /* ~Ba_orig & Be_orig */
    xorq   %rsi, %r9                /* E4 = Bu ^ (~Ba & Be) */
.endm

/* ========================================================================= */
/* FUSED_ROUND: one round of Keccak-f[1600]                                 */
/* RC_OFF: byte offset into RC table (%rsi) for this round's iota constant  */
/* ========================================================================= */
.macro FUSED_ROUND RC_PTR

    THETA_PARITY

    /* ================================================================= */
    /* Output row 0: input lanes 0,6,12,18,24                            */
    /* D cols: D0,D1,D2,D3,D4    Rho: 0,44,43,21,14                     */
    /* ================================================================= */
    movq    0*8(%rdi), %rax
    xorq   %r10, %rax                   /* ^D0, rho=0: no rotate */

    movq    6*8(%rdi), %rbx
    xorq   %r11, %rbx
    rorxq  $20, %rbx, %rbx              /* ROL 44 */

    movq   12*8(%rdi), %rdx
    xorq   %r12, %rdx
    rorxq  $21, %rdx, %rdx              /* ROL 43 */

    movq   18*8(%rdi), %r8
    xorq   %r13, %r8
    rorxq  $43, %r8, %r8                /* ROL 21 */

    movq   24*8(%rdi), %r9
    xorq   %r14, %r9
    rorxq  $50, %r9, %r9                /* ROL 14 */

    /* Chi + Iota for row 0 */
    movq   %rax, %r15
    movq   %rbx, %rbp

    andnq  %rdx, %rbx, %rbx
    xorq   %rbx, %rax
    xorq   (\RC_PTR), %rax              /* Iota — rsi now free for CHI temp */

    andnq  %r8,  %rdx, %rbx
    xorq   %rbp, %rbx                   /* E1 */

    andnq  %r9,  %r8,  %rsi
    xorq   %rsi, %rdx                   /* E2 */

    andnq  %r15, %r9,  %rsi
    xorq   %rsi, %r8                    /* E3 */

    andnq  %rbp, %r15, %rsi
    xorq   %rsi, %r9                    /* E4 */

    movq   %rax,  0*8(%rdi)
    movq   %rbx,  1*8(%rdi)
    movq   %rdx,  2*8(%rdi)
    movq   %r8,   3*8(%rdi)
    movq   %r9,   4*8(%rdi)

    /* ================================================================= */
    /* Output row 1: input lanes 3,9,10,16,22                            */
    /* D cols: D3,D4,D0,D1,D2    Rho: 28,20,3,45,61                     */
    /* ================================================================= */
    movq    3*8(%rdi), %rax
    xorq   %r13, %rax
    rorxq  $36, %rax, %rax              /* ROL 28 */

    movq    9*8(%rdi), %rbx
    xorq   %r14, %rbx
    rorxq  $44, %rbx, %rbx              /* ROL 20 */

    movq   10*8(%rdi), %rdx
    xorq   %r10, %rdx
    rorxq  $61, %rdx, %rdx              /* ROL 3 */

    movq   16*8(%rdi), %r8
    xorq   %r11, %r8
    rorxq  $19, %r8, %r8                /* ROL 45 */

    movq   22*8(%rdi), %r9
    xorq   %r12, %r9
    rorxq  $3,  %r9, %r9                /* ROL 61 */

    CHI_ROW

    movq   %rax,  5*8(%rdi)
    movq   %rbx,  6*8(%rdi)
    movq   %rdx,  7*8(%rdi)
    movq   %r8,   8*8(%rdi)
    movq   %r9,   9*8(%rdi)

    /* ================================================================= */
    /* Output row 2: input lanes 1,7,13,19,20                            */
    /* D cols: D1,D2,D3,D4,D0    Rho: 1,6,25,8,18                       */
    /* ================================================================= */
    movq    1*8(%rdi), %rax
    xorq   %r11, %rax
    rorxq  $63, %rax, %rax              /* ROL 1 */

    movq    7*8(%rdi), %rbx
    xorq   %r12, %rbx
    rorxq  $58, %rbx, %rbx              /* ROL 6 */

    movq   13*8(%rdi), %rdx
    xorq   %r13, %rdx
    rorxq  $39, %rdx, %rdx              /* ROL 25 */

    movq   19*8(%rdi), %r8
    xorq   %r14, %r8
    rorxq  $56, %r8, %r8                /* ROL 8 */

    movq   20*8(%rdi), %r9
    xorq   %r10, %r9
    rorxq  $46, %r9, %r9                /* ROL 18 */

    CHI_ROW

    movq   %rax, 10*8(%rdi)
    movq   %rbx, 11*8(%rdi)
    movq   %rdx, 12*8(%rdi)
    movq   %r8,  13*8(%rdi)
    movq   %r9,  14*8(%rdi)

    /* ================================================================= */
    /* Output row 3: input lanes 4,5,11,17,23                            */
    /* D cols: D4,D0,D1,D2,D3    Rho: 27,36,10,15,56                    */
    /* ================================================================= */
    movq    4*8(%rdi), %rax
    xorq   %r14, %rax
    rorxq  $37, %rax, %rax              /* ROL 27 */

    movq    5*8(%rdi), %rbx
    xorq   %r10, %rbx
    rorxq  $28, %rbx, %rbx              /* ROL 36 */

    movq   11*8(%rdi), %rdx
    xorq   %r11, %rdx
    rorxq  $54, %rdx, %rdx              /* ROL 10 */

    movq   17*8(%rdi), %r8
    xorq   %r12, %r8
    rorxq  $49, %r8, %r8                /* ROL 15 */

    movq   23*8(%rdi), %r9
    xorq   %r13, %r9
    rorxq  $8,  %r9, %r9                /* ROL 56 */

    CHI_ROW

    movq   %rax, 15*8(%rdi)
    movq   %rbx, 16*8(%rdi)
    movq   %rdx, 17*8(%rdi)
    movq   %r8,  18*8(%rdi)
    movq   %r9,  19*8(%rdi)

    /* ================================================================= */
    /* Output row 4: input lanes 2,8,14,15,21                            */
    /* D cols: D2,D3,D4,D0,D1    Rho: 62,55,39,41,2                     */
    /* ================================================================= */
    movq    2*8(%rdi), %rax
    xorq   %r12, %rax
    rorxq  $2,  %rax, %rax              /* ROL 62 */

    movq    8*8(%rdi), %rbx
    xorq   %r13, %rbx
    rorxq  $9,  %rbx, %rbx              /* ROL 55 */

    movq   14*8(%rdi), %rdx
    xorq   %r14, %rdx
    rorxq  $25, %rdx, %rdx              /* ROL 39 */

    movq   15*8(%rdi), %r8
    xorq   %r10, %r8
    rorxq  $23, %r8, %r8                /* ROL 41 */

    movq   21*8(%rdi), %r9
    xorq   %r11, %r9
    rorxq  $62, %r9, %r9                /* ROL 2 */

    CHI_ROW

    movq   %rax, 20*8(%rdi)
    movq   %rbx, 21*8(%rdi)
    movq   %rdx, 22*8(%rdi)
    movq   %r8,  23*8(%rdi)
    movq   %r9,  24*8(%rdi)

.endm  /* FUSED_ROUND */


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

    leaq   .Lscalar_rc(%rip), %rsi
    movl   $12, %ecx

.balign 32
.Lscalar_loop:
    pushq  %rcx                      /* save loop counter (round clobbers it) */
    FUSED_ROUND %rsi
    addq   $8, %rsi
    FUSED_ROUND %rsi
    addq   $8, %rsi
    popq   %rcx
    decl   %ecx
    jnz    .Lscalar_loop

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
