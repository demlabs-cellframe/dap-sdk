cmake_minimum_required(VERSION 3.10)
project(bigint_test C CXX)

set(CMAKE_C_STANDARD 11)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 14)

include_directories(../../include/)
 
set(BOOST_REQUESTED_VERSION 1.65)
 
if (NOT Boost_INCLUDE_DIR)
        FIND_PACKAGE( Boost COMPONENTS system program_options REQUIRED )
endif()
#INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIR}
#                    ../../include/)

# Find GTest package
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

add_executable(${PROJECT_NAME}
               bigint_tests.cc
               bigint_logic_tests.cc
)

target_link_libraries(${PROJECT_NAME}
        dap_core
        ${Boost_LIBRARIES}
        ${GTEST_LIBRARIES}
        pthread
        m
)

target_include_directories(${PROJECT_NAME}
        INTERFACE
        ../../include/
        ${GTEST_INCLUDE_DIRS}
)


