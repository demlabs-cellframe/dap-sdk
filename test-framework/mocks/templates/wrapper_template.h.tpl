/**
 * Auto-generated wrapper template
 * Copy the needed wrappers to your test file and customize as needed
 */

#include "dap_mock.h"
#include "dap_mock_linker_wrapper.h"

{{#/bin/sh:
# Generate wrapper stubs for missing functions
# MISSING_FUNCTIONS is passed as newline-separated list in environment
MISSING_FUNCTIONS="${MISSING_FUNCTIONS}"

WRAPPER_STUBS=""
if [ -n "$MISSING_FUNCTIONS" ]; then
    while IFS= read -r func || [ -n "$func" ]; do
        [ -z "$func" ] && continue
        
        WRAPPER_STUBS="${WRAPPER_STUBS}// Wrapper for ${func}"$'\n'
        WRAPPER_STUBS="${WRAPPER_STUBS}DAP_MOCK_WRAPPER_CUSTOM(void*, ${func},"$'\n'
        WRAPPER_STUBS="${WRAPPER_STUBS}    (/* add parameters here */))"$'\n'
        WRAPPER_STUBS="${WRAPPER_STUBS}{"$'\n'
        WRAPPER_STUBS="${WRAPPER_STUBS}    if (g_mock_${func} && g_mock_${func}->enabled) {"$'\n'
        WRAPPER_STUBS="${WRAPPER_STUBS}        // Add your mock logic here"$'\n'
        WRAPPER_STUBS="${WRAPPER_STUBS}        return g_mock_${func}->return_value.ptr;"$'\n'
        WRAPPER_STUBS="${WRAPPER_STUBS}    }"$'\n'
        WRAPPER_STUBS="${WRAPPER_STUBS}    return __real_${func}(/* forward parameters */);"$'\n'
        WRAPPER_STUBS="${WRAPPER_STUBS}}"$'\n'
        WRAPPER_STUBS="${WRAPPER_STUBS}"$'\n'
    done <<EOF
$MISSING_FUNCTIONS
EOF
fi

echo -n "$WRAPPER_STUBS"
}}

