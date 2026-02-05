# secp256k1 Performance Benchmarks
# Compares DAP native implementation with competitors

cmake_minimum_required(VERSION 3.10)

# Option to include competitor implementations
option(BENCHMARK_SECP256K1_COMPETITORS "Include competitor secp256k1 implementations in benchmark" ON)

set(SECP256K1_BENCHMARK_SOURCES
    benchmark_secp256k1.c
)

# Competitors directory
set(COMPETITORS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/competitors)

# Check if competitors are available
if(BENCHMARK_SECP256K1_COMPETITORS AND EXISTS "${COMPETITORS_DIR}/secp256k1_lib/libsecp256k1.a")
    set(HAVE_SECP256K1_COMPETITOR TRUE)
    message(STATUS "[secp256k1 benchmark] bitcoin-core/secp256k1 competitor: FOUND")
else()
    set(HAVE_SECP256K1_COMPETITOR FALSE)
    if(BENCHMARK_SECP256K1_COMPETITORS)
        message(STATUS "[secp256k1 benchmark] bitcoin-core/secp256k1 competitor: NOT FOUND")
        message(STATUS "[secp256k1 benchmark] Run ./download_competitors.sh to download competitors")
    endif()
endif()

# Check for OpenSSL
if(BENCHMARK_SECP256K1_COMPETITORS)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        set(HAVE_OPENSSL_COMPETITOR TRUE)
        message(STATUS "[secp256k1 benchmark] OpenSSL ECDSA competitor: FOUND (${OPENSSL_VERSION})")
    else()
        set(HAVE_OPENSSL_COMPETITOR FALSE)
        message(STATUS "[secp256k1 benchmark] OpenSSL ECDSA competitor: NOT FOUND")
    endif()
endif()

# Create benchmark executable
add_executable(benchmark_secp256k1 ${SECP256K1_BENCHMARK_SOURCES})

# Suppress warn_unused_result for benchmark (we intentionally ignore some returns)
target_compile_options(benchmark_secp256k1 PRIVATE -Wno-unused-result)

# Link with DAP SDK
target_link_libraries(benchmark_secp256k1 PRIVATE
    dap_crypto
    dap_core
)

# Include DAP SDK headers
target_include_directories(benchmark_secp256k1 PRIVATE
    ${CMAKE_SOURCE_DIR}/module/crypto/include
    ${CMAKE_SOURCE_DIR}/module/core/include
)

# Add competitor implementations
if(HAVE_SECP256K1_COMPETITOR)
    target_compile_definitions(benchmark_secp256k1 PRIVATE HAVE_SECP256K1_COMPETITOR)
    target_include_directories(benchmark_secp256k1 PRIVATE
        ${COMPETITORS_DIR}/secp256k1_lib/include
    )
    target_link_libraries(benchmark_secp256k1 PRIVATE
        ${COMPETITORS_DIR}/secp256k1_lib/libsecp256k1.a
    )
endif()

if(HAVE_OPENSSL_COMPETITOR)
    target_compile_definitions(benchmark_secp256k1 PRIVATE HAVE_OPENSSL_COMPETITOR)
    target_include_directories(benchmark_secp256k1 PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(benchmark_secp256k1 PRIVATE OpenSSL::Crypto)
endif()

# Add to CTest
add_test(NAME secp256k1_benchmark COMMAND benchmark_secp256k1)
set_tests_properties(secp256k1_benchmark PROPERTIES
    LABELS "performance;crypto"
    TIMEOUT 300
)

# Custom target to run benchmark
add_custom_target(run_secp256k1_benchmark
    COMMAND benchmark_secp256k1
    DEPENDS benchmark_secp256k1
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running secp256k1 performance benchmark"
)
