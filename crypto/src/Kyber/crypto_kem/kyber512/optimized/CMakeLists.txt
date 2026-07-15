cmake_minimum_required(VERSION 3.10)

project (dap_crypto_kyber512 C)
set(CMAKE_C_STANDARD 11)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../cmake/LibraryHelpers.cmake)

add_definitions ("-D_GNU_SOURCE")

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-stringop-overflow")
    if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 12.2)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Warray-parameter=0")
    endif()
endif()

file(GLOB DAP_CRYPTO_KYBER512_SOURCES
    aes256ctr.c
    cbd.c
    fips202_kyber.c
    indcpa.c
    kem.c
    ntt_kyber.c
    poly_kyber.c
    polyvec.c
    reduce_kyber.c
    sha256_kyber.c
    sha512_kyber.c
    symmetric-aes.c
    symmetric-shake.c
    verify.c
)

file(GLOB DAP_CRYPTO_KYBER512_HEADERS
    aes256ctr.h
    cbd.h
    fips202_kyber.h
    indcpa.h
    kem.h
    kyber512.h
    ntt_kyber.h
    params.h
    poly_kyber.h
    polyvec.h
    reduce_kyber.h
    sha2.h
    speed_print.h
    symmetric.h
    verify.h
)

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_CRYPTO_KYBER512_SOURCES} HEADERS ${DAP_CRYPTO_KYBER512_HEADERS})

get_filename_component(DAP_CRYPTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ ABSOLUTE)
target_include_directories(${PROJECT_NAME} PRIVATE ${DAP_CRYPTO_SRC_DIR})
target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC .)

dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core)

if (${BUILD_DAP_CRYPTO_KYBER512_TESTS} MATCHES ON)
    enable_testing()
    add_subdirectory(test)
endif()

if(INSTALL_DAP_SDK)
    INSTALL(TARGETS ${PROJECT_NAME}
            LIBRARY DESTINATION lib/dap/crypto/
            ARCHIVE DESTINATION lib/dap/crypto/
    )
endif()
