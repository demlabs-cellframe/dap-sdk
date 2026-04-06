# ECDSA Performance Benchmarks
# Compares DAP native implementation with competitors (bitcoin-core/secp256k1, OpenSSL)

cmake_minimum_required(VERSION 3.10)

set(ECDSA_BENCHMARK_SOURCES
    benchmark_ecdsa.c
)

# Competitors directory
set(COMPETITORS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/competitors)

# Check if competitors are available
if(BUILD_BENCHMARKS AND EXISTS "${COMPETITORS_DIR}/secp256k1_lib/libsecp256k1.a")
    set(HAVE_SECP256K1_COMPETITOR TRUE)
    message(STATUS "[ECDSA benchmark] bitcoin-core/secp256k1 competitor: FOUND")
else()
    set(HAVE_SECP256K1_COMPETITOR FALSE)
    if(BUILD_BENCHMARKS)
        message(STATUS "[ECDSA benchmark] bitcoin-core/secp256k1 competitor: NOT FOUND")
        message(STATUS "[ECDSA benchmark] Run ./download_competitors.sh to download competitors")
    endif()
endif()

# Check for OpenSSL
if(BUILD_BENCHMARKS)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        set(HAVE_OPENSSL_COMPETITOR TRUE)
        message(STATUS "[ECDSA benchmark] OpenSSL ECDSA competitor: FOUND (${OPENSSL_VERSION})")
    else()
        set(HAVE_OPENSSL_COMPETITOR FALSE)
        message(STATUS "[ECDSA benchmark] OpenSSL ECDSA competitor: NOT FOUND")
    endif()
endif()

# Create benchmark executable
add_executable(benchmark_ecdsa ${ECDSA_BENCHMARK_SOURCES})

# Suppress warn_unused_result for benchmark (we intentionally ignore some returns)
target_compile_options(benchmark_ecdsa PRIVATE -Wno-unused-result)

# Link with DAP SDK
# dap_crypto depends on dap_json, dap_config, dap_math — OBJECT lib deps don't propagate
target_link_libraries(benchmark_ecdsa PRIVATE
    dap_crypto
    dap_core
    dap_json
    dap_math
    dap_config
)

# Include DAP SDK headers
target_include_directories(benchmark_ecdsa PRIVATE
    ${CMAKE_SOURCE_DIR}/module/crypto/include
    ${CMAKE_SOURCE_DIR}/module/core/include
    ${CMAKE_SOURCE_DIR}/module/crypto/src/sig/classic/ecdsa
)

# Architecture-specific scalar benchmarks
if(BUILD_BENCHMARKS)
    target_compile_definitions(benchmark_ecdsa PRIVATE BENCHMARK_SCALAR_ARCH)
    
    # Include internal ECDSA headers
    target_include_directories(benchmark_ecdsa PRIVATE
        ${CMAKE_SOURCE_DIR}/module/crypto/src/sig/classic/ecdsa
    )
endif()

# Add competitor implementations
if(HAVE_SECP256K1_COMPETITOR )
    target_compile_definitions(benchmark_ecdsa PRIVATE HAVE_SECP256K1_COMPETITOR)
    target_include_directories(benchmark_ecdsa PRIVATE
        ${COMPETITORS_DIR}/secp256k1_lib/include
        ${COMPETITORS_DIR}  # For internal scalar headers
    )
    target_link_libraries(benchmark_ecdsa PRIVATE
        ${COMPETITORS_DIR}/secp256k1_lib/libsecp256k1.a
    )
    
    # Add internal scalar benchmark source for competitor
    target_sources(benchmark_ecdsa PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/secp256k1_scalar_bench.c
    )
endif()

if(HAVE_OPENSSL_COMPETITOR)
    target_compile_definitions(benchmark_ecdsa PRIVATE HAVE_OPENSSL_COMPETITOR)
    target_include_directories(benchmark_ecdsa PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(benchmark_ecdsa PRIVATE OpenSSL::Crypto)
    if(WIN32 OR MINGW)
        find_package(ZLIB QUIET)
        if(ZLIB_FOUND)
            target_link_libraries(benchmark_ecdsa PRIVATE ZLIB::ZLIB)
        else()
            target_link_libraries(benchmark_ecdsa PRIVATE z)
        endif()
    endif()
endif()

# Add to CTest
add_test(NAME ecdsa_benchmark COMMAND benchmark_ecdsa)
set_tests_properties(ecdsa_benchmark PROPERTIES
    LABELS "performance;crypto"
    TIMEOUT 300
)

# Custom target to run benchmark
add_custom_target(run_ecdsa_benchmark
    COMMAND benchmark_ecdsa
    DEPENDS benchmark_ecdsa
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running ECDSA performance benchmark"
)
