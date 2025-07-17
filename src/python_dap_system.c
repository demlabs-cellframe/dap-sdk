#include "../include/python_cellframe_common.h"
#include <stdio.h>
#include <time.h>
uint64_t dap_time_now(void) { return (uint64_t)time(NULL); }
const char* dap_time_to_str_rfc822(uint64_t timestamp) { static char buf[32]; sprintf(buf, "%llu", timestamp); return buf; }
void dap_log_level_set(int level) {}
void dap_log_set_external_output(int output_type, void* callback) {}
void dap_log_set_format(int format) {}
