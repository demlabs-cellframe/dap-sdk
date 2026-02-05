cmake_minimum_required(VERSION 3.10)
project (dap_crypto)

# fix implicit declaration warnings
add_definitions ("-D_GNU_SOURCE")

if(NOT DEFINED SPHINCSPLUS_FLEX)
# define sphincs hash params
    # choose one hashtype
    # set(SPHINCS_HASH_TYPE "sphincs-haraka-128f")
    # set(SPHINCS_HASH_TYPE "sphincs-haraka-128s")
    # set(SPHINCS_HASH_TYPE "sphincs-haraka-192f")
    # set(SPHINCS_HASH_TYPE "sphincs-haraka-192s")
    # set(SPHINCS_HASH_TYPE "sphincs-haraka-256f")
    # set(SPHINCS_HASH_TYPE "sphincs-haraka-256s")
    set(SPHINCS_HASH_TYPE "sphincs-sha2-128f")
    # set(SPHINCS_HASH_TYPE "sphincs-sha2-128s")
    # set(SPHINCS_HASH_TYPE "sphincs-sha2-192f")
    # set(SPHINCS_HASH_TYPE "sphincs-sha2-192s")
    # set(SPHINCS_HASH_TYPE "sphincs-sha2-256f")

    # choose one thash
    # set(SPHINCS_THASH "robust")
    set(SPHINCS_THASH "simple")

    add_definitions ("-DPARAMS=${SPHINCS_HASH_TYPE}")
    add_definitions ("-DTHASH=${SPHINCS_THASH}")
    
    file( GLOB SPHINCS_BASE_SRCS
        src/sphincsplus/address.c 
        src/sphincsplus/randombytes.c 
        src/sphincsplus/merkle.c 
        src/sphincsplus/wots.c 
        src/sphincsplus/wotsx1.c 
        src/sphincsplus/utils.c 
        src/sphincsplus/utilsx1.c 
        src/sphincsplus/fors.c 
        src/sphincsplus/sign.c
        src/sphincsplus/sphincsplus_params.c
    )

    file( GLOB SPHINCS_BASE_HEADERS
        src/sphincsplus/params.h 
        src/sphincsplus/address.h 
        src/sphincsplus/randombytes.h 
        src/sphincsplus/merkle.h 
        src/sphincsplus/wots.h 
        src/sphincsplus/wotsx1.h 
        src/sphincsplus/utils.h 
        src/sphincsplus/utilsx1.h 
        src/sphincsplus/fors.h 
        src/sphincsplus/api.h  
        src/sphincsplus/hash.h 
        src/sphincsplus/thash.h
        src/sphincsplus/sphincsplus_params.h
    )

    # add needed files
    if(SPHINCS_HASH_TYPE MATCHES "shake")
        file( GLOB SPHINCS_SRCS
            src/sphincsplus/fips202.c 
            src/sphincsplus/hash_shake.c 
            src/sphincsplus/thash_shake_${SPHINCS_THASH}.c
        )

        file( GLOB SPHINCS_HEADERS
            src/sphincsplus/fips202.h
        )
    elseif(SPHINCS_HASH_TYPE MATCHES "haraka")
        file( GLOB SPHINCS_SRCS
            src/sphincsplus/haraka.c 
            src/sphincsplus/hash_haraka.c 
            src/sphincsplus/thash_haraka_${SPHINCS_THASH}.c
        )

        file( GLOB SPHINCS_HEADERS
            src/sphincsplus/haraka.h
        )
    elseif(SPHINCS_HASH_TYPE MATCHES "sha2")
        file( GLOB SPHINCS_SRCS
            src/sphincsplus/sha2.c 
            src/sphincsplus/hash_sha2.c 
            src/sphincsplus/thash_sha2_${SPHINCS_THASH}.c
        )

        file( GLOB SPHINCS_HEADERS
            src/sphincsplus/sha2.h
        )
    endif()
else()
    add_definitions("-DSPHINCSPLUS_FLEX")
    file( GLOB SPHINCS_SRCS
        src/sphincsplus/*.c
    )
    file( GLOB SPHINCS_HEADERS
        src/sphincsplus/*.h
    )
endif()

file( GLOB CRYPTO_SRCS
      src/*.c
      src/rand/*.c
      src/iaes/*.c
      src/oaes/*.c
      src/blowfish/*.c
      src/GOST/*.c
      src/salsa2012/*.c
      src/seed/*.c
      src/keccak/dap_keccak_ref.c
      src/msrln/*.c
      src/newhope/*.c
      src/sig_bliss/*.c
      src/sig_tesla/*.c
      src/sig_picnic/*.c
      src/sig_dilithium/*.c
      # src/ringct20/*.c # REMOVED: ringct20 module deleted
      src/falcon/codec.c
      src/falcon/common.c
      src/falcon/falcon.c
      src/falcon/fft.c
      src/falcon/fpr.c
      src/falcon/keygen.c
      src/falcon/rng.c
      src/falcon/shake.c
      src/falcon/sign.c
      src/falcon/vrfy.c
      src/chipmunk/*.c
      src/hash/sha2-256/*.c
      ${SPHINCS_BASE_SRCS}
      ${SPHINCS_SRCS}
) 

if (BUILD_WITH_TPS_TEST)
    set (BUILD_WITH_ECDSA ON)
endif()

if (BUILD_WITH_ECDSA)
    file (GLOB ECDSA_SRC src/sig_ecdsa/*.c)
    set (CRYPTO_SRCS ${CRYPTO_SRCS} ${ECDSA_SRC})
    add_definitions("-DDAP_ECDSA")
else()
    list (FILTER CRYPTO_SRCS EXCLUDE REGEX ".*ecdsa.c")
endif()

if (BUILD_WITH_SHIPOVNIK)
    file (GLOB SHIPOVNIK_SRC
        src/sig_shipovnik/*.c
        src/sig_shipovnik/streebog/*.c
    )
    set (CRYPTO_SRCS ${CRYPTO_SRCS} ${SHIPOVNIK_SRC})
    add_definitions("-DDAP_SHIPOVNIK")
else()
    list (FILTER CRYPTO_SRCS EXCLUDE REGEX ".*shipovnik.c")
endif()

get_filename_component(full_path_src ${CMAKE_CURRENT_SOURCE_DIR}/src/sig_shipovnik/streebog/gost3411-2012.c ABSOLUTE)
list(REMOVE_ITEM CRYPTO_SRCS "${full_path_src}")

file( GLOB CRYPTO_HEADERS
      src/*.h
      src/rand/*.h
      src/iaes/*.h
      src/oaes/*.h
      src/blowfish/*.h
      src/GOST/*.h
      src/salsa2012/*.h
      src/seed/*.h
      src/keccak/*.h
      src/msrln/*.h
      src/newhope/*.h
      src/sig_bliss/*.h
      src/sig_tesla/*.h
      src/sig_picnic/*.h
      src/sig_dilithium/*.h
      # src/ringct20/*.h # REMOVED: ringct20 module deleted
      src/falcon/*.h
      src/chipmunk/*.h
      src/hash/sha2-256/*.h
      ${SPHINCS_BASE_HEADERS}
      ${SPHINCS_HEADERS}
      include/*.h
)
if (BUILD_WITH_ECDSA)
    file (GLOB ECDSA_HDR src/sig_ecdsa/*.h)
    set (CRYPTO_HEADERS ${CRYPTO_HEADERS} ${ECDSA_HDR})
else()
    list (FILTER CRYPTO_HEADERS EXCLUDE REGEX ".*ecdsa.h")
endif()

if (BUILD_WITH_SHIPOVNIK)
    file (GLOB SHIPOVNIK_HDR
        src/sig_shipovnik/*.h
        src/sig_shipovnik/streebog/*.h
    )
    set (CRYPTO_HEADERS ${CRYPTO_HEADERS} ${SHIPOVNIK_HDR})
else()
    list (FILTER CRYPTO_HEADERS EXCLUDE REGEX ".*shipovnik.h")
endif()


# NOTE: Warning suppression removed to fix underlying bugs
# Previously suppressed: -Wstringop-overflow, -Warray-parameter

enable_language(ASM)


set( CRYPTO_INCLUDE_PRIVATE src/seed src/rand src/iaes src/oaes src/keccak src/msrln src/sig_bliss src/sig_tesla src/sig_picnic src/sig_dilithium src/falcon src/sig_shipovnik src/sig_shipovnik/streebog src/sphincsplus src/chipmunk src/hash/sha2-256 src include)

# XKCP removed - using native DAP Keccak implementation
# secp256k1 removed - using native DAP ECDSA implementation (sig_ecdsa/)


option (DAP_PQLR "PQRL Intergration" OFF)

if ( DAP_PQLR)
    message("[*] Building with QApp PQLR libraty from path ${DAP_PQLR}")
    IF (POLICY CMP0074) # using PackageName_ROOT as prefix
        CMAKE_POLICY(SET CMP0074 NEW)
    ENDIF()
    FIND_PACKAGE(PQLR REQUIRED)
    message ("[ ] Include dir: ${PQLR_INCLUDE_DIR}")
    INCLUDE_DIRECTORIES(${PQLR_INCLUDE_DIR})

    file(GLOB DAP_PQLR_SRC src/pqlr/*.c src/pqlr/*.h)
    #set(CRYPTO_INCLUDE_PRIVATE ${CRYPTO_INCLUDE_PRIVATE} ${DAP_PQRL}/include )
    add_definitions("-DDAP_PQLR")
endif()


# =============================================================================
# Keccak SIMD optimizations - Code Generation
# Generated files go to build directory to keep source tree clean
# =============================================================================

set(KECCAK_SIMD_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/keccak_simd_gen")
file(MAKE_DIRECTORY ${KECCAK_SIMD_GEN_DIR})

message(STATUS "[dap_crypto] Generating Keccak SIMD implementations from templates...")
message(STATUS "[dap_crypto] Output directory: ${KECCAK_SIMD_GEN_DIR}")

# Set up environment for code generation
set(CODEGEN_ENV
    "CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}"
    "DAP_TPL_DIR=${CMAKE_SOURCE_DIR}/module/test/dap_tpl"
)

# Run code generation at configure time
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env ${CODEGEN_ENV}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/keccak/generate_simd.sh ${KECCAK_SIMD_GEN_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/keccak
    RESULT_VARIABLE KECCAK_CODEGEN_RESULT
    OUTPUT_VARIABLE KECCAK_CODEGEN_OUTPUT
    ERROR_VARIABLE KECCAK_CODEGEN_ERROR
)

if(NOT KECCAK_CODEGEN_RESULT EQUAL 0)
    message(WARNING "[dap_crypto] Keccak SIMD code generation failed:")
    message(WARNING "  Exit code: ${KECCAK_CODEGEN_RESULT}")
    message(WARNING "  Output: ${KECCAK_CODEGEN_OUTPUT}")
    message(WARNING "  Error: ${KECCAK_CODEGEN_ERROR}")
    message(WARNING "  Only reference implementation will be available")
else()
    message(STATUS "[dap_crypto] Keccak SIMD code generation successful")
endif()

# Collect generated SIMD sources and set compiler flags
set(KECCAK_SIMD_SRCS "")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686|i386")
    # x86/x64: AVX-512, AVX2, SSE2 implementations (generated)
    set(KECCAK_AVX512_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_avx512.c)
    set(KECCAK_AVX2_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_avx2.c)
    set(KECCAK_SSE2_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_sse2.c)
    
    if(EXISTS ${KECCAK_AVX512_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_AVX512_SRC})
        set_source_files_properties(${KECCAK_AVX512_SRC} PROPERTIES 
            COMPILE_FLAGS "-mavx512f -mavx512dq -mavx512bw -mavx512vl"
            GENERATED TRUE)
    endif()
    
    if(EXISTS ${KECCAK_AVX2_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_AVX2_SRC})
        set_source_files_properties(${KECCAK_AVX2_SRC} PROPERTIES 
            COMPILE_FLAGS "-mavx2 -mbmi -mbmi2"
            GENERATED TRUE)
    endif()
    
    if(EXISTS ${KECCAK_SSE2_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_SSE2_SRC})
        set_source_files_properties(${KECCAK_SSE2_SRC} PROPERTIES 
            COMPILE_FLAGS "-msse2"
            GENERATED TRUE)
    endif()
    
    message(STATUS "[dap_crypto] Keccak SIMD: AVX-512, AVX2, SSE2 enabled (generated)")
    
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    # ARM64: NEON, SVE, SVE2 implementations (generated)
    set(KECCAK_NEON_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_neon.c)
    set(KECCAK_SVE_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_sve.c)
    set(KECCAK_SVE2_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_sve2.c)
    
    if(EXISTS ${KECCAK_NEON_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_NEON_SRC})
        set_source_files_properties(${KECCAK_NEON_SRC} PROPERTIES GENERATED TRUE)
    endif()
    
    if(EXISTS ${KECCAK_SVE_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_SVE_SRC})
        set_source_files_properties(${KECCAK_SVE_SRC} PROPERTIES 
            COMPILE_FLAGS "-march=armv8.2-a+sve"
            GENERATED TRUE)
    endif()
    
    if(EXISTS ${KECCAK_SVE2_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_SVE2_SRC})
        set_source_files_properties(${KECCAK_SVE2_SRC} PROPERTIES 
            COMPILE_FLAGS "-march=armv8.2-a+sve2"
            GENERATED TRUE)
    endif()
    
    message(STATUS "[dap_crypto] Keccak SIMD: NEON, SVE, SVE2 enabled (generated)")
    
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
    # ARM32: NEON implementation (generated)
    set(KECCAK_NEON_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_neon.c)
    
    if(EXISTS ${KECCAK_NEON_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_NEON_SRC})
        set_source_files_properties(${KECCAK_NEON_SRC} PROPERTIES 
            COMPILE_FLAGS "-mfpu=neon"
            GENERATED TRUE)
    endif()
    
    message(STATUS "[dap_crypto] Keccak SIMD: NEON enabled (generated, ARM32)")
endif()

# Add generated SIMD sources to main crypto sources
set(CRYPTO_SRCS ${CRYPTO_SRCS} ${KECCAK_SIMD_SRCS})

# Add include path for generated headers (if any)
include_directories(${KECCAK_SIMD_GEN_DIR})

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${CRYPTO_SRCS} HEADERS ${CRYPTO_HEADERS} ${DAP_PQLR_SRC})

## Get all properties that cmake supports
execute_process(COMMAND cmake --help-property-list OUTPUT_VARIABLE CMAKE_PROPERTY_LIST)
## Convert command output into a CMake list
STRING(REGEX REPLACE ";" "\\\\;" CMAKE_PROPERTY_LIST "${CMAKE_PROPERTY_LIST}")
STRING(REGEX REPLACE "\n" ";" CMAKE_PROPERTY_LIST "${CMAKE_PROPERTY_LIST}")

list(REMOVE_DUPLICATES CMAKE_PROPERTY_LIST)

function(print_target_properties tgt)
    if(NOT TARGET ${tgt})
      message("There is no target named '${tgt}'")
      return()
    endif()

    foreach (prop ${CMAKE_PROPERTY_LIST})
        string(REPLACE "<CONFIG>" "${CMAKE_BUILD_TYPE}" prop ${prop})
        get_target_property(propval ${tgt} ${prop})
        if (propval)
            message ("${tgt} ${prop} = ${propval}")
        endif()
    endforeach(prop)
endfunction(print_target_properties)

option(BUILD_DAP_CRYPTO_KYBER512 "Build Kyber512 crypto module" ON) # default KEM for assymetric crypto
if(BUILD_DAP_CRYPTO_KYBER512)
    add_subdirectory(src/Kyber/crypto_kem/kyber512/optimized kyber512_build)
    add_dependencies(${PROJECT_NAME} dap_crypto_kyber512)
    target_link_libraries(${PROJECT_NAME} dap_crypto_kyber512)
    message("[+] DAP Crypto: Kyber512 support enabled")
else()
    message("[!] DAP Crypto: Kyber512 support disabled ")
endif()

if (DAP_PQRL)

    TARGET_LINK_LIBRARIES(dap_crypto ${PQLR_DILITHIUM_LIBRARIES})

    IF(WIN32)
        ADD_CUSTOM_COMMAND(TARGET dap_crypto POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${PQLR_BINARY_DIR}/pqlr.dll"
            "$<TARGET_FILE_DIR:dap_crypto>")
    ENDIF()
endif()


target_include_directories(dap_crypto PRIVATE ${CRYPTO_INCLUDE_PRIVATE})
target_include_directories(dap_crypto PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rand
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)






# Store dependencies for final library - no linking of OBJECT targets  
set(CRYPTO_LIBS "dap_crypto_kyber512")
# secp256k1 removed - using native DAP ECDSA implementation
if(UNIX)
    list(APPEND CRYPTO_LIBS "m")
endif()

target_link_libraries(dap_crypto dap_core dap_json)

if(UNIX)
  target_link_libraries(dap_crypto m)
endif()

# Export dependencies for main SDK build
set(DAP_CRYPTO_DEPS "${CRYPTO_LIBS}" CACHE INTERNAL "dap_crypto external dependencies")

# shishua moved from 3rdparty to src/rand/shishua (native implementation)
target_include_directories(dap_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/rand/shishua)
target_include_directories(dap_crypto PUBLIC src/Kyber/crypto_kem/kyber512/optimized)
# Native ECDSA implementation in sig_ecdsa/ - no external includes needed

if(BUILD_CRYPTO_TESTS)
    enable_testing()
    add_subdirectory(test)
    add_definitions("-DDAP_CRYPTO_TESTS")
endif()

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${CRYPTO_HEADERS}")

INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/crypto/
        ARCHIVE DESTINATION lib/dap/crypto/
        PUBLIC_HEADER DESTINATION include/dap/crypto/
)

INSTALL(DIRECTORY src/rand/ DESTINATION include/dap/crypto/rand/)
INSTALL(DIRECTORY src/msrln/ DESTINATION include/dap/crypto/msrln/)
INSTALL(DIRECTORY src/sig_dilithium/ DESTINATION include/dap/crypto/sig_dilithium/)
endif()
