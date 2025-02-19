#include <string.h>
#include <errno.h>
#include <stdint.h>
#include "dap_config.h"
#include "uthash.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#ifdef DAP_OS_WINDOWS
#include "dap_list.h"
#endif

#define LOG_TAG "dap_config"

//dap_config_t *g_configs_table = NULL;

typedef struct dap_config_item {
    char type, *name;
    union dap_config_val {
        bool        val_bool;
        char        *val_str;
        char        **val_arr;
        int64_t     val_int;
    } val;
    UT_hash_handle hh;
} dap_config_item_t;

static char *s_configs_path = NULL;
dap_config_t *g_config = NULL;

static bool debug_config = false;

int dap_config_init(const char *a_configs_path)
{
    if (!a_configs_path || !a_configs_path[0]) {
        log_it(L_ERROR, "Empty path!");
        return -1;
    }
#ifdef DAP_OS_WINDOWS
    // Check up under Windows, in Linux is not required
    if(!dap_valid_ascii_symbols(a_configs_path)) {
        log_it(L_ERROR, "Supported only ASCII symbols for directory path");
        return -1;
    }
#endif
    if(dap_dir_test(a_configs_path)) {
        DAP_DEL_Z(s_configs_path);
        s_configs_path = dap_strdup(a_configs_path);
        return 0;
    } else {
        log_it(L_ERROR, "Invalid path %s!", a_configs_path);
        return -2;
    }
}

const char *dap_config_path()
{
    return s_configs_path;
}

#define dap_config_item_del(a_item)         \
do {                                        \
    DAP_DELETE(a_item->name);               \
    switch (a_item->type) {                 \
    case DAP_CONFIG_ITEM_STRING:            \
        DAP_DELETE(a_item->val.val_str);    \
        break;                              \
    case DAP_CONFIG_ITEM_ARRAY:             \
        dap_strfreev(a_item->val.val_arr);  \
        break;                              \
    default:                                \
        break;                              \
    }                                       \
    DAP_DELETE(a_item);                     \
} while (0)

void dap_config_dump(dap_config_t *a_conf) {
    dap_config_item_t *l_item = NULL, *l_tmp = NULL;
    log_it(L_DEBUG, " Config %s", a_conf->path);
    HASH_ITER(hh, a_conf->items, l_item, l_tmp) {
        switch (l_item->type) {
        case DAP_CONFIG_ITEM_STRING:
            log_it(L_DEBUG, " String param: %s = %s", l_item->name, l_item->val.val_str);
            break;
        case DAP_CONFIG_ITEM_DECIMAL:
            log_it(L_DEBUG, " Int param: %s = %"DAP_UINT64_FORMAT_U, l_item->name, l_item->val.val_int);
            break;
        case DAP_CONFIG_ITEM_BOOL:
            log_it(L_DEBUG, " Bool param: %s = %d", l_item->name, l_item->val.val_bool);
            break;
        case DAP_CONFIG_ITEM_ARRAY: {
            log_it(L_DEBUG, " Array param: %s = ", l_item->name);
            for (char **l_str = l_item->val.val_arr; *l_str; ++l_str) {
                log_it(L_DEBUG, " %s", *l_str);
            }
            break;
        }
        }
    }
}

#ifdef DAP_OS_WINDOWS
static int s_name_sort_cb(dap_list_t *a_str1, dap_list_t *a_str2) {
    char    *l_str1 = a_str1->data,
            *l_str2 = a_str2->data;
    return dap_strcmp(l_str1, l_str2);
}
#endif

static int _dap_config_load(const char* a_abs_path, dap_config_t **a_conf) {
    if (!a_conf || !*a_conf) {
        log_it(L_ERROR, "Config is not initialized");
        return 1;
    }
    FILE *f = fopen(a_abs_path, "r");
    if (!f) {
        log_it(L_ERROR, "Can't open config file \"%s\", error %d", a_abs_path, errno);
        return 2;
    }
#define MAX_CONFIG_LINE_LEN 1024
    unsigned l_len = MAX_CONFIG_LINE_LEN, l_shift = 0;
    log_it(L_DEBUG, "Opened config %s", a_abs_path);

    char    *l_line = DAP_NEW_Z_SIZE(char, l_len),
            *l_section = NULL;
    dap_config_item_t *l_item = NULL;
    char l_type = '\0', *l_key_for_arr = NULL, **l_values_arr = NULL;
    for (uint16_t l_line_counter = 0, l_line_counter2 = 0; fgets(l_line + l_shift, MAX_CONFIG_LINE_LEN, f); ++l_line_counter) {
        if (!l_shift)
            l_line = DAP_REALLOC(l_line, MAX_CONFIG_LINE_LEN);
        unsigned l_eol = strcspn(l_line + l_shift, "\r\n") + l_shift;
        if (l_eol == l_len - 1) {
            if (l_line_counter != l_line_counter2) {
                log_it(L_WARNING, "Config \"%s\": line %d is too long, preserving the tail ...", a_abs_path, l_line_counter);
            }
            l_line_counter2 = l_line_counter--;
            l_shift = l_eol;
            l_len += (MAX_CONFIG_LINE_LEN - 1);
            l_line = DAP_REALLOC(l_line, l_len);
            continue;
        }
        if (l_shift) {
            l_len = MAX_CONFIG_LINE_LEN;
            l_shift = 0;
        }
        l_eol = strcspn(l_line, "#\r\n");
        l_line[l_eol] = '\0';
        {
            char *l_read = l_line, *l_write = l_line;
            do {
                if ( !isspace(*l_read) )
                    *l_write++ = *l_read;
            } while (*l_read++);
            l_eol = (unsigned)(l_write - l_line - 1);
        }
        if (!l_eol) {
            // No useful data remained
            continue;
        }
        char *l_key = NULL, *l_val = NULL;
        if (!l_values_arr) {
            --l_eol;
            if (l_line[0] == '[' && l_line[l_eol] == ']') {
                // A section start
                l_line[l_eol] = '\0';
                DAP_DEL_Z(l_section);
                l_section = dap_strdup(l_line + 1);
                continue;
            } else if (!l_section) {
                log_it(L_WARNING, "Config \"%s\": line %d belongs to unknown section. Dump it",
                       a_abs_path, l_line_counter);
                continue;
            }
            if (! (l_key = strtok_r(l_line, "=", &l_val)) || !l_val ) {
                log_it(L_WARNING, "Config \"%s\": unknown pattern on line %d, dump it",
                                  a_abs_path, l_line_counter);
                continue;
            }
        } else {
            l_val = l_line;
        }
        union dap_config_val l_item_val = { };
        if (*l_val != '[' && !l_values_arr) {
            // Single val
            l_type = DAP_CONFIG_ITEM_STRING;
            if (!*l_val)
                l_type = 'r';
            else if (
         #ifdef DAP_OS_WINDOWS
                     !stricmp(l_val, "true")
         #else
                     !strcasecmp(l_val, "true")
         #endif
                     )
            {
                l_type = DAP_CONFIG_ITEM_BOOL;
                l_item_val.val_bool = true;
            } else if (
         #ifdef DAP_OS_WINDOWS
                     !stricmp(l_val, "false")
         #else
                     !strcasecmp(l_val, "false")
         #endif
                     )
            {
                l_type = DAP_CONFIG_ITEM_BOOL;
                l_item_val.val_bool = false;
            } else {
                errno = 0;
                char *tmp;
                long long val = strtoll(l_val, &tmp, 10);
                bool fail = ( tmp == l_val || *tmp != '\0' || ((val == LLONG_MIN || val == LLONG_MAX) && errno == ERANGE) );
                if ( !fail ) {
                    l_item_val.val_int = val;
                    l_type = DAP_CONFIG_ITEM_DECIMAL;
                }
            }
            if (l_type == DAP_CONFIG_ITEM_STRING)
                l_item_val.val_str = dap_strdup(l_val);
        } else {
            // Array of strings
            if (!l_values_arr)
                ++l_val;
            if (l_type != 'r') {
                l_type = DAP_CONFIG_ITEM_ARRAY;
                int l_pos = dap_strlen(l_val) - 1;
                char l_term = l_val[l_pos];
                if (l_term == ']') {
                    l_val[l_pos] = '\0';
                }
                char **l_vals = dap_strsplit(l_val, ",", -1);
                if (!*l_vals) {
                    l_type = 'r';
                } else {
                    l_values_arr = dap_str_appv(l_values_arr, l_vals, NULL);
                }

                DAP_DELETE(l_vals);
                if (l_term != ']' || !l_values_arr) {
                    if (!l_key_for_arr && l_key)
                        l_key_for_arr = strdup(l_key);
                    continue;
                }
                if (!dap_str_countv(l_values_arr))
                    l_type = 'r';
            }
        }

        char *l_name = dap_strdup_printf("%s:%s", l_section, l_key_for_arr ? l_key_for_arr : l_key);
        DAP_DEL_Z(l_key_for_arr);
        for (char *c = l_name; *c; ++c) {
            if (*c == '-')
                *c = '_';
        }
        HASH_FIND_STR((*a_conf)->items, l_name, l_item);

        switch (l_type) {
        // 'r' is for an item being removed
        case 'r':
            DAP_DELETE(l_name);
            if (l_item) {
                HASH_DEL((*a_conf)->items, l_item);
                dap_config_item_del(l_item);
            }
            dap_strfreev(l_values_arr);
            l_values_arr = NULL;
            break;
        case DAP_CONFIG_ITEM_ARRAY:
            l_item_val.val_arr = dap_str_appv(l_item_val.val_arr, l_values_arr, NULL);
            DAP_DEL_Z(l_values_arr);
            if (l_item)
                dap_strfreev(l_item->val.val_arr);
        default:
            if (!l_item) {
                l_item = DAP_NEW_Z(dap_config_item_t);
                l_item->name = l_name;
                HASH_ADD_KEYPTR(hh, (*a_conf)->items, l_item->name, strlen(l_item->name), l_item);
            } else {
                DAP_DELETE(l_name);
                if ( l_type == DAP_CONFIG_ITEM_STRING )
                    DAP_DELETE(l_item->val.val_str);
            }
            l_item->type = l_type;
            l_item->val = l_item_val;
            break;
        }
    }
    DAP_DELETE(l_line);
    DAP_DEL_Z(l_key_for_arr);
    DAP_DEL_Z(l_section);
    fclose(f);
    return 0;
}

dap_config_t *dap_config_open(const char* a_file_path) {
    if (!a_file_path || !a_file_path[0]) {
        log_it(L_ERROR, "Empty config name!");
        return NULL;
    }
    log_it(L_DEBUG, "Looking for config name %s...", a_file_path);
    char l_path[MAX_PATH + 1] = "";
    int l_pos = dap_strncmp(a_file_path, s_configs_path, strlen(s_configs_path) - 4)
            ? snprintf(l_path, MAX_PATH, "%s/%s.cfg", s_configs_path, a_file_path)
            : snprintf(l_path, MAX_PATH, "%s.cfg", a_file_path);

    if (l_pos >= MAX_PATH) {
        log_it(L_ERROR, "Too long config name!");
        return NULL;
    }

    int l_basic_len = strlen(l_path) - 4;
    char *l_basic_name = dap_strdup_printf("%.*s", l_basic_len, l_path);
#if 0
    dap_config_t *l_conf = NULL;
    HASH_FIND_STR(g_configs_table, l_basic_name, l_conf);
    if (!l_conf) {
        l_conf = DAP_NEW_Z(dap_config_t);
        l_conf->path = l_basic_name;
        HASH_ADD_KEYPTR(hh, g_configs_table, l_conf->path, l_basic_len, l_conf);
    } else {
        DAP_DELETE(l_basic_name);
    }
#endif
    dap_config_t *l_conf = DAP_NEW_Z(dap_config_t);
    l_conf->path = l_basic_name;
    if (_dap_config_load(l_path, &l_conf))
       return NULL;
    debug_config = g_config ? dap_config_get_item_bool(g_config, "general", "debug-config") : false;

    if (l_pos >= MAX_PATH - 3)
        return l_conf;

    memcpy(l_path + l_pos, ".d", 2);
#ifdef DAP_OS_WINDOWS
    DIR *l_dir = opendir(l_path);
    if (!l_dir) {
        log_it(L_DEBUG, "Cannot open directory %s", l_path);
        if (debug_config)
            dap_config_dump(l_conf);
        return l_conf;
    }
    struct dirent *l_entry;
    dap_list_t *l_filenames = NULL;
    while ((l_entry = readdir(l_dir))) {
        const char *l_filename = l_entry->d_name;
        if (!strncmp(l_filename + strlen(l_filename) - 4, ".cfg", 4))
            l_filenames = dap_list_append(l_filenames, dap_strdup(l_filename));
    }
    closedir(l_dir);
    l_filenames = dap_list_sort(l_filenames, s_name_sort_cb);
    for (dap_list_t *l_filename = l_filenames; l_filename; l_filename = l_filename->next) {
        char *l_entry_file = dap_strdup_printf("%s/%s", l_path, (char*)l_filename->data);
        _dap_config_load(l_entry_file, &l_conf);
        DAP_DELETE(l_entry_file);
    }
    dap_list_free_full(l_filenames, NULL);
#else
    struct dirent **l_entries;
    int l_err = scandir(l_path, &l_entries, 0, alphasort);
    if (l_err < 0) {
        log_it(L_DEBUG, "Cannot open directory %s", l_path);
        if (debug_config)
            dap_config_dump(l_conf);
        return l_conf;
    }
    for (int i = 0; i < l_err; ++i) {
        if (!strncmp(l_entries[i]->d_name + strlen(l_entries[i]->d_name) - 4, ".cfg", 4)) {
            char *l_entry_file = dap_strdup_printf("%s/%s", l_path, l_entries[i]->d_name);
            _dap_config_load(l_entry_file, &l_conf);
            DAP_DELETE(l_entry_file);
        }
        DAP_DELETE(l_entries[i]);
    }
    DAP_DELETE(l_entries);
#endif
    if (debug_config)
        dap_config_dump(l_conf);
    return l_conf;
}

dap_config_item_t *dap_config_get_item(dap_config_t *a_config, const char *a_section, const char *a_item_name) {
    if (!a_config || !a_section || !a_item_name)
        return NULL;
    char *l_key = dap_strdup_printf("%s:%s", a_section, a_item_name);
    for (char *c = l_key; *c; ++c) {
        if (*c == '-')
            *c = '_';
    }
    dap_config_item_t *l_item = NULL;
    HASH_FIND_STR(a_config->items, l_key, l_item);
    if (!l_item) {
        debug_if(debug_config, L_DEBUG, "Not found param \"%s\"", l_key);
    }
    DAP_DELETE(l_key);
    return l_item;
}

dap_config_item_type_t dap_config_get_item_type(dap_config_t *a_config, const char *a_section, const char *a_item_name) {
    dap_config_item_t *l_item = dap_config_get_item(a_config, a_section, a_item_name);
    return l_item ? l_item->type : '\0';
}

bool dap_config_get_item_bool_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, bool a_default) {
    dap_config_item_t *l_item = dap_config_get_item(a_config, a_section, a_item_name);
    if (!l_item)
        return a_default;
    if (l_item->type != DAP_CONFIG_ITEM_BOOL) {
        log_it(L_ERROR, "Parameter \"%s\" '%c' is not bool", l_item->name, l_item->type);
        return a_default;
    }
    return l_item->val.val_bool;
}

int64_t _dap_config_get_item_int(dap_config_t *a_config, const char *a_section, const char *a_item_name, int64_t a_default) {
    dap_config_item_t *l_item = dap_config_get_item(a_config, a_section, a_item_name);
    if (!l_item)
        return a_default;
    switch (l_item->type) {
    case DAP_CONFIG_ITEM_DECIMAL:
        return l_item->val.val_int;
    default:
        return log_it(L_ERROR, "Parameter \"%s\" '%c' is not signed integer", l_item->name, l_item->type), a_default;
    }
}

uint64_t _dap_config_get_item_uint(dap_config_t *a_config, const char *a_section, const char *a_item_name, uint64_t a_default) {
    dap_return_val_if_pass(!a_config, a_default);
    dap_config_item_t *l_item = dap_config_get_item(a_config, a_section, a_item_name);
    if (!l_item)
        return a_default;
    switch (l_item->type) {
    case DAP_CONFIG_ITEM_DECIMAL:
        return l_item->val.val_int < 0
                ? log_it(L_WARNING, "Unsigned parameter \"%s\" requested, but the value is negative: %"DAP_UINT64_FORMAT_U,
                         l_item->name, l_item->val.val_int), a_default
                : (uint64_t)l_item->val.val_int;
    default:
        return log_it(L_ERROR, "Parameter \"%s\" '%c' is not unsigned integer", l_item->name, l_item->type), a_default;
    }
}

const char *dap_config_get_item_str_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, const char *a_default) {
    dap_config_item_t *l_item = dap_config_get_item(a_config, a_section, a_item_name);
    if (!l_item)
        return a_default;
    switch (l_item->type) {
    case DAP_CONFIG_ITEM_STRING:
        return l_item->val.val_str;
    case DAP_CONFIG_ITEM_ARRAY:
        return *l_item->val.val_arr;
    case DAP_CONFIG_ITEM_BOOL:
        return l_item->val.val_bool ? "true" : "false";
    case DAP_CONFIG_ITEM_DECIMAL: {
        static _Thread_local char s_ret[sizeof(dap_maxint_str_t)];
        return (const char*)memcpy(s_ret, dap_itoa(l_item->val.val_int), sizeof(dap_maxint_str_t));
    }
    default:
        log_it(L_ERROR, "Parameter \"%s\" '%c' is not string", l_item->name, l_item->type);
        return a_default;
    }
}

char *dap_config_get_item_str_path_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, const char *a_default) {
    const char *l_val = dap_config_get_item_str(a_config, a_section, a_item_name);
    if (!l_val)
        return dap_strdup(a_default);
    if ( dap_path_is_absolute(l_val) )
        return dap_strdup(l_val);
    char *l_dir = dap_path_get_dirname(a_config->path), *l_ret = dap_canonicalize_path(l_val, l_dir);
    //log_it(L_DEBUG, "Config-path item: %s: composed from %s and %s", l_ret, l_item->val.val_str, l_dir);
    return DAP_DELETE(l_dir), l_ret;
}

const char **dap_config_get_array_str(dap_config_t *a_config, const char *a_section, const char *a_item_name, uint16_t *array_length) {
    dap_config_item_t *l_item = dap_config_get_item(a_config, a_section, a_item_name);
    if (array_length)
        *array_length = 0;
    if (!l_item)
        return NULL;
    if (l_item->type != DAP_CONFIG_ITEM_ARRAY) {
        log_it(L_WARNING, "Parameter \"%s\" '%c' is not array", l_item->name, l_item->type);
        if (array_length)
            *array_length = 1;
        static _Thread_local const char* s_ret;
        return s_ret = dap_config_get_item_str(a_config, a_section, a_item_name), &s_ret;
    }
    if (array_length)
        *array_length = dap_str_countv(l_item->val.val_arr);
    return (const char**)l_item->val.val_arr;
}

char **dap_config_get_item_str_path_array(dap_config_t *a_config, const char *a_section, const char *a_item_name, uint16_t *array_length) { 
    if ( !array_length )
        return NULL;
    const char **l_paths_cfg = dap_config_get_array_str(a_config, a_section, a_item_name, array_length);
    if ( !l_paths_cfg || !*array_length )
        return NULL;
    char *l_cfg_path = dap_path_get_dirname(a_config->path), **l_paths = DAP_NEW_Z_COUNT(char*, *array_length);
    for (int i = 0; i < *array_length; ++i) {
        l_paths[i] = dap_path_is_absolute(l_paths_cfg[i]) ? dap_strdup(l_paths_cfg[i]) : dap_canonicalize_path(l_paths_cfg[i], l_cfg_path);
    }
    return DAP_DELETE(l_cfg_path), l_paths;
}

void dap_config_get_item_str_path_array_free(char **paths_array, uint16_t array_length) {
    DAP_DEL_ARRAY(paths_array, array_length);
    DAP_DELETE(paths_array);
}

double dap_config_get_item_double_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, double a_default) {
    dap_config_item_t *l_item = dap_config_get_item(a_config, a_section, a_item_name);
    if (!l_item)
        return a_default;
    switch (l_item->type) {
    case DAP_CONFIG_ITEM_STRING:
        return strtod(l_item->val.val_str, NULL);
    case DAP_CONFIG_ITEM_DECIMAL:
        return (double)l_item->val.val_int;
    default:
        return log_it(L_ERROR, "Parameter \"%s\" '%c' can't be represented as double", l_item->name, l_item->type), a_default;
    }
}

void dap_config_close(dap_config_t *a_conf) {
    if (!a_conf)
        return;
    DAP_DELETE(a_conf->path);
    dap_config_item_t *l_item = NULL, *l_tmp = NULL;
    HASH_ITER(hh, a_conf->items, l_item, l_tmp) {
        HASH_DEL(a_conf->items, l_item);
        dap_config_item_del(l_item);
    }
#if 0
    HASH_DEL(g_configs_table, a_conf);
#endif
    DAP_DELETE(a_conf);
}

void dap_config_deinit() {

}

int dap_config_stream_addrs_parse(dap_config_t *a_cfg, const char *a_config, const char *a_section, dap_stream_node_addr_t **a_addrs, uint16_t *a_addrs_count)
{
    dap_return_val_if_pass(!a_cfg || !a_config || !a_config || !a_section || !a_addrs_count, -1);
    const char **l_nodes_addrs = dap_config_get_array_str(a_cfg, a_config, a_section, a_addrs_count);
    if (*a_addrs_count) {
        log_it(L_DEBUG, "Start parse stream addrs in cofnig %s section %s", a_config, a_section);
        *a_addrs = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(dap_stream_node_addr_t, *a_addrs_count, -2);
        for (uint16_t i = 0; i < *a_addrs_count; ++i) {
            if (dap_stream_node_addr_from_str(*a_addrs + i, l_nodes_addrs[i])) {
                log_it(L_ERROR, "Incorrect format of %s address \"%s\", fix net config and restart node", a_section, l_nodes_addrs[i]);
                return -3;
            }
            log_it(L_DEBUG, "Stream addr " NODE_ADDR_FP_STR " parsed successfully", NODE_ADDR_FP_ARGS_S((*a_addrs)[i]));
        }
    }
    return 0;
}
