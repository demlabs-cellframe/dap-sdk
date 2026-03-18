cmake_minimum_required(VERSION 3.10)

cmake_policy(SET CMP0002 NEW)
if(POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
endif()

project(dap_crypto_kyber1024 C)
set(CMAKE_C_STANDARD 11)

add_definitions("-D_GNU_SOURCE")

set(KYBER512_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../kyber512/optimized)

set(DAP_CRYPTO_KYBER1024_SOURCES
    ${KYBER512_SRC}/aes256ctr.c
    ${KYBER512_SRC}/cbd.c
    ${KYBER512_SRC}/fips202_kyber.c
    ${KYBER512_SRC}/indcpa.c
    ${KYBER512_SRC}/kem.c
    ${KYBER512_SRC}/ntt_kyber.c
    ${KYBER512_SRC}/poly_kyber.c
    ${KYBER512_SRC}/polyvec.c
    ${KYBER512_SRC}/reduce_kyber.c
    ${KYBER512_SRC}/sha256_kyber.c
    ${KYBER512_SRC}/sha512_kyber.c
    ${KYBER512_SRC}/symmetric-aes.c
    ${KYBER512_SRC}/symmetric-shake.c
    ${KYBER512_SRC}/verify.c
)

add_library(${PROJECT_NAME} STATIC ${DAP_CRYPTO_KYBER1024_SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE KYBER_K=4)

target_link_libraries(${PROJECT_NAME} dap_core dap_hash dap_math dap_math_bignum)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${KYBER512_SRC})
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../core/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../math/bignum/include
)

if(INSTALL_DAP_SDK)
    INSTALL(TARGETS ${PROJECT_NAME}
            LIBRARY DESTINATION lib/dap/crypto/
            ARCHIVE DESTINATION lib/dap/crypto/
    )
endif()
