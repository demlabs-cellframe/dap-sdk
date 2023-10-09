cmake_minimum_required(VERSION 3.10)
project (dap_crypto)

# fix implicit declaration warnings
add_definitions ("-D_GNU_SOURCE")

# define sphincs hash params
    #choose one hashtype
    if(NOT DEFINED SPHINCS_HASH_TYPE)
        #set(SPHINCS_HASH_TYPE "sphincs-haraka-128f")
        #set(SPHINCS_HASH_TYPE "sphincs-haraka-128s")
        #set(SPHINCS_HASH_TYPE "sphincs-haraka-192f")
        #set(SPHINCS_HASH_TYPE "sphincs-haraka-192s")
        #set(SPHINCS_HASH_TYPE "sphincs-haraka-256f")
        #set(SPHINCS_HASH_TYPE "sphincs-haraka-256s")
        #set(SPHINCS_HASH_TYPE "sphincs-sha2-128f")
        #set(SPHINCS_HASH_TYPE "sphincs-sha2-128s")
        #set(SPHINCS_HASH_TYPE "sphincs-sha2-192f")
        #set(SPHINCS_HASH_TYPE "sphincs-sha2-192s")
        #set(SPHINCS_HASH_TYPE "sphincs-sha2-256f")
        #set(SPHINCS_HASH_TYPE "sphincs-sha2-256s")
        #set(SPHINCS_HASH_TYPE "sphincs-shake-128f")
        #set(SPHINCS_HASH_TYPE "sphincs-shake-128s")
        #set(SPHINCS_HASH_TYPE "sphincs-shake-192f")
        #set(SPHINCS_HASH_TYPE "sphincs-shake-192s")
            set(SPHINCS_HASH_TYPE "sphincs-shake-256f")
        #set(SPHINCS_HASH_TYPE "sphincs-shake-256s")
    endif()

    #choose one thash
    if(NOT DEFINED SPHINCS_HASH_TYPE)
            set(SPHINCS_THASH "robust")
        #set(SPHINCS_THASH "simple")
    endif()

    add_definitions ("-DPARAMS=${SPHINCS_HASH_TYPE}")
    add_definitions ("-DTHASH=${SPHINCS_THASH}")

#add needed files
if(SPHINCS_HASH_TYPE MATCHES "shake")
    file( GLOB SPHINCS_SRCS
        src/sphincs_plus/fips202.c 
        src/sphincs_plus/hash_shake.c 
        src/sphincs_plus/thash_shake_${THASH}.c
    )

    file( GLOB SPHINCS_HEADERS
        src/sphincs_plus/fips202.h
    )
elseif(SPHINCS_HASH_TYPE MATCHES "haraka")
    file( GLOB SPHINCS_SRCS
        src/sphincs_plus/haraka.c 
        src/sphincs_plus/hash_haraka.c 
        src/sphincs_plus/thash_haraka_${THASH}.c
    )

    file( GLOB SPHINCS_HEADERS
        src/sphincs_plus/haraka.h
    )
elseif(SPHINCS_HASH_TYPE MATCHES "sha2")
    file( GLOB SPHINCS_SRCS
        src/sphincs_plus/sha2.c 
        src/sphincs_plus/hash_sha2.c 
        src/sphincs_plus/thash_sha2_${THASH}.c
    )

    file( GLOB SPHINCS_HEADERS
        src/sphincs_plus/sha2.h
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
      src/ringct20/*.c
      src/falcon/*.c
      src/sphincs_plus/address.c 
      src/sphincs_plus/randombytes.c 
      src/sphincs_plus/merkle.c 
      src/sphincs_plus/wots.c 
      src/sphincs_plus/wotsx1.c 
      src/sphincs_plus/utils.c 
      src/sphincs_plus/utilsx1.c 
      src/sphincs_plus/fors.c 
      src/sphincs_plus/sign.c
      ${SPHINCS_SRCS}

)

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
      src/sig_tesla/*.h
      src/sig_picnic/*.h
      src/sig_dilithium/*.h
      src/ringct20/*.h
      src/falcon/*.h
      src/sphincs_plus/params.h 
      src/sphincs_plus/address.h 
      src/sphincs_plus/randombytes.h 
      src/sphincs_plus/merkle.h 
      src/sphincs_plus/wots.h 
      src/sphincs_plus/wotsx1.h 
      src/sphincs_plus/utils.h 
      src/sphincs_plus/utilsx1.h 
      src/sphincs_plus/fors.h 
      src/sphincs_plus/api.h  
      src/sphincs_plus/hash.h 
      src/sphincs_plus/thash.h
      ${SPHINCS_HEADERS}
      include/*.h
)

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

set( CRYPTO_INCLUDE_PRIVATE src/seed src/rand src/iaes src/oaes src/sha3 src/msrln src/sig_bliss src/sig_tesla src/sig_picnic src/sig_dilithium src/falcon src include)
add_subdirectory (src/Kyber/crypto_kem/kyber512/optimized/)

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

if(WIN32)
  target_link_libraries(dap_crypto dap_core dap_crypto_kyber512 dap_json-c)
endif()

if(UNIX)
  target_link_libraries(dap_crypto dap_core dap_crypto_kyber512 dap_json-c m)
endif()

target_include_directories(dap_crypto PUBLIC ../3rdparty/shishua)

if(BUILD_CRYPTO_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
