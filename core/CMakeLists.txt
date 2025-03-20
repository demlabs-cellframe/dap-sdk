cmake_minimum_required(VERSION 3.10)
project (dap_core)

add_definitions("-D_GNU_SOURCE") 

file (GLOB CORE_HEADERS include/*.h)
file (GLOB CORE_SRCS src/*.c src/etc/*.c)

if(UNIX)
    file (GLOB CORE_HEADERS ${CORE_HEADERS} include/unix/*.h src/unix/*.h)
    file (GLOB CORE_SRCS ${CORE_SRCS} src/unix/*.c)

    if(DARWIN)
        file (GLOB CORE_HEADERS ${CORE_HEADERS} src/darwin/macos/*.h)
        file (GLOB CORE_SRCS ${CORE_SRCS} src/darwin/macos/*.c)
    else()
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
#This paths will be used by project-dependent project libraries
target_include_directories(${PROJECT_NAME} PUBLIC . 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/uthash/src)
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
    if(DARWIN)
        add_subdirectory(src/darwin)
        set(CORE_LIBRARIES ${CORE_LIBRARIES} dap_core_darwin)
    elseif(NOT ANDROID)
        set(CORE_LIBRARIES ${CORE_LIBRARIES} rt)
    endif() 
endif()

if (BUILD_WITH_ZIP)
    set(CORE_LIBRARIES ${CORE_LIBRARIES} zip)
    add_definitions ("-DDAP_BUILD_WITH_ZIP")
endif()

target_link_libraries(${PROJECT_NAME} ${CORE_LIBRARIES} dap_json-c)

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
    #INSTALL(DIRECTORY src/unix/  DESTINATION include/dap/core/ FILES_MATCHING PATTERN "*.h")
    #endif()
endif()

