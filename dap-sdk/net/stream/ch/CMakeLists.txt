cmake_minimum_required(VERSION 3.10)
project (dap_stream_ch)
  
file(GLOB DAP_STREAM_CH_SRCS *.c)
file(GLOB DAP_STREAM_CH_HDRS include/*.h)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${DAP_STREAM_CH_SRCS} HEADERS ${DAP_STREAM_CH_HDRS})

target_link_libraries(dap_stream_ch dap_core dap_crypto dap_stream)

target_include_directories(dap_stream_ch INTERFACE .)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/uthash/src)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../io/include)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_STREAM_CH_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/stream/ch/
        ARCHIVE DESTINATION lib/dap/net/stream/ch/
        PUBLIC_HEADER DESTINATION include/dap/net/stream/ch/
)
endif()