cmake_minimum_required(VERSION 3.12)
project (dap_core)

# Include required CMake modules for register_module_headers function
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PackageComponents.cmake)

add_definitions("-D_GNU_SOURCE") 

file (GLOB CORE_HEADERS include/*.h)
# Exclude dap_json.h - moved to module/json
list(FILTER CORE_HEADERS EXCLUDE REGEX ".*dap_json\\.h$")

file (GLOB CORE_SRCS src/*.c src/etc/*.c)
# Exclude dap_json.c - moved to module/json
list(FILTER CORE_SRCS EXCLUDE REGEX ".*dap_json\\.c$")

if(UNIX)
    file (GLOB CORE_HEADERS ${CORE_HEADERS} include/unix/*.h src/unix/*.h)
    file (GLOB CORE_SRCS ${CORE_SRCS} src/unix/*.c)

    if(MACOS)
        file (GLOB CORE_HEADERS ${CORE_HEADERS} src/darwin/macos/*.h)
        file (GLOB CORE_SRCS ${CORE_SRCS} src/darwin/macos/*.c)
    elseif(LINUX)
        file (GLOB CORE_HEADERS ${CORE_HEADERS} src/unix/linux/*.h)
        file (GLOB CORE_SRCS ${CORE_SRCS} src/unix/linux/*.c)
    endif()
elseif(WIN32)
    file (GLOB CORE_SRCS ${CORE_SRCS} src/win32/*.c)
    # dap_strptime.c is now in src/ and included via glob
endif()

# rpmalloc removed - was optional allocator, never enabled by default
# If custom allocator needed, implement via dap_alloc callbacks

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${CORE_SRCS} HEADERS ${CORE_HEADERS})

# Register headers for component-based installation
register_module_headers(DIRECTORIES include)

#This paths will be used by project-dependent project libraries
target_include_directories(${PROJECT_NAME} PUBLIC . 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/uthash/src)

# Platform-specific include directories
if(UNIX)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/unix)
endif()
if(WIN32)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/win32)
endif()
# rpmalloc include/definitions removed - see comment above
if(WIN32)
    include_directories(include/)
    add_subdirectory(src/win32)
    set(CORE_LIBRARIES dap_core_win32)
endif()

if(UNIX)
    add_subdirectory(src/unix)
    set(CORE_LIBRARIES dap_core_unix)
    if(MACOS)
        add_subdirectory(src/darwin)
        set(CORE_LIBRARIES ${CORE_LIBRARIES} dap_core_darwin)
    elseif(NOT ANDROID AND NOT APPLE)
        # rt (realtime) library is only needed on Linux, not on Apple platforms (macOS/iOS/tvOS/watchOS)
        set(CORE_LIBRARIES ${CORE_LIBRARIES} rt)
    endif() 
endif()

if (BUILD_WITH_ZIP)
    set(CORE_LIBRARIES ${CORE_LIBRARIES} zip)
    add_definitions ("-DDAP_BUILD_WITH_ZIP")
endif()
# json-c is now part of module/json - no longer included here

# Link macOS system frameworks (required by dap_network_monitor.c compiled into dap_core)
if(APPLE)
    target_link_libraries(${PROJECT_NAME} INTERFACE
        "-framework CoreFoundation"
        "-framework SystemConfiguration"
    )
endif()

# Store dependencies for final library - no linking of OBJECT targets
set(DAP_CORE_DEPS "${CORE_LIBRARIES}" CACHE INTERNAL "dap_core external dependencies")

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

    #if (DAP_OS_LINUX)
    #INSTALL(DIRECTORY src/unix/linux/  DESTINATION include/dap/core/ FILES_MATCHING PATTERN "*.h")
    #INSTALL(DIRECTORY src/unix/  DESTINATION include/dap/core/ FILES_MATCHING PATTERN "*.h")
    #endif()
endif()

