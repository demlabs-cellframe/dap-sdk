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

include_directories(${PROJECT_NAME} include http_client/include)
add_library(${PROJECT_NAME} STATIC ${HTTP_SERVER_SRCS} ${HTTP_SERVER_HDRS})
target_include_directories(${PROJECT_NAME} INTERFACE . http_client/include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/uthash/src)

if(DARWIN)
    target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include)
endif()

if (UNIX)
    target_include_directories(${PROJECT_NAME} PUBLIC ${IPUTILS_INCLUDE_DIRS})
endif()

if(WIN32)
  target_link_libraries(${PROJECT_NAME} regex tre intl iconv
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
target_link_libraries(${PROJECT_NAME} dap_core dap_io dap_json-c)

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${HTTP_SERVER_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/server/http_server/
        ARCHIVE DESTINATION lib/dap/net/server/http_server/
        PUBLIC_HEADER DESTINATION include/dap/net/server/http_server/
)
endif()
