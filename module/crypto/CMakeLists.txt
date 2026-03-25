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
        src/sig/sphincsplus/address.c 
        src/sig/sphincsplus/randombytes.c 
        src/sig/sphincsplus/merkle.c 
        src/sig/sphincsplus/wots.c 
        src/sig/sphincsplus/wotsx1.c 
        src/sig/sphincsplus/utils.c 
        src/sig/sphincsplus/utilsx1.c 
        src/sig/sphincsplus/fors.c 
        src/sig/sphincsplus/sign.c
        src/sig/sphincsplus/sphincsplus_params.c
    )

    file( GLOB SPHINCS_BASE_HEADERS
        src/sig/sphincsplus/params.h 
        src/sig/sphincsplus/address.h 
        src/sig/sphincsplus/randombytes.h 
        src/sig/sphincsplus/merkle.h 
        src/sig/sphincsplus/wots.h 
        src/sig/sphincsplus/wotsx1.h 
        src/sig/sphincsplus/utils.h 
        src/sig/sphincsplus/utilsx1.h 
        src/sig/sphincsplus/fors.h 
        src/sig/sphincsplus/api.h  
        src/sig/sphincsplus/hash.h 
        src/sig/sphincsplus/thash.h
        src/sig/sphincsplus/sphincsplus_params.h
    )

    # add needed files
    if(SPHINCS_HASH_TYPE MATCHES "shake")
        file( GLOB SPHINCS_SRCS
            src/sig/sphincsplus/spx_fips202.c
            src/sig/sphincsplus/hash_shake.c 
            src/sig/sphincsplus/thash_shake_${SPHINCS_THASH}.c
        )

        file( GLOB SPHINCS_HEADERS
            src/sig/sphincsplus/spx_fips202.h
        )
    elseif(SPHINCS_HASH_TYPE MATCHES "haraka")
        file( GLOB SPHINCS_SRCS
            src/sig/sphincsplus/haraka.c 
            src/sig/sphincsplus/hash_haraka.c 
            src/sig/sphincsplus/thash_haraka_${SPHINCS_THASH}.c
        )

        file( GLOB SPHINCS_HEADERS
            src/sig/sphincsplus/haraka.h
        )
    elseif(SPHINCS_HASH_TYPE MATCHES "sha2")
        file( GLOB SPHINCS_SRCS
            src/sig/sphincsplus/sha2.c 
            src/sig/sphincsplus/hash_sha2.c 
            src/sig/sphincsplus/thash_sha2_${SPHINCS_THASH}.c
        )

        file( GLOB SPHINCS_HEADERS
            src/sig/sphincsplus/sha2.h
        )
    endif()
else()
    add_definitions("-DSPHINCSPLUS_FLEX")
    file( GLOB SPHINCS_SRCS
        src/sig/sphincsplus/*.c
    )
    file( GLOB SPHINCS_HEADERS
        src/sig/sphincsplus/*.h
    )
endif()

option(BUILD_DEPRECATED_CRYPTO "Build deprecated cryptographic algorithms (Bliss, Picnic, Tesla, NewHope, MSRLN, Salsa2012)" ON)
option(BUILD_CLASSIC_CRYPTO "Build classic (non-PQ) cryptographic algorithms (GOST/Kuznyechik, Shipovnik, ECDSA)" ON)

file( GLOB CRYPTO_SRCS
      src/*.c
      # --- sig (active PQ) ---
      src/sig/dilithium/*.c
      src/sig/falcon/codec.c
      src/sig/falcon/common.c
      src/sig/falcon/dap_enc_falcon.c
      src/sig/falcon/falcon.c
      src/sig/falcon/fft.c
      src/sig/falcon/fpr.c
      src/sig/falcon/keygen.c
      src/sig/falcon/rng.c
      src/sig/falcon/shake.c
      src/sig/falcon/sign.c
      src/sig/falcon/vrfy.c
      src/sig/chipmunk/*.c
      src/sig/ntru_prime/*.c
      # --- sym (active) ---
      src/sym/aes/*.c
      src/sym/seed/*.c
      src/sym/chacha20/*.c
      # --- kem (active) ---
      src/kem/kyber/dap_enc_kyber.c
      src/kem/mlkem/dap_enc_mlkem.c
      src/kem/mlkem/dap_mlkem_batch.c
      src/kem/ntru_prime/*.c
      # --- sphincsplus ---
      src/sig/sphincsplus/dap_enc_sphincsplus.c
      ${SPHINCS_BASE_SRCS}
      ${SPHINCS_SRCS}
)

# Deprecated algorithms (OFF by default — legacy, not recommended for new code)
if(BUILD_DEPRECATED_CRYPTO)
    file(GLOB DEPRECATED_SRCS
        src/sig/deprecated/bliss/*.c
        src/sig/deprecated/tesla/*.c
        src/sig/deprecated/picnic/*.c
        src/kem/deprecated/msrln/*.c
        src/kem/deprecated/newhope/*.c
        src/sym/deprecated/salsa2012/*.c
    )
    set(CRYPTO_SRCS ${CRYPTO_SRCS} ${DEPRECATED_SRCS})
    message("[+] DAP Crypto: Deprecated algorithms enabled")
endif()

# Classic (non-PQ) algorithms (ON by default)
if(BUILD_CLASSIC_CRYPTO)
    file(GLOB CLASSIC_SRCS
        src/sym/classic/gost/*.c
    )
    set(CRYPTO_SRCS ${CRYPTO_SRCS} ${CLASSIC_SRCS})
    message("[+] DAP Crypto: Classic algorithms enabled (GOST)")
endif()

# ECDSA is now native implementation, enable by default
option(BUILD_WITH_ECDSA "Build with native secp256k1/ECDSA support" ON)

if (BUILD_WITH_TPS_TEST)
    set (BUILD_WITH_ECDSA ON)
endif()

if (BUILD_WITH_ECDSA)
    # =========================================================================
    # ECDSA - Native secp256k1 implementation
    # Scalar multiplication implementations generated from template:
    #   - ecdsa_scalar_mul_generic.c     - Portable C (__uint128_t)
    #   - ecdsa_scalar_mul_x86_64_asm.c  - x86-64 inline assembly (MULQ)
    #   - ecdsa_scalar_mul_avx2_bmi2.c   - AVX2+BMI2 (MULX/ADCX/ADOX)
    #   - ecdsa_scalar_mul_neon.c        - ARM64 NEON (MUL/UMULH)
    # =========================================================================
    
    # Generate arch-specific implementations and collect all ECDSA sources
    add_subdirectory(src/sig/classic/ecdsa)
    
    # Add all ECDSA sources (static + generated)
    set (CRYPTO_SRCS ${CRYPTO_SRCS} ${ECDSA_SOURCES})
    
    # SVE files need -march=armv8.2-a+sve for GCC 10 compatibility.
    # Must be set here (parent scope) since dap_crypto target is built in this directory.
    # Skip on Apple: Apple Silicon doesn't support SVE, and the flag breaks universal builds.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64" AND NOT APPLE)
        set_source_files_properties(
            ${ECDSA_GEN_INCLUDE_DIR}/ecdsa_scalar_mul_sve.c
            ${ECDSA_GEN_INCLUDE_DIR}/ecdsa_field_sve.c
            PROPERTIES COMPILE_FLAGS "-march=armv8.2-a+sve"
        )
    endif()
    add_definitions("-DDAP_ECDSA")
    
    # Include directories for generated files
    include_directories(${ECDSA_GEN_INCLUDE_DIR})
    
    message(STATUS "[ECDSA] Native secp256k1 with arch-optimized scalar arithmetic")
else()
    list (FILTER CRYPTO_SRCS EXCLUDE REGEX ".*ecdsa.c")
endif()

if (BUILD_WITH_SHIPOVNIK)
    file (GLOB SHIPOVNIK_SRC
        src/sig/classic/shipovnik/*.c
        src/sig/classic/shipovnik/streebog/*.c
    )
    set (CRYPTO_SRCS ${CRYPTO_SRCS} ${SHIPOVNIK_SRC})
    add_definitions("-DDAP_SHIPOVNIK")
else()
    list (FILTER CRYPTO_SRCS EXCLUDE REGEX ".*shipovnik.c")
endif()

get_filename_component(full_path_src ${CMAKE_CURRENT_SOURCE_DIR}/src/sig/classic/shipovnik/streebog/gost3411-2012.c ABSOLUTE)
list(REMOVE_ITEM CRYPTO_SRCS "${full_path_src}")

file( GLOB CRYPTO_HEADERS
      src/*.h
      src/sig/dilithium/*.h
      src/sig/falcon/*.h
      src/sig/chipmunk/*.h
      src/sym/aes/*.h
      src/sym/seed/*.h
      src/sym/chacha20/*.h
      ${SPHINCS_BASE_HEADERS}
      ${SPHINCS_HEADERS}
      include/*.h
)

if(BUILD_DEPRECATED_CRYPTO)
    file(GLOB DEPRECATED_HDRS
        src/sig/deprecated/bliss/*.h
        src/sig/deprecated/tesla/*.h
        src/sig/deprecated/picnic/*.h
        src/kem/deprecated/msrln/*.h
        src/kem/deprecated/newhope/*.h
        src/sym/deprecated/salsa2012/*.h
    )
    set(CRYPTO_HEADERS ${CRYPTO_HEADERS} ${DEPRECATED_HDRS})
endif()

if(BUILD_CLASSIC_CRYPTO)
    file(GLOB CLASSIC_HDRS
        src/sym/classic/gost/*.h
    )
    set(CRYPTO_HEADERS ${CRYPTO_HEADERS} ${CLASSIC_HDRS})
endif()
if (BUILD_WITH_ECDSA)
    file (GLOB ECDSA_HDR src/sig/classic/ecdsa/*.h)
    set (CRYPTO_HEADERS ${CRYPTO_HEADERS} ${ECDSA_HDR})
else()
    list (FILTER CRYPTO_HEADERS EXCLUDE REGEX ".*ecdsa.h")
endif()

if (BUILD_WITH_SHIPOVNIK)
    file (GLOB SHIPOVNIK_HDR
        src/sig/classic/shipovnik/*.h
        src/sig/classic/shipovnik/streebog/*.h
    )
    set (CRYPTO_HEADERS ${CRYPTO_HEADERS} ${SHIPOVNIK_HDR})
else()
    list (FILTER CRYPTO_HEADERS EXCLUDE REGEX ".*shipovnik.h")
endif()


# NOTE: Warning suppression removed to fix underlying bugs
# Previously suppressed: -Wstringop-overflow, -Warray-parameter

enable_language(ASM)


set( CRYPTO_INCLUDE_PRIVATE
    src/sig src/sig/dilithium src/sig/falcon src/sig/chipmunk src/sig/ntru_prime src/sig/sphincsplus
    src/sig/classic/ecdsa src/sig/classic/ecdsa/arch
    src/sym src/sym/aes src/sym/seed src/sym/chacha20
    src/kem src/kem/mlkem src/kem/ntru_prime
    src include
)

if(BUILD_DEPRECATED_CRYPTO)
    list(APPEND CRYPTO_INCLUDE_PRIVATE
        src/sig/deprecated src/sig/deprecated/bliss src/sig/deprecated/tesla src/sig/deprecated/picnic
        src/kem/deprecated src/kem/deprecated/msrln src/kem/deprecated/newhope
        src/sym/deprecated src/sym/deprecated/salsa2012
    )
endif()

if(BUILD_CLASSIC_CRYPTO)
    list(APPEND CRYPTO_INCLUDE_PRIVATE src/sym/classic/gost)
endif()

if(BUILD_WITH_SHIPOVNIK)
    list(APPEND CRYPTO_INCLUDE_PRIVATE src/sig/classic/shipovnik src/sig/classic/shipovnik/streebog)
endif()

# XKCP removed - using native DAP Keccak implementation (now in module/hash)
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

    file(GLOB DAP_PQLR_SRC src/deprecated/pqlr/*.c src/deprecated/pqlr/*.h)
    #set(CRYPTO_INCLUDE_PRIVATE ${CRYPTO_INCLUDE_PRIVATE} ${DAP_PQRL}/include )
    add_definitions("-DDAP_PQLR")
endif()


# Keccak SIMD optimizations moved to module/hash

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${CRYPTO_SRCS} HEADERS ${CRYPTO_HEADERS} ${DAP_PQLR_SRC})

# --- AES hardware acceleration compile flags ---
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686|i386")
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sym/aes/dap_aes_ni.c
        PROPERTIES COMPILE_FLAGS "-maes -msse2"
    )
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sym/aes/dap_aes_armce.c
        PROPERTIES COMPILE_FLAGS "-march=armv8-a+crypto"
    )
endif()

# --- Dilithium specialized SIMD ---
set(DIL_TPL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/sig/dilithium)
set(DIL_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/dilithium_ntt_gen)
set(DIL_REDUCE_TPL "${DIL_TPL_DIR}/dap_dilithium_simd_reduce.tpl")

# Per-variant NTT generation: each arch gets its own NTT_INNER_FILE (Keccak pattern)
dap_arch_generate_variant(
    TEMPLATE ${DIL_TPL_DIR}/dap_dilithium_ntt_simd.c.tpl
    OUTPUT   ${DIL_GEN_DIR}/dap_dilithium_ntt_avx2.c
    ARCH     avx2
    ARGS     "PRIMITIVES_FILE=${DAP_PRIMITIVES_DIR}/x86/int32_avx2.tpl"
             "NTT_INNER_FILE=${DIL_TPL_DIR}/arch/x86/ntt_inner_avx2.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${DIL_TPL_DIR}/dap_dilithium_ntt_simd.c.tpl
    OUTPUT   ${DIL_GEN_DIR}/dap_dilithium_ntt_avx2_512vl.c
    ARCH     avx2_512vl
    ARGS     "PRIMITIVES_FILE=${DAP_PRIMITIVES_DIR}/x86/int32_avx2.tpl"
             "NTT_INNER_FILE=${DIL_TPL_DIR}/arch/x86/ntt_inner_avx2.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${DIL_TPL_DIR}/dap_dilithium_ntt_simd.c.tpl
    OUTPUT   ${DIL_GEN_DIR}/dap_dilithium_ntt_avx512.c
    ARCH     avx512
    ARGS     "PRIMITIVES_FILE=${DAP_PRIMITIVES_DIR}/x86/int32_avx512.tpl"
             "NTT_INNER_FILE=${DIL_TPL_DIR}/arch/x86/ntt_inner_avx512.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${DIL_TPL_DIR}/dap_dilithium_ntt_simd.c.tpl
    OUTPUT   ${DIL_GEN_DIR}/dap_dilithium_ntt_neon.c
    ARCH     neon
    ARGS     "PRIMITIVES_FILE=${DAP_PRIMITIVES_DIR}/arm/int32_neon.tpl"
             "NTT_INNER_FILE=${DIL_TPL_DIR}/arch/generic/ntt_inner_scalar.tpl"
)
dap_arch_generate(
    TEMPLATE   ${DIL_TPL_DIR}/dap_dilithium_poly_simd.c.tpl
    OUTPUT_DIR ${DIL_GEN_DIR}
    PREFIX     dap_dilithium_poly
    VARIANTS
        avx2         "${DAP_PRIMITIVES_DIR}/x86/int32_avx2.tpl"
        avx512       "${DAP_PRIMITIVES_DIR}/x86/int32_avx512.tpl"
        neon         "${DAP_PRIMITIVES_DIR}/arm/int32_neon.tpl"
    EXTRA_ARGS "REDUCE_FILE=${DIL_REDUCE_TPL}"
               "OPS_FILE=${DIL_TPL_DIR}/dap_dilithium_simd_poly_ops.tpl"
)
# Per-variant rejection sampling generation
dap_arch_generate_variant(
    TEMPLATE ${DIL_TPL_DIR}/dap_dilithium_sample_simd.c.tpl
    OUTPUT   ${DIL_GEN_DIR}/dap_dilithium_sample_avx2.c
    ARCH     avx2
    ARGS     "SAMPLE_BODY=${DIL_TPL_DIR}/arch/x86/rej_uniform_avx2.inc"
)
dap_arch_generate_variant(
    TEMPLATE ${DIL_TPL_DIR}/dap_dilithium_sample_simd.c.tpl
    OUTPUT   ${DIL_GEN_DIR}/dap_dilithium_sample_neon.c
    ARCH     neon
    ARGS     "SAMPLE_BODY=${DIL_TPL_DIR}/arch/arm/rej_uniform_neon.inc"
)
dap_arch_add_sources(TARGET ${PROJECT_NAME} OUTPUT_DIR ${DIL_GEN_DIR}
    SOURCES
        dap_dilithium_ntt_avx2.c        avx2
        dap_dilithium_ntt_avx2_512vl.c  avx2_512vl
        dap_dilithium_ntt_avx512.c      avx512
        dap_dilithium_ntt_neon.c        neon
        dap_dilithium_poly_avx2.c       avx2
        dap_dilithium_poly_avx512.c     avx512
        dap_dilithium_poly_neon.c       neon
        dap_dilithium_sample_avx2.c     avx2
        dap_dilithium_sample_neon.c     neon
)
target_include_directories(${PROJECT_NAME} PRIVATE ${DIL_GEN_DIR})

# --- ChaCha20 multi-block SIMD (Keccak-pattern layout) ---
set(CHACHA_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/src/sym/chacha20)
set(CHACHA_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/chacha20_arch_gen)

dap_arch_generate_variant(
    TEMPLATE ${CHACHA_DIR}/dap_chacha20_simd.c.tpl
    OUTPUT   ${CHACHA_GEN_DIR}/dap_chacha20_sse2.c
    ARCH     sse2
    ARGS     "PRIMITIVES_FILE=${CHACHA_DIR}/arch/x86/sse2_primitives.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${CHACHA_DIR}/dap_chacha20_simd.c.tpl
    OUTPUT   ${CHACHA_GEN_DIR}/dap_chacha20_avx2.c
    ARCH     avx2
    ARGS     "PRIMITIVES_FILE=${CHACHA_DIR}/arch/x86/avx2_primitives.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${CHACHA_DIR}/dap_chacha20_simd.c.tpl
    OUTPUT   ${CHACHA_GEN_DIR}/dap_chacha20_avx2_512vl.c
    ARCH     avx2_512vl
    ARGS     "PRIMITIVES_FILE=${CHACHA_DIR}/arch/x86/avx512vl_primitives.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${CHACHA_DIR}/dap_chacha20_simd.c.tpl
    OUTPUT   ${CHACHA_GEN_DIR}/dap_chacha20_avx512.c
    ARCH     avx512
    ARGS     "PRIMITIVES_FILE=${CHACHA_DIR}/arch/x86/avx512_primitives.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${CHACHA_DIR}/dap_chacha20_simd.c.tpl
    OUTPUT   ${CHACHA_GEN_DIR}/dap_chacha20_neon.c
    ARCH     neon
    ARGS     "PRIMITIVES_FILE=${CHACHA_DIR}/arch/arm/neon_primitives.tpl"
)
dap_arch_add_sources(TARGET ${PROJECT_NAME} OUTPUT_DIR ${CHACHA_GEN_DIR}
    SOURCES
        dap_chacha20_sse2.c  sse2
        dap_chacha20_avx2.c  avx2
        dap_chacha20_avx2_512vl.c  avx2_512vl
        dap_chacha20_avx512.c  avx512
        dap_chacha20_neon.c  neon
)
target_include_directories(${PROJECT_NAME} PRIVATE ${CHACHA_GEN_DIR})

# --- ChaCha20 hand-tuned assembly — shared framework + arch impl (Keccak pattern) ---
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    enable_language(ASM)
    dap_tpl_generate(
        TEMPLATE ${CHACHA_DIR}/dap_chacha20_asm.S.tpl
        OUTPUT   ${CHACHA_GEN_DIR}/dap_chacha20_avx512vl.S
        ARGS     "ARCH_IMPL=${CHACHA_DIR}/arch/x86/chacha20_asm_avx512vl.tpl"
    )
    target_sources(${PROJECT_NAME} PRIVATE ${CHACHA_GEN_DIR}/dap_chacha20_avx512vl.S)
    set_source_files_properties(${CHACHA_GEN_DIR}/dap_chacha20_avx512vl.S
        PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512vl"
    )
endif()

# --- Poly1305 multi-block SIMD ---
set(POLY1305_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/poly1305_arch_gen)

dap_arch_generate_variant(
    TEMPLATE ${CHACHA_DIR}/dap_poly1305_simd.c.tpl
    OUTPUT   ${POLY1305_GEN_DIR}/dap_poly1305_avx2.c
    ARCH     avx2
    ARGS     "IMPL_FILE=${CHACHA_DIR}/arch/x86/avx2_poly1305_impl.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${CHACHA_DIR}/dap_poly1305_simd.c.tpl
    OUTPUT   ${POLY1305_GEN_DIR}/dap_poly1305_avx512_ifma.c
    ARCH     avx512_ifma
    ARGS     "IMPL_FILE=${CHACHA_DIR}/arch/x86/avx512_ifma_poly1305_impl.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${CHACHA_DIR}/dap_poly1305_simd.c.tpl
    OUTPUT   ${POLY1305_GEN_DIR}/dap_poly1305_neon.c
    ARCH     neon
    ARGS     "IMPL_FILE=${CHACHA_DIR}/arch/arm/neon_poly1305_impl.tpl"
)
dap_arch_add_sources(TARGET ${PROJECT_NAME} OUTPUT_DIR ${POLY1305_GEN_DIR}
    SOURCES
        dap_poly1305_avx2.c  avx2
        dap_poly1305_avx512_ifma.c  avx512_ifma
        dap_poly1305_neon.c  neon
)
target_include_directories(${PROJECT_NAME} PRIVATE ${POLY1305_GEN_DIR})

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

# --- ML-KEM (FIPS 203) — single codebase compiled 3× with MLKEM_K=2/3/4
add_subdirectory(src/kem/mlkem mlkem_build)
add_dependencies(${PROJECT_NAME} dap_mlkem_512 dap_mlkem_768 dap_mlkem_1024)
target_link_libraries(${PROJECT_NAME} INTERFACE dap_mlkem_512 dap_mlkem_768 dap_mlkem_1024)
message("[+] DAP Crypto: ML-KEM-512/768/1024 support enabled")

if (DAP_PQRL)

    TARGET_LINK_LIBRARIES(dap_crypto INTERFACE ${PQLR_DILITHIUM_LIBRARIES})

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
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CRYPTO_INCLUDE_PRIVATE}
)






# Store dependencies for final library - no linking of OBJECT targets  
set(CRYPTO_LIBS "dap_mlkem_512" "dap_mlkem_768" "dap_mlkem_1024")
# secp256k1 removed - using native DAP ECDSA implementation
if(UNIX)
    list(APPEND CRYPTO_LIBS "m")
endif()

dap_link_libraries(dap_crypto INTERFACE dap_core dap_json dap_hash dap_hash_table dap_config dap_math dap_math_bignum)

if(TARGET dap_gpu)
    dap_link_libraries(dap_crypto INTERFACE dap_gpu)
    target_compile_definitions(dap_crypto PUBLIC DAP_HAS_GPU)
endif()

if(UNIX)
  target_link_libraries(dap_crypto INTERFACE m)
endif()

# Export dependencies for main SDK build
set(DAP_CRYPTO_DEPS "${CRYPTO_LIBS}" CACHE INTERNAL "dap_crypto external dependencies")

target_include_directories(dap_crypto PUBLIC src/kem/mlkem)
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

INSTALL(DIRECTORY src/sig/dilithium/ DESTINATION include/dap/crypto/sig/dilithium/)
endif()
