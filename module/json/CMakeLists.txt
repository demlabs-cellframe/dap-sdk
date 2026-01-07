cmake_minimum_required(VERSION 3.12)
project(dap_json)

# Include required CMake modules
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PackageComponents.cmake)

add_definitions("-D_GNU_SOURCE")

# Collect JSON module sources and headers
file(GLOB JSON_HEADERS include/*.h)
file(GLOB JSON_INTERNAL_HEADERS include/internal/*.h)
file(GLOB JSON_SRCS src/*.c)
file(GLOB JSON_STAGE1_SRCS src/stage1/*.c)
file(GLOB JSON_STAGE2_SRCS src/stage2/*.c)

# Combine all sources
set(JSON_ALL_SRCS ${JSON_SRCS} ${JSON_STAGE1_SRCS} ${JSON_STAGE2_SRCS})

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${JSON_ALL_SRCS} HEADERS ${JSON_HEADERS} ${JSON_INTERNAL_HEADERS})

# Register headers for component-based installation
register_module_headers(DIRECTORIES include)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/json-c
    ${CMAKE_CURRENT_BINARY_DIR}/../../3rdparty/json-c
)

# Link json-c library (temporary - will be replaced with native implementation)
add_subdirectory(../../3rdparty/json-c ${CMAKE_CURRENT_BINARY_DIR}/json-c)

# Dependencies - json depends on core and json-c
set(JSON_LIBRARIES "dap_core;dap_json-c" CACHE INTERNAL "dap_json external dependencies")

# Link dependencies
target_link_libraries(${PROJECT_NAME} PUBLIC dap_core PRIVATE ${JSON_LIBRARIES})

# Tests
if(BUILD_DAP_TESTS)
    add_definitions("-DDAP_JSON_TESTS")
    message("Enabled dap_json comprehensive test suite")
    enable_testing()
    # Note: Tests are in tests/unit/json/ and managed by tests/CMakeLists.txt
    # New test suite (Phase 0.2): Unicode, numeric edges, invalid JSON, boundaries, benchmarks
endif()

# Installation
if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${JSON_HEADERS}")
    INSTALL(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION lib/dap/json
        ARCHIVE DESTINATION lib/dap/json
        PUBLIC_HEADER DESTINATION include/dap/json/
    )
endif()

