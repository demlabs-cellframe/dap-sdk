cmake_minimum_required(VERSION 3.10)
project(dap_link_manager)
add_definitions ("-D_GNU_SOURCE")

file(GLOB DAP_LINK_MANAGER_SOURCES FILES *.c)
file(GLOB DAP_LINK_MANAGER_HEADERS FILES include/*.h)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${DAP_LINK_MANAGER_SOURCES} HEADERS ${DAP_LINK_MANAGER_HEADERS})

# Ensure link_manager builds after crypto module (which includes XKCP)
add_dependencies(${PROJECT_NAME} dap_crypto)

# Get XKCP include directories from crypto module's XKCP target
get_target_property(XKCP_INCLUDES dap_crypto_XKCP INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(${PROJECT_NAME} PUBLIC ${XKCP_INCLUDES})




# Store dependencies for final library - no linking of OBJECT targets
set(LINK_MANAGER_LIBS "")
if(UNIX AND NOT ANDROID AND NOT DARWIN)
    list(APPEND LINK_MANAGER_LIBS "rt")
endif()

set(DAP_LINK_MANAGER_DEPS "${LINK_MANAGER_LIBS}" CACHE INTERNAL "dap_link_manager external dependencies")


target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE .)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../core/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../core/src/unix)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../crypto/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../io/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/uthash/src)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/json-c)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/../../../module/core/json-c)



target_include_directories(${PROJECT_NAME} PUBLIC ../client/include)
target_include_directories(${PROJECT_NAME} PUBLIC ../stream/stream/include)
target_include_directories(${PROJECT_NAME} PUBLIC ../stream/session/include)
target_include_directories(${PROJECT_NAME} PUBLIC ../stream/ch/include)
target_include_directories(${PROJECT_NAME} PUBLIC ../server/http_server/include)
target_include_directories(${PROJECT_NAME} PUBLIC ../server/http_server/http_client/include)
target_include_directories(${PROJECT_NAME} PUBLIC ../server/enc_server/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../global-db/include)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_LINK_MANAGER_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/link_manager/
        ARCHIVE DESTINATION lib/dap/net/link_manager/
        PUBLIC_HEADER DESTINATION include/dap/net/link_manager/
)
endif()