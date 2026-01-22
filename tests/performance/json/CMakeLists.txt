cmake_minimum_required(VERSION 3.12)

# Performance tests for dap_json
message("[+] Configuring dap_json performance tests")

set(COMPETITORS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/competitors")

# ============================================================================
# Find competitor libraries
# ============================================================================

# simdjson
find_path(SIMDJSON_INCLUDE_DIR 
    NAMES simdjson.h
    PATHS "${COMPETITORS_DIR}/simdjson/include"
    NO_DEFAULT_PATH
)
find_library(SIMDJSON_LIBRARY
    NAMES simdjson libsimdjson.a
    PATHS "${COMPETITORS_DIR}/simdjson/build"
    NO_DEFAULT_PATH
)

# RapidJSON (header-only)
find_path(RAPIDJSON_INCLUDE_DIR
    NAMES rapidjson/document.h
    PATHS "${COMPETITORS_DIR}/rapidjson/include"
    NO_DEFAULT_PATH
)

# yajl
find_path(YAJL_INCLUDE_DIR
    NAMES yajl/yajl_parse.h
    PATHS "${COMPETITORS_DIR}/yajl/build/yajl-2.1.1/include"
          "${COMPETITORS_DIR}/yajl/build/yajl-2.1.0/include"
          "${COMPETITORS_DIR}/yajl/src/api"
    NO_DEFAULT_PATH
)
find_library(YAJL_LIBRARY
    NAMES yajl_s libyajl_s.a yajl libyajl.a
    PATHS "${COMPETITORS_DIR}/yajl/build/yajl-2.1.1/lib"
          "${COMPETITORS_DIR}/yajl/build/yajl-2.1.0/lib"
          "${COMPETITORS_DIR}/yajl/build/src"
    NO_DEFAULT_PATH
)

# json-c
find_path(JSONC_INCLUDE_DIR
    NAMES json.h
    PATHS "${COMPETITORS_DIR}/json-c/build"
    NO_DEFAULT_PATH
)
find_library(JSONC_LIBRARY
    NAMES json-c libjson-c.a
    PATHS "${COMPETITORS_DIR}/json-c/build"
    NO_DEFAULT_PATH
)

# ============================================================================
# Competitive benchmark (full comparison)
# ============================================================================

add_executable(benchmark_competitive_full
    benchmark_competitive_full.cpp
)

target_link_libraries(benchmark_competitive_full PRIVATE
    dap_core
    dap_json
    m
    pthread
)

# Add librt for POSIX timers on Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(benchmark_competitive_full PRIVATE rt)
endif()

target_include_directories(benchmark_competitive_full PRIVATE
    ${CMAKE_SOURCE_DIR}/module/core/include
    ${CMAKE_SOURCE_DIR}/module/json/include
    ${CMAKE_SOURCE_DIR}/module/json/src
)

# Add competitor libraries if found
if(SIMDJSON_INCLUDE_DIR AND SIMDJSON_LIBRARY)
    message("  [✓] simdjson found: ${SIMDJSON_LIBRARY}")
    target_include_directories(benchmark_competitive_full PRIVATE ${SIMDJSON_INCLUDE_DIR})
    target_link_libraries(benchmark_competitive_full PRIVATE ${SIMDJSON_LIBRARY})
    target_compile_definitions(benchmark_competitive_full PRIVATE HAVE_SIMDJSON=1)
else()
    message("  [!] simdjson not found (run download_competitors.sh)")
endif()

if(RAPIDJSON_INCLUDE_DIR)
    message("  [✓] RapidJSON found (header-only)")
    target_include_directories(benchmark_competitive_full PRIVATE ${RAPIDJSON_INCLUDE_DIR})
    target_compile_definitions(benchmark_competitive_full PRIVATE HAVE_RAPIDJSON=1)
else()
    message("  [!] RapidJSON not found (run download_competitors.sh)")
endif()

if(YAJL_INCLUDE_DIR AND YAJL_LIBRARY)
    message("  [✓] yajl found: ${YAJL_LIBRARY}")
    target_include_directories(benchmark_competitive_full PRIVATE ${YAJL_INCLUDE_DIR})
    target_link_libraries(benchmark_competitive_full PRIVATE ${YAJL_LIBRARY})
    target_compile_definitions(benchmark_competitive_full PRIVATE HAVE_YAJL=1)
else()
    message("  [!] yajl not found (run download_competitors.sh)")
endif()

if(JSONC_INCLUDE_DIR AND JSONC_LIBRARY)
    message("  [✓] json-c found: ${JSONC_LIBRARY}")
    # json-c needs both: build dir (for generated headers) and source dir (for arraylist.h, etc.)
    target_include_directories(benchmark_competitive_full PRIVATE 
        ${JSONC_INCLUDE_DIR}  # build directory first (for config.h)
        "${COMPETITORS_DIR}/json-c"  # source directory (for arraylist.h, etc.)
    )
    target_link_libraries(benchmark_competitive_full PRIVATE ${JSONC_LIBRARY})
    target_compile_definitions(benchmark_competitive_full PRIVATE HAVE_JSON_C=1)
else()
    message("  [!] json-c not found (run download_competitors.sh)")
endif()

# ============================================================================
# Stage 1 multi-architecture benchmark
# ============================================================================

add_executable(benchmark_stage1_multi_arch
    benchmark_stage1_multi_arch.c
)

target_link_libraries(benchmark_stage1_multi_arch PRIVATE
    dap_core
    dap_json
)

target_include_directories(benchmark_stage1_multi_arch PRIVATE
    ${CMAKE_SOURCE_DIR}/module/core/include
    ${CMAKE_SOURCE_DIR}/module/json/include
    ${CMAKE_SOURCE_DIR}/module/json/src
    ${CMAKE_SOURCE_DIR}/module/json/src/stage1
)

message("[+] dap_json performance tests configured")
