# DapBuild.cmake — build type detection and flags (Debug, Release, Asan, etc.)
# Included before OS_Detection; OS_Detection uses DAP_* and applies platform-specific flags.

include_guard(GLOBAL)

# Allow Asan as a first-class build type alongside Debug and Release
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug;Release;RelWithDebInfo;MinSizeRel;Asan")

# -----------------------------------------------------------------------------
# Build type → DAP_* variables (used by OS_Detection and other cmake scripts)
# -----------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Asan")
    set(DAP_ASAN_BUILD ON)
    set(DAP_DEBUG ON)   # Asan build keeps debug symbols and no strip
    set(DAP_RELEASE OFF)
    message("[!] Asan build (AddressSanitizer)")
elseif((CMAKE_BUILD_TYPE STREQUAL "Debug") OR (DEFINED DAP_DEBUG AND DAP_DEBUG))
    set(DAP_ASAN_BUILD OFF)
    set(DAP_DEBUG ON)
    set(DAP_RELEASE OFF)
    message("[!] Debug build")
else()
    set(DAP_ASAN_BUILD OFF)
    set(DAP_DEBUG OFF)
    set(DAP_RELEASE ON)
    if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        message("[!] Release build (with debug info)")
        set(DAP_DBG_INFO ON)
    else()
        message("[!] Release build")
        set(DAP_DBG_INFO OFF)
    endif()
endif()

# -----------------------------------------------------------------------------
# Asan-specific flags (used when CMAKE_BUILD_TYPE=Asan; CMake applies *_ASAN automatically)
# and as DAP_ASAN_* for OS_Detection when it composes _CCOPT/_LOPT manually.
# -----------------------------------------------------------------------------
set(DAP_ASAN_C_FLAGS "-fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer -fno-common")
set(DAP_ASAN_LINKER_FLAGS "-fsanitize=address")

# CMake applies CMAKE_C_FLAGS_<BUILD_TYPE> when that build type is selected.
# Asan: debug-friendly, no -O3 (sanitizers often prefer -O1).
set(CMAKE_C_FLAGS_ASAN "-DDAP_DEBUG -g -O1 ${DAP_ASAN_C_FLAGS}")
set(CMAKE_CXX_FLAGS_ASAN "${CMAKE_C_FLAGS_ASAN}")
set(CMAKE_EXE_LINKER_FLAGS_ASAN "${DAP_ASAN_LINKER_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_ASAN "${DAP_ASAN_LINKER_FLAGS}")
