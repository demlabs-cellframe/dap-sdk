# Shared HTTP-DAP phase machine for transport adapters (SDK-aware).
# Extracted from the REALITY adapter (plan phase 0). In-tree only: it depends
# on dap-sdk types (dap_events_socket/dap_stream/dap_client), so there is no
# standalone mode (unlike the SDK-free carrier core).

cmake_minimum_required(VERSION 3.16)

if(NOT TARGET dap_core)
    return()
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)

create_object_library(dap_trans_carrier_dap DAP_INTERNAL_MODULES
    "dap_carrier_dap.c" HEADERS "dap_carrier_dap.h")

target_include_directories(dap_trans_carrier_dap PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
# dap_tls_stream_buffer.h (header-only plaintext tail, shared with TLS)
target_include_directories(dap_trans_carrier_dap PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tls
)

dap_link_libraries(dap_trans_carrier_dap INTERFACE
    dap_core
    dap_crypto
    dap_io
    dap_stream
    dap_client
    dap_net_trans
)
message(STATUS "Transport carrier DAP phase machine: ON (dap_trans_carrier_dap)")