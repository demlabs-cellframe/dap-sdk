/*
 * DAP Global DB Store Object Utilities
 * 
 * Functions for managing dap_global_db_store_obj_t structures:
 * allocation, copying, freeing.
 * 
 * Note: This header is included by dap_global_db.h after type definitions.
 * Do not include this header directly - include dap_global_db.h instead.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration - full type in dap_global_db.h
struct dap_global_db_store_obj;
typedef struct dap_global_db_store_obj dap_global_db_store_obj_t;

// ============================================================================
// Store object management
// ============================================================================

/**
 * @brief Copy array of store objects
 * @param a_store_obj Source objects
 * @param a_store_count Number of objects
 * @return Newly allocated copy (caller owns) or NULL
 */
dap_global_db_store_obj_t *dap_global_db_store_obj_copy(dap_global_db_store_obj_t *a_store_obj, size_t a_store_count);

/**
 * @brief Copy single store object with extra data
 * @param a_store_obj Source object
 * @param a_ext Extra data to append
 * @param a_ext_size Size of extra data
 * @return Newly allocated copy (caller owns) or NULL
 */
dap_global_db_store_obj_t *dap_global_db_store_obj_copy_ext(dap_global_db_store_obj_t *a_store_obj, void *a_ext, size_t a_ext_size);

/**
 * @brief Copy store objects into pre-allocated destination
 * @param a_store_objs_dest Destination array (must be allocated)
 * @param a_store_objs_src Source array
 * @param a_store_count Number of objects
 * @return a_store_objs_dest or NULL on error
 */
dap_global_db_store_obj_t *dap_global_db_store_objs_copy(dap_global_db_store_obj_t *a_store_objs_dest,
                                                const dap_global_db_store_obj_t *a_store_objs_src,
                                                size_t a_store_count);

/**
 * @brief Free array of store objects
 * @param a_store_obj Objects to free
 * @param a_store_count Number of objects
 */
void dap_global_db_store_obj_free(dap_global_db_store_obj_t *a_store_obj, size_t a_store_count);

/**
 * @brief Free single store object
 * @param a_store_obj Object to free
 */
DAP_STATIC_INLINE void dap_global_db_store_obj_free_one(dap_global_db_store_obj_t *a_store_obj)
{
    dap_global_db_store_obj_free(a_store_obj, 1);
}

#ifdef __cplusplus
}
#endif
