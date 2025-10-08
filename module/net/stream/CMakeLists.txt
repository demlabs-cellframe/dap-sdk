project(libdap-stream C)
cmake_minimum_required(VERSION 3.10)


set(DAP_STREAM_TEST OFF)

if (DAP_STREAM_TEST)
    add_definitions("-DDAP_STREAM_TEST")
    add_subdirectory(test)
endif()

add_subdirectory(session)
add_subdirectory(event)
add_subdirectory(stream)
add_subdirectory(ch)
