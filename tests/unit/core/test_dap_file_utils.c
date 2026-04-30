/**
 * @file test_dap_file_utils.c
 * @brief Unit tests for dap_file_utils path handling.
 */

#include <dap_test.h>
#include <dap_common.h>
#include <dap_file_utils.h>
#include <dap_strfuncs.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef _WIN32
static char s_test_root[PATH_MAX];

static void make_path(char *a_out, size_t a_out_size, const char *a_base, const char *a_child)
{
    int l_rc = snprintf(a_out, a_out_size, "%s/%s", a_base, a_child);
    dap_assert_PIF(l_rc > 0 && (size_t)l_rc < a_out_size, "Build test path");
}

static bool path_exists_exact(const char *a_path)
{
    struct stat l_stat;
    return stat(a_path, &l_stat) == 0;
}

static bool path_is_file_exact(const char *a_path)
{
    struct stat l_stat;
    return stat(a_path, &l_stat) == 0 && S_ISREG(l_stat.st_mode);
}

static bool path_is_dir_exact(const char *a_path)
{
    struct stat l_stat;
    return stat(a_path, &l_stat) == 0 && S_ISDIR(l_stat.st_mode);
}

static void mkdir_checked(const char *a_path)
{
    if (mkdir(a_path, 0700) != 0 && errno != EEXIST) {
        dap_test_msg("mkdir(%s) failed: %s", a_path, strerror(errno));
        dap_fail("Create test directory");
    }
}

static void write_file_checked(const char *a_path, const char *a_contents)
{
    FILE *l_file = fopen(a_path, "wb");
    if (!l_file) {
        dap_test_msg("fopen(%s) failed: %s", a_path, strerror(errno));
        dap_fail("Create test file");
    }

    size_t l_len = strlen(a_contents);
    if (fwrite(a_contents, 1, l_len, l_file) != l_len) {
        fclose(l_file);
        dap_fail("Write test file");
    }

    if (fclose(l_file) != 0)
        dap_fail("Close test file");
}

static void setup_test_root(void)
{
    char l_template[] = "/tmp/dap_file_utils_test_XXXXXX";
    char *l_root = mkdtemp(l_template);
    dap_assert_PIF(l_root != NULL, "Create temporary test root");

    int l_rc = snprintf(s_test_root, sizeof(s_test_root), "%s", l_root);
    dap_assert_PIF(l_rc > 0 && (size_t)l_rc < sizeof(s_test_root), "Store temporary test root");
}

static void cleanup_test_root(void)
{
    if (s_test_root[0]) {
        dap_rm_rf(s_test_root);
        s_test_root[0] = '\0';
    }
}
#endif

static void test_path_to_native_inplace(void)
{
    char l_path[] = "a\\b/c\\d";

    dap_path_to_native_inplace(l_path);
#ifdef _WIN32
    dap_assert(strcmp(l_path, "a\\b\\c\\d") == 0, "dap_path_to_native_inplace converts slashes on Windows");
#else
    dap_assert(strcmp(l_path, "a\\b/c\\d") == 0, "dap_path_to_native_inplace preserves POSIX backslashes");
#endif
}

#ifdef _WIN32
static char s_windows_test_root[PATH_MAX];

static void windows_create_dir_checked(const char *a_path)
{
    if (!CreateDirectoryA(a_path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        dap_test_msg("CreateDirectoryA(%s) failed: %lu", a_path, (unsigned long)GetLastError());
        dap_fail("Create Windows test directory");
    }
}

static void windows_write_file_checked(const char *a_path, const char *a_contents)
{
    FILE *l_file = fopen(a_path, "wb");
    if (!l_file) {
        dap_test_msg("fopen(%s) failed", a_path);
        dap_fail("Create Windows test file");
    }

    size_t l_len = strlen(a_contents);
    if (fwrite(a_contents, 1, l_len, l_file) != l_len) {
        fclose(l_file);
        dap_fail("Write Windows test file");
    }

    if (fclose(l_file) != 0)
        dap_fail("Close Windows test file");

    SetFileAttributesA(a_path, FILE_ATTRIBUTE_NORMAL);
}

static char *windows_path_with_slashes(const char *a_path)
{
    char *l_path = dap_strdup(a_path);
    dap_assert_PIF(l_path != NULL, "Duplicate Windows path");

    for (char *l_pos = l_path; *l_pos; l_pos++) {
        if (*l_pos == '\\')
            *l_pos = '/';
    }
    return l_path;
}

static void setup_windows_test_root(void)
{
    char l_temp_path[PATH_MAX];
    DWORD l_temp_len = GetTempPathA((DWORD)sizeof(l_temp_path), l_temp_path);
    dap_assert_PIF(l_temp_len > 0 && l_temp_len < sizeof(l_temp_path), "Get Windows temporary path");

    int l_rc = snprintf(s_windows_test_root, sizeof(s_windows_test_root), "%sdap_file_utils_test_%lu_%lu",
                        l_temp_path, (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
    dap_assert_PIF(l_rc > 0 && (size_t)l_rc < sizeof(s_windows_test_root), "Build Windows temporary test root");

    dap_path_to_native_inplace(s_windows_test_root);
    dap_rm_rf(s_windows_test_root);
    windows_create_dir_checked(s_windows_test_root);
}

static void cleanup_windows_test_root(void)
{
    if (s_windows_test_root[0]) {
        dap_rm_rf(s_windows_test_root);
        s_windows_test_root[0] = '\0';
    }
}

static void test_windows_path_to_native_variants(void)
{
    char l_drive_path[] = "C:/cellframe/data/file.txt";
    char l_unc_path[] = "//server/share/folder/file.txt";
    char l_mixed_relative[] = "one/two\\three/four";

    dap_path_to_native_inplace(l_drive_path);
    dap_path_to_native_inplace(l_unc_path);
    dap_path_to_native_inplace(l_mixed_relative);

    dap_assert(strcmp(l_drive_path, "C:\\cellframe\\data\\file.txt") == 0, "Windows drive path separators are normalized");
    dap_assert(strcmp(l_unc_path, "\\\\server\\share\\folder\\file.txt") == 0, "Windows UNC path separators are normalized");
    dap_assert(strcmp(l_mixed_relative, "one\\two\\three\\four") == 0, "Windows mixed relative separators are normalized");
}

static void test_windows_build_filename_normalizes_separators(void)
{
    char *l_drive_path = dap_build_filename("C:/cellframe", "data/subdir", "file.txt", NULL);
    char *l_unc_path = dap_build_filename("//server/share", "folder/subdir", "file.txt", NULL);

    dap_assert(l_drive_path != NULL, "dap_build_filename returns Windows drive path");
    dap_assert(l_unc_path != NULL, "dap_build_filename returns Windows UNC path");
    dap_assert(strcmp(l_drive_path, "C:\\cellframe\\data\\subdir\\file.txt") == 0, "dap_build_filename normalizes Windows drive path");
    dap_assert(strcmp(l_unc_path, "\\\\server\\share\\folder\\subdir\\file.txt") == 0, "dap_build_filename normalizes Windows UNC path");

    DAP_DELETE(l_drive_path);
    DAP_DELETE(l_unc_path);
}

static void test_windows_file_helpers_normalize_separators(void)
{
    char *l_dir_path = dap_build_filename(s_windows_test_root, "helpers", NULL);
    dap_assert_PIF(l_dir_path != NULL, "Build Windows helper directory path");
    char *l_file_path = dap_build_filename(l_dir_path, "file.txt", NULL);
    dap_assert_PIF(l_file_path != NULL, "Build Windows helper file path");

    dap_assert(dap_mkdir_with_parents(l_dir_path) == 0, "Create Windows helper fixture directory");
    windows_write_file_checked(l_file_path, "helper contents\n");

    char *l_dir_slash_path = windows_path_with_slashes(l_dir_path);
    char *l_file_slash_path = windows_path_with_slashes(l_file_path);

    dap_assert(dap_dir_test(l_dir_slash_path), "dap_dir_test normalizes slash input on Windows");
    dap_assert(!dap_dir_test(l_file_slash_path), "dap_dir_test rejects normalized file path on Windows");
    dap_assert(dap_file_test(l_file_slash_path), "dap_file_test normalizes slash input on Windows");
    dap_assert(dap_file_simple_test(l_file_slash_path), "dap_file_simple_test normalizes slash input on Windows");
    dap_assert(!dap_file_test(l_dir_slash_path), "dap_file_test rejects normalized directory path on Windows");
    dap_assert(!dap_file_simple_test(l_dir_slash_path), "dap_file_simple_test rejects normalized directory path on Windows");

    size_t l_length = 0;
    char *l_contents = dap_file_get_contents2(l_file_slash_path, &l_length);
    dap_assert(l_contents != NULL, "dap_file_get_contents2 normalizes slash input on Windows");
    dap_assert(l_length == strlen("helper contents\n"), "dap_file_get_contents2 reports normalized Windows file length");
    dap_assert(strcmp(l_contents, "helper contents\n") == 0, "dap_file_get_contents2 reads normalized Windows file path");

    DAP_DELETE(l_contents);
    DAP_DELETE(l_dir_slash_path);
    DAP_DELETE(l_file_slash_path);
    DAP_DELETE(l_file_path);
    DAP_DELETE(l_dir_path);
}

static void test_windows_mkdir_and_rm_rf_normalize_separators(void)
{
    char *l_parent_path = dap_build_filename(s_windows_test_root, "mkdir_rm", NULL);
    dap_assert_PIF(l_parent_path != NULL, "Build Windows rm parent path");
    char *l_nested_path = dap_build_filename(l_parent_path, "one", "two", NULL);
    dap_assert_PIF(l_nested_path != NULL, "Build Windows mkdir nested path");

    char *l_nested_slash_path = windows_path_with_slashes(l_nested_path);
    char *l_parent_slash_path = windows_path_with_slashes(l_parent_path);

    dap_assert(dap_mkdir_with_parents(l_nested_slash_path) == 0, "dap_mkdir_with_parents normalizes slash input on Windows");
    dap_assert(dap_dir_test(l_nested_path), "dap_mkdir_with_parents creates normalized Windows nested directory");

    dap_rm_rf(l_parent_slash_path);
    dap_assert(!dap_dir_test(l_parent_path), "dap_rm_rf removes normalized Windows directory path");

    DAP_DELETE(l_nested_slash_path);
    DAP_DELETE(l_parent_slash_path);
    DAP_DELETE(l_nested_path);
    DAP_DELETE(l_parent_path);
}
#endif

static void test_file_get_contents2_null_filename(void)
{
    size_t l_length = 123;
    char *l_contents = dap_file_get_contents2(NULL, &l_length);

    dap_assert(l_contents == NULL, "dap_file_get_contents2 rejects NULL filename");
}

#ifndef _WIN32
static void test_build_filename_preserves_literal_backslash(void)
{
    char l_expected[PATH_MAX];
    make_path(l_expected, sizeof(l_expected), s_test_root, "build/a\\b");

    char *l_built = dap_build_filename(s_test_root, "build", "a\\b", NULL);
    dap_assert(l_built != NULL, "dap_build_filename returns path");
    dap_assert(strcmp(l_built, l_expected) == 0, "dap_build_filename preserves literal POSIX backslash");

    DAP_DELETE(l_built);
}

static void test_file_and_dir_helpers_target_literal_path(void)
{
    char l_base[PATH_MAX], l_literal_file[PATH_MAX], l_nested_a[PATH_MAX], l_nested_dir[PATH_MAX];
    make_path(l_base, sizeof(l_base), s_test_root, "file_helpers");
    mkdir_checked(l_base);

    make_path(l_literal_file, sizeof(l_literal_file), l_base, "a\\b");
    make_path(l_nested_a, sizeof(l_nested_a), l_base, "a");
    make_path(l_nested_dir, sizeof(l_nested_dir), l_nested_a, "b");

    write_file_checked(l_literal_file, "literal file");
    mkdir_checked(l_nested_a);
    mkdir_checked(l_nested_dir);

    dap_assert(path_is_file_exact(l_literal_file), "Literal a\\b fixture is a file");
    dap_assert(path_is_dir_exact(l_nested_dir), "Nested a/b fixture is a directory");
    dap_assert(dap_file_test(l_literal_file), "dap_file_test targets literal POSIX backslash path");
    dap_assert(dap_file_simple_test(l_literal_file), "dap_file_simple_test targets literal POSIX backslash path");
    dap_assert(!dap_dir_test(l_literal_file), "dap_dir_test does not target nested directory for literal file path");

    char l_dir_base[PATH_MAX], l_literal_dir[PATH_MAX], l_nested_file_parent[PATH_MAX], l_nested_file[PATH_MAX];
    make_path(l_dir_base, sizeof(l_dir_base), s_test_root, "dir_helpers");
    mkdir_checked(l_dir_base);

    make_path(l_literal_dir, sizeof(l_literal_dir), l_dir_base, "a\\b");
    make_path(l_nested_file_parent, sizeof(l_nested_file_parent), l_dir_base, "a");
    make_path(l_nested_file, sizeof(l_nested_file), l_nested_file_parent, "b");

    mkdir_checked(l_literal_dir);
    mkdir_checked(l_nested_file_parent);
    write_file_checked(l_nested_file, "nested file");

    dap_assert(path_is_dir_exact(l_literal_dir), "Literal a\\b fixture is a directory");
    dap_assert(path_is_file_exact(l_nested_file), "Nested a/b fixture is a file");
    dap_assert(dap_dir_test(l_literal_dir), "dap_dir_test targets literal POSIX backslash path");
    dap_assert(!dap_file_test(l_literal_dir), "dap_file_test does not target nested file for literal directory path");
    dap_assert(!dap_file_simple_test(l_literal_dir), "dap_file_simple_test does not target nested file for literal directory path");
}

static void test_file_get_contents2_targets_literal_path(void)
{
    char l_base[PATH_MAX], l_literal_file[PATH_MAX], l_nested_a[PATH_MAX], l_nested_file[PATH_MAX];
    make_path(l_base, sizeof(l_base), s_test_root, "contents");
    mkdir_checked(l_base);

    make_path(l_literal_file, sizeof(l_literal_file), l_base, "a\\b");
    make_path(l_nested_a, sizeof(l_nested_a), l_base, "a");
    make_path(l_nested_file, sizeof(l_nested_file), l_nested_a, "b");

    write_file_checked(l_literal_file, "literal contents\n");
    mkdir_checked(l_nested_a);
    write_file_checked(l_nested_file, "nested contents\n");

    size_t l_length = 0;
    char *l_contents = dap_file_get_contents2(l_literal_file, &l_length);
    dap_assert(l_contents != NULL, "dap_file_get_contents2 reads literal POSIX backslash path");
    dap_assert(l_length == strlen("literal contents\n"), "dap_file_get_contents2 reports literal file length");
    dap_assert(strcmp(l_contents, "literal contents\n") == 0, "dap_file_get_contents2 does not read nested a/b");

    DAP_DELETE(l_contents);
}

static void test_mkdir_with_parents_targets_literal_path(void)
{
    char l_base[PATH_MAX], l_literal_parent[PATH_MAX], l_literal_child[PATH_MAX], l_nested_parent[PATH_MAX];
    make_path(l_base, sizeof(l_base), s_test_root, "mkdir_helpers");
    mkdir_checked(l_base);

    make_path(l_literal_parent, sizeof(l_literal_parent), l_base, "a\\b");
    make_path(l_literal_child, sizeof(l_literal_child), l_literal_parent, "c");
    make_path(l_nested_parent, sizeof(l_nested_parent), l_base, "a/b");

    dap_assert(dap_mkdir_with_parents(l_literal_child) == 0, "dap_mkdir_with_parents accepts literal POSIX backslash path");
    dap_assert(path_is_dir_exact(l_literal_parent), "dap_mkdir_with_parents creates literal a\\b directory");
    dap_assert(path_is_dir_exact(l_literal_child), "dap_mkdir_with_parents creates child under literal a\\b");
    dap_assert(!path_exists_exact(l_nested_parent), "dap_mkdir_with_parents does not create nested a/b");
}

static void test_canonicalize_filename_targets_literal_path(void)
{
    char l_base[PATH_MAX], l_literal_file[PATH_MAX], l_nested_a[PATH_MAX], l_nested_file[PATH_MAX];
    make_path(l_base, sizeof(l_base), s_test_root, "canonicalize");
    mkdir_checked(l_base);

    make_path(l_literal_file, sizeof(l_literal_file), l_base, "a\\b");
    make_path(l_nested_a, sizeof(l_nested_a), l_base, "a");
    make_path(l_nested_file, sizeof(l_nested_file), l_nested_a, "b");

    write_file_checked(l_literal_file, "literal canonical");
    mkdir_checked(l_nested_a);
    write_file_checked(l_nested_file, "nested canonical");

    char *l_expected = realpath(l_literal_file, NULL);
    char *l_canonical = dap_canonicalize_filename("a\\b", l_base);

    dap_assert(l_expected != NULL, "realpath resolves literal POSIX backslash fixture");
    dap_assert(l_canonical != NULL, "dap_canonicalize_filename resolves literal POSIX backslash path");
    dap_assert(strcmp(l_canonical, l_expected) == 0, "dap_canonicalize_filename does not resolve nested a/b");

    DAP_DELETE(l_expected);
    DAP_DELETE(l_canonical);
}

static void test_rm_rf_targets_literal_path(void)
{
    char l_base[PATH_MAX], l_literal_dir[PATH_MAX], l_literal_child[PATH_MAX];
    char l_nested_a[PATH_MAX], l_nested_dir[PATH_MAX], l_nested_child[PATH_MAX];
    make_path(l_base, sizeof(l_base), s_test_root, "rm_rf");
    mkdir_checked(l_base);

    make_path(l_literal_dir, sizeof(l_literal_dir), l_base, "a\\b");
    make_path(l_literal_child, sizeof(l_literal_child), l_literal_dir, "child.txt");
    make_path(l_nested_a, sizeof(l_nested_a), l_base, "a");
    make_path(l_nested_dir, sizeof(l_nested_dir), l_nested_a, "b");
    make_path(l_nested_child, sizeof(l_nested_child), l_nested_dir, "child.txt");

    mkdir_checked(l_literal_dir);
    write_file_checked(l_literal_child, "literal child");
    mkdir_checked(l_nested_a);
    mkdir_checked(l_nested_dir);
    write_file_checked(l_nested_child, "nested child");

    dap_rm_rf(l_literal_dir);

    dap_assert(!path_exists_exact(l_literal_dir), "dap_rm_rf removes literal POSIX backslash directory");
    dap_assert(!path_exists_exact(l_literal_child), "dap_rm_rf removes child under literal POSIX backslash directory");
    dap_assert(path_is_dir_exact(l_nested_dir), "dap_rm_rf leaves nested a/b directory untouched");
    dap_assert(path_is_file_exact(l_nested_child), "dap_rm_rf leaves child under nested a/b untouched");
}
#endif

int main(void)
{
    dap_log_level_set(L_CRITICAL);
    dap_print_module_name("dap_file_utils");

    test_path_to_native_inplace();
#ifdef _WIN32
    test_windows_path_to_native_variants();
    test_windows_build_filename_normalizes_separators();
    setup_windows_test_root();
    test_windows_file_helpers_normalize_separators();
    test_windows_mkdir_and_rm_rf_normalize_separators();
    cleanup_windows_test_root();
#endif
    test_file_get_contents2_null_filename();

#ifndef _WIN32
    setup_test_root();
    test_build_filename_preserves_literal_backslash();
    test_file_and_dir_helpers_target_literal_path();
    test_file_get_contents2_targets_literal_path();
    test_mkdir_with_parents_targets_literal_path();
    test_canonicalize_filename_targets_literal_path();
    test_rm_rf_targets_literal_path();
    cleanup_test_root();
#else
    dap_assert(1, "POSIX-only path tests skipped on Windows");
#endif

    return 0;
}
