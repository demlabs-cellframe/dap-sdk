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
      src/kem/kyber/dap_enc_mlkem.c
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
    src/kem src/kem/ntru_prime
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

# --- ChaCha20 multi-block SIMD ---
set(CHACHA_SIMD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/sym/chacha20/simd)
set(CHACHA_GEN_DIR  ${CMAKE_CURRENT_BINARY_DIR}/chacha20_arch_gen)

dap_arch_generate_variant(
    TEMPLATE ${CHACHA_SIMD_DIR}/dap_chacha20_simd.c.tpl
    OUTPUT   ${CHACHA_GEN_DIR}/dap_chacha20_sse2.c
    ARCH     sse2
    ARGS     "PRIMITIVES_FILE=${CHACHA_SIMD_DIR}/arch/x86/sse2_primitives.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${CHACHA_SIMD_DIR}/dap_chacha20_simd.c.tpl
    OUTPUT   ${CHACHA_GEN_DIR}/dap_chacha20_avx2.c
    ARCH     avx2
    ARGS     "PRIMITIVES_FILE=${CHACHA_SIMD_DIR}/arch/x86/avx2_primitives.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${CHACHA_SIMD_DIR}/dap_chacha20_simd.c.tpl
    OUTPUT   ${CHACHA_GEN_DIR}/dap_chacha20_neon.c
    ARCH     neon
    ARGS     "PRIMITIVES_FILE=${CHACHA_SIMD_DIR}/arch/arm/neon_primitives.tpl"
)
dap_arch_add_sources(TARGET ${PROJECT_NAME} OUTPUT_DIR ${CHACHA_GEN_DIR}
    SOURCES
        dap_chacha20_sse2.c  sse2
        dap_chacha20_avx2.c  avx2
        dap_chacha20_neon.c  neon
)
target_include_directories(${PROJECT_NAME} PRIVATE ${CHACHA_GEN_DIR})

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

option(BUILD_DAP_CRYPTO_KYBER512 "Build Kyber512 crypto module" ON)
if(BUILD_DAP_CRYPTO_KYBER512)
    add_subdirectory(src/kem/kyber/crypto_kem/kyber512/optimized kyber512_build)
    add_dependencies(${PROJECT_NAME} dap_crypto_kyber512)
    target_link_libraries(${PROJECT_NAME} dap_crypto_kyber512)
    message("[+] DAP Crypto: Kyber512 support enabled")
else()
    message("[!] DAP Crypto: Kyber512 support disabled ")
endif()

option(BUILD_DAP_CRYPTO_KYBER768 "Build Kyber768 crypto module" ON)
if(BUILD_DAP_CRYPTO_KYBER768)
    add_subdirectory(src/kem/kyber/crypto_kem/kyber768/optimized kyber768_build)
    add_dependencies(${PROJECT_NAME} dap_crypto_kyber768)
    target_link_libraries(${PROJECT_NAME} dap_crypto_kyber768)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DAP_KYBER768=1)
    message("[+] DAP Crypto: Kyber768 support enabled")
endif()

option(BUILD_DAP_CRYPTO_KYBER1024 "Build Kyber1024 crypto module" ON)
if(BUILD_DAP_CRYPTO_KYBER1024)
    add_subdirectory(src/kem/kyber/crypto_kem/kyber1024/optimized kyber1024_build)
    add_dependencies(${PROJECT_NAME} dap_crypto_kyber1024)
    target_link_libraries(${PROJECT_NAME} dap_crypto_kyber1024)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DAP_KYBER1024=1)
    message("[+] DAP Crypto: Kyber1024 support enabled")
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
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CRYPTO_INCLUDE_PRIVATE}
)






# Store dependencies for final library - no linking of OBJECT targets  
set(CRYPTO_LIBS "dap_crypto_kyber512")
if(BUILD_DAP_CRYPTO_KYBER768)
    list(APPEND CRYPTO_LIBS "dap_crypto_kyber768")
endif()
if(BUILD_DAP_CRYPTO_KYBER1024)
    list(APPEND CRYPTO_LIBS "dap_crypto_kyber1024")
endif()
# secp256k1 removed - using native DAP ECDSA implementation
if(UNIX)
    list(APPEND CRYPTO_LIBS "m")
endif()

target_link_libraries(dap_crypto dap_core dap_json dap_hash dap_hash_table dap_config dap_math_bignum)

if(UNIX)
  target_link_libraries(dap_crypto m)
endif()

# Export dependencies for main SDK build
set(DAP_CRYPTO_DEPS "${CRYPTO_LIBS}" CACHE INTERNAL "dap_crypto external dependencies")

target_include_directories(dap_crypto PUBLIC src/kem/kyber/crypto_kem/kyber512/optimized)
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
