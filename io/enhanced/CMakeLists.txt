cmake_minimum_required(VERSION 3.10)
project(dap_io_enhanced C)

# clangd / LSP: emit build/compile_commands.json for this subtree only
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
add_definitions(-D_GNU_SOURCE)

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

if(TARGET dap_core)
    # In-tree build: use real project headers
    target_link_libraries(${PROJECT_NAME} dap_core dap_io)
else()
    # Standalone build: use lightweight compat shims
    target_include_directories(${PROJECT_NAME} PUBLIC compat)
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
