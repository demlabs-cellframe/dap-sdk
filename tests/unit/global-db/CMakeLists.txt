cmake_minimum_required(VERSION 3.10)
project(test_global_db C)

# B-tree unit tests
add_executable(test_globaldb_btree test_globaldb_btree.c)
target_link_libraries(test_globaldb_btree dap_sdk dap_test)
target_include_directories(test_globaldb_btree PRIVATE
    ${CMAKE_SOURCE_DIR}/module/global-db/include
    ${CMAKE_SOURCE_DIR}/module/global-db
)

# Storage layer tests
add_executable(test_globaldb_storage test_globaldb_storage.c)
target_link_libraries(test_globaldb_storage dap_sdk dap_test)
target_include_directories(test_globaldb_storage PRIVATE
    ${CMAKE_SOURCE_DIR}/module/global-db/include
)

# Migration tests
add_executable(test_globaldb_migrate test_globaldb_migrate.c)
target_link_libraries(test_globaldb_migrate dap_global_db_migrate dap_sdk dap_test)
target_include_directories(test_globaldb_migrate PRIVATE
    ${CMAKE_SOURCE_DIR}/module/global-db/include
    ${CMAKE_SOURCE_DIR}/module/global-db/migrate
)

# Register tests with CTest
add_test(NAME test_globaldb_btree COMMAND test_globaldb_btree)
set_tests_properties(test_globaldb_btree PROPERTIES LABELS "unit;global-db;btree")

add_test(NAME test_globaldb_storage COMMAND test_globaldb_storage)
set_tests_properties(test_globaldb_storage PROPERTIES LABELS "unit;global-db;storage")

add_test(NAME test_globaldb_migrate COMMAND test_globaldb_migrate)
set_tests_properties(test_globaldb_migrate PROPERTIES LABELS "integration;global-db;migration")

message("[+] GlobalDB tests: B-tree, Storage, Migration")
