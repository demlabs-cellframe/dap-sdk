#include "../include/python_cellframe_common.h"
int dap_config_init(void) { return 0; }
void dap_config_deinit(void) {}
void* dap_config_open(const char* path) { return (void*)1; }
void dap_config_close(void* config) {}
const char* dap_config_get_item_str(void* config, const char* section, const char* key, const char* default_value) { return default_value; }
int dap_config_get_item_int(void* config, const char* section, const char* key, int default_value) { return default_value; }
bool dap_config_get_item_bool(void* config, const char* section, const char* key, bool default_value) { return default_value; }
bool dap_config_set_item_str(void* config, const char* section, const char* key, const char* value) { return true; }
bool dap_config_set_item_int(void* config, const char* section, const char* key, int value) { return true; }
bool dap_config_set_item_bool(void* config, const char* section, const char* key, bool value) { return true; }
const char* dap_config_get_sys_dir(void) { return "/tmp"; }
const char* py_m_dap_config_get_item(const char* section, const char* key, const char* default_value) { return default_value; }
const char* py_m_dap_config_get_sys_dir(void) { return "/tmp"; }
