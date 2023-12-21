#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "dap_binary_tree.h"
#include "uthash.h"

typedef struct dap_conf {
    char *path;
    struct dap_conf_item {
        char type, *name;
        union dap_conf_val {
            bool        val_bool;
            char        *val_str;
            dap_list_t  *val_list;
            int64_t     val_int;
            uint64_t    val_uint;
        } val;
        UT_hash_handle hh;
    } *items;
    UT_hash_handle hh;
} dap_conf_t;

extern dap_conf_t *g_configs_table;

#ifdef __cplusplus
extern "C" {
#endif

int dap_conf_init(const char*);
dap_conf_t *dap_conf_load(const char*);

void dap_conf_close(dap_conf_t*);

const char * dap_conf_path();

uint16_t dap_config_get_item_uint16(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name);
uint16_t dap_config_get_item_uint16_default(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name, uint16_t a_default);

int16_t dap_config_get_item_int16(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name);
int16_t dap_config_get_item_int16_default(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name, int16_t a_default);

uint32_t dap_config_get_item_uint32(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name);
uint32_t dap_config_get_item_uint32_default(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name, uint32_t a_default);

int32_t dap_config_get_item_int32(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name);
int32_t dap_config_get_item_int32_default(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name, int32_t a_default);

int64_t dap_config_get_item_int64(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name);
int64_t dap_config_get_item_int64_default(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name, int64_t a_default);

uint64_t dap_config_get_item_uint64(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name);
uint64_t dap_config_get_item_uint64_default(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name, uint64_t a_default);

const char * dap_config_get_item_str(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name);
const char * dap_config_get_item_str_default(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name, const char * a_value_default);

const char * dap_config_get_item_path(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name);
const char * dap_config_get_item_path_default(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name, const char * a_value_default);

char** dap_config_get_array_str(dap_conf_t * a_config, const char * a_section_path,
                                      const char * a_item_name, uint16_t * array_length);

bool dap_config_get_item_bool(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name);
bool dap_config_get_item_bool_default(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name, bool a_default);

double dap_config_get_item_double(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name);
double dap_config_get_item_double_default(dap_conf_t * a_config, const char * a_section_path, const char * a_item_name, double a_default);

#ifdef __cplusplus
}
#endif
