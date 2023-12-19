#include <string.h>
#include "dap_tsd.h"
#include "dap_config2.h"
#include "uthash.h"
#include "dap_file_utils.h"

#define LOG_TAG "dap_config"

dap_conf_t *g_conf = NULL;

typedef struct dap_conf_item dap_conf_item_t;

static char *s_configs_path = NULL;

int dap_conf_init(const char *a_configs_path)
{
    if (!a_configs_path || !a_configs_path[0]) {
        log_it(L_ERROR, "Empty path!");
        return -1;
    }
#ifdef _WIN32
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

dap_conf_t *dap_conf_load(const char* a_file_name) {
    if (!a_file_name || !a_file_name[0]) {
        log_it(L_ERROR, "Empty config name!");
        return NULL;
    }
    log_it(L_DEBUG, "Looking for config name %s...", a_file_name);
    char *l_file_path = dap_strdup_printf("%s/%s.cfg",
                                          s_configs_path, a_file_name);
    FILE *f = fopen(l_file_path, "r");
    if (!f) {
        log_it(L_ERROR, "Can't open config file \"%s\", error %d", l_file_path, errno);
        return NULL;
    }
    unsigned l_max_len = 1024;
    log_it(L_DEBUG,"Opened config %s", l_file_path);

    dap_conf_t *l_conf = NULL;
    HASH_FIND_STR(g_conf, l_file_path, l_conf);
    if (!l_conf) {
        l_conf = DAP_NEW_Z(dap_conf_t);

    }
    dap_conf_t *l_conf = g_conf ? g_conf : DAP_NEW_Z(dap_conf_t);
    char *l_line = DAP_NEW_STACK_SIZE(char, l_max_len), *l_section = NULL;
    dap_conf_item_t     *l_item             = NULL;
    dap_list_t          *l_values_tsd_list  = NULL;
    size_t l_values_size = 0;
    for (uint16_t l_line_counter = 0; fgets(l_line, l_max_len, f); ++l_line_counter) {
        unsigned l_eol = strcspn(l_line, "#\r\n");
        if (l_eol == l_max_len - 1) {
            log_it(L_WARNING, "Config \"%s\": line %d is too long. Dump it",
                   l_file_path, l_line_counter);
            continue;
        }
        l_line[l_eol] = '\0';
        l_line = dap_strstrip(l_line);
        unsigned l_stripped_len = strlen(l_line);
        if (!l_stripped_len)
            // No useful data remained
            continue;
        char *l_key = NULL, *l_val = NULL;
        if (!l_values_tsd_list) {
            --l_stripped_len;
            if (l_line[0] == '[' && l_line[l_stripped_len] == ']') {
                // A section start
                l_line[l_stripped_len] = '\0';
                DAP_DEL_Z(l_section);
                l_section = dap_strdup(l_line + 1);
                continue;
            } else if (!l_section) {
                log_it(L_WARNING, "Config \"%s\": line %d belongs to unknown section. Dump it",
                       l_file_path, l_line_counter);
                continue;
            }
            if (!strchr(l_line, '=')) {
                log_it(L_WARNING, "Config \"%s\": unknown pattern on line %d, dump it",
                       l_file_path, l_line_counter);
                continue;
            }
            l_key = strtok_r(l_line, "=", &l_val);
        } else {
            l_val = l_line;
        }
        dap_tsd_t *l_tsd = NULL;
        char l_type = '\0';
        if (*l_val != '[' && !l_values_tsd_list) {
            // Single val
            l_type = 'd';
            if (!dap_strcmp(l_val, "true") || !dap_strcmp(l_val, "false"))
                l_type = 'b';
            else {
                for (char *c = l_val; *c != '\0'; ++c) {
                    if (!isdigit(*c)) {
                        l_type = 's';
                        break;
                    }
                }
            }
            switch (l_type) {
            case 'd': {
                uint64_t l_uval = strtoull(l_val, NULL, 10);
                l_tsd = dap_tsd_create_scalar(l_type, l_uval);
                break;
            }
            case 'b': {
                bool l_bval = l_val[0] == 't';
                l_tsd = dap_tsd_create_scalar(l_type, l_bval);
                break;
            }
            case 's':
                l_tsd = dap_tsd_create_string(l_type, l_val);
                break;
            default: break;
            }
        } else {
            // Array of strings
            if (!l_values_tsd_list)
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
                    l_tsd = dap_tsd_create_string(l_type, *l_cur_val);
                    l_values_tsd_list = dap_list_append(l_values_tsd_list, l_tsd);
                    l_values_size += dap_tsd_size(l_tsd);
                }
                DAP_DELETE(*l_cur_val);
            }
            DAP_DELETE(l_vals);
            if (l_term != ']' || !l_values_size)
                continue;
        }
        if (!l_item) {
            l_item = DAP_NEW_Z(dap_conf_item_t);
            l_item->name = dap_strdup_printf("%s:%s", l_section, l_key);
        }

        if (l_values_size && l_values_tsd_list) {
            l_item->val = DAP_NEW_Z_SIZE(char, l_values_size);
            for (dap_list_t *l_elem = l_values_tsd_list; l_elem; l_elem = dap_list_next(l_elem)) {
                l_tsd = l_elem->data;
                l_values_size -= dap_tsd_size(l_tsd);
                memcpy(l_item->val + l_values_size, l_tsd, dap_tsd_size(l_tsd));
                DAP_DELETE(l_tsd);
            }
            if (l_values_size)
                log_it(L_ERROR, "Inconsistent shift %zu, must be 0!", l_values_size);
            dap_list_free(l_values_tsd_list);
            l_values_tsd_list = NULL;
        } else {
            l_item->val = DAP_DUP_SIZE(l_tsd, dap_tsd_size(l_tsd));
            DAP_DELETE(l_tsd);
        }

        dap_conf_item_t *l_item_tmp = NULL;
        HASH_FIND_STR(l_conf->items, l_item->name, l_item_tmp);
        if (l_item_tmp) {
            HASH_DEL(l_conf->items, l_item_tmp);
            DAP_DELETE(l_item_tmp);
        }
        l_item_tmp = l_item;
        HASH_ADD_KEYPTR(hh, l_conf->items, l_item->name, strlen(l_item->name), l_item_tmp);
        l_item = NULL;
    }

    if (l_item) {
        if (l_values_size && l_values_tsd_list) {
            l_item->val = DAP_NEW_Z_SIZE(char, l_values_size);
            for (dap_list_t *l_elem = l_values_tsd_list; l_elem; l_elem = dap_list_next(l_elem)) {
                dap_tsd_t *l_tsd = l_elem->data;
                l_values_size -= dap_tsd_size(l_tsd);
                memcpy(l_item->val + l_values_size, l_tsd, dap_tsd_size(l_tsd));
                DAP_DELETE(l_tsd);
            }
        }
        dap_conf_item_t *l_item_tmp = NULL;
        HASH_FIND_STR(l_conf->items, l_item->name, l_item_tmp);
        if (l_item_tmp) {
            HASH_DEL(l_conf->items, l_item_tmp);
            DAP_DELETE(l_item_tmp);
        }
        HASH_ADD_KEYPTR(hh, l_conf->items, l_item->name, strlen(l_item->name), l_item);
    }
    if (g_conf != l_conf)
        g_conf = l_conf;
    fclose(f);
    DAP_DELETE(l_file_path);
    return l_conf;
}

void dap_conf_close(dap_conf_t *a_conf) {
    DAP_DEL_Z(a_conf->path);
    dap_conf_item_t *l_item = NULL, *l_tmp = NULL;
    HASH_ITER(hh, a_conf->items, l_item, l_tmp) {
        HASH_DEL(a_conf->items, l_item);
        DAP_DELETE(l_item->name);
        DAP_DELETE(l_item->val);
        DAP_DELETE(l_item);
    }
    DAP_DELETE(a_conf);
}
