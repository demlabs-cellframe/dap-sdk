/**
 * Auto-generated wrapper template
 * Copy the needed wrappers to your test file and customize as needed
 */

#include "dap_mock.h"
#include "dap_mock_linker_wrapper.h"

{{#if MISSING_FUNCTIONS}}
    {{#for func in MISSING_FUNCTIONS}}
        {{#if func}}
// Wrapper for {{func}}
DAP_MOCK_WRAPPER_CUSTOM(void*, {{func}},
    (/* add parameters here */))
{
    if (g_mock_{{func}} && g_mock_{{func}}->enabled) {
        // Add your mock logic here
        return g_mock_{{func}}->return_value.ptr;
    }
    return __real_{{func}}(/* forward parameters */);
}

        {{/if}}
    {{/for}}
{{/if}}
