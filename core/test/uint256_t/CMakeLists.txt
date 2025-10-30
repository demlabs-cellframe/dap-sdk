cmake_minimum_required(VERSION 3.10)
project(uint256_test C CXX)

# Suppress CMP0167 warning: FindBoost module is deprecated
# We use modern find_package approach without FindBoost module
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_C_STANDARD 11)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 14)

#include_directories(../../include/)
 
# Boost is required for multiprecision library (used for testing uint256_t)
# Use modern CMake approach: find_package will use BoostConfig.cmake if available,
# otherwise fall back to manual search
set(BOOST_REQUESTED_VERSION 1.65)

# Try to find Boost using modern Config mode first
find_package(Boost ${BOOST_REQUESTED_VERSION} QUIET COMPONENTS system program_options)

if(NOT Boost_FOUND)
    # Fallback: try to find Boost without version requirement
    find_package(Boost QUIET COMPONENTS system program_options)
endif()

if(NOT Boost_FOUND)
    # If Boost is not found, try to use pkg-config or manual search
    # Note: Boost::multiprecision is header-only, so we mainly need headers
    find_path(BOOST_INCLUDE_DIR 
        NAMES boost/multiprecision/cpp_int.hpp
        PATHS /usr/include /usr/local/include
    )
    
    if(BOOST_INCLUDE_DIR)
        message(STATUS "Found Boost headers at: ${BOOST_INCLUDE_DIR}")
        set(Boost_FOUND TRUE)
        set(Boost_INCLUDE_DIRS ${BOOST_INCLUDE_DIR})
        # multiprecision is header-only, so we don't need to link libraries
        # but we still need system and program_options if they are used
        find_library(BOOST_SYSTEM_LIB 
            NAMES boost_system
            PATHS /usr/lib /usr/local/lib
        )
        find_library(BOOST_PROGRAM_OPTIONS_LIB
            NAMES boost_program_options
            PATHS /usr/lib /usr/local/lib
        )
        if(BOOST_SYSTEM_LIB)
            set(Boost_SYSTEM_LIBRARY ${BOOST_SYSTEM_LIB})
        endif()
        if(BOOST_PROGRAM_OPTIONS_LIB)
            set(Boost_PROGRAM_OPTIONS_LIBRARY ${BOOST_PROGRAM_OPTIONS_LIB})
        endif()
    endif()
endif()

if(NOT Boost_FOUND)
    message(WARNING "Boost not found. Install Boost development package (libboost-dev or boost-devel)")
    message(WARNING "For Debian/Ubuntu: sudo apt-get install libboost-dev")
    message(WARNING "For Fedora/RHEL: sudo dnf install boost-devel")
    message(FATAL_ERROR "Boost is required for uint256_t tests")
endif()

# Boost::multiprecision is header-only, so we mainly need include directories
# If Boost was found via Config mode, targets may already exist
if(TARGET Boost::headers)
    # Modern BoostConfig provides Boost::headers target
    set(BOOST_TARGET Boost::headers)
else()
    if(TARGET Boost::boost)
        # Alternative modern target name
        set(BOOST_TARGET Boost::boost)
    else()
        # Create our own imported target for multiprecision (header-only)
        if(NOT TARGET Boost::multiprecision)
            add_library(Boost::multiprecision INTERFACE IMPORTED)
            set_target_properties(Boost::multiprecision PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIRS}"
            )
        endif()
        set(BOOST_TARGET Boost::multiprecision)
    endif()
endif()

# Create Boost::system imported target if needed
if(Boost_SYSTEM_LIBRARY AND NOT TARGET Boost::system)
    add_library(Boost::system SHARED IMPORTED)
    set_target_properties(Boost::system PROPERTIES
        IMPORTED_LOCATION "${Boost_SYSTEM_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIRS}"
    )
endif()

# Create Boost::program_options imported target if needed
if(Boost_PROGRAM_OPTIONS_LIBRARY AND NOT TARGET Boost::program_options)
    add_library(Boost::program_options SHARED IMPORTED)
    set_target_properties(Boost::program_options PROPERTIES
        IMPORTED_LOCATION "${Boost_PROGRAM_OPTIONS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIRS}"
    )
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
		${BOOST_TARGET}
		m
)

# Link system and program_options if they were found
if(TARGET Boost::system)
    target_link_libraries(${PROJECT_NAME} Boost::system)
endif()
if(TARGET Boost::program_options)
    target_link_libraries(${PROJECT_NAME} Boost::program_options)
endif()

target_include_directories(${PROJECT_NAME}
        INTERFACE
        ../../include/
        PRIVATE
        ${Boost_INCLUDE_DIRS}
)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})

