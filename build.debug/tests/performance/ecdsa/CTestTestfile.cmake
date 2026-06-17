# CMake generated Testfile for 
# Source directory: /mnt/store/work/dap-sdk/tests/performance/ecdsa
# Build directory: /mnt/store/work/dap-sdk/build.debug/tests/performance/ecdsa
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(ecdsa_benchmark "/mnt/store/work/dap-sdk/build.debug/tests/performance/ecdsa/benchmark_ecdsa")
set_tests_properties(ecdsa_benchmark PROPERTIES  LABELS "performance;crypto" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/performance/ecdsa/CMakeLists.txt;84;add_test;/mnt/store/work/dap-sdk/tests/performance/ecdsa/CMakeLists.txt;0;")
