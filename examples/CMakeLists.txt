cmake_minimum_required(VERSION 3.10)
project(examples C)

set(CMAKE_C_STANDARD 11)

add_executable(rng_example rng_example.c)

#target_include_directories(rng_example INTERFACE
#        ../3rdparty
#        ../crypto/include
#        ../core/include
#        ../../cellframe-sdk/modules/common/include
#        ../crypto/src/sha3
#        ../crypto/src/XKCP/lib/common
#        )

target_link_libraries( rng_example
        cellframe-sdk
)
