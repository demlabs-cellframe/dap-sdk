<<<<<<< HEAD
﻿cmake_minimum_required(VERSION 3.10)

project(crypto_cert_test)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../module/test/mocks/DAPMockAutoWrap.cmake)

=======
﻿if(TARGET crypto_cert_test)
    return() # The project has already been built.
endif()
project(crypto_cert_test)

>>>>>>> a8b8799642f830d976bc7686526ea201333815dd
file(GLOB SOURCES *.c)
file(GLOB HEADERS *.h)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

<<<<<<< HEAD
target_link_libraries(${PROJECT_NAME}
    dap_crypto
    dap_core
    dap_test
)

if(UNIX)
    target_link_libraries(${PROJECT_NAME} m pthread)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../test-framework
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../crypto/include
)

dap_mock_autowrap(${PROJECT_NAME})

add_test(
    NAME ${PROJECT_NAME}
    COMMAND ${PROJECT_NAME}
)

set_tests_properties(${PROJECT_NAME} PROPERTIES TIMEOUT 300)
=======
target_link_libraries(${PROJECT_NAME} dap_crypto dap_core dap_test dap_crypto_kyber512 pthread)

add_test(
    NAME crypto_cert_test
    COMMAND crypto_cert_test
)
>>>>>>> a8b8799642f830d976bc7686526ea201333815dd
