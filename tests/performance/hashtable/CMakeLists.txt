# DAP SDK Hash Table Performance Benchmarks
cmake_minimum_required(VERSION 3.10)

project(benchmark_hashtable C)

# Main benchmark executable (dap_ht only - no competitors needed)
add_executable(${PROJECT_NAME}
    benchmark_hashtable.c
)

# Link DAP SDK
target_link_libraries(${PROJECT_NAME} PRIVATE dap_sdk)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../module/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Option to enable competitor benchmarks
option(BENCHMARK_COMPETITORS "Include competitor hash table implementations in benchmark" OFF)

if(BENCHMARK_COMPETITORS)
    # Check if competitors are downloaded
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/competitors/uthash.h")
        target_compile_definitions(${PROJECT_NAME} PRIVATE BENCHMARK_UTHASH)
        message(STATUS "[benchmark_hashtable] uthash enabled")
    else()
        message(STATUS "[benchmark_hashtable] uthash not found - run download_competitors.sh")
    endif()
    
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/competitors/khash.h")
        target_compile_definitions(${PROJECT_NAME} PRIVATE BENCHMARK_KHASH)
        message(STATUS "[benchmark_hashtable] khash enabled")
    else()
        message(STATUS "[benchmark_hashtable] khash not found - run download_competitors.sh")
    endif()
    
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/competitors/stb_ds.h")
        target_compile_definitions(${PROJECT_NAME} PRIVATE BENCHMARK_STB_DS)
        message(STATUS "[benchmark_hashtable] stb_ds enabled")
    else()
        message(STATUS "[benchmark_hashtable] stb_ds not found - run download_competitors.sh")
    endif()
    
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/competitors
    )
endif()

# Add as CTest
add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES
    TIMEOUT ${DAP_T300}
    LABELS "performance;hashtable"
)

# Install download script
install(PROGRAMS download_competitors.sh 
    DESTINATION share/dap-sdk/benchmarks/hashtable
)
