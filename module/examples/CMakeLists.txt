cmake_minimum_required(VERSION 3.10)
project(rng_examples C)

set(CMAKE_C_STANDARD 11)

add_executable(${PROJECT_NAME} rng_example.c)

target_include_directories(${PROJECT_NAME} PRIVATE
        ../3rdparty
        ../crypto/include
        ../core/include
        ../../../../cellframe-sdk/modules/chain/include
        ../../../../cellframe-sdk/modules/common/include
        ../crypto/src/sha3
        ../crypto/src/XKCP/lib/common
        )

target_link_libraries( ${PROJECT_NAME}
        dap_core dap_crypto
)
