#include <string.h>
#include "dap_config2.h"
#include "uthash.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"

#define LOG_TAG "dap_config"

dap_conf_t *g_configs_table = NULL;

typedef struct dap_conf_item dap_conf_item_t;

static char *s_configs_path = NULL;

int dap_conf_init(const char *a_configs_path)
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

const char *dap_conf_path()
{
    return s_configs_path;
}

void dap_conf_item_del(struct dap_conf_item *a_item);

static int _dap_conf_load(const char* a_abs_path, dap_conf_t **a_conf) {
    if (!a_conf || !*a_conf) {
        log_it(L_ERROR, "Config is not initialized");
        return 1;
    }
    FILE *f = fopen(a_abs_path, "r");
    if (!f) {
        log_it(L_ERROR, "Can't open config file \"%s\", error %d", a_abs_path, errno);
        return 2;
    }
    unsigned l_max_len = 1024;
    log_it(L_DEBUG,"Opened config %s", a_abs_path);


    char *l_line = DAP_NEW_STACK_SIZE(char, l_max_len), *l_section = NULL;
    dap_conf_item_t     *l_item         = NULL;
    dap_list_t          *l_values_list  = NULL;
    for (uint16_t l_line_counter = 0; fgets(l_line, l_max_len, f); ++l_line_counter) {
        unsigned l_eol = strcspn(l_line, "#\r\n");
        if (l_eol == l_max_len - 1) {
            log_it(L_WARNING, "Config \"%s\": line %d is too long. Dump it",
                   a_abs_path, l_line_counter);
            continue;
        }
        l_line[l_eol] = '\0';
        l_line = dap_strstrip(l_line);
        unsigned l_stripped_len = strlen(l_line);
        if (!l_stripped_len)
            // No useful data remained
            continue;
        char *l_key = NULL, *l_val = NULL;
        if (!l_values_list) {
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
        union dap_conf_val l_item_val = { };
        char l_type = '\0';
        if (*l_val != '[' && !l_values_list) {
            // Single val
            l_type = 'd';
            if (!*l_val)
                l_type = 'r';
            else if (
         #ifdef DAP_OS_WINDOWS
                     !stricmp(l_val, "true")
         #else
                     !strcasecmp(l_val, "true")
         #endif
                     )
                l_type = 0;
            else if (
         #ifdef DAP_OS_WINDOWS
                     !stricmp(l_val, "false")
         #else
                     !strcasecmp(l_val, "false")
         #endif
                     )
                l_type = 1;
            else {
                char *c;
                if (*l_val == '-') {
                    c = l_val + 1;
                } else {
                    c = l_val;
                    l_type = 'u';
                }
                while (*c++) {
                    if (!isdigit(*c)) {
                        l_type = 's';
                        break;
                    }
                }
            }
            switch (l_type) {
            case '0':
            case '1':
                l_item_val.val_bool = l_type;
                l_type = 'b';
                break;
            case 'd': {
                l_item_val.val_int = strtoll(l_val, NULL, 10);
                break;
            }
            case 'u':{
                l_item_val.val_uint = strtoull(l_val, NULL, 10);
                break;
            }
            case 's':
                l_item_val.val_str = dap_strdup(l_val);
                break;
            default:
                break;
            }
        } else {
            // Array of strings
            if (!l_values_list)
                ++l_val;
            l_type = 'a';
            char l_term = '\0';
            {
                char *l_tmp = l_val, *l_tmp1 = l_val;
                int l_shift = 0;
                do {
                    while(isspace(*l_tmp1)) {
                        ++l_tmp1;
                        ++l_shift;
                    }
                } while((*l_tmp++ = *l_tmp1++));
                l_tmp1 -= (l_shift + 2);
                if ((l_term = *l_tmp1) == ']') {
                    *l_tmp1 = '\0';
                }
            }
            char **l_vals = dap_strsplit(l_val, ",", -1);
            for (char **l_cur_val = l_vals; *l_cur_val; ++l_cur_val) {
                if (*l_cur_val[0]) {
                    l_values_list = dap_list_append(l_values_list, *l_cur_val);
                } else {
                    l_type = 'r';
                    DAP_DELETE(*l_cur_val);
                }
            }
            DAP_DELETE(l_vals);
            if (l_term != ']' || !l_values_list)
                continue;
        }

        char *l_name = dap_strdup_printf("%s:%s", l_section, l_key);
        HASH_FIND_STR((*a_conf)->items, l_name, l_item);

        switch (l_type) {
        case 'r':
            if (l_item) {
                DAP_DELETE(l_name);
                HASH_DEL((*a_conf)->items, l_item);
                dap_conf_item_del(l_item);
            }
            dap_list_free_full(l_values_list, NULL);
            break;
        default:
            if (!l_item) {
                l_item = DAP_NEW_Z(dap_conf_item_t);
                l_item->name = l_name;
                HASH_ADD_KEYPTR(hh, (*a_conf)->items, l_item->name, strlen(l_item->name), l_item);
            } else {
                DAP_DELETE(l_name);
            }
            l_item->type = l_type;
            l_item_val.val_list = l_item_val.val_list ? dap_list_concat(l_item_val.val_list, l_values_list) : l_values_list;
            l_values_list = NULL;
            break;
        }
    }
    fclose(f);
    return 0;
}

dap_conf_t *dap_conf_load(const char* a_file_path) {
    if (!a_file_path || !a_file_path[0]) {
        log_it(L_ERROR, "Empty config name!");
        return NULL;
    }
    log_it(L_DEBUG, "Looking for config name %s...", a_file_path);
    char l_path[MAX_PATH] = { '\0' };
    int l_pos = dap_strncmp(a_file_path, s_configs_path, strlen(s_configs_path))
            ? dap_snprintf(l_path, MAX_PATH, "%s/%s.cfg", s_configs_path, a_file_path)
            : dap_snprintf(l_path, MAX_PATH, "%s.cfg", a_file_path);

    if (l_pos >= MAX_PATH) {
        log_it(L_ERROR, "Too long config name!");
        return NULL;
    }

    int l_basic_len = strlen(l_path) - 4;
    char *l_basic_name = dap_strdup_printf("%.*s", l_basic_len, l_path);
    dap_conf_t *l_conf = NULL;
    HASH_FIND_STR(g_configs_table, l_basic_name, l_conf);
    if (!l_conf) {
        l_conf = DAP_NEW_Z(dap_conf_t);
        l_conf->path = l_basic_name;
        HASH_ADD_KEYPTR(hh, g_configs_table, l_conf->path, l_basic_len, l_conf);
    } else {
        DAP_DELETE(l_basic_name);
    }

    if (_dap_conf_load(l_path, &l_conf))
       return NULL;

    if (l_pos >= MAX_PATH - 3)
        return l_conf;

    strncpy(l_path + l_pos, ".d", 2);
    struct dirent **l_entries;
    int l_err = scandir(l_path, &l_entries, 0, alphasort);
    if (l_err < 0) {
        log_it(L_ERROR, "Cannot open directory %s", l_path);
        return l_conf;
    }
    for (int i = 0; i < l_err; ++i) {
        if (!strncmp(l_entries[i]->d_name + strlen(l_entries[i]->d_name) - 4, ".cfg", 4)) {
            char *l_entry_file = dap_strdup_printf("%s/%s", l_path, l_entries[i]->d_name);
            _dap_conf_load(l_entry_file, &l_conf);
            DAP_DELETE(l_entry_file);
        }
        DAP_DELETE(l_entries[i]);
    }
    DAP_DELETE(l_entries);
    return l_conf;
}

int64_t dap_conf_get_item_int(dap_conf_t *a_config, const char *a_section, const char *a_item_name, int64_t a_default) {
    char *l_key = dap_strdup_printf("%s:%s", a_section, a_item_name);
    struct dap_conf_item *l_item;
    dap_tsd_t *l_val_tsd;
    HASH_FIND_STR(a_config->items, l_key, l_item);
    if (!l_item || !(l_val_tsd = dap_tsd_find(l_item->val, l_item->size, 'd'))) {
        log_it(L_ERROR, "Not found param \"%s\" of specified type", l_key);
        DAP_DELETE(l_key);
        return a_default;
    }
    dap_tsd_t *;
    if (!l_tsd) {
        log_it(L_ERROR, "")
    }
}

void dap_conf_item_del(struct dap_conf_item *a_item) {
    DAP_DELETE(a_item->name);
    switch (a_item->type) {
    case 's':
        DAP_DELETE(a_item->val.val_str);
        break;
    case 'a':
        dap_list_free_full(a_item->val.val_list, NULL);
        break;
    default:
        break;
    }
    DAP_DELETE(a_item);
}

void dap_conf_close(dap_conf_t *a_conf) {
    DAP_DEL_Z(a_conf->path);
    dap_conf_item_t *l_item = NULL, *l_tmp = NULL;
    HASH_ITER(hh, a_conf->items, l_item, l_tmp) {
        HASH_DEL(a_conf->items, l_item);
        dap_conf_item_del(l_item);
    }
    DAP_DELETE(a_conf);
}
