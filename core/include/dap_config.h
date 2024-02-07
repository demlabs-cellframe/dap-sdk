#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "uthash.h"

typedef struct dap_conf {
    char *path;
    struct dap_config_item {
        char type, *name;
        union dap_config_val {
            bool        val_bool;
            char        *val_str;
            char        **val_arr;
            int64_t     val_int;
        } val;
        UT_hash_handle hh;
    } *items;
    UT_hash_handle hh;
} dap_config_t;

#ifdef __cplusplus
extern "C" {
#endif

#if 0
extern dap_config_t *g_configs_table;
#endif
extern dap_config_t *g_config;

int dap_config_init(const char*);
dap_config_t *dap_config_open(const char*);

void dap_config_close(dap_config_t*);
void dap_config_deinit();

const char *dap_config_path();

bool dap_config_get_item_bool_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, bool a_default);
int64_t _dap_config_get_item_int(dap_config_t *a_config, const char *a_section, const char *a_item_name, int64_t a_default);
uint64_t _dap_config_get_item_uint(dap_config_t *a_config, const char *a_section, const char *a_item_name, uint64_t a_default);
const char *dap_config_get_item_str_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, const char *a_default);
const char *dap_config_get_item_str_path_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, const char *a_default);
char** dap_config_get_array_str(dap_config_t *a_config, const char *a_section, const char *a_item_name, uint16_t *array_length);
double dap_config_get_item_double_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, double a_default);

#define dap_config_get_item_bool(a_conf, a_path, a_item) dap_config_get_item_bool_default(a_conf, a_path, a_item, false)

#define dap_config_get_item_str(a_conf, a_path, a_item) dap_config_get_item_str_default(a_conf, a_path, a_item, NULL)

#define dap_config_get_item_uint16(a_conf, a_path, a_item) (uint16_t)_dap_config_get_item_uint(a_conf, a_path, a_item, 0)
#define dap_config_get_item_uint16_default(a_conf, a_path, a_item, a_default) (uint16_t)_dap_config_get_item_uint(a_conf, a_path, a_item, a_default)

#define dap_config_get_item_int16(a_conf, a_path, a_item) (int16_t)_dap_config_get_item_int(a_conf, a_path, a_item, 0)
#define dap_config_get_item_int16_default(a_conf, a_path, a_item, a_default) (int16_t)_dap_config_get_item_int(a_conf, a_path, a_item, a_default)

#define dap_config_get_item_uint32(a_conf, a_path, a_item) (uint32_t)_dap_config_get_item_uint(a_conf, a_path, a_item, 0)
#define dap_config_get_item_uint32_default(a_conf, a_path, a_item, a_default) (uint32_t)_dap_config_get_item_uint(a_conf, a_path, a_item, a_default)

#define dap_config_get_item_int32(a_conf, a_path, a_item) (int32_t)_dap_config_get_item_int(a_conf, a_path, a_item, 0)
#define dap_config_get_item_int32_default(a_conf, a_path, a_item, a_default) (int32_t)_dap_config_get_item_int(a_conf, a_path, a_item, a_default)

#define dap_config_get_item_uint64(a_conf, a_path, a_item) (uint64_t)_dap_config_get_item_uint(a_conf, a_path, a_item, 0)
#define dap_config_get_item_uint64_default(a_conf, a_path, a_item, a_default) (uint64_t)_dap_config_get_item_uint(a_conf, a_path, a_item, a_default)

#define dap_config_get_item_int64(a_conf, a_path, a_item) (int64_t)_dap_config_get_item_int(a_conf, a_path, a_item, 0)
#define dap_config_get_item_int64_default(a_conf, a_path, a_item, a_default) (int64_t)_dap_config_get_item_int(a_conf, a_path, a_item, a_default)

#define dap_config_get_item_path(a_conf, a_path, a_item) dap_config_get_item_str_path_default(a_conf, a_path, a_item, NULL)
#define dap_config_get_item_path_default(a_conf, a_path, a_item, a_default) dap_config_get_item_str_path_default(a_conf, a_path, a_item, a_default)

#define dap_config_get_item_double(a_conf, a_path, a_item) dap_config_get_item_double_default(a_conf, a_path, a_item, 0)

#ifdef __cplusplus
}
#endif
