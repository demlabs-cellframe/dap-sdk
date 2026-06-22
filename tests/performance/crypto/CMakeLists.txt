cmake_minimum_required(VERSION 3.10)

add_executable(benchmark_crypto benchmark_crypto.c)
target_link_libraries(benchmark_crypto dap_sdk)

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
    if(WIN32 OR MINGW)
        find_package(ZLIB QUIET)
        if(ZLIB_FOUND)
            target_link_libraries(benchmark_crypto ZLIB::ZLIB)
        else()
            target_link_libraries(benchmark_crypto z)
        endif()
        target_link_libraries(benchmark_crypto crypt32)
    endif()
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

add_executable(bench_dilithium_components bench_dilithium_components.c)
target_link_libraries(bench_dilithium_components dap_sdk)
set_target_properties(bench_dilithium_components PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmarks"
)

macro(add_crypto_bench NAME)
    add_executable(${NAME} ${NAME}.c)
    target_link_libraries(${NAME} dap_sdk)
    set_target_properties(${NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmarks"
    )
endmacro()

add_crypto_bench(bench_mlkem_raw)
add_crypto_bench(bench_mlkem_components)
add_crypto_bench(bench_ntt_micro)
add_crypto_bench(bench_rng_overhead)
add_crypto_bench(bench_chipmunk_mring)
add_crypto_bench(bench_chipmunk_ring)

# bench_chipmunk_mring: optional liboqs for ML-DSA comparison
if(EXISTS "${COMP_INSTALL}/lib/liboqs.a" AND EXISTS "${COMP_INSTALL}/include/oqs/oqs.h")
    message(STATUS "[bench_chipmunk_mring] liboqs found — ML-DSA comparison enabled")
    target_compile_definitions(bench_chipmunk_mring PRIVATE HAVE_LIBOQS=1)
    target_include_directories(bench_chipmunk_mring PRIVATE "${COMP_INSTALL}/include")
    target_link_libraries(bench_chipmunk_mring "${COMP_INSTALL}/lib/liboqs.a")
endif()

# bench_mlkem_breakdown needs hash wrapper TU for inline function access
add_executable(bench_mlkem_breakdown bench_mlkem_breakdown.c bench_mlkem_hash_wrappers.c)
target_link_libraries(bench_mlkem_breakdown dap_sdk)
set_target_properties(bench_mlkem_breakdown PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmarks"
)
