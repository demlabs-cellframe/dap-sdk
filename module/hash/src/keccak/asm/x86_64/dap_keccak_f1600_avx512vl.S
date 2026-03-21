/*
 * dap_keccak_f1600_avx512vl.S
 *
 * Hand-tuned Keccak-f[1600] permutation using AVX-512VL.
 * Lane-per-register layout: ymm0-ymm24 hold 25 state lanes.
 * One shared permutation core serves both single (1x) and 4-way (4x) modes.
 *
 * Key design choices:
 *   - vpternlogq $0x96 for 3-way XOR (Theta column parity + D application)
 *   - vpternlogq $0xD2 for Chi step (a ^ (~b & c) in one instruction)
 *   - vprolq for Rho lane rotation (replaces shift+shift+or)
 *   - 32 YMM registers via EVEX encoding — zero register spills
 *   - Pi via register moves — eliminated by hardware register renaming
 *   - Chi uses π-adjusted row groupings to defer Pi to end of round
 *   - Theta/Rho/Chi interleaved for instruction-level parallelism
 *
 * Register plan:
 *   ymm0-ymm24   state lanes  (state[i] = A[i%5, i/5])
 *   ymm25-ymm29  column parities / D temporaries
 *   ymm30-ymm31  Chi row temporaries
 *   r13d          round counter
 *   r14           round constant pointer
 *
 * Authors: DAP SDK Team
 * Copyright: DeM Labs Inc. 2026
 * License: GNU GPL v3
 */

.text

/* ============================================================================
 * 1x permutation wrapper
 *
 * void dap_hash_keccak_permute_avx512vl_asm(dap_hash_keccak_state_t *state)
 * ============================================================================ */
.globl  dap_hash_keccak_permute_avx512vl_asm
.type   dap_hash_keccak_permute_avx512vl_asm, @function
.balign 32
dap_hash_keccak_permute_avx512vl_asm:
    pushq   %r13
    pushq   %r14

    vmovq   (8*0)(%rdi),  %xmm0
    vmovq   (8*1)(%rdi),  %xmm1
    vmovq   (8*2)(%rdi),  %xmm2
    vmovq   (8*3)(%rdi),  %xmm3
    vmovq   (8*4)(%rdi),  %xmm4
    vmovq   (8*5)(%rdi),  %xmm5
    vmovq   (8*6)(%rdi),  %xmm6
    vmovq   (8*7)(%rdi),  %xmm7
    vmovq   (8*8)(%rdi),  %xmm8
    vmovq   (8*9)(%rdi),  %xmm9
    vmovq   (8*10)(%rdi), %xmm10
    vmovq   (8*11)(%rdi), %xmm11
    vmovq   (8*12)(%rdi), %xmm12
    vmovq   (8*13)(%rdi), %xmm13
    vmovq   (8*14)(%rdi), %xmm14
    vmovq   (8*15)(%rdi), %xmm15
    vmovq   (8*16)(%rdi), %xmm16
    vmovq   (8*17)(%rdi), %xmm17
    vmovq   (8*18)(%rdi), %xmm18
    vmovq   (8*19)(%rdi), %xmm19
    vmovq   (8*20)(%rdi), %xmm20
    vmovq   (8*21)(%rdi), %xmm21
    vmovq   (8*22)(%rdi), %xmm22
    vmovq   (8*23)(%rdi), %xmm23
    vmovq   (8*24)(%rdi), %xmm24

    pushq   %rdi
    call    .Lpermute_f1600
    popq    %rdi

    vmovq   %xmm0,  (8*0)(%rdi)
    vmovq   %xmm1,  (8*1)(%rdi)
    vmovq   %xmm2,  (8*2)(%rdi)
    vmovq   %xmm3,  (8*3)(%rdi)
    vmovq   %xmm4,  (8*4)(%rdi)
    vmovq   %xmm5,  (8*5)(%rdi)
    vmovq   %xmm6,  (8*6)(%rdi)
    vmovq   %xmm7,  (8*7)(%rdi)
    vmovq   %xmm8,  (8*8)(%rdi)
    vmovq   %xmm9,  (8*9)(%rdi)
    vmovq   %xmm10, (8*10)(%rdi)
    vmovq   %xmm11, (8*11)(%rdi)
    vmovq   %xmm12, (8*12)(%rdi)
    vmovq   %xmm13, (8*13)(%rdi)
    vmovq   %xmm14, (8*14)(%rdi)
    vmovq   %xmm15, (8*15)(%rdi)
    vmovq   %xmm16, (8*16)(%rdi)
    vmovq   %xmm17, (8*17)(%rdi)
    vmovq   %xmm18, (8*18)(%rdi)
    vmovq   %xmm19, (8*19)(%rdi)
    vmovq   %xmm20, (8*20)(%rdi)
    vmovq   %xmm21, (8*21)(%rdi)
    vmovq   %xmm22, (8*22)(%rdi)
    vmovq   %xmm23, (8*23)(%rdi)
    vmovq   %xmm24, (8*24)(%rdi)

    popq    %r14
    popq    %r13
    ret
.size   dap_hash_keccak_permute_avx512vl_asm, .-dap_hash_keccak_permute_avx512vl_asm


/* ============================================================================
 * 4x permutation wrapper
 *
 * void dap_keccak_x4_permute_avx512vl_asm(dap_keccak_x4_state_t *state)
 *
 * Interleaved layout: lanes[lane*4 + instance], each YMM = 4 instances of
 * the same lane.  The permutation is element-wise so it processes all four
 * instances in parallel.
 * ============================================================================ */
.globl  dap_keccak_x4_permute_avx512vl_asm
.type   dap_keccak_x4_permute_avx512vl_asm, @function
.balign 32
dap_keccak_x4_permute_avx512vl_asm:
    pushq   %r13
    pushq   %r14

    vmovdqu64   (32*0)(%rdi),  %ymm0
    vmovdqu64   (32*1)(%rdi),  %ymm1
    vmovdqu64   (32*2)(%rdi),  %ymm2
    vmovdqu64   (32*3)(%rdi),  %ymm3
    vmovdqu64   (32*4)(%rdi),  %ymm4
    vmovdqu64   (32*5)(%rdi),  %ymm5
    vmovdqu64   (32*6)(%rdi),  %ymm6
    vmovdqu64   (32*7)(%rdi),  %ymm7
    vmovdqu64   (32*8)(%rdi),  %ymm8
    vmovdqu64   (32*9)(%rdi),  %ymm9
    vmovdqu64   (32*10)(%rdi), %ymm10
    vmovdqu64   (32*11)(%rdi), %ymm11
    vmovdqu64   (32*12)(%rdi), %ymm12
    vmovdqu64   (32*13)(%rdi), %ymm13
    vmovdqu64   (32*14)(%rdi), %ymm14
    vmovdqu64   (32*15)(%rdi), %ymm15
    vmovdqu64   (32*16)(%rdi), %ymm16
    vmovdqu64   (32*17)(%rdi), %ymm17
    vmovdqu64   (32*18)(%rdi), %ymm18
    vmovdqu64   (32*19)(%rdi), %ymm19
    vmovdqu64   (32*20)(%rdi), %ymm20
    vmovdqu64   (32*21)(%rdi), %ymm21
    vmovdqu64   (32*22)(%rdi), %ymm22
    vmovdqu64   (32*23)(%rdi), %ymm23
    vmovdqu64   (32*24)(%rdi), %ymm24

    pushq   %rdi
    call    .Lpermute_f1600
    popq    %rdi

    vmovdqu64   %ymm0,  (32*0)(%rdi)
    vmovdqu64   %ymm1,  (32*1)(%rdi)
    vmovdqu64   %ymm2,  (32*2)(%rdi)
    vmovdqu64   %ymm3,  (32*3)(%rdi)
    vmovdqu64   %ymm4,  (32*4)(%rdi)
    vmovdqu64   %ymm5,  (32*5)(%rdi)
    vmovdqu64   %ymm6,  (32*6)(%rdi)
    vmovdqu64   %ymm7,  (32*7)(%rdi)
    vmovdqu64   %ymm8,  (32*8)(%rdi)
    vmovdqu64   %ymm9,  (32*9)(%rdi)
    vmovdqu64   %ymm10, (32*10)(%rdi)
    vmovdqu64   %ymm11, (32*11)(%rdi)
    vmovdqu64   %ymm12, (32*12)(%rdi)
    vmovdqu64   %ymm13, (32*13)(%rdi)
    vmovdqu64   %ymm14, (32*14)(%rdi)
    vmovdqu64   %ymm15, (32*15)(%rdi)
    vmovdqu64   %ymm16, (32*16)(%rdi)
    vmovdqu64   %ymm17, (32*17)(%rdi)
    vmovdqu64   %ymm18, (32*18)(%rdi)
    vmovdqu64   %ymm19, (32*19)(%rdi)
    vmovdqu64   %ymm20, (32*20)(%rdi)
    vmovdqu64   %ymm21, (32*21)(%rdi)
    vmovdqu64   %ymm22, (32*22)(%rdi)
    vmovdqu64   %ymm23, (32*23)(%rdi)
    vmovdqu64   %ymm24, (32*24)(%rdi)

    popq    %r14
    popq    %r13
    ret
.size   dap_keccak_x4_permute_avx512vl_asm, .-dap_keccak_x4_permute_avx512vl_asm


/* ============================================================================
 * Core permutation — 24 rounds of Keccak-f[1600]
 *
 * All operations are element-wise on 64-bit qwords inside each YMM register,
 * so the same code handles both 1x (one qword per YMM) and 4x (four qwords
 * per YMM) transparently.
 *
 * Round structure (reordered from spec for better ILP):
 *   θ (Theta)  →  ρ (Rho)  →  χ_π (Chi with deferred Pi)  →  ι (Iota)  →  π (Pi)
 *
 * The Chi step operates on π-adjusted row groupings so the Pi register
 * rename can be postponed to the very end of each round:
 *   Row 0: {0, 6, 12, 18, 24}
 *   Row 1: {3, 9, 10, 16, 22}
 *   Row 2: {1, 7, 13, 19, 20}
 *   Row 3: {4, 5, 11, 17, 23}
 *   Row 4: {2, 8, 14, 15, 21}
 *
 * Clobbers: ymm25-ymm31, r13, r14
 * ============================================================================ */
.balign 64
.Lpermute_f1600:
    movl    $24, %r13d
    leaq    .Lrc(%rip), %r14

.balign 32
.Lround:
    /* ---- THETA: column parities (partial — 3 of 5 elements each) ---- */
    vmovdqa64   %ymm0, %ymm25
    vpternlogq  $0x96, %ymm5, %ymm10, %ymm25
    vmovdqa64   %ymm1, %ymm26
    vpternlogq  $0x96, %ymm11, %ymm6, %ymm26
    vmovdqa64   %ymm2, %ymm27
    vpternlogq  $0x96, %ymm12, %ymm7, %ymm27

    vmovdqa64   %ymm3, %ymm28
    vpternlogq  $0x96, %ymm13, %ymm8, %ymm28
    vmovdqa64   %ymm4, %ymm29
    vpternlogq  $0x96, %ymm14, %ymm9, %ymm29
    vpternlogq  $0x96, %ymm20, %ymm15, %ymm25      /* C[0] complete */

    vpternlogq  $0x96, %ymm21, %ymm16, %ymm26      /* C[1] complete */
    vpternlogq  $0x96, %ymm22, %ymm17, %ymm27      /* C[2] complete */
    vpternlogq  $0x96, %ymm23, %ymm18, %ymm28      /* C[3] complete */

    /* ---- D values + Theta application (interleaved with Rho) ----
     * D[x] = C[(x+4)%5] ^ ROL64(C[(x+1)%5], 1)                       */
    vprolq      $1, %ymm26, %ymm30                 /* ROL(C[1],1)       */
    vprolq      $1, %ymm27, %ymm31                 /* ROL(C[2],1)       */
    vpternlogq  $0x96, %ymm24, %ymm19, %ymm29      /* C[4] complete    */

    /* D[0] = C[4] ^ ROL(C[1],1) — apply to column 0 */
    vpternlogq  $0x96, %ymm30, %ymm29, %ymm0
    vpternlogq  $0x96, %ymm30, %ymm29, %ymm10
    vpternlogq  $0x96, %ymm30, %ymm29, %ymm20

    vpternlogq  $0x96, %ymm30, %ymm29, %ymm5
    vpternlogq  $0x96, %ymm30, %ymm29, %ymm15
    vprolq      $1, %ymm28, %ymm30                 /* ROL(C[3],1)       */

    /* D[1] = C[0] ^ ROL(C[2],1) — apply to column 1 */
    vpternlogq  $0x96, %ymm31, %ymm25, %ymm6
    vpternlogq  $0x96, %ymm31, %ymm25, %ymm16
    vpternlogq  $0x96, %ymm31, %ymm25, %ymm1

    vpternlogq  $0x96, %ymm31, %ymm25, %ymm11
    vpternlogq  $0x96, %ymm31, %ymm25, %ymm21
    vprolq      $1, %ymm29, %ymm31                 /* ROL(C[4],1)       */

    vpbroadcastq    (%r14), %ymm29                  /* pre-load Iota RC  */
    addq    $8, %r14

    /* D[2] = C[1] ^ ROL(C[3],1) — apply to column 2 */
    vpternlogq  $0x96, %ymm30, %ymm26, %ymm12
    vpternlogq  $0x96, %ymm30, %ymm26, %ymm7
    vpternlogq  $0x96, %ymm30, %ymm26, %ymm22

    vpternlogq  $0x96, %ymm30, %ymm26, %ymm17
    vpternlogq  $0x96, %ymm30, %ymm26, %ymm2
    vprolq      $1, %ymm25, %ymm30                 /* ROL(C[0],1)       */

    /* D[3] = C[2] ^ ROL(C[4],1) — apply to column 3, begin Rho */
    vpternlogq  $0x96, %ymm31, %ymm27, %ymm3
    vpternlogq  $0x96, %ymm31, %ymm27, %ymm13
    vpternlogq  $0x96, %ymm31, %ymm27, %ymm23

    vprolq      $44, %ymm6, %ymm6                  /* ρ[6]              */
    vpternlogq  $0x96, %ymm31, %ymm27, %ymm18
    vpternlogq  $0x96, %ymm31, %ymm27, %ymm8

    vprolq      $43, %ymm12, %ymm12                /* ρ[12]             */
    vprolq      $21, %ymm18, %ymm18                /* ρ[18]             */

    /* D[4] = C[3] ^ ROL(C[0],1) — apply to column 4, continue Rho */
    vpternlogq  $0x96, %ymm30, %ymm28, %ymm24
    vprolq      $14, %ymm24, %ymm24                /* ρ[24]             */
    vprolq      $28, %ymm3, %ymm3                  /* ρ[3]              */
    vpternlogq  $0x96, %ymm30, %ymm28, %ymm9

    vprolq      $20, %ymm9, %ymm9                  /* ρ[9]              */
    vprolq      $3, %ymm10, %ymm10                 /* ρ[10]             */
    vpternlogq  $0x96, %ymm30, %ymm28, %ymm19

    vprolq      $45, %ymm16, %ymm16                /* ρ[16]             */
    vprolq      $61, %ymm22, %ymm22                /* ρ[22]             */
    vpternlogq  $0x96, %ymm30, %ymm28, %ymm4

    vprolq      $1, %ymm1, %ymm1                   /* ρ[1]              */
    vprolq      $6, %ymm7, %ymm7                   /* ρ[7]              */
    vpternlogq  $0x96, %ymm30, %ymm28, %ymm14

    /* ---- CHI (with π-adjusted rows) interleaved with remaining Rho ---- */

    vprolq      $25, %ymm13, %ymm13                /* ρ[13]             */
    vprolq      $8, %ymm19, %ymm19                 /* ρ[19]             */

    /* χ Row 0: {0, 6, 12, 18, 24} */
    vmovdqa64   %ymm0, %ymm30
    vpternlogq  $0xD2, %ymm12, %ymm6, %ymm30

    vprolq      $18, %ymm20, %ymm20                /* ρ[20]             */
    vprolq      $27, %ymm4, %ymm4                  /* ρ[4]              */
    vpxorq      %ymm29, %ymm30, %ymm30             /* ι: state[0]^=RC   */

    vprolq      $36, %ymm5, %ymm5                  /* ρ[5]              */
    vprolq      $10, %ymm11, %ymm11                /* ρ[11]             */
    vmovdqa64   %ymm6, %ymm31
    vpternlogq  $0xD2, %ymm18, %ymm12, %ymm31

    vprolq      $15, %ymm17, %ymm17                /* ρ[17]             */
    vprolq      $56, %ymm23, %ymm23                /* ρ[23]             */
    vpternlogq  $0xD2, %ymm24, %ymm18, %ymm12

    vprolq      $62, %ymm2, %ymm2                  /* ρ[2]              */
    vprolq      $55, %ymm8, %ymm8                  /* ρ[8]              */
    vpternlogq  $0xD2, %ymm0, %ymm24, %ymm18

    vprolq      $39, %ymm14, %ymm14                /* ρ[14]             */
    vprolq      $41, %ymm15, %ymm15                /* ρ[15]             */
    vpternlogq  $0xD2, %ymm6, %ymm0, %ymm24
    vmovdqa64   %ymm30, %ymm0
    vmovdqa64   %ymm31, %ymm6

    vprolq      $2, %ymm21, %ymm21                 /* ρ[21]             */

    /* χ Row 1: {3, 9, 10, 16, 22} */
    vmovdqa64   %ymm3, %ymm30
    vpternlogq  $0xD2, %ymm10, %ymm9, %ymm30
    vmovdqa64   %ymm9, %ymm31
    vpternlogq  $0xD2, %ymm16, %ymm10, %ymm31

    vpternlogq  $0xD2, %ymm22, %ymm16, %ymm10
    vpternlogq  $0xD2, %ymm3, %ymm22, %ymm16
    vpternlogq  $0xD2, %ymm9, %ymm3, %ymm22
    vmovdqa64   %ymm30, %ymm3
    vmovdqa64   %ymm31, %ymm9

    /* χ Row 2: {1, 7, 13, 19, 20} */
    vmovdqa64   %ymm1, %ymm30
    vpternlogq  $0xD2, %ymm13, %ymm7, %ymm30
    vmovdqa64   %ymm7, %ymm31
    vpternlogq  $0xD2, %ymm19, %ymm13, %ymm31
    vpternlogq  $0xD2, %ymm20, %ymm19, %ymm13

    vpternlogq  $0xD2, %ymm1, %ymm20, %ymm19
    vpternlogq  $0xD2, %ymm7, %ymm1, %ymm20
    vmovdqa64   %ymm30, %ymm1
    vmovdqa64   %ymm31, %ymm7

    /* χ Row 3: {4, 5, 11, 17, 23} */
    vmovdqa64   %ymm4, %ymm30
    vpternlogq  $0xD2, %ymm11, %ymm5, %ymm30

    vmovdqa64   %ymm5, %ymm31
    vpternlogq  $0xD2, %ymm17, %ymm11, %ymm31
    vpternlogq  $0xD2, %ymm23, %ymm17, %ymm11
    vpternlogq  $0xD2, %ymm4, %ymm23, %ymm17

    vpternlogq  $0xD2, %ymm5, %ymm4, %ymm23
    vmovdqa64   %ymm30, %ymm4
    vmovdqa64   %ymm31, %ymm5

    /* χ Row 4: {2, 8, 14, 15, 21} */
    vmovdqa64   %ymm2, %ymm30
    vpternlogq  $0xD2, %ymm14, %ymm8, %ymm30
    vmovdqa64   %ymm8, %ymm31
    vpternlogq  $0xD2, %ymm15, %ymm14, %ymm31

    vpternlogq  $0xD2, %ymm21, %ymm15, %ymm14
    vpternlogq  $0xD2, %ymm2, %ymm21, %ymm15
    vpternlogq  $0xD2, %ymm8, %ymm2, %ymm21
    vmovdqa64   %ymm30, %ymm2
    vmovdqa64   %ymm31, %ymm8

    /* ---- PI: 24-element register rename cycle ----
     * 3→5→16→8→21→24→4→15→23→19→13→12→2→20→14→22→9→6→1→10→7→11→17→18→3
     * Position 0 is a fixed point.                                      */
    vmovdqa64   %ymm3,  %ymm30
    vmovdqa64   %ymm18, %ymm3
    vmovdqa64   %ymm17, %ymm18
    vmovdqa64   %ymm11, %ymm17
    vmovdqa64   %ymm7,  %ymm11
    vmovdqa64   %ymm10, %ymm7
    vmovdqa64   %ymm1,  %ymm10
    vmovdqa64   %ymm6,  %ymm1
    vmovdqa64   %ymm9,  %ymm6
    vmovdqa64   %ymm22, %ymm9
    vmovdqa64   %ymm14, %ymm22
    vmovdqa64   %ymm20, %ymm14
    vmovdqa64   %ymm2,  %ymm20
    vmovdqa64   %ymm12, %ymm2
    vmovdqa64   %ymm13, %ymm12
    vmovdqa64   %ymm19, %ymm13
    vmovdqa64   %ymm23, %ymm19
    vmovdqa64   %ymm15, %ymm23
    vmovdqa64   %ymm4,  %ymm15
    vmovdqa64   %ymm24, %ymm4
    vmovdqa64   %ymm21, %ymm24
    vmovdqa64   %ymm8,  %ymm21
    vmovdqa64   %ymm16, %ymm8
    vmovdqa64   %ymm5,  %ymm16
    vmovdqa64   %ymm30, %ymm5

    decl    %r13d
    jnz     .Lround
    ret
.size   .Lpermute_f1600, .-.Lpermute_f1600


/* ============================================================================
 * Round constants
 * ============================================================================ */
.section .rodata
.balign 64
.Lrc:
    .quad   0x0000000000000001, 0x0000000000008082
    .quad   0x800000000000808a, 0x8000000080008000
    .quad   0x000000000000808b, 0x0000000080000001
    .quad   0x8000000080008081, 0x8000000000008009
    .quad   0x000000000000008a, 0x0000000000000088
    .quad   0x0000000080008009, 0x000000008000000a
    .quad   0x000000008000808b, 0x800000000000008b
    .quad   0x8000000000008089, 0x8000000000008003
    .quad   0x8000000000008002, 0x8000000000000080
    .quad   0x000000000000800a, 0x800000008000000a
    .quad   0x8000000080008081, 0x8000000000008080
    .quad   0x0000000080000001, 0x8000000080008008

.section .note.GNU-stack,"",@progbits
