cmake_minimum_required(VERSION 3.10)
project(uint256_test C CXX)

set(CMAKE_C_STANDARD 11)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 14)

#include_directories(../../include/)
 
set(BOOST_REQUESTED_VERSION 1.65)
 
if (NOT Boost_INCLUDE_DIR)
        FIND_PACKAGE( Boost COMPONENTS system program_options REQUIRED )
endif()
#INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR}
#                    ../../include/)
include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG main
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
#enable_testing()
add_executable(${PROJECT_NAME}
               256_tests.cc
)
target_link_libraries(${PROJECT_NAME}
		dap_core
		GTest::gtest_main
		GTest::gtest
		${Boost_LIBRARIES}
		m
)
target_include_directories(${PROJECT_NAME}
        INTERFACE
        ../../include/
)

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

