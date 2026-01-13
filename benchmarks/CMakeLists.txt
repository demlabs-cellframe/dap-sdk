cmake_minimum_required(VERSION 3.12)

# Benchmarks are optional
option(BUILD_DAP_SDK_BENCHMARKS "Build DAP SDK benchmarks" OFF)

if(NOT BUILD_DAP_SDK_BENCHMARKS)
    message("[!] DAP SDK benchmarks disabled (use -DBUILD_DAP_SDK_BENCHMARKS=ON to enable)")
    return()
endif()

message("[+] DAP SDK benchmarks enabled")

# Stage 1 Multi-Architecture Benchmark
add_executable(benchmark_stage1_multi_arch
    benchmark_stage1_multi_arch.c
)

target_link_libraries(benchmark_stage1_multi_arch
    dap_core
    dap_json
)

target_include_directories(benchmark_stage1_multi_arch PRIVATE
    ${CMAKE_SOURCE_DIR}/module/core/include
    ${CMAKE_SOURCE_DIR}/module/json/include
    ${CMAKE_SOURCE_DIR}/module/json/src
    ${CMAKE_SOURCE_DIR}/module/json/src/stage1
)

# Install benchmarks
install(TARGETS benchmark_stage1_multi_arch
    RUNTIME DESTINATION bin/benchmarks
    COMPONENT benchmarks
)

# Competitive Benchmark Suite (Phase 2.6)
add_executable(benchmark_competitive_full
    benchmark_competitive_full.cpp
)

# Enable C++17 for simdjson
set_target_properties(benchmark_competitive_full PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

target_link_libraries(benchmark_competitive_full
    dap_core
    dap_json
)

target_include_directories(benchmark_competitive_full PRIVATE
    ${CMAKE_SOURCE_DIR}/module/core/include
    ${CMAKE_SOURCE_DIR}/module/json/include
    ${CMAKE_SOURCE_DIR}/module/json/src
)

# Check for simdjson
find_path(SIMDJSON_INCLUDE_DIR simdjson.h
    PATHS ${CMAKE_SOURCE_DIR}/benchmarks/competitors/simdjson/include
    NO_DEFAULT_PATH
)

if(SIMDJSON_INCLUDE_DIR)
    message("[+] simdjson found: ${SIMDJSON_INCLUDE_DIR}")
    target_compile_definitions(benchmark_competitive_full PRIVATE HAVE_SIMDJSON=1)
    target_include_directories(benchmark_competitive_full PRIVATE ${SIMDJSON_INCLUDE_DIR})
    
    # Try to find simdjson library
    find_library(SIMDJSON_LIBRARY
        NAMES simdjson
        PATHS ${CMAKE_SOURCE_DIR}/benchmarks/competitors/simdjson/build
        NO_DEFAULT_PATH
    )
    
    if(SIMDJSON_LIBRARY)
        target_link_libraries(benchmark_competitive_full ${SIMDJSON_LIBRARY})
    else()
        message("[!] simdjson library not found, will try to build it")
    endif()
else()
    message("[!] simdjson not found (optional competitor)")
endif()

install(TARGETS benchmark_competitive_full
    RUNTIME DESTINATION bin/benchmarks
    COMPONENT benchmarks
)
