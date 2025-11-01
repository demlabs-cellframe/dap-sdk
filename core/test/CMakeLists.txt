cmake_minimum_required(VERSION 3.10)

project(core_test)

set(CMAKE_C_STANDARD 11)

# Include DAP Mock Framework
include(${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework/mocks/DAPMockAutoWrap.cmake)

file(GLOB SRCS *.c)

# IMPORTANT: Do NOT compile unix/*.c here - they are compiled in dap_core_unix
# Instead, we will link dap_core_unix library
# But we DO compile unix/*_test.c (test files)
if(UNIX)
    if (OFF_CORE_CELLFRAME_SDK_TESTS_OFF MATCHES "network-monitor")
        file(GLOB PLATROFM_DEP_TEST_SRCS unix/dap_cpu*_test.c unix/dap_process*_test.c)
        file(GLOB PLATROFM_DEP_HEADERS unix/dap_cpu*.h unix/dap_process*.h)  
    else()
        file(GLOB PLATROFM_DEP_TEST_SRCS unix/*_test.c)
        file(GLOB PLATROFM_DEP_HEADERS unix/*.h)  
    endif()
    list(APPEND SRCS ${PLATROFM_DEP_TEST_SRCS})
endif()

add_executable(${PROJECT_NAME} ${SRCS})

# Link dap_core + dap_core_unix (contains unix/*.c functions)
if(UNIX AND TARGET dap_core_unix)
    target_link_libraries(core_test dap_test dap_core dap_core_unix pthread)
else()
    target_link_libraries(core_test dap_test dap_core pthread)
endif()

# Include mock framework headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../test-framework/mocks
)

# Apply mock autowrap
dap_mock_autowrap(${PROJECT_NAME})
if (DAP_USE_RPMALLOC)
add_executable(rpmalloc_test
  ../../3rdparty/rpmalloc/test/test.h
  ../../3rdparty/rpmalloc/test/thread.h 
  ../../3rdparty/rpmalloc/test/thread.c
  ../../3rdparty/rpmalloc/test/main.c
)
target_link_libraries(rpmalloc_test dap_core m pthread dl)
target_include_directories(rpmalloc_test PRIVATE ../../3rdparty/rpmalloc/test)
if (NOT PLATFORM_ARM AND VERSION MATCHES "7l")
    add_test(
        NAME rpm-test
        COMMAND rpmalloc_test
    )
else()
    message("[-] Disable rpm test")
endif()
endif(DAP_USE_RPMALLOC)

add_test(
    NAME core-test
    COMMAND core_test
)

if(UNIX)
    target_include_directories(${PROJECT_NAME} PRIVATE unix)
    target_include_directories(${PROJECT_NAME} PRIVATE unix/linux)
endif()
add_subdirectory(uint256_t)

