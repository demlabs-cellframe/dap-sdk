cmake_minimum_required(VERSION 3.12)
project(test_gpu_ntt)

if(NOT TARGET dap_gpu)
    return()
endif()

find_package(Vulkan QUIET)

add_executable(${PROJECT_NAME} test_gpu_ntt.c)
target_link_libraries(${PROJECT_NAME} PRIVATE
    dap_core
    dap_math_bignum
    dap_crypto
    dap_gpu
)
if(Vulkan_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${Vulkan_LIBRARIES})
endif()
add_test(NAME gpu_ntt_correctness COMMAND ${PROJECT_NAME})

add_executable(bench_gpu_ntt bench_gpu_ntt.c)
target_link_libraries(bench_gpu_ntt PRIVATE
    dap_core
    dap_math_bignum
    dap_crypto
    dap_gpu
)
if(Vulkan_FOUND)
    target_link_libraries(bench_gpu_ntt PRIVATE ${Vulkan_LIBRARIES})
endif()

add_executable(test_batch_verify test_batch_verify.c)
target_link_libraries(test_batch_verify PRIVATE
    dap_core
    dap_math_bignum
    dap_crypto
    dap_gpu
)
if(Vulkan_FOUND)
    target_link_libraries(test_batch_verify PRIVATE ${Vulkan_LIBRARIES})
endif()
add_test(NAME batch_verify_correctness COMMAND test_batch_verify)
