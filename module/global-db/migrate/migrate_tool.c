/*
 * DAP Global DB Migration CLI Tool
 *
 * Usage:
 *   dap_gdb_migrate_tool <source> <destination> [options]
 *
 * Options:
 *   -v, --verbose     Verbose output
 *   -s, --skip-errors Continue on errors
 *   --no-verify       Skip verification
 *   --mdbx            Force MDBX format
 *   --sql             Force SQL format
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "dap_common.h"
#include "dap_global_db_migrate.h"

static void s_print_usage(const char *prog)
{
    fprintf(stderr, "DAP Global DB Migration Tool\n\n");
    fprintf(stderr, "Usage: %s <source> <destination> [options]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -v, --verbose      Verbose output\n");
    fprintf(stderr, "  -s, --skip-errors  Continue on errors\n");
    fprintf(stderr, "  --no-verify        Skip verification after migration\n");
    fprintf(stderr, "  --mdbx             Force MDBX file format\n");
    fprintf(stderr, "  --sql              Force SQL dump format\n");
    fprintf(stderr, "  -h, --help         Show this help\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s /old/global_db.mdbx /new/storage/\n", prog);
    fprintf(stderr, "  %s /backup/dump.sql /new/storage/ --sql -v\n", prog);
}

static void s_progress_cb(const char *group, size_t current, size_t total, void *arg)
{
    (void)total;
    (void)arg;
    printf("\r  [%s] %zu records...", group, current);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    // Initialize DAP
    dap_log_level_set(L_WARNING);
    
    static struct option long_opts[] = {
        {"verbose",     no_argument, 0, 'v'},
        {"skip-errors", no_argument, 0, 's'},
        {"no-verify",   no_argument, 0, 'n'},
        {"mdbx",        no_argument, 0, 'm'},
        {"sql",         no_argument, 0, 'q'},
        {"help",        no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    dap_global_db_migrate_options_t opts = DAP_GLOBAL_DB_MIGRATE_OPTIONS_DEFAULT;
    int force_format = 0;  // 0=auto, 1=mdbx, 2=sql
    
    int opt;
    while ((opt = getopt_long(argc, argv, "vsnmqh", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'v':
                opts.verbose = true;
                dap_log_level_set(L_DEBUG);
                break;
            case 's':
                opts.skip_errors = true;
                break;
            case 'n':
                opts.verify_after = false;
                break;
            case 'm':
                force_format = 1;
                break;
            case 'q':
                force_format = 2;
                break;
            case 'h':
                s_print_usage(argv[0]);
                return 0;
            default:
                s_print_usage(argv[0]);
                return 1;
        }
    }
    
    if (optind + 2 > argc) {
        fprintf(stderr, "Error: source and destination paths required\n\n");
        s_print_usage(argv[0]);
        return 1;
    }
    
    const char *source = argv[optind];
    const char *dest = argv[optind + 1];
    
    if (opts.verbose) {
        opts.progress_cb = s_progress_cb;
        printf("Migrating: %s -> %s\n", source, dest);
    }
    
    dap_global_db_migrate_result_t result;
    
    switch (force_format) {
        case 1:
            result = dap_global_db_migrate_mdbx(source, dest, &opts);
            break;
        case 2:
            result = dap_global_db_migrate_sql(source, dest, &opts);
            break;
        default:
            result = dap_global_db_migrate_auto(source, dest, &opts);
            break;
    }
    
    if (opts.verbose)
        printf("\n");
    
    if (result.status != DAP_MIGRATE_OK) {
        fprintf(stderr, "Migration failed: %s\n", 
                result.error_message ? result.error_message : dap_global_db_migrate_strerror(result.status));
        dap_global_db_migrate_result_free(&result);
        return 1;
    }
    
    printf("Migration complete:\n");
    printf("  Groups:   %zu\n", result.groups_migrated);
    printf("  Records:  %zu\n", result.records_migrated);
    printf("  Failed:   %zu\n", result.records_failed);
    printf("  Bytes:    %zu\n", result.bytes_migrated);
    
    dap_global_db_migrate_result_free(&result);
    return 0;
}
