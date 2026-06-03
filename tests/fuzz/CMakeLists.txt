cmake_minimum_required(VERSION 3.10)
project(dap_fuzz C)

# Fuzzing requires Clang + libFuzzer.
# Build:
#   CC=clang cmake -DBUILD_FUZZING=ON -DCMAKE_C_FLAGS="-fsanitize=fuzzer-no-link,address" ..
#   cmake --build . --target fuzz_chacha20_decrypt
# Run:
#   ./tests/fuzz/fuzz_chacha20_decrypt corpus/chacha20/ -max_len=4096

set(FUZZ_TARGETS
    crypto/fuzz_chacha20_decrypt
    crypto/fuzz_aes256_decrypt
    crypto/fuzz_mlkem_decaps
    crypto/fuzz_sign_verify
    crypto/fuzz_sign_deser
)

foreach(FUZZ_SRC ${FUZZ_TARGETS})
    get_filename_component(FUZZ_NAME ${FUZZ_SRC} NAME_WE)

    add_executable(${FUZZ_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/${FUZZ_SRC}.c)

    target_link_libraries(${FUZZ_NAME} PRIVATE dap_sdk)
    target_link_options(${FUZZ_NAME} PRIVATE -fsanitize=fuzzer,address)

    target_include_directories(${FUZZ_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../module/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../module/crypto/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../module/crypto/src/sym/chacha20
    )

    set_target_properties(${FUZZ_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    )
endforeach()

message("[+] Fuzz targets configured: ${FUZZ_TARGETS}")
