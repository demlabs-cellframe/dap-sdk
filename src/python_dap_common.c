#include "../include/python_cellframe_common.h"
int dap_global_db_init(void) { return 0; }
void dap_global_db_deinit(void) {}
int dap_global_db_set(const char* group, const char* key, const void* value, size_t value_size) { return 0; }
void* dap_global_db_get(const char* group, const char* key, size_t* value_size) { if(value_size) *value_size=0; return NULL; }
bool dap_global_db_del(const char* group, const char* key) { return true; }
int dap_events_init(void) { return 0; }
void dap_events_deinit(void) {}
void* dap_events_new(void) { return (void*)1; }
void dap_events_delete(void* events) {}
int dap_events_subscribe(void* events, const char* event_type, void* callback) { return 1; }
void dap_events_unsubscribe(void* events, int subscription_id) {}
int dap_events_emit(void* events, const char* event_type, const void* data, size_t data_size) { return 0; }
