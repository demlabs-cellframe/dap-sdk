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

