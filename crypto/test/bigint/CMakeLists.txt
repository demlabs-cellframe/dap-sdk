cmake_minimum_required(VERSION 3.10)
project(bigint_test C CXX)

# Set C++14 as minimum standard (required for GoogleTest)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find required packages
set(BOOST_REQUESTED_VERSION 1.65)
find_package(Boost ${BOOST_REQUESTED_VERSION} COMPONENTS system program_options REQUIRED)
find_package(GTest REQUIRED)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Add all test files
add_executable(${PROJECT_NAME}
    bigint_logic_tests.cc
    bigint_add_test.cc
    bigint_test_utilities.cc
    bigint_test_utilities.h
    prompt_eng_add.cc
    ../../src/bigint/add.c
    ../../src/bigint/add_specific_limb_size.c
    ../../src/bigint/adder_subtractor.c
    ../../src/bigint/circuit_formalism.c
    ../../src/bigint/single_bit.c
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    dap_crypto
    ${Boost_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
    pthread
    m
)

# Set include directories for the target
target_include_directories(${PROJECT_NAME}
    PRIVATE
    ../../src/bigint/
    ${GTEST_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})






