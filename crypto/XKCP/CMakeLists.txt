cmake_minimum_required(VERSION 3.10)
include (../../cmake/TargetArch_Detection.cmake)
target_architecture(TARGET_ARCH)
message("TAracrc ${TARGET_ARCH}")
project (dap_crypto_XKCP)

SET(XKCP_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/xkcp_build_src/)



if (${TARGET_ARCH} MATCHES "x86_64")
    SET(XKCP_TARGET libdap-XKCP-x8664.a)
elseif(${TARGET_ARCH} MATCHES "armv7")
    SET(XKCP_TARGET libdap-XKCP-armv7.a)
elseif(${TARGET_ARCH} MATCHES "armv8")
    SET(XKCP_TARGET libdap-XKCP-armv8.a)
else()
    SET(XKCP_TARGET libdap-XKCP-ref.a)
endif()

message("[*] XKCP target: ${TARGET_ARCH} ${XKCP_TARGET}")
message("[*] XKCP build dir: ${XKCP_BUILD_DIR}")
message("[*] XKCP make command: ${CMAKE_MAKE_PROGRAM}")
message("[*] XKCP make CC: ${CMAKE_C_COMPILER}")
message("[*] XKCP make AR: ${CMAKE_AR}")
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION ${XKCP_BUILD_DIR})

message("[*] XKCP OSXSYSROOT: ${CMAKE_OSX_SYSROOT}")

add_custom_target(BuildXKCP ALL
    COMMAND ${CMAKE_COMMAND}  -E env AR=${CMAKE_AR} CC=${CMAKE_C_COMPILER} SDKROOT=${CMAKE_OSX_SYSROOT}  ${CMAKE_MAKE_PROGRAM} -C ./xkcp_build_src ${XKCP_TARGET}
)

SET(XKCP_BUILD_INCLUDES ${XKCP_BUILD_DIR}/bin/${XKCP_TARGET}.headers/)
SET(XKCP_BUILD_LIB ${XKCP_BUILD_DIR}/bin/${XKCP_TARGET})
file(MAKE_DIRECTORY ${XKCP_BUILD_INCLUDES})

add_library(${PROJECT_NAME} STATIC IMPORTED GLOBAL)
add_dependencies(${PROJECT_NAME} BuildXKCP) 

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

