cmake_minimum_required(VERSION 3.10)

set(BENCHMARK_SOURCES benchmark_crypto.c)

add_executable(benchmark_crypto ${BENCHMARK_SOURCES})

target_link_libraries(benchmark_crypto dap_sdk)
target_include_directories(benchmark_crypto PRIVATE
    ${CMAKE_SOURCE_DIR}/module/crypto/include
    ${CMAKE_SOURCE_DIR}/module/core/include
)

# --- Competitor detection ---
set(COMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/competitors")
set(COMP_INSTALL "${COMP_DIR}/install")

# liboqs
if(EXISTS "${COMP_INSTALL}/lib/liboqs.a" AND EXISTS "${COMP_INSTALL}/include/oqs/oqs.h")
    message(STATUS "[benchmark_crypto] liboqs found")
    target_compile_definitions(benchmark_crypto PRIVATE HAVE_LIBOQS=1)
    target_include_directories(benchmark_crypto PRIVATE "${COMP_INSTALL}/include")
    target_link_libraries(benchmark_crypto "${COMP_INSTALL}/lib/liboqs.a")
endif()

# OpenSSL
find_package(OpenSSL QUIET)
if(OPENSSL_FOUND)
    message(STATUS "[benchmark_crypto] OpenSSL ${OPENSSL_VERSION} found")
    target_compile_definitions(benchmark_crypto PRIVATE HAVE_OPENSSL=1)
    target_link_libraries(benchmark_crypto OpenSSL::Crypto)
endif()

# libsodium
if(EXISTS "${COMP_INSTALL}/lib/libsodium.a" AND EXISTS "${COMP_INSTALL}/include/sodium.h")
    message(STATUS "[benchmark_crypto] libsodium found")
    target_compile_definitions(benchmark_crypto PRIVATE HAVE_LIBSODIUM=1)
    target_include_directories(benchmark_crypto PRIVATE "${COMP_INSTALL}/include")
    target_link_libraries(benchmark_crypto "${COMP_INSTALL}/lib/libsodium.a")
endif()

set_target_properties(benchmark_crypto PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmarks"
)
