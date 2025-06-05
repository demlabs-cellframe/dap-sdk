cmake_minimum_required(VERSION 3.10)
project(bigint_test C CXX)

# Set C++14 as minimum standard (required for GoogleTest)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include directories
include_directories(../../include/ ../../../crypto/src/bigint/)

# Find required packages
set(BOOST_REQUESTED_VERSION 1.65)
find_package(Boost ${BOOST_REQUESTED_VERSION} COMPONENTS system program_options REQUIRED)
find_package(GTest REQUIRED)

# Include test directories
include_directories(${GTEST_INCLUDE_DIRS})

# Add all test files
add_executable(${PROJECT_NAME}
    bigint_tests.cc
    bigint_logic_tests.cc
    bigint_add_test.cc
    prompt_eng_add.cc
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    dap_core
    dap_crypto
    ${Boost_LIBRARIES}
    ${GTEST_LIBRARIES}
    pthread
    m
)

# Set include directories for the target
target_include_directories(${PROJECT_NAME}
    PRIVATE
    ../../include/
    ../../../crypto/src/bigint/
    ${GTEST_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

add_test(
    NAME bigint-test
    COMMAND bigint_test
)






