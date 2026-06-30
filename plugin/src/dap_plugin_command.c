
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_cli_server.h"
#include "dap_plugin_manifest.h"
#include "dap_plugin.h"
#include "uthash.h"
#include "utlist.h"
#include "dap_plugin_command.h"

#define LOG_TAG "dap_plugin_command"

static bool s_l_restart_plugins = false;

static int s_command_handler(int a_argc, char **a_argv, void **a_str_reply, int a_version);

/**
 * @brief Build CLI commands section for plugin show from manifest params field
 * @param a_manifest Plugin manifest with optional params (CLI command names)
 * @return Allocated text block or NULL if no params declared
 */
static char *s_plugin_manifest_cli_commands_text(dap_plugin_manifest_t *a_manifest)
{
    if(!a_manifest || !a_manifest->params_count || !a_manifest->params)
        return NULL;
    char *l_result = dap_strdup(" CLI commands:\n");
    if(!l_result)
        return NULL;
    for(size_t i = 0; i < a_manifest->params_count; i++){
        const char *l_name = a_manifest->params[i];
        if(!l_name || !l_name[0])
            continue;
        dap_cli_cmd_t *l_cmd = dap_cli_server_cmd_find(l_name);
        if(l_cmd && l_cmd->doc)
            l_result = dap_strjoin(NULL, l_result, "  ", l_name, "\t- ", l_cmd->doc, "\n", NULL);
        else if(l_cmd)
            l_result = dap_strjoin(NULL, l_result, "  ", l_name,
                    "\t- (registered, use 'help ", l_name, "' for details)\n", NULL);
        else
            l_result = dap_strjoin(NULL, l_result, "  ", l_name,
                    "\t- (declared in manifest, not registered in CLI)\n", NULL);
        if(!l_result)
            return NULL;
    }
    return l_result;
}

/**
 * @brief dap_chain_plugins_command_create
 */
void dap_plugin_command_init(void)
{
    if (!s_l_restart_plugins){
        dap_cli_server_cmd_add("plugin", s_command_handler, NULL,
                                           "Commands for working with plugins:\n",
                                           "plugin list\n"
                                           "\tShow plugins list\n"
                                           "plugin show <plugin name>\n"
                                           "\tShow plugin details (incl. CLI commands from manifest params)\n"
                                           "plugin restart\n"
                                           "\tRestart all plugins\n"
                                           "plugin reload <plugin name>\n"
                                           "\tRestart plugin <plugin name>\n\n");
        s_l_restart_plugins = true;
    }
}

/**
 * @brief dap_plugin_command_deinit
 */
void dap_plugin_command_deinit(void)
{

}

/**
 * @brief s_command_handler
 * @param a_argc
 * @param a_argv
 * @param a_str_reply
 * @return
 */
static int s_command_handler(int a_argc, char **a_argv, void **a_str_reply, int a_version)
{
    enum {
        CMD_NONE, CMD_LIST, CMD_SHOW_NAME, CMD_RESTART, CMD_RELOAD_NAME
    };
    int l_arg_index = 1;
    int l_cmd_name = CMD_NONE;
    dap_plugin_manifest_t *l_manifest = NULL, *l_tmp = NULL;
    const char * l_cmd_arg = NULL;
    if (dap_cli_server_cmd_find_option_val(a_argv,l_arg_index, a_argc, "list", &l_cmd_arg))
        l_cmd_name = CMD_LIST;
    if (dap_cli_server_cmd_find_option_val(a_argv,l_arg_index, a_argc, "show", &l_cmd_arg))
        l_cmd_name = CMD_SHOW_NAME;
    if (dap_cli_server_cmd_find_option_val(a_argv,l_arg_index, a_argc, "restart", &l_cmd_arg))
        l_cmd_name = CMD_RESTART;
    if (dap_cli_server_cmd_find_option_val(a_argv,l_arg_index, a_argc, "reload", &l_cmd_arg))
        l_cmd_name = CMD_RELOAD_NAME;
    switch (l_cmd_name) {
        case CMD_LIST:{
            char *l_str = NULL;
            l_str = dap_strdup("|\tName plugin\t|\tVersion\t|\tAuthor(s)\t|\n");
            HASH_ITER(hh,dap_plugin_manifest_all(), l_manifest, l_tmp){
                l_str = dap_strjoin(NULL,
                                  l_str, "|\t",l_manifest->name, "\t|\t", l_manifest->version, "\t|\t", l_manifest->author, "\t|\n", NULL);

            }
            dap_cli_server_cmd_set_reply_text(a_str_reply, "%s", l_str);
            if(l_str)
                DAP_DELETE(l_str);
        }break;
        case CMD_SHOW_NAME:
            if(!l_cmd_arg){
                dap_cli_server_cmd_set_reply_text(a_str_reply, "Need argument for this command");
                break;
            }
            HASH_FIND_STR(dap_plugin_manifest_all(), l_cmd_arg, l_manifest);
            if(l_manifest){
                char *l_deps = dap_plugin_manifests_get_list_dependencies(l_manifest);
                char *l_cmds = s_plugin_manifest_cli_commands_text(l_manifest);
                if(l_cmds){
                    dap_cli_server_cmd_set_reply_text(a_str_reply,
                            " Name: %s\n Version: %s\n Author: %s\n"
                            " Description: %s\n Dependencies: %s\n%s\n",
                            l_manifest->name, l_manifest->version, l_manifest->author,
                            l_manifest->description, l_deps ? l_deps : " ", l_cmds);
                } else {
                    dap_cli_server_cmd_set_reply_text(a_str_reply,
                            " Name: %s\n Version: %s\n Author: %s\n"
                            " Description: %s\n Dependencies: %s \n\n",
                            l_manifest->name, l_manifest->version, l_manifest->author,
                            l_manifest->description, l_deps ? l_deps : " ");
                }
                DAP_DEL_Z(l_deps);
                DAP_DEL_Z(l_cmds);
            } else {
                dap_cli_server_cmd_set_reply_text(a_str_reply, "Can't find a plugin named %s", l_cmd_arg);
            }
            break;
        case CMD_RESTART:
            log_it(L_NOTICE, "Restart plugin modules");
            dap_plugin_stop_all();
            dap_plugin_load_all();
            dap_plugin_preinit_all();
            dap_plugin_start_all();
            log_it(L_NOTICE, "Restart completed");
            dap_cli_server_cmd_set_reply_text(a_str_reply, "Restart completed");
            break;
        case CMD_RELOAD_NAME:{
            if (!l_cmd_arg) {
                dap_cli_server_cmd_set_reply_text(a_str_reply, "Need argument for this command");
                break;
            }
            int l_result;
            l_result = dap_plugin_stop(l_cmd_arg);
            switch (l_result) {
                case 0: //All is good
                    break;
                case -4:
                    dap_cli_server_cmd_set_reply_text(a_str_reply, "A plugin named \"%s\" was not found.", l_cmd_arg);
                    break;
                case -5:
                    dap_cli_server_cmd_set_reply_text(a_str_reply, "A plugin named \"%s\" is not loaded", l_cmd_arg);
                    break;
                default:
                    dap_cli_server_cmd_set_reply_text(a_str_reply, "An unforeseen error has occurred.");
                    break;
            }
            if(l_result == 0){
                l_result = dap_plugin_start(l_cmd_arg);
                switch (l_result) {
                    case 0:
                        dap_cli_server_cmd_set_reply_text(a_str_reply, "Restart \"%s\" plugin is completed successfully.", l_cmd_arg);
                        break;
                    case -1:
                        dap_cli_server_cmd_set_reply_text(a_str_reply, "Plugin \"%s\" has unsupported type, pls check manifest file", l_cmd_arg);
                        break;
                    case -2:
                        dap_cli_server_cmd_set_reply_text(a_str_reply,
                                                          "\"%s\" plugin has unresolved dependencies. Restart all plugins.",
                                                          l_cmd_arg);
                        break;
                    case -3:
                        dap_cli_server_cmd_set_reply_text(a_str_reply, "Registration manifest for \"%s\" plugin is failed.", l_cmd_arg);
                        break;
                    case -4:
                        dap_cli_server_cmd_set_reply_text(a_str_reply, "Plugin \"%s\" was not found.", l_cmd_arg);
                        break;
                    case -5:
                        dap_cli_server_cmd_set_reply_text(a_str_reply, "Plugin \"%s\" can't load", l_cmd_arg);
                        break;
                    default:
                        dap_cli_server_cmd_set_reply_text(a_str_reply, "An unforeseen error has occurred.");
                        break;
                }
            }
        }break;
        default:
            dap_cli_server_cmd_set_reply_text(a_str_reply, "Arguments are incorrect.");
            break;

    }
    return 0;
}
