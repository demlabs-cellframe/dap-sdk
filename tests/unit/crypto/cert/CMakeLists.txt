cmake_minimum_required(VERSION 3.10)

project(crypto_cert_test)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/test/mocks/DAPMockAutoWrap.cmake)

file(GLOB SOURCES *.c)
file(GLOB HEADERS *.h)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME}
    dap_sdk
    dap_test
)

if(UNIX AND NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} m pthread)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
)

dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT ${DAP_T300})
