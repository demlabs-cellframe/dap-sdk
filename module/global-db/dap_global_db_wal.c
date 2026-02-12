/*
 * DAP Global DB Write-Ahead Log (WAL)
 *
 * Implementation of WAL for crash recovery and durability.
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_hash.h"
#include "dap_global_db_wal.h"

#define LOG_TAG "dap_global_db_wal"

// ============================================================================
// Internal types
// ============================================================================

struct dap_global_db_wal {
    int fd;                     // File descriptor
    char *path;                 // WAL file path
    uint64_t sequence;          // Current sequence number
    size_t write_offset;        // Current write position
    size_t unflushed_bytes;     // Bytes written since last sync
    bool needs_recovery;        // True if WAL has uncommitted data
};

// ============================================================================
// CRC32 (simple implementation)
// ============================================================================

static uint32_t s_crc32_table[256];
static bool s_crc32_initialized = false;

static void s_crc32_init(void)
{
    if (s_crc32_initialized)
        return;
    
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        }
        s_crc32_table[i] = c;
    }
    s_crc32_initialized = true;
}

static uint32_t s_crc32(const void *a_data, size_t a_len)
{
    s_crc32_init();
    const uint8_t *p = (const uint8_t *)a_data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < a_len; i++) {
        crc = s_crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// ============================================================================
// Internal helpers
// ============================================================================

static int s_write_header(dap_global_db_wal_t *a_wal)
{
    dap_global_db_wal_header_t l_header = {
        .magic = DAP_GLOBAL_DB_WAL_MAGIC,
        .version = DAP_GLOBAL_DB_WAL_VERSION,
        .sequence = a_wal->sequence
    };
    
    if (lseek(a_wal->fd, 0, SEEK_SET) < 0)
        return -1;
    
    if (write(a_wal->fd, &l_header, sizeof(l_header)) != sizeof(l_header))
        return -1;
    
    return 0;
}

static int s_read_header(dap_global_db_wal_t *a_wal)
{
    dap_global_db_wal_header_t l_header;
    
    if (lseek(a_wal->fd, 0, SEEK_SET) < 0)
        return -1;
    
    ssize_t l_read = read(a_wal->fd, &l_header, sizeof(l_header));
    if (l_read == 0) {
        // Empty file, initialize
        a_wal->sequence = 0;
        a_wal->write_offset = sizeof(dap_global_db_wal_header_t);
        return s_write_header(a_wal);
    }
    
    if (l_read != sizeof(l_header)) {
        log_it(L_ERROR, "Failed to read WAL header from %s", a_wal->path);
        return -1;
    }
    
    if (l_header.magic != DAP_GLOBAL_DB_WAL_MAGIC) {
        log_it(L_ERROR, "Invalid WAL magic in %s", a_wal->path);
        return -1;
    }
    
    if (l_header.version != DAP_GLOBAL_DB_WAL_VERSION) {
        log_it(L_ERROR, "Unsupported WAL version %u in %s", l_header.version, a_wal->path);
        return -1;
    }
    
    a_wal->sequence = l_header.sequence;
    
    // Find write offset (end of file)
    off_t l_end = lseek(a_wal->fd, 0, SEEK_END);
    if (l_end < 0)
        return -1;
    
    a_wal->write_offset = (size_t)l_end;
    
    // Check if there's uncommitted data
    a_wal->needs_recovery = (a_wal->write_offset > sizeof(dap_global_db_wal_header_t));
    
    return 0;
}

static int s_write_record(dap_global_db_wal_t *a_wal, uint8_t a_op,
                          const void *a_data, size_t a_data_len)
{
    size_t l_record_size = sizeof(dap_global_db_wal_record_t) + a_data_len;
    byte_t *l_buf = DAP_NEW_Z_SIZE(byte_t, l_record_size);
    if (!l_buf)
        return -1;
    
    dap_global_db_wal_record_t *l_rec = (dap_global_db_wal_record_t *)l_buf;
    l_rec->length = l_record_size;
    l_rec->op = a_op;
    
    if (a_data_len && a_data)
        memcpy(l_rec->data, a_data, a_data_len);
    
    // Calculate CRC over op + data
    l_rec->crc32 = s_crc32(&l_rec->op, 1 + a_data_len);
    
    // Write at current offset
    if (lseek(a_wal->fd, a_wal->write_offset, SEEK_SET) < 0) {
        DAP_DELETE(l_buf);
        return -1;
    }
    
    ssize_t l_written = write(a_wal->fd, l_buf, l_record_size);
    DAP_DELETE(l_buf);
    
    if (l_written != (ssize_t)l_record_size) {
        log_it(L_ERROR, "Failed to write WAL record: %s", strerror(errno));
        return -1;
    }
    
    a_wal->write_offset += l_record_size;
    a_wal->unflushed_bytes += l_record_size;
    
    // Auto-sync if threshold reached
    if (a_wal->unflushed_bytes >= DAP_GLOBAL_DB_WAL_SYNC_BYTES) {
        fsync(a_wal->fd);
        a_wal->unflushed_bytes = 0;
    }
    
    return 0;
}

// ============================================================================
// Public API - Lifecycle
// ============================================================================

dap_global_db_wal_t *dap_global_db_wal_open(const char *a_wal_path)
{
    dap_return_val_if_fail(a_wal_path, NULL);
    
    dap_global_db_wal_t *l_wal = DAP_NEW_Z(dap_global_db_wal_t);
    if (!l_wal)
        return NULL;
    
    l_wal->path = dap_strdup(a_wal_path);
    l_wal->fd = open(a_wal_path, O_RDWR | O_CREAT, 0644);
    
    if (l_wal->fd < 0) {
        log_it(L_ERROR, "Failed to open WAL %s: %s", a_wal_path, strerror(errno));
        DAP_DEL_MULTY(l_wal->path, l_wal);
        return NULL;
    }
    
    if (s_read_header(l_wal) < 0) {
        close(l_wal->fd);
        DAP_DEL_MULTY(l_wal->path, l_wal);
        return NULL;
    }
    
    log_it(L_DEBUG, "WAL opened: %s (seq=%lu, recovery=%s)", 
           a_wal_path, l_wal->sequence, l_wal->needs_recovery ? "yes" : "no");
    
    return l_wal;
}

void dap_global_db_wal_close(dap_global_db_wal_t *a_wal)
{
    if (!a_wal)
        return;
    
    if (a_wal->fd >= 0) {
        fsync(a_wal->fd);
        close(a_wal->fd);
    }
    
    DAP_DEL_MULTY(a_wal->path, a_wal);
}

int dap_global_db_wal_recover(dap_global_db_wal_t *a_wal,
                               dap_global_db_wal_replay_cb_t a_replay_cb,
                               void *a_arg)
{
    dap_return_val_if_fail(a_wal, -1);
    
    if (!a_wal->needs_recovery) {
        log_it(L_DEBUG, "WAL %s: no recovery needed", a_wal->path);
        return 0;
    }
    
    log_it(L_INFO, "WAL %s: starting recovery", a_wal->path);
    
    // Position after header
    if (lseek(a_wal->fd, sizeof(dap_global_db_wal_header_t), SEEK_SET) < 0)
        return -1;
    
    int l_replayed = 0;
    bool l_committed = false;
    
    while (1) {
        // Read record header
        dap_global_db_wal_record_t l_rec_hdr;
        ssize_t l_read = read(a_wal->fd, &l_rec_hdr, sizeof(l_rec_hdr));
        
        if (l_read == 0)
            break;  // EOF
        
        if (l_read != sizeof(l_rec_hdr)) {
            log_it(L_WARNING, "WAL: incomplete record header, stopping recovery");
            break;
        }
        
        // Validate record length
        if (l_rec_hdr.length < sizeof(l_rec_hdr) || l_rec_hdr.length > 64 * 1024 * 1024) {
            log_it(L_WARNING, "WAL: invalid record length %u, stopping recovery", l_rec_hdr.length);
            break;
        }
        
        size_t l_data_len = l_rec_hdr.length - sizeof(l_rec_hdr);
        byte_t *l_data = NULL;
        
        if (l_data_len > 0) {
            l_data = DAP_NEW_SIZE(byte_t, l_data_len);
            if (!l_data)
                return -1;
            
            l_read = read(a_wal->fd, l_data, l_data_len);
            if (l_read != (ssize_t)l_data_len) {
                DAP_DELETE(l_data);
                log_it(L_WARNING, "WAL: incomplete record data, stopping recovery");
                break;
            }
        }
        
        // Verify CRC
        size_t l_crc_len = 1 + l_data_len;
        byte_t *l_crc_buf = DAP_NEW_SIZE(byte_t, l_crc_len);
        l_crc_buf[0] = l_rec_hdr.op;
        if (l_data_len)
            memcpy(l_crc_buf + 1, l_data, l_data_len);
        
        uint32_t l_crc = s_crc32(l_crc_buf, l_crc_len);
        DAP_DELETE(l_crc_buf);
        
        if (l_crc != l_rec_hdr.crc32) {
            DAP_DEL_Z(l_data);
            log_it(L_WARNING, "WAL: CRC mismatch, stopping recovery");
            break;
        }
        
        // Process record
        if (l_rec_hdr.op == DAP_GLOBAL_DB_WAL_OP_COMMIT) {
            l_committed = true;
            DAP_DEL_Z(l_data);
            continue;
        }
        
        if (l_rec_hdr.op == DAP_GLOBAL_DB_WAL_OP_CHECKPOINT) {
            // Checkpoint means everything before is applied
            l_committed = false;
            l_replayed = 0;
            DAP_DEL_Z(l_data);
            continue;
        }
        
        // Replay callback
        if (a_replay_cb) {
            int l_rc = a_replay_cb(l_rec_hdr.op, l_data, l_data_len, a_arg);
            if (l_rc < 0) {
                DAP_DEL_Z(l_data);
                log_it(L_ERROR, "WAL: replay callback failed");
                return -1;
            }
        }
        
        l_replayed++;
        DAP_DEL_Z(l_data);
    }
    
    log_it(L_INFO, "WAL %s: recovery complete, replayed %d records (committed=%s)",
           a_wal->path, l_replayed, l_committed ? "yes" : "no");
    
    a_wal->needs_recovery = false;
    
    return l_replayed;
}

// ============================================================================
// Public API - Operations
// ============================================================================

int dap_global_db_wal_write(dap_global_db_wal_t *a_wal,
                             dap_global_db_hash_t a_hash,
                             const char *a_key,
                             const void *a_value, size_t a_value_len,
                             const void *a_sign, size_t a_sign_len,
                             uint8_t a_flags)
{
    dap_return_val_if_fail(a_wal, -1);
    
    // Pack data: [hash][flags][key_len][key][value_len][value][sign_len][sign]
    size_t l_key_len = a_key ? strlen(a_key) + 1 : 0;
    size_t l_total = sizeof(a_hash) + 1 + 4 + l_key_len + 4 + a_value_len + 4 + a_sign_len;
    
    byte_t *l_buf = DAP_NEW_Z_SIZE(byte_t, l_total);
    if (!l_buf)
        return -1;
    
    byte_t *p = l_buf;
    
    // Hash
    memcpy(p, &a_hash, sizeof(a_hash));
    p += sizeof(a_hash);
    
    // Flags
    *p++ = a_flags;
    
    // Key
    uint32_t l_key_len32 = l_key_len;
    memcpy(p, &l_key_len32, 4);
    p += 4;
    if (l_key_len) {
        memcpy(p, a_key, l_key_len);
        p += l_key_len;
    }
    
    // Value
    uint32_t l_value_len32 = a_value_len;
    memcpy(p, &l_value_len32, 4);
    p += 4;
    if (a_value_len && a_value) {
        memcpy(p, a_value, a_value_len);
        p += a_value_len;
    }
    
    // Signature
    uint32_t l_sign_len32 = a_sign_len;
    memcpy(p, &l_sign_len32, 4);
    p += 4;
    if (a_sign_len && a_sign) {
        memcpy(p, a_sign, a_sign_len);
    }
    
    int l_rc = s_write_record(a_wal, DAP_GLOBAL_DB_WAL_OP_INSERT, l_buf, l_total);
    DAP_DELETE(l_buf);
    
    return l_rc;
}

int dap_global_db_wal_delete(dap_global_db_wal_t *a_wal, dap_global_db_hash_t a_hash)
{
    dap_return_val_if_fail(a_wal, -1);
    return s_write_record(a_wal, DAP_GLOBAL_DB_WAL_OP_DELETE, &a_hash, sizeof(a_hash));
}

int dap_global_db_wal_commit(dap_global_db_wal_t *a_wal)
{
    dap_return_val_if_fail(a_wal, -1);
    
    int l_rc = s_write_record(a_wal, DAP_GLOBAL_DB_WAL_OP_COMMIT, NULL, 0);
    if (l_rc == 0) {
        a_wal->sequence++;
        fsync(a_wal->fd);
        a_wal->unflushed_bytes = 0;
    }
    
    return l_rc;
}

int dap_global_db_wal_checkpoint(dap_global_db_wal_t *a_wal)
{
    dap_return_val_if_fail(a_wal, -1);
    
    // Update header with current sequence
    if (s_write_header(a_wal) < 0)
        return -1;
    
    // Truncate file after header
    if (ftruncate(a_wal->fd, sizeof(dap_global_db_wal_header_t)) < 0) {
        log_it(L_ERROR, "Failed to truncate WAL: %s", strerror(errno));
        return -1;
    }
    
    a_wal->write_offset = sizeof(dap_global_db_wal_header_t);
    a_wal->unflushed_bytes = 0;
    
    fsync(a_wal->fd);
    
    log_it(L_DEBUG, "WAL checkpoint: %s (seq=%lu)", a_wal->path, a_wal->sequence);
    
    return 0;
}

int dap_global_db_wal_sync(dap_global_db_wal_t *a_wal)
{
    dap_return_val_if_fail(a_wal, -1);
    
    if (fsync(a_wal->fd) < 0)
        return -1;
    
    a_wal->unflushed_bytes = 0;
    return 0;
}

size_t dap_global_db_wal_size(dap_global_db_wal_t *a_wal)
{
    return a_wal ? a_wal->write_offset : 0;
}
