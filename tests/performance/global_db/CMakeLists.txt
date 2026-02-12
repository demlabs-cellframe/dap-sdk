cmake_minimum_required(VERSION 3.10)
project(benchmark_kv_storage C)

# Auto-detect competitors built by fetch_competitors.sh
set(COMPETITORS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/competitors")
set(COMPETITORS_INSTALL_DIR "${COMPETITORS_DIR}/install")

# If competitors were fetched, auto-enable available backends
if(EXISTS "${COMPETITORS_INSTALL_DIR}/include/mdbx.h")
    option(WITH_MDBX "Build with MDBX support" ON)
else()
    option(WITH_MDBX "Build with MDBX support" OFF)
endif()

if(EXISTS "${COMPETITORS_INSTALL_DIR}/include/lmdb.h")
    option(WITH_LMDB "Build with LMDB support" ON)
else()
    option(WITH_LMDB "Build with LMDB support" OFF)
endif()

if(IS_DIRECTORY "${COMPETITORS_INSTALL_DIR}/include/rocksdb")
    option(WITH_ROCKSDB "Build with RocksDB support" ON)
else()
    option(WITH_ROCKSDB "Build with RocksDB support" OFF)
endif()

if(IS_DIRECTORY "${COMPETITORS_INSTALL_DIR}/include/leveldb")
    option(WITH_LEVELDB "Build with LevelDB support" ON)
else()
    option(WITH_LEVELDB "Build with LevelDB support" OFF)
endif()

set(SOURCES
    benchmark_kv_storage.c
)

add_executable(${PROJECT_NAME} ${SOURCES})

# Use the final shared library — dap_global_db pulls in networking, cluster, stream, etc.
target_link_libraries(${PROJECT_NAME} dap_sdk)

# Add competitors include/lib paths if they exist
if(IS_DIRECTORY "${COMPETITORS_INSTALL_DIR}")
    target_include_directories(${PROJECT_NAME} PRIVATE "${COMPETITORS_INSTALL_DIR}/include")
    link_directories("${COMPETITORS_INSTALL_DIR}/lib")
endif()

# Optional backends
if(WITH_MDBX)
    find_library(MDBX_LIB mdbx HINTS "${COMPETITORS_INSTALL_DIR}/lib")
    if(MDBX_LIB)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_MDBX)
        target_link_libraries(${PROJECT_NAME} ${MDBX_LIB})
        message(STATUS "[benchmark] MDBX support enabled: ${MDBX_LIB}")
    else()
        message(WARNING "[benchmark] MDBX requested but library not found")
    endif()
endif()

if(WITH_LMDB)
    find_library(LMDB_LIB lmdb HINTS "${COMPETITORS_INSTALL_DIR}/lib")
    if(LMDB_LIB)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_LMDB)
        target_link_libraries(${PROJECT_NAME} ${LMDB_LIB})
        message(STATUS "[benchmark] LMDB support enabled: ${LMDB_LIB}")
    else()
        message(WARNING "[benchmark] LMDB requested but library not found")
    endif()
endif()

if(WITH_ROCKSDB)
    find_library(ROCKSDB_LIB rocksdb HINTS "${COMPETITORS_INSTALL_DIR}/lib")
    if(ROCKSDB_LIB)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_ROCKSDB)
        target_link_libraries(${PROJECT_NAME} ${ROCKSDB_LIB})
        # RocksDB requires C++ runtime and pthreads
        target_link_libraries(${PROJECT_NAME} stdc++ pthread dl)
        message(STATUS "[benchmark] RocksDB support enabled: ${ROCKSDB_LIB}")
    else()
        message(WARNING "[benchmark] RocksDB requested but library not found")
    endif()
endif()

if(WITH_LEVELDB)
    find_library(LEVELDB_LIB leveldb HINTS "${COMPETITORS_INSTALL_DIR}/lib")
    if(LEVELDB_LIB)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_LEVELDB)
        target_link_libraries(${PROJECT_NAME} ${LEVELDB_LIB})
        # LevelDB requires C++ runtime and pthreads
        target_link_libraries(${PROJECT_NAME} stdc++ pthread)
        message(STATUS "[benchmark] LevelDB support enabled: ${LEVELDB_LIB}")
    else()
        message(WARNING "[benchmark] LevelDB requested but library not found")
    endif()
endif()

# Link math for timing functions
target_link_libraries(${PROJECT_NAME} m)

# Set RPATH so the executable can find competitor .so files at runtime
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_RPATH "${COMPETITORS_INSTALL_DIR}/lib"
    INSTALL_RPATH "${COMPETITORS_INSTALL_DIR}/lib"
)

# Install
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
