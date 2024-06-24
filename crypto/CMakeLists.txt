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
    # set(SPHINCS_HASH_TYPE "sphincs-sha2-256s")
    # set(SPHINCS_HASH_TYPE "sphincs-shake-128f")
    # set(SPHINCS_HASH_TYPE "sphincs-shake-128s")
    # set(SPHINCS_HASH_TYPE "sphincs-shake-192f")
    # set(SPHINCS_HASH_TYPE "sphincs-shake-192s")
    # set(SPHINCS_HASH_TYPE "sphincs-shake-256f")

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
      src/sha3/*.c
      src/msrln/*.c
      src/newhope/*.c
      src/sig_bliss/*.c
      src/sig_tesla/*.c
      src/sig_picnic/*.c
      src/sig_dilithium/*.c
      src/sig_shipovnik/*.c
      src/sig_shipovnik/streebog/*.c
      src/ringct20/*.c
      src/falcon/*.c
      ${SPHINCS_BASE_SRCS}
      ${SPHINCS_SRCS}
) 

if (BUILD_WITH_TPS_TEST)
    file (GLOB ECDSA_SRC src/sig_ecdsa/*.c)
    set (CRYPTO_SRCS ${CRYPTO_SRCS} ${ECDSA_SRC})
    add_definitions("-DDAP_TPS_TEST")
else()
    list (FILTER CRYPTO_SRCS EXCLUDE REGEX ".*ecdsa.c")
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
      src/sha3/*.h
      src/msrln/*.h
      src/newhope/*.h
      src/sig_bliss/*.h
      src/sig_shipovnik/*.h
      src/sig_shipovnik/streebog/*.h
      src/sig_tesla/*.h
      src/sig_picnic/*.h
      src/sig_dilithium/*.h
      src/ringct20/*.h
      src/falcon/*.h
      ${SPHINCS_BASE_HEADERS}
      ${SPHINCS_HEADERS}
      include/*.h
)
if (BUILD_WITH_TPS_TEST)
    file (GLOB ECDSA_HDR src/sig_ecdsa/*.h)
    set (CRYPTO_HEADERS ${CRYPTO_HEADERS} ${ECDSA_HDR})
else()
    list (FILTER CRYPTO_HEADERS EXCLUDE REGEX ".*ecdsa.h")
endif()

set_source_files_properties(src/sig_picnic/picnic_impl.c PROPERTIES COMPILE_FLAGS -Wno-stringop-overflow)
if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12.2)
    set_source_files_properties(src/sha3/fips202.c PROPERTIES COMPILE_FLAGS -Warray-parameter=0)
endif()

enable_language(ASM)

file( GLOB XKCP_SRCS
  #src/XKCP/lib/high/Keccak/FIPS202/KeccakHash.c
  #src/XKCP/lib/high/Keccak/KeccakSpongeWidth1600.c
  src/XKCP/lib/high/Keccak/SP800-185/SP800-185.c
  src/XKCP/lib/high/Keccak/FIPS202/SimpleFIPS202.c
)

set( CRYPTO_INCLUDE_PRIVATE src/seed src/rand src/iaes src/oaes src/sha3 src/msrln src/sig_bliss src/sig_tesla src/sig_picnic src/sig_dilithium src/falcon src/sig_shipovnik src/sig_shipovnik/streebog src/sphincsplus src include)
add_subdirectory (src/Kyber/crypto_kem/kyber512/optimized/)
if (BUILD_WITH_TPS_TEST)
    add_subdirectory (${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/secp256k1/ ${CMAKE_CURRENT_BINARY_DIR}/secp256k1)
endif()

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

if(WIN32)
  if(BUILD_64)
    #file( GLOB XKCP_SRCS2 src/XKCP/lib/low/KeccakP-1600/OptimizedAsmX86-64/KeccakP-1600-x86-64-gas.s ) - SIGSEGV on MinGW
    file( GLOB XKCP_SRCS2 src/XKCP/lib/low/KeccakP-1600/Compact64/KeccakP-1600-compact64.c )
  else()
    file( GLOB XKCP_SRCS2 src/XKCP/lib/low/KeccakP-1600/Inplace32BI/KeccakP-1600-inplace32BI.c )
  endif()
endif()

if(UNIX)
    if (LINUX)
	if(DAP_DEBUG)
	    if(BUILD_64)
		file( GLOB XKCP_SRCS2 src/XKCP/lib/low/KeccakP-1600/Compact64/KeccakP-1600-compact64.c )
    	    else()
		file( GLOB XKCP_SRCS2 src/XKCP/lib/low/KeccakP-1600/Inplace32BI/KeccakP-1600-inplace32BI.c )
    	    endif()
	endif()
    elseif(APPLE)
	if(BUILD_64)
	    if((MACOS) AND(MACOS_ARCH STREQUAL "x86_64") )
		file( GLOB XKCP_SRCS2 src/XKCP/lib/low/KeccakP-1600/OptimizedAsmX86-64/KeccakP-1600-x86-64-gas_Apple.s )
	    else()
		file( GLOB XKCP_SRCS2 src/XKCP/lib/low/KeccakP-1600/Compact64/KeccakP-1600-compact64.c )
	    endif()
	else()
	    file( GLOB XKCP_SRCS2 src/XKCP/lib/low/KeccakP-1600/Inplace32BI/KeccakP-1600-inplace32BI.c )
	endif()
    elseif(BSD)
	if(BUILD_64)
            file( GLOB XKCP_SRCS2 src/XKCP/lib/low/KeccakP-1600/Compact64/KeccakP-1600-compact64.c )
	else()
	    file( GLOB XKCP_SRCS2 src/XKCP/lib/low/KeccakP-1600/Inplace32BI/KeccakP-1600-inplace32BI.c )
        endif()
    else()
	file( GLOB XKCP_SRCS2 src/XKCP/lib/low/KeccakP-1600/Inplace32BI/KeccakP-1600-inplace32BI.c )
    endif()

endif()





add_library(${PROJECT_NAME} STATIC ${CRYPTO_SRCS} ${XKCP_SRCS} ${XKCP_SRCS2} ${CRYPTO_HEADERS} ${DAP_PQLR_SRC} )

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
target_include_directories(dap_crypto INTERFACE src/ src/sha3 include/)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src)

target_include_directories(dap_crypto PUBLIC
        src/XKCP/lib/common
        src/XKCP/lib/low/common
        src/XKCP/lib/high/Keccak
        src/XKCP/lib/high/common
        src/XKCP/lib/high/Keccak/FIPS202
        src/XKCP/lib/high/Keccak/SP800-185
        src/XKCP/lib/low/KeccakP-1600/Optimized
)

if(WIN32)
  if(BUILD_64)

    #target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600/OptimizedAsmX86-64)
    target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600/Compact64)

    if (__AVX__)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times4/SIMD256)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times4/SIMD512/AVX2ufull)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn4)
    elseif(__AVX2__)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times2/SIMD512)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times2/SIMD512/AVX512ufull)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times4/SIMD512)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times4/SIMD512/AVX512ufull)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times8/SIMD512)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times8/SIMD512/AVX512ufull)
    elseif(__SSE4_1__)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times2/SIMD128)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times2/SIMD128/SSE2ufull)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times4/FallbackOn2)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn2)
    else()
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times2/FallbackOn1)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times4/FallbackOn1)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn1)
    endif()

  else()
    target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600/Inplace32BI)

    if (__AVX__)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times4/SIMD256)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times4/SIMD512/AVX2ufull)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn4)
    elseif(__AVX2__)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times2/SIMD512)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times2/SIMD512/AVX512ufull)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times4/SIMD512)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times4/SIMD512/AVX512ufull)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times8/SIMD512)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times8/SIMD512/AVX512ufull)
    elseif(__SSE4_1__)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times2/SIMD128)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times2/SIMD128/SSE2ufull)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times4/FallbackOn2)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn2)
    else()
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times2/FallbackOn1)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times4/FallbackOn1)
        target_include_directories(dap_crypto PUBLIC src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn1)
    endif()
  endif()
endif()

if(UNIX)
  if(BUILD_64)
    target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600/OptimizedAsmX86-64)
    target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600/OptimizedAsmX86-64)

    if(__AVX2__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times2/SIMD512)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times2/SIMD512/AVX512ufull)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times2/SIMD512)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times2/SIMD512/AVX512ufull)
    elseif(__SSE4_1__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times2/SIMD128)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times2/SIMD128/SSE2ufull)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times2/SIMD128)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times2/SIMD128/SSE2ufull)
    else()
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times2/FallbackOn1)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times2/FallbackOn1)
    endif()

    if(__AVX2__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times4/SIMD512)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times4/SIMD512/AVX512ufull)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times4/SIMD512)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times4/SIMD512/AVX512ufull)
    elseif(__AVX__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times4/SIMD256)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times4/SIMD512/AVX2ufull)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times4/SIMD256)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times4/SIMD512/AVX2ufull)
    elseif(__SSE4_1__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times4/FallbackOn2)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times4/FallbackOn2)
    else()
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times4/FallbackOn1)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times4/FallbackOn1)
    endif()

    if(__AVX2__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times8/SIMD512)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times8/SIMD512/AVX512ufull)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times8/SIMD512)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times8/SIMD512/AVX512ufull)
    elseif(__AVX__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn4)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn4)
    elseif(__SSE4_1__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn2)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn2)
    else()
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn1)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn1)
    endif()

  else()
    target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600/Inplace32BI)
    target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600/Inplace32BI)

    if(__AVX2__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times2/SIMD512)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times2/SIMD512/AVX512ufull)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times2/SIMD512)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times2/SIMD512/AVX512ufull)
    elseif(__SSE4_1__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times2/SIMD128)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times2/SIMD128/SSE2ufull)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times2/SIMD128)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times2/SIMD128/SSE2ufull)
    else()
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times2/FallbackOn1)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times2/FallbackOn1)
    endif()

    if(__AVX2__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times4/SIMD512)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times4/SIMD512/AVX512ufull)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times4/SIMD512)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times4/SIMD512/AVX512ufull)
    elseif(__AVX__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times4/SIMD256)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times4/SIMD512/AVX2ufull)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times4/SIMD256)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times4/SIMD512/AVX2ufull)
    elseif(__SSE4_1__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times4/FallbackOn2)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times4/FallbackOn2)
    else()
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times4/FallbackOn1)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times4/FallbackOn1)
    endif()

    if(__AVX2__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times8/SIMD512)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times8/SIMD512/AVX512ufull)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times8/SIMD512)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times8/SIMD512/AVX512ufull)
    elseif(__AVX__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn4)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn4)
    elseif(__SSE4_1__)
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn2)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn2)
    else()
        target_include_directories(dap_crypto INTERFACE src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn1)
        target_include_directories(dap_crypto PRIVATE src/XKCP/lib/low/KeccakP-1600-times8/FallbackOn1)
    endif()

  endif()
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/json-c)

target_link_libraries(dap_crypto dap_core dap_crypto_kyber512 dap_json-c)
if (BUILD_WITH_TPS_TEST)
    target_link_libraries(dap_crypto secp256k1)
endif()

if(UNIX)
  target_link_libraries(dap_crypto m)
endif()

target_include_directories(dap_crypto PUBLIC ../3rdparty/shishua)
if (BUILD_WITH_TPS_TEST)
    target_include_directories(dap_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/secp256k1/include)
    target_include_directories(dap_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/secp256k1/src)
endif()

if(BUILD_CRYPTO_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${CRYPTO_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/crypto/
        ARCHIVE DESTINATION lib/dap/crypto/
        PUBLIC_HEADER DESTINATION include/dap/crypto/
)

INSTALL(DIRECTORY src/XKCP/ DESTINATION include/dap/crypto/XKCP/)
INSTALL(DIRECTORY src/rand/ DESTINATION include/dap/crypto/rand/)
INSTALL(DIRECTORY src/msrln/ DESTINATION include/dap/crypto/msrln/)
INSTALL(DIRECTORY src/sig_dilithium/ DESTINATION include/dap/crypto/sig_dilithium/)
INSTALL(DIRECTORY src/sha3/ DESTINATION include/dap/crypto/sha3/)
endif()
