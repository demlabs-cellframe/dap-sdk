# SH Section Extensions for DAP Mocking
# Registers environment variables and output filters for SH sections
# This is project-specific extension, not part of dap_tpl core
# Uses extension registry from dap_tpl/core/sh_section_extensions.awk

# Note: This file must be included AFTER core/sh_section_extensions.awk
# The registry functions are provided by dap_tpl core

# Initialize DAP mocking extensions
# Registers all mocking-related environment variables and filters
function init_dap_mocking_extensions() {
    # Register environment variables for mocking
    register_sh_env_var("CUSTOM_MOCKS_LIST")
    register_sh_env_var("WRAPPER_FUNCTIONS")
    register_sh_env_var("PARAM_COUNTS_ARRAY")
    register_sh_env_var("MAX_ARGS_COUNT")
    register_sh_env_var("MISSING_FUNCTIONS")
    register_sh_env_var("temp_file")
    
    # Register output filters for mocking debug output
    register_sh_output_filter("^\\+")
    register_sh_output_filter("^CUSTOM_MOCKS_LIST=")
    register_sh_output_filter("^WRAPPER_FUNCTIONS=")
    register_sh_output_filter("^set -x")
    register_sh_output_filter("^DEBUG:")
}

# Auto-initialize when loaded
BEGIN {
    init_dap_mocking_extensions()
}

