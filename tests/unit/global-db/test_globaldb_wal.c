/*
 * DAP GlobalDB Write-Ahead Log (WAL) commit/recovery unit tests.
 *
 * W67 (confcall breather-wave hostile scan): dap_global_db_wal_commit()
 * had zero callers anywhere in the tree, and dap_global_db_wal_recover()
 * tracked a `l_committed` flag but never actually gated replay on it —
 * every INSERT/DELETE record was replayed unconditionally regardless of
 * whether a COMMIT record ever followed it. This broke the durability/
 * atomicity guarantees the WAL header documents. Fixed by (1) wiring
 * dap_global_db_wal_commit() into the storage-layer write/erase call
 * sites (dap_global_db.c's s_storage_write/s_storage_erase), and (2)
 * making recovery buffer records since the last commit boundary and only
 * replay them once an actual COMMIT record is seen, discarding anything
 * still buffered at EOF (a torn, never-committed transaction).
 *
 * These tests exercise the WAL module directly (dap_global_db_wal_open/
 * write/delete/commit/recover), not through the storage layer, so the
 * commit-gating behavior itself is proven independent of dap_global_db.c's
 * own wiring.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef DAP_OS_WINDOWS
#include <unistd.h>
#endif
#include <sys/stat.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_test.h"
#include "dap_global_db.h"
#include "dap_global_db_wal.h"

#define TEST_DIR "/tmp/test_globaldb_wal"
#define TEST_WAL_PATH TEST_DIR "/test.wal"

static void s_cleanup_test_dir(void)
{
    dap_rm_rf(TEST_DIR);
}

static dap_global_db_hash_t s_make_hash(uint64_t a_seed)
{
    dap_global_db_hash_t h = { .bets = htobe64(a_seed), .becrc = htobe64(a_seed * 7 + 1) };
    return h;
}

/* Replay-callback context: counts calls and records the last op/data seen,
 * so each test can assert exactly what (if anything) got replayed. */
typedef struct s_replay_ctx {
    int calls;
    dap_global_db_wal_op_t last_op;
    char last_key_seen[128];
} s_replay_ctx_t;

static int s_replay_cb(dap_global_db_wal_op_t a_op, const byte_t *a_data, size_t a_len, void *a_arg)
{
    s_replay_ctx_t *l_ctx = a_arg;
    l_ctx->calls++;
    l_ctx->last_op = a_op;
    l_ctx->last_key_seen[0] = 0;
    if (a_op == DAP_GLOBAL_DB_WAL_OP_INSERT && a_data && a_len > sizeof(dap_global_db_hash_t) + 1 + 4) {
        /* Wire layout from dap_global_db_wal_write: [hash][flags][key_len][key]... */
        const byte_t *p = a_data + sizeof(dap_global_db_hash_t) + 1;
        uint32_t l_key_len; memcpy(&l_key_len, p, 4); p += 4;
        if (l_key_len > 0 && l_key_len < sizeof(l_ctx->last_key_seen)) {
            memcpy(l_ctx->last_key_seen, p, l_key_len);
        }
    }
    return 0;
}

static void test_wal_committed_insert_is_replayed(void)
{
    dap_test_msg("Testing WAL: a committed INSERT is replayed on recovery");

    s_cleanup_test_dir();
    dap_mkdir_with_parents(TEST_DIR);

    dap_global_db_hash_t l_hash = s_make_hash(1);
    {
        dap_global_db_wal_t *l_wal = dap_global_db_wal_open(TEST_WAL_PATH);
        dap_assert(l_wal != NULL, "WAL should open");

        int rc = dap_global_db_wal_write(l_wal, l_hash, "committed_key",
                                         "value", 6, NULL, 0, 0);
        dap_assert(rc == 0, "wal_write should succeed");

        rc = dap_global_db_wal_commit(l_wal);
        dap_assert(rc == 0, "wal_commit should succeed");

        dap_global_db_wal_close(l_wal);
    }

    /* Reopen — needs_recovery must be true (write_offset > header size). */
    {
        dap_global_db_wal_t *l_wal = dap_global_db_wal_open(TEST_WAL_PATH);
        dap_assert(l_wal != NULL, "WAL should reopen");

        s_replay_ctx_t l_ctx = {0};
        int l_replayed = dap_global_db_wal_recover(l_wal, s_replay_cb, &l_ctx);
        dap_assert(l_replayed == 1, "exactly 1 record should be replayed");
        dap_assert(l_ctx.calls == 1, "replay callback should fire exactly once");
        dap_assert(l_ctx.last_op == DAP_GLOBAL_DB_WAL_OP_INSERT, "replayed op should be INSERT");
        dap_assert(strcmp(l_ctx.last_key_seen, "committed_key") == 0,
                  "replayed record's key should match what was written");

        dap_global_db_wal_close(l_wal);
    }

    dap_pass_msg("Committed INSERT is replayed");
}

static void test_wal_uncommitted_insert_is_discarded(void)
{
    dap_test_msg("Testing WAL: an UNCOMMITTED INSERT (torn transaction) is discarded, not replayed");

    s_cleanup_test_dir();
    dap_mkdir_with_parents(TEST_DIR);

    dap_global_db_hash_t l_hash = s_make_hash(2);
    {
        dap_global_db_wal_t *l_wal = dap_global_db_wal_open(TEST_WAL_PATH);
        dap_assert(l_wal != NULL, "WAL should open");

        /* Write, but simulate a crash BEFORE commit — never call
         * dap_global_db_wal_commit(). This is exactly the "process died
         * mid-write" scenario the fix targets. */
        int rc = dap_global_db_wal_write(l_wal, l_hash, "torn_key",
                                         "value", 6, NULL, 0, 0);
        dap_assert(rc == 0, "wal_write should succeed");

        dap_global_db_wal_close(l_wal);
    }

    {
        dap_global_db_wal_t *l_wal = dap_global_db_wal_open(TEST_WAL_PATH);
        dap_assert(l_wal != NULL, "WAL should reopen");

        s_replay_ctx_t l_ctx = {0};
        int l_replayed = dap_global_db_wal_recover(l_wal, s_replay_cb, &l_ctx);
        dap_assert(l_replayed == 0, "0 records should be replayed — the write was never committed");
        dap_assert(l_ctx.calls == 0, "replay callback must NOT fire for an uncommitted record");

        dap_global_db_wal_close(l_wal);
    }

    dap_pass_msg("Uncommitted INSERT is correctly discarded (atomicity preserved)");
}

static void test_wal_mixed_committed_and_uncommitted(void)
{
    dap_test_msg("Testing WAL: committed records replay, a trailing uncommitted one does not");

    s_cleanup_test_dir();
    dap_mkdir_with_parents(TEST_DIR);

    {
        dap_global_db_wal_t *l_wal = dap_global_db_wal_open(TEST_WAL_PATH);
        dap_assert(l_wal != NULL, "WAL should open");

        /* Transaction 1: committed. */
        dap_global_db_hash_t l_h1 = s_make_hash(10);
        dap_assert(dap_global_db_wal_write(l_wal, l_h1, "key_one", "v1", 3, NULL, 0, 0) == 0,
                  "write key_one");
        dap_assert(dap_global_db_wal_commit(l_wal) == 0, "commit tx 1");

        /* Transaction 2: committed. */
        dap_global_db_hash_t l_h2 = s_make_hash(11);
        dap_assert(dap_global_db_wal_write(l_wal, l_h2, "key_two", "v2", 3, NULL, 0, 0) == 0,
                  "write key_two");
        dap_assert(dap_global_db_wal_commit(l_wal) == 0, "commit tx 2");

        /* Transaction 3: NOT committed (the crash point). */
        dap_global_db_hash_t l_h3 = s_make_hash(12);
        dap_assert(dap_global_db_wal_write(l_wal, l_h3, "key_three_torn", "v3", 3, NULL, 0, 0) == 0,
                  "write key_three_torn (never committed)");

        dap_global_db_wal_close(l_wal);
    }

    {
        dap_global_db_wal_t *l_wal = dap_global_db_wal_open(TEST_WAL_PATH);
        dap_assert(l_wal != NULL, "WAL should reopen");

        s_replay_ctx_t l_ctx = {0};
        int l_replayed = dap_global_db_wal_recover(l_wal, s_replay_cb, &l_ctx);
        dap_assert(l_replayed == 2, "exactly 2 committed records should be replayed");
        dap_assert(l_ctx.calls == 2, "replay callback should fire exactly twice");
        /* The callback is invoked once per committed record, in order — the
         * last call observed should be the SECOND committed key, never the
         * torn third one. */
        dap_assert(strcmp(l_ctx.last_key_seen, "key_two") == 0,
                  "last replayed record must be the second COMMITTED key, "
                  "not the torn third transaction");

        dap_global_db_wal_close(l_wal);
    }

    dap_pass_msg("Mixed committed/uncommitted: only committed records replay, in order");
}

static void test_wal_checkpoint_discards_pending_and_resets(void)
{
    dap_test_msg("Testing WAL: CHECKPOINT truncates the file and later recovery finds nothing");

    s_cleanup_test_dir();
    dap_mkdir_with_parents(TEST_DIR);

    {
        dap_global_db_wal_t *l_wal = dap_global_db_wal_open(TEST_WAL_PATH);
        dap_assert(l_wal != NULL, "WAL should open");

        dap_global_db_hash_t l_h = s_make_hash(20);
        dap_assert(dap_global_db_wal_write(l_wal, l_h, "pre_checkpoint_key", "v", 2, NULL, 0, 0) == 0,
                  "write before checkpoint");
        dap_assert(dap_global_db_wal_commit(l_wal) == 0, "commit before checkpoint");

        /* Checkpoint truncates the WAL back to just the header — this is
         * the "B-tree has been synced, WAL contents are now redundant"
         * signal used after a successful flush. */
        dap_assert(dap_global_db_wal_checkpoint(l_wal) == 0, "checkpoint should succeed");

        dap_global_db_wal_close(l_wal);
    }

    {
        dap_global_db_wal_t *l_wal = dap_global_db_wal_open(TEST_WAL_PATH);
        dap_assert(l_wal != NULL, "WAL should reopen");

        s_replay_ctx_t l_ctx = {0};
        int l_replayed = dap_global_db_wal_recover(l_wal, s_replay_cb, &l_ctx);
        dap_assert(l_replayed == 0, "checkpointed WAL should have nothing left to replay");
        dap_assert(l_ctx.calls == 0, "replay callback must not fire after a checkpoint");

        dap_global_db_wal_close(l_wal);
    }

    dap_pass_msg("Checkpoint correctly truncates and leaves nothing to recover");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    dap_log_level_set(L_WARNING);

    dap_test_msg("=== DAP GlobalDB WAL commit/recovery Unit Tests ===\n");

    test_wal_committed_insert_is_replayed();
    test_wal_uncommitted_insert_is_discarded();
    test_wal_mixed_committed_and_uncommitted();
    test_wal_checkpoint_discards_pending_and_resets();

    s_cleanup_test_dir();

    dap_test_msg("\n=== All WAL tests passed ===\n");
    return 0;
}
