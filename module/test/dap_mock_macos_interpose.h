// macOS dyld interposition helper
// This file provides macros for function interposition on macOS
// Usage: Include this before dap_mock.h on macOS builds

#ifndef DAP_MOCK_MACOS_INTERPOSE_H
#define DAP_MOCK_MACOS_INTERPOSE_H

#ifdef __APPLE__
#include <mach-o/dyld-interposing.h>

// macOS interposition: DYLD_INSERT_LIBRARIES mechanism
// Define interposition pairs in special section
// Format: DYLD_INTERPOSE(replacement, original)

// Helper macro to create interposition for mocked functions
#define DAP_MOCK_INTERPOSE(func_name) \
    DYLD_INTERPOSE(__wrap_##func_name, func_name)

#endif // __APPLE__

#endif // DAP_MOCK_MACOS_INTERPOSE_H
