# DapArchCodegen.cmake
# Unified architecture-specific code generation for DAP SDK.
#
# Provides three macros:
#   dap_arch_generate_variant()  — generate ONE output from ONE template
#   dap_arch_generate()          — generate multiple arch variants from one template
#   dap_arch_add_sources()       — add generated sources to a target with correct compile flags
#
# Requires DAP_TPL_DIR and DAP_ARCH_DIR to be set before include().

if(NOT DEFINED DAP_ARCH_DIR)
    message(FATAL_ERROR "DAP_ARCH_DIR must be set before including DapArchCodegen.cmake")
endif()
if(NOT DEFINED DAP_TPL_DIR)
    message(FATAL_ERROR "DAP_TPL_DIR must be set before including DapArchCodegen.cmake")
endif()

set(_DAP_ARCH_CODEGEN_SCRIPT "${DAP_ARCH_DIR}/dap_arch_codegen.sh")
if(NOT EXISTS "${_DAP_ARCH_CODEGEN_SCRIPT}")
    message(FATAL_ERROR "dap_arch_codegen.sh not found at ${_DAP_ARCH_CODEGEN_SCRIPT}")
endif()

# ============================================================================
# Standard architecture table
# Each entry: _DAP_ARCH_<FIELD>_<ARCH>
# Fields: GUARD, FLAGS, FLAGS_ARM32, INCLUDES, TARGET_ATTR, NAME
# ============================================================================

# --- x86 ---
set(_DAP_ARCH_NAME_sse2       "SSE2")
set(_DAP_ARCH_GUARD_sse2      "x86")
set(_DAP_ARCH_FLAGS_sse2      "-msse2")
set(_DAP_ARCH_INCLUDES_sse2   "#include <emmintrin.h>")
set(_DAP_ARCH_TARGET_ATTR_sse2 "__attribute__((target(\"sse2\")))")

set(_DAP_ARCH_NAME_avx2       "AVX2")
set(_DAP_ARCH_GUARD_avx2      "x86")
set(_DAP_ARCH_FLAGS_avx2      "-mavx2")
set(_DAP_ARCH_INCLUDES_avx2   "#include <immintrin.h>")
set(_DAP_ARCH_TARGET_ATTR_avx2 "__attribute__((target(\"avx2\")))")

set(_DAP_ARCH_NAME_avx2_bmi2  "AVX2+BMI2")
set(_DAP_ARCH_GUARD_avx2_bmi2 "x86")
set(_DAP_ARCH_FLAGS_avx2_bmi2 "-mavx2 -mbmi -mbmi2")
set(_DAP_ARCH_INCLUDES_avx2_bmi2 "#include <immintrin.h>")
set(_DAP_ARCH_TARGET_ATTR_avx2_bmi2 "__attribute__((target(\"avx2,bmi,bmi2\")))")

set(_DAP_ARCH_NAME_avx2_512vl "AVX2+AVX-512VL")
set(_DAP_ARCH_GUARD_avx2_512vl "x86")
set(_DAP_ARCH_FLAGS_avx2_512vl "-mavx2 -mavx512f -mavx512vl -mavx512bw")
set(_DAP_ARCH_INCLUDES_avx2_512vl "#include <immintrin.h>")
set(_DAP_ARCH_TARGET_ATTR_avx2_512vl "__attribute__((target(\"avx2,avx512f,avx512vl,avx512bw\")))")

set(_DAP_ARCH_NAME_avx512     "AVX-512")
set(_DAP_ARCH_GUARD_avx512    "x86")
set(_DAP_ARCH_FLAGS_avx512    "-mavx512f -mavx512dq -mavx512bw -mavx512vl")
set(_DAP_ARCH_INCLUDES_avx512 "#include <immintrin.h>")
set(_DAP_ARCH_TARGET_ATTR_avx512 "__attribute__((target(\"avx512f,avx512bw,avx512vl\")))")

set(_DAP_ARCH_NAME_avx512_ifma     "AVX-512 IFMA")
set(_DAP_ARCH_GUARD_avx512_ifma    "x86")
set(_DAP_ARCH_FLAGS_avx512_ifma    "-mavx512f -mavx512ifma -mavx512vl")
set(_DAP_ARCH_INCLUDES_avx512_ifma "#include <immintrin.h>")
set(_DAP_ARCH_TARGET_ATTR_avx512_ifma "__attribute__((target(\"avx512f,avx512ifma,avx512vl\")))")

set(_DAP_ARCH_NAME_avx512_vbmi2     "AVX-512 VBMI2")
set(_DAP_ARCH_GUARD_avx512_vbmi2    "x86")
set(_DAP_ARCH_FLAGS_avx512_vbmi2    "-mavx512f -mavx512bw -mavx512vl -mavx512vbmi2 -mpopcnt")
set(_DAP_ARCH_INCLUDES_avx512_vbmi2 "#include <immintrin.h>")
set(_DAP_ARCH_TARGET_ATTR_avx512_vbmi2 "__attribute__((target(\"avx2,avx512bw,avx512vl,avx512vbmi2,popcnt\")))")

set(_DAP_ARCH_NAME_x86_64_asm "x86-64 ASM")
set(_DAP_ARCH_GUARD_x86_64_asm "x86")
set(_DAP_ARCH_FLAGS_x86_64_asm "")
set(_DAP_ARCH_INCLUDES_x86_64_asm "")
set(_DAP_ARCH_TARGET_ATTR_x86_64_asm "")

# --- ARM ---
set(_DAP_ARCH_NAME_neon       "NEON")
set(_DAP_ARCH_GUARD_neon      "arm")
set(_DAP_ARCH_FLAGS_neon      "")
set(_DAP_ARCH_FLAGS_ARM32_neon "-mfpu=neon")
set(_DAP_ARCH_INCLUDES_neon   "#include <arm_neon.h>")
set(_DAP_ARCH_TARGET_ATTR_neon "")

set(_DAP_ARCH_NAME_sve        "SVE")
set(_DAP_ARCH_GUARD_sve       "sve")
set(_DAP_ARCH_FLAGS_sve       "-march=armv8.2-a+sve")
set(_DAP_ARCH_INCLUDES_sve    "#include <arm_sve.h>")
set(_DAP_ARCH_TARGET_ATTR_sve "__attribute__((target(\"+sve\")))")

set(_DAP_ARCH_NAME_sve2       "SVE2")
set(_DAP_ARCH_GUARD_sve2      "sve")
set(_DAP_ARCH_FLAGS_sve2      "-march=armv8.2-a+sve2")
set(_DAP_ARCH_INCLUDES_sve2   "#include <arm_sve.h>")
set(_DAP_ARCH_TARGET_ATTR_sve2 "__attribute__((target(\"+sve2\")))")

# --- Generic ---
set(_DAP_ARCH_NAME_generic    "Generic")
set(_DAP_ARCH_GUARD_generic   "none")
set(_DAP_ARCH_FLAGS_generic   "")
set(_DAP_ARCH_INCLUDES_generic "")
set(_DAP_ARCH_TARGET_ATTR_generic "")

# Guard-family mapping for platform filtering
set(_DAP_ARCH_FAMILY_x86   "sse2;avx2;avx2_bmi2;avx2_512vl;avx512;avx512_ifma;avx512_vbmi2;x86_64_asm")
set(_DAP_ARCH_FAMILY_arm   "neon")
set(_DAP_ARCH_FAMILY_sve   "sve;sve2")

# Portable ASM macros (ELF/Mach-O/PE-COFF compatibility)
set(DAP_ASM_MACROS "${DAP_PRIMITIVES_DIR}/asm_portable.tpl" CACHE INTERNAL "")

# Shared primitive library paths (auto-injected as PRIM_LIB template variable)
# Templates live in module/optimization/primitives/, exposed via DAP_PRIMITIVES_DIR
set(_DAP_ARCH_PRIM_LIB_sse2       "${DAP_PRIMITIVES_DIR}/x86/sse2.tpl")
set(_DAP_ARCH_PRIM_LIB_avx2       "${DAP_PRIMITIVES_DIR}/x86/avx2.tpl")
set(_DAP_ARCH_PRIM_LIB_avx2_bmi2  "${DAP_PRIMITIVES_DIR}/x86/avx2.tpl")
set(_DAP_ARCH_PRIM_LIB_avx2_512vl "${DAP_PRIMITIVES_DIR}/x86/avx2.tpl")
set(_DAP_ARCH_PRIM_LIB_avx512     "${DAP_PRIMITIVES_DIR}/x86/avx512.tpl")
set(_DAP_ARCH_PRIM_LIB_avx512_ifma "${DAP_PRIMITIVES_DIR}/x86/avx512.tpl")
set(_DAP_ARCH_PRIM_LIB_neon       "${DAP_PRIMITIVES_DIR}/arm/neon.tpl")
set(_DAP_ARCH_PRIM_LIB_sve        "${DAP_PRIMITIVES_DIR}/arm/sve.tpl")
set(_DAP_ARCH_PRIM_LIB_sve2       "${DAP_PRIMITIVES_DIR}/arm/sve2.tpl")

# ============================================================================
# dap_arch_generate_variant(TEMPLATE t OUTPUT o ARCH a [ARGS key=val ...])
# Generate ONE output file from ONE template.
# ============================================================================
function(dap_arch_generate_variant)
    cmake_parse_arguments(ARG "" "TEMPLATE;OUTPUT;ARCH;GUARD" "ARGS" ${ARGN})

    if(NOT ARG_TEMPLATE)
        message(FATAL_ERROR "dap_arch_generate_variant: TEMPLATE is required")
    endif()
    if(NOT ARG_OUTPUT)
        message(FATAL_ERROR "dap_arch_generate_variant: OUTPUT is required")
    endif()
    if(NOT ARG_ARCH)
        message(FATAL_ERROR "dap_arch_generate_variant: ARCH is required")
    endif()

    set(_arch "${ARG_ARCH}")

    # Look up standard values; fall back to empty strings for unknown archs
    if(DEFINED _DAP_ARCH_NAME_${_arch})
        set(_name "${_DAP_ARCH_NAME_${_arch}}")
    else()
        set(_name "${_arch}")
    endif()

    if(ARG_GUARD)
        set(_guard "${ARG_GUARD}")
    elseif(DEFINED _DAP_ARCH_GUARD_${_arch})
        set(_guard "${_DAP_ARCH_GUARD_${_arch}}")
    else()
        set(_guard "none")
    endif()

    if(DEFINED _DAP_ARCH_INCLUDES_${_arch})
        set(_includes "${_DAP_ARCH_INCLUDES_${_arch}}")
    else()
        set(_includes "")
    endif()

    if(DEFINED _DAP_ARCH_TARGET_ATTR_${_arch})
        set(_target_attr "${_DAP_ARCH_TARGET_ATTR_${_arch}}")
    else()
        set(_target_attr "")
    endif()

    # Build argument list: standard arch values first, then user ARGS (which can override)
    set(_tpl_args
        "ARCH_NAME=${_name}"
        "ARCH_LOWER=${_arch}"
        "ARCH_INCLUDES=${_includes}"
        "TARGET_ATTR=${_target_attr}"
    )

    # Auto-inject shared primitive library path for this architecture
    if(DEFINED _DAP_ARCH_PRIM_LIB_${_arch})
        list(APPEND _tpl_args "PRIM_LIB=${_DAP_ARCH_PRIM_LIB_${_arch}}")
    endif()

    # Auto-inject portable ASM macros for .S templates
    if(ARG_OUTPUT MATCHES "\\.S$" AND DEFINED DAP_ASM_MACROS)
        list(APPEND _tpl_args "ASM_MACROS=${DAP_ASM_MACROS}")
    endif()

    if(ARG_ARGS)
        list(APPEND _tpl_args ${ARG_ARGS})
    endif()

    # Delegate template processing to dap_arch_codegen.sh (template + guard in one pass)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
            "DAP_TPL_DIR=${DAP_TPL_DIR}"
            "DAP_ARCH_TEMPLATE=${ARG_TEMPLATE}"
            "DAP_ARCH_OUTPUT=${ARG_OUTPUT}"
            "DAP_ARCH_GUARD=${_guard}"
            bash "${_DAP_ARCH_CODEGEN_SCRIPT}" ${_tpl_args}
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
    )

    if(NOT _rc EQUAL 0)
        message(WARNING "[DapArchCodegen] Failed to generate ${ARG_OUTPUT}:")
        message(WARNING "  exit code: ${_rc}")
        message(WARNING "  stdout: ${_out}")
        message(WARNING "  stderr: ${_err}")
    endif()
endfunction()

# ============================================================================
# dap_arch_generate(TEMPLATE t OUTPUT_DIR d PREFIX p
#                   VARIANTS arch1 prim1 [arch2 prim2 ...]
#                   [EXTRA_ARGS key=val ...])
# Generate multiple arch variants from one template.
# Output files: ${OUTPUT_DIR}/${PREFIX}_${arch}.c
# VARIANTS is a flat list of pairs: (arch_id, primitives_path).
# ============================================================================
function(dap_arch_generate)
    cmake_parse_arguments(ARG "" "TEMPLATE;OUTPUT_DIR;PREFIX" "VARIANTS;EXTRA_ARGS" ${ARGN})

    if(NOT ARG_TEMPLATE)
        message(FATAL_ERROR "dap_arch_generate: TEMPLATE is required")
    endif()
    if(NOT ARG_OUTPUT_DIR)
        message(FATAL_ERROR "dap_arch_generate: OUTPUT_DIR is required")
    endif()
    if(NOT ARG_PREFIX)
        message(FATAL_ERROR "dap_arch_generate: PREFIX is required")
    endif()

    file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")

    list(LENGTH ARG_VARIANTS _vlen)
    math(EXPR _last "${_vlen} - 1")
    set(_i 0)

    while(_i LESS _vlen)
        list(GET ARG_VARIANTS ${_i} _arch)
        math(EXPR _j "${_i} + 1")
        list(GET ARG_VARIANTS ${_j} _primitives)
        math(EXPR _i "${_j} + 1")

        set(_output "${ARG_OUTPUT_DIR}/${ARG_PREFIX}_${_arch}.c")
        set(_args "PRIMITIVES_FILE=${_primitives}")

        if(ARG_EXTRA_ARGS)
            list(APPEND _args ${ARG_EXTRA_ARGS})
        endif()

        dap_arch_generate_variant(
            TEMPLATE "${ARG_TEMPLATE}"
            OUTPUT   "${_output}"
            ARCH     "${_arch}"
            ARGS     ${_args}
        )
    endwhile()
endfunction()

# ============================================================================
# dap_arch_add_sources(TARGET t OUTPUT_DIR d SOURCES file1 arch1 [file2 arch2 ...])
# Add generated sources to target with correct COMPILE_FLAGS and GENERATED=TRUE.
# Platform filtering: only includes files whose arch matches the build platform.
# On APPLE: includes everything (arch guards in .c files handle selection).
# ============================================================================
function(dap_arch_add_sources)
    cmake_parse_arguments(ARG "" "TARGET;OUTPUT_DIR" "SOURCES" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "dap_arch_add_sources: TARGET is required")
    endif()
    if(NOT ARG_OUTPUT_DIR)
        message(FATAL_ERROR "dap_arch_add_sources: OUTPUT_DIR is required")
    endif()

    # Determine which arch families are available on this platform
    set(_available_archs "generic")
    if(APPLE)
        list(APPEND _available_archs ${_DAP_ARCH_FAMILY_x86} ${_DAP_ARCH_FAMILY_arm})
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|i686|i386")
        list(APPEND _available_archs ${_DAP_ARCH_FAMILY_x86})
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        list(APPEND _available_archs ${_DAP_ARCH_FAMILY_arm} ${_DAP_ARCH_FAMILY_sve})
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
        list(APPEND _available_archs ${_DAP_ARCH_FAMILY_arm})
    endif()

    set(_added_sources "")

    list(LENGTH ARG_SOURCES _slen)
    set(_i 0)
    while(_i LESS _slen)
        list(GET ARG_SOURCES ${_i} _file)
        math(EXPR _j "${_i} + 1")
        list(GET ARG_SOURCES ${_j} _arch)
        math(EXPR _i "${_j} + 1")

        # Platform filter
        list(FIND _available_archs "${_arch}" _found)
        if(_found EQUAL -1)
            continue()
        endif()

        set(_src "${ARG_OUTPUT_DIR}/${_file}")
        if(NOT EXISTS "${_src}")
            message(WARNING "[DapArchCodegen] Generated source not found: ${_src}")
            continue()
        endif()

        # Compile flags from standard table
        set(_flags "")
        if(DEFINED _DAP_ARCH_FLAGS_${_arch})
            set(_flags "${_DAP_ARCH_FLAGS_${_arch}}")
        endif()

        # ARM32 override
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            if(DEFINED _DAP_ARCH_FLAGS_ARM32_${_arch})
                set(_flags "${_DAP_ARCH_FLAGS_ARM32_${_arch}}")
            endif()
        endif()

        # On Apple universal builds, don't set per-file ISA flags — target attrs handle it
        if(APPLE)
            set(_flags "")
        endif()

        if(_flags)
            set_source_files_properties("${_src}" PROPERTIES COMPILE_FLAGS "${_flags}")
        endif()

        set_source_files_properties("${_src}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE)

        list(APPEND _added_sources "${_src}")
    endwhile()

    if(_added_sources)
        target_sources(${ARG_TARGET} PRIVATE ${_added_sources})
        target_include_directories(${ARG_TARGET} PRIVATE "${ARG_OUTPUT_DIR}")
    endif()
endfunction()
