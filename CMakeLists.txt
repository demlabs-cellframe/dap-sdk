cmake_minimum_required(VERSION 3.10)
project(python-dap C)

set(CMAKE_C_STANDARD 11)

# =========================================
# PYTHON-DAP CONDITIONAL BUILD SYSTEM
# =========================================
# Python-dap conditional SDK embedding:
# - If BUILD_SHARED=ON (dynamic) → embeds static DAP SDK from submodule  
# - If BUILD_SHARED=OFF (static) → embeds nothing (for plugin integration)

# Build configuration options
option(BUILD_SHARED "Build as shared library (standalone mode)" ON)
option(BUILD_STATIC "Build as static library (for integration)" OFF)
option(BUILD_TESTS "Build tests" OFF)

if(BUILD_SHARED)
    message("[+] Python-DAP: Building as SHARED → embedding static DAP SDK")
    set(PYTHON_DAP_LIB_TYPE SHARED)
    set(PYTHON_DAP_EMBED_SDK TRUE)
    set(PYTHON_DAP_BUILD_MODE "shared")
else()
    message("[+] Python-DAP: Building as STATIC → NO SDK embedding (for plugin integration)")
    set(PYTHON_DAP_LIB_TYPE STATIC) 
    set(PYTHON_DAP_EMBED_SDK FALSE)
    set(PYTHON_DAP_BUILD_MODE "static")
endif()

# =========================================
# CONDITIONAL DAP SDK INTEGRATION  
# =========================================
if(PYTHON_DAP_EMBED_SDK)
    message("[+] Python-DAP: Force building DAP SDK as STATIC for embedding")
    
    # Force DAP SDK to build as static for embedding
    set(BUILD_SHARED OFF CACHE BOOL "Force DAP SDK to static for python-dap" FORCE)
    
    # Configure DAP SDK modules we need - complete set for full functionality
    set(DAPSDK_MODULES "core crypto io global-db network-core network-client network-server network-stream network-link_manager")
    message("[+] Python-DAP: DAPSDK_MODULES: ${DAPSDK_MODULES}")
    
    # Include dap-sdk as static
    add_subdirectory(dap-sdk)
    
    # Restore BUILD_SHARED setting for python-dap itself
    set(BUILD_SHARED ON CACHE BOOL "Restore for python-dap" FORCE)
    
    message("[+] Python-DAP: DAP SDK embedded as static")
else()
    message("[+] Python-DAP: NO SDK embedding - static mode for plugin integration")
endif()

# Find Python
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# =========================================
# PYTHON-DAP LIBRARY CREATION
# =========================================
# Core python_dap source files
set(PYTHON_DAP_SOURCES
    src/python_dap.c
    src/python_dap_core.c
    src/python_dap_config.c
    src/python_dap_crypto.c  # Re-enabled
    src/python_dap_network.c
    src/python_dap_system.c
    src/python_dap_common.c
    src/python_dap_time.c
    src/python_dap_logging.c
    src/python_dap_events.c
    src/python_dap_client.c
    src/python_dap_server.c
    src/python_gdb.c
)

# Create python_dap library with conditional type
add_library(python_dap ${PYTHON_DAP_LIB_TYPE} ${PYTHON_DAP_SOURCES})

# Set properties based on build mode
if(BUILD_SHARED)
    set_target_properties(python_dap PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        VERSION 2.4.0
        SOVERSION 2
    )
    target_compile_definitions(python_dap PRIVATE PYTHON_DAP_BUILDING_SHARED)
    target_compile_definitions(python_dap INTERFACE PYTHON_DAP_USING_SHARED)
else()
    target_compile_definitions(python_dap INTERFACE PYTHON_DAP_USING_STATIC)
endif()

# =========================================
# CONDITIONAL SDK LINKING
# =========================================
if(PYTHON_DAP_EMBED_SDK)
    message("[+] Python-DAP: Linking with embedded static DAP SDK")
    
    # Link with final DAP SDK library (contains all modules)
    # Link all libraries in one command for proper static linking order
    target_link_libraries(python_dap PRIVATE -Wl,--export-dynamic dap_sdk dap_crypto_XKCP dap_crypto_kyber512 dap_link_manager dap_core_unix dap_json-c rt)
    
    # Include DAP SDK directories
    target_include_directories(python_dap PRIVATE
        dap-sdk/core/include
        dap-sdk/crypto/include
        dap-sdk/crypto/XKCP/lib/high/Keccak/FIPS202
        dap-sdk/crypto/XKCP/lib/high/Keccak
        dap-sdk/crypto/XKCP/lib/low
        dap-sdk/crypto/XKCP/lib/low/KeccakP-1600/AVX2
        dap-sdk/crypto/XKCP/lib/low/KeccakP-1600/x86-64
        dap-sdk/io/include
        dap-sdk/global-db/include
        dap-sdk/net/client/include
        dap-sdk/net/server/http_server/include
        dap-sdk/net/stream/stream/include
        dap-sdk/net/stream/ch/include
        dap-sdk/net/stream/session/include
        dap-sdk/3rdparty/uthash/src
    )
else()
    message("[+] Python-DAP: NO SDK linking - static mode for plugin integration")
    # In static mode, no SDK linking - plugin will provide SDK access
endif()

# Common includes
target_include_directories(python_dap PUBLIC
    ${Python3_INCLUDE_DIRS}
    include
)

# Python linking
target_link_libraries(python_dap PRIVATE -Wl,--export-dynamic ${Python3_LIBRARIES})

# Add XKCP configuration definitions
target_compile_definitions(python_dap PRIVATE
    XKCP_has_KeccakP1600=1
    KeccakP1600_implementation=reference
    KeccakP1600_reference=1
    KeccakP1600_useLaneComplementing=0
)

# Set library properties
set_target_properties(python_dap PROPERTIES
    PREFIX ""
    OUTPUT_NAME "python_dap"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib"
)

# Install library
install(TARGETS python_dap
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# Tests
if(BUILD_TESTS)
    enable_testing()
    # add_subdirectory(tests/c_tests)
endif()

# =========================================
# PYTHON-DAP ARCHITECTURE SUMMARY
# =========================================
message("[+] ===========================================")
message("[+] PYTHON-DAP BUILD ARCHITECTURE:")
message("[+] - Library mode: ${PYTHON_DAP_BUILD_MODE}")
if(PYTHON_DAP_EMBED_SDK)
    message("[+] - DAP SDK: embedded static (${DAPSDK_MODULES})")
    message("[+] - SDK linking: dap_sdk (final combined library)")
else()
    message("[+] - DAP SDK: NOT embedded (static mode)")
    message("[+] - SDK linking: NONE (for plugin integration)")
endif()
message("[+] - Python linking: ${Python3_LIBRARIES}")
message("[+] ===========================================")
