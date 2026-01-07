cmake_minimum_required(VERSION 3.10)

project(crypto_enc_test)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/test/mocks/DAPMockAutoWrap.cmake)

file(GLOB SRC *.c)
file(GLOB HEADERS *.h)

add_executable(${PROJECT_NAME} ${SRC} ${HEADERS})

target_link_libraries(${PROJECT_NAME}
    dap_test
    dap_core
    dap_crypto
)

if(UNIX)
    target_link_libraries(${PROJECT_NAME} m pthread)
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
