cmake_minimum_required(VERSION 3.10)
project(dap_crypto_tls C)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LibraryHelpers.cmake)

set(DAP_TLS_BACKEND "" CACHE STRING "TLS backend (auto, openssl, boringssl, apple, schannel)")
if(NOT DAP_TLS_BACKEND OR DAP_TLS_BACKEND STREQUAL "auto")
    if(APPLE)
        set(DAP_TLS_BACKEND_DETECTED "apple")
    elseif(WIN32)
        set(DAP_TLS_BACKEND_DETECTED "schannel")
    else()
        set(DAP_TLS_BACKEND_DETECTED "openssl")
    endif()
else()
    set(DAP_TLS_BACKEND_DETECTED "${DAP_TLS_BACKEND}")
endif()

message(STATUS "Real TLS backend: ${DAP_TLS_BACKEND_DETECTED}")

set(DAP_TLS_BACKEND_SRC "")
set(DAP_TLS_LINK_LIBS "")

if(DAP_TLS_BACKEND_DETECTED STREQUAL "openssl")
    set(DAP_TLS_EXISTING_OPENSSL_TARGETS "")
    foreach(tls_dependency OpenSSL::SSL OpenSSL::Crypto)
        if(TARGET ${tls_dependency})
            list(APPEND DAP_TLS_EXISTING_OPENSSL_TARGETS ${tls_dependency})
        endif()
    endforeach()
    find_package(OpenSSL 1.1.1 REQUIRED COMPONENTS SSL Crypto)
    # SDK object-library dependencies are propagated to targets outside this
    # directory (daemon, GUI, plugins). Imported targets must remain visible.
    foreach(tls_dependency OpenSSL::SSL OpenSSL::Crypto)
        if(NOT tls_dependency IN_LIST DAP_TLS_EXISTING_OPENSSL_TARGETS)
            set_property(TARGET ${tls_dependency} PROPERTY IMPORTED_GLOBAL TRUE)
        endif()
    endforeach()
    set(DAP_TLS_BACKEND_SRC "${CMAKE_CURRENT_SOURCE_DIR}/os/openssl/dap_tls_openssl.c")
    set(DAP_TLS_LINK_LIBS OpenSSL::SSL OpenSSL::Crypto)
    set(DAP_TLS_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR})
    message(STATUS "  OpenSSL version: ${OPENSSL_VERSION}")
elseif(DAP_TLS_BACKEND_DETECTED STREQUAL "boringssl")
    # BoringSSL is API-compatible with OpenSSL; reuse the openssl backend source.
    # Enables browser-like ClientHello fingerprinting (anti-DPI). Requires
    # -DBORINGSSL_ROOT=<prefix> with include/ and lib/libssl.a + libcrypto.a.
    if(NOT BORINGSSL_ROOT)
        message(FATAL_ERROR "DAP_TLS_BACKEND=boringssl requires -DBORINGSSL_ROOT=<prefix> (include/ + lib/libssl.a,libcrypto.a)")
    endif()
    set(DAP_TLS_BACKEND_SRC "${CMAKE_CURRENT_SOURCE_DIR}/os/openssl/dap_tls_openssl.c")
    set(DAP_TLS_INCLUDE_DIRS "${BORINGSSL_ROOT}/include")
    find_library(BORINGSSL_SSL_LIB NAMES ssl
        PATHS "${BORINGSSL_ROOT}/lib" "${BORINGSSL_ROOT}/build/ssl" NO_DEFAULT_PATH REQUIRED)
    find_library(BORINGSSL_CRYPTO_LIB NAMES crypto
        PATHS "${BORINGSSL_ROOT}/lib" "${BORINGSSL_ROOT}/build/crypto" NO_DEFAULT_PATH REQUIRED)
    set(DAP_TLS_LINK_LIBS ${BORINGSSL_SSL_LIB} ${BORINGSSL_CRYPTO_LIB})
    # BoringSSL crypto contains C++; ensure a C++ stdlib and threads are linked.
    if(NOT APPLE)
        list(APPEND DAP_TLS_LINK_LIBS stdc++)
    endif()
    list(APPEND DAP_TLS_LINK_LIBS pthread)
    set(DAP_TLS_BACKEND_DEFINES DAP_TLS_BACKEND_BORINGSSL=1)
    # Optional fingerprint APIs (present in recent BoringSSL, absent in OpenSSL).
    # check_symbol_exists links a test binary: provide BoringSSL + a C++ runtime,
    # otherwise the checks fail silently and GREASE/permutation stay disabled.
    include(CheckSymbolExists)
    set(_dap_tls_saved_inc "${CMAKE_REQUIRED_INCLUDES}")
    set(_dap_tls_saved_libs "${CMAKE_REQUIRED_LIBRARIES}")
    set(CMAKE_REQUIRED_INCLUDES "${BORINGSSL_ROOT}/include")
    set(CMAKE_REQUIRED_LIBRARIES "${BORINGSSL_SSL_LIB};${BORINGSSL_CRYPTO_LIB}")
    if(APPLE)
        list(APPEND CMAKE_REQUIRED_LIBRARIES c++)
    else()
        list(APPEND CMAKE_REQUIRED_LIBRARIES stdc++)
    endif()
    list(APPEND CMAKE_REQUIRED_LIBRARIES pthread)
    check_symbol_exists(SSL_CTX_set_grease_enabled "openssl/ssl.h" DAP_TLS_HAVE_SSL_CTX_SET_GREASE_ENABLED)
    check_symbol_exists(SSL_CTX_set_permute_extensions "openssl/ssl.h" DAP_TLS_HAVE_SSL_CTX_SET_PERMUTE_EXTENSIONS)
    set(CMAKE_REQUIRED_INCLUDES "${_dap_tls_saved_inc}")
    set(CMAKE_REQUIRED_LIBRARIES "${_dap_tls_saved_libs}")
    if(DAP_TLS_HAVE_SSL_CTX_SET_GREASE_ENABLED)
        list(APPEND DAP_TLS_BACKEND_DEFINES DAP_TLS_HAVE_SSL_CTX_SET_GREASE_ENABLED=1)
    endif()
    if(DAP_TLS_HAVE_SSL_CTX_SET_PERMUTE_EXTENSIONS)
        list(APPEND DAP_TLS_BACKEND_DEFINES DAP_TLS_HAVE_SSL_CTX_SET_PERMUTE_EXTENSIONS=1)
    endif()
    message(STATUS "  BoringSSL root: ${BORINGSSL_ROOT} (ctx_grease=${DAP_TLS_HAVE_SSL_CTX_SET_GREASE_ENABLED} ctx_permute=${DAP_TLS_HAVE_SSL_CTX_SET_PERMUTE_EXTENSIONS})")
elseif(DAP_TLS_BACKEND_DETECTED STREQUAL "apple")
    set(DAP_TLS_BACKEND_SRC "${CMAKE_CURRENT_SOURCE_DIR}/os/apple/dap_tls_apple.c")
    find_library(SECURITY_FRAMEWORK Security REQUIRED)
    set(DAP_TLS_LINK_LIBS ${SECURITY_FRAMEWORK})
elseif(DAP_TLS_BACKEND_DETECTED STREQUAL "schannel")
    set(DAP_TLS_BACKEND_SRC "${CMAKE_CURRENT_SOURCE_DIR}/os/windows/dap_tls_schannel.c")
    set(DAP_TLS_LINK_LIBS secur32 crypt32)
else()
    message(FATAL_ERROR "Unknown TLS backend: ${DAP_TLS_BACKEND_DETECTED}")
endif()

file(GLOB TLS_HDRS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

create_object_library(dap_crypto_tls DAP_INTERNAL_MODULES ${DAP_TLS_BACKEND_SRC} HEADERS ${TLS_HDRS})

target_include_directories(dap_crypto_tls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(DAP_TLS_INCLUDE_DIRS)
    if(DAP_TLS_BACKEND_DETECTED STREQUAL "boringssl")
        # Any directory-scope include_directories() (e.g. OpenSSL via
        # buildconfigs/macos.cmake) is inherited BEFORE target-level entries;
        # prepend BoringSSL so <openssl/*.h> resolves to BoringSSL for this target.
        target_include_directories(dap_crypto_tls BEFORE PRIVATE ${DAP_TLS_INCLUDE_DIRS})
    else()
        target_include_directories(dap_crypto_tls PRIVATE ${DAP_TLS_INCLUDE_DIRS})
    endif()
endif()

if(DAP_TLS_BACKEND_DEFINES)
    target_compile_definitions(dap_crypto_tls PRIVATE ${DAP_TLS_BACKEND_DEFINES})
endif()

dap_link_libraries(dap_crypto_tls INTERFACE dap_core)

if(DAP_TLS_LINK_LIBS)
    if(DAP_LINUX_OPENSSL_STATIC_LIBS AND DAP_TLS_BACKEND_DETECTED STREQUAL "openssl")
        # Same as dap_trans_websocket: headers only, no static libcrypto.a on .so plugins.
    else()
        target_link_libraries(dap_crypto_tls INTERFACE ${DAP_TLS_LINK_LIBS})
    endif()
endif()
