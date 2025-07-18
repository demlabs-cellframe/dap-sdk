#ifndef PYTHON_DAP_H
#define PYTHON_DAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Python wrapper functions - these are wrappers around DAP SDK functions
// to avoid type conflicts and provide Python-friendly interfaces

// Core DAP wrapper functions 
int py_dap_common_init(const char *console_title, const char *a_log_file);
void py_dap_common_deinit(void);

// Memory management wrappers
void* py_dap_malloc(size_t size);
void py_dap_free(void* ptr);
void* py_dap_calloc(size_t num, size_t size);
void* py_dap_realloc(void* ptr, size_t size);

// Config wrapper functions
int py_dap_config_init(const char* config_path);
void py_dap_config_deinit(void);
void* py_dap_config_open(const char* path);
void py_dap_config_close(void* config);
const char* py_dap_config_get_item_str(void* config, const char* section, const char* key, const char* default_value);
int py_dap_config_get_item_int(void* config, const char* section, const char* key, int default_value);
bool py_dap_config_get_item_bool(void* config, const char* section, const char* key, bool default_value);
int py_dap_config_set_item_str(void* config, const char* section, const char* key, const char* value);
int py_dap_config_set_item_int(void* config, const char* section, const char* key, int value);
int py_dap_config_set_item_bool(void* config, const char* section, const char* key, bool value);
const char* py_dap_config_get_sys_dir(void);
char* py_m_dap_config_get_item(void* config, const char* section, const char* key);
const char* py_m_dap_config_get_sys_dir(void);

// Crypto wrapper functions
int py_dap_crypto_init(void);
void py_dap_crypto_deinit(void);
void* py_dap_crypto_key_create(const char* type);
void py_dap_crypto_key_destroy(void* key);
int py_dap_crypto_key_sign(void* key, const void* data, size_t data_size, void* signature, size_t* signature_size);
bool py_dap_crypto_key_verify(void* key, const void* data, size_t data_size, const void* signature, size_t signature_size);

// Hash wrapper functions  
void* py_dap_hash_fast_create(const void* data, size_t size);
void* py_dap_hash_slow_create(const void* data, size_t size);

// Network wrapper functions
int py_dap_network_init(void);
void py_dap_network_deinit(void);

// System wrapper functions
char* py_exec_with_ret_multistring(const char* command);

// Time wrapper functions
int py_dap_time_to_str_rfc822(char * out, size_t out_size_max, uint64_t timestamp);
uint64_t py_dap_time_now(void);

// Logging wrapper functions
void py_dap_log_level_set(int level);
void py_dap_log_set_external_output(int output_type, void* callback);  
void py_dap_log_set_format(int format);

// Global DB wrapper functions
int py_dap_global_db_init(void);
void py_dap_global_db_deinit(void);
int py_dap_global_db_set(const char* group, const char* key, const void* value, size_t value_size);
void* py_dap_global_db_get(const char* group, const char* key, size_t* value_size);
int py_dap_global_db_del(const char* group, const char* key);

// Events wrapper functions
int py_dap_events_init(uint32_t a_worker_threads_count, uint32_t a_connections_max);
void py_dap_events_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // PYTHON_DAP_H
