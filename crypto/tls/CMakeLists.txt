cmake_minimum_required(VERSION 3.10)
project(dap_crypto_tls C)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/LibraryHelpers.cmake)

set(DAP_TLS_BACKEND "" CACHE STRING "TLS backend (auto, openssl, apple, schannel)")
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
    target_include_directories(dap_crypto_tls PRIVATE ${DAP_TLS_INCLUDE_DIRS})
endif()

dap_link_libraries(dap_crypto_tls INTERFACE dap_core)

if(DAP_TLS_LINK_LIBS)
    if(DAP_LINUX_OPENSSL_STATIC_LIBS AND DAP_TLS_BACKEND_DETECTED STREQUAL "openssl")
        # Same as dap_trans_websocket: headers only, no static libcrypto.a on .so plugins.
    else()
        target_link_libraries(dap_crypto_tls INTERFACE ${DAP_TLS_LINK_LIBS})
    endif()
endif()
