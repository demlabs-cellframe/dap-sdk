# CMake generated Testfile for 
# Source directory: /mnt/store/work/dap-sdk/tests/unit/crypto/pqc
# Build directory: /mnt/store/work/dap-sdk/build.debug/tests/unit/crypto/pqc
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_pqc_algorithms "/mnt/store/work/dap-sdk/build.debug/tests/unit/crypto/pqc/test_pqc_algorithms")
set_tests_properties(test_pqc_algorithms PROPERTIES  LABELS "unit;crypto;pqc" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/crypto/pqc/CMakeLists.txt;10;add_test;/mnt/store/work/dap-sdk/tests/unit/crypto/pqc/CMakeLists.txt;0;")
add_test(test_shake_legacy_kat "/mnt/store/work/dap-sdk/build.debug/tests/unit/crypto/pqc/test_shake_legacy_kat")
set_tests_properties(test_shake_legacy_kat PROPERTIES  LABELS "unit;crypto;pqc;kat;regression" TIMEOUT "30" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/crypto/pqc/CMakeLists.txt;23;add_test;/mnt/store/work/dap-sdk/tests/unit/crypto/pqc/CMakeLists.txt;0;")
