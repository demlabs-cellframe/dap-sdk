cmake_minimum_required(VERSION 3.10)

project(crypto_falcon_test)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/test/mocks/DAPMockAutoWrap.cmake)

add_executable(${PROJECT_NAME}
    test_falcon.c
)

target_link_libraries(${PROJECT_NAME}
    dap_crypto
    dap_core
    dap_test
)

if(UNIX)
    target_link_libraries(${PROJECT_NAME} m pthread)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/test
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/crypto/src/falcon
)

dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 300)

