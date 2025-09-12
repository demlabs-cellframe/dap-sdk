cmake_minimum_required(VERSION 3.10)
project (dap_core)

add_definitions("-D_GNU_SOURCE") 

file (GLOB CORE_HEADERS include/*.h)
file (GLOB CORE_SRCS src/*.c src/etc/*.c)

if(UNIX)
    file (GLOB CORE_HEADERS ${CORE_HEADERS} include/unix/*.h src/unix/*.h)
    file (GLOB CORE_SRCS ${CORE_SRCS} src/unix/*.c)
    if(LINUX)
        file (GLOB CORE_HEADERS ${CORE_HEADERS} src/unix/linux/*.h)
        file (GLOB CORE_SRCS ${CORE_SRCS} src/unix/linux/*.c)
    endif()
elseif(WIN32)
    file (GLOB CORE_SRCS ${CORE_SRCS} src/win32/*.c ../3rdparty/strptime.c)
    set_source_files_properties(../3rdparty/strptime PROPERTIES COMPILE_FLAGS -Wno-deprecated-declarations)
endif()

if (DAP_USE_RPMALLOC)
    message("[!] rpmalloc ON")
    file (GLOB CORE_HEADERS ${CORE_HEADERS} ../3rdparty/rpmalloc/rpmalloc/rpmalloc.h)
    file (GLOB CORE_SRCS ${CORE_SRCS} ../3rdparty/rpmalloc/rpmalloc/rpmalloc.c)
endif()

add_library(${PROJECT_NAME} STATIC ${CORE_SRCS} ${CORE_HEADERS})

# Define XKCP configuration for hash functions
add_definitions(-DXKCP_has_KeccakP1600)

#This paths will be used by project-dependent project libraries
# Cross-platform KeccakP-1600 implementation selection
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(KECCAK_IMPL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../crypto/XKCP/lib/low/KeccakP-1600/x86-64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(KECCAK_IMPL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../crypto/XKCP/lib/low/KeccakP-1600/ARMv8A")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    set(KECCAK_IMPL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../crypto/XKCP/lib/low/KeccakP-1600/ARM")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # 64-bit fallback
    set(KECCAK_IMPL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../crypto/XKCP/lib/low/KeccakP-1600/ref-64bits")
else()
    # 32-bit fallback
    set(KECCAK_IMPL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../crypto/XKCP/lib/low/KeccakP-1600/ref-32bits")
endif()

target_include_directories(${PROJECT_NAME} PUBLIC . 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../crypto/XKCP/lib/high/Keccak/FIPS202
    ${CMAKE_CURRENT_SOURCE_DIR}/../crypto/XKCP/lib/high/Keccak
    ${CMAKE_CURRENT_SOURCE_DIR}/../crypto/XKCP/lib/common
    ${KECCAK_IMPL_PATH})
if (DAP_USE_RPMALLOC)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/rpmalloc/rpmalloc/)
    target_compile_definitions(${PROJECT_NAME} PUBLIC DAP_USE_RPMALLOC ENABLE_OVERRIDE=1 ENABLE_PRELOAD=1)
endif()
if(WIN32)
    include_directories(include/)
    add_subdirectory(src/win32)
    set(CORE_LIBRARIES dap_core_win32)
endif()

if(UNIX)
    add_subdirectory(src/unix)
    set(CORE_LIBRARIES dap_core_unix)
    if(LINUX)
        set(CORE_LIBRARIES ${CORE_LIBRARIES} rt)
    endif() 
endif()

if (BUILD_WITH_ZIP)
    set(CORE_LIBRARIES ${CORE_LIBRARIES} zip)
    add_definitions ("-DDAP_BUILD_WITH_ZIP")
endif()
add_subdirectory(../3rdparty/json-c ${CMAKE_CURRENT_BINARY_DIR}/json-c)
target_link_libraries(${PROJECT_NAME} ${CORE_LIBRARIES} dap_json-c)

# Add dependency on XKCP for hash functions used by security monitor
if (DAPSDK_MODULES MATCHES "crypto")
    add_dependencies(${PROJECT_NAME} dap_crypto_XKCP)
endif()

if(BUILD_DAP_TESTS)
    add_definitions ("-DDAP_CORE_TESTS")
    message("Enabled dap_core test")
    enable_testing()
    add_definitions("-DDAP_NETWORK_MONITOR_TEST_OFF")
    add_subdirectory(test)
endif()

if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER "${CORE_HEADERS}")
    INSTALL(TARGETS ${PROJECT_NAME} 
            LIBRARY DESTINATION lib/dap/core
            ARCHIVE DESTINATION lib/dap/core
            PUBLIC_HEADER DESTINATION include/dap/core/
    )


    INSTALL(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src/  DESTINATION include/dap/core/ FILES_MATCHING PATTERN "*.h")
    #if (DAP_OS_LINUX)
    #INSTALL(DIRECTORY src/unix/linux/  DESTINATION include/dap/core/ FILES_MATCHING PATTERN "*.h")
    #endif()
endif()
