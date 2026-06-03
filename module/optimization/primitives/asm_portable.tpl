{{! ================================================================ }}
{{! Portable ASM Macros — ELF / Mach-O / PE-COFF compatibility     }}
{{! ================================================================ }}
{{! Include via {{#include ASM_MACROS}} in any .S.tpl template.     }}
{{! Provides FUNC_TYPE, FUNC_SIZE, GNU_STACK, ADRP_PAIR macros     }}
{{! that expand correctly on each object format.                    }}
{{! ================================================================ }}

#ifdef __ELF__
# define FUNC_TYPE(name) .type name, @function
# define FUNC_SIZE(name) .size name, .-name
#else
# define FUNC_TYPE(name)
# define FUNC_SIZE(name)
#endif

#if defined(__ELF__)
# define GNU_STACK .section .note.GNU-stack,"",@progbits
#else
# define GNU_STACK
#endif

/* Read-only data: ELF .rodata is invalid on Mach-O (use __TEXT,__const). */
#if defined(__APPLE__)
# define SECTION_RODATA .section __TEXT,__const
#elif defined(__ELF__)
# define SECTION_RODATA .section .rodata
#else
# define SECTION_RODATA .section .rdata
#endif

#if defined(__APPLE__)
# define ADRP_LO(reg, sym) adrp reg, sym@PAGE ; add reg, reg, sym@PAGEOFF
#elif defined(__ELF__)
# define ADRP_LO(reg, sym) adrp reg, sym ; add reg, reg, :lo12:sym
#endif

/* ================================================================ */
/* x86-64 Calling Convention Abstraction                            */
/* ================================================================ */
/* Windows x64: args in RCX, RDX, R8, R9; RDI/RSI callee-saved      */
/* System V:    args in RDI, RSI, RDX, RCX; RDI/RSI caller-saved    */
/*                                                                  */
/* WIN64_SYSV_PROLOG: convert Win64 args to SysV positions          */
/* WIN64_SYSV_EPILOG: restore callee-saved RDI/RSI                  */
/* Use for functions expecting SysV ABI internally.                 */
/* ================================================================ */

#if defined(_WIN32) || defined(__CYGWIN__)
/* Save RDI/RSI (callee-saved on Win64), move args to SysV positions */
# define WIN64_SYSV_PROLOG_1ARG \
    pushq   %rdi ; \
    movq    %rcx, %rdi

# define WIN64_SYSV_EPILOG_1ARG \
    popq    %rdi

# define WIN64_SYSV_PROLOG_4ARG \
    pushq   %rdi ; \
    pushq   %rsi ; \
    movq    %rcx, %rdi ; \
    movq    %rdx, %rsi ; \
    movq    %r8,  %rdx ; \
    movq    %r9,  %rcx

# define WIN64_SYSV_EPILOG_4ARG \
    popq    %rsi ; \
    popq    %rdi

#else
/* System V ABI: no conversion needed */
# define WIN64_SYSV_PROLOG_1ARG
# define WIN64_SYSV_EPILOG_1ARG
# define WIN64_SYSV_PROLOG_4ARG
# define WIN64_SYSV_EPILOG_4ARG
#endif
