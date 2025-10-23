cmake_minimum_required(VERSION 3.10)
project(dap_notify_srv C)
  
file(GLOB DAP_NOTIFY_SRV_SRCS FILES src/*.c)
file(GLOB DAP_NOTIFY_SRV_HDRS FILES include/*.h)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${DAP_NOTIFY_SRV_SRCS} HEADERS ${DAP_NOTIFY_SRV_HDRS})

target_include_directories(${PROJECT_NAME} PUBLIC include)

target_link_libraries(${PROJECT_NAME} dap_core dap_io dap_json-c )

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_NOTIFY_SRV_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/server/notify_server/
        ARCHIVE DESTINATION lib/dap/net/server/notify_server/
        PUBLIC_HEADER DESTINATION include/dap/net/server/notify_server/
)
endif()