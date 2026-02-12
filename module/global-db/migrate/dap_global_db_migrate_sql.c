/*
 * DAP Global DB Migration - SQL Dump Implementation
 *
 * Parses SQL dump files and migrates data to native B-tree format.
 * Supports SQLite and PostgreSQL dump formats.
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_global_db.h"
#include "dap_global_db_storage.h"
#include "dap_global_db_migrate.h"

#define LOG_TAG "dap_global_db_migrate_sql"

// Maximum line length in SQL dump
#define MAX_SQL_LINE    (16 * 1024 * 1024)

// ============================================================================
// SQL parsing helpers
// ============================================================================

// Skip whitespace
static const char *s_skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

// Check if line starts with keyword (case-insensitive)
static bool s_starts_with_ci(const char *a_line, const char *a_prefix)
{
    while (*a_prefix) {
        if (tolower((unsigned char)*a_line) != tolower((unsigned char)*a_prefix))
            return false;
        a_line++;
        a_prefix++;
    }
    return true;
}

// Parse quoted string, return allocated copy
static char *s_parse_quoted(const char **a_pos)
{
    const char *p = *a_pos;
    char quote = *p;
    
    if (quote != '\'' && quote != '"')
        return NULL;
    
    p++;
    const char *start = p;
    
    while (*p && *p != quote) {
        if (*p == '\\' && p[1])
            p++;
        p++;
    }
    
    size_t len = p - start;
    char *result = DAP_NEW_SIZE(char, len + 1);
    if (!result)
        return NULL;
    
    // Unescape
    char *d = result;
    p = start;
    while (*p && *p != quote) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'n': *d++ = '\n'; break;
                case 'r': *d++ = '\r'; break;
                case 't': *d++ = '\t'; break;
                case '0': *d++ = '\0'; break;
                default:  *d++ = *p; break;
            }
        } else {
            *d++ = *p;
        }
        p++;
    }
    *d = '\0';
    
    if (*p == quote)
        p++;
    
    *a_pos = p;
    return result;
}

// Parse hex blob (X'...' or \x...)
static byte_t *s_parse_blob(const char **a_pos, size_t *a_len)
{
    const char *p = *a_pos;
    
    // Skip X or \x prefix
    if (*p == 'X' || *p == 'x')
        p++;
    if (*p == '\'')
        p++;
    else if (*p == '\\' && p[1] == 'x')
        p += 2;
    
    const char *start = p;
    while (isxdigit((unsigned char)*p))
        p++;
    
    size_t hex_len = p - start;
    if (hex_len == 0 || (hex_len % 2) != 0) {
        *a_len = 0;
        return NULL;
    }
    
    *a_len = hex_len / 2;
    byte_t *result = DAP_NEW_SIZE(byte_t, *a_len);
    if (!result)
        return NULL;
    
    for (size_t i = 0; i < *a_len; i++) {
        unsigned int val;
        sscanf(start + i * 2, "%2x", &val);
        result[i] = (byte_t)val;
    }
    
    if (*p == '\'')
        p++;
    
    *a_pos = p;
    return result;
}

// ============================================================================
// SQL INSERT parsing
// ============================================================================

typedef struct {
    char *group;
    char *key;
    byte_t *value;
    size_t value_len;
    uint64_t timestamp;
    uint64_t crc;
    uint8_t flags;
    byte_t *sign;
    size_t sign_len;
} sql_record_t;

static void s_record_free(sql_record_t *r)
{
    DAP_DEL_MULTY(r->group, r->key, r->value, r->sign);
    memset(r, 0, sizeof(*r));
}

// Parse INSERT INTO table VALUES (...)
static bool s_parse_insert(const char *a_line, sql_record_t *a_rec)
{
    memset(a_rec, 0, sizeof(*a_rec));
    
    const char *p = s_skip_ws(a_line);
    
    // INSERT INTO
    if (!s_starts_with_ci(p, "INSERT INTO"))
        return false;
    p += 11;
    p = s_skip_ws(p);
    
    // Table name (group)
    const char *table_start = p;
    while (*p && !isspace((unsigned char)*p) && *p != '(')
        p++;
    
    a_rec->group = dap_strdup_printf("%.*s", (int)(p - table_start), table_start);
    
    // Find VALUES
    p = strstr(p, "VALUES");
    if (!p)
        p = strstr(a_line, "values");
    if (!p) {
        s_record_free(a_rec);
        return false;
    }
    p += 6;
    p = s_skip_ws(p);
    
    // Skip (
    if (*p != '(') {
        s_record_free(a_rec);
        return false;
    }
    p++;
    p = s_skip_ws(p);
    
    // Parse values: (timestamp, crc, key, value, flags, sign)
    // or: (key, value, timestamp, flags)
    // Format depends on original schema
    
    // Try to parse timestamp as number
    char *endptr;
    a_rec->timestamp = strtoull(p, &endptr, 10);
    if (endptr != p) {
        p = endptr;
        p = s_skip_ws(p);
        if (*p == ',') p++;
        p = s_skip_ws(p);
        
        // CRC
        a_rec->crc = strtoull(p, &endptr, 10);
        if (endptr != p) {
            p = endptr;
            p = s_skip_ws(p);
            if (*p == ',') p++;
            p = s_skip_ws(p);
        }
    }
    
    // Key (quoted string)
    a_rec->key = s_parse_quoted(&p);
    if (!a_rec->key) {
        s_record_free(a_rec);
        return false;
    }
    
    p = s_skip_ws(p);
    if (*p == ',') p++;
    p = s_skip_ws(p);
    
    // Value (blob or quoted string)
    if (*p == 'X' || *p == 'x' || (*p == '\\' && p[1] == 'x')) {
        a_rec->value = s_parse_blob(&p, &a_rec->value_len);
    } else if (*p == '\'' || *p == '"') {
        char *val_str = s_parse_quoted(&p);
        if (val_str) {
            a_rec->value_len = strlen(val_str);
            a_rec->value = (byte_t *)val_str;
        }
    }
    
    p = s_skip_ws(p);
    if (*p == ',') p++;
    p = s_skip_ws(p);
    
    // Flags (optional)
    a_rec->flags = (uint8_t)strtoul(p, &endptr, 10);
    if (endptr != p) {
        p = endptr;
        p = s_skip_ws(p);
        if (*p == ',') p++;
        p = s_skip_ws(p);
    }
    
    // Signature (optional blob)
    if (*p == 'X' || *p == 'x' || (*p == '\\' && p[1] == 'x')) {
        a_rec->sign = s_parse_blob(&p, &a_rec->sign_len);
    }
    
    return true;
}

// ============================================================================
// Implementation
// ============================================================================

dap_global_db_migrate_result_t dap_global_db_migrate_sql_impl(
    const char *a_sql_path,
    const char *a_dest_path,
    const dap_global_db_migrate_options_t *a_opts)
{
    dap_global_db_migrate_result_t l_result = {0};
    
    // Initialize destination storage
    if (dap_global_db_storage_init(a_dest_path) < 0) {
        l_result.status = DAP_MIGRATE_ERR_DEST;
        l_result.error_message = dap_strdup("Failed to initialize destination storage");
        return l_result;
    }
    
    FILE *l_fp = fopen(a_sql_path, "r");
    if (!l_fp) {
        l_result.status = DAP_MIGRATE_ERR_SOURCE;
        l_result.error_message = dap_strdup_printf("Cannot open SQL file: %s", a_sql_path);
        dap_global_db_storage_deinit();
        return l_result;
    }
    
    char *l_line = DAP_NEW_SIZE(char, MAX_SQL_LINE);
    if (!l_line) {
        fclose(l_fp);
        l_result.status = DAP_MIGRATE_ERR_MEMORY;
        l_result.error_message = dap_strdup("Memory allocation failed");
        dap_global_db_storage_deinit();
        return l_result;
    }
    
    char *l_current_group = NULL;
    size_t l_line_num = 0;
    
    while (fgets(l_line, MAX_SQL_LINE, l_fp)) {
        l_line_num++;
        
        // Skip comments and empty lines
        const char *p = s_skip_ws(l_line);
        if (*p == '\0' || *p == '-' || *p == '#')
            continue;
        
        // Parse INSERT statements
        if (s_starts_with_ci(p, "INSERT")) {
            sql_record_t l_rec;
            if (s_parse_insert(p, &l_rec)) {
                // Track groups
                if (!l_current_group || strcmp(l_current_group, l_rec.group) != 0) {
                    DAP_DEL_Z(l_current_group);
                    l_current_group = dap_strdup(l_rec.group);
                    l_result.groups_migrated++;
                    
                    if (a_opts->verbose)
                        log_it(L_INFO, "  Migrating group: %s", l_rec.group);
                }
                
                // Get or create B-tree for group
                dap_global_db_btree_t *l_btree = dap_global_db_storage_group_get_or_create(l_rec.group);
                if (l_btree) {
                    // Build key
                    dap_global_db_btree_key_t l_key = {
                        .bets = htobe64(l_rec.timestamp),
                        .becrc = htobe64(l_rec.crc)
                    };
                    
                    // Insert
                    int l_rc = dap_global_db_btree_insert(l_btree, &l_key,
                                                          l_rec.key, l_rec.key ? strlen(l_rec.key) + 1 : 0,
                                                          l_rec.value, l_rec.value_len,
                                                          l_rec.sign, l_rec.sign_len,
                                                          l_rec.flags);
                    if (l_rc >= 0) {
                        l_result.records_migrated++;
                        l_result.bytes_migrated += l_rec.value_len;
                    } else {
                        l_result.records_failed++;
                        if (!a_opts->skip_errors) {
                            log_it(L_ERROR, "Insert failed at line %zu", l_line_num);
                        }
                    }
                }
                
                s_record_free(&l_rec);
                
                // Progress
                if (a_opts->progress_cb && (l_result.records_migrated % 1000 == 0)) {
                    a_opts->progress_cb(l_current_group, l_result.records_migrated, 0, a_opts->progress_arg);
                }
            }
        }
    }
    
    DAP_DEL_MULTY(l_line, l_current_group);
    fclose(l_fp);
    
    // Flush and deinit
    dap_global_db_storage_flush();
    dap_global_db_storage_deinit();
    
    if (a_opts->verbose) {
        log_it(L_INFO, "SQL migration complete: %zu groups, %zu records, %zu bytes",
               l_result.groups_migrated, l_result.records_migrated, l_result.bytes_migrated);
    }
    
    l_result.status = DAP_MIGRATE_OK;
    return l_result;
}
