/*
 * DAP Memory-Mapped File I/O — Unit Tests
 *
 * Tests all public API functions of dap_mmap:
 * - open/close lifecycle
 * - get_ptr / get_size / get_fd accessors
 * - read/write through mapping
 * - resize (grow & shrink)
 * - sync / sync_range
 * - advise / advise_range
 * - edge cases & error paths
 * - data persistence across close/reopen
 * - private (COW) mapping
 * - concurrent independent mappings
 *
 * Authors:
 * DAP SDK Team
 * Copyright (c) 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

#include "dap_mmap.h"
#include "dap_common.h"
#include "dap_test.h"
#include "dap_test_helpers.h"

#define LOG_TAG "test_dap_mmap"

static const char *s_test_dir = "/tmp/dap_mmap_test";

// ============================================================================
// Helpers
// ============================================================================

static void s_ensure_test_dir(void)
{
    mkdir(s_test_dir, 0755);
}

static void s_make_path(char *a_buf, size_t a_buf_size, const char *a_name)
{
    snprintf(a_buf, a_buf_size, "%s/%s", s_test_dir, a_name);
}

static void s_remove_file(const char *a_path)
{
    unlink(a_path);
}

// ============================================================================
// Test: open/close lifecycle
// ============================================================================

static void test_open_close_basic(void)
{
    dap_print_module_name("mmap open/close basic");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_basic.dat");
    s_remove_file(l_path);

    // Create new file with RDWR + CREATE
    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 4096);
    dap_assert(l_m != NULL, "Open with CREATE succeeds");

    void *l_ptr = dap_mmap_get_ptr(l_m);
    dap_assert(l_ptr != NULL, "get_ptr returns non-NULL");

    size_t l_size = dap_mmap_get_size(l_m);
    dap_assert(l_size >= 4096, "get_size >= requested 4096");

    int l_fd = dap_mmap_get_fd(l_m);
    dap_assert(l_fd >= 0, "get_fd returns valid fd");

    dap_mmap_close(l_m);

    // Verify file exists
    struct stat l_st;
    dap_assert(stat(l_path, &l_st) == 0, "File exists after close");
    dap_assert(l_st.st_size >= 4096, "File size >= 4096 after close");

    s_remove_file(l_path);
    dap_pass_msg("open/close basic");
}

// ============================================================================
// Test: NULL safety
// ============================================================================

static void test_null_safety(void)
{
    dap_print_module_name("mmap NULL safety");

    // open with NULL path should return NULL
    dap_mmap_t *l_m = dap_mmap_open(NULL, DAP_MMAP_RDWR | DAP_MMAP_CREATE, 4096);
    dap_assert(l_m == NULL, "Open with NULL path returns NULL");

    // All accessors safe on NULL
    dap_assert(dap_mmap_get_ptr(NULL) == NULL, "get_ptr(NULL) returns NULL");
    dap_assert(dap_mmap_get_size(NULL) == 0, "get_size(NULL) returns 0");
    dap_assert(dap_mmap_get_fd(NULL) == -1, "get_fd(NULL) returns -1");

    // Mutators safe on NULL
    dap_assert(dap_mmap_resize(NULL, 8192) == -1, "resize(NULL) returns -1");
    dap_assert(dap_mmap_sync(NULL, DAP_MMAP_SYNC_SYNC) == -1, "sync(NULL) returns -1");
    dap_assert(dap_mmap_sync_range(NULL, 0, 100, DAP_MMAP_SYNC_SYNC) == -1, "sync_range(NULL) returns -1");
    dap_assert(dap_mmap_advise(NULL, DAP_MMAP_ADVISE_RANDOM) == -1, "advise(NULL) returns -1");
    dap_assert(dap_mmap_advise_range(NULL, 0, 100, DAP_MMAP_ADVISE_RANDOM) == -1, "advise_range(NULL) returns -1");

    // close(NULL) is safe (no crash)
    dap_mmap_close(NULL);

    dap_pass_msg("NULL safety");
}

// ============================================================================
// Test: open nonexistent without CREATE
// ============================================================================

static void test_open_nonexistent(void)
{
    dap_print_module_name("mmap open nonexistent");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "does_not_exist_12345.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_READ | DAP_MMAP_SHARED, 0);
    dap_assert(l_m == NULL, "Open nonexistent file without CREATE returns NULL");

    dap_pass_msg("open nonexistent");
}

// ============================================================================
// Test: read/write through mapping
// ============================================================================

static void test_read_write(void)
{
    dap_print_module_name("mmap read/write");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_rw.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 8192);
    dap_assert(l_m != NULL, "Open for RW");

    uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
    dap_assert(l_ptr != NULL, "Pointer valid");

    // Write a pattern
    const char *l_magic = "DAP_MMAP_TEST_PATTERN_2026";
    size_t l_magic_len = strlen(l_magic) + 1;
    memcpy(l_ptr, l_magic, l_magic_len);

    // Write pattern at offset 4000
    memcpy(l_ptr + 4000, l_magic, l_magic_len);

    // Read back and verify
    dap_assert(memcmp(l_ptr, l_magic, l_magic_len) == 0, "Pattern at offset 0 correct");
    dap_assert(memcmp(l_ptr + 4000, l_magic, l_magic_len) == 0, "Pattern at offset 4000 correct");

    // Write integers
    for (int i = 0; i < 100; i++) {
        ((uint32_t *)(l_ptr + 1000))[i] = (uint32_t)i * 12345;
    }
    for (int i = 0; i < 100; i++) {
        dap_assert(((uint32_t *)(l_ptr + 1000))[i] == (uint32_t)i * 12345, "Integer pattern correct");
    }

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("read/write");
}

// ============================================================================
// Test: data persistence across close/reopen
// ============================================================================

static void test_persistence(void)
{
    dap_print_module_name("mmap persistence");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_persist.dat");
    s_remove_file(l_path);

    // Write data
    {
        dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 4096);
        dap_assert(l_m != NULL, "Create mapping");

        uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
        // Write marker
        const uint64_t l_marker = 0xDEADBEEFCAFEBABEULL;
        memcpy(l_ptr, &l_marker, sizeof(l_marker));
        // Fill with sequential bytes
        for (int i = 0; i < 256; i++)
            l_ptr[64 + i] = (uint8_t)i;

        dap_mmap_sync(l_m, DAP_MMAP_SYNC_SYNC);
        dap_mmap_close(l_m);
    }

    // Reopen and verify
    {
        dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_READ | DAP_MMAP_SHARED, 0);
        dap_assert(l_m != NULL, "Reopen mapping");
        dap_assert(dap_mmap_get_size(l_m) >= 4096, "Size preserved");

        uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
        uint64_t l_marker;
        memcpy(&l_marker, l_ptr, sizeof(l_marker));
        dap_assert(l_marker == 0xDEADBEEFCAFEBABEULL, "Marker persisted");

        for (int i = 0; i < 256; i++)
            dap_assert(l_ptr[64 + i] == (uint8_t)i, "Sequential bytes persisted");

        dap_mmap_close(l_m);
    }

    s_remove_file(l_path);
    dap_pass_msg("persistence");
}

// ============================================================================
// Test: resize grow
// ============================================================================

static void test_resize_grow(void)
{
    dap_print_module_name("mmap resize grow");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_resize_grow.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 4096);
    dap_assert(l_m != NULL, "Create mapping");

    size_t l_orig_size = dap_mmap_get_size(l_m);

    // Write data at beginning
    uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
    const uint64_t l_marker = 0x1234567890ABCDEFULL;
    memcpy(l_ptr, &l_marker, sizeof(l_marker));

    // Resize to 64K
    int l_rc = dap_mmap_resize(l_m, 65536);
    dap_assert(l_rc == 0, "Resize to 64K succeeds");
    dap_assert(dap_mmap_get_size(l_m) >= 65536, "New size >= 64K");

    // Pointer may have changed after mremap
    l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
    dap_assert(l_ptr != NULL, "Pointer still valid after resize");

    // Old data preserved
    uint64_t l_check;
    memcpy(&l_check, l_ptr, sizeof(l_check));
    dap_assert(l_check == l_marker, "Data preserved after grow");

    // Can write to new region
    memset(l_ptr + 60000, 0xAA, 1000);
    dap_assert(l_ptr[60000] == 0xAA, "Write to grown region succeeds");

    // Resize to 1 MB
    l_rc = dap_mmap_resize(l_m, 1024 * 1024);
    dap_assert(l_rc == 0, "Resize to 1MB succeeds");
    l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
    memcpy(&l_check, l_ptr, sizeof(l_check));
    dap_assert(l_check == l_marker, "Data preserved after 1MB grow");

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("resize grow");
}

// ============================================================================
// Test: resize shrink
// ============================================================================

static void test_resize_shrink(void)
{
    dap_print_module_name("mmap resize shrink");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_resize_shrink.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 1024 * 1024);
    dap_assert(l_m != NULL, "Create 1MB mapping");

    uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
    const uint64_t l_marker = 0xAABBCCDDEEFF0011ULL;
    memcpy(l_ptr, &l_marker, sizeof(l_marker));

    // Shrink to 8K
    int l_rc = dap_mmap_resize(l_m, 8192);
    dap_assert(l_rc == 0, "Shrink to 8K succeeds");
    dap_assert(dap_mmap_get_size(l_m) >= 8192, "Size >= 8K after shrink");

    l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
    uint64_t l_check;
    memcpy(&l_check, l_ptr, sizeof(l_check));
    dap_assert(l_check == l_marker, "Data preserved in retained region after shrink");

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("resize shrink");
}

// ============================================================================
// Test: resize noop (same size)
// ============================================================================

static void test_resize_noop(void)
{
    dap_print_module_name("mmap resize noop");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_resize_noop.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 4096);
    dap_assert(l_m != NULL, "Create mapping");

    size_t l_size = dap_mmap_get_size(l_m);
    void *l_ptr = dap_mmap_get_ptr(l_m);

    // Resize to same page-aligned size
    int l_rc = dap_mmap_resize(l_m, l_size);
    dap_assert(l_rc == 0, "Resize to same size returns 0");
    dap_assert(dap_mmap_get_size(l_m) == l_size, "Size unchanged");
    dap_assert(dap_mmap_get_ptr(l_m) == l_ptr, "Pointer unchanged for noop");

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("resize noop");
}

// ============================================================================
// Test: resize error (zero size)
// ============================================================================

static void test_resize_zero(void)
{
    dap_print_module_name("mmap resize zero");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_resize_zero.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 4096);
    dap_assert(l_m != NULL, "Create mapping");

    int l_rc = dap_mmap_resize(l_m, 0);
    dap_assert(l_rc == -1, "Resize to 0 returns error");
    dap_assert(dap_mmap_get_size(l_m) >= 4096, "Size unchanged after failed resize");

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("resize zero");
}

// ============================================================================
// Test: multiple resizes
// ============================================================================

static void test_resize_multiple(void)
{
    dap_print_module_name("mmap resize multiple");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_resize_multi.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 4096);
    dap_assert(l_m != NULL, "Create mapping");

    // Write marker
    uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
    memset(l_ptr, 0x42, 100);

    // Grow/shrink cycle
    size_t l_sizes[] = {8192, 65536, 4096, 1024 * 1024, 16384, 4096};
    for (size_t i = 0; i < sizeof(l_sizes) / sizeof(l_sizes[0]); i++) {
        int l_rc = dap_mmap_resize(l_m, l_sizes[i]);
        dap_assert(l_rc == 0, "Resize cycle step succeeds");
        dap_assert(dap_mmap_get_ptr(l_m) != NULL, "Pointer valid after resize");
        dap_assert(dap_mmap_get_size(l_m) >= l_sizes[i], "Size correct after resize");

        // First 100 bytes should be preserved (all sizes >= 4096)
        l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
        for (int j = 0; j < 100; j++)
            dap_assert(l_ptr[j] == 0x42, "Data preserved across resize cycle");
    }

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("resize multiple");
}

// ============================================================================
// Test: sync
// ============================================================================

static void test_sync(void)
{
    dap_print_module_name("mmap sync");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_sync.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 8192);
    dap_assert(l_m != NULL, "Create mapping");

    uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
    memset(l_ptr, 0xAB, 8192);

    // Test both sync modes
    int l_rc = dap_mmap_sync(l_m, DAP_MMAP_SYNC_ASYNC);
    dap_assert(l_rc == 0, "Async sync succeeds");

    l_rc = dap_mmap_sync(l_m, DAP_MMAP_SYNC_SYNC);
    dap_assert(l_rc == 0, "Synchronous sync succeeds");

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("sync");
}

// ============================================================================
// Test: sync_range
// ============================================================================

static void test_sync_range(void)
{
    dap_print_module_name("mmap sync_range");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_sync_range.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 16384);
    dap_assert(l_m != NULL, "Create mapping");

    uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
    memset(l_ptr, 0xCD, 16384);

    // Sync first page
    int l_rc = dap_mmap_sync_range(l_m, 0, 4096, DAP_MMAP_SYNC_SYNC);
    dap_assert(l_rc == 0, "Sync range first page");

    // Sync middle range (non-page-aligned offset)
    l_rc = dap_mmap_sync_range(l_m, 1000, 2000, DAP_MMAP_SYNC_SYNC);
    dap_assert(l_rc == 0, "Sync range non-aligned offset");

    // Sync last bytes
    l_rc = dap_mmap_sync_range(l_m, 16000, 384, DAP_MMAP_SYNC_ASYNC);
    dap_assert(l_rc == 0, "Sync range tail");

    // Out-of-bounds should fail
    l_rc = dap_mmap_sync_range(l_m, 16000, 1000, DAP_MMAP_SYNC_SYNC);
    dap_assert(l_rc == -1, "Sync range out-of-bounds returns error");

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("sync_range");
}

// ============================================================================
// Test: advise (all hint types)
// ============================================================================

static void test_advise(void)
{
    dap_print_module_name("mmap advise");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_advise.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 65536);
    dap_assert(l_m != NULL, "Create mapping");

    // All advise types should succeed (advisory, no hard failure)
    int l_rc;
    l_rc = dap_mmap_advise(l_m, DAP_MMAP_ADVISE_NORMAL);
    dap_assert(l_rc == 0, "Advise NORMAL");

    l_rc = dap_mmap_advise(l_m, DAP_MMAP_ADVISE_RANDOM);
    dap_assert(l_rc == 0, "Advise RANDOM");

    l_rc = dap_mmap_advise(l_m, DAP_MMAP_ADVISE_SEQUENTIAL);
    dap_assert(l_rc == 0, "Advise SEQUENTIAL");

    l_rc = dap_mmap_advise(l_m, DAP_MMAP_ADVISE_WILLNEED);
    dap_assert(l_rc == 0, "Advise WILLNEED");

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("advise");
}

// ============================================================================
// Test: advise_range
// ============================================================================

static void test_advise_range(void)
{
    dap_print_module_name("mmap advise_range");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_advise_range.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 65536);
    dap_assert(l_m != NULL, "Create mapping");

    int l_rc;
    l_rc = dap_mmap_advise_range(l_m, 0, 4096, DAP_MMAP_ADVISE_WILLNEED);
    dap_assert(l_rc == 0, "Advise range first page WILLNEED");

    l_rc = dap_mmap_advise_range(l_m, 4096, 8192, DAP_MMAP_ADVISE_SEQUENTIAL);
    dap_assert(l_rc == 0, "Advise range second page SEQUENTIAL");

    // Out-of-bounds should fail
    l_rc = dap_mmap_advise_range(l_m, 60000, 10000, DAP_MMAP_ADVISE_RANDOM);
    dap_assert(l_rc == -1, "Advise range out-of-bounds returns error");

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("advise_range");
}

// ============================================================================
// Test: private mapping (copy-on-write)
// ============================================================================

static void test_private_mapping(void)
{
    dap_print_module_name("mmap private mapping");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_private.dat");
    s_remove_file(l_path);

    // Create shared, write data
    {
        dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 4096);
        dap_assert(l_m != NULL, "Create shared mapping");
        uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
        memset(l_ptr, 0x55, 4096);
        dap_mmap_sync(l_m, DAP_MMAP_SYNC_SYNC);
        dap_mmap_close(l_m);
    }

    // Open private, modify — original file should not change
    {
        dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_PRIVATE, 0);
        dap_assert(l_m != NULL, "Open private mapping");
        uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
        dap_assert(l_ptr[0] == 0x55, "Private sees original data");
        // Modify through private mapping
        memset(l_ptr, 0xAA, 4096);
        dap_assert(l_ptr[0] == 0xAA, "Private modification visible in-process");
        dap_mmap_close(l_m);
    }

    // Reopen shared — data should be unchanged
    {
        dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_READ | DAP_MMAP_SHARED, 0);
        dap_assert(l_m != NULL, "Reopen shared after private modification");
        uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
        dap_assert(l_ptr[0] == 0x55, "Original data unchanged after private COW");
        dap_mmap_close(l_m);
    }

    s_remove_file(l_path);
    dap_pass_msg("private mapping");
}

// ============================================================================
// Test: large mapping (multi-MB)
// ============================================================================

static void test_large_mapping(void)
{
    dap_print_module_name("mmap large mapping");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_large.dat");
    s_remove_file(l_path);

    size_t l_size = 16 * 1024 * 1024; // 16 MB
    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, l_size);
    dap_assert(l_m != NULL, "Create 16MB mapping");
    dap_assert(dap_mmap_get_size(l_m) >= l_size, "Size >= 16MB");

    uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);

    // Write patterns at various offsets
    l_ptr[0] = 0x01;
    l_ptr[l_size / 2] = 0x02;
    l_ptr[l_size - 1] = 0x03;

    dap_assert(l_ptr[0] == 0x01, "Start byte correct");
    dap_assert(l_ptr[l_size / 2] == 0x02, "Middle byte correct");
    dap_assert(l_ptr[l_size - 1] == 0x03, "End byte correct");

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("large mapping");
}

// ============================================================================
// Test: stress resize with data integrity
// ============================================================================

static void test_resize_stress(void)
{
    dap_print_module_name("mmap resize stress");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_resize_stress.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 4096);
    dap_assert(l_m != NULL, "Create mapping");

    // Write initial pattern
    uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
    for (int i = 0; i < 64; i++)
        l_ptr[i] = (uint8_t)(i ^ 0xA5);

    // Grow in steps, verify data integrity each time
    size_t l_cur = 4096;
    for (int step = 0; step < 12; step++) {
        l_cur *= 2;
        int l_rc = dap_mmap_resize(l_m, l_cur);
        dap_assert(l_rc == 0, "Resize step succeeds");
        dap_assert(dap_mmap_get_size(l_m) >= l_cur, "Size correct");

        l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
        for (int i = 0; i < 64; i++)
            dap_assert(l_ptr[i] == (uint8_t)(i ^ 0xA5), "Data integrity across resize");

        // Write at new end
        l_ptr[l_cur - 1] = 0xFF;
        dap_assert(l_ptr[l_cur - 1] == 0xFF, "Write at new end ok");
    }

    // Final size should be 4096 * 2^12 = 16 MB
    dap_assert(dap_mmap_get_size(l_m) >= 16 * 1024 * 1024, "Final size >= 16MB");

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("resize stress");
}

// ============================================================================
// Test: two independent mappings of the same file (shared visibility)
// ============================================================================

static void test_two_mappings_shared(void)
{
    dap_print_module_name("mmap two shared mappings");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_two_shared.dat");
    s_remove_file(l_path);

    dap_mmap_t *l_m1 = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 8192);
    dap_assert(l_m1 != NULL, "First mapping created");

    dap_mmap_t *l_m2 = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_SHARED, 0);
    dap_assert(l_m2 != NULL, "Second mapping created");

    uint8_t *l_ptr1 = (uint8_t *)dap_mmap_get_ptr(l_m1);
    uint8_t *l_ptr2 = (uint8_t *)dap_mmap_get_ptr(l_m2);

    // Write through first mapping
    l_ptr1[0] = 0xDE;
    l_ptr1[1] = 0xAD;

    // Should be visible through second mapping (shared)
    dap_assert(l_ptr2[0] == 0xDE, "Shared: write through m1 visible in m2 [0]");
    dap_assert(l_ptr2[1] == 0xAD, "Shared: write through m1 visible in m2 [1]");

    // Write through second mapping
    l_ptr2[100] = 0xBE;

    // Should be visible through first
    dap_assert(l_ptr1[100] == 0xBE, "Shared: write through m2 visible in m1");

    dap_mmap_close(l_m2);
    dap_mmap_close(l_m1);
    s_remove_file(l_path);
    dap_pass_msg("two shared mappings");
}

// ============================================================================
// Test: page-aligned sizes
// ============================================================================

static void test_page_alignment(void)
{
    dap_print_module_name("mmap page alignment");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_page_align.dat");
    s_remove_file(l_path);

    // Request non-aligned size — should be rounded up
    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 5000);
    dap_assert(l_m != NULL, "Create with non-aligned size");

    size_t l_size = dap_mmap_get_size(l_m);
    long l_page_size = sysconf(_SC_PAGESIZE);
    if (l_page_size <= 0) l_page_size = 4096;

    dap_assert(l_size >= 5000, "Size >= requested");
    dap_assert(l_size % (size_t)l_page_size == 0, "Size is page-aligned");

    // Resize to non-aligned
    int l_rc = dap_mmap_resize(l_m, 10001);
    dap_assert(l_rc == 0, "Resize to non-aligned succeeds");
    l_size = dap_mmap_get_size(l_m);
    dap_assert(l_size >= 10001, "New size >= 10001");
    dap_assert(l_size % (size_t)l_page_size == 0, "New size is page-aligned");

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("page alignment");
}

// ============================================================================
// Test: initial_size = 0 for existing file
// ============================================================================

static void test_open_existing_size_zero(void)
{
    dap_print_module_name("mmap open existing with initial_size=0");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_size_zero.dat");
    s_remove_file(l_path);

    // Create 64K file
    {
        dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, 65536);
        dap_assert(l_m != NULL, "Create 64K file");
        uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
        l_ptr[0] = 0x77;
        dap_mmap_sync(l_m, DAP_MMAP_SYNC_SYNC);
        dap_mmap_close(l_m);
    }

    // Reopen with initial_size=0 → should use existing file size
    {
        dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_READ | DAP_MMAP_SHARED, 0);
        dap_assert(l_m != NULL, "Reopen with size 0");
        dap_assert(dap_mmap_get_size(l_m) >= 65536, "Size from existing file >= 64K");
        uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
        dap_assert(l_ptr[0] == 0x77, "Data preserved");
        dap_mmap_close(l_m);
    }

    s_remove_file(l_path);
    dap_pass_msg("open existing with initial_size=0");
}

// ============================================================================
// Test: fill entire mapped region (boundary access)
// ============================================================================

static void test_fill_boundary(void)
{
    dap_print_module_name("mmap fill boundary");

    char l_path[256];
    s_make_path(l_path, sizeof(l_path), "test_fill.dat");
    s_remove_file(l_path);

    size_t l_size = 4096;
    dap_mmap_t *l_m = dap_mmap_open(l_path, DAP_MMAP_RDWR | DAP_MMAP_CREATE | DAP_MMAP_SHARED, l_size);
    dap_assert(l_m != NULL, "Create mapping");

    uint8_t *l_ptr = (uint8_t *)dap_mmap_get_ptr(l_m);
    size_t l_actual = dap_mmap_get_size(l_m);

    // Fill entire region with pattern
    for (size_t i = 0; i < l_actual; i++)
        l_ptr[i] = (uint8_t)(i & 0xFF);

    // Verify
    for (size_t i = 0; i < l_actual; i++)
        dap_assert(l_ptr[i] == (uint8_t)(i & 0xFF), "Byte at boundary correct");

    dap_mmap_close(l_m);
    s_remove_file(l_path);
    dap_pass_msg("fill boundary");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    dap_set_appname("test_dap_mmap");
    dap_log_level_set(L_WARNING);

    printf("\n============================================================\n");
    printf("DAP Memory-Mapped File I/O Unit Tests\n");
    printf("============================================================\n\n");

    s_ensure_test_dir();

    // Lifecycle
    test_open_close_basic();
    test_null_safety();
    test_open_nonexistent();

    // Read/Write
    test_read_write();
    test_persistence();
    test_fill_boundary();

    // Resize
    test_resize_grow();
    test_resize_shrink();
    test_resize_noop();
    test_resize_zero();
    test_resize_multiple();
    test_resize_stress();

    // Sync
    test_sync();
    test_sync_range();

    // Advise
    test_advise();
    test_advise_range();

    // Mapping modes
    test_private_mapping();
    test_large_mapping();

    // Multi-mapping
    test_two_mappings_shared();

    // Edge cases
    test_page_alignment();
    test_open_existing_size_zero();

    printf("\n============================================================\n");
    printf("All dap_mmap tests PASSED\n");
    printf("============================================================\n\n");

    return 0;
}
