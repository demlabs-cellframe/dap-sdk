# Standalone build for the REALITY transport module (SOLID isolation):
#   reality_core      -- pure protocol core, ZERO SDK includes
#   reality_port_ossl -- OpenSSL + PQClean ML-KEM + socket/time ports
#   reality_tests     -- unit tests (no network) and interop test (live node)
#
# SDK integration is gated elsewhere by DAP_SDK_NET_TRANS_REALITY (default OFF);
# this file must build on its own:
#   cmake -S . -B /tmp/reality-build -DOPENSSL_ROOT=<prefix>
#   cmake --build /tmp/reality-build && ctest --test-dir /tmp/reality-build

cmake_minimum_required(VERSION 3.16)
project(reality_transport C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(REALITY_BUILD_TESTS "Build reality transport tests" ON)

if(NOT DEFINED OPENSSL_ROOT)
    message(FATAL_ERROR "Pass -DOPENSSL_ROOT=<prefix with include/ and lib/>")
endif()

# --- vendored ML-KEM-768 (PQClean clean, CC0) ---
file(GLOB SRC_MLKEM port/mlkem/*.c)

add_library(reality_core STATIC
    core/reality_fp_chrome.c
    core/reality_fp_build.c
    core/reality_auth.c
    core/vless_codec.c
    core/vless_vision.c
    core/rate_limiter.c
    core/reality_tls13.c
    core/reality_session.c
)
target_include_directories(reality_core PUBLIC include core)
target_compile_options(reality_core PRIVATE -Wall -Wextra)

add_library(reality_port_ossl STATIC
    ${SRC_MLKEM}
    port/reality_crypto_openssl.c
    port/reality_io_socket.c
    port/reality_time_std.c
    port/reality_bridge.c
    port/reality_forwarder.c
)
target_include_directories(reality_port_ossl PUBLIC include port)
target_include_directories(reality_port_ossl PRIVATE ${OPENSSL_ROOT}/include core)
target_compile_options(reality_port_ossl PRIVATE -Wall)

find_library(CRYPTO_LIB crypto PATHS ${OPENSSL_ROOT}/lib NO_DEFAULT_PATH)
find_library(SSL_LIB ssl PATHS ${OPENSSL_ROOT}/lib NO_DEFAULT_PATH)
if(NOT CRYPTO_LIB)
    find_library(CRYPTO_LIB crypto)
endif()
target_link_libraries(reality_port_ossl PUBLIC ${CRYPTO_LIB})
find_package(Threads REQUIRED)
target_link_libraries(reality_port_ossl PUBLIC Threads::Threads)

if(REALITY_BUILD_TESTS)
    enable_testing()

    add_executable(test_reality_unit tests/test_unit.c)
    target_link_libraries(test_reality_unit PRIVATE reality_core reality_port_ossl)
    add_test(NAME unit COMMAND test_reality_unit)

    add_executable(test_reality_interop tests/test_interop.c)
    target_link_libraries(test_reality_interop PRIVATE reality_core reality_port_ossl)
    add_test(NAME interop COMMAND test_reality_interop)

    add_executable(test_reality_bridge tests/test_bridge.c)
    target_link_libraries(test_reality_bridge PRIVATE reality_core reality_port_ossl)
    add_test(NAME bridge COMMAND test_reality_bridge)

    add_executable(test_reality_forwarder tests/test_forwarder.c)
    target_link_libraries(test_reality_forwarder PRIVATE reality_core reality_port_ossl)
    add_test(NAME forwarder COMMAND test_reality_forwarder)

    set_tests_properties(interop bridge forwarder PROPERTIES
        ENVIRONMENT "REALITY_NODE=95.179.231.80;REALITY_PORT=443;REALITY_SNI=www.samsung.com;REALITY_PUBKEY=wOhxSy2vj3r1DMsUoBuNwPyElcrvj3dGbr_Mv4i4TQQ;REALITY_SHORTID=b7598d95;REALITY_UUID=95e571e3-af83-42c4-8eb6-0c06b189b0dc")
endif()
