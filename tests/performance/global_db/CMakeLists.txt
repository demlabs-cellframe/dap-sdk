cmake_minimum_required(VERSION 3.10)
project(benchmark_kv_storage C)

# Options for optional backends
option(WITH_MDBX "Build with MDBX support" OFF)
option(WITH_LMDB "Build with LMDB support" OFF)
option(WITH_ROCKSDB "Build with RocksDB support" OFF)
option(WITH_LEVELDB "Build with LevelDB support" OFF)

set(SOURCES
    benchmark_kv_storage.c
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME}
    dap_global_db
    dap_core
    dap_crypto
)

# Optional backends
if(WITH_MDBX)
    find_library(MDBX_LIB mdbx)
    if(MDBX_LIB)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_MDBX)
        target_link_libraries(${PROJECT_NAME} ${MDBX_LIB})
        message(STATUS "MDBX support enabled")
    else()
        message(WARNING "MDBX library not found")
    endif()
endif()

if(WITH_LMDB)
    find_library(LMDB_LIB lmdb)
    if(LMDB_LIB)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_LMDB)
        target_link_libraries(${PROJECT_NAME} ${LMDB_LIB})
        message(STATUS "LMDB support enabled")
    else()
        message(WARNING "LMDB library not found")
    endif()
endif()

if(WITH_ROCKSDB)
    find_library(ROCKSDB_LIB rocksdb)
    if(ROCKSDB_LIB)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_ROCKSDB)
        target_link_libraries(${PROJECT_NAME} ${ROCKSDB_LIB})
        message(STATUS "RocksDB support enabled")
    else()
        message(WARNING "RocksDB library not found")
    endif()
endif()

if(WITH_LEVELDB)
    find_library(LEVELDB_LIB leveldb)
    if(LEVELDB_LIB)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_LEVELDB)
        target_link_libraries(${PROJECT_NAME} ${LEVELDB_LIB})
        message(STATUS "LevelDB support enabled")
    else()
        message(WARNING "LevelDB library not found")
    endif()
endif()

# Link math for timing functions
target_link_libraries(${PROJECT_NAME} m)

# Install
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
