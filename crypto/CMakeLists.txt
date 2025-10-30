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
      src/ringct20/*.c
      src/falcon/*.c
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

list (FILTER CRYPTO_SRCS EXCLUDE REGEX ".*test_falcon.c$")
list (FILTER CRYPTO_SRCS EXCLUDE REGEX ".*speed.c$")

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
      src/sig_tesla/*.h
      src/sig_picnic/*.h
      src/sig_dilithium/*.h
      src/ringct20/*.h
      src/falcon/*.h
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



set_source_files_properties(src/sig_picnic/picnic_impl.c PROPERTIES COMPILE_FLAGS -Wno-stringop-overflow)
if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12.2)
    set_source_files_properties(src/sha3/fips202.c PROPERTIES COMPILE_FLAGS -Warray-parameter=0)
    set_source_files_properties(src/oaes/oaes_lib.c PROPERTIES COMPILE_FLAGS -Wstringop-overflow=0)
endif()

enable_language(ASM)


set( CRYPTO_INCLUDE_PRIVATE src/seed src/rand src/iaes src/oaes src/sha3 src/msrln src/sig_bliss src/sig_tesla src/sig_picnic src/sig_dilithium src/falcon src/sig_shipovnik src/sig_shipovnik/streebog src/sphincsplus src include)
add_subdirectory (src/Kyber/crypto_kem/kyber512/optimized/)
add_subdirectory(./XKCP)
if (BUILD_WITH_TPS_TEST OR BUILD_WITH_ECDSA)
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


add_library(${PROJECT_NAME} STATIC ${CRYPTO_SRCS} ${CRYPTO_HEADERS} ${DAP_PQLR_SRC} )

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

add_dependencies(${PROJECT_NAME} dap_crypto_XKCP) 
target_link_libraries(${PROJECT_NAME} dap_crypto_XKCP)
# Get XKCP include directory from the target property
get_target_property(XKCP_INCLUDE_DIR dap_crypto_XKCP INTERFACE_INCLUDE_DIRECTORIES)
if(XKCP_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PUBLIC ${XKCP_INCLUDE_DIR})
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
target_include_directories(dap_crypto INTERFACE src/ src/sha3 include/)
# Add XKCP include directories for KeccakHash.h
get_target_property(XKCP_INCLUDE_DIR dap_crypto_XKCP INTERFACE_INCLUDE_DIRECTORIES)
if(XKCP_INCLUDE_DIR)
    target_include_directories(dap_crypto PUBLIC ${XKCP_INCLUDE_DIR})
    target_include_directories(dap_crypto INTERFACE ${XKCP_INCLUDE_DIR})
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src)



target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/json-c)

target_link_libraries(dap_crypto dap_core dap_crypto_kyber512 dap_json-c)

if (BUILD_WITH_TPS_TEST OR BUILD_WITH_ECDSA)
    target_link_libraries(dap_crypto secp256k1)
endif()


if(UNIX)
  target_link_libraries(dap_crypto m)
endif()

target_include_directories(dap_crypto PUBLIC ../3rdparty/shishua)
if (BUILD_WITH_ECDSA)
    target_include_directories(dap_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/secp256k1/include)
    target_include_directories(dap_crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/secp256k1/src)
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
INSTALL(DIRECTORY src/sha3/ DESTINATION include/dap/crypto/sha3/)
endif()
