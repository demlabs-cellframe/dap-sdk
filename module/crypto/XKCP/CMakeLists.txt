cmake_minimum_required(VERSION 3.13)
include (../../../cmake/TargetArch_Detection.cmake)
target_architecture(TARGET_ARCH)

project (dap_crypto_XKCP)

#locate make & xsltproc tools 
find_program(XMLSTYLEPROC xsltproc)
if(NOT XMLSTYLEPROC)
    message(FATAL_ERROR "xsltproc required for XKCP build.")
endif()

find_program(MAKE make)
if(NOT MAKE)
    find_program(MAKE gmake)
endif()

if(NOT MAKE)
    find_program(MAKE mingw32-make)
endif()

if(NOT MAKE)
    message(FATAL_ERROR "gnu-make required for XKCP build")
endif()

add_library(${PROJECT_NAME} STATIC IMPORTED GLOBAL)

option(DAP_CRYPTO_XKCP_REFERENCE "Use xkcp reference implemintation" OFF)
option(DAP_CRYPTO_XKCP_PLAINC "Use xkcp plain-c implemintation" OFF)

include(CheckCCompilerFlag)
unset(COMPILER_SUPPORTS_MARCH_NATIVE CACHE)
CHECK_C_COMPILER_FLAG(-march=native COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
    SET(TARGET_POSTFIX "-native")
endif()

if (CMAKE_C_COMPILER_TARGET)
    set(MAKE_C_FLAGS "--target=${CMAKE_C_COMPILER_TARGET} ${CMAKE_C_FLAGS} ")
else()
    set(MAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif()

message("[*] XKCP build from src")
message("[*] XKCP make command: ${MAKE}")
message("[*] XKCP make CC: ${CMAKE_C_COMPILER}")
message("[*] XKCP make COMPILER_TARGET: ${CMAKE_C_COMPILER_TARGET}")
message("[*] XKCP make AR: ${CMAKE_AR}")
message("[*] XKCP compiller supports -march=native: ${COMPILER_SUPPORTS_MARCH_NATIVE}")
message("[*] XKCP SYSROOT: ${CMAKE_OSX_SYSROOT}")
message("[*] XKCP CFLAGS: ${MAKE_C_FLAGS}")

# copy all source files to build directory
function(copy_source_files target_dir)
    execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${target_dir})
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${target_dir})
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${target_dir}/bin)

    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory 
                    ${CMAKE_CURRENT_SOURCE_DIR}/lib
                    ${target_dir}/lib)
                    
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory 
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests
                    ${target_dir}/tests)
                    
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory 
                    ${CMAKE_CURRENT_SOURCE_DIR}/util
                    ${target_dir}/util)
                
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory 
                    ${CMAKE_CURRENT_SOURCE_DIR}/Standalone
                    ${target_dir}/Standalone)

    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory 
                    ${CMAKE_CURRENT_SOURCE_DIR}/support
                    ${target_dir}/support)

    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory 
                    ${CMAKE_CURRENT_SOURCE_DIR}/doc
                    ${target_dir}/doc)

    file(GLOB BUILD_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.build")
    foreach(BUILD_FILE ${BUILD_FILES})
        execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${BUILD_FILE} ${target_dir}/)
    endforeach()

    # Generate Makefile dynamically in build directory (instead of copying from sources)
    set(MAKEFILE_CONTENT "XKCBpath = support/XKCBuild\n\ninclude $(XKCBpath)/src/Main.makefile")
    file(WRITE ${target_dir}/Makefile "${MAKEFILE_CONTENT}")
                    
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy 
                    ${CMAKE_CURRENT_SOURCE_DIR}/Makefile.build
                    ${target_dir}/Makefile.build)
endfunction()

if (APPLE AND CMAKE_OSX_ARCHITECTURES)
    message("[*] XKCP: Apple multi-architecture build detected")
    message("[*] XKCP: Using plain-c implementation for fat library with separate build directories")
    
    set(XKCP_LIB_PATHS)
    
    foreach(ARCH ${CMAKE_OSX_ARCHITECTURES})

        set(ARCH_BUILD_DIR "/tmp/xkcp_build_${ARCH}")
        message("[*] XKCP: Creating build directory for ${ARCH}: ${ARCH_BUILD_DIR}")
        
        copy_source_files(${ARCH_BUILD_DIR})
        
        set(ARCH_TARGET libdap-XKCP-plainc${TARGET_POSTFIX}.a)
        
        # list for lipo
        set(ARCH_TARGET_PATH ${ARCH_BUILD_DIR}/bin/${ARCH_TARGET})
        list(APPEND XKCP_LIB_PATHS ${ARCH_TARGET_PATH})
        
        set(MAKE_C_FLAGS "-arch ${ARCH} ${CMAKE_C_FLAGS} ")

        add_custom_command(
            OUTPUT ${ARCH_TARGET_PATH}
            COMMAND ${CMAKE_COMMAND} -E env AR=${CMAKE_AR} CC=${CMAKE_C_COMPILER} CFLAGS=${MAKE_C_FLAGS} SDKROOT=${CMAKE_OSX_SYSROOT} ${MAKE} -C ${ARCH_BUILD_DIR} ${ARCH_TARGET}
            COMMENT "Building XKCP plain-c implementation for ${ARCH}"
        )
    endforeach()
    
    SET(XKCP_TARGET libdap-XKCP-plainc-fat${TARGET_POSTFIX}.a)
    SET(XKCP_BUILD_DIR "/tmp/xkcp_build_fat")
    file(MAKE_DIRECTORY ${XKCP_BUILD_DIR}/bin)
    SET(XKCP_BUILD_LIB ${XKCP_BUILD_DIR}/bin/${XKCP_TARGET})
    SET(XKCP_BUILD_INCLUDES ${XKCP_BUILD_DIR}/bin/${XKCP_TARGET}.headers/)
    file(MAKE_DIRECTORY ${XKCP_BUILD_INCLUDES})
    
    #call lipo 
    add_custom_command(
        OUTPUT ${XKCP_BUILD_LIB}
        DEPENDS ${XKCP_LIB_PATHS}
        COMMAND lipo -create ${XKCP_LIB_PATHS} -output ${XKCP_BUILD_LIB}
        COMMENT "Creating fat library with lipo"
    )
    
    # headers are same, use first 
    list(GET CMAKE_OSX_ARCHITECTURES 0 FIRST_ARCH)
    set(FIRST_ARCH_BUILD_DIR "/tmp/xkcp_build_${FIRST_ARCH}")
    set(FIRST_ARCH_HEADERS "${FIRST_ARCH_BUILD_DIR}/bin/libdap-XKCP-plainc${TARGET_POSTFIX}.a.headers")
    
    add_custom_command(
        OUTPUT ${XKCP_BUILD_INCLUDES}/KeccakHash.h
        DEPENDS ${XKCP_BUILD_LIB}
        COMMAND ${CMAKE_COMMAND} -E copy_directory 
                ${FIRST_ARCH_HEADERS}
                ${XKCP_BUILD_INCLUDES}
        COMMENT "Copying headers from ${FIRST_ARCH} build to fat library"
    )
    
    add_custom_target(BuildXKCP ALL DEPENDS ${XKCP_BUILD_LIB} ${XKCP_BUILD_INCLUDES}/KeccakHash.h)
    
else()
    # single architecture builds
    #STRING(REPLACE "/" "_" PROJECT_PATH_SAFE ${CMAKE_CURRENT_SOURCE_DIR})
    #SET(XKCP_BUILD_DIR "/tmp/xkcp_build_${PROJECT_PATH_SAFE}")
    SET(XKCP_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/xkcp_build_src/)
    copy_source_files(${XKCP_BUILD_DIR})
    
    if (${TARGET_ARCH} MATCHES "x86_64")
        SET(XKCP_TARGET libdap-XKCP-x8664${TARGET_POSTFIX}.a)
    elseif(${TARGET_ARCH} MATCHES "armv7")
        SET(XKCP_TARGET libdap-XKCP-armv7${TARGET_POSTFIX}.a)
    elseif(${TARGET_ARCH} MATCHES "armv8")
        SET(XKCP_TARGET libdap-XKCP-armv8${TARGET_POSTFIX}.a)
    else()
        SET(XKCP_TARGET libdap-XKCP-ref${TARGET_POSTFIX}.a)
        message("[!] XKCP: Unknown architecture ${TARGET_ARCH}, use reference inplementation")
        set(DAP_CRYPTO_XKCP_REFERENCE ON)
    endif()

    IF (DAP_CRYPTO_XKCP_REFERENCE)
        SET(XKCP_TARGET libdap-XKCP-ref${TARGET_POSTFIX}.a)
        message("[!] XKCP: Force use reference impl for ${TARGET_ARCH}")
    endif()

    IF (DAP_CRYPTO_XKCP_PLAINC)
        SET(XKCP_TARGET libdap-XKCP-plainc${TARGET_POSTFIX}.a)
        message("[!] XKCP: Force use plain-c impl for ${TARGET_ARCH}")
    endif()

    message("[*] XKCP target: ${TARGET_ARCH} ${XKCP_TARGET}")
    SET(XKCP_BUILD_INCLUDES ${XKCP_BUILD_DIR}/bin/${XKCP_TARGET}.headers/)
    SET(XKCP_BUILD_LIB ${XKCP_BUILD_DIR}/bin/${XKCP_TARGET})
    file(MAKE_DIRECTORY ${XKCP_BUILD_INCLUDES})

    add_custom_target(BuildXKCP ALL
        COMMAND ${CMAKE_COMMAND} -E env AR=${CMAKE_AR} CC=${CMAKE_C_COMPILER} CFLAGS=${MAKE_C_FLAGS} SDKROOT=${CMAKE_OSX_SYSROOT} ${MAKE} -C ${XKCP_BUILD_DIR} ${XKCP_TARGET}
        BYPRODUCTS ${XKCP_BUILD_LIB}
    )
endif()

if ((CMAKE_C_COMPILER_ID STREQUAL "GNU") AND NOT WIN32)
    target_link_options(${PROJECT_NAME} INTERFACE -z noexecstack)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES 
    IMPORTED_LOCATION ${XKCP_BUILD_LIB}
    INTERFACE_INCLUDE_DIRECTORIES ${XKCP_BUILD_INCLUDES}
    PUBLIC_HEADER "${XKCP_BUILD_INCLUDES}"
)

add_dependencies(${PROJECT_NAME} BuildXKCP) 

if(INSTALL_DAP_SDK)
    INSTALL(DIRECTORY ${XKCP_BUILD_INCLUDES} DESTINATION include/dap/crypto/XKCP)
    INSTALL(FILES ${XKCP_BUILD_LIB} DESTINATION lib/dap/crypto/)
endif()

