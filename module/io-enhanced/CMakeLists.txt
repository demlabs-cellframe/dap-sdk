cmake_minimum_required(VERSION 3.10)
project(dap_io_enhanced C)

# clangd / LSP: emit build/compile_commands.json for this subtree only
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
add_definitions(-D_GNU_SOURCE)

option(DAP_IO_ENH_BOOTSTRAP_CORE "When standalone, add sibling dap_core as subproject" ON)

option(DAP_IO_STATS "Enable I/O statistics collection" ON)
if(DAP_IO_STATS)
    add_definitions(-DDAP_IO_STATS)
endif()

set(DAP_IO_ENH_SOURCES
    dap_vmqueue.c
    dap_vmqueue_olb.c
    dap_worker_common.c
    dap_worker_timer.c
    dap_worker_ipc.c
    dap_proc_thread_enh.c
    dap_conn_slab.c
    dap_io_lifecycle.c
    dap_io_access.c
)

if(WIN32)
    list(APPEND DAP_IO_ENH_SOURCES dap_worker_win.c)
else()
    list(APPEND DAP_IO_ENH_SOURCES dap_worker_posix.c)
endif()

add_library(${PROJECT_NAME} STATIC ${DAP_IO_ENH_SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC include)

if(NOT TARGET dap_core AND DAP_IO_ENH_BOOTSTRAP_CORE)
    set(DAP_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    if(EXISTS "${DAP_SDK_ROOT}/CMakeLists.txt")
        # Minimal bootstrap: build only dap_core (avoid pulling full dap-sdk stack).
        include("${DAP_SDK_ROOT}/cmake/OS_Detection.cmake")
        include("${DAP_SDK_ROOT}/cmake/ObjectLibraryHelpers.cmake")
        include("${DAP_SDK_ROOT}/cmake/DapLibrary.cmake")
        set(INSTALL_DAP_SDK OFF CACHE BOOL "Disable install when used as subproject" FORCE)
        set(BUILD_DAP_TESTS OFF CACHE BOOL "Disable dap_core tests in io-enhanced bootstrap" FORCE)
        add_subdirectory("${DAP_SDK_ROOT}/module/core" "${CMAKE_BINARY_DIR}/dap-sdk-core")
    endif()
endif()

if(TARGET dap_core)
    # In-tree build: use real project headers
    target_link_libraries(${PROJECT_NAME} dap_core)
    if(TARGET dap_io)
        target_link_libraries(${PROJECT_NAME} dap_io)
    endif()
else()
    # Standalone build: use dap_common.h from sibling core module.
    set(DAP_CORE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../core/include")
    if(NOT EXISTS "${DAP_CORE_INCLUDE_DIR}/dap_common.h")
        message(FATAL_ERROR "dap_common.h not found at ${DAP_CORE_INCLUDE_DIR}")
    endif()
    target_include_directories(${PROJECT_NAME} PUBLIC "${DAP_CORE_INCLUDE_DIR}")
endif()

if(UNIX)
    target_link_libraries(${PROJECT_NAME} pthread)
endif()
if(WIN32)
    target_link_libraries(${PROJECT_NAME} ws2_32 synchronization)
endif()

if(NOT TARGET dap_core)
    enable_testing()
    add_subdirectory(tests)
endif()
