cmake_minimum_required(VERSION 3.10)
project(uint256_test C CXX)

set(CMAKE_C_STANDARD 11)

# GoogleTest requires at least C++17 (for newer versions)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#include_directories(../../include/)
 
set(BOOST_REQUESTED_VERSION 1.65)
 
if (NOT Boost_INCLUDE_DIR)
        FIND_PACKAGE( Boost REQUIRED )
endif()

# Try to find system GoogleTest first
find_package(GTest)

if(NOT GTest_FOUND)
    # If not found, use FetchContent
    include(FetchContent)
    FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    set(GTEST_LIBRARIES gtest_main gtest)
else()
    message(STATUS "Using system GoogleTest")
    set(GTEST_LIBRARIES GTest::gtest_main GTest::gtest)
endif()
add_executable(${PROJECT_NAME}
               256_tests.cc
)

# Set include directories  
target_include_directories(${PROJECT_NAME} PRIVATE
        ../../include/
        ${Boost_INCLUDE_DIR}
)

# Link with GoogleTest (system or FetchContent)
target_link_libraries(${PROJECT_NAME} PRIVATE
		dap_core
		${GTEST_LIBRARIES}
		m
)

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _HAS_STD_BYTE=0)
endif()

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

