# CMake generated Testfile for 
# Source directory: /home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap
# Build directory: /home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/build_debug
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(python_dap_unit_tests "/usr/bin/cmake" "--build" "." "--target" "unit_tests")
set_tests_properties(python_dap_unit_tests PROPERTIES  TIMEOUT "300" WORKING_DIRECTORY "/home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/build_debug" _BACKTRACE_TRIPLES "/home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/CMakeLists.txt;256;add_test;/home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/CMakeLists.txt;0;")
add_test(python_dap_integration_tests "/usr/bin/cmake" "--build" "." "--target" "integration_tests")
set_tests_properties(python_dap_integration_tests PROPERTIES  TIMEOUT "600" WORKING_DIRECTORY "/home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/build_debug" _BACKTRACE_TRIPLES "/home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/CMakeLists.txt;262;add_test;/home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/CMakeLists.txt;0;")
add_test(python_dap_regression_tests "/usr/bin/cmake" "--build" "." "--target" "regression_tests")
set_tests_properties(python_dap_regression_tests PROPERTIES  TIMEOUT "300" WORKING_DIRECTORY "/home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/build_debug" _BACKTRACE_TRIPLES "/home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/CMakeLists.txt;268;add_test;/home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/CMakeLists.txt;0;")
add_test(python_dap_all_tests "/usr/bin/cmake" "--build" "." "--target" "python_tests")
set_tests_properties(python_dap_all_tests PROPERTIES  TIMEOUT "900" WORKING_DIRECTORY "/home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/build_debug" _BACKTRACE_TRIPLES "/home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/CMakeLists.txt;274;add_test;/home/naeper/work/cellframe/cellframe-node.rc-6/plugins/plugin-python/python-dap/CMakeLists.txt;0;")
subdirs("dap-sdk")
