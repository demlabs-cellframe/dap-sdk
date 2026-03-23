cmake_minimum_required(VERSION 3.10)
project(dap_io C)

add_definitions(-DDAP_NET_CLIENT_NO_SSL -D_GNU_SOURCE)

set(_IO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

file(GLOB DAP_IO_HEADERS ${_IO_DIR}/include/*.h)

set(DAP_IO_SOURCES
    ${_IO_DIR}/dap_events.c
    ${_IO_DIR}/dap_events_socket.c
    ${_IO_DIR}/dap_context.c
    ${_IO_DIR}/dap_worker.c
    ${_IO_DIR}/dap_proc_thread.c
    ${_IO_DIR}/dap_timerfd.c
)

dap_add_library(${PROJECT_NAME} ${DAP_IO_SOURCES} HEADERS ${DAP_IO_HEADERS})
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_crypto dap_hash_table dap_math dap_net_common)

target_include_directories(${PROJECT_NAME} PUBLIC ${_IO_DIR}/include)
