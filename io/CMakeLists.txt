cmake_minimum_required(VERSION 3.10)

project (dap_io C)
set(CMAKE_C_STANDARD 11)

add_definitions ("-D_GNU_SOURCE")

file(GLOB DAP_IO_SOURCES *.c)
file(GLOB DAP_IO_HEADERS include/*.h)
if(WIN32)
    set(DAP_IO_SOURCES ${DAP_IO_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/wepoll/wepoll.c)
    set(DAP_IO_HEADERS ${DAP_IO_HEADERS} ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/wepoll/wepoll.h)
    include_directories(../3rdparty/wepoll)
elseif(LINUX OR ANDROID)
    file(GLOB DAP_IO_LINUX_SRCS linux/*.c)
    file(GLOB DAP_IO_LINUX_HEADERS linux/*.h)
endif()

add_library(${PROJECT_NAME} STATIC ${DAP_IO_HEADERS} ${DAP_IO_SOURCES} ${DAP_IO_LINUX_HEADERS} ${DAP_IO_LINUX_SRCS})

target_link_libraries(${PROJECT_NAME} dap_core dap_crypto)
if(DAPSDK_MODULES MATCHES "ssl-support")
  target_link_libraries(${PROJECT_NAME} wolfssl)
endif()

if(UNIX)
  if(NOT ANDROID)
      target_link_libraries(${PROJECT_NAME} pthread)
  endif()

endif()

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE src)

if (WIN32)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/wepoll)
    target_link_libraries(${PROJECT_NAME} ws2_32)
elseif (LINUX OR ANDROID)
    target_include_directories(${PROJECT_NAME} INTERFACE ./linux)
endif()

if (${BUILD_DAP_IO_TESTS} MATCHES ON)
    enable_testing()
    add_subdirectory(test)
endif()

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_IO_HEADERS}" "${DAP_IO_LINUX_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/io/
        ARCHIVE DESTINATION lib/dap/io/
        PUBLIC_HEADER DESTINATION include/dap/io/
)
endif()
