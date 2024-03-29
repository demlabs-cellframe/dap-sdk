cmake_minimum_required(VERSION 3.10)
project(dap_enc_server C)
  
file(GLOB DAP_ENC_SERVER_SRCS FILES *.c)
file(GLOB DAP_ENC_SERVER_HDRS FILES include/*.h)

if(WIN32)
    add_definitions ("-DUNDEBUG")
    add_definitions ("-DNDEBUG")
    add_definitions ("-DWIN32")
    add_definitions ("-D_WINDOWS")
    add_definitions ("-D__WINDOWS__")
    add_definitions ("-D_CRT_SECURE_NO_WARNINGS")
endif()

add_library(${PROJECT_NAME} STATIC ${DAP_ENC_SERVER_SRCS} ${DAP_ENC_SERVER_HDRS})
target_include_directories(${PROJECT_NAME} INTERFACE . include/)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/uthash/src)

target_link_libraries(${PROJECT_NAME} dap_core dap_crypto dap_io dap_http_server dap_json-c)
if(WIN32)
    target_link_libraries(${PROJECT_NAME}
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

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_ENC_SERVER_HDRS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/net/server/enc_server/
        ARCHIVE DESTINATION lib/dap/net/server/enc_server/
        PUBLIC_HEADER DESTINATION include/dap/net/server/enc_server/
)
endif()
