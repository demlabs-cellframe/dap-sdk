#include "dap_common.h"
#include "dap_config.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_json.h"

#include "dap_plugin_manifest.h"
#include "uthash.h"
#include <string.h>

#define LOG_TAG "dap_plugin_manifest"

static dap_plugin_manifest_t* s_manifests = NULL;

static void s_manifest_delete(dap_plugin_manifest_t *a_manifest);

/**
 * @brief dap_plugin_manifest_init
 * @return
 */
int dap_plugin_manifest_init()
{
    return 0;
}

/**
 * @brief dap_plugin_manifest_deinit
 */
void dap_plugin_manifest_deinit()
{
    dap_plugin_manifest_t *l_manifest, * l_tmp;
    HASH_ITER(hh,s_manifests,l_manifest,l_tmp){
        HASH_DELETE(hh, s_manifests, l_manifest);
        s_manifest_delete(l_manifest);
    }
}


/**
 * @brief dap_plugin_manifest_add_from_scratch
 * @param a_name
 * @param a_type
 * @param a_author
 * @param a_version
 * @param a_description
 * @param a_dependencies_names
 * @param a_dependencies_count
 * @param a_params
 * @param a_params_count
 * @return
 */
dap_plugin_manifest_t* dap_plugin_manifest_add_builtin(const char *a_name, const char * a_type,
                                                            const char * a_author, const char * a_version,
                                                            const char * a_description, char ** a_dependencies_names,
                                                            size_t a_dependencies_count, char ** a_params, size_t a_params_count)
{
    dap_plugin_manifest_t *l_manifest = NULL;
    HASH_FIND_STR(s_manifests, a_name, l_manifest);
    if(l_manifest){
        log_it(L_ERROR, "Plugin name \"%s\" is already present", a_name);
        return NULL;
    }

    l_manifest = DAP_NEW_Z(dap_plugin_manifest_t);
    if (!l_manifest) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    strncpy(l_manifest->name,a_name, sizeof(l_manifest->name)-1);
    l_manifest->type = dap_strdup(a_type);
    l_manifest->is_builtin = true;
    l_manifest->author = dap_strdup(a_author);
    l_manifest->version = dap_strdup(a_version);
    l_manifest->description = dap_strdup(a_description);
    l_manifest->dependencies_names = DAP_NEW_Z_SIZE(char *, sizeof(char*)* a_dependencies_count);
    for(size_t i = 0; i < a_dependencies_count; i++){
        l_manifest->dependencies_names[i] = dap_strdup (a_dependencies_names[i]);
    }
    l_manifest->dependencies_count = a_dependencies_count;

    l_manifest->params_count = a_params_count;
    l_manifest->params = DAP_NEW_Z_SIZE(char *, sizeof(char*)* a_params_count);
    for(size_t i = 0; i < a_params_count; i++){
        l_manifest->params[i] = dap_strdup (a_params[i]);
    }
    HASH_ADD_STR(s_manifests,name,l_manifest);
    return l_manifest;
}

/**
 * @brief dap_plugin_manifest_add_from_file
 * @param file_path
 * @return
 */
dap_plugin_manifest_t* dap_plugin_manifest_add_from_file(const char *a_file_path)
{
   // Open json file_path
    dap_json_t *l_json = dap_json_from_file(a_file_path);
    
    if(!l_json) {
        log_it(L_ERROR, "Can't open manifest file on path: %s", a_file_path);
        return NULL;
    }
    
    if(!dap_json_is_object(l_json)) {
        log_it(L_ERROR, "Invalid manifest structure, shoud be a json object: %s", a_file_path);
        dap_json_object_free(l_json);
        return NULL;
    }

    // Get manifest fields - these return borrowed references (freed with l_json)
    dap_json_t *j_name = dap_json_object_get_object(l_json, "name");
    dap_json_t *j_version = dap_json_object_get_object(l_json, "version");;
    dap_json_t *j_dependencies = dap_json_object_get_array(l_json, "dependencies");
    dap_json_t *j_author = dap_json_object_get_object(l_json, "author");
    dap_json_t *j_description = dap_json_object_get_object(l_json, "description");
    dap_json_t *j_path = dap_json_object_get_object(l_json, "path");
    dap_json_t *j_params = dap_json_object_get_array(l_json, "params");
    dap_json_t *j_type = dap_json_object_get_object(l_json, "type");

    const char *l_name, *l_version, *l_author, *l_description, *l_type, *l_path;
    l_name = j_name ? dap_json_object_get_string(l_json, "name") : NULL;
    l_version = j_version ? dap_json_object_get_string(l_json, "version") : NULL;
    l_author = j_author ? dap_json_object_get_string(l_json, "author") : NULL;
    l_description = j_description ? dap_json_object_get_string(l_json, "description") : NULL;
    l_type = j_type ? dap_json_object_get_string(l_json, "type") : NULL;
    l_path = j_path ? dap_json_object_get_string(l_json, "path") : NULL;
    
    if (!l_name || !l_version || !l_author || !l_description || !l_type)
    {
        log_it(L_ERROR, "Invalid manifest structure, insuficient fields %s", a_file_path);
        // All j_* are borrowed - freed automatically with l_json
        dap_json_object_free(l_json);
        return NULL;
    }

    dap_plugin_manifest_t *l_manifest = NULL;
    HASH_FIND_STR(s_manifests, l_name, l_manifest);
    if (l_manifest) {
        // All j_* are borrowed - freed automatically with l_json
        dap_json_object_free(l_json);
        log_it(L_ERROR, "Plugin name \"%s\" is already present", l_name);
        return l_manifest;
    }

    size_t l_dependencies_count = j_dependencies ? dap_json_array_length(j_dependencies) : 0;
    size_t l_params_count =      j_params ? dap_json_array_length(j_params) : 0;

    char **l_dependencies_names = NULL, **l_params = NULL;
    // Read dependencies;
    if(l_dependencies_count)
    {
        if (!( l_dependencies_names = DAP_NEW_Z_COUNT(char*, l_dependencies_count) )) {
            // All j_* are borrowed - freed automatically with l_json
            dap_json_object_free(l_json);
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return NULL;
        }

        for (size_t i = 0; i <  l_dependencies_count; i++){
            dap_json_t *l_dep_item = dap_json_array_get_idx(j_dependencies, i);
            const char *l_dep_str = dap_json_to_string(l_dep_item);
            l_dependencies_names[i] = dap_strdup(l_dep_str);
            // l_dep_item is borrowed - freed automatically with j_dependencies
        }
    }

    // Read additional params
    if(l_params_count)
    {
        if (!( l_params = DAP_NEW_Z_COUNT(char*, l_params_count) )) {
            // All j_* are borrowed - freed automatically with l_json
            dap_json_object_free(l_json);
            DAP_DEL_ARRAY(l_dependencies_names, l_dependencies_count);
            DAP_DELETE(l_dependencies_names);
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return NULL;
        }
        for (size_t i = 0; i < l_params_count; i++){
            dap_json_t *l_param_item = dap_json_array_get_idx(j_params, i);
            const char *l_param_str = dap_json_to_string(l_param_item);
            l_params[i] = dap_strdup(l_param_str);
            // l_param_item is borrowed - freed automatically with j_params
        }
    }

    // Create manifest itself
    if (!( l_manifest = DAP_NEW_Z(dap_plugin_manifest_t) )) {
        // All j_* are borrowed - freed automatically with l_json
        dap_json_object_free(l_json);
        DAP_DEL_ARRAY(l_dependencies_names, l_dependencies_count);
        DAP_DEL_ARRAY(l_params, l_params_count);
        DAP_DEL_MULTY(l_dependencies_names, l_params);
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }

    strncpy(l_manifest->name,l_name, sizeof(l_manifest->name)-1);
    l_manifest->type = dap_strdup(l_type);
    l_manifest->author = dap_strdup(l_author);
    l_manifest->version = dap_strdup(l_version);
    l_manifest->description = dap_strdup(l_description);
    l_manifest->dependencies_names = l_dependencies_names;
    l_manifest->dependencies_count = l_dependencies_count;
    l_manifest->params_count = l_params_count;
    l_manifest->params = l_params;
    l_manifest->path = l_path ? dap_strdup(l_path) : dap_path_get_dirname(a_file_path);

    char    *l_config_path = dap_strdup_printf("%s/%s", l_manifest->path, l_manifest->name),
            *l_config_path_test = dap_strdup_printf("%s.cfg", l_config_path);
    if(dap_file_test(l_config_path_test)) // If present custom config
        l_manifest->config = dap_config_open(l_config_path);
    DAP_DEL_MULTY(l_config_path, l_config_path_test);
    HASH_ADD_STR(s_manifests, name, l_manifest);
    
    // Free all wrappers before returning success
    dap_json_object_free(j_name);
    dap_json_object_free(j_version);
    dap_json_object_free(j_dependencies);
    dap_json_object_free(j_author);
    dap_json_object_free(j_description);
    dap_json_object_free(j_path);
    dap_json_object_free(j_params);
    dap_json_object_free(j_type);
    dap_json_object_free(l_json);
    
    return l_manifest;
}

/**
 * @brief Returns all the manifests declared in system
 * @return
 */
dap_plugin_manifest_t* dap_plugin_manifest_all()
{
    return s_manifests;
}

/**
 * @brief Find plugin manifest by its unique name
 * @param a_name Plugin name
 * @return Pointer to manifest object if found or NULL if not
 */
dap_plugin_manifest_t *dap_plugin_manifest_find(const char *a_name)
{
    dap_plugin_manifest_t *l_ret = NULL;
    HASH_FIND_STR(s_manifests,a_name,l_ret);
    return l_ret;
}

/**
 * @brief Create string with list of dependencies, breaking by ","
 * @param a_element
 * @return
 */
char* dap_plugin_manifests_get_list_dependencies(dap_plugin_manifest_t *a_element)
{
    if (a_element->dependencies == NULL) {
        return NULL;
    } else {
        char *l_result = "";
        dap_plugin_manifest_dependence_t * l_dep, *l_tmp;
        HASH_ITER(hh,a_element->dependencies,l_dep,l_tmp){
            dap_plugin_manifest_t * l_dep_manifest = l_dep->manifest;
            if (l_dep->hh.hh_next )
                l_result = dap_strjoin(NULL, l_result, l_dep_manifest->name, ", ", NULL);
            else
                l_result = dap_strjoin(NULL, l_result, l_dep_manifest->name, NULL);
        }
        return l_result;
    }
}

/**
 * @brief s_manifest_delete
 * @param a_manifest
 */
static void s_manifest_delete(dap_plugin_manifest_t *a_manifest)
{
    DAP_DEL_ARRAY(a_manifest->dependencies_names, a_manifest->dependencies_count);
    DAP_DEL_ARRAY(a_manifest->params, a_manifest->params_count);
    DAP_DEL_MULTY(a_manifest->name, a_manifest->version, a_manifest->author, a_manifest->description,
                  a_manifest->type, (char*)a_manifest->path, a_manifest->dependencies_names, a_manifest->params);
    dap_plugin_manifest_dependence_t * l_dep, *l_tmp;
    HASH_ITER(hh,a_manifest->dependencies,l_dep,l_tmp){
        HASH_DELETE(hh, a_manifest->dependencies, l_dep);
        DAP_DELETE(l_dep);
    }
    if (a_manifest->params) {
        for(size_t i = 0; i < a_manifest->params_count; ++i)
            DAP_DELETE(a_manifest->params[i]);
        DAP_DELETE(a_manifest->params);
    }
    DAP_DELETE(a_manifest);
}

/**
 * @brief dap_plugins_manifest_remove
 * @param a_name
 * @return
 */
bool dap_plugins_manifest_remove(const char *a_name)
{
    dap_plugin_manifest_t *l_manifest = NULL;
    HASH_FIND_STR(s_manifests, a_name,l_manifest);
    if(l_manifest)
        HASH_DEL(s_manifests, l_manifest);
    else
        return false;

    s_manifest_delete(l_manifest);
    return true;
}

