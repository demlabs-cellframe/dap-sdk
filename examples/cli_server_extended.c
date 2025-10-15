/*
 * Example of using dap_cli_server_cmd_add_ext with extended parameters
 * 
 * This example demonstrates how to register CLI commands with flags
 * including JSON-RPC support, authentication requirements, etc.
 */

#include "dap_cli_server.h"
#include "dap_json.h"
#include <stdio.h>
#include <string.h>

// Example JSON-RPC command callback
int example_json_rpc_cmd(int argc, char **argv, void **a_str_reply, int a_version)
{
    (void)argc; (void)argv; (void)a_version;
    
    // Create JSON response
    dap_json_t *json_reply = dap_json_new_object();
    dap_json_object_add(json_reply, "result", dap_json_new_string("JSON-RPC command executed successfully"));
    dap_json_object_add(json_reply, "version", dap_json_new_number(a_version));
    
    // Convert to string and set reply
    char *json_str = dap_json_to_string(json_reply);
    dap_cli_server_cmd_set_reply_text(a_str_reply, "%s", json_str);
    
    DAP_FREE(json_str);
    dap_json_delete(json_reply);
    
    return 0;
}

// Example regular command callback
int example_regular_cmd(int argc, char **argv, void **a_str_reply, int a_version)
{
    (void)a_version;
    
    dap_cli_server_cmd_set_reply_text(a_str_reply, "Regular command executed with %d arguments", argc);
    
    for (int i = 0; i < argc; i++) {
        dap_cli_server_cmd_set_reply_text(a_str_reply, "  arg[%d]: %s", i, argv[i]);
    }
    
    return 0;
}

// Example authenticated command callback
int example_auth_cmd(int argc, char **argv, void **a_str_reply, int a_version)
{
    (void)argc; (void)argv; (void)a_version;
    
    dap_cli_server_cmd_set_reply_text(a_str_reply, "Authenticated command executed (would require auth in real implementation)");
    
    return 0;
}

// Example deprecated command callback
int example_deprecated_cmd(int argc, char **argv, void **a_str_reply, int a_version)
{
    (void)argc; (void)argv; (void)a_version;
    
    dap_cli_server_cmd_set_reply_text(a_str_reply, "WARNING: This command is deprecated and will be removed in future versions");
    
    return 0;
}

// Example experimental command callback
int example_experimental_cmd(int argc, char **argv, void **a_str_reply, int a_version)
{
    (void)argc; (void)argv; (void)a_version;
    
    dap_cli_server_cmd_set_reply_text(a_str_reply, "EXPERIMENTAL: This command is experimental and may change");
    
    return 0;
}

void register_extended_commands(void)
{
    // Example 1: JSON-RPC command
    dap_cli_server_cmd_params_t json_rpc_params = {
        .name = "json_test",
        .func = example_json_rpc_cmd,
        .doc = "Test JSON-RPC command",
        .id = 1001,
        .doc_ex = "This command demonstrates JSON-RPC functionality with extended parameters",
        .overrides = {0}, // No overrides
        .flags = {
            .is_json_rpc = true,
            .is_async = false,
            .requires_auth = false,
            .is_deprecated = false,
            .is_experimental = false
        }
    };
    dap_cli_server_cmd_add_ext(&json_rpc_params);
    
    // Example 2: Regular command
    dap_cli_server_cmd_params_t regular_params = {
        .name = "regular_test",
        .func = example_regular_cmd,
        .doc = "Test regular command",
        .id = 1002,
        .doc_ex = "This command demonstrates regular CLI functionality",
        .overrides = {0},
        .flags = {
            .is_json_rpc = false,
            .is_async = false,
            .requires_auth = false,
            .is_deprecated = false,
            .is_experimental = false
        }
    };
    dap_cli_server_cmd_add_ext(&regular_params);
    
    // Example 3: Authenticated command
    dap_cli_server_cmd_params_t auth_params = {
        .name = "auth_test",
        .func = example_auth_cmd,
        .doc = "Test authenticated command",
        .id = 1003,
        .doc_ex = "This command requires authentication",
        .overrides = {0},
        .flags = {
            .is_json_rpc = false,
            .is_async = false,
            .requires_auth = true,
            .is_deprecated = false,
            .is_experimental = false
        }
    };
    dap_cli_server_cmd_add_ext(&auth_params);
    
    // Example 4: Deprecated command
    dap_cli_server_cmd_params_t deprecated_params = {
        .name = "deprecated_test",
        .func = example_deprecated_cmd,
        .doc = "Test deprecated command",
        .id = 1004,
        .doc_ex = "This command is deprecated",
        .overrides = {0},
        .flags = {
            .is_json_rpc = false,
            .is_async = false,
            .requires_auth = false,
            .is_deprecated = true,
            .is_experimental = false
        }
    };
    dap_cli_server_cmd_add_ext(&deprecated_params);
    
    // Example 5: Experimental command
    dap_cli_server_cmd_params_t experimental_params = {
        .name = "experimental_test",
        .func = example_experimental_cmd,
        .doc = "Test experimental command",
        .id = 1005,
        .doc_ex = "This command is experimental",
        .overrides = {0},
        .flags = {
            .is_json_rpc = false,
            .is_async = false,
            .requires_auth = false,
            .is_deprecated = false,
            .is_experimental = true
        }
    };
    dap_cli_server_cmd_add_ext(&experimental_params);
    
    // Example 6: Async JSON-RPC command
    dap_cli_server_cmd_params_t async_json_params = {
        .name = "async_json_test",
        .func = example_json_rpc_cmd,
        .doc = "Test async JSON-RPC command",
        .id = 1006,
        .doc_ex = "This command demonstrates async JSON-RPC functionality",
        .overrides = {0},
        .flags = {
            .is_json_rpc = true,
            .is_async = true,
            .requires_auth = true,
            .is_deprecated = false,
            .is_experimental = true
        }
    };
    dap_cli_server_cmd_add_ext(&async_json_params);
}

int main(void)
{
    // Initialize CLI server
    dap_cli_server_init(true, "cli-server");
    
    // Register extended commands
    register_extended_commands();
    
    printf("Extended CLI commands registered successfully!\n");
    printf("Available commands:\n");
    printf("  - json_test: JSON-RPC command\n");
    printf("  - regular_test: Regular command\n");
    printf("  - auth_test: Authenticated command\n");
    printf("  - deprecated_test: Deprecated command\n");
    printf("  - experimental_test: Experimental command\n");
    printf("  - async_json_test: Async JSON-RPC command\n");
    
    // Cleanup
    dap_cli_server_deinit();
    
    return 0;
}
