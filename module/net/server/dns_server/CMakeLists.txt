cmake_minimum_required(VERSION 3.10)
project (dap_dns_server C)

file(GLOB DAP_DNS_SRV_SRCS *.c)
file(GLOB DAP_DNS_SRV_HEADERS include/*.h)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${DAP_DNS_SRV_SRCS} HEADERS ${DAP_DNS_SRV_HEADERS})

target_link_libraries(${PROJECT_NAME} dap_core dap_io m dap_json_rpc dap_client)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_DNS_SRV_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/server/dns_server/
        ARCHIVE DESTINATION lib/dap/net/server/dns_server/
        PUBLIC_HEADER DESTINATION include/dap/net/server/dns_server/
)
endif()
