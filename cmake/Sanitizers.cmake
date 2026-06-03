# cmake/Sanitizers.cmake — compiler sanitizer support
#
# Options:
#   DAP_SANITIZE_ADDRESS  — AddressSanitizer + LeakSanitizer  (GCC/Clang)
#   DAP_SANITIZE_UNDEFINED — UndefinedBehaviorSanitizer       (GCC/Clang)
#   DAP_SANITIZE_THREAD   — ThreadSanitizer                   (GCC/Clang)
#   DAP_SANITIZE_MEMORY   — MemorySanitizer                   (Clang only)
#
# Note: ASan and TSan are mutually exclusive.
#       MSan and ASan are mutually exclusive.

option(DAP_SANITIZE_ADDRESS   "Enable AddressSanitizer (ASan)" OFF)
option(DAP_SANITIZE_UNDEFINED "Enable UndefinedBehaviorSanitizer (UBSan)" OFF)
option(DAP_SANITIZE_THREAD    "Enable ThreadSanitizer (TSan)" OFF)
option(DAP_SANITIZE_MEMORY    "Enable MemorySanitizer (MSan, Clang only)" OFF)

set(_dap_san_flags "")

if(DAP_SANITIZE_ADDRESS AND DAP_SANITIZE_THREAD)
    message(FATAL_ERROR "ASan and TSan cannot be used simultaneously")
endif()

if(DAP_SANITIZE_ADDRESS AND DAP_SANITIZE_MEMORY)
    message(FATAL_ERROR "ASan and MSan cannot be used simultaneously")
endif()

if(DAP_SANITIZE_ADDRESS)
    string(APPEND _dap_san_flags " -fsanitize=address -fno-omit-frame-pointer")
    message(STATUS "[Sanitizers] AddressSanitizer enabled")
endif()

if(DAP_SANITIZE_UNDEFINED)
    string(APPEND _dap_san_flags " -fsanitize=undefined -fno-sanitize-recover=all")
    message(STATUS "[Sanitizers] UndefinedBehaviorSanitizer enabled")
endif()

if(DAP_SANITIZE_THREAD)
    string(APPEND _dap_san_flags " -fsanitize=thread")
    message(STATUS "[Sanitizers] ThreadSanitizer enabled")
endif()

if(DAP_SANITIZE_MEMORY)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MSan requires Clang (current compiler: ${CMAKE_C_COMPILER_ID})")
    endif()
    string(APPEND _dap_san_flags " -fsanitize=memory -fno-omit-frame-pointer -fsanitize-memory-track-origins=2")
    message(STATUS "[Sanitizers] MemorySanitizer enabled")
endif()

if(_dap_san_flags)
    string(STRIP "${_dap_san_flags}" _dap_san_flags)
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${_dap_san_flags}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_dap_san_flags}")
    set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS} ${_dap_san_flags}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_dap_san_flags}")
    message(STATUS "[Sanitizers] Flags: ${_dap_san_flags}")
endif()
