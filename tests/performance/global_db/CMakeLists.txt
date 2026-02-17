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

if(EXISTS "${COMPETITORS_INSTALL_DIR}/include/tidesdb/db.h")
    option(WITH_TIDESDB "Build with TidesDB support" ON)
else()
    option(WITH_TIDESDB "Build with TidesDB support" OFF)
endif()

if(EXISTS "${COMPETITORS_INSTALL_DIR}/include/wiredtiger.h")
    option(WITH_WIREDTIGER "Build with WiredTiger support" ON)
else()
    option(WITH_WIREDTIGER "Build with WiredTiger support" OFF)
endif()

if(EXISTS "${COMPETITORS_INSTALL_DIR}/include/sophia.h")
    option(WITH_SOPHIA "Build with Sophia support" ON)
else()
    option(WITH_SOPHIA "Build with Sophia support" OFF)
endif()

set(SOURCES
    benchmark_kv_storage.c
)

add_executable(${PROJECT_NAME} ${SOURCES})

# Use the final shared library — dap_global_db pulls in networking, cluster, stream, etc.
target_link_libraries(${PROJECT_NAME} dap_sdk pthread)

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

if(WITH_TIDESDB)
    find_library(TIDESDB_LIB tidesdb HINTS "${COMPETITORS_INSTALL_DIR}/lib")
    if(TIDESDB_LIB)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_TIDESDB)
        target_link_libraries(${PROJECT_NAME} ${TIDESDB_LIB})
        # TidesDB requires compression libraries
        target_link_libraries(${PROJECT_NAME} zstd lz4 snappy pthread)
        message(STATUS "[benchmark] TidesDB support enabled: ${TIDESDB_LIB}")
    else()
        message(WARNING "[benchmark] TidesDB requested but library not found")
    endif()
endif()

if(WITH_WIREDTIGER)
    find_library(WIREDTIGER_LIB wiredtiger HINTS "${COMPETITORS_INSTALL_DIR}/lib")
    if(WIREDTIGER_LIB)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_WIREDTIGER)
        target_link_libraries(${PROJECT_NAME} ${WIREDTIGER_LIB})
        target_link_libraries(${PROJECT_NAME} pthread dl)
        message(STATUS "[benchmark] WiredTiger support enabled: ${WIREDTIGER_LIB}")
    else()
        message(WARNING "[benchmark] WiredTiger requested but library not found")
    endif()
endif()

if(WITH_SOPHIA)
    find_library(SOPHIA_LIB sophia HINTS "${COMPETITORS_INSTALL_DIR}/lib")
    if(SOPHIA_LIB)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_SOPHIA)
        target_link_libraries(${PROJECT_NAME} ${SOPHIA_LIB})
        target_link_libraries(${PROJECT_NAME} pthread)
        message(STATUS "[benchmark] Sophia support enabled: ${SOPHIA_LIB}")
    else()
        message(WARNING "[benchmark] Sophia requested but library not found")
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
