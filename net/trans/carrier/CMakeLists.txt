# Shared transport-carrier layer (SOLID isolation, plan phase 0):
#   carrier_bridge   -- session-agnostic thread + pipe pump
#   carrier ports    -- portable pipe / blocking-TCP io / platform compat
#   carrier_trust.h  -- prepared-trust contract (header-only)
#
# ZERO SDK dependencies, cross-platform (POSIX + Windows/MinGW).
#
# Dual mode:
#  - top-level (cmake -S net/trans/carrier): standalone build + unit tests;
#  - included from the dap-sdk tree (add_subdirectory): builds the object
#    library dap_trans_carrier that transport modules (reality/trojan/anytls/
#    xhttp/shadowtls) link against. No module auto-registration: the carrier
#    is a support library, not a transport.
#
# Standalone usage:
#   cmake -S . -B /tmp/carrier-build && cmake --build /tmp/carrier-build
#   ctest --test-dir /tmp/carrier-build

cmake_minimum_required(VERSION 3.16)

# In-tree when built inside the dap-sdk tree (dap_core target exists). This
# detection is robust even when carrier is add_subdirectory()'d from another
# standalone project (e.g. reality's standalone build), where CMAKE_SOURCE_DIR
# differs from this dir but no SDK targets exist.
if(TARGET dap_core)
    # ================= in-tree dap-sdk module =================
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)

    set(CARRIER_MODULE_SRCS
        carrier_bridge.c
        port/carrier_pipe.c
        port/carrier_io_socket.c
    )

    create_object_library(dap_trans_carrier DAP_INTERNAL_MODULES "${CARRIER_MODULE_SRCS}" HEADERS "")
    target_include_directories(dap_trans_carrier PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_include_directories(dap_trans_carrier PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/port
    )
    if(WIN32)
        target_link_libraries(dap_trans_carrier INTERFACE ws2_32 iphlpapi)
    endif()
    # pthread comes through consumers' dap_io link chain (same convention as
    # reality: do not reference Threads::Threads from this directory scope).
    message(STATUS "Transport carrier layer: ON (dap_trans_carrier)")
    return()
endif()

project(carrier C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(CARRIER_BUILD_TESTS "Build carrier unit tests" ON)

add_library(carrier STATIC
    carrier_bridge.c
    port/carrier_pipe.c
    port/carrier_io_socket.c
)
target_include_directories(carrier PUBLIC include port)
if(NOT MSVC)
    target_compile_options(carrier PRIVATE -Wall -Wextra)
endif()
find_package(Threads REQUIRED)
target_link_libraries(carrier PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(carrier PUBLIC ws2_32 iphlpapi)
endif()

if(CARRIER_BUILD_TESTS)
    enable_testing()
    add_executable(test_carrier_unit tests/test_unit.c)
    target_link_libraries(test_carrier_unit PRIVATE carrier)
    add_test(NAME carrier_unit COMMAND test_carrier_unit)
    set_tests_properties(carrier_unit PROPERTIES TIMEOUT 30)
endif()