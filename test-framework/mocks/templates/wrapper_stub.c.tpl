// Wrapper for {{FUNC_NAME}}
DAP_MOCK_WRAPPER_CUSTOM(void*, {{FUNC_NAME}},
    (/* add parameters here */))
{
    if (g_mock_{{FUNC_NAME}} && g_mock_{{FUNC_NAME}}->enabled) {
        // Add your mock logic here
        return g_mock_{{FUNC_NAME}}->return_value.ptr;
    }
    return __real_{{FUNC_NAME}}(/* forward parameters */);
}

