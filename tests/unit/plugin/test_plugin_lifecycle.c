#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#define rmdir _rmdir
#else
#include <unistd.h>
#endif

#include "dap_common.h"
#include "dap_file_utils.h"
#include "dap_plugin.h"
#include "dap_plugin_binary.h"
#include "dap_plugin_manifest.h"
#include "dap_strfuncs.h"
#include "dap_test.h"

#ifndef TEST_BINARY_NO_ENTRIES_PATH
#define TEST_BINARY_NO_ENTRIES_PATH NULL
#endif

typedef struct test_plugin_counters {
    int load_count;
    int preinit_count;
    int init_count;
    int unload_count;
    int preinit_result;
} test_plugin_counters_t;

typedef struct legacy_callbacks_abi {
    dap_plugin_type_callback_load_t load;
    dap_plugin_type_callback_unload_t unload;
} legacy_callbacks_abi_t;

static test_plugin_counters_t *s_current_counters = NULL;

static int s_test_load(dap_plugin_manifest_t *a_manifest, void **a_pvt_data, char **a_error_str)
{
    (void)a_manifest;
    (void)a_error_str;
    dap_assert(a_pvt_data != NULL, "load receives private data pointer");
    dap_assert(s_current_counters != NULL, "test counters are configured");
    s_current_counters->load_count++;
    *a_pvt_data = s_current_counters;
    return 0;
}

static int s_test_preinit(dap_plugin_manifest_t *a_manifest, void *a_pvt_data, char **a_error_str)
{
    (void)a_manifest;
    (void)a_error_str;
    test_plugin_counters_t *l_counters = (test_plugin_counters_t *)a_pvt_data;
    dap_assert(l_counters != NULL, "preinit receives private data");
    l_counters->preinit_count++;
    return l_counters->preinit_result;
}

static int s_test_init(dap_plugin_manifest_t *a_manifest, void *a_pvt_data, char **a_error_str)
{
    (void)a_manifest;
    (void)a_error_str;
    test_plugin_counters_t *l_counters = (test_plugin_counters_t *)a_pvt_data;
    dap_assert(l_counters != NULL, "init receives private data");
    l_counters->init_count++;
    return 0;
}

static int s_test_unload(dap_plugin_manifest_t *a_manifest, void *a_pvt_data, char **a_error_str)
{
    (void)a_manifest;
    (void)a_error_str;
    test_plugin_counters_t *l_counters = (test_plugin_counters_t *)a_pvt_data;
    dap_assert(l_counters != NULL, "unload receives private data");
    l_counters->unload_count++;
    return 0;
}

static void s_cleanup_manifest(const char *a_name)
{
    if (dap_plugin_status(a_name) == STATUS_RUNNING)
        dap_plugin_stop(a_name);
    dap_plugins_manifest_remove(a_name);
    s_current_counters = NULL;
}

static char *s_make_temp_dir(const char *a_name)
{
#if defined(_WIN32)
    char l_tmp_path[MAX_PATH + 1] = {0};
    DWORD l_tmp_len = GetTempPathA((DWORD)sizeof(l_tmp_path), l_tmp_path);
    dap_assert(l_tmp_len > 0 && l_tmp_len < sizeof(l_tmp_path), "temporary path is available");

    for (int i = 0; i < 100; i++) {
        char l_template[MAX_PATH + 1] = {0};
        int l_rc = snprintf(l_template, sizeof(l_template), "%sdap_plugin_%s_XXXXXX", l_tmp_path, a_name);
        dap_assert(l_rc > 0 && (size_t)l_rc < sizeof(l_template), "temporary plugin directory path fits");

        dap_assert(_mktemp_s(l_template, sizeof(l_template)) == 0 && l_template[0] != '\0',
                   "temporary plugin directory name is generated");
        if (CreateDirectoryA(l_template, NULL))
            return dap_strdup(l_template);

        dap_assert(GetLastError() == ERROR_ALREADY_EXISTS, "temporary plugin directory creation failed unexpectedly");
    }

    dap_assert(false, "temporary plugin directory is created");
    return NULL;
#else
    const char *l_tmpdir = getenv("TMPDIR");
    if (!l_tmpdir || !*l_tmpdir)
        l_tmpdir = "/tmp";
    char l_template[256] = {0};
    int l_rc = snprintf(l_template, sizeof(l_template), "%s/dap_plugin_%s_XXXXXX", l_tmpdir, a_name);
    dap_assert(l_rc > 0 && (size_t)l_rc < sizeof(l_template), "temporary plugin directory path fits");
    char *l_dir = mkdtemp(l_template);
    dap_assert(l_dir != NULL, "temporary plugin directory is created");
    return dap_strdup(l_dir);
#endif
}

static char *s_expected_binary_path(const char *a_dir, const char *a_name)
{
#if defined(DAP_OS_DARWIN)
    return dap_strdup_printf("%s/%s.darwin.%s.dylib", a_dir, a_name, dap_get_arch());
#elif defined(DAP_OS_LINUX)
    return dap_strdup_printf("%s/%s.linux-common.%s.so", a_dir, a_name, dap_get_arch());
#else
    return dap_strdup_printf("%s/%s.windows.%s.dll", a_dir, a_name, dap_get_arch());
#endif
}

static void s_write_text_file(const char *a_path, const char *a_text)
{
    FILE *l_file = fopen(a_path, "wb");
    dap_assert(l_file != NULL, "test plugin file is created");
    size_t l_size = strlen(a_text);
    dap_assert(fwrite(a_text, 1, l_size, l_file) == l_size, "test plugin file is written");
    dap_assert(fclose(l_file) == 0, "test plugin file is closed");
}

static void s_copy_file(const char *a_src_path, const char *a_dst_path)
{
    FILE *l_src = fopen(a_src_path, "rb");
    dap_assert(l_src != NULL, "source fixture plugin is opened");
    FILE *l_dst = fopen(a_dst_path, "wb");
    dap_assert(l_dst != NULL, "destination fixture plugin is created");

    unsigned char l_buf[4096];
    size_t l_read = 0;
    while ((l_read = fread(l_buf, 1, sizeof(l_buf), l_src)) > 0)
        dap_assert(fwrite(l_buf, 1, l_read, l_dst) == l_read, "fixture plugin chunk is copied");

    dap_assert(ferror(l_src) == 0, "fixture plugin is read completely");
    dap_assert(fclose(l_src) == 0, "source fixture plugin is closed");
    dap_assert(fclose(l_dst) == 0, "destination fixture plugin is closed");
}

static bool s_path_is_mapped(const char *a_path)
{
#if defined(DAP_OS_LINUX)
    FILE *l_maps = fopen("/proc/self/maps", "r");
    dap_assert(l_maps != NULL, "process memory map is readable");
    char l_line[4096];
    bool l_found = false;
    while (fgets(l_line, sizeof(l_line), l_maps)) {
        if (strstr(l_line, a_path)) {
            l_found = true;
            break;
        }
    }
    fclose(l_maps);
    return l_found;
#else
    (void)a_path;
    return false;
#endif
}

static dap_plugin_manifest_t *s_add_binary_manifest(const char *a_name, const char *a_dir)
{
    dap_plugin_manifest_t *l_manifest = dap_plugin_manifest_add_builtin(a_name, "binary", "test", "1.0",
                                                                         "binary failure fixture", NULL, 0, NULL, 0);
    dap_assert(l_manifest != NULL, "binary fixture manifest is registered");
    l_manifest->path = dap_strdup(a_dir);
    dap_assert(l_manifest->path != NULL, "binary fixture manifest path is assigned");
    return l_manifest;
}

static void s_cleanup_binary_fixture(const char *a_name, const char *a_path, const char *a_dir)
{
    dap_assert(dap_plugins_manifest_remove(a_name), "binary fixture manifest is removed");
    if (a_path)
        remove(a_path);
    if (a_dir)
        rmdir(a_dir);
}

static void test_legacy_callback_layout(void)
{
    dap_print_module_name("legacy callback layout");

    test_plugin_counters_t l_counters = {0};
    s_current_counters = &l_counters;

    dap_plugin_type_callbacks_t l_public_callbacks = { s_test_load, s_test_unload };
    dap_assert(l_public_callbacks.load == s_test_load, "legacy initializer keeps load first");
    dap_assert(l_public_callbacks.unload == s_test_unload, "legacy initializer keeps unload second");
    dap_assert(offsetof(dap_plugin_type_callbacks_t, unload) == sizeof(l_public_callbacks.load),
               "legacy callback struct keeps two-field layout");

    legacy_callbacks_abi_t l_legacy_callbacks = { s_test_load, s_test_unload };
    dap_assert(dap_plugin_type_create("test_legacy_type", (dap_plugin_type_callbacks_t *)&l_legacy_callbacks) == 0,
               "legacy two-field callback descriptor is accepted");
    dap_assert(dap_plugin_manifest_add_builtin("test_legacy_plugin", "test_legacy_type", "test", "1.0",
                                               "legacy plugin", NULL, 0, NULL, 0) != NULL,
               "legacy test manifest is registered");

    dap_assert(dap_plugin_start_all() == 0, "start_all starts legacy plugin");
    dap_assert(l_counters.load_count == 1, "legacy plugin load called once");
    dap_assert(l_counters.unload_count == 0, "legacy unload is not called during preinit");
    dap_assert(dap_plugin_status("test_legacy_plugin") == STATUS_RUNNING, "legacy plugin reports running after start");
    dap_assert(dap_plugin_stop("test_legacy_plugin") == 0, "legacy plugin stops");
    dap_assert(l_counters.unload_count == 1, "legacy unload called once on stop");

    s_cleanup_manifest("test_legacy_plugin");
}

static void test_start_all_runs_full_lifecycle(void)
{
    dap_print_module_name("start_all lifecycle");

    test_plugin_counters_t l_counters = {0};
    s_current_counters = &l_counters;

    dap_plugin_type_callbacks_ex_t l_callbacks = { .size = sizeof(l_callbacks) };
    l_callbacks.load = s_test_load;
    l_callbacks.unload = s_test_unload;
    l_callbacks.preinit = s_test_preinit;
    l_callbacks.init = s_test_init;

    dap_assert(dap_plugin_type_create_ex("test_full_lifecycle_type", &l_callbacks) == 0,
               "extended callback descriptor is accepted");
    dap_assert(dap_plugin_manifest_add_builtin("test_full_lifecycle_plugin", "test_full_lifecycle_type", "test", "1.0",
                                               "full lifecycle plugin", NULL, 0, NULL, 0) != NULL,
               "full lifecycle test manifest is registered");

    dap_assert(dap_plugin_start_all() == 0, "start_all loads, preinits, and inits plugin");
    dap_assert(l_counters.load_count == 1, "load called once");
    dap_assert(l_counters.preinit_count == 1, "preinit called once");
    dap_assert(l_counters.init_count == 1, "init called once");
    dap_assert(dap_plugin_status("test_full_lifecycle_plugin") == STATUS_RUNNING, "plugin reports running after init");
    dap_assert(dap_plugin_stop("test_full_lifecycle_plugin") == 0, "full lifecycle plugin stops");
    dap_assert(l_counters.unload_count == 1, "unload called once");

    s_cleanup_manifest("test_full_lifecycle_plugin");
}

static void test_load_only_does_not_report_running(void)
{
    dap_print_module_name("load-only status");

    test_plugin_counters_t l_counters = {0};
    s_current_counters = &l_counters;

    dap_plugin_type_callbacks_ex_t l_callbacks = { .size = sizeof(l_callbacks) };
    l_callbacks.load = s_test_load;
    l_callbacks.unload = s_test_unload;
    l_callbacks.preinit = s_test_preinit;
    l_callbacks.init = s_test_init;

    dap_assert(dap_plugin_type_create_ex("test_load_only_type", &l_callbacks) == 0,
               "load-only callback descriptor is accepted");
    dap_assert(dap_plugin_manifest_add_builtin("test_load_only_plugin", "test_load_only_type", "test", "1.0",
                                               "load-only plugin", NULL, 0, NULL, 0) != NULL,
               "load-only test manifest is registered");

    dap_assert(dap_plugin_load_all() == 0, "load_all loads plugin");
    dap_assert(l_counters.load_count == 1, "load-only plugin load called once");
    dap_assert(l_counters.preinit_count == 0, "load_all does not call preinit");
    dap_assert(l_counters.init_count == 0, "load_all does not call init");
    dap_assert(dap_plugin_status("test_load_only_plugin") == STATUS_STOPPED,
               "load-only plugin is not reported running");
    dap_assert(dap_plugin_stop("test_load_only_plugin") == 0, "load-only plugin unloads");

    s_cleanup_manifest("test_load_only_plugin");
}

static void test_preinit_failure_skips_init_and_rolls_back(void)
{
    dap_print_module_name("preinit failure rollback");

    test_plugin_counters_t l_counters = {
        .preinit_result = -42
    };
    s_current_counters = &l_counters;

    dap_plugin_type_callbacks_ex_t l_callbacks = { .size = sizeof(l_callbacks) };
    l_callbacks.load = s_test_load;
    l_callbacks.unload = s_test_unload;
    l_callbacks.preinit = s_test_preinit;
    l_callbacks.init = s_test_init;

    dap_assert(dap_plugin_type_create_ex("test_preinit_fail_type", &l_callbacks) == 0,
               "failing preinit callback descriptor is accepted");
    dap_assert(dap_plugin_manifest_add_builtin("test_preinit_fail_plugin", "test_preinit_fail_type", "test", "1.0",
                                               "preinit failure plugin", NULL, 0, NULL, 0) != NULL,
               "preinit failure test manifest is registered");

    dap_assert(dap_plugin_start_all() == 1, "start_all reports preinit failure");
    dap_assert(l_counters.load_count == 1, "load called before preinit failure");
    dap_assert(l_counters.preinit_count == 1, "preinit called once");
    dap_assert(l_counters.init_count == 0, "init skipped after preinit failure");
    dap_assert(l_counters.unload_count == 1, "failed preinit module is rolled back");
    dap_assert(dap_plugin_status("test_preinit_fail_plugin") == STATUS_STOPPED,
               "failed preinit plugin is not reported running");

    s_cleanup_manifest("test_preinit_fail_plugin");
}

static void test_binary_load_failures_cleanup(void)
{
    dap_print_module_name("binary load failure cleanup");

#if defined(DAP_OS_UNIX) && !defined(__ANDROID__)
    dap_assert(dap_plugin_binary_init() == 0, "binary plugin type is registered");

    char *l_missing_dir = s_make_temp_dir("missing");
    s_add_binary_manifest("test_missing_binary", l_missing_dir);
    dap_assert(dap_plugin_load_all() > 0, "missing binary plugin load fails");
    dap_assert(dap_plugin_status("test_missing_binary") == STATUS_STOPPED,
               "missing binary plugin leaves no running module");
    dap_assert(dap_plugin_stop("test_missing_binary") == -5,
               "missing binary plugin leaves no dangling module record");
    s_cleanup_binary_fixture("test_missing_binary", NULL, l_missing_dir);
    DAP_DELETE(l_missing_dir);

    char *l_broken_dir = s_make_temp_dir("broken");
    char *l_broken_path = s_expected_binary_path(l_broken_dir, "test_broken_binary");
    s_write_text_file(l_broken_path, "not a shared library\n");
    s_add_binary_manifest("test_broken_binary", l_broken_dir);
    dap_assert(dap_plugin_load_all() > 0, "broken binary plugin load fails");
    dap_assert(dap_plugin_status("test_broken_binary") == STATUS_STOPPED,
               "broken binary plugin leaves no running module");
    dap_assert(dap_plugin_stop("test_broken_binary") == -5,
               "broken binary plugin leaves no dangling module record");
    s_cleanup_binary_fixture("test_broken_binary", l_broken_path, l_broken_dir);
    DAP_DEL_MULTY(l_broken_path, l_broken_dir);

    char *l_no_entries_dir = s_make_temp_dir("no_entries");
    char *l_no_entries_path = s_expected_binary_path(l_no_entries_dir, "test_no_entries_binary");
    s_copy_file(TEST_BINARY_NO_ENTRIES_PATH, l_no_entries_path);
    dap_assert(!s_path_is_mapped(l_no_entries_path), "no-entry fixture starts unmapped");
    s_add_binary_manifest("test_no_entries_binary", l_no_entries_dir);
    dap_assert(dap_plugin_load_all() > 0, "binary plugin without entry points fails load");
    dap_assert(dap_plugin_status("test_no_entries_binary") == STATUS_STOPPED,
               "no-entry binary plugin leaves no running module");
    dap_assert(dap_plugin_stop("test_no_entries_binary") == -5,
               "no-entry binary plugin leaves no dangling module record");
    dap_assert(!s_path_is_mapped(l_no_entries_path), "no-entry binary plugin handle is closed after failure");
    s_cleanup_binary_fixture("test_no_entries_binary", l_no_entries_path, l_no_entries_dir);
    DAP_DEL_MULTY(l_no_entries_path, l_no_entries_dir);

    dap_plugin_stop_all();
    dap_plugin_binary_deinit();
#else
    dap_pass_msg("binary load failure cleanup skipped on this platform");
#endif
}

int main(void)
{
    dap_log_level_set(L_CRITICAL);
    dap_plugin_manifest_init();

    test_legacy_callback_layout();
    test_start_all_runs_full_lifecycle();
    test_load_only_does_not_report_running();
    test_preinit_failure_skips_init_and_rolls_back();
    test_binary_load_failures_cleanup();

    dap_plugin_manifest_deinit();
    return 0;
}
