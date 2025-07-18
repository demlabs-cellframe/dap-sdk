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

// Client wrapper functions
void* py_dap_client_new(void);
void py_dap_client_delete(void* a_client);
int py_dap_client_connect(void* a_client, const char* a_addr, uint16_t a_port);
int py_dap_client_disconnect(void* a_client);
int py_dap_client_write(void* a_client, const void* a_data, size_t a_data_size);
ssize_t py_dap_client_read(void* a_client, void* a_data, size_t a_data_size);
int py_dap_client_get_stage(void* a_client);

// Additional time wrapper functions
uint64_t py_dap_time_now_sec(void);
uint64_t py_dap_time_now_usec(void);
char* py_dap_time_to_str_rfc822(uint64_t a_timestamp_sec);
char* py_dap_time_to_str_rfc3339(uint64_t a_timestamp_sec);
uint64_t py_dap_time_from_str_rfc822(const char* a_time_str);
void py_dap_usleep(uint64_t a_microseconds);
void py_dap_sleep(uint32_t a_seconds);
uint64_t py_dap_gettimeofday(void);

// Additional logging wrapper functions
void py_dap_log_it(int a_level, const char* a_format, ...);
void py_dap_log_it_debug(const char* a_format, ...);
void py_dap_log_it_info(const char* a_format, ...);
void py_dap_log_it_warning(const char* a_format, ...);
void py_dap_log_it_error(const char* a_format, ...);
void py_dap_log_it_critical(const char* a_format, ...);

// Additional events wrapper functions  
int py_dap_events_start(void);
int py_dap_events_stop(void);
void* py_dap_events_socket_create(int a_type, void* a_callback);
void py_dap_events_socket_delete(void* a_socket);
void* py_dap_events_socket_queue_ptr(void* a_socket);
int py_dap_events_socket_assign_on_worker_mt(void* a_socket, int a_worker_num);
void py_dap_events_socket_event_proc_add(void* a_socket, uint32_t a_events, void* a_callback);
void py_dap_events_socket_event_proc_remove(void* a_socket, uint32_t a_events);

// Additional client wrapper functions
void py_dap_client_set_stage_callback(void* a_client, void* a_callback);
void py_dap_client_set_data_callback(void* a_client, void* a_callback);
void py_dap_client_set_callbacks(void* a_client, void* a_connected_cb, void* a_error_cb, void* a_delete_cb);
int py_dap_client_get_remote_addr(void* a_client, char* a_addr_buf, size_t a_addr_buf_size);
void py_dap_client_set_auth_cert(void* a_client, void* a_cert);

#ifdef __cplusplus
}
#endif

#endif // PYTHON_DAP_H
