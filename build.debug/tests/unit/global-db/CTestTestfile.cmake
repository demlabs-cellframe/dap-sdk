# CMake generated Testfile for 
# Source directory: /mnt/store/work/dap-sdk/tests/unit/global-db
# Build directory: /mnt/store/work/dap-sdk/build.debug/tests/unit/global-db
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_globaldb_btree "/mnt/store/work/dap-sdk/build.debug/tests/unit/global-db/test_globaldb_btree")
set_tests_properties(test_globaldb_btree PROPERTIES  LABELS "unit;global-db;btree" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/global-db/CMakeLists.txt;20;add_test;/mnt/store/work/dap-sdk/tests/unit/global-db/CMakeLists.txt;0;")
add_test(test_globaldb_storage "/mnt/store/work/dap-sdk/build.debug/tests/unit/global-db/test_globaldb_storage")
set_tests_properties(test_globaldb_storage PROPERTIES  LABELS "unit;global-db;storage" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/global-db/CMakeLists.txt;26;add_test;/mnt/store/work/dap-sdk/tests/unit/global-db/CMakeLists.txt;0;")
add_test(test_globaldb_migrate "/mnt/store/work/dap-sdk/build.debug/tests/unit/global-db/test_globaldb_migrate")
set_tests_properties(test_globaldb_migrate PROPERTIES  LABELS "integration;global-db;migration" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/global-db/CMakeLists.txt;29;add_test;/mnt/store/work/dap-sdk/tests/unit/global-db/CMakeLists.txt;0;")
