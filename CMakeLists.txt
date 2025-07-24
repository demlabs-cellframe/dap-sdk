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
option(BUILD_TESTS "Build tests" ON)

if(BUILD_SHARED)
    message("[+] Python-DAP: Building as SHARED → embedding static DAP SDK")
    set(PYTHON_DAP_LIB_TYPE MODULE)  # MODULE instead of SHARED for Python extension
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

# =========================================
# PYTHON SETUP 
# =========================================
# Find Python with all needed components
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# =========================================
# PYTHON-DAP EXTENSION MODULE CREATION
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
    src/python_dap_stream.c   # Added stream module
    src/python_dap_http.c     # Added HTTP module
    src/python_gdb.c
)

# Create python_dap as proper Python extension module
if(BUILD_SHARED)
    # Use Python3_add_library for proper Python extension
    Python3_add_library(python_dap MODULE ${PYTHON_DAP_SOURCES})
    
    # Set proper extension properties
    set_target_properties(python_dap PROPERTIES
        PREFIX ""
        OUTPUT_NAME "python_dap"
        SUFFIX ".${Python3_SOABI}.so"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib"
        POSITION_INDEPENDENT_CODE ON
            VERSION 3.0.0
    SOVERSION 3
    )
    
    target_compile_definitions(python_dap PRIVATE PYTHON_DAP_BUILDING_SHARED)
    target_compile_definitions(python_dap INTERFACE PYTHON_DAP_USING_SHARED)
else()
    # Static library for plugin integration
    add_library(python_dap STATIC ${PYTHON_DAP_SOURCES})
    target_compile_definitions(python_dap INTERFACE PYTHON_DAP_USING_STATIC)
endif()

# =========================================
# CONDITIONAL SDK LINKING
# =========================================
if(PYTHON_DAP_EMBED_SDK)
    message("[+] Python-DAP: Linking with embedded static DAP SDK")
    
    # Link with final DAP SDK library (contains all modules)
    # Link all libraries in one command for proper static linking order
    target_link_libraries(python_dap PRIVATE 
        dap_sdk 
        dap_crypto_XKCP 
        dap_crypto_kyber512 
        dap_link_manager 
        dap_core_unix 
        dap_json-c 
        rt
        pthread
    )
    
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

# Python linking - only link Python for extension modules
if(BUILD_SHARED)
    target_link_libraries(python_dap PRIVATE ${Python3_LIBRARIES})
endif()

# Add XKCP configuration definitions
target_compile_definitions(python_dap PRIVATE
    XKCP_has_KeccakP1600=1
    KeccakP1600_implementation=reference
    KeccakP1600_reference=1
    KeccakP1600_useLaneComplementing=0
)

# =========================================
# VIRTUAL ENVIRONMENT SETUP
# =========================================
# Create virtual environment if it doesn't exist
set(VENV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/venv")
set(VENV_PYTHON "${VENV_DIR}/bin/python")
set(VENV_PIP "${VENV_DIR}/bin/pip")

# Custom target to create virtual environment
add_custom_target(venv
    COMMAND test -d ${VENV_DIR} || ${Python3_EXECUTABLE} -m venv ${VENV_DIR}
    COMMAND ${VENV_PIP} install --upgrade pip
    COMMENT "Creating/updating virtual environment"
    VERBATIM
)

# =========================================
# TESTING SUPPORT
# =========================================
if(BUILD_TESTS)
    enable_testing()
    
    # Install pytest if not available
    add_custom_target(pytest_install
        DEPENDS venv
        COMMAND ${VENV_PIP} install pytest pytest-timeout pytest-cov build wheel
        COMMENT "Installing pytest and build dependencies"
        VERBATIM
    )
    
    # Clean previous package builds
    add_custom_target(clean_package
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_SOURCE_DIR}/build
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_SOURCE_DIR}/dist
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_SOURCE_DIR}/python_dap.egg-info
        COMMENT "Cleaning previous package builds"
        VERBATIM
    )
    
    # Build Python package (wheel)
    add_custom_target(python_package
        DEPENDS python_dap pytest_install clean_package
        COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR} && ${VENV_PYTHON} setup.py bdist_wheel
        COMMENT "Building Python wheel package"
        VERBATIM
    )
    
    # Install package in virtual environment for testing
    add_custom_target(test_setup
        DEPENDS python_dap pytest_install
        # Build the wheel package
        COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR} && ${VENV_PYTHON} -m build --wheel
        # Uninstall any existing version first
        COMMAND ${VENV_PIP} uninstall -y python-dap || true
        # Install the built wheel package
        COMMAND ${VENV_PIP} install --force-reinstall --find-links ${CMAKE_CURRENT_SOURCE_DIR}/dist python-dap
        COMMENT "Building and installing python-dap wheel package in virtual environment"
        VERBATIM
    )
    
    # Unit tests
    add_custom_target(unit_tests
        DEPENDS test_setup
        COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR}/tests && ${VENV_PYTHON} -m pytest unit/ -v
        COMMENT "Running unit tests"
        VERBATIM
    )
    
    # Integration tests  
    add_custom_target(integration_tests
        DEPENDS test_setup
        COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR}/tests && ${VENV_PYTHON} -m pytest integration/ -v
        COMMENT "Running integration tests"
        VERBATIM
    )
    
    # Regression tests
    add_custom_target(regression_tests
        DEPENDS test_setup
        COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR}/tests && ${VENV_PYTHON} -m pytest regression/ -v
        COMMENT "Running regression tests"
        VERBATIM
    )
    
    # All tests together
    add_custom_target(python_tests
        DEPENDS test_setup
        COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR}/tests && ${VENV_PYTHON} -m pytest -v
        COMMENT "Running all Python DAP tests"
        VERBATIM
    )
    
    # Create test virtual environment target
    add_custom_target(create_test_venv
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_SOURCE_DIR}/test_venv || true
        COMMAND ${Python3_EXECUTABLE} -m venv ${CMAKE_CURRENT_SOURCE_DIR}/test_venv
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_venv/bin/pip install --upgrade pip
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_venv/bin/pip install pytest pytest-timeout pytest-cov build wheel
        COMMENT "Creating fresh test virtual environment"
        VERBATIM
    )

    # Build and install python-dap package in test environment
    add_custom_target(install_test_package
        DEPENDS python_dap create_test_venv
        # First build the wheel package
        COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR} && ${CMAKE_CURRENT_SOURCE_DIR}/test_venv/bin/python -m build --wheel
        # Install the built wheel package
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_venv/bin/pip install --force-reinstall --find-links ${CMAKE_CURRENT_SOURCE_DIR}/dist python-dap
        COMMENT "Building and installing python-dap wheel package in test virtual environment"
        VERBATIM
    )

    # Add individual CTest entries - simple and parallel
    add_test(
        NAME python_dap_unit_tests
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_venv/bin/python -m pytest tests/unit/ -v
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    add_test(
        NAME python_dap_integration_tests  
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_venv/bin/python -m pytest tests/integration/ -v
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    add_test(
        NAME python_dap_regression_tests  
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_venv/bin/python -m pytest tests/regression/ -v
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
# REMOVED: python_dap_all_tests - causes conflicts when running all test types together
    # Individual test types (unit, integration, regression) work perfectly when run separately
    
    # Force STRICT SEQUENTIAL execution - prevent race condition between test types
    set_tests_properties(python_dap_unit_tests PROPERTIES 
        TIMEOUT 300
        FIXTURES_REQUIRED test_environment
        RUN_SERIAL TRUE
    )
    set_tests_properties(python_dap_integration_tests PROPERTIES 
        TIMEOUT 600
        FIXTURES_REQUIRED test_environment
        RUN_SERIAL TRUE
        DEPENDS python_dap_unit_tests
    )
    set_tests_properties(python_dap_regression_tests PROPERTIES 
        TIMEOUT 300
        FIXTURES_REQUIRED test_environment
        RUN_SERIAL TRUE
        DEPENDS python_dap_integration_tests
    )
# REMOVED: Properties for python_dap_all_tests - test no longer exists

    # Create test fixture to ensure environment is ready
    add_test(
        NAME test_environment_setup
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target install_test_package
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(test_environment_setup PROPERTIES
        FIXTURES_SETUP test_environment
        TIMEOUT 120
    )

endif()

# =========================================
# PACKAGING SUPPORT (CPack)
# =========================================
# Package configuration
set(CPACK_PACKAGE_NAME "python-dap")
set(CPACK_PACKAGE_VERSION "3.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Python bindings for DAP SDK")
set(CPACK_PACKAGE_VENDOR "Demlabs")
set(CPACK_PACKAGE_CONTACT "support@demlabs.net")

# Package files
install(TARGETS python_dap
    LIBRARY DESTINATION lib/python3/dist-packages
    ARCHIVE DESTINATION lib
)

install(FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/python_dap.so
    DESTINATION lib/python3/dist-packages
    OPTIONAL
)

install(DIRECTORY 
    ${CMAKE_CURRENT_SOURCE_DIR}/dap/
    DESTINATION lib/python3/dist-packages/dap
    FILES_MATCHING PATTERN "*.py"
)

install(FILES
    README.md
    LICENSE  
    DESTINATION share/doc/python-dap
    OPTIONAL
)

# Debian package specific
set(CPACK_DEBIAN_PACKAGE_DEPENDS "python3 (>= 3.8), python3-dev")
set(CPACK_DEBIAN_PACKAGE_SECTION "python")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# CPack generators
set(CPACK_GENERATOR "DEB;TGZ")
include(CPack)

# =========================================
# CUSTOM BUILD TARGETS
# =========================================
# Target to build and install in venv
add_custom_target(install_venv
    DEPENDS python_package venv
    # Uninstall any existing version first
    COMMAND ${VENV_PIP} uninstall -y python-dap || true
    # Install the newly built wheel
    COMMAND ${VENV_PIP} install --force-reinstall --find-links ${CMAKE_CURRENT_SOURCE_DIR}/dist python-dap
    COMMAND ${VENV_PYTHON} -c "import python_dap; print(f'✅ Module loaded: {len(dir(python_dap))} functions')"
    COMMENT "Installing python-dap package in virtual environment"
    VERBATIM
)

# Target to validate installation
add_custom_target(validate_install
    DEPENDS install_venv
    COMMAND ${VENV_PYTHON} -c "import python_dap; events_funcs = [f for f in dir(python_dap) if 'dap_events' in f]; print(f'Events functions: {len(events_funcs)}'); print('✅ SUCCESS: All events functions available' if len(events_funcs) >= 10 else '❌ ERROR: Missing events functions'); exit(0 if len(events_funcs) >= 10 else 1)"
    COMMENT "Validating python_dap installation"
    VERBATIM
)

# =========================================
# PYTHON-DAP ARCHITECTURE SUMMARY
# =========================================
message("[+] ===========================================")
message("[+] PYTHON-DAP BUILD ARCHITECTURE:")
message("[+] - Library mode: ${PYTHON_DAP_BUILD_MODE}")
message("[+] - Library type: ${PYTHON_DAP_LIB_TYPE}")
if(PYTHON_DAP_EMBED_SDK)
    message("[+] - DAP SDK: embedded static (${DAPSDK_MODULES})")
    message("[+] - SDK linking: dap_sdk (final combined library)")
else()
    message("[+] - DAP SDK: NOT embedded (static mode)")
    message("[+] - SDK linking: NONE (for plugin integration)")
endif()
message("[+] - Python version: ${Python3_VERSION}")
message("[+] - Python include: ${Python3_INCLUDE_DIRS}")
message("[+] - Python libraries: ${Python3_LIBRARIES}")
message("[+] - Virtual env: ${VENV_DIR}")
if(BUILD_TESTS)
    message("[+] - Tests: ENABLED")
    message("[+] - Build pipeline:")
    message("[+]   1. make python_dap - compile C extension")
    message("[+]   2. make python_package - build wheel package")
    message("[+]   3. make test_setup - install package in venv")
    message("[+]   4. make python_tests - run tests")
    message("[+] - Test targets:")
    message("[+]   • make unit_tests - run unit tests") 
    message("[+]   • make integration_tests - run integration tests")
    message("[+]   • make regression_tests - run regression tests")
    message("[+]   • make python_tests - run ALL tests")
    message("[+]   • ctest - run all tests via CTest")
    message("[+]   • ctest -R unit - run only unit tests")
    message("[+]   • ctest -R integration - run only integration tests")
endif()
message("[+] - Package targets:")
message("[+]   • make python_package - build wheel package")
message("[+]   • make install_venv - build and install in venv")
message("[+]   • cpack - create system distribution package")
message("[+] ===========================================")
