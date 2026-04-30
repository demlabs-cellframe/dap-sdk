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
#include <stddef.h>
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

static bool s_debug_more = false;
static char *s_plugins_root_path = NULL;

typedef struct plugin_type_callbacks_internal{
    dap_plugin_type_callback_load_t load;
    dap_plugin_type_callback_unload_t unload;
    dap_plugin_type_callback_preinit_t preinit;
    dap_plugin_type_callback_init_t init;
} plugin_type_callbacks_internal_t;

struct plugin_type{
    char name[64];
    plugin_type_callbacks_internal_t callbacks;
    UT_hash_handle hh;
};

typedef enum plugin_module_state{
    PLUGIN_MODULE_LOADED,
    PLUGIN_MODULE_PREINITED,
    PLUGIN_MODULE_RUNNING,
    PLUGIN_MODULE_FAILED
} plugin_module_state_t;

struct plugin_module{
    char name[64];
    struct plugin_type *type;
    dap_plugin_manifest_t *manifest;

    void * pvt_data; // Here are placed type-related things
    plugin_module_state_t state;
    UT_hash_handle hh;
};

typedef struct plugin_start_rollback_item {
    dap_plugin_manifest_t *manifest;
    struct plugin_start_rollback_item *next;
} plugin_start_rollback_item_t;

struct plugin_type *s_types = NULL; // List of all registred plugin types
struct plugin_module *s_modules = NULL; // List of all loaded modules
static int s_stop(dap_plugin_manifest_t * a_manifest);
static int s_load(dap_plugin_manifest_t * a_manifest);
static int s_load_with_deps(dap_plugin_manifest_t * a_manifest, size_t a_depth);
static int s_start_with_deps(dap_plugin_manifest_t * a_manifest, size_t a_depth,
                             plugin_start_rollback_item_t **a_rollback);
static int s_preinit(struct plugin_module * a_module);
static int s_init(struct plugin_module * a_module);
static int s_plugin_type_create_internal(const char * a_name, const plugin_type_callbacks_internal_t * a_callbacks);
static int s_check_dependencies_loaded(dap_plugin_manifest_t * a_manifest);
static int s_check_dependencies_preinited(dap_plugin_manifest_t * a_manifest);
static int s_check_dependencies_running(dap_plugin_manifest_t * a_manifest);
static int s_rollback_push(plugin_start_rollback_item_t **a_rollback, dap_plugin_manifest_t *a_manifest);
static void s_rollback_run(plugin_start_rollback_item_t **a_rollback);
static void s_rollback_free(plugin_start_rollback_item_t **a_rollback);

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
    if(!a_callbacks){
        log_it(L_CRITICAL, "Can't create plugin type without callbacks!");
        return -2;
    }
    plugin_type_callbacks_internal_t l_callbacks = {
        .load = a_callbacks->load,
        .unload = a_callbacks->unload
    };
    return s_plugin_type_create_internal(a_name, &l_callbacks);
}

int dap_plugin_type_create_ex(const char* a_name, const dap_plugin_type_callbacks_ex_t *a_callbacks)
{
    if(!a_callbacks){
        log_it(L_CRITICAL, "Can't create plugin type without callbacks!");
        return -2;
    }
    if(a_callbacks->size < offsetof(dap_plugin_type_callbacks_ex_t, unload) + sizeof(a_callbacks->unload)){
        log_it(L_CRITICAL, "Can't create plugin type with too small callback descriptor!");
        return -2;
    }
    plugin_type_callbacks_internal_t l_callbacks = {
        .load = a_callbacks->load,
        .unload = a_callbacks->unload
    };
    if(a_callbacks->size >= offsetof(dap_plugin_type_callbacks_ex_t, preinit) + sizeof(a_callbacks->preinit))
        l_callbacks.preinit = a_callbacks->preinit;
    if(a_callbacks->size >= offsetof(dap_plugin_type_callbacks_ex_t, init) + sizeof(a_callbacks->init))
        l_callbacks.init = a_callbacks->init;
    return s_plugin_type_create_internal(a_name, &l_callbacks);
}

static int s_plugin_type_create_internal(const char * a_name, const plugin_type_callbacks_internal_t * a_callbacks)
{
    if(!a_name){
        log_it(L_CRITICAL, "Can't create plugin type without name!");
        return -1;
    }
    struct plugin_type * l_type = DAP_NEW_Z(struct plugin_type);
    if(!l_type){
        log_it(L_CRITICAL, "OOM on new type create");
        return -3;
    }
    strncpy(l_type->name,a_name, sizeof(l_type->name)-1);
    l_type->callbacks = *a_callbacks;
    HASH_ADD_STR(s_types,name,l_type);
    log_it(L_NOTICE, "Plugin type \"%s\" added", a_name);
    return 0;
}

/**
 * @brief dap_plugin_load_all
 * Load all registered plugins (dlopen/import) without calling preinit or init
 */
int dap_plugin_load_all(void)
{
    int l_errors = 0;
    dap_plugin_manifest_t * l_manifest, *l_tmp;
    HASH_ITER(hh, dap_plugin_manifest_all(), l_manifest, l_tmp) {
        if (s_load(l_manifest))
            l_errors++;
    }
    return l_errors;
}

/**
 * @brief dap_plugin_preinit_all
 * Call preinit callback on all loaded modules (before chains load)
 */
int dap_plugin_preinit_all(void)
{
    int l_errors = 0;
    struct plugin_module * l_module, *l_tmp;
    HASH_ITER(hh, s_modules, l_module, l_tmp) {
        if (s_preinit(l_module)) {
            l_errors++;
            int l_stop_ret = s_stop(l_module->manifest);
            if (l_stop_ret)
                log_it(L_WARNING, "Rollback failed for plugin \"%s\" after preinit error, stop code %d",
                       l_module->name, l_stop_ret);
        }
    }
    return l_errors;
}

/**
 * Call init callback on all loaded modules (after chains load)
 */
int dap_plugin_init_all(void)
{
    int l_errors = 0;
    struct plugin_module * l_module, *l_tmp;
    HASH_ITER(hh, s_modules, l_module, l_tmp) {
        if (s_init(l_module)) {
            l_errors++;
            int l_stop_ret = s_stop(l_module->manifest);
            if (l_stop_ret)
                log_it(L_WARNING, "Rollback failed for plugin \"%s\" after init error, stop code %d",
                       l_module->name, l_stop_ret);
        }
    }
    return l_errors;
}

/**
 * @brief dap_plugin_start_all
 * Load, preinit, and init all registered plugins.
 */
int dap_plugin_start_all(void)
{
    int l_errors = dap_plugin_load_all();
    l_errors += dap_plugin_preinit_all();
    l_errors += dap_plugin_init_all();
    return l_errors;
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
    int l_ret = l_module->type->callbacks.unload
            ? l_module->type->callbacks.unload(a_manifest,l_module->pvt_data, &l_err_str)
            : 0;
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
    plugin_start_rollback_item_t *l_rollback = NULL;
    int l_ret = s_start_with_deps(l_manifest, 0, &l_rollback);
    if (l_ret)
        s_rollback_run(&l_rollback);
    else
        s_rollback_free(&l_rollback);
    return l_ret;
}

static int s_rollback_push(plugin_start_rollback_item_t **a_rollback, dap_plugin_manifest_t *a_manifest)
{
    if (!a_rollback || !a_manifest)
        return -4;
    for (plugin_start_rollback_item_t *l_item = *a_rollback; l_item; l_item = l_item->next)
        if (l_item->manifest == a_manifest)
            return 0;
    plugin_start_rollback_item_t *l_item = DAP_NEW_Z(plugin_start_rollback_item_t);
    if (!l_item) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return -3;
    }
    l_item->manifest = a_manifest;
    l_item->next = *a_rollback;
    *a_rollback = l_item;
    return 0;
}

static void s_rollback_run(plugin_start_rollback_item_t **a_rollback)
{
    if (!a_rollback)
        return;
    while (*a_rollback) {
        plugin_start_rollback_item_t *l_item = *a_rollback;
        *a_rollback = l_item->next;
        int l_stop_ret = s_stop(l_item->manifest);
        if (l_stop_ret && l_stop_ret != -5)
            log_it(L_WARNING, "Rollback failed for plugin \"%s\", stop code %d",
                   l_item->manifest->name, l_stop_ret);
        DAP_DELETE(l_item);
    }
}

static void s_rollback_free(plugin_start_rollback_item_t **a_rollback)
{
    if (!a_rollback)
        return;
    while (*a_rollback) {
        plugin_start_rollback_item_t *l_item = *a_rollback;
        *a_rollback = l_item->next;
        DAP_DELETE(l_item);
    }
}

/**
 * @brief s_load
 * Load a single plugin: dlopen/import, add to s_modules
 */
static int s_load(dap_plugin_manifest_t * a_manifest)
{
    return s_load_with_deps(a_manifest, 0);
}

static int s_check_dependencies_loaded(dap_plugin_manifest_t * a_manifest)
{
    if (!a_manifest)
        return -4;
    for (size_t i = 0; i < a_manifest->dependencies_count; i++) {
        const char *l_dep_name = a_manifest->dependencies_names[i];
        dap_plugin_manifest_t *l_dep_manifest = dap_plugin_manifest_find(l_dep_name);
        if (!l_dep_manifest) {
            log_it(L_ERROR, "Plugin \"%s\" has unresolved dependency \"%s\"", a_manifest->name, l_dep_name);
            return -2;
        }
        struct plugin_module *l_dep_module = NULL;
        HASH_FIND_STR(s_modules, l_dep_name, l_dep_module);
        if (!l_dep_module || l_dep_module->state == PLUGIN_MODULE_FAILED) {
            log_it(L_ERROR, "Plugin \"%s\" dependency \"%s\" is not loaded", a_manifest->name, l_dep_name);
            return -6;
        }
    }
    return 0;
}

static int s_check_dependencies_preinited(dap_plugin_manifest_t * a_manifest)
{
    if (!a_manifest)
        return -4;
    for (size_t i = 0; i < a_manifest->dependencies_count; i++) {
        const char *l_dep_name = a_manifest->dependencies_names[i];
        dap_plugin_manifest_t *l_dep_manifest = dap_plugin_manifest_find(l_dep_name);
        if (!l_dep_manifest) {
            log_it(L_ERROR, "Plugin \"%s\" has unresolved dependency \"%s\"", a_manifest->name, l_dep_name);
            return -2;
        }
        struct plugin_module *l_dep_module = NULL;
        HASH_FIND_STR(s_modules, l_dep_name, l_dep_module);
        if (!l_dep_module || (l_dep_module->state != PLUGIN_MODULE_PREINITED &&
                              l_dep_module->state != PLUGIN_MODULE_RUNNING)) {
            log_it(L_ERROR, "Plugin \"%s\" dependency \"%s\" did not complete preinit", a_manifest->name, l_dep_name);
            return -6;
        }
    }
    return 0;
}

static int s_check_dependencies_running(dap_plugin_manifest_t * a_manifest)
{
    if (!a_manifest)
        return -4;
    for (size_t i = 0; i < a_manifest->dependencies_count; i++) {
        const char *l_dep_name = a_manifest->dependencies_names[i];
        dap_plugin_manifest_t *l_dep_manifest = dap_plugin_manifest_find(l_dep_name);
        if (!l_dep_manifest) {
            log_it(L_ERROR, "Plugin \"%s\" has unresolved dependency \"%s\"", a_manifest->name, l_dep_name);
            return -2;
        }
        struct plugin_module *l_dep_module = NULL;
        HASH_FIND_STR(s_modules, l_dep_name, l_dep_module);
        if (!l_dep_module || l_dep_module->state != PLUGIN_MODULE_RUNNING) {
            log_it(L_ERROR, "Plugin \"%s\" dependency \"%s\" is not running", a_manifest->name, l_dep_name);
            return -6;
        }
    }
    return 0;
}

/**
 * @brief s_load_with_deps
 * Load a single plugin and its dependencies: dlopen/import, add to s_modules
 */
static int s_load_with_deps(dap_plugin_manifest_t * a_manifest, size_t a_depth)
{
    if (!a_manifest)
        return -4;
    struct plugin_module * l_module = NULL;
    HASH_FIND_STR(s_modules, a_manifest->name, l_module);
    if (l_module)
        return l_module->state == PLUGIN_MODULE_FAILED ? -6 : 0;

    struct plugin_type * l_type = NULL;
    HASH_FIND_STR(s_types, a_manifest->type, l_type);
    if (!l_type) {
        log_it(L_ERROR, "Plugin \"%s\" with type \"%s\" is not recognized", a_manifest->name, a_manifest->type);
        return -1;
    }
    if (!l_type->callbacks.load) {
        log_it(L_ERROR, "Plugin \"%s\" type \"%s\" has no load callback", a_manifest->name, a_manifest->type);
        return -1;
    }
    if (a_depth > HASH_COUNT(dap_plugin_manifest_all())) {
        log_it(L_ERROR, "Plugin \"%s\" has recursive dependencies", a_manifest->name);
        return -2;
    }
    if (a_manifest->dependencies_count) {
        log_it(L_NOTICE, "Check for plugin %s dependencies", a_manifest->name);
        for (size_t i = 0; i < a_manifest->dependencies_count; i++) {
            dap_plugin_manifest_t *l_dep_manifest = dap_plugin_manifest_find(a_manifest->dependencies_names[i]);
            if (!l_dep_manifest) {
                log_it(L_ERROR, "Plugin \"%s\" has unresolved dependency \"%s\"",
                       a_manifest->name, a_manifest->dependencies_names[i]);
                return -2;
            }
            int l_dep_ret = s_load_with_deps(l_dep_manifest, a_depth + 1);
            if (l_dep_ret) {
                log_it(L_ERROR, "Plugin \"%s\" dependency \"%s\" failed to load",
                       a_manifest->name, a_manifest->dependencies_names[i]);
                return l_dep_ret;
            }
        }
        int l_dep_state_ret = s_check_dependencies_loaded(a_manifest);
        if (l_dep_state_ret)
            return l_dep_state_ret;
    }

    char * l_err_str = NULL;
    void * l_pvt_data = NULL;
    int l_ret = l_type->callbacks.load(a_manifest, &l_pvt_data, &l_err_str);
    if (l_ret) {
        log_it(L_ERROR, "Can't load plugin \"%s\" because of error \"%s\" (code %d)", a_manifest->name,
               l_err_str ? l_err_str : "<UNKNOWN>", l_ret);
        DAP_DELETE(l_err_str);
    } else {
        l_module = DAP_NEW_Z(struct plugin_module);
        if (!l_module) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            if (l_type->callbacks.unload && l_pvt_data) {
                char *l_unload_err_str = NULL;
                int l_unload_ret = l_type->callbacks.unload(a_manifest, l_pvt_data, &l_unload_err_str);
                if (l_unload_ret)
                    log_it(L_WARNING, "Cleanup failed for plugin \"%s\" after load bookkeeping error: \"%s\" (code %d)",
                           a_manifest->name, l_unload_err_str ? l_unload_err_str : "<UNKNOWN>", l_unload_ret);
                DAP_DELETE(l_unload_err_str);
            }
            return -1;
        }
        l_module->pvt_data = l_pvt_data;
        strncpy(l_module->name, a_manifest->name, sizeof(l_module->name) - 1);
        l_module->name[sizeof(l_module->name) - 1] = '\0';
        l_module->type = l_type;
        l_module->manifest = a_manifest;
        l_module->state = PLUGIN_MODULE_LOADED;
        HASH_ADD_STR(s_modules, name, l_module);
        log_it(L_NOTICE, "Plugin \"%s\" is loaded", a_manifest->name);
    }
    return l_ret;
}

static int s_start_with_deps(dap_plugin_manifest_t * a_manifest, size_t a_depth,
                             plugin_start_rollback_item_t **a_rollback)
{
    if (!a_manifest)
        return -4;
    if (!a_rollback)
        return -4;
    if (a_depth > HASH_COUNT(dap_plugin_manifest_all())) {
        log_it(L_ERROR, "Plugin \"%s\" has recursive dependencies", a_manifest->name);
        return -2;
    }

    for (size_t i = 0; i < a_manifest->dependencies_count; i++) {
        dap_plugin_manifest_t *l_dep_manifest = dap_plugin_manifest_find(a_manifest->dependencies_names[i]);
        if (!l_dep_manifest) {
            log_it(L_ERROR, "Plugin \"%s\" has unresolved dependency \"%s\"",
                   a_manifest->name, a_manifest->dependencies_names[i]);
            return -2;
        }
        int l_dep_ret = s_start_with_deps(l_dep_manifest, a_depth + 1, a_rollback);
        if (l_dep_ret) {
            log_it(L_ERROR, "Plugin \"%s\" dependency \"%s\" failed to start",
                   a_manifest->name, a_manifest->dependencies_names[i]);
            return l_dep_ret;
        }
    }

    int l_ret = s_load(a_manifest);
    if (l_ret)
        return l_ret;

    struct plugin_module * l_module = NULL;
    HASH_FIND_STR(s_modules, a_manifest->name, l_module);
    if (!l_module)
        return -5;

    bool l_should_rollback = l_module->state != PLUGIN_MODULE_RUNNING;
    l_ret = s_preinit(l_module);
    if (l_ret) {
        int l_stop_ret = s_stop(a_manifest);
        if (l_stop_ret)
            log_it(L_WARNING, "Rollback failed for plugin \"%s\" after preinit error %d, stop code %d",
                   a_manifest->name, l_ret, l_stop_ret);
        return l_ret;
    }

    l_ret = s_init(l_module);
    if (l_ret) {
        int l_stop_ret = s_stop(a_manifest);
        if (l_stop_ret)
            log_it(L_WARNING, "Rollback failed for plugin \"%s\" after init error %d, stop code %d",
                   a_manifest->name, l_ret, l_stop_ret);
        return l_ret;
    }

    if (l_should_rollback) {
        l_ret = s_rollback_push(a_rollback, a_manifest);
        if (l_ret) {
            int l_stop_ret = s_stop(a_manifest);
            if (l_stop_ret)
                log_it(L_WARNING, "Rollback failed for plugin \"%s\" after rollback bookkeeping error %d, stop code %d",
                       a_manifest->name, l_ret, l_stop_ret);
            return l_ret;
        }
    }
    return 0;
}

/**
 * @brief s_preinit
 * Call preinit callback on a loaded module (optional, skips if not set)
 */
static int s_preinit(struct plugin_module * a_module)
{
    if (!a_module)
        return -4;
    if (a_module->state == PLUGIN_MODULE_FAILED)
        return -6;
    if (a_module->state == PLUGIN_MODULE_PREINITED || a_module->state == PLUGIN_MODULE_RUNNING)
        return 0;
    int l_dep_ret = s_check_dependencies_preinited(a_module->manifest);
    if (l_dep_ret)
        return l_dep_ret;
    if (!a_module->type->callbacks.preinit) {
        a_module->state = PLUGIN_MODULE_PREINITED;
        return 0;
    }
    char * l_err_str = NULL;
    int l_ret = a_module->type->callbacks.preinit(a_module->manifest, a_module->pvt_data, &l_err_str);
    if (l_ret) {
        a_module->state = PLUGIN_MODULE_FAILED;
        log_it(L_ERROR, "Preinit failed for plugin \"%s\": \"%s\" (code %d)", a_module->name,
               l_err_str ? l_err_str : "<UNKNOWN>", l_ret);
        DAP_DELETE(l_err_str);
    } else {
        a_module->state = PLUGIN_MODULE_PREINITED;
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
    if (!a_module)
        return -4;
    if (a_module->state == PLUGIN_MODULE_RUNNING)
        return 0;
    if (a_module->state == PLUGIN_MODULE_FAILED)
        return -6;
    if (a_module->state != PLUGIN_MODULE_PREINITED) {
        log_it(L_WARNING, "Plugin \"%s\" init skipped because preinit phase was not completed", a_module->name);
        return -6;
    }
    int l_dep_ret = s_check_dependencies_running(a_module->manifest);
    if (l_dep_ret)
        return l_dep_ret;
    if (!a_module->type->callbacks.init) {
        a_module->state = PLUGIN_MODULE_RUNNING;
        return 0;
    }
    char * l_err_str = NULL;
    int l_ret = a_module->type->callbacks.init(a_module->manifest, a_module->pvt_data, &l_err_str);
    if (l_ret) {
        a_module->state = PLUGIN_MODULE_FAILED;
        log_it(L_ERROR, "Init failed for plugin \"%s\": \"%s\" (code %d)", a_module->name,
               l_err_str ? l_err_str : "<UNKNOWN>", l_ret);
        DAP_DELETE(l_err_str);
    } else {
        a_module->state = PLUGIN_MODULE_RUNNING;
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
    if(l_module && l_module->state == PLUGIN_MODULE_RUNNING){
        return STATUS_RUNNING;
    }
    dap_plugin_manifest_t * l_manifest = dap_plugin_manifest_find(a_name);
    if(l_manifest)
        return STATUS_STOPPED;
    return STATUS_NONE;
}
