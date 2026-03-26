/*
 * Scalar GPR+BMI2 Keccak-f[1600] — register-persistent in-place rounds.
 *
 * Derived from GCC -O3 output of keccak_f1600_opt64.c (the C reference).
 * Keeps ~7 state lanes in callee-saved registers across all 24 rounds,
 * remaining 18 in stack slots.  Two rounds per loop iteration.
 *
 * For x86-64 with BMI2.  Fallback permutation when AVX-512 is unavailable
 * or less efficient (e.g., some Intel client cores).
 */

	.text
	.balign 64
	.globl	dap_hash_keccak_permute_scalar_bmi2
	.type	dap_hash_keccak_permute_scalar_bmi2, @function
dap_hash_keccak_permute_scalar_bmi2:
	pushq	%r15
	movq	%rdi, %rdx
	pushq	%r14
	pushq	%r13
	pushq	%r12
	pushq	%rbp
	pushq	%rbx
	subq	$88, %rsp
	movq	40(%rdi), %rax
	movq	(%rdi), %r15
	movq	160(%rdi), %r10
	movq	128(%rdi), %r14
	movq	%rax, -80(%rsp)
	movq	80(%rdi), %rax
	movq	%rax, -40(%rsp)
	movq	120(%rdi), %rax
	movq	%rax, -8(%rsp)
	movq	8(%rdi), %rax
	movq	%rax, -112(%rsp)
	movq	48(%rdi), %rax
	movq	%rax, -72(%rsp)
	movq	88(%rdi), %rax
	movq	%rax, -32(%rsp)
	movq	168(%rdi), %rax
	movq	%rax, 16(%rsp)
	movq	16(%rdi), %rax
	movq	%rax, -104(%rsp)
	movq	56(%rdi), %rax
	movq	96(%rdi), %r13
	movq	176(%rdi), %r12
	movq	%rdx, 80(%rsp)
	movq	%rax, -64(%rsp)
	movq	136(%rdi), %rax
	movq	144(%rdi), %r11
	movq	%rax, (%rsp)
	movq	24(%rdi), %rax
	movq	%rax, -96(%rsp)
	movq	64(%rdi), %rax
	movq	%rax, -56(%rsp)
	movq	104(%rdi), %rax
	movq	%rax, -24(%rsp)
	movq	184(%rdi), %rax
	movq	%rax, 24(%rsp)
	movq	32(%rdi), %rax
	movq	%rax, -88(%rsp)
	movq	72(%rdi), %rax
	movq	%rax, -48(%rsp)
	movq	112(%rdi), %rax
	movq	%rax, -16(%rsp)
	movq	152(%rdi), %rax
	movq	%rax, 8(%rsp)
	movq	192(%rdi), %rax
	leaq	.Lrc(%rip), %rdi
	movq	%rdi, -120(%rsp)
	.balign 64
	.p2align 3
.L2:
	movq	-80(%rsp), %rdi
	movq	-40(%rsp), %rsi
	movq	-8(%rsp), %rdx
	movq	-32(%rsp), %rbx
	xorq	%r15, %rdi
	movq	16(%rsp), %rcx
	movq	-64(%rsp), %r8
	xorq	%rsi, %rdi
	movq	-112(%rsp), %rsi
	movq	(%rsp), %r9
	xorq	%rdx, %rdi
	movq	-72(%rsp), %rdx
	xorq	%r10, %rdi
	xorq	%rdx, %rsi
	movq	-24(%rsp), %rdx
	movq	%rsi, %rbp
	movq	-88(%rsp), %rsi
	xorq	%rbx, %rbp
	xorq	%r14, %rbp
	xorq	%rcx, %rbp
	movq	-104(%rsp), %rcx
	xorq	%r8, %rcx
	movq	-56(%rsp), %r8
	movq	%rcx, %rbx
	movq	-96(%rsp), %rcx
	xorq	%r13, %rbx
	xorq	%r9, %rbx
	xorq	%r8, %rcx
	movq	-48(%rsp), %r9
	movq	24(%rsp), %r8
	xorq	%rdx, %rcx
	movq	-16(%rsp), %rdx
	xorq	%r12, %rbx
	xorq	%r9, %rsi
	xorq	%r11, %rcx
	rorx	$63, %rbp, %r9
	xorq	%rdx, %rsi
	movq	8(%rsp), %rdx
	xorq	%r8, %rcx
	rorx	$63, %rbx, %r8
	xorq	%rdi, %r8
	rorx	$63, %rdi, %rdi
	xorq	%rcx, %rdi
	xorq	%rdx, %rsi
	rorx	$63, %rcx, %rdx
	movq	-120(%rsp), %rcx
	xorq	%rbp, %rdx
	xorq	%rax, %rsi
	xorq	%rdx, %r13
	xorq	%rdi, %rax
	xorq	%rsi, %r9
	rorx	$63, %rsi, %rsi
	xorq	%rbx, %rsi
	movq	%r15, %rbx
	movq	-72(%rsp), %r15
	rorx	$21, %r13, %r13
	xorq	%r9, %rbx
	xorq	%rsi, %r11
	rorx	$43, %r11, %r11
	rorx	$50, %rax, %rax
	xorq	%r8, %r15
	rorx	$20, %r15, %rbp
	andn	%r13, %rbp, %r15
	movq	%r15, -72(%rsp)
	movq	(%rcx), %r15
	xorq	%rbx, %r15
	movq	%r15, %rcx
	movq	-72(%rsp), %r15
	xorq	%rcx, %r15
	andn	%r11, %r13, %rcx
	xorq	%rbp, %rcx
	xorq	%r8, %r14
	movq	%rcx, 32(%rsp)
	andn	%rax, %r11, %rcx
	xorq	%r13, %rcx
	rorx	$19, %r14, %r14
	movq	%rcx, 40(%rsp)
	andn	%rbx, %rax, %rcx
	movq	%rcx, %r13
	andn	%rbp, %rbx, %rbx
	xorq	%r11, %r13
	xorq	%rax, %rbx
	movq	-40(%rsp), %r11
	movq	-96(%rsp), %rax
	movq	%r13, -72(%rsp)
	movq	-48(%rsp), %r13
	xorq	%rdx, %r12
	xorq	%r9, %r10
	xorq	%rsi, %rax
	xorq	%r9, %r11
	rorx	$3, %r12, %r12
	rorx	$46, %r10, %r10
	xorq	%rdi, %r13
	rorx	$61, %r11, %r11
	rorx	$36, %rax, %rax
	movq	%rbx, 48(%rsp)
	rorx	$44, %r13, %rcx
	andn	%r11, %rcx, %rbx
	xorq	%rax, %rbx
	movq	%rbx, 56(%rsp)
	andn	%r14, %r11, %rbx
	xorq	%rcx, %rbx
	movq	%rbx, -96(%rsp)
	andn	%r12, %r14, %rbx
	movq	%rbx, %r13
	xorq	%r11, %r13
	andn	%rax, %r12, %r11
	xorq	%r14, %r11
	andn	%rcx, %rax, %rax
	movq	%r13, -40(%rsp)
	movq	-64(%rsp), %r14
	xorq	%r12, %rax
	movq	%r11, 64(%rsp)
	movq	8(%rsp), %r11
	xorq	%rdx, %r14
	movq	%rax, -48(%rsp)
	movq	-112(%rsp), %rax
	rorx	$58, %r14, %rcx
	movq	-24(%rsp), %r14
	xorq	%rdi, %r11
	rorx	$56, %r11, %r11
	xorq	%r8, %rax
	xorq	%rsi, %r14
	rorx	$63, %rax, %rax
	rorx	$39, %r14, %rbp
	andn	%rbp, %rcx, %rbx
	movq	%rbx, %r14
	andn	%r11, %rbp, %rbx
	movq	%rbx, %r12
	xorq	%rax, %r14
	xorq	%rcx, %r12
	andn	%r10, %r11, %rbx
	xorq	%rbp, %rbx
	andn	%rax, %r10, %rbp
	andn	%rcx, %rax, %rax
	xorq	%r10, %rax
	movq	(%rsp), %r10
	xorq	%r11, %rbp
	movq	%r12, 8(%rsp)
	movq	-32(%rsp), %r11
	xorq	%rdx, %r10
	movq	%rbp, -24(%rsp)
	movq	-80(%rsp), %rbp
	rorx	$49, %r10, %r12
	movq	%rax, 72(%rsp)
	movq	24(%rsp), %r10
	xorq	%r8, %r11
	movq	-88(%rsp), %rax
	xorq	%r9, %rbp
	rorx	$54, %r11, %r11
	movq	%r14, -80(%rsp)
	xorq	%rsi, %r10
	rorx	$28, %rbp, %rbp
	xorq	%rdi, %rax
	rorx	$8, %r10, %rcx
	andn	%r11, %rbp, %r10
	andn	%rcx, %r12, %r13
	rorx	$37, %rax, %rax
	xorq	%rax, %r10
	xorq	%r11, %r13
	movq	%r10, 24(%rsp)
	andn	%r12, %r11, %r10
	andn	%rax, %rcx, %r11
	xorq	%r12, %r11
	movq	-104(%rsp), %r12
	xorq	%rbp, %r10
	andn	%rbp, %rax, %rax
	xorq	%rcx, %rax
	movq	-56(%rsp), %rbp
	movq	%rax, -32(%rsp)
	xorq	%r12, %rdx
	movq	-8(%rsp), %r12
	movq	%r13, (%rsp)
	xorq	%rbp, %rsi
	movq	-16(%rsp), %rbp
	rorx	$2, %rdx, %rdx
	xorq	%r12, %r9
	rorx	$9, %rsi, %rsi
	movq	56(%rsp), %r12
	rorx	$23, %r9, %rcx
	movq	16(%rsp), %r9
	xorq	%rbp, %rdi
	rorx	$25, %rdi, %rdi
	xorq	%r9, %r8
	andn	%rcx, %rdi, %r9
	xorq	%rsi, %r9
	rorx	$62, %r8, %rax
	movq	%r9, %rbp
	andn	%rdi, %rsi, %r8
	andn	%rax, %rcx, %r9
	xorq	%rdx, %r8
	xorq	%rdi, %r9
	andn	%rdx, %rax, %rdi
	andn	%rsi, %rdx, %rdx
	movq	24(%rsp), %rsi
	xorq	%r14, %r12
	movq	%r9, -64(%rsp)
	xorq	%rcx, %rdi
	movq	-96(%rsp), %r9
	movq	32(%rsp), %r14
	movq	%rdi, -8(%rsp)
	xorq	%rax, %rdx
	xorq	%rsi, %r12
	movq	8(%rsp), %rsi
	movq	%rbp, 16(%rsp)
	xorq	%r9, %r14
	movq	40(%rsp), %rdi
	xorq	%r8, %r12
	xorq	%rsi, %r14
	movq	-40(%rsp), %rsi
	movq	(%rsp), %rcx
	xorq	%r15, %r12
	movq	-64(%rsp), %rax
	xorq	%r10, %r14
	xorq	%rsi, %rdi
	xorq	%rbp, %r14
	movq	64(%rsp), %rsi
	movq	-72(%rsp), %rbp
	movq	%rdi, %r13
	movq	-8(%rsp), %rdi
	xorq	%rbx, %r13
	xorq	%rsi, %rbp
	movq	-48(%rsp), %rsi
	xorq	%rcx, %r13
	movq	72(%rsp), %rcx
	xorq	%rax, %r13
	movq	-24(%rsp), %rax
	xorq	%rcx, %rsi
	xorq	%rax, %rbp
	movq	48(%rsp), %rax
	xorq	%r11, %rbp
	xorq	%rax, %rsi
	movq	-32(%rsp), %rax
	xorq	%rdi, %rbp
	rorx	$63, %r14, %rdi
	rorx	$63, %rbp, %rcx
	xorq	%r14, %rcx
	movq	-120(%rsp), %r14
	xorq	%rax, %rsi
	rorx	$63, %r13, %rax
	xorq	%r12, %rax
	rorx	$63, %r12, %r12
	xorq	%rdx, %rsi
	xorq	%rbp, %r12
	xorq	%rsi, %rdi
	rorx	$63, %rsi, %rsi
	xorq	%r13, %rsi
	xorq	%rdi, %r15
	movq	%r15, %rbp
	movq	%r9, %r15
	movq	8(%r14), %r9
	xorq	%rax, %r15
	xorq	%rcx, %rbx
	xorq	%rsi, %r11
	xorq	%r12, %rdx
	xorq	%rbp, %r9
	rorx	$20, %r15, %r13
	rorx	$21, %rbx, %rbx
	rorx	$43, %r11, %r11
	andn	%rbx, %r13, %r15
	andn	%r11, %rbx, %r14
	xorq	%r9, %r15
	movq	%r14, %r9
	xorq	%r13, %r9
	rorx	$50, %rdx, %rdx
	andn	%rdx, %r11, %r14
	xorq	%rax, %r10
	movq	%r9, -112(%rsp)
	movq	%r14, %r9
	rorx	$19, %r10, %r10
	xorq	%rbx, %r9
	andn	%rbp, %rdx, %rbx
	andn	%r13, %rbp, %rbp
	movq	%rbp, %r13
	movq	%r9, -104(%rsp)
	movq	%rbx, %r9
	movq	-72(%rsp), %rbp
	xorq	%rdx, %r13
	xorq	%r11, %r9
	movq	-80(%rsp), %rbx
	movq	-48(%rsp), %r11
	movq	%r13, -88(%rsp)
	xorq	%rsi, %rbp
	movq	%r9, -96(%rsp)
	movq	-64(%rsp), %r9
	xorq	%r12, %r11
	xorq	%rdi, %rbx
	rorx	$36, %rbp, %rdx
	rorx	$44, %r11, %r11
	rorx	$61, %rbx, %rbx
	andn	%rbx, %r11, %rbp
	movq	%rbp, %r13
	xorq	%rdx, %r13
	andn	%r10, %rbx, %rbp
	xorq	%rcx, %r9
	movq	%r13, -80(%rsp)
	movq	%rbp, %r13
	rorx	$3, %r9, %r9
	andn	%r9, %r10, %rbp
	xorq	%r11, %r13
	movq	%r13, -72(%rsp)
	movq	%rbp, %r13
	xorq	%rbx, %r13
	andn	%rdx, %r9, %rbx
	andn	%r11, %rdx, %rdx
	movq	-24(%rsp), %r11
	movq	%r13, -64(%rsp)
	movq	%rbx, %r13
	xorq	%r10, %r13
	movq	-40(%rsp), %r10
	xorq	%rsi, %r11
	movq	%r13, -56(%rsp)
	movq	%rdx, %r13
	movq	32(%rsp), %rdx
	rorx	$39, %r11, %r11
	xorq	%rcx, %r10
	xorq	%r9, %r13
	rorx	$58, %r10, %r9
	movq	-32(%rsp), %r10
	xorq	%rax, %rdx
	movq	%r13, -48(%rsp)
	rorx	$63, %rdx, %rdx
	xorq	%r12, %r10
	andn	%r11, %r9, %rbx
	movq	%rbx, %r13
	xorq	%rdi, %r8
	xorq	%rdx, %r13
	rorx	$56, %r10, %r10
	andn	%r10, %r11, %rbx
	rorx	$46, %r8, %r8
	movq	%r13, -40(%rsp)
	movq	%rbx, %r13
	xorq	%r9, %r13
	movq	%r13, -32(%rsp)
	andn	%r8, %r10, %r13
	xorq	%r11, %r13
	andn	%rdx, %r8, %r11
	xorq	%r10, %r11
	andn	%r9, %rdx, %rdx
	movq	%r11, -24(%rsp)
	movq	8(%rsp), %r10
	movq	%rdx, %r11
	movq	56(%rsp), %r9
	movq	48(%rsp), %rdx
	xorq	%r8, %r11
	movq	(%rsp), %rbx
	movq	-8(%rsp), %r8
	xorq	%rax, %r10
	movq	%r11, -16(%rsp)
	xorq	%r12, %rdx
	xorq	%rdi, %r9
	rorx	$54, %r10, %r10
	movq	16(%rsp), %rbp
	rorx	$28, %r9, %r9
	xorq	%rcx, %rbx
	andn	%r10, %r9, %r11
	xorq	%rsi, %r8
	rorx	$37, %rdx, %rdx
	xorq	%rdx, %r11
	rorx	$49, %rbx, %rbx
	rorx	$8, %r8, %r8
	movq	%r11, -8(%rsp)
	andn	%r8, %rbx, %r11
	xorq	%r10, %r11
	andn	%rbx, %r10, %r14
	movq	%r11, (%rsp)
	xorq	%r9, %r14
	andn	%rdx, %r8, %r11
	xorq	%rbx, %r11
	andn	%r9, %rdx, %rdx
	movq	%rdx, %r9
	movq	64(%rsp), %rdx
	movq	24(%rsp), %r10
	movq	40(%rsp), %rbx
	xorq	%rbp, %rax
	xorq	%r8, %r9
	xorq	%rdx, %rsi
	movq	72(%rsp), %rdx
	xorq	%r10, %rdi
	rorx	$62, %rax, %r8
	rorx	$23, %rdi, %rdi
	rorx	$9, %rsi, %rsi
	xorq	%rbx, %rcx
	movq	%r9, 8(%rsp)
	xorq	%rdx, %r12
	rorx	$2, %rcx, %rcx
	rorx	$25, %r12, %rdx
	andn	%rdx, %rsi, %r10
	andn	%rdi, %rdx, %rax
	xorq	%rsi, %rax
	movq	%rax, 16(%rsp)
	andn	%rcx, %r8, %rax
	xorq	%rdi, %rax
	xorq	%rcx, %r10
	addq	$16, -120(%rsp)
	andn	%r8, %rdi, %r12
	leaq	192+.Lrc(%rip), %rdi
	xorq	%rdx, %r12
	movq	%rax, 24(%rsp)
	andn	%rsi, %rcx, %rax
	movq	-120(%rsp), %rcx
	xorq	%r8, %rax
	cmpq	%rcx, %rdi
	jne	.L2
	movq	80(%rsp), %rdx
	movq	-112(%rsp), %rdi
	movq	%rdi, 8(%rdx)
	movq	-104(%rsp), %rdi
	movq	%r15, (%rdx)
	movq	%rdi, 16(%rdx)
	movq	-96(%rsp), %rdi
	movq	%r13, 96(%rdx)
	movq	%rdi, 24(%rdx)
	movq	-88(%rsp), %rdi
	movq	%r14, 128(%rdx)
	movq	%rdi, 32(%rdx)
	movq	-80(%rsp), %rdi
	movq	%rdi, 40(%rdx)
	movq	-72(%rsp), %rdi
	movq	%rdi, 48(%rdx)
	movq	-64(%rsp), %rdi
	movq	%rdi, 56(%rdx)
	movq	-56(%rsp), %rdi
	movq	%rdi, 64(%rdx)
	movq	-48(%rsp), %rdi
	movq	%rdi, 72(%rdx)
	movq	-40(%rsp), %rdi
	movq	%rdi, 80(%rdx)
	movq	-32(%rsp), %rdi
	movq	%rdi, 88(%rdx)
	movq	-24(%rsp), %rdi
	movq	%rdi, 104(%rdx)
	movq	-16(%rsp), %rdi
	movq	%rdi, 112(%rdx)
	movq	-8(%rsp), %rdi
	movq	%rdi, 120(%rdx)
	movq	(%rsp), %rdi
	movq	%r11, 144(%rdx)
	movq	%rdi, 136(%rdx)
	movq	16(%rsp), %rdi
	movq	%r9, 152(%rdx)
	movq	%rdi, 168(%rdx)
	movq	24(%rsp), %rdi
	movq	%r10, 160(%rdx)
	movq	%r12, 176(%rdx)
	movq	%rdi, 184(%rdx)
	movq	%rax, 192(%rdx)
	addq	$88, %rsp
	popq	%rbx
	popq	%rbp
	popq	%r12
	popq	%r13
	popq	%r14
	popq	%r15
	ret
	.size	dap_hash_keccak_permute_scalar_bmi2, .-dap_hash_keccak_permute_scalar_bmi2
	.section	.rodata
	.balign 64
	.type	.Lrc, @object
	.size	.Lrc, 192
.Lrc:
	.quad	1
	.quad	32898
	.quad	-9223372036854742902
	.quad	-9223372034707259392
	.quad	32907
	.quad	2147483649
	.quad	-9223372034707259263
	.quad	-9223372036854743031
	.quad	138
	.quad	136
	.quad	2147516425
	.quad	2147483658
	.quad	2147516555
	.quad	-9223372036854775669
	.quad	-9223372036854742903
	.quad	-9223372036854743037
	.quad	-9223372036854743038
	.quad	-9223372036854775680
	.quad	32778
	.quad	-9223372034707292150
	.quad	-9223372034707259263
	.quad	-9223372036854742912
	.quad	2147483649
	.quad	-9223372034707259384
	.section	.note.GNU-stack,"",@progbits
