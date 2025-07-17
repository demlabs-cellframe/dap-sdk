/*
 * Python DAP Config Implementation
 * Wrapper functions around DAP SDK config functions
 */

#include "python_dap.h"
#include "dap_config.h"
#include "dap_strfuncs.h"
#include <errno.h>

// Config wrapper implementations

int py_dap_config_init(const char* config_path) {
    return dap_config_init(config_path);
}

void py_dap_config_deinit(void) {
    dap_config_deinit();
}

void* py_dap_config_open(const char* path) {
    return (void*)dap_config_open(path);
}

void py_dap_config_close(void* config) {
    if (config) {
        dap_config_close((dap_config_t*)config);
    }
}

const char* py_dap_config_get_item_str(void* config, const char* section, const char* key, const char* default_value) {
    if (!config || !section || !key) {
        return default_value;
    }
    return dap_config_get_item_str_default((dap_config_t*)config, section, key, default_value);
}

char* py_m_dap_config_get_item(void* config, const char* section, const char* key) {
    if (!config || !section || !key) {
        return NULL;
    }
    
    const char* value = dap_config_get_item_str((dap_config_t*)config, section, key);
    return value ? dap_strdup(value) : NULL;
}

const char* py_dap_config_get_sys_dir(void) {
    // Simplified implementation
    return "/tmp";
}

const char* py_m_dap_config_get_sys_dir(void) {
    // Simplified implementation  
    return "/tmp";
}

// System directory getter - simplified
char* py_m_dap_config_get_sys_dir_path(void) {
    return dap_strdup("/opt/dap");
}

// Memory allocation wrapper
void* py_m_dap_new_size(size_t size) {
    return DAP_NEW_SIZE(uint8_t, size);
}

// Memory deallocation wrapper
void py_m_dap_delete(void* ptr) {
    if (ptr) {
        DAP_DELETE(ptr);
    }
} 