cmake_minimum_required(VERSION 3.10)
include (../../cmake/TargetArch_Detection.cmake)
target_architecture(TARGET_ARCH)

project (dap_crypto_XKCP)

add_library(${PROJECT_NAME} STATIC IMPORTED GLOBAL)

option(DAP_CRYPTO_XKCP_USE_PREBUILDS "Use prebuilded optimizations" OFF)
option(DAP_CRYPTO_XKCP_REFERENCE "Use xkcp reference implemintation" OFF)

IF (DAP_CRYPTO_XKCP_USE_PREBUILDS)
    #all prebuilds are done with gcc, it support -march=native
    SET(TARGET_POSTFIX "-native")
else()
    #if building from src - check for compiller support
    include(CheckCCompilerFlag)
    unset(COMPILER_SUPPORTS_MARCH_NATIVE CACHE)
    CHECK_C_COMPILER_FLAG(-march=native COMPILER_SUPPORTS_MARCH_NATIVE)
    if(COMPILER_SUPPORTS_MARCH_NATIVE)
        SET(TARGET_POSTFIX "-native")
    endif()
endif()


if (${TARGET_ARCH} MATCHES "x86_64")
    SET(XKCP_TARGET libdap-XKCP-x8664${TARGET_POSTFIX}.a)
elseif(${TARGET_ARCH} MATCHES "armv7")
    SET(XKCP_TARGET libdap-XKCP-armv7${TARGET_POSTFIX}.a)
elseif(${TARGET_ARCH} MATCHES "armv8")
    SET(XKCP_TARGET libdap-XKCP-armv8${TARGET_POSTFIX}.a)
else()
    SET(XKCP_TARGET libdap-XKCP-ref${TARGET_POSTFIX}.a)
    message("[!] XKCP: Unknown architecture ${TARGET_ARCH}, use reference inplementation")
    set(DAP_CRYPTO_XKCP_USE_PREBUILDS OFF) 
endif()

IF (DAP_CRYPTO_XKCP_REFERENCE)
    SET(XKCP_TARGET libdap-XKCP-ref${TARGET_POSTFIX}.a)
    message("[!] XKCP: Force use reference impl for ${TARGET_ARCH}")
    set(DAP_CRYPTO_XKCP_USE_PREBUILDS OFF) 
endif()

IF (NOT DAP_CRYPTO_XKCP_USE_PREBUILDS)

    SET(XKCP_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/xkcp_build_src/)
    message("[*] XKCP build from src")
    message("[*] XKCP target: ${TARGET_ARCH} ${XKCP_TARGET}")
    message("[*] XKCP build dir: ${XKCP_BUILD_DIR}")
    message("[*] XKCP make command: ${CMAKE_MAKE_PROGRAM}")
    message("[*] XKCP make CC: ${CMAKE_C_COMPILER}")
    message("[*] XKCP make AR: ${CMAKE_AR}")
    message("[*] XKCP compiller supports -march=native: ${COMPILER_SUPPORTS_MARCH_NATIVE}")
    message("[*] XKCP SYSROOT: ${CMAKE_OSX_SYSROOT}")

    
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION ${XKCP_BUILD_DIR})
    add_custom_target(BuildXKCP ALL
        COMMAND ${CMAKE_COMMAND}  -E env AR=${CMAKE_AR} CC=${CMAKE_C_COMPILER} SDKROOT=${CMAKE_OSX_SYSROOT}  ${CMAKE_MAKE_PROGRAM} -C ./xkcp_build_src ${XKCP_TARGET}
    )

    SET(XKCP_BUILD_INCLUDES ${XKCP_BUILD_DIR}/bin/${XKCP_TARGET}.headers/)
    SET(XKCP_BUILD_LIB ${XKCP_BUILD_DIR}/bin/${XKCP_TARGET})
    file(MAKE_DIRECTORY ${XKCP_BUILD_INCLUDES})

    add_dependencies(${PROJECT_NAME} BuildXKCP) 
else()
    message("[*] XKCP use optimized prebuilds for ${TARGET_ARCH}")
    SET(XKCP_BUILD_LIB ${CMAKE_CURRENT_SOURCE_DIR}/prebuilds/${TARGET_ARCH}/${XKCP_TARGET})
    SET(XKCP_BUILD_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/prebuilds/${TARGET_ARCH}/${XKCP_TARGET}.headers/)
endif()

#hack to create a path for INTERFACE_INCLUDE_DIRECTORIES, with will be populated later

set_target_properties(${PROJECT_NAME}
    PROPERTIES IMPORTED_LOCATION ${XKCP_BUILD_LIB}
)

set_target_properties(${PROJECT_NAME}
    PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${XKCP_BUILD_INCLUDES}
)

set_target_properties(${PROJECT_NAME}  PROPERTIES PUBLIC_HEADER ${XKCP_BUILD_INCLUDES})


if(INSTALL_DAP_SDK)

INSTALL(DIRECTORY ${XKCP_BUILD_INCLUDES} DESTINATION include/dap/crypto/XKCP)
INSTALL(FILES ${XKCP_BUILD_LIB} DESTINATION lib/dap/crypto/)

endif()

