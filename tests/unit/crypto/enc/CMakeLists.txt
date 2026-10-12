cmake_minimum_required(VERSION 3.10)

project(crypto_enc_test)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework/mocks/DAPMockAutoWrap.cmake)

file(GLOB SRC *.c)
file(GLOB HEADERS *.h)

add_executable(${PROJECT_NAME} ${SRC} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE
    dap_test
    dap_core_static
    dap_crypto_static
)

if(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE m pthread)
endif()

# Newer Apple clang rejects folded VLA extensions under -Werror in this
# 5.8-native test; keep the warning but non-fatal. The option name differs
# between clang versions (vla-cast-extension vs vla-cxx-extension) - probe it.
include(CheckCCompilerFlag)
if(APPLE)
    check_c_compiler_flag("-Wno-error=vla-cast-extension" HAS_VLA_CAST_EXT)
    check_c_compiler_flag("-Wno-error=vla-cxx-extension" HAS_VLA_CXX_EXT)
    if(HAS_VLA_CAST_EXT)
        target_compile_options(${PROJECT_NAME} PRIVATE -Wno-error=vla-cast-extension)
    elseif(HAS_VLA_CXX_EXT)
        target_compile_options(${PROJECT_NAME} PRIVATE -Wno-error=vla-cxx-extension)
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../crypto/src/rand
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../crypto/src/sphincsplus
)

dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 300)
