# CMake generated Testfile for 
# Source directory: /mnt/store/work/dap-sdk/tests/performance/hashtable
# Build directory: /mnt/store/work/dap-sdk/build.debug/tests/performance/hashtable
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(benchmark_hashtable "/mnt/store/work/dap-sdk/build.debug/tests/performance/hashtable/benchmark_hashtable")
set_tests_properties(benchmark_hashtable PROPERTIES  LABELS "performance;hashtable" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/performance/hashtable/CMakeLists.txt;51;add_test;/mnt/store/work/dap-sdk/tests/performance/hashtable/CMakeLists.txt;0;")
