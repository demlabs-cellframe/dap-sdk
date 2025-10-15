cmake_minimum_required(VERSION 3.10)
project(dap_stream_event C)

file(GLOB STREAM_EVENT_SRCS *.c)
file(GLOB STREAM_EVENT_HDRS include/*.h)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${STREAM_EVENT_SRCS} HEADERS ${STREAM_EVENT_HDRS})

# Minimal dependencies - only net_common for types
target_link_libraries(${PROJECT_NAME} dap_core dap_net_common)

target_include_directories(${PROJECT_NAME} INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${STREAM_EVENT_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/stream/event/
        ARCHIVE DESTINATION lib/dap/net/stream/event/
        PUBLIC_HEADER DESTINATION include/dap/net/stream/event/
)
endif()

