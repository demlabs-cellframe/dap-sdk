cmake_minimum_required(VERSION 3.10)

project (dap_io C)
set(CMAKE_C_STANDARD 11)

add_definitions ("-D_GNU_SOURCE")

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LibraryHelpers.cmake)

file(GLOB DAP_IO_SOURCES *.c)
file(GLOB DAP_IO_HEADERS include/*.h)
if(WIN32)
    set(DAP_IO_SOURCES ${DAP_IO_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/wepoll/wepoll.c)
    set(DAP_IO_HEADERS ${DAP_IO_HEADERS} ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/wepoll/wepoll.h)
    include_directories(../3rdparty/wepoll)
elseif(LINUX OR ANDROID)
    file(GLOB DAP_IO_LINUX_SRCS linux/*.c)
    file(GLOB DAP_IO_LINUX_HEADERS linux/*.h)
    set (DAP_IO_HEADERS ${DAP_IO_HEADERS} ${DAP_IO_LINUX_HEADERS})
    set (DAP_IO_SOURCES ${DAP_IO_SOURCES} ${DAP_IO_LINUX_SRCS})
elseif(MACOS)
    file(GLOB DAP_IO_MACOS_SRCS macos/*.c)
    file(GLOB DAP_IO_MACOS_HEADERS macos/*.h)
    set (DAP_IO_HEADERS ${DAP_IO_HEADERS} ${DAP_IO_MACOS_HEADERS})
    set (DAP_IO_SOURCES ${DAP_IO_SOURCES} ${DAP_IO_MACOS_SRCS})
endif()

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${DAP_IO_SOURCES} HEADERS ${DAP_IO_HEADERS})

# Link dependencies as INTERFACE
# XKCP include directories are automatically propagated from dap_crypto via INTERFACE_INCLUDE_DIRECTORIES
# Use dap_link_libraries for automatic include propagation with cycle detection
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_crypto)

if(DAPSDK_MODULES MATCHES "ssl-support")
  target_link_libraries(${PROJECT_NAME} INTERFACE wolfssl)
endif()
# pthread is a system library, will be linked at final stage
if(UNIX)
  if(NOT ANDROID)
      # Store for later linking at final library stage
      set(DAP_IO_LIBRARIES pthread)
  endif()
endif()

target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(${PROJECT_NAME} PRIVATE src)
# All dependency include directories (including dap_crypto/src/rand) are automatically propagated via dap_link_libraries

if (WIN32)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/wepoll)
    target_link_libraries(${PROJECT_NAME} ws2_32)
elseif (LINUX OR ANDROID)
    target_include_directories(${PROJECT_NAME} INTERFACE ./linux)
elseif(MACOS)
    target_include_directories(${PROJECT_NAME} INTERFACE ./macos)
endif()

if (${BUILD_DAP_IO_TESTS} MATCHES ON)
    enable_testing()
    add_subdirectory(test)
endif()

if(INSTALL_DAP_SDK)
set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${DAP_IO_HEADERS}")
INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/io/
        ARCHIVE DESTINATION lib/dap/io/
        PUBLIC_HEADER DESTINATION include/dap/io/
)
endif()
