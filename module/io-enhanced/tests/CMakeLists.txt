set(TEST_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${DAP_CORE_INCLUDE_DIR}
)

add_executable(bench_vmqueue internal/bench_vmqueue.c)
target_include_directories(bench_vmqueue PRIVATE ${TEST_INCLUDES})
target_link_libraries(bench_vmqueue dap_io_enhanced pthread)
add_test(NAME bench_vmqueue COMMAND bench_vmqueue 100000)

add_executable(test_stress internal/test_stress.c)
target_include_directories(test_stress PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_stress dap_io_enhanced pthread)
add_test(NAME test_stress COMMAND test_stress)

add_executable(test_pipeline public_api/test_pipeline.c)
target_include_directories(test_pipeline PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_pipeline dap_io_enhanced pthread)
add_test(NAME test_pipeline COMMAND test_pipeline)

add_executable(bench_wfq_throughput internal/bench_wfq_throughput.c)
target_include_directories(bench_wfq_throughput PRIVATE ${TEST_INCLUDES})
target_link_libraries(bench_wfq_throughput dap_io_enhanced pthread)
add_test(NAME bench_wfq_throughput COMMAND bench_wfq_throughput)

add_executable(test_timer_drain public_api/test_timer_drain.c)
target_include_directories(test_timer_drain PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_timer_drain dap_io_enhanced pthread)
add_test(NAME test_timer_drain COMMAND test_timer_drain)

add_executable(test_user_callbacks public_api/test_user_callbacks.c)
target_include_directories(test_user_callbacks PRIVATE ${TEST_INCLUDES})
target_link_libraries(test_user_callbacks dap_io_enhanced pthread)
add_test(NAME test_user_callbacks COMMAND test_user_callbacks)

