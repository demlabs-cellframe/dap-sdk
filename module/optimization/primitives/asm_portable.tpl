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
