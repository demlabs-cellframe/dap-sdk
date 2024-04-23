cmake_minimum_required(VERSION 3.10)
project (dap_XKCP)

SET(XKCP_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/xkcp_build_src/)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION ${XKCP_BUILD_DIR})

add_custom_target(BuildXKCP ALL
    COMMAND make -C ./xkcp_build_src libdap-XKCP.a
)

SET(XKCP_BUILD_INCLUDES ${XKCP_BUILD_DIR}/bin/libdap-XKCP.a.headers/)
SET(XKCP_BUILD_LIB ${XKCP_BUILD_DIR}/bin/libdap-XKCP.a )
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

INSTALL(TARGETS ${PROJECT_NAME} 
        LIBRARY DESTINATION lib/dap/crypto/XKCP/
        ARCHIVE DESTINATION lib/dap/crypto/XKCP/
        PUBLIC_HEADER DESTINATION include/dap/crypto/XKCP
)

INSTALL(DIRECTORY src/XKCP/ DESTINATION include/dap/crypto/XKCP/)
INSTALL(DIRECTORY src/rand/ DESTINATION include/dap/crypto/rand/)
INSTALL(DIRECTORY src/msrln/ DESTINATION include/dap/crypto/msrln/)
INSTALL(DIRECTORY src/sig_dilithium/ DESTINATION include/dap/crypto/sig_dilithium/)
INSTALL(DIRECTORY src/sha3/ DESTINATION include/dap/crypto/sha3/)
endif()

