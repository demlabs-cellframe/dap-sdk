cmake_minimum_required(VERSION 3.10)
project(dap_link_manager)
add_definitions ("-D_GNU_SOURCE")

file(GLOB DAP_LINK_MANAGER_SOURCES FILES *.c)
file(GLOB DAP_LINK_MANAGER_HEADERS FILES include/*.h)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${DAP_LINK_MANAGER_SOURCES} HEADERS ${DAP_LINK_MANAGER_HEADERS})

# Link with required modules
# Note: Most dependencies auto-propagate, but some needed explicitly for headers
# TODO Phase 8.1: Remove client dependency via abstraction layer (client interface)
target_link_libraries(${PROJECT_NAME} dap_core dap_io dap_crypto dap_json_rpc dap_client)

# Ensure link_manager builds after crypto module (which includes XKCP)
add_dependencies(${PROJECT_NAME} dap_crypto)

# Get XKCP include directories from crypto module's XKCP target
get_target_property(XKCP_INCLUDES dap_crypto_XKCP INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(${PROJECT_NAME} PUBLIC ${XKCP_INCLUDES})

# All includes auto-propagate via linked modules above
# Only add local includes
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE .)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_LINK_MANAGER_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/link_manager/
        ARCHIVE DESTINATION lib/dap/net/link_manager/
        PUBLIC_HEADER DESTINATION include/dap/net/link_manager/
)
endif()