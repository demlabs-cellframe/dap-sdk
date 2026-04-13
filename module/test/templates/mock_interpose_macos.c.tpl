// Auto-generated mock interposition library for macOS
// This dylib is loaded via DYLD_INSERT_LIBRARIES.
// It uses dyld interpose to replace original functions with wrappers.
//
// How it works:
// 1. Dylib is loaded via DYLD_INSERT_LIBRARIES
// 2. __DATA,__interpose section tells dyld to replace function pointers
// 3. All calls to original functions go through our interpose functions
// 4. Interpose functions call __wrap_ from test executable
//
// References:
// - Apple dyld interposition: https://opensource.apple.com/source/dyld/

#ifdef __APPLE__
#include <stddef.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

// Dyld interpose structure - tells dyld which functions to replace
typedef struct {
    const void *replacement;
    const void *replacee;
} interpose_t;

// Generic function pointer for calling __wrap_ functions
typedef intptr_t (*generic_func_t)();

{{#if MOCK_FUNCTIONS}}
{{#for func in MOCK_FUNCTIONS|newline}}
// =============================================================================
// Interpose for: {{func}}
// =============================================================================

// Cache for function pointers
static generic_func_t s_wrap_{{func}} = NULL;

// Declare original function (from cellframe_sdk.dylib)
extern intptr_t {{func}}(void);

// Interpose replacement function
__attribute__((used))
static intptr_t interpose_{{func}}(void) {
    // Get __wrap_ function from test executable
    if (!s_wrap_{{func}}) {
        s_wrap_{{func}} = (generic_func_t)dlsym(RTLD_DEFAULT, "__wrap_{{func}}");
        if (!s_wrap_{{func}}) {
            // __wrap_ not found - call original via RTLD_NEXT
            generic_func_t real_func = (generic_func_t)dlsym(RTLD_NEXT, "{{func}}");
            if (real_func) return real_func();
            fprintf(stderr, "FATAL: Neither __wrap_{{func}} nor {{func}} found\n");
            abort();
        }
    }
    return s_wrap_{{func}}();
}

// __real_{{func}} - calls the actual original function
// Provided for DAP_MOCK_WRAPPER_* macros that may call __real_
__attribute__((visibility("default")))
intptr_t __real_{{func}}(void) {
    generic_func_t real_func = (generic_func_t)dlsym(RTLD_NEXT, "{{func}}");
    if (!real_func) {
        fprintf(stderr, "FATAL: dlsym(RTLD_NEXT, \"{{func}}\") failed\n");
        abort();
    }
    return real_func();
}

{{/for}}

// =============================================================================
// DYLD INTERPOSE SECTION
// =============================================================================
// This section is processed by dyld at load time.
// It tells dyld to replace each original function with our interpose version.

__attribute__((used, section("__DATA,__interpose")))
static const interpose_t s_interpose_array[] = {
{{#for func in MOCK_FUNCTIONS|newline}}
    { (const void *)interpose_{{func}}, (const void *){{func}} },
{{/for}}
};

{{else}}
// No functions to interpose
static const int _no_mocks_placeholder = 0;
{{/if}}

#endif // __APPLE__
