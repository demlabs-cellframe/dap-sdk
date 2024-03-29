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

typedef struct dap_config_item dap_config_item_t;

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
    if(dap_dir_test(a_configs_path) || !dap_mkdir_with_parents(a_configs_path)) {
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
{                                           \
    DAP_DELETE(a_item->name);               \
    switch (a_item->type) {                 \
    case 's':                               \
        DAP_DELETE(a_item->val.val_str);    \
        break;                              \
    case 'a':                               \
        dap_strfreev(a_item->val.val_arr);  \
        break;                              \
    default:                                \
        break;                              \
    }                                       \
    DAP_DELETE(a_item);                     \
}

void dap_config_dump(dap_config_t *a_conf) {
    dap_config_item_t *l_item = NULL, *l_tmp = NULL;
    log_it(L_DEBUG, " Config %s", a_conf->path);
    HASH_ITER(hh, a_conf->items, l_item, l_tmp) {
        switch (l_item->type) {
        case 's':
            log_it(L_DEBUG, " String param: %s = %s", l_item->name, l_item->val.val_str);
            break;
        case 'd':
            log_it(L_DEBUG, " Int param: %s = %ld", l_item->name, l_item->val.val_int);
            break;
        case 'b':
            log_it(L_DEBUG, " Bool param: %s = %d", l_item->name, l_item->val.val_bool);
            break;
        case 'a': {
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
            char *l_tmp = l_line, *l_tmp1 = l_line;
            int l_shift = 0;
            do {
                while(isspace(*l_tmp1)) {
                    ++l_tmp1;
                    ++l_shift;
                }
            } while((*l_tmp++ = *l_tmp1++));
        }
        unsigned l_stripped_len = strlen(l_line);
        if (!l_stripped_len) {
            // No useful data remained
            continue;
        }
        char *l_key = NULL, *l_val = NULL;
        if (!l_values_arr) {
            --l_stripped_len;
            if (l_line[0] == '[' && l_line[l_stripped_len] == ']') {
                // A section start
                l_line[l_stripped_len] = '\0';
                DAP_DEL_Z(l_section);
                l_section = dap_strdup(l_line + 1);
                continue;
            } else if (!l_section) {
                log_it(L_WARNING, "Config \"%s\": line %d belongs to unknown section. Dump it",
                       a_abs_path, l_line_counter);
                continue;
            }
            if (!strchr(l_line, '=')) {
                log_it(L_WARNING, "Config \"%s\": unknown pattern on line %d, dump it",
                       a_abs_path, l_line_counter);
                continue;
            }
            l_key = strtok_r(l_line, "=", &l_val);
        } else {
            l_val = l_line;
        }
        union dap_config_val l_item_val = { };
        if (*l_val != '[' && !l_values_arr) {
            // Single val
            l_type = 's';
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
                l_type = 'b';
                l_item_val.val_bool = true;
            } else if (
         #ifdef DAP_OS_WINDOWS
                     !stricmp(l_val, "false")
         #else
                     !strcasecmp(l_val, "false")
         #endif
                     )
            {
                l_type = 'b';
                l_item_val.val_bool = false;
            } else {
                errno = 0;
                char *tmp;
                long long val = strtoll(l_val, &tmp, 10);
                bool fail = ( tmp == l_val || *tmp != '\0' || ((val == LLONG_MIN || val == LLONG_MAX) && errno == ERANGE) );
                if ( !fail ) {
                    l_item_val.val_int = val;
                    l_type = 'd';
                }
            }
            if (l_type == 's')
                l_item_val.val_str = dap_strdup(l_val);
        } else {
            // Array of strings
            if (!l_values_arr)
                ++l_val;
            if (l_type != 'r') {
                l_type = 'a';
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
        case 'r':
            DAP_DELETE(l_name);
            if (l_item) {
                HASH_DEL((*a_conf)->items, l_item);
                dap_config_item_del(l_item);
            }
            dap_strfreev(l_values_arr);
            l_values_arr = NULL;
            break;
        case 'a':
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
    char l_path[MAX_PATH] = { '\0' };
    int l_pos = dap_strncmp(a_file_path, s_configs_path, strlen(s_configs_path) - 4)
            ? dap_snprintf(l_path, MAX_PATH, "%s/%s.cfg", s_configs_path, a_file_path)
            : dap_snprintf(l_path, MAX_PATH, "%s.cfg", a_file_path);

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

struct dap_config_item *dap_config_get_item(dap_config_t *a_config, const char *a_section, const char *a_item_name) {
    char *l_key = dap_strdup_printf("%s:%s", a_section, a_item_name);
    for (char *c = l_key; *c; ++c) {
        if (*c == '-')
            *c = '_';
    }
    struct dap_config_item *l_item = NULL;
    HASH_FIND_STR(a_config->items, l_key, l_item);
    if (!l_item) {
        debug_if(debug_config, L_DEBUG, "Not found param \"%s\"", l_key);
    }
    DAP_DELETE(l_key);
    return l_item;
}

bool dap_config_get_item_bool_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, bool a_default) {
    dap_config_item_t *l_item = dap_config_get_item(a_config, a_section, a_item_name);
    if (!l_item)
        return a_default;
    if (l_item->type != 'b') {
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
    case 'd':
        return l_item->val.val_int;
    default:
        return log_it(L_ERROR, "Parameter \"%s\" '%c' is not signed integer", l_item->name, l_item->type), a_default;
    }
}

uint64_t _dap_config_get_item_uint(dap_config_t *a_config, const char *a_section, const char *a_item_name, uint64_t a_default) {
    dap_config_item_t *l_item = dap_config_get_item(a_config, a_section, a_item_name);
    if (!l_item)
        return a_default;
    switch (l_item->type) {
    case 'd':
        return l_item->val.val_int < 0
                ? log_it(L_WARNING, "Unsigned parameter \"%s\" requested, but the value is negative: %ld",
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
    case 's':
        return l_item->val.val_str;
    case 'a':
        return l_item->val.val_arr[0];
    case 'd':
        return dap_itoa(l_item->val.val_int);
    case 'b':
        return dap_itoa(l_item->val.val_bool);
    default:
        log_it(L_ERROR, "Parameter \"%s\" '%c' is not string", l_item->name, l_item->type);
        return a_default;
    }
}

const char *dap_config_get_item_str_path_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, const char *a_default) {
    dap_config_item_t *l_item = dap_config_get_item(a_config, a_section, a_item_name);
    if (!l_item)
        return a_default;
    if (l_item->type != 's') {
        log_it(L_ERROR, "Parameter \"%s\" '%c' is not string", l_item->name, l_item->type);
        return a_default;
    }
    char l_abs_path[strlen(a_config->path) + 5];
    dap_stpcpy(l_abs_path, a_config->path);
    char *l_dir = dap_path_get_dirname(l_abs_path);
    char *l_ret = dap_canonicalize_filename(l_item->val.val_str, l_dir);
    log_it(L_DEBUG, "Config-path item: %s: composed from %s and %s",
           l_ret, l_item->val.val_str, l_dir);
    DAP_DELETE(l_dir);
    return l_ret;
}

char **dap_config_get_array_str(dap_config_t *a_config, const char *a_section, const char *a_item_name, uint16_t *array_length) {
    dap_config_item_t *l_item = dap_config_get_item(a_config, a_section, a_item_name);
    if (array_length)
        *array_length = 0;
    if (!l_item)
        return NULL;
    if (l_item->type != 'a') {
        log_it(L_ERROR, "Parameter \"%s\" '%c' is not array", l_item->name, l_item->type);
        return NULL;
    }
    if (array_length)
        *array_length = dap_str_countv(l_item->val.val_arr);
    return l_item->val.val_arr;
}

double dap_config_get_item_double_default(dap_config_t *a_config, const char *a_section, const char *a_item_name, double a_default) {
    dap_config_item_t *l_item = dap_config_get_item(a_config, a_section, a_item_name);
    if (!l_item)
        return a_default;
    switch (l_item->type) {
    case 's':
        return strtod(l_item->val.val_str, NULL);
    case 'd':
        return (double)l_item->val.val_int;
    default:
        log_it(L_ERROR, "Parameter \"%s\" '%c' can't be represented as double", l_item->name, l_item->type);
        return a_default;
    }
}


void dap_config_close(dap_config_t *a_conf) {
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
