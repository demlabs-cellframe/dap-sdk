cmake_minimum_required(VERSION 3.10)
project (dap_core)

# Include LibraryHelpers for object library creation
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LibraryHelpers.cmake)

add_definitions("-D_GNU_SOURCE") 

file (GLOB CORE_HEADERS include/*.h)
file (GLOB CORE_SRCS src/*.c src/etc/*.c)

if(UNIX)
    file (GLOB CORE_HEADERS ${CORE_HEADERS} include/unix/*.h src/unix/*.h)
    # DO NOT compile src/unix/*.c here - they are compiled in dap_core_unix
    # file (GLOB CORE_SRCS ${CORE_SRCS} src/unix/*.c)
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

# Create OBJECT library instead of STATIC
create_object_library(${PROJECT_NAME} DAP_INTERNAL_MODULES ${CORE_SRCS} HEADERS ${CORE_HEADERS})
#This paths will be used by project-dependent project libraries
target_include_directories(${PROJECT_NAME} PUBLIC . 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src)
# json-c headers are needed for compilation
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/json-c)
# json-c config.h is generated in build directory
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/json-c)
if (DAP_USE_RPMALLOC)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/rpmalloc/rpmalloc/)
    target_compile_definitions(${PROJECT_NAME} PUBLIC DAP_USE_RPMALLOC ENABLE_OVERRIDE=1 ENABLE_PRELOAD=1)
endif()
if(WIN32)
    include_directories(include/)
    add_subdirectory(src/win32)
    # dap_core_win32 is now OBJECT library
    if(TARGET dap_core_win32)
        target_link_libraries(${PROJECT_NAME} INTERFACE dap_core_win32)
    endif()
endif()

if(UNIX)
    add_subdirectory(src/unix)
    # dap_core_unix is now OBJECT library
    if(TARGET dap_core_unix)
        target_link_libraries(${PROJECT_NAME} INTERFACE dap_core_unix)
    endif()
    # rt is a system library, will be linked at final stage
    if(LINUX)
        list(APPEND CORE_LIBRARIES rt)
    endif() 
endif()

if (BUILD_WITH_ZIP)
    set(CORE_LIBRARIES ${CORE_LIBRARIES} zip)
    add_definitions ("-DDAP_BUILD_WITH_ZIP")
endif()
add_subdirectory(../3rdparty/json-c ${CMAKE_CURRENT_BINARY_DIR}/json-c)
# For OBJECT libraries, target_link_libraries only propagates interfaces
# External libraries (like dap_json-c) will be linked at final library stage
# System libraries (like rt) are handled by create_final_shared_library
target_link_libraries(${PROJECT_NAME} INTERFACE ${CORE_LIBRARIES})
# dap_json-c is an OBJECT library, link it as interface
if(TARGET dap_json-c)
    target_link_libraries(${PROJECT_NAME} INTERFACE dap_json-c)
endif()

# Old tests moved to dap-sdk/tests/unit/core/
# Using new test infrastructure with GoogleTest

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
