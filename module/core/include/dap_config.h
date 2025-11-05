#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "uthash.h"
#include "dap_common.h"

// Forward declarations
typedef struct dap_config_item dap_config_item_t;
typedef struct dap_conf dap_config_t;

// ============================================================================
// Custom Parser System - Dependency Inversion Layer
// ============================================================================

/**
 * @brief Custom parser callback type
 * @param a_cfg Config object
 * @param a_config Config name
 * @param a_section Section name
 * @param a_out_data Output pointer (caller allocates based on parser needs)
 * @param a_out_count Output count of parsed items
 * @return 0 on success, negative on error
 */
typedef int (*dap_config_custom_parser_t)(struct dap_conf *a_cfg, const char *a_config, 
                                           const char *a_section, void **a_out_data, 
                                           uint16_t *a_out_count);

/**
 * @brief Register custom parser for specific data type
 * @param a_parser_name Unique parser name (e.g., "stream_addrs")
 * @param a_parser Parser callback function
 * @return 0 on success, negative on error
 */
int dap_config_register_parser(const char *a_parser_name, dap_config_custom_parser_t a_parser);

/**
 * @brief Call registered custom parser
 * @param a_parser_name Parser name
 * @param a_cfg Config object
 * @param a_config Config name
 * @param a_section Section name
 * @param a_out_data Output pointer
 * @param a_out_count Output count
 * @return 0 on success, negative on error
 */
int dap_config_call_parser(const char *a_parser_name, struct dap_conf *a_cfg, const char *a_config,
                           const char *a_section, void **a_out_data, uint16_t *a_out_count);

typedef enum {
    DAP_CONFIG_ITEM_UNKNOWN = '\0',
    DAP_CONFIG_ITEM_ARRAY   = 'a',
    DAP_CONFIG_ITEM_BOOL    = 'b',
    DAP_CONFIG_ITEM_DECIMAL = 'd',
    DAP_CONFIG_ITEM_STRING  = 's'
} dap_config_item_type_t;

typedef struct dap_conf {
    char *path;
    dap_config_item_t *items;
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
dap_config_t *dap_config_create_empty(void);
void dap_config_set_item_str(dap_config_t *a_config, const char *a_section, const char *a_item_name, const char *a_value);

void dap_config_close(dap_config_t*);
void dap_config_deinit();

const char *dap_config_path();

dap_config_item_type_t dap_config_get_item_type(dap_config_t *a_config, const char *a_section, const char *a_item_name);
bool dap_config_get_item_bool_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, bool a_default);
int64_t _dap_config_get_item_int(dap_config_t *a_config, const char *a_section, const char *a_item_name, int64_t a_default);
uint64_t _dap_config_get_item_uint(dap_config_t *a_config, const char *a_section, const char *a_item_name, uint64_t a_default);
const char *dap_config_get_item_str_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, const char *a_default);
char *dap_config_get_item_str_path_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, const char *a_default);
const char** dap_config_get_array_str(dap_config_t *a_config, const char *a_section, const char *a_item_name, uint16_t *array_length);
char **dap_config_get_item_str_path_array(dap_config_t *a_config, const char *a_section, const char *a_item_name, uint16_t *array_length);
void dap_config_get_item_str_path_array_free(char **paths_array, uint16_t array_length);
double dap_config_get_item_double_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, double a_default);

// dap_config_stream_addrs_parse moved to dap_net_common.h (network-specific function)

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
