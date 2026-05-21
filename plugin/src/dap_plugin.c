/*
* Authors:
* Alexey V. Stratulat <alexey.stratulat@demlabs.net>
* Dmitriy Gerasimov <dmitriy.gerasimov@demlabs.net
* DeM Labs Inc.   https://demlabs.net
* Copyright  (c) 2017-2022
* All rights reserved.

This file is part of DAP (Distributed Applications Platform) the open source project

   DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   DAP is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "uthash.h"
#include "dap_config.h"
#include "dap_common.h"
#include "dap_file_utils.h"
#include "dap_plugin_manifest.h"
#include "dap_plugin_command.h"
#include "dap_plugin_binary.h"

#include "dap_plugin.h"
#include "dap_strfuncs.h"
#include "dap_list.h"


#define LOG_TAG "dap_plugin"

static char *s_plugins_root_path = NULL;

struct plugin_type{
    char name[64];
    dap_plugin_type_callbacks_t callbacks;
    UT_hash_handle hh;
} * s_types;


struct plugin_module{
    char name[64];
    struct plugin_type *type;
    dap_plugin_manifest_t *manifest;

    void * pvt_data; // Here are placed type-related things
    UT_hash_handle hh;
};

struct plugin_type *s_types = NULL; // List of all registred plugin types
struct plugin_module *s_modules = NULL; // List of all loaded modules
static int s_stop(dap_plugin_manifest_t * a_manifest);
static int s_load(dap_plugin_manifest_t * a_manifest);
static int s_preinit(struct plugin_module * a_module);
static int s_init(struct plugin_module * a_module);

static void s_solve_dependencies();


/**
 * @brief dap_plugin_init
 * @param a_root_path
 * @return
 */
int dap_plugin_init(const char * a_root_path)
{
    s_plugins_root_path = dap_strdup(a_root_path);

    log_it(L_INFO, "Start plugins initialization on root path %s", s_plugins_root_path);
    if (!dap_dir_test(s_plugins_root_path)){
        log_it(L_ERROR, "Can't find \"%s\" directory", s_plugins_root_path);
        return -1;
    }

    dap_plugin_manifest_init();
    dap_plugin_command_init();
    dap_plugin_binary_init();


    //Get list files
    dap_list_name_directories_t *l_list_plugins_name = dap_get_subs(s_plugins_root_path);
    dap_list_name_directories_t *l_element;
    // Register manifests
    log_it(L_DEBUG, "Start registration of manifests");

    char *l_name_file = NULL;
    LL_FOREACH(l_list_plugins_name, l_element){
        log_it(L_NOTICE, "Registration of \"%s\" manifest", l_element->name_directory);
        l_name_file = dap_strjoin("",s_plugins_root_path, "/", l_element->name_directory, "/manifest.json", NULL);
        if (!dap_plugin_manifest_add_from_file(l_name_file)){
            log_it(L_ERROR, "Registration of \"%s\" manifest is failed", l_element->name_directory);
        }
        DAP_FREE(l_name_file);
    }

    dap_subs_free(l_list_plugins_name);
    s_solve_dependencies();
    return 0;
}

void dap_plugin_deinit(){
    log_it(L_NOTICE, "Deinitialize plugins");
    dap_plugin_stop_all();
    dap_plugin_binary_deinit();
    dap_plugin_manifest_deinit();
    dap_plugin_command_deinit();
}

/**
 * @brief dap_plugin_root_path
 * @return Root path used for plugin discovery
 */
const char *dap_plugin_root_path(void)
{
    return s_plugins_root_path;
}



/**
 * @brief s_solve_dependencies
 */
static void s_solve_dependencies()
{
    // TODO solving dependencies
}


/**
 * @brief Create new plugin type. Same name will be new plugin itself to make dependencies from plugin type as from plugin
 * @param a_name Plugin type name
 * @param a_callbacks Set of callbacks
 * @return Returns 0 if success otherwise if not
 */
int dap_plugin_type_create(const char* a_name, dap_plugin_type_callbacks_t* a_callbacks)
{
    if(!a_name){
        log_it(L_CRITICAL, "Can't create plugin type without name!");
        return -1;
    }
    if(!a_callbacks){
        log_it(L_CRITICAL, "Can't create plugin type without callbacks!");
        return -2;
    }
    struct plugin_type * l_type = DAP_NEW_Z(struct plugin_type);
    if(!l_type){
        log_it(L_CRITICAL, "OOM on new type create");
        return -3;
    }
    strncpy(l_type->name,a_name, sizeof(l_type->name)-1);
    memcpy(&l_type->callbacks,a_callbacks,sizeof(l_type->callbacks));
    HASH_ADD_STR(s_types,name,l_type);
    log_it(L_NOTICE, "Plugin type \"%s\" added", a_name);
    return 0;
}

/**
 * @brief dap_plugin_load_all
 * Load all registered plugins (dlopen/import) without calling preinit or init
 */
void dap_plugin_load_all()
{
    dap_plugin_manifest_t * l_manifest, *l_tmp;
    HASH_ITER(hh, dap_plugin_manifest_all(), l_manifest, l_tmp) {
        s_load(l_manifest);
    }
}

/**
 * @brief dap_plugin_preinit_all
 * Call preinit callback on all loaded modules (before chains load)
 */
void dap_plugin_preinit_all()
{
    struct plugin_module * l_module, *l_tmp;
    HASH_ITER(hh, s_modules, l_module, l_tmp) {
        s_preinit(l_module);
    }
}

/**
 * @brief dap_plugin_start_all
 * Call init callback on all loaded modules (after chains load)
 */
void dap_plugin_start_all()
{
    struct plugin_module * l_module, *l_tmp;
    HASH_ITER(hh, s_modules, l_module, l_tmp) {
        s_init(l_module);
    }
}

/**
 * @brief dap_plugin_stop_all
 */
void dap_plugin_stop_all()
{
    dap_plugin_manifest_t * l_manifest, *l_tmp;
    HASH_ITER(hh,dap_plugin_manifest_all(),l_manifest,l_tmp ){
        s_stop(l_manifest);
    }
}

/**
 * @brief dap_plugin_stop
 * @param a_name
 * @return
 */
int dap_plugin_stop(const char * a_name)
{
    dap_plugin_manifest_t * l_manifest = dap_plugin_manifest_find(a_name);
    if(l_manifest)
        return s_stop(l_manifest);
    else
        return -4; // Not found

}

/**
 * @brief Stop services by manifest
 * @param a_manifest
 * @return
 */
static int s_stop(dap_plugin_manifest_t * a_manifest)
{
    if(!a_manifest)
        return -4;
    struct plugin_module * l_module = NULL;
    HASH_FIND_STR(s_modules, a_manifest->name , l_module);
    if(! l_module){
        log_it(L_ERROR, "Plugin \"%s\" is not loaded", a_manifest->type);
        return -5;
    }
    // unload plugin
    char * l_err_str = NULL;
    int l_ret = l_module->type->callbacks.unload(a_manifest,l_module->pvt_data, &l_err_str);
    if(l_ret){ // Error while unloading
        log_it(L_ERROR, "Can't unload plugin \"%s\" because of error \"%s\" (code %d)",a_manifest->name,
               l_err_str?l_err_str:"<UNKNOWN>", l_ret);
        DAP_DELETE(l_err_str);
    }else{
        HASH_DELETE(hh, s_modules,l_module);
        DAP_DELETE(l_module);
    }
    return l_ret;
}

/**
 * @brief dap_plugin_start
 * @param a_name
 * @return
 */
int dap_plugin_start(const char * a_name)
{
    dap_plugin_manifest_t * l_manifest = dap_plugin_manifest_find(a_name);
    if (!l_manifest)
        return -4; // Not found
    int l_ret = s_load(l_manifest);
    if (l_ret)
        return l_ret;
    struct plugin_module * l_module = NULL;
    HASH_FIND_STR(s_modules, a_name, l_module);
    if (!l_module)
        return -5;
    s_preinit(l_module);
    s_init(l_module);
    return 0;
}

/**
 * @brief s_load
 * Load a single plugin: dlopen/import, add to s_modules
 */
static int s_load(dap_plugin_manifest_t * a_manifest)
{
    struct plugin_type * l_type = NULL;
    HASH_FIND_STR(s_types, a_manifest->type, l_type);
    if (!l_type) {
        log_it(L_ERROR, "Plugin \"%s\" with type \"%s\" is not recognized", a_manifest->name, a_manifest->type);
        return -1;
    }
    if (a_manifest->dependencies != NULL) {
        log_it(L_NOTICE, "Check for plugin %s dependencies", a_manifest->name);
        bool l_is_unsolved = false;
        for (size_t i = 0; i < a_manifest->dependencies_count; i++) {
            dap_plugin_manifest_dependence_t * l_dep = NULL;
            HASH_FIND_STR(a_manifest->dependencies, a_manifest->dependencies_names[i], l_dep);
            if (!l_dep) {
                log_it(L_ERROR, "Unsolved dependence \"%s\"", a_manifest->dependencies_names[i]);
                l_is_unsolved = true;
            }
        }
        if (l_is_unsolved)
            return -2;
    }

    char * l_err_str = NULL;
    void * l_pvt_data = NULL;
    int l_ret = l_type->callbacks.load(a_manifest, &l_pvt_data, &l_err_str);
    if (l_ret) {
        log_it(L_ERROR, "Can't load plugin \"%s\" because of error \"%s\" (code %d)", a_manifest->name,
               l_err_str ? l_err_str : "<UNKNOWN>", l_ret);
        DAP_DELETE(l_err_str);
    } else {
        struct plugin_module * l_module = DAP_NEW_Z(struct plugin_module);
        if (!l_module) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return -1;
        }
        l_module->pvt_data = l_pvt_data;
        strncpy(l_module->name, a_manifest->name, sizeof(l_module->name) - 1);
        l_module->name[sizeof(l_module->name) - 1] = '\0';
        l_module->type = l_type;
        l_module->manifest = a_manifest;
        HASH_ADD_STR(s_modules, name, l_module);
        log_it(L_NOTICE, "Plugin \"%s\" is loaded", a_manifest->name);
    }
    return l_ret;
}

/**
 * @brief s_preinit
 * Call preinit callback on a loaded module (optional, skips if not set)
 */
static int s_preinit(struct plugin_module * a_module)
{
    if (!a_module || !a_module->type->callbacks.preinit)
        return 0;
    char * l_err_str = NULL;
    int l_ret = a_module->type->callbacks.preinit(a_module->manifest, a_module->pvt_data, &l_err_str);
    if (l_ret) {
        log_it(L_ERROR, "Preinit failed for plugin \"%s\": \"%s\" (code %d)", a_module->name,
               l_err_str ? l_err_str : "<UNKNOWN>", l_ret);
        DAP_DELETE(l_err_str);
    } else {
        log_it(L_DEBUG, "Plugin \"%s\" preinit completed", a_module->name);
    }
    return l_ret;
}

/**
 * @brief s_init
 * Call init callback on a loaded module (optional, skips if not set)
 */
static int s_init(struct plugin_module * a_module)
{
    if (!a_module || !a_module->type->callbacks.init)
        return 0;
    char * l_err_str = NULL;
    int l_ret = a_module->type->callbacks.init(a_module->manifest, a_module->pvt_data, &l_err_str);
    if (l_ret) {
        log_it(L_ERROR, "Init failed for plugin \"%s\": \"%s\" (code %d)", a_module->name,
               l_err_str ? l_err_str : "<UNKNOWN>", l_ret);
        DAP_DELETE(l_err_str);
    } else {
        log_it(L_DEBUG, "Plugin \"%s\" init completed", a_module->name);
    }
    return l_ret;
}

/**
 * @brief dap_plugin_status
 * @param a_name
 * @return
 */
dap_plugin_status_t dap_plugin_status(const char * a_name)
{
    struct plugin_module * l_module = NULL;
    HASH_FIND_STR(s_modules,a_name,l_module);
    if(l_module){
        return STATUS_RUNNING;
    }
    dap_plugin_manifest_t * l_manifest = dap_plugin_manifest_find(a_name);
    if(l_manifest)
        return STATUS_STOPPED;
    return STATUS_NONE;
}



