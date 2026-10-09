# dap_link_manager_test: registered here (tests/ subtree) rather than from
# net/link_manager/test/ because the ${MODULE}_static libraries the helper
# links only exist after every module has registered (root CMakeLists).
project(dap_link_manager_test)

include(${CMAKE_SOURCE_DIR}/tests/cmake/dap_test_helpers.cmake)

add_executable(${PROJECT_NAME}
    ${CMAKE_SOURCE_DIR}/net/link_manager/test/dap_link_manager_test.c
)

dap_test_link_libraries(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/net/link_manager/include
    ${CMAKE_SOURCE_DIR}/io/include
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/net/stream/ch/include
    ${CMAKE_SOURCE_DIR}/net/stream/stream/include
    ${CMAKE_SOURCE_DIR}/3rdparty/uthash/src
)

if(NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

add_test(NAME dap_link_manager_test COMMAND dap_link_manager_test)
