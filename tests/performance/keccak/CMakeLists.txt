# Keccak/SHA3 Performance Benchmark
# Compares DAP implementation against competitors

cmake_minimum_required(VERSION 3.13)

project(benchmark_keccak C)

include(${DAP_SDK_ROOT_DIR}/cmake/TargetArch_Detection.cmake)
target_architecture(TARGET_ARCH)

# =============================================================================
# Competitor implementations (optional, enabled via BUILD_BENCHMARKS)
# =============================================================================

set(COMPETITORS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/competitors")

if(BUILD_BENCHMARKS)
    message(STATUS "[benchmark_keccak] BUILD_BENCHMARKS enabled - checking for competitors...")
    
    # -------------------------------------------------------------------------
    # XKCP - eXtended Keccak Code Package (native build with platform optimizations)
    # Uses XKCP's native build system directly in its source directory
    # 
    # XKCP target format: <platform>/libXKCP.a where <platform> is:
    #   - x86-64: automatic runtime selection among 64-bit plain C and SSSE3, AVX2, AVX-512
    #   - ARMv8A: ARM64 NEON optimized
    #   - ARMv7A: ARM32 NEON optimized
    #   - generic64: plain C optimized for 64-bit
    #   - compact: minimal code size
    # See XKCP/README.markdown for full list
    # -------------------------------------------------------------------------
    set(XKCP_DIR "${COMPETITORS_DIR}/XKCP")
    
    if(EXISTS "${XKCP_DIR}/Makefile.build" AND EXISTS "${XKCP_DIR}/support/XKCBuild/src/Main.makefile")
        message(STATUS "[benchmark_keccak] Found XKCP repository with build system")
        
        # Find required tools
        find_program(XMLSTYLEPROC xsltproc)
        find_program(MAKE make)
        if(NOT MAKE)
            find_program(MAKE gmake)
        endif()
        
        if(XMLSTYLEPROC AND MAKE)
            message(STATUS "[benchmark_keccak] XKCP build tools found: make=${MAKE}, xsltproc=${XMLSTYLEPROC}")
            
            # Determine XKCP platform target based on architecture
            # These are standard XKCP target names - do not change!
            if(${TARGET_ARCH} MATCHES "x86_64")
                # x86-64 includes runtime CPU detection for AVX2/AVX512
                set(XKCP_PLATFORM "x86-64")
            elseif(${TARGET_ARCH} MATCHES "aarch64" OR ${TARGET_ARCH} MATCHES "arm64")
                set(XKCP_PLATFORM "ARMv8A")
            elseif(${TARGET_ARCH} MATCHES "armv7")
                set(XKCP_PLATFORM "ARMv7A")
            elseif(${TARGET_ARCH} MATCHES "i386" OR ${TARGET_ARCH} MATCHES "i686")
                set(XKCP_PLATFORM "generic32")
            else()
                set(XKCP_PLATFORM "generic64")
                message(STATUS "[benchmark_keccak] Unknown arch ${TARGET_ARCH}, using generic64")
            endif()
            
            # Full target name in XKCP format: <platform>/libXKCP.a
            set(XKCP_MAKE_TARGET "${XKCP_PLATFORM}/libXKCP.a")
            message(STATUS "[benchmark_keccak] XKCP target: ${XKCP_MAKE_TARGET}")
            
            # XKCP builds into bin/ directory inside its source tree
            # Library: bin/<platform>/libXKCP.a
            # Config: bin/.build/<platform>/libXKCP.a/config.h
            set(XKCP_BUILD_LIB "${XKCP_DIR}/bin/${XKCP_PLATFORM}/libXKCP.a")
            set(XKCP_BUILD_CONFIG_DIR "${XKCP_DIR}/bin/.build/${XKCP_PLATFORM}/libXKCP.a")
            
            # Include directories for XKCP (platform-specific):
            # - lib/high/Keccak/FIPS202/ for SimpleFIPS202.h, KeccakHash.h
            # - lib/high/Keccak/ for KeccakSponge.h, etc.
            # - lib/common/ for align.h, brg_endian.h
            # - bin/.build/<platform>/libXKCP.a/ for config.h (generated)
            # - lib/low/common/ for SnP-common.h
            # - Platform-specific low-level includes
            set(XKCP_INCLUDE_DIRS
                "${XKCP_DIR}/lib/high/Keccak/FIPS202"
                "${XKCP_DIR}/lib/high/Keccak"
                "${XKCP_DIR}/lib/common"
                "${XKCP_DIR}/lib/low/common"
                "${XKCP_BUILD_CONFIG_DIR}"
            )
            
            # Add platform-specific low-level includes for Keccak-p[1600]
            if(XKCP_PLATFORM STREQUAL "x86-64")
                # x86-64 with runtime dispatch
                list(APPEND XKCP_INCLUDE_DIRS
                    "${XKCP_DIR}/lib/low/x86-64-dispatch"
                    "${XKCP_DIR}/lib/low/KeccakP-1600/plain-64bits"
                    "${XKCP_DIR}/lib/low/KeccakP-1600/AVX2"
                    "${XKCP_DIR}/lib/low/KeccakP-1600/AVX512"
                )
            elseif(XKCP_PLATFORM STREQUAL "ARMv8A")
                list(APPEND XKCP_INCLUDE_DIRS
                    "${XKCP_DIR}/lib/low/KeccakP-1600/ARMv8A"
                )
            elseif(XKCP_PLATFORM STREQUAL "ARMv7A")
                list(APPEND XKCP_INCLUDE_DIRS
                    "${XKCP_DIR}/lib/low/KeccakP-1600/ARMv7A-NEON"
                )
            else()
                # Generic 64-bit
                list(APPEND XKCP_INCLUDE_DIRS
                    "${XKCP_DIR}/lib/low/KeccakP-1600/plain-64bits"
                    "${XKCP_DIR}/lib/low/KeccakP-1600/plain-64bits/SnP"
                )
            endif()
            
            # Create config directory upfront (CMake validates includes at configure time)
            execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${XKCP_BUILD_CONFIG_DIR})
            
            # Set extra compiler flags for optimization
            set(XKCP_EXTRA_FLAGS "")
            include(CheckCCompilerFlag)
            unset(COMPILER_SUPPORTS_MARCH_NATIVE CACHE)
            check_c_compiler_flag(-march=native COMPILER_SUPPORTS_MARCH_NATIVE)
            if(COMPILER_SUPPORTS_MARCH_NATIVE)
                set(XKCP_EXTRA_FLAGS "-march=native -mtune=native")
            endif()
            
            # Create imported library target
            add_library(xkcp_competitor STATIC IMPORTED GLOBAL)
            
            # Custom command to build XKCP using its native make system
            add_custom_command(
                OUTPUT ${XKCP_BUILD_LIB}
                COMMAND ${CMAKE_COMMAND} -E env 
                    CC=${CMAKE_C_COMPILER}
                    AR=${CMAKE_AR}
                    "EXTRA_CFLAGS=${XKCP_EXTRA_FLAGS}"
                    ${MAKE} ${XKCP_MAKE_TARGET}
                WORKING_DIRECTORY ${XKCP_DIR}
                COMMENT "Building XKCP ${XKCP_MAKE_TARGET} with platform optimizations"
                VERBATIM
            )
            
            add_custom_target(BuildXKCP DEPENDS ${XKCP_BUILD_LIB})
            
            set_target_properties(xkcp_competitor PROPERTIES
                IMPORTED_LOCATION ${XKCP_BUILD_LIB}
                INTERFACE_INCLUDE_DIRECTORIES "${XKCP_INCLUDE_DIRS}"
            )
            
            add_dependencies(xkcp_competitor BuildXKCP)
            
            set(HAVE_XKCP TRUE)
        else()
            if(NOT XMLSTYLEPROC)
                message(STATUS "[benchmark_keccak] xsltproc not found - install libxslt package")
            endif()
            if(NOT MAKE)
                message(STATUS "[benchmark_keccak] make/gmake not found")
            endif()
            set(HAVE_XKCP FALSE)
        endif()
    else()
        if(NOT EXISTS "${XKCP_DIR}/Makefile.build")
            message(STATUS "[benchmark_keccak] XKCP not found. Run download_competitors.sh first.")
        elseif(NOT EXISTS "${XKCP_DIR}/support/XKCBuild/src/Main.makefile")
            message(STATUS "[benchmark_keccak] XKCP XKCBuild submodule not found. Run: cd ${XKCP_DIR} && git submodule update --init --recursive")
        endif()
        set(HAVE_XKCP FALSE)
    endif()
    
    # -------------------------------------------------------------------------
    # tiny_sha3 - Minimal SHA3 implementation (simple C, no build system needed)
    # -------------------------------------------------------------------------
    set(TINY_SHA3_DIR "${COMPETITORS_DIR}/tiny_sha3")
    
    if(EXISTS "${TINY_SHA3_DIR}/sha3.c")
        message(STATUS "[benchmark_keccak] Found tiny_sha3 repository")
        set(HAVE_TINY_SHA3 TRUE)
    else()
        message(STATUS "[benchmark_keccak] tiny_sha3 not found. Run download_competitors.sh first.")
        set(HAVE_TINY_SHA3 FALSE)
    endif()
endif()

# =============================================================================
# Benchmark executable
# =============================================================================

# Determine sources based on available competitors
set(BENCHMARK_SOURCES benchmark_keccak.c)

if(BUILD_BENCHMARKS AND HAVE_XKCP AND HAVE_TINY_SHA3)
    add_compile_definitions(BENCHMARK_COMPETITORS)
    list(APPEND BENCHMARK_SOURCES ${TINY_SHA3_DIR}/sha3.c)
    message(STATUS "[benchmark_keccak] All competitors enabled with platform optimizations")
endif()

add_executable(benchmark_keccak ${BENCHMARK_SOURCES})

# Link competitors
if(BUILD_BENCHMARKS AND HAVE_XKCP AND HAVE_TINY_SHA3)
    target_link_libraries(benchmark_keccak xkcp_competitor)
    target_include_directories(benchmark_keccak PRIVATE ${TINY_SHA3_DIR})
endif()

# Link with dap_crypto and its dependencies
# dap_crypto uses dap_json, dap_config, dap_math — OBJECT library deps don't propagate automatically
target_link_libraries(benchmark_keccak
    dap_crypto
    dap_core
    dap_json
    dap_math
    dap_config
)

# System libraries
if(UNIX AND NOT ANDROID)
    target_link_libraries(benchmark_keccak pthread m)
endif()

# =============================================================================
# Test target
# =============================================================================

enable_testing()

add_test(NAME keccak_correctness
    COMMAND benchmark_keccak
)

message(STATUS "[benchmark_keccak] Keccak/SHA3 performance benchmark configured")
