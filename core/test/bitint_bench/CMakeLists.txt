# Optional: C23 _BitInt(256) vs dap_math_ops.h — not registered with CTest, independent of BUILD_DAP_TESTS.
# Enable: cmake -DBUILD_DAP_MATH_BITINT_BENCH=ON (from cellframe-node or dap-sdk tree).
# Requires GCC >= 14 (or Clang with unsigned _BitInt(256)) and __int128.

include(CheckCSourceCompiles)

set(_BITINT_C_STD "")
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
        set(_BITINT_C_STD "-std=gnu23")
    else()
        set(_BITINT_C_STD "-std=gnu2x")
    endif()
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(_BITINT_C_STD "-std=gnu2x")
endif()

if(NOT _BITINT_C_STD STREQUAL "")
    set(CMAKE_REQUIRED_FLAGS "${_BITINT_C_STD}")
    check_c_source_compiles(
        "typedef unsigned _BitInt(256) W; int main(void) { W x = 1; return (int)x; }"
        DAP_C_COMPILER_HAS_BITINT_256)
    unset(CMAKE_REQUIRED_FLAGS)
endif()

if(NOT DAP_C_COMPILER_HAS_BITINT_256)
    message(WARNING
        "dap_math_bitint_bench not built: need unsigned _BitInt(256) "
        "(e.g. GCC 14+ with -std=gnu23). Current: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}.")
else()
    add_executable(dap_math_bitint_bench dap_math_bitint_bench.c)

    # Same include layout as dap_core (for dap_common.h / portable_endian.h / uthash)
    target_include_directories(dap_math_bitint_bench PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/uthash/src
    )

    target_compile_options(dap_math_bitint_bench PRIVATE ${_BITINT_C_STD} -O2 -Wall -Wextra)
endif()
