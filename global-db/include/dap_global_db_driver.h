/*
 * Authors:
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Kelvin Project https://github.com/kelvinblockchain
 * Copyright  (c) 2019
 * All rights reserved.

 This file is part of DAP (Deus Applications Prototypes) the open source project

 DAP (Deus Applicaions Prototypes) is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 DAP is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.

    MODIFICATION HISTORY:
        08-MAY-2022 RRL Added <ctx> field to the DB Driver interface table is called as <dap_db_driver_callbacks_t>;
                        a set of limits - see DAP$K/SZ constant definitions;
                        added lengths for the character fields.
 */

#pragma once

#include "dap_time.h"
#include "dap_list.h"
#include "dap_sign.h"

#define DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX   128UL                               /* A maximum size of group name */
#define DAP_GLOBAL_DB_GROUPS_COUNT_MAX      1024UL                              /* A maximum number of groups */
#define DAP_GLOBAL_DB_KEY_SIZE_MAX          512UL                               /* A limit for the key's length in DB */

#define DAP_GLOBAL_DB_COND_READ_COUNT_DEFAULT 256UL                             /* Default count of records to return with conditional read */

enum dap_global_db_record_flags {
    DAP_GLOBAL_DB_RECORD_COMMON = 0,
    DAP_GLOBAL_DB_RECORD_PINNED = 1,
    DAP_GLOBAL_DB_RECORD_NEW = 2
};

typedef enum dap_global_db_optype {
    DAP_GLOBAL_DB_OPTYPE_ADD  = 0x61,    /* 'a', */                             /* Operation type INSERT/ADD */
    DAP_GLOBAL_DB_OPTYPE_DEL  = 0x64,    /* 'd', */                             /* Operation type DELETE */
} dap_global_db_optype_t;

typedef struct dap_global_db_driver_hash {
    dap_nanotime_t bets;
    uint64_t becrc;
} DAP_ALIGN_PACKED dap_global_db_driver_hash_t;

typedef struct dap_store_obj {
    char *group;                    // Name of database table analogue (key-value DB have no 'table' defined)
    const char *key;                // Unique database key
    byte_t *value;                  // Database value corresponsing with database key
    size_t value_len;               // Length of database value
    uint8_t flags;                  // Now it is only 'pinned' flag, pointed to record can be removed only by author
    dap_sign_t *sign;               // Crypto sign for authentication and security checks
    dap_nanotime_t timestamp;       // Timestamp of record creation, in nanoseconds since EPOCH
    uint64_t crc;                   // Integrity control
    dap_global_db_optype_t type;    // Operation type - for event notifiers
    byte_t ext[];                   // For extra data transfer between sync callbacks
} dap_store_obj_t;

DAP_STATIC_INLINE dap_global_db_driver_hash_t dap_global_db_driver_hash_get(dap_store_obj_t *a_obj)
{
    dap_global_db_driver_hash_t l_ret = { .bets = htobe64(a_obj->timestamp), .becrc = htobe64(a_obj->crc) };
    return l_ret;
}

DAP_STATIC_INLINE int dap_global_db_driver_hash_compare(dap_global_db_driver_hash_t a_hash1, dap_global_db_driver_hash_t a_hash2)
{
    return memcmp(&a_hash1, &a_hash2, sizeof(dap_global_db_driver_hash_t));
}

DAP_STATIC_INLINE int dap_store_obj_driver_hash_compare(dap_store_obj_t *a_obj1, dap_store_obj_t *a_obj2)
{
    if (!a_obj1)
        return a_obj2 ? -1 : 0;
    if (!a_obj2)
        return 1;
    dap_global_db_driver_hash_t l_hash1 = dap_global_db_driver_hash_get(a_obj1),
                                l_hash2 = dap_global_db_driver_hash_get(a_obj2);
    return dap_global_db_driver_hash_compare(l_hash1, l_hash2);
}

DAP_STATIC_INLINE char *dap_global_db_driver_hash_print(dap_global_db_driver_hash_t a_hash)
{
    char *l_ret = DAP_NEW_Z_SIZE(char, sizeof(a_hash) * 2 + 3);
    if (!l_ret)
        return NULL;
    strcpy(l_ret, "0x");
    dap_bin2hex(l_ret + 2, &a_hash, sizeof(a_hash));
    return l_ret;
}

extern const dap_global_db_driver_hash_t c_dap_global_db_driver_hash_start;

DAP_STATIC_INLINE bool dap_global_db_driver_hash_is_blank(dap_global_db_driver_hash_t a_blank_candidate)
{
    return !memcmp(&a_blank_candidate, &c_dap_global_db_driver_hash_start, sizeof(dap_global_db_driver_hash_t));
}

typedef int (*dap_db_driver_write_callback_t)(dap_store_obj_t *a_store_obj);
typedef dap_store_obj_t* (*dap_db_driver_read_callback_t)(const char *a_group, const char *a_key, size_t *a_count_out);
typedef dap_store_obj_t* (*dap_db_driver_read_cond_callback_t)(const char *a_group, dap_global_db_driver_hash_t a_hash_from, size_t *a_count);
typedef dap_store_obj_t* (*dap_db_driver_read_last_callback_t)(const char *a_group);
typedef size_t (*dap_db_driver_read_count_callback_t)(const char *a_group, dap_global_db_driver_hash_t a_hash_from);
typedef dap_list_t* (*dap_db_driver_get_groups_callback_t)(const char *a_mask);
typedef bool (*dap_db_driver_is_obj_callback_t)(const char *a_group, const char *a_key);
typedef bool (*dap_db_driver_is_hash_callback_t)(const char *a_group, dap_global_db_driver_hash_t a_hash);
typedef dap_store_obj_t * (*dap_db_driver_get_by_hash_callback_t)(const char *a_group, dap_global_db_driver_hash_t a_hash);
typedef int (*dap_db_driver_callback_t)(void);

typedef struct dap_db_driver_callbacks {
    dap_db_driver_write_callback_t      apply_store_obj;                    /* Performs an DB's action like: INSERT/DELETE/UPDATE for the given
                                                                              'store object' */
    dap_db_driver_read_callback_t       read_store_obj;                     /* Retreive 'store object' from DB */
    dap_db_driver_read_last_callback_t  read_last_store_obj;
    dap_db_driver_read_cond_callback_t  read_cond_store_obj;
    dap_db_driver_read_count_callback_t read_count_store;

    dap_db_driver_get_groups_callback_t get_groups_by_mask;                 /* Return a list of tables/groups has been matched to pattern */

    dap_db_driver_is_obj_callback_t     is_obj;                             /* Check for existence of a record in the table/group for
                                                                              a given <key> */
    dap_db_driver_is_hash_callback_t    is_hash;                            /* Check for existence of a record in the table/group for
                                                                              a given driver hash */
    dap_db_driver_get_by_hash_callback_t get_by_hash;                       /* Retrieve a record from the table/group for a given driver hash */

    dap_db_driver_callback_t            transaction_start;                  /* Allocate DB context for consequtive operations */
    dap_db_driver_callback_t            transaction_end;                    /* Release DB context at end of DB consequtive operations */

    dap_db_driver_callback_t            deinit;
    dap_db_driver_callback_t            flush;
} dap_db_driver_callbacks_t;

int     dap_db_driver_init(const char *driver_name, const char *a_filename_db, int a_mode_async);
void    dap_db_driver_deinit(void);

uint32_t dap_store_obj_checksum(dap_store_obj_t *a_obj);
dap_store_obj_t *dap_store_obj_copy(dap_store_obj_t *a_store_obj, size_t a_store_count);
dap_store_obj_t *dap_store_obj_copy_ext(dap_store_obj_t *a_store_obj, void *a_ext, size_t a_ext_size);
dap_store_obj_t *dap_global_db_store_objs_copy(dap_store_obj_t *, const dap_store_obj_t *, size_t);
void    dap_store_obj_free(dap_store_obj_t *a_store_obj, size_t a_store_count);
DAP_STATIC_INLINE void dap_store_obj_free_one(dap_store_obj_t *a_store_obj) { return dap_store_obj_free(a_store_obj, 1); }
int     dap_db_driver_flush(void);

int dap_global_db_driver_apply(dap_store_obj_t *a_store_obj, size_t a_store_count);
int dap_global_db_driver_add(dap_store_obj_t *a_store_obj, size_t a_store_count);
int dap_global_db_driver_delete(dap_store_obj_t * a_store_obj, size_t a_store_count);
dap_store_obj_t *dap_global_db_driver_read_last(const char *a_group);
dap_store_obj_t *dap_global_db_driver_cond_read(const char *a_group, dap_global_db_driver_hash_t a_hash_from, size_t *a_count_out);
dap_store_obj_t *dap_global_db_driver_read(const char *a_group, const char *a_key, size_t *count_out);
dap_store_obj_t *dap_global_db_driver_get_by_hash(const char *a_group, dap_global_db_driver_hash_t a_hash);
bool dap_global_db_driver_is(const char *a_group, const char *a_key);
bool dap_global_db_driver_is_hash(const char *a_group, dap_global_db_driver_hash_t a_hash);
size_t dap_global_db_driver_count(const char *a_group, dap_global_db_driver_hash_t a_hash_from);
dap_list_t *dap_global_db_driver_get_groups_by_mask(const char *a_group_mask);
