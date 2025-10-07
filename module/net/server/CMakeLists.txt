project(libdap-server C)
cmake_minimum_required(VERSION 3.10)

add_subdirectory(cli_server)
add_subdirectory(notify_server)
add_subdirectory(http_server)
add_subdirectory(enc_server)
add_subdirectory(dns_server)
add_subdirectory(json_rpc)


if (BUILD_LIB_DAP_SERVER_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()



if(INSTALL_DAP_SDK)

INSTALL(FILES ${CMAKE_SOURCE_DIR}/3rdparty/http_status_code.h DESTINATION include/dap/net/)

endif()
