# CMake generated Testfile for 
# Source directory: /mnt/store/work/dap-sdk/tests/unit/core
# Build directory: /mnt/store/work/dap-sdk/build.debug/tests/unit/core
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_dap_common "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_dap_common")
set_tests_properties(test_dap_common PROPERTIES  LABELS "unit;core;common;macros" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;24;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
add_test(test_dap_config "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_dap_config")
set_tests_properties(test_dap_config PROPERTIES  LABELS "unit;core;config" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;47;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
add_test(test_dap_strfuncs "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_dap_strfuncs")
set_tests_properties(test_dap_strfuncs PROPERTIES  LABELS "unit;core;strfuncs" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;69;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
add_test(test_dap_circular "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_dap_circular")
set_tests_properties(test_dap_circular PROPERTIES  LABELS "unit;core;circular" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;92;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
add_test(test_dap_cpu_monitor "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_dap_cpu_monitor")
set_tests_properties(test_dap_cpu_monitor PROPERTIES  LABELS "unit;core;cpu-monitor" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;116;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
add_test(test_dap_process_memory "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_dap_process_memory")
set_tests_properties(test_dap_process_memory PROPERTIES  LABELS "unit;core;process-memory" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;140;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
add_test(test_dap_arena "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_dap_arena")
set_tests_properties(test_dap_arena PROPERTIES  LABELS "unit;core;arena;memory" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;163;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
add_test(test_arena_refcount "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_arena_refcount")
set_tests_properties(test_arena_refcount PROPERTIES  LABELS "unit;core;arena;refcount;memory" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;185;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
add_test(test_dap_string_pool "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_dap_string_pool")
set_tests_properties(test_dap_string_pool PROPERTIES  LABELS "unit;core;string_pool;memory" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;207;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
add_test(test_dap_dl "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_dap_dl")
set_tests_properties(test_dap_dl PROPERTIES  LABELS "unit;core;dap_dl;list" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;231;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
add_test(test_dap_ht "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_dap_ht")
set_tests_properties(test_dap_ht PROPERTIES  LABELS "unit;core;dap_ht;hash_table" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;253;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
add_test(test_dap_hash_fast "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_dap_hash_fast")
set_tests_properties(test_dap_hash_fast PROPERTIES  LABELS "unit;core;dap_hash_fast;hash" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;275;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
add_test(test_dap_mmap "/mnt/store/work/dap-sdk/build.debug/tests/unit/core/test_dap_mmap")
set_tests_properties(test_dap_mmap PROPERTIES  LABELS "unit;core;dap_mmap;io" TIMEOUT "300" _BACKTRACE_TRIPLES "/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;298;add_test;/mnt/store/work/dap-sdk/tests/unit/core/CMakeLists.txt;0;")
subdirs("uint256_t")
