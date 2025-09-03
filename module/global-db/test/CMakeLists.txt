cmake_minimum_required(VERSION 3.4.1)

project(global_db_test)

add_executable(${PROJECT_NAME} dap_global_db_test.c)

target_link_libraries(${PROJECT_NAME} dap_core dap_test dap_global_db dap_crypto dap_io dap_stream dap_client pthread)
add_test(
		NAME global_db_test
		COMMAND global_db_test
)
