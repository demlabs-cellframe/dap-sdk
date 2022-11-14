cmake_minimum_required(VERSION 3.10)

project (dap_io C)
set(CMAKE_C_STANDARD 11)

add_definitions ("-D_GNU_SOURCE")

file(GLOB DAP_IO_SOURCES *.c)
file(GLOB DAP_IO_HEADERS include/*.h)
if(WIN32)
    set(DAP_IO_SOURCES ${DAP_IO_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/wepoll/wepoll.c)
    set(DAP_IO_HEADERS ${DAP_IO_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/wepoll/wepoll.h)
    include_directories(../3rdparty/wepoll)
endif()

add_library(${PROJECT_NAME} STATIC ${DAP_IO_HEADERS} ${DAP_IO_SOURCES})

if(DAPSDK_MODULES MATCHES "ssl-support")
  target_link_libraries(${PROJECT_NAME} dap_core dap_crypto wolfssl)
else()
  target_link_libraries(${PROJECT_NAME} dap_core dap_crypto)
endif()

if(UNIX)
  if(NOT ANDROID)
      target_link_libraries(${PROJECT_NAME} pthread)
  endif()

endif()

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/uthash/src)

if (WIN32)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/wepoll)
endif()

if (${BUILD_DAP_IO_TESTS} MATCHES ON)
    enable_testing()
    add_subdirectory(test)
endif()

