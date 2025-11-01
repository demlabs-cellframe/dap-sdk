cmake_minimum_required(VERSION 3.10)
project (dap_http_server C)

file(GLOB HTTP_SERVER_SRCS FILES *.c http_client/*.c)
file(GLOB HTTP_SERVER_HDRS FILES include/*.h http_client/include/*.h)

if (ANDROID)
  list(FILTER HTTP_SERVER_SRCS EXCLUDE REGEX ".*dap_http_folder\\.c$")
  list(FILTER HTTP_SERVER_HDRS EXCLUDE REGEX ".*dap_http_folder\\.h$")
  message(${HTTP_SERVER_SRCS})
endif()


if(WIN32)
    add_definitions ("-DUNDEBUG")
    add_definitions ("-DNDEBUG")
    add_definitions ("-DWIN32")
    add_definitions ("-D_WINDOWS")
    add_definitions ("-D__WINDOWS__")
    add_definitions ("-D_CRT_SECURE_NO_WARNINGS")
endif()

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/LibraryHelpers.cmake)

include_directories(${PROJECT_NAME} include http_client/include)

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${HTTP_SERVER_SRCS} HEADERS ${HTTP_SERVER_HDRS})
# Include directories - use absolute paths for proper propagation
target_include_directories(${PROJECT_NAME} INTERFACE . ${CMAKE_CURRENT_SOURCE_DIR}/http_client/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/http_client/include)
# All dependency include directories (including dap_core with 3rdparty/uthash/src) are automatically propagated via dap_link_libraries

if(DARWIN)
    target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include)
endif()

if (UNIX)
    target_include_directories(${PROJECT_NAME} PUBLIC ${IPUTILS_INCLUDE_DIRS})
endif()

if(WIN32)
  target_link_libraries(${PROJECT_NAME} iconv
  kernel32
  user32
  shell32
  winmm
  gdi32
  advapi32
  ole32
  version
  imm32
  oleaut32
  ws2_32
  ntdll
  psapi
  shlwapi
  bcrypt
  crypt32
  secur32
)
endif()
# Link dependencies as INTERFACE for OBJECT library
# System libraries (iconv, kernel32, etc.) will be linked at final stage
# Use dap_link_libraries for automatic include propagation with cycle detection
# dap_crypto is needed because dap_http_ban_list_client.h includes dap_hash.h which needs KeccakHash.h
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_io dap_json-c dap_http_common dap_crypto)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${HTTP_SERVER_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/server/http_server/
        ARCHIVE DESTINATION lib/dap/net/server/http_server/
        PUBLIC_HEADER DESTINATION include/dap/net/server/http_server/
)
endif()
