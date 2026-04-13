cmake_minimum_required(VERSION 3.10)
project(dap_session)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/LibraryHelpers.cmake)

file(GLOB SESSION_SRCS *.c)
file(GLOB SESSION_HDRS include/*.h)

create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${SESSION_SRCS} HEADERS ${SESSION_HDRS})

dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_crypto dap_hash_table dap_net_common)

target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${SESSION_HDRS}")
    INSTALL(TARGETS ${PROJECT_NAME}
            LIBRARY DESTINATION lib/dap/net/stream/session/
            ARCHIVE DESTINATION lib/dap/net/stream/session/
            PUBLIC_HEADER DESTINATION include/dap/net/stream/session/
    )
endif()
