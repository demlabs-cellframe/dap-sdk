cmake_minimum_required(VERSION 3.10)

set(MLKEM_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_ntt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_cbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_poly.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_polyvec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_indcpa.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_kem.c
)

set(MLKEM_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../math/bignum/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../hash/include
)

set(MLKEM_LINK_LIBS dap_core dap_hash dap_math_bignum)

# --- ML-KEM-512 (K=2) ---
add_library(dap_mlkem_512 STATIC ${MLKEM_COMMON_SOURCES})
target_compile_definitions(dap_mlkem_512 PRIVATE MLKEM_K=2)
target_include_directories(dap_mlkem_512 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(dap_mlkem_512 PRIVATE ${MLKEM_INCLUDE_DIRS})
target_link_libraries(dap_mlkem_512 ${MLKEM_LINK_LIBS})

# --- ML-KEM-768 (K=3, default) ---
add_library(dap_mlkem_768 STATIC ${MLKEM_COMMON_SOURCES})
target_compile_definitions(dap_mlkem_768 PRIVATE MLKEM_K=3)
target_include_directories(dap_mlkem_768 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(dap_mlkem_768 PRIVATE ${MLKEM_INCLUDE_DIRS})
target_link_libraries(dap_mlkem_768 ${MLKEM_LINK_LIBS})

# --- ML-KEM-1024 (K=4) ---
add_library(dap_mlkem_1024 STATIC ${MLKEM_COMMON_SOURCES})
target_compile_definitions(dap_mlkem_1024 PRIVATE MLKEM_K=4)
target_include_directories(dap_mlkem_1024 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(dap_mlkem_1024 PRIVATE ${MLKEM_INCLUDE_DIRS})
target_link_libraries(dap_mlkem_1024 ${MLKEM_LINK_LIBS})

if(INSTALL_DAP_SDK)
    install(TARGETS dap_mlkem_512 dap_mlkem_768 dap_mlkem_1024
            LIBRARY DESTINATION lib/dap/crypto/
            ARCHIVE DESTINATION lib/dap/crypto/)
endif()
