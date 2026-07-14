/*
 * Authors:
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe  https://cellframe.net
 * Copyright  (c) 2026
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "dap_common.h"
#include "dap_config.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_string.h"
#include "dap_events_socket.h"
#include "dap_cli_http_docs.h"

#define LOG_TAG "dap_cli_http_docs"

#define DAP_CLI_HTTP_DOCS_CFG_PATH "http-index-path"
#define DAP_CLI_HTTP_DOCS_CFG_DEFAULT "../../share/docs/rpc"

#ifndef BUILD_HASH
#define BUILD_HASH "unknown"
#endif
#ifndef BUILD_TS
#define BUILD_TS "unknown"
#endif

static char *s_docs_root = NULL;
static char *s_cfg_section = NULL;
static char *s_index_body = NULL;
static size_t s_index_body_size = 0;
static bool s_docs_stub_mode = false;

typedef struct doc_name_alias {
    const char *doc_name;
    const char *allowed_name;
} doc_name_alias_t;

static const doc_name_alias_t s_doc_aliases[] = {
    { "srv_dex", "srv_xchange" },
    { "dex", "srv_xchange" },
    { NULL, NULL }
};

/**
 * @brief Check whether path points to an existing directory
 * @param a_path Directory path
 * @return true if directory exists
 */
static bool s_is_existing_dir(const char *a_path)
{
    if (!a_path || !*a_path)
        return false;

    struct stat l_stat = { 0 };
#ifndef DAP_OS_WINDOWS
    /* lstat: docs root itself must not be a symlink escape trampoline */
    if (lstat(a_path, &l_stat) != 0)
        return false;
    if (S_ISLNK(l_stat.st_mode)) {
        log_it(L_WARNING, "RPC docs root \"%s\" is a symlink — refused", a_path);
        return false;
    }
#else
    if (stat(a_path, &l_stat) != 0)
        return false;
#endif
    return S_ISDIR(l_stat.st_mode);
}

/**
 * @brief Resolve physical absolute path (follows final symlink components via realpath)
 * @param a_path Input path
 * @return Allocated real path or NULL
 */
static char *s_realpath_dup(const char *a_path)
{
    if (!a_path || !*a_path)
        return NULL;
#ifndef DAP_OS_WINDOWS
    char *l_real = realpath(a_path, NULL);
    if (!l_real)
        return NULL;
    char *l_dup = dap_strdup(l_real);
    free(l_real);
    return l_dup;
#else
    return dap_strdup(a_path);
#endif
}

/**
 * @brief Resolve docs path relative to active config file
 * @param a_cfg_section Config section name
 * @return Allocated docs root path or NULL
 */
static char *s_resolve_docs_root(const char *a_cfg_section)
{
    const char *l_cfg_path = dap_config_get_item_str(g_config, a_cfg_section, DAP_CLI_HTTP_DOCS_CFG_PATH);
    if (!l_cfg_path)
        l_cfg_path = DAP_CLI_HTTP_DOCS_CFG_DEFAULT;

    char *l_logical = NULL;
    if (dap_path_is_absolute(l_cfg_path))
        l_logical = dap_strdup(l_cfg_path);
    else {
        if (!g_config || !g_config->path)
            return NULL;
        char *l_dir = dap_path_get_dirname(g_config->path);
        l_logical = dap_canonicalize_path(l_cfg_path, l_dir);
        DAP_DELETE(l_dir);
    }
    if (!l_logical)
        return NULL;

    /* Prefer realpath so prefix checks use the physical directory */
    char *l_real = s_realpath_dup(l_logical);
    if (l_real) {
        DAP_DELETE(l_logical);
        return l_real;
    }
    return l_logical;
}

/**
 * @brief Check whether file path stays inside docs root (logical string prefix)
 * @param a_file_path Absolute or relative candidate path
 * @return true if path is inside docs root
 */
static bool s_path_under_docs_root(const char *a_file_path)
{
    if (!s_docs_root || !a_file_path)
        return false;

    char *l_root = dap_canonicalize_path(".", s_docs_root);
    char *l_file = dap_canonicalize_path(a_file_path, NULL);
    if (!l_root || !l_file) {
        DAP_DELETE(l_root);
        DAP_DELETE(l_file);
        return false;
    }

    size_t l_root_len = strlen(l_root);
    /* Strip trailing slash on root for stable prefix compare */
    while (l_root_len > 1 && l_root[l_root_len - 1] == '/')
        l_root[--l_root_len] = '\0';

    bool l_ok = !strncmp(l_file, l_root, l_root_len)
            && (l_file[l_root_len] == '/' || l_file[l_root_len] == '\0');
    DAP_DELETE(l_root);
    DAP_DELETE(l_file);
    return l_ok;
}

/**
 * @brief Reject path traversal / symlink escape before reading a docs file
 * @param a_path Candidate filesystem path under docs root
 * @return true if the path is a regular non-symlink file inside docs root
 */
static bool s_is_safe_docs_file(const char *a_path)
{
    if (!a_path || !s_docs_root)
        return false;

    if (!s_path_under_docs_root(a_path))
        return false;

    struct stat l_stat = { 0 };
#ifndef DAP_OS_WINDOWS
    if (lstat(a_path, &l_stat) != 0)
        return false;

    /* Never follow symlinks out of the docs tree */
    if (S_ISLNK(l_stat.st_mode)) {
        log_it(L_WARNING, "Refused symlink docs asset: %s", a_path);
        return false;
    }
#else
    if (stat(a_path, &l_stat) != 0)
        return false;
#endif
    if (!S_ISREG(l_stat.st_mode))
        return false;

#ifndef DAP_OS_WINDOWS
    /* realpath resolves any remaining intermediate links; must stay under root */
    char *l_real_file = s_realpath_dup(a_path);
    char *l_real_root = s_realpath_dup(s_docs_root);
    if (!l_real_file || !l_real_root) {
        DAP_DELETE(l_real_file);
        DAP_DELETE(l_real_root);
        return false;
    }

    size_t l_root_len = strlen(l_real_root);
    while (l_root_len > 1 && l_real_root[l_root_len - 1] == '/')
        l_real_root[--l_root_len] = '\0';

    bool l_ok = !strncmp(l_real_file, l_real_root, l_root_len)
            && (l_real_file[l_root_len] == '/' || l_real_file[l_root_len] == '\0');
    if (!l_ok)
        log_it(L_WARNING, "Docs path escapes root after realpath: %s -> %s (root %s)",
               a_path, l_real_file, l_real_root);

    DAP_DELETE(l_real_file);
    DAP_DELETE(l_real_root);
    return l_ok;
#else
    return true;
#endif
}

/**
 * @brief Return extension for documentation asset (without leading dot)
 * @param a_basename File basename
 * @return "html", "js", "css", "svg" or NULL
 */
static const char *s_doc_extension(const char *a_basename)
{
    if (!a_basename)
        return NULL;
    const char *l_dot = strrchr(a_basename, '.');
    if (!l_dot || l_dot == a_basename)
        return NULL;
    if (!strcmp(l_dot, ".html"))
        return "html";
    if (!strcmp(l_dot, ".js"))
        return "js";
    if (!strcmp(l_dot, ".css"))
        return "css";
    if (!strcmp(l_dot, ".svg"))
        return "svg";
    return NULL;
}

/**
 * @brief Content-Type for a documentation asset extension
 * @param a_ext Extension without leading dot
 * @return MIME type string
 */
static const char *s_doc_content_type(const char *a_ext)
{
    if (!a_ext)
        return "application/octet-stream";
    if (!strcmp(a_ext, "html"))
        return "text/html; charset=utf-8";
    if (!strcmp(a_ext, "js"))
        return "application/javascript; charset=utf-8";
    if (!strcmp(a_ext, "css"))
        return "text/css; charset=utf-8";
    if (!strcmp(a_ext, "svg"))
        return "image/svg+xml";
    return "application/octet-stream";
}

/**
 * @brief Validate documentation file basename
 * @param a_basename File name without directory part
 * @return true if basename is safe to serve
 */
static bool s_is_safe_doc_basename(const char *a_basename)
{
    if (!a_basename || !*a_basename)
        return false;

    const char *l_ext = s_doc_extension(a_basename);
    if (!l_ext)
        return false;

    size_t l_stem_len = (size_t)(strrchr(a_basename, '.') - a_basename);
    if (!l_stem_len)
        return false;

    for (size_t i = 0; i < l_stem_len; ++i) {
        char l_ch = a_basename[i];
        if (!(isalnum((unsigned char)l_ch) || l_ch == '_' || l_ch == '-'))
            return false;
    }
    return true;
}

/**
 * @brief Map documentation module name to allowed_cmd entry
 * @param a_doc_name Module name from file name
 * @return Name used in allowed_cmd list
 */
static const char *s_doc_allowed_name(const char *a_doc_name)
{
    for (size_t i = 0; s_doc_aliases[i].doc_name; ++i) {
        if (!strcmp(a_doc_name, s_doc_aliases[i].doc_name))
            return s_doc_aliases[i].allowed_name;
    }
    return a_doc_name;
}

/**
 * @brief Check whether documentation module is allowed by cli-server config
 * @param a_doc_name Module name from file name
 * @return true if module is allowed
 */
static bool s_doc_module_allowed(const char *a_doc_name)
{
    const char *l_allowed_name = s_doc_allowed_name(a_doc_name);
    const char **l_allowed_cmds = dap_config_get_array_str(g_config, s_cfg_section, "allowed_cmd", NULL);
    if (!l_allowed_cmds)
        return true;
    return !!dap_str_find(l_allowed_cmds, l_allowed_name);
}

/**
 * @brief Build full path to documentation file under docs root
 * @param a_basename File basename
 * @param a_out Output buffer
 * @param a_out_size Output buffer size
 * @return true if resulting path is safe
 */
static bool s_docs_file_path(const char *a_basename, char *a_out, size_t a_out_size)
{
    if (!s_is_safe_doc_basename(a_basename))
        return false;

    int l_written = snprintf(a_out, a_out_size, "%s/%s", s_docs_root, a_basename);
    if (l_written <= 0 || (size_t)l_written >= a_out_size)
        return false;

    return s_is_safe_docs_file(a_out);
}

/**
 * @brief Read file from docs directory if allowed
 * @param a_basename File basename
 * @param a_size Output file size
 * @return Allocated file contents or NULL
 */
static char *s_read_allowed_doc_file(const char *a_basename, size_t *a_size)
{
    if (s_docs_stub_mode || !s_docs_root)
        return NULL;

    char l_path[4096] = { 0 };
    if (!s_docs_file_path(a_basename, l_path, sizeof(l_path)))
        return NULL;

    const char *l_ext = s_doc_extension(a_basename);
    /* Static UI assets are always serveable; HTML modules still respect allowed_cmd */
    if (l_ext && !strcmp(l_ext, "html")) {
        char l_module[128] = { 0 };
        size_t l_name_len = (size_t)(strrchr(a_basename, '.') - a_basename);
        if (l_name_len >= sizeof(l_module))
            return NULL;
        memcpy(l_module, a_basename, l_name_len);
        if (!s_doc_module_allowed(l_module))
            return NULL;
    }

    return dap_file_get_contents2(l_path, a_size);
}

/**
 * @brief Wrap HTTP response with body and content type
 * @param a_status HTTP status code
 * @param a_reason HTTP reason phrase
 * @param a_content_type MIME type
 * @param a_body Response body
 * @param a_body_size Body size in bytes
 * @return Allocated HTTP response
 */
static char *s_wrap_http_response(int a_status, const char *a_reason, const char *a_content_type,
                                  const char *a_body, size_t a_body_size)
{
    return dap_strdup_printf("HTTP/1.1 %d %s\r\n"
                             "Content-Type: %s\r\n"
                             "Content-Length: %zu\r\n"
                             "Connection: close\r\n"
                             "Access-Control-Allow-Origin: *\r\n"
                             "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                             "Access-Control-Allow-Headers: Content-Type\r\n"
                             "\r\n%.*s",
                             a_status, a_reason, a_content_type, a_body_size,
                             (int)a_body_size, a_body ? a_body : "");
}

/**
 * @brief Wrap HTTP response with HTML headers
 * @param a_status HTTP status code
 * @param a_reason HTTP reason phrase
 * @param a_body Response body
 * @param a_body_size Body size in bytes
 * @return Allocated HTTP response
 */
static char *s_wrap_http_html(int a_status, const char *a_reason, const char *a_body, size_t a_body_size)
{
    return s_wrap_http_response(a_status, a_reason, "text/html; charset=utf-8", a_body, a_body_size);
}

/**
 * @brief Send HTTP response and close CLI arg state
 * @param a_es Client socket
 * @param a_arg CLI arg pointer storage
 * @param a_status HTTP status code
 * @param a_reason HTTP reason phrase
 * @param a_content_type MIME type
 * @param a_body Response body
 * @param a_body_size Body size in bytes
 */
static void s_send_response(dap_events_socket_t *a_es, void **a_arg, int a_status, const char *a_reason,
                            const char *a_content_type, const char *a_body, size_t a_body_size)
{
    char *l_response = a_body
            ? s_wrap_http_response(a_status, a_reason, a_content_type, a_body, a_body_size)
            : dap_strdup_printf("HTTP/1.1 %d %s\r\n"
                                "Content-Type: %s\r\n"
                                "Content-Length: 0\r\n"
                                "Connection: close\r\n"
                                "Access-Control-Allow-Origin: *\r\n"
                                "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                                "Access-Control-Allow-Headers: Content-Type\r\n"
                                "\r\n",
                                a_status, a_reason, a_content_type);
    if (l_response) {
        if (!dap_events_socket_write_mt(a_es->worker, a_es->uuid, l_response, strlen(l_response)))
            DAP_DELETE(l_response);
    }
    if (a_arg && *a_arg) {
        DAP_DELETE(*a_arg);
        *a_arg = NULL;
    }
}

/**
 * @brief Send HTML HTTP response and close CLI arg state
 * @param a_es Client socket
 * @param a_arg CLI arg pointer storage
 * @param a_status HTTP status code
 * @param a_reason HTTP reason phrase
 * @param a_body Response body
 * @param a_body_size Body size in bytes
 */
static void s_send_html_response(dap_events_socket_t *a_es, void **a_arg, int a_status, const char *a_reason,
                                   const char *a_body, size_t a_body_size)
{
    s_send_response(a_es, a_arg, a_status, a_reason, "text/html; charset=utf-8", a_body, a_body_size);
}

/**
 * @brief Append documentation module to auto-generated index
 * @param a_index Target string
 * @param a_cmd Command name from allowed_cmd
 */
static void s_index_append_module(dap_string_t **a_index, const char *a_cmd)
{
    char l_basename[160] = { 0 };
    snprintf(l_basename, sizeof(l_basename), "%s.html", a_cmd);

    size_t l_size = 0;
    char *l_body = s_read_allowed_doc_file(l_basename, &l_size);
    if (!l_body)
        return;

    dap_string_append_printf(*a_index,
                             "<section id=\"%s\">\n"
                             "<h2><a href=\"/%s\">%s</a></h2>\n",
                             a_cmd, l_basename, a_cmd);
    dap_string_append_len(*a_index, l_body, (int)l_size);
    dap_string_append(*a_index, "\n</section>\n");
    DAP_DELETE(l_body);
}

/**
 * @brief Build default stub page when RPC docs directory is missing
 */
static void s_build_stub_index(void)
{
    DAP_DELETE(s_index_body);
    s_index_body = NULL;
    s_index_body_size = 0;

    const char *l_app = dap_get_appname();
    s_index_body = dap_strdup_printf(
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "<meta charset=\"utf-8\">\n"
        "<title>%s RPC</title>\n"
        "</head>\n"
        "<body>\n"
        "<h1>Hello, I am %s</h1>\n"
        "<p>Version: %s</p>\n"
        "<p>Build: %s (%s)</p>\n"
        "</body>\n"
        "</html>\n",
        l_app, l_app, DAP_VERSION, BUILD_TS, BUILD_HASH);
    if (s_index_body)
        s_index_body_size = strlen(s_index_body);
}

/**
 * @brief Build auto-generated index page from allowed_cmd modules
 */
static void s_rebuild_index_cache(void)
{
    DAP_DELETE(s_index_body);
    s_index_body = NULL;
    s_index_body_size = 0;

    if (!s_docs_root || s_docs_stub_mode)
        return;

    char l_index_path[4096] = { 0 };
    snprintf(l_index_path, sizeof(l_index_path), "%s/index.html", s_docs_root);
    if (dap_file_test(l_index_path) && s_is_safe_docs_file(l_index_path)) {
        s_index_body = dap_file_get_contents2(l_index_path, &s_index_body_size);
        return;
    }

    dap_string_t *l_index = dap_string_new(
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "<meta charset=\"utf-8\">\n"
        "<title>Cellframe Node RPC</title>\n"
        "</head>\n"
        "<body>\n"
        "<h1>Cellframe Node RPC</h1>\n"
        "<nav><ul>\n");

    uint16_t l_allowed_count = 0;
    const char **l_allowed_cmds = dap_config_get_array_str(g_config, s_cfg_section, "allowed_cmd", &l_allowed_count);
    if (l_allowed_cmds) {
        for (uint16_t i = 0; i < l_allowed_count; ++i) {
            char l_basename[160] = { 0 };
            snprintf(l_basename, sizeof(l_basename), "%s.html", l_allowed_cmds[i]);
            char l_path[4096] = { 0 };
            if (!s_docs_file_path(l_basename, l_path, sizeof(l_path)) || !dap_file_test(l_path))
                continue;
            dap_string_append_printf(l_index, "<li><a href=\"/#%s\">%s</a></li>\n", l_allowed_cmds[i], l_allowed_cmds[i]);
        }
    }

    dap_string_append(l_index, "</ul></nav>\n");

    if (l_allowed_cmds) {
        for (uint16_t i = 0; i < l_allowed_count; ++i)
            s_index_append_module(&l_index, l_allowed_cmds[i]);
    }

    dap_string_append(l_index,
                     "</body>\n"
                     "</html>\n");

    s_index_body_size = (size_t)l_index->len;
    s_index_body = dap_strdup(l_index->str);
    dap_string_free(l_index, true);
}

/**
 * @brief Parse HTTP GET start-line and extract request path
 * @param a_buf Request buffer
 * @brief Parse HTTP GET or OPTIONS request path
 * @param a_buf Request buffer
 * @param a_buf_size Buffer size
 * @param a_path Output path buffer
 * @param a_path_size Path buffer size
 * @param a_is_options Set to true when method is OPTIONS
 * @return 1 on GET/OPTIONS, 0 if not those methods, -1 on malformed request
 */
static int s_parse_http_doc_path(const char *a_buf, size_t a_buf_size, char *a_path, size_t a_path_size,
                                 bool *a_is_options)
{
    if (a_is_options)
        *a_is_options = false;

    const char *l_line_end = memchr(a_buf, '\n', a_buf_size);
    if (!l_line_end || l_line_end == a_buf || *(l_line_end - 1) != '\r')
        return -1;

    const char *l_cp = a_buf;
    const char *l_line_limit = l_line_end - 1;
    while (l_cp < l_line_limit && isspace((unsigned char)*l_cp))
        l_cp++;

    size_t l_method_len = 0;
    if ((size_t)(l_line_limit - l_cp) >= 3 && !strncmp(l_cp, "GET", 3))
        l_method_len = 3;
    else if ((size_t)(l_line_limit - l_cp) >= 7 && !strncmp(l_cp, "OPTIONS", 7)) {
        l_method_len = 7;
        if (a_is_options)
            *a_is_options = true;
    } else
        return 0;

    l_cp += l_method_len;
    if (l_cp < l_line_limit && !isspace((unsigned char)*l_cp))
        return 0;

    while (l_cp < l_line_limit && isspace((unsigned char)*l_cp))
        l_cp++;

    const char *l_path_start = l_cp;
    while (l_cp < l_line_limit && *l_cp != '/' && !isspace((unsigned char)*l_cp))
        l_cp++;
    if (l_cp < l_line_limit && *l_cp == '/')
        l_path_start = l_cp;

    const char *l_path_end = l_path_start;
    while (l_path_end < l_line_limit && *l_path_end != '?' && !isspace((unsigned char)*l_path_end))
        l_path_end++;

    size_t l_len = (size_t)(l_path_end - l_path_start);
    if (!l_len) {
        if (a_path_size < 2)
            return -1;
        strcpy(a_path, "/");
        return 1;
    }
    if (l_len >= a_path_size)
        return -1;

    memcpy(a_path, l_path_start, l_len);
    a_path[l_len] = '\0';
    return 1;
}

int dap_cli_http_docs_init(const char *a_cfg_section)
{
    dap_cli_http_docs_deinit();

    s_cfg_section = dap_strdup(a_cfg_section);
    s_docs_root = s_resolve_docs_root(a_cfg_section);

    if (!s_docs_root || !s_is_existing_dir(s_docs_root)) {
        log_it(L_WARNING, "RPC docs path \"%s\" not found, serving default stub page",
               s_docs_root ? s_docs_root : "(unresolved)");
        s_docs_stub_mode = true;
        s_build_stub_index();
        return 0;
    }

    s_rebuild_index_cache();
    if (!s_index_body)
        s_build_stub_index();

    log_it(L_NOTICE, "RPC HTTP docs enabled: %s", s_docs_root);
    return 0;
}

void dap_cli_http_docs_deinit(void)
{
    DAP_DEL_Z(s_docs_root);
    DAP_DEL_Z(s_cfg_section);
    DAP_DEL_Z(s_index_body);
    s_index_body_size = 0;
    s_docs_stub_mode = false;
}

int dap_cli_http_docs_try_get(dap_events_socket_t *a_es, void **a_arg)
{
    if (!a_es)
        return 0;

    const char *l_buf = (const char *)a_es->buf_in;
    if (a_es->buf_in_size < 5)
        return 0;

    if (!strstr(l_buf, "\r\n\r\n"))
        return 0;

    char l_path[1024] = { 0 };
    bool l_is_options = false;
    int l_parse_res = s_parse_http_doc_path(l_buf, a_es->buf_in_size, l_path, sizeof(l_path), &l_is_options);
    if (!l_parse_res)
        return 0;
    if (l_parse_res < 0) {
        s_send_html_response(a_es, a_arg, 400, "Bad Request", NULL, 0);
        return 1;
    }

    /* CORS preflight for Try-it-out from browser */
    if (l_is_options) {
        s_send_response(a_es, a_arg, 204, "No Content", "text/plain", NULL, 0);
        return 1;
    }

    if (!strcmp(l_path, "/") || !strcmp(l_path, "/index.html")) {
        if (!s_index_body)
            s_build_stub_index();
        s_send_html_response(a_es, a_arg, 200, "OK", s_index_body, s_index_body_size);
        return 1;
    }

    if (l_path[0] == '/')
        memmove(l_path, l_path + 1, strlen(l_path));

    /* Defense in depth: reject any absolute/relative path component before basename checks */
    if (!*l_path || strchr(l_path, '/') || strchr(l_path, '\\') || strstr(l_path, "..")) {
        s_send_html_response(a_es, a_arg, 404, "Not Found", NULL, 0);
        return 1;
    }

    if (!s_is_safe_doc_basename(l_path)) {
        s_send_html_response(a_es, a_arg, 404, "Not Found", NULL, 0);
        return 1;
    }

    size_t l_size = 0;
    char *l_body = s_read_allowed_doc_file(l_path, &l_size);
    if (!l_body) {
        s_send_html_response(a_es, a_arg, 404, "Not Found", NULL, 0);
        return 1;
    }

    const char *l_ext = s_doc_extension(l_path);
    s_send_response(a_es, a_arg, 200, "OK", s_doc_content_type(l_ext), l_body, l_size);
    DAP_DELETE(l_body);
    return 1;
}
