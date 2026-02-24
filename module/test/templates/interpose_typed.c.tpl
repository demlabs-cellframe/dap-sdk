// Auto-generated mock override library for macOS
// Linked BEFORE cellframe_sdk with -flat_namespace for symbol override
// Uses TYPED functions for proper ARM64 ABI (critical for struct returns!)
#ifdef __APPLE__
#include <stddef.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

// Include SDK headers for type definitions
#include "dap_common.h"
#include "dap_chain_common.h"
#include "dap_chain_datum_tx.h"
#include "dap_chain_ledger.h"
#include "dap_math_ops.h"

{{#if FUNCTIONS_DATA}}
{{#for entry in FUNCTIONS_DATA|newline}}
{{entry|split|pipe}}
{{#set func_name={{entry|part|0}}}}
{{#set ret_type={{entry|part|1}}}}
{{#set param_decl={{entry|part|2}}}}
{{#set param_names={{entry|part|3}}}}
{{#set is_struct={{entry|part|4}}}}

{{#if is_struct == '1'}}
// === Typed override for: {{func_name}} (struct return: {{ret_type}}) ===
// CRITICAL: Struct returns on ARM64 use hidden pointer parameter (x8 register)
// Cannot use variadic forwarding - must have exact signature!

// Forward declaration of wrapper (defined in test)
extern {{ret_type}} __wrap_{{func_name}}({{param_decl}});

// The override function that replaces original
__attribute__((visibility("default")))
{{ret_type}} {{func_name}}({{param_decl}}) {
    // Always call the mock wrapper
    return __wrap_{{func_name}}({{param_names}});
}

{{else}}
// === Override for: {{func_name}} (pointer/scalar return: {{ret_type}}) ===
typedef {{ret_type}} (*{{func_name}}_func_t)({{param_decl}});
static {{func_name}}_func_t s_real_{{func_name}} = NULL;

// Forward declaration of wrapper (defined in test)
extern {{ret_type}} __wrap_{{func_name}}({{param_decl}});

// Provide __real_{{func_name}} for DAP_MOCK_WRAPPER_* macros
__attribute__((visibility("default")))
{{ret_type}} __real_{{func_name}}({{param_decl}}) {
    if (!s_real_{{func_name}}) {
        s_real_{{func_name}} = ({{func_name}}_func_t)dlsym(RTLD_NEXT, "{{func_name}}");
        if (!s_real_{{func_name}}) {
            fprintf(stderr, "FATAL: dlsym(RTLD_NEXT, \"{{func_name}}\") failed: %s\n", dlerror());
            abort();
        }
    }
    return s_real_{{func_name}}({{param_names}});
}

// Override function - replaces {{func_name}} due to link order + flat_namespace
__attribute__((visibility("default")))
{{ret_type}} {{func_name}}({{param_decl}}) {
    return __wrap_{{func_name}}({{param_names}});
}

{{/if}}
{{/for}}
{{/if}}
#endif // __APPLE__
