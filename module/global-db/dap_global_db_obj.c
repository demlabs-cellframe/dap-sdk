/*
 * DAP Global DB Store Object Utilities
 * 
 * Functions for managing dap_global_db_store_obj_t structures.
 */

#include <string.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_sign.h"
#include "dap_global_db.h"

#define LOG_TAG "gdb_obj"

// ============================================================================
// Constants
// ============================================================================

const dap_global_db_hash_t c_dap_global_db_hash_blank = { 0 };

// ============================================================================
// Internal helpers
// ============================================================================

static inline void s_global_db_store_obj_copy_one(dap_global_db_store_obj_t *a_dst, const dap_global_db_store_obj_t *a_src)
{
    *a_dst = *a_src;
    
    a_dst->group = dap_strdup(a_src->group);
    if (a_src->group && !a_dst->group)
        return log_it(L_CRITICAL, "%s", c_error_memory_alloc);

    a_dst->key = dap_strdup(a_src->key);
    if (a_src->key && !a_dst->key) {
        DAP_DELETE(a_dst->group);
        return log_it(L_CRITICAL, "%s", c_error_memory_alloc);
    }
    
    if (a_src->sign) {
        size_t l_sign_size = dap_sign_get_size(a_src->sign);
        a_dst->sign = DAP_DUP_SIZE(a_src->sign, l_sign_size);
        if (!a_dst->sign) {
            DAP_DEL_MULTY(a_dst->group, a_dst->key);
            return log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        }
    }
    
    if (a_src->value) {
        if (!a_src->value_len) {
            log_it(L_WARNING, "Inconsistent global DB object copy requested");
        } else {
            a_dst->value = DAP_DUP_SIZE(a_src->value, a_src->value_len);
            if (!a_dst->value) {
                DAP_DEL_MULTY(a_dst->group, a_dst->key, a_dst->sign);
                return log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            }
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

dap_global_db_store_obj_t *dap_global_db_store_obj_copy(dap_global_db_store_obj_t *a_store_obj, size_t a_store_count)
{
    if (!a_store_obj || !a_store_count)
        return NULL;

    dap_global_db_store_obj_t *l_result = DAP_NEW_Z_COUNT(dap_global_db_store_obj_t, a_store_count);
    if (!l_result) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }

    for (size_t i = 0; i < a_store_count; i++) {
        s_global_db_store_obj_copy_one(&l_result[i], &a_store_obj[i]);
    }
    
    return l_result;
}

dap_global_db_store_obj_t *dap_global_db_store_obj_copy_ext(dap_global_db_store_obj_t *a_store_obj, void *a_ext, size_t a_ext_size)
{
    if (!a_store_obj)
        return NULL;
    
    dap_global_db_store_obj_t *l_result = DAP_NEW_Z_SIZE(dap_global_db_store_obj_t, sizeof(dap_global_db_store_obj_t) + a_ext_size);
    if (!l_result) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    
    s_global_db_store_obj_copy_one(l_result, a_store_obj);
    
    if (a_ext_size && a_ext)
        memcpy(l_result->ext, a_ext, a_ext_size);
    
    return l_result;
}

dap_global_db_store_obj_t *dap_global_db_store_objs_copy(dap_global_db_store_obj_t *a_store_objs_dest,
                                                const dap_global_db_store_obj_t *a_store_objs_src,
                                                size_t a_store_count)
{
    dap_return_val_if_pass(!a_store_objs_dest || !a_store_objs_src || !a_store_count, NULL);

    for (size_t i = 0; i < a_store_count; i++) {
        s_global_db_store_obj_copy_one(&a_store_objs_dest[i], &a_store_objs_src[i]);
    }
    
    return a_store_objs_dest;
}

void dap_global_db_store_obj_free(dap_global_db_store_obj_t *a_store_obj, size_t a_store_count)
{
    if (!a_store_obj || !a_store_count)
        return;

    for (size_t i = 0; i < a_store_count; i++) {
        DAP_DEL_MULTY(a_store_obj[i].group, a_store_obj[i].key, 
                      a_store_obj[i].value, a_store_obj[i].sign);
    }
    
    DAP_DELETE(a_store_obj);
}
