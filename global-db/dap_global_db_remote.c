#include <string.h>
#include <stdlib.h>

#include "dap_global_db.h"
#include "dap_global_db_remote.h"
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_string.h"
#include "dap_time.h"
#include "dap_hash.h"

#define LOG_TAG "dap_global_db_remote"

static dap_list_t *s_sync_group_items = NULL;
static dap_list_t *s_sync_group_extra_items = NULL;

static void s_clear_sync_grp(void *a_elm);
static int s_db_add_sync_group(dap_list_t **a_grp_list, dap_sync_group_item_t *a_item);

void dap_global_db_sync_init()
{

}

/**
 * @brief Deinitialize a database.
 * @note You should call this function at the end.
 * @return (none)
 */
void dap_global_db_sync_deinit()
{
    dap_list_free_full(s_sync_group_items, s_clear_sync_grp);
    dap_list_free_full(s_sync_group_extra_items, s_clear_sync_grp);
    s_sync_group_extra_items = s_sync_group_items = NULL;
}

/**
 * @brief Adds a group name for synchronization.
 * @param a_net_name a net name string, for all net a_net_name=null
 * @param a_group_prefix a prefix of the group name
 * @param a_callback a callback function
 * @param a_arg a pointer to an argument
 * @return (none)
 */
void dap_global_db_add_sync_group(const char *a_net_name, const char *a_group_mask, dap_store_obj_callback_notify_t a_callback, void *a_arg)
{
    dap_sync_group_item_t *l_item = DAP_NEW_Z(dap_sync_group_item_t);
    if (!l_item) {
        log_it(L_ERROR, "Memory allocation error in dap_global_db_add_sync_group");
        return;
    }
    l_item->net_name = dap_strdup(a_net_name);
    l_item->group_mask = dap_strdup_printf("%s.*", a_group_mask);
    dap_global_db_add_notify_group_mask(dap_global_db_context_get_default()->instance, l_item->group_mask, a_callback, a_arg);
    s_db_add_sync_group(&s_sync_group_items, l_item);
}

/**
 * @brief Adds a group name for synchronization with especially node addresses.
 * @param a_net_name a net name string, for all net a_net_name=null
 * @param a_group_mask a group mask string
 * @param a_callback a callabck function
 * @param a_arg a pointer to an argument
 * @return (none)
 */
void dap_global_db_add_sync_extra_group(const char *a_net_name, const char *a_group_mask, dap_store_obj_callback_notify_t a_callback, void *a_arg)
{
    dap_sync_group_item_t* l_item = DAP_NEW_Z(dap_sync_group_item_t);
    if (!l_item) {
        log_it(L_ERROR, "Memory allocation error in dap_global_db_add_sync_extra_group");
        return;
    }
    l_item->net_name = dap_strdup(a_net_name);
    l_item->group_mask = dap_strdup(a_group_mask);
    s_db_add_sync_group(&s_sync_group_extra_items, l_item);
    dap_global_db_add_notify_group_mask(dap_global_db_context_get_default()->instance, a_group_mask, a_callback, a_arg);
}

/**
 * @brief Gets a list of a group mask for s_sync_group_items.
 * @param a_net_name a net name string, for all net a_net_name=null
 * @return Returns a pointer to a list of a group mask.
 */
dap_list_t* dap_chain_db_get_sync_groups(const char *a_net_name)
{
    if(!a_net_name)
        return dap_list_copy(s_sync_group_items);

    dap_list_t *l_list_out = NULL;
    dap_list_t *l_list_group = s_sync_group_items;
    while(l_list_group) {
        if(!dap_strcmp(a_net_name, ((dap_sync_group_item_t*) l_list_group->data)->net_name)) {
            l_list_out = dap_list_append(l_list_out, l_list_group->data);
        }
        l_list_group = dap_list_next(l_list_group);
    }
    return l_list_out;
}

/**
 * @brief Gets a list of a group mask for s_sync_group_items.
 * @param a_net_name a net name string, for all net a_net_name=null
 * @return Returns a pointer to a list of a group mask.
 */
dap_list_t* dap_chain_db_get_sync_extra_groups(const char *a_net_name)
{
    if(!a_net_name)
        return dap_list_copy(s_sync_group_extra_items);

    dap_list_t *l_list_out = NULL;
    dap_list_t *l_list_group = s_sync_group_extra_items;
    while(l_list_group) {
        if(!dap_strcmp(a_net_name, ((dap_sync_group_item_t*) l_list_group->data)->net_name)) {
            l_list_out = dap_list_append(l_list_out, l_list_group->data);
        }
        l_list_group = dap_list_next(l_list_group);
    }
    return l_list_out;
}

// New notificators & sync mechanics (cluster architecture)

int dap_global_db_add_notify_group_mask(dap_global_db_instance_t *a_dbi, const char *a_group_mask, dap_store_obj_callback_notify_t a_callback, void *a_arg)
{
    if (!a_callback) {
        log_it(L_ERROR, "Trying to set NULL callback for mask %s", a_group_mask);
        return -1;
    }
    for (dap_list_t *it = a_dbi->notify_groups; it; it = it->next) {
        dap_global_db_notify_item_t *l_item = it->data;
        if (!dap_strcmp(l_item->group_mask, a_group_mask)) {
            log_it(L_WARNING, "Group mask '%s' already present in the list, ignore it", a_group_mask);
            return -2;
        }
    }
    dap_global_db_notify_item_t *l_item_new = DAP_NEW_Z(dap_global_db_notify_item_t);
    if (!l_item_new) {
        log_it(L_ERROR, "Memory allocation error in dap_global_db_add_notify_group_mask");
        return -1;
    }
    l_item_new->group_mask = dap_strdup(a_group_mask);
    l_item_new->callback_notify = a_callback;
    l_item_new->callback_arg = a_arg;
    a_dbi->notify_groups = dap_list_append(a_dbi->notify_groups, l_item_new);
    return 0;
}

dap_list_t *dap_global_db_get_notify_groups(dap_global_db_instance_t *a_dbi)
{
    return a_dbi->notify_groups;
}

/**
 * @brief s_clear_sync_grp
 * @param a_elm
 */
static void s_clear_sync_grp(void *a_elm)
{
    dap_sync_group_item_t *l_item = (dap_sync_group_item_t *)a_elm;
    DAP_DELETE(l_item->group_mask);
    DAP_DELETE(l_item);
}

/**
 * @brief s_db_add_sync_group
 * @param a_grp_list
 * @param a_item
 * @return
 */
static int s_db_add_sync_group(dap_list_t **a_grp_list, dap_sync_group_item_t *a_item)
{
    for (dap_list_t *it = *a_grp_list; it; it = it->next) {
        dap_sync_group_item_t *l_item = (dap_sync_group_item_t *)it->data;
        if (!dap_strcmp(l_item->group_mask, a_item->group_mask) && !dap_strcmp(l_item->net_name, a_item->net_name)) {
            log_it(L_WARNING, "Group mask '%s' already present in the list, ignore it", a_item->group_mask);
            s_clear_sync_grp(a_item);
            return -1;
        }
    }
    *a_grp_list = dap_list_append(*a_grp_list, a_item);
    return 0;
}

/**
 * @brief A function for a thread for reading a log list
 *
 * @param arg a pointer to the log list structure
 * @return Returns NULL.
 */
static void *s_list_thread_proc(void *arg)
{
    dap_db_log_list_t *l_dap_db_log_list = (dap_db_log_list_t *)arg;
    uint32_t l_time_store_lim_hours = l_dap_db_log_list->db_context->instance->store_time_limit;
    uint64_t l_limit_time = l_time_store_lim_hours ? dap_nanotime_now() - dap_nanotime_from_sec(l_time_store_lim_hours * 3600) : 0;
    for (dap_list_t *l_groups = l_dap_db_log_list->groups; l_groups; l_groups = dap_list_next(l_groups)) {
        dap_db_log_list_group_t *l_group_cur = (dap_db_log_list_group_t *)l_groups->data;
        char *l_del_group_name_replace = NULL;
        char l_obj_type;
        if (!dap_fnmatch("*.del", l_group_cur->name, 0)) {
            l_obj_type = DAP_DB$K_OPTYPE_DEL;
            size_t l_del_name_len = strlen(l_group_cur->name) - 4; //strlen(".del");
            l_del_group_name_replace = DAP_NEW_SIZE(char, l_del_name_len + 1);
            memcpy(l_del_group_name_replace, l_group_cur->name, l_del_name_len);
            l_del_group_name_replace[l_del_name_len] = '\0';
        } else {
            l_obj_type = DAP_DB$K_OPTYPE_ADD;
        }
        uint64_t l_item_start = l_group_cur->last_id_synced + 1;
        dap_nanotime_t l_time_allowed = dap_nanotime_now() + dap_nanotime_from_sec(3600 * 24); // to be sure the timestamp is invalid
        while (l_group_cur->count && l_dap_db_log_list->is_process) { // Number of records to be synchronized
            size_t l_item_count = 0;//min(64, l_group_cur->count);
            size_t l_objs_total_size = 0;
            dap_store_obj_t *l_objs = dap_global_db_get_all_raw_sync(l_group_cur->name, 0, &l_item_count);
            if (!l_dap_db_log_list->is_process) {
                dap_store_obj_free(l_objs, l_item_count);
                return NULL;
            }
            // go to next group
            if (!l_objs)
                break;
            // set new start pos = lastitem pos + 1
            l_item_start = l_objs[l_item_count - 1].id + 1;
            // TODO
            UNUSED(l_item_start);
            l_group_cur->count = 0; //-= l_item_count;
            dap_list_t *l_list = NULL;
            for (size_t i = 0; i < l_item_count; i++) {
                dap_store_obj_t *l_obj_cur = l_objs + i;
                if (!l_obj_cur)
                    continue;
                l_obj_cur->type = l_obj_type;
                if (l_obj_cur->timestamp >> 32 == 0 ||
                        l_obj_cur->timestamp > l_time_allowed ||
                        l_obj_cur->group == NULL) {
                    dap_global_db_driver_delete(l_obj_cur, 1);
                    continue;       // the object is broken
                }
                if (l_obj_type == DAP_DB$K_OPTYPE_DEL) {
                    if (l_limit_time && l_obj_cur->timestamp < l_limit_time) {
                        dap_global_db_driver_delete(l_obj_cur, 1);
                        continue;
                    }
                    DAP_DELETE((char *)l_obj_cur->group);
                    l_obj_cur->group = dap_strdup(l_del_group_name_replace);
                }
                dap_db_log_list_obj_t *l_list_obj = DAP_NEW_Z(dap_db_log_list_obj_t);
                if (!l_list_obj) {
                    log_it(L_ERROR, "Memory allocation error in s_list_thread_proc");
                    dap_store_obj_free(l_objs, l_item_count);
                    return NULL;
                }
                uint64_t l_cur_id = l_obj_cur->id;
                l_obj_cur->id = 0;
                dap_global_db_pkt_t *l_pkt = dap_global_db_pkt_serialize(l_obj_cur);
                dap_hash_fast(l_pkt->data, l_pkt->data_size, &l_list_obj->hash);
                dap_global_db_pkt_change_id(l_pkt, l_cur_id);
                l_list_obj->pkt = l_pkt;
                l_list = dap_list_append(l_list, l_list_obj);
                l_objs_total_size += dap_db_log_list_obj_get_size(l_list_obj);
            }
            dap_store_obj_free(l_objs, l_item_count);
            pthread_mutex_lock(&l_dap_db_log_list->list_mutex);
            // add l_list to items_list
            l_dap_db_log_list->items_list = dap_list_concat(l_dap_db_log_list->items_list, l_list);
            l_dap_db_log_list->size += l_objs_total_size;
            while (l_dap_db_log_list->size > DAP_DB_LOG_LIST_MAX_SIZE && l_dap_db_log_list->is_process)
                pthread_cond_wait(&l_dap_db_log_list->cond, &l_dap_db_log_list->list_mutex);
            pthread_mutex_unlock(&l_dap_db_log_list->list_mutex);
        }
        DAP_DEL_Z(l_del_group_name_replace);
        if (!l_dap_db_log_list->is_process)
            return NULL;
    }

    pthread_mutex_lock(&l_dap_db_log_list->list_mutex);
    l_dap_db_log_list->is_process = false;
    pthread_mutex_unlock(&l_dap_db_log_list->list_mutex);
    return NULL;
}

/**
 * @brief Starts a thread that readding a log list
 * @note instead dap_db_log_get_list()
 *
 * @param l_net net for sync
 * @param a_addr a pointer to the structure
 * @param a_flags flags
 * @return Returns a pointer to the log list structure if successful, otherwise NULL pointer.
 */
dap_db_log_list_t *dap_db_log_list_start(const char *a_net_name, uint64_t a_node_addr, int a_flags)
{
#ifdef GDB_SYNC_ALWAYS_FROM_ZERO
    a_flags |= F_DB_LOG_SYNC_FROM_ZERO;
#endif

    debug_if(g_dap_global_db_debug_more, L_DEBUG, "Start loading db list_write...");
    dap_db_log_list_t *l_dap_db_log_list = DAP_NEW_Z(dap_db_log_list_t);
    if (!l_dap_db_log_list) {
            log_it(L_ERROR, "Memory allocation error in dap_db_log_list_start");
            return NULL;
        }
    l_dap_db_log_list->db_context = dap_global_db_context_get_default();

    // Add groups for the selected network only
    dap_list_t *l_groups_masks = dap_chain_db_get_sync_groups(a_net_name);
    if (a_flags & F_DB_LOG_ADD_EXTRA_GROUPS) {
        dap_list_t *l_extra_groups_masks = dap_chain_db_get_sync_extra_groups(a_net_name);
        l_groups_masks = dap_list_concat(l_groups_masks, l_extra_groups_masks);
    }
    dap_list_t *l_groups_names = NULL;
    for (dap_list_t *l_cur_mask = l_groups_masks; l_cur_mask; l_cur_mask = dap_list_next(l_cur_mask)) {
        char *l_cur_mask_data = ((dap_sync_group_item_t *)l_cur_mask->data)->group_mask;
        l_groups_names = dap_list_concat(l_groups_names, dap_global_db_driver_get_groups_by_mask(l_cur_mask_data));
    }
    dap_list_free(l_groups_masks);

    // Check for banned/whitelisted groups
    dap_global_db_instance_t *l_dbi = l_dap_db_log_list->db_context->instance;
    if (l_dbi->whitelist || l_dbi->blacklist) {
        dap_list_t *l_used_list = l_dbi->whitelist ? l_dbi->whitelist : l_dbi->blacklist;
        for (dap_list_t *l_group = l_groups_names; l_group; ) {
            bool l_found = false;
            for (dap_list_t *it = l_used_list; it; it = it->next) {
                if (!dap_fnmatch(it->data, l_group->data, FNM_NOESCAPE)) {
                    l_found = true;
                    break;
                }
            }
            dap_list_t *l_tmp = l_group->next;
            if (l_used_list == l_dbi->whitelist ? !l_found : l_found)
                l_groups_names = dap_list_delete_link(l_groups_names, l_group);
            l_group = l_tmp;
        }
    }

    l_dap_db_log_list->groups = l_groups_names; // repalce name of group with group item
    for (dap_list_t *l_group = l_dap_db_log_list->groups; l_group; l_group = dap_list_next(l_group)) {
        dap_db_log_list_group_t *l_sync_group = DAP_NEW_Z(dap_db_log_list_group_t);
        if (!l_sync_group) {
            log_it(L_ERROR, "Memory allocation error in dap_db_log_list_start");
            DAP_DEL_Z(l_dap_db_log_list);
            return NULL;
        }
        l_sync_group->name = (char *)l_group->data;
        if (a_flags & F_DB_LOG_SYNC_FROM_ZERO)
            l_sync_group->last_id_synced = 0;
        else
            l_sync_group->last_id_synced = dap_db_get_last_id_remote(a_node_addr, l_sync_group->name);
        l_sync_group->count = dap_global_db_driver_count(l_sync_group->name, l_sync_group->last_id_synced + 1);
        l_dap_db_log_list->items_number += l_sync_group->count;
        l_group->data = (void *)l_sync_group;
    }
    l_dap_db_log_list->items_rest = l_dap_db_log_list->items_number;
    if (!l_dap_db_log_list->items_number) {
        DAP_DELETE(l_dap_db_log_list);
        return NULL;
    }
    l_dap_db_log_list->is_process = true;
    pthread_mutex_init(&l_dap_db_log_list->list_mutex, NULL);
    pthread_cond_init(&l_dap_db_log_list->cond, NULL);
    pthread_create(&l_dap_db_log_list->thread, NULL, s_list_thread_proc, l_dap_db_log_list);
    return l_dap_db_log_list;
}

/**
 * @brief Gets a number of objects from a log list.
 *
 * @param a_db_log_list a pointer to the log list structure
 * @return Returns the number if successful, otherwise 0.
 */
size_t dap_db_log_list_get_count(dap_db_log_list_t *a_db_log_list)
{
    if(!a_db_log_list)
        return 0;
    size_t l_items_number;
    pthread_mutex_lock(&a_db_log_list->list_mutex);
    l_items_number = a_db_log_list->items_number;
    pthread_mutex_unlock(&a_db_log_list->list_mutex);
    return l_items_number;
}

/**
 * @brief Gets a number of rest objects from a log list.
 *
 * @param a_db_log_list a pointer to the log list structure
 * @return Returns the number if successful, otherwise 0.
 */
size_t dap_db_log_list_get_count_rest(dap_db_log_list_t *a_db_log_list)
{
    if(!a_db_log_list)
        return 0;
    size_t l_items_rest;
    pthread_mutex_lock(&a_db_log_list->list_mutex);
    l_items_rest = a_db_log_list->items_rest;
    pthread_mutex_unlock(&a_db_log_list->list_mutex);
    return l_items_rest;
}

/**
 * @brief Gets an object from a list.
 *
 * @param a_db_log_list a pointer to the log list
 * @return Returns a pointer to the object.
 */
dap_db_log_list_obj_t *dap_db_log_list_get(dap_db_log_list_t *a_db_log_list)
{
    if (!a_db_log_list)
        return NULL;
    pthread_mutex_lock(&a_db_log_list->list_mutex);
    int l_is_process = a_db_log_list->is_process;
    // check first item
    dap_list_t *l_list = a_db_log_list->items_list;
    dap_db_log_list_obj_t *l_ret = NULL;
    if (l_list) {
        a_db_log_list->items_list = dap_list_remove_link(a_db_log_list->items_list, l_list);
        a_db_log_list->items_rest--;
        l_ret = l_list->data;
        size_t l_old_size = a_db_log_list->size;
        a_db_log_list->size -= dap_db_log_list_obj_get_size(l_ret);
        DAP_DELETE(l_list);
        if (l_old_size > DAP_DB_LOG_LIST_MAX_SIZE &&
                a_db_log_list->size <= DAP_DB_LOG_LIST_MAX_SIZE)
            pthread_cond_signal(&a_db_log_list->cond);
    }
    pthread_mutex_unlock(&a_db_log_list->list_mutex);
    //log_it(L_DEBUG, "get item n=%d", a_db_log_list->items_number - a_db_log_list->items_rest);
    return l_ret ? l_ret : DAP_INT_TO_POINTER(l_is_process);
}

/**
 * @brief Deallocates memory of a list item
 *
 * @param a_item a pointer to the list item
 * @returns (none)
 */
static void s_dap_db_log_list_delete_item(void *a_item)
{
    dap_db_log_list_obj_t *l_list_item = (dap_db_log_list_obj_t *)a_item;
    DAP_DELETE(l_list_item->pkt);
    DAP_DELETE(l_list_item);
}

/**
 * @brief Deallocates memory of a log list.
 *
 * @param a_db_log_list a pointer to the log list structure
 * @returns (none)
 */
void dap_db_log_list_delete(dap_db_log_list_t *a_db_log_list)
{
    if(!a_db_log_list)
        return;
    // stop thread if it has created
    if(a_db_log_list->thread) {
        pthread_mutex_lock(&a_db_log_list->list_mutex);
        a_db_log_list->is_process = false;
        pthread_cond_signal(&a_db_log_list->cond);
        pthread_mutex_unlock(&a_db_log_list->list_mutex);
        pthread_join(a_db_log_list->thread, NULL);
    }
    dap_list_free_full(a_db_log_list->groups, NULL);
    dap_list_free_full(a_db_log_list->items_list, (dap_callback_destroyed_t)s_dap_db_log_list_delete_item);
    pthread_mutex_destroy(&a_db_log_list->list_mutex);
    DAP_DELETE(a_db_log_list);
}

/**
 * @brief Sets last id of a remote node.
 *
 * @param a_node_addr a node adress
 * @param a_id id
 * @param a_group a group name string
 * @return Returns true if successful, otherwise false.
 */
bool dap_db_set_last_id_remote(uint64_t a_node_addr, uint64_t a_id, char *a_group)
{
char	l_key[DAP_GLOBAL_DB_KEY_MAX];

    snprintf(l_key, sizeof(l_key) - 1, "%"DAP_UINT64_FORMAT_U"%s", a_node_addr, a_group);
    return dap_global_db_set(GROUP_LOCAL_NODE_LAST_ID,l_key, &a_id, sizeof(uint64_t), false, NULL, NULL ) == 0;
}

/**
 * @brief Gets last id of a remote node.
 *
 * @param a_node_addr a node adress
 * @param a_group a group name string
 * @return Returns id if successful, otherwise 0.
 */
uint64_t dap_db_get_last_id_remote(uint64_t a_node_addr, char *a_group)
{
    char *l_node_addr_str = dap_strdup_printf("%"DAP_UINT64_FORMAT_U"%s", a_node_addr, a_group);
    size_t l_id_len = 0;
    byte_t *l_id = dap_global_db_get_sync(GROUP_LOCAL_NODE_LAST_ID, l_node_addr_str, &l_id_len, NULL, NULL);
    uint64_t l_ret_id = 0;
    if (l_id) {
        if (l_id_len == sizeof(uint64_t))
            memcpy(&l_ret_id, l_id, l_id_len);
        DAP_DELETE(l_id);
    }
    DAP_DELETE(l_node_addr_str);
    return l_ret_id;
}


/**
 * @brief Gets a size of an object.
 *
 * @param store_obj a pointer to the object
 * @return Returns the size.
 */
static size_t dap_db_get_size_pdap_store_obj_t(pdap_store_obj_t store_obj)
{
    size_t size = sizeof(uint32_t) + 2 * sizeof(uint16_t) +
            3 * sizeof(uint64_t) + dap_strlen(store_obj->group) +
            dap_strlen(store_obj->key) + store_obj->value_len;
    return size;
}

/**
 * @brief Multiples data into a_old_pkt structure from a_new_pkt structure.
 * @param a_old_pkt a pointer to the old object
 * @param a_new_pkt a pointer to the new object
 * @return Returns a pointer to the multiple object
 */
dap_global_db_pkt_t *dap_global_db_pkt_pack(dap_global_db_pkt_t *a_old_pkt, dap_global_db_pkt_t *a_new_pkt)
{
    if (!a_new_pkt)
        return a_old_pkt;
    if (a_old_pkt)
        a_old_pkt = (dap_global_db_pkt_t *)DAP_REALLOC(a_old_pkt,
                                                       a_old_pkt->data_size + a_new_pkt->data_size + sizeof(dap_global_db_pkt_t));
    else
        a_old_pkt = DAP_NEW_Z_SIZE(dap_global_db_pkt_t, a_new_pkt->data_size + sizeof(dap_global_db_pkt_t));
    memcpy(a_old_pkt->data + a_old_pkt->data_size, a_new_pkt->data, a_new_pkt->data_size);
    a_old_pkt->data_size += a_new_pkt->data_size;
    a_old_pkt->obj_count++;
    return a_old_pkt;
}

/**
 * @brief Changes id in a packed structure.
 *
 * @param a_pkt a pointer to the packed structure
 * @param a_id id
 * @return (none)
 */
void dap_global_db_pkt_change_id(dap_global_db_pkt_t *a_pkt, uint64_t a_id)
{
    uint16_t l_gr_len = *(uint16_t*)(a_pkt->data + sizeof(uint32_t));
    size_t l_id_offset = sizeof(uint32_t) + sizeof(uint16_t) + l_gr_len;
    *(uint64_t *)(a_pkt->data + l_id_offset) = a_id;
}

/**
 * @brief Serializes an object into a packed structure.
 * @param a_store_obj a pointer to the object to be serialized
 * @return Returns a pointer to the packed sructure if successful, otherwise NULL.
 */
dap_global_db_pkt_t *dap_global_db_pkt_serialize(dap_store_obj_t *a_store_obj)
{
int len;
unsigned char *pdata;

    if (!a_store_obj)
        return NULL;

    uint32_t l_data_size_out = dap_db_get_size_pdap_store_obj_t(a_store_obj);
    dap_global_db_pkt_t *l_pkt = DAP_NEW_SIZE(dap_global_db_pkt_t, l_data_size_out + sizeof(dap_global_db_pkt_t));

    /* Fill packet header */
    l_pkt->data_size = l_data_size_out;
    l_pkt->obj_count = 1;
    l_pkt->timestamp = 0;

    /* Put serialized data into the payload part of the packet */
    pdata = l_pkt->data;
    *( (uint32_t *) pdata) =  a_store_obj->type;                pdata += sizeof(uint32_t);

    len = dap_strlen(a_store_obj->group);
    *( (uint16_t *) pdata) = (uint16_t) len;                    pdata += sizeof(uint16_t);
    memcpy(pdata, a_store_obj->group, len);                     pdata += len;

    *( (uint64_t *) pdata) = a_store_obj->id;                   pdata += sizeof(uint64_t);
    *( (uint64_t *) pdata) = a_store_obj->timestamp;            pdata += sizeof(uint64_t);

    len = dap_strlen(a_store_obj->key);
    *( (uint16_t *) pdata) = (uint16_t) len;                    pdata += sizeof(uint16_t);
    memcpy(pdata, a_store_obj->key, len);                       pdata += len;

    *( (uint64_t *) pdata) = a_store_obj->value_len;            pdata += sizeof(uint64_t);
    memcpy(pdata, a_store_obj->value, a_store_obj->value_len);  pdata += a_store_obj->value_len;

    assert( (uint32_t)(pdata - l_pkt->data) == l_data_size_out);
    return l_pkt;
}

/**
 * @brief Deserializes some objects from a packed structure into an array of objects.
 * @param pkt a pointer to the serialized packed structure
 * @param store_obj_count[out] a number of deserialized objects in the array
 * @return Returns a pointer to the first object in the array, if successful; otherwise NULL.
 */
dap_store_obj_t *dap_global_db_pkt_deserialize(const dap_global_db_pkt_t *a_pkt, size_t *a_store_obj_count)
{
uint32_t l_count, l_cur_count;
uint64_t l_size;
unsigned char *pdata, *pdata_end;
dap_store_obj_t *l_store_obj_arr, *l_obj;

    if(!a_pkt || a_pkt->data_size < sizeof(dap_global_db_pkt_t))
        return NULL;

    l_count = a_pkt->obj_count;
    l_size = l_count <= UINT16_MAX ? l_count * sizeof(struct dap_store_obj) : 0;

    l_store_obj_arr = DAP_NEW_Z_SIZE(dap_store_obj_t, l_size);

    if (!l_store_obj_arr || !l_size)
    {
        log_it(L_ERROR, "Invalid size: can't allocate %"DAP_UINT64_FORMAT_U" bytes, errno=%d", l_size, errno);
        DAP_DEL_Z(l_store_obj_arr);
        return NULL;
    }

    pdata = (unsigned char *) a_pkt->data;                                  /* Set <pdata> to begin of payload */
    pdata_end = pdata + a_pkt->data_size;                                   /* Set <pdata_end> to end of payload area
                                                                              will be used to prevent out-of-buffer case */
    l_obj = l_store_obj_arr;

    for ( l_cur_count = l_count ; l_cur_count; l_cur_count--, l_obj++ )
    {
        if ( (pdata  + sizeof (uint32_t)) > pdata_end )                     /* Check for buffer boundaries */
            {log_it(L_ERROR, "Broken GDB element: can't read 'type' field"); break;}
        l_obj->type = *((uint32_t *) pdata);
        pdata += sizeof(uint32_t);


        if ( (pdata  + sizeof (uint16_t)) > pdata_end )
            {log_it(L_ERROR, "Broken GDB element: can't read 'group_length' field"); break;}
        l_obj->group_len = *((uint16_t *) pdata);
        pdata += sizeof(uint16_t);

        if ( !l_obj->group_len )
            {log_it(L_ERROR, "Broken GDB element: 'group_len' field is zero"); break;}


        if ( (pdata + l_obj->group_len) > pdata_end )
            {log_it(L_ERROR, "Broken GDB element: can't read 'group' field"); break;}
        l_obj->group = DAP_NEW_Z_SIZE(char, l_obj->group_len + 1);
        if (!l_obj->group) {
            log_it(L_ERROR, "Memory allocation error in dap_global_db_pkt_deserialize");
            DAP_DEL_Z(l_store_obj_arr);
            return NULL;
        }
        memcpy(l_obj->group, pdata, l_obj->group_len);
        pdata += l_obj->group_len;



        if ( (pdata + sizeof (uint64_t)) > pdata_end )
            {log_it(L_ERROR, "Broken GDB element: can't read 'id' field"); break;}
        l_obj->id = *((uint64_t *) pdata);
        pdata += sizeof(uint64_t);



        if ( (pdata + sizeof (uint64_t)) > pdata_end )
            {log_it(L_ERROR, "Broken GDB element: can't read 'timestamp' field");  break;}
        l_obj->timestamp = *((uint64_t *) pdata);
        pdata += sizeof(uint64_t);


        if ( (pdata + sizeof (uint16_t)) > pdata_end)
            {log_it(L_ERROR, "Broken GDB element: can't read 'key_length' field"); break;}
        l_obj->key_len = *((uint16_t *) pdata);
        pdata += sizeof(uint16_t);

        if ( !l_obj->key_len )
            {log_it(L_ERROR, "Broken GDB element: 'key_length' field is zero"); break;}

        if ((pdata + l_obj->key_len) > pdata_end)
            {log_it(L_ERROR, "Broken GDB element: 'key_length' field is out from allocated memory"); break;}

        l_obj->key_byte = DAP_NEW_SIZE(byte_t, l_obj->key_len + 1);
        if (!l_obj->key_byte) {
            log_it(L_ERROR, "Memory allocation error in dap_global_db_pkt_deserialize");
            DAP_DEL_Z(l_obj->group);
            DAP_DEL_Z(l_store_obj_arr);
            return NULL;
        }
        memcpy( l_obj->key_byte, pdata, l_obj->key_len);
        l_obj->key_byte[l_obj->key_len] = '\0';
        pdata += l_obj->key_len;


        if ( (pdata + sizeof (uint64_t)) > pdata_end )
            {log_it(L_ERROR, "Broken GDB element: can't read 'value_length' field"); break;}
        l_obj->value_len = *((uint64_t *) pdata);
        pdata += sizeof(uint64_t);

        if (l_obj->value_len) {
            if ( (pdata + l_obj->value_len) > pdata_end )
                {log_it(L_ERROR, "Broken GDB element: can't read 'value' field"); break;}
            l_obj->value = DAP_NEW_SIZE(uint8_t, l_obj->value_len);
            if (!l_obj->value) {
                log_it(L_ERROR, "Memory allocation error in dap_global_db_pkt_deserialize");
                DAP_DEL_Z(l_obj->key_byte);
                DAP_DEL_Z(l_obj->group);
                DAP_DEL_Z(l_store_obj_arr);
                return NULL;
            }
            memcpy(l_obj->value, pdata, l_obj->value_len);
            pdata += l_obj->value_len;
        }
    }

    assert(pdata <= pdata_end);

    // Return the number of completely filled dap_store_obj_t structures
    // because l_cur_count may be less than l_count due to too little memory
    if (a_store_obj_count)
        *a_store_obj_count = l_count;

    return l_store_obj_arr;
}

static int s_global_db_check_group_mask_check(dap_global_db_context_t *a_global_db_context, dap_store_obj_t *a_obj,
                                              dap_list_t *a_masks) {
    if (!a_global_db_context || !a_obj || !a_masks) {
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "The s_global_db_check_group_mask_check function cannot accept "
                                                      "NULL values.");
        return -1;
    }
    for (dap_list_t *i = a_masks; i; i = i->next) {
        dap_sync_group_item_t *l_item = (dap_sync_group_item_t*)i->data;
        if (!dap_fnmatch(l_item->group_mask, a_obj->group, 0)){
            debug_if(g_dap_global_db_debug_more, L_DEBUG, "Group %s match mask %s.", a_obj->group, l_item->group_mask);
            return 0;
        }
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "Group %s does not match mask %s.", a_obj->group, l_item->group_mask);
    }
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "Group %s does not match any of the masks.", a_obj->group);
    return -2;
}

int dap_global_db_remote_apply_obj_unsafe(dap_global_db_context_t *a_global_db_context, dap_store_obj_t *a_obj,
                                          dap_global_db_callback_results_raw_t a_callback, void *a_arg)
{
    // timestamp for exist obj
    dap_nanotime_t l_timestamp_cur = 0;
    // Record is pinned or not
    bool l_is_pinned_cur = false;
    if (s_global_db_check_group_mask_check(a_global_db_context, a_obj, s_sync_group_items) &&
        s_global_db_check_group_mask_check(a_global_db_context, a_obj, s_sync_group_extra_items)) {
        log_it(L_WARNING, "An entry in the group %s was rejected because the group name did not match any of the masks.", a_obj->group);
        DAP_DELETE(a_arg);
        return -4;
    }
    if (dap_global_db_driver_is(a_obj->group, a_obj->key)) {
        dap_store_obj_t *l_read_obj = dap_global_db_driver_read(a_obj->group, a_obj->key, NULL);
        if (l_read_obj) {
            l_timestamp_cur = l_read_obj->timestamp;
            l_is_pinned_cur = l_read_obj->flags & RECORD_PINNED;
            dap_store_obj_free_one(l_read_obj);
        }
    }
    // Do not overwrite pinned records
    if (l_is_pinned_cur) {
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Can't %s record from group %s key %s - current record is pinned",
                                a_obj->type != DAP_DB$K_OPTYPE_DEL ? "remove" : "rewrite", a_obj->group, a_obj->key);
        DAP_DELETE(a_arg);
        return -1;
    }
    // Deleted time
    dap_nanotime_t l_timestamp_del = dap_global_db_get_del_ts_unsafe(a_global_db_context, a_obj->group, a_obj->key);
    // Limit time
    uint32_t l_time_store_lim_hours = a_global_db_context->instance->store_time_limit;
    uint64_t l_limit_time = l_time_store_lim_hours ? dap_nanotime_now() - dap_nanotime_from_sec(l_time_store_lim_hours * 3600) : 0;
    //check whether to apply the received data into the database
    bool l_apply = false;
    // check the applied object newer that we have stored or erased
    if (a_obj->timestamp > (uint64_t)l_timestamp_del &&
            a_obj->timestamp > (uint64_t)l_timestamp_cur &&
            (a_obj->type != DAP_DB$K_OPTYPE_DEL || a_obj->timestamp > l_limit_time)) {
        l_apply = true;
    }
    if (g_dap_global_db_debug_more) {
        char l_ts_str[50];
        dap_time_to_str_rfc822(l_ts_str, sizeof(l_ts_str), dap_nanotime_to_sec(a_obj->timestamp));
        log_it(L_DEBUG, "Unpacked log history: type='%c' (0x%02hhX) group=\"%s\" key=\"%s\""
                " timestamp=\"%s\" value_len=%" DAP_UINT64_FORMAT_U,
                (char )a_obj->type, (char)a_obj->type, a_obj->group,
                a_obj->key, l_ts_str, a_obj->value_len);
    }
    if (!l_apply) {
        if (g_dap_global_db_debug_more) {
            if (a_obj->timestamp <= (uint64_t)l_timestamp_cur)
                log_it(L_WARNING, "New data not applied, because newly object exists");
            if (a_obj->timestamp <= (uint64_t)l_timestamp_del)
                log_it(L_WARNING, "New data not applied, because newly object is deleted");
            if ((a_obj->type == DAP_DB$K_OPTYPE_DEL && a_obj->timestamp <= l_limit_time))
                log_it(L_WARNING, "New data not applied, because object is too old");
        }
        DAP_DELETE(a_arg);
        return -2;
    }
    // save data to global_db
    if (dap_global_db_set_raw(a_obj, 1, a_callback, a_arg)) {
        DAP_DELETE(a_arg);
        log_it(L_ERROR, "Can't send save GlobalDB request");
        return -3;
    }
    return 0;
}

struct gdb_apply_args {
    dap_store_obj_t *obj;
    dap_global_db_callback_results_raw_t callback;
    void *cb_arg;
};

static void s_db_apply_obj(dap_global_db_context_t *a_global_db_context, void *a_arg)
{
    struct gdb_apply_args *l_args = a_arg;
    dap_global_db_remote_apply_obj_unsafe(a_global_db_context, l_args->obj, l_args->callback, l_args->cb_arg);
    dap_store_obj_free_one(l_args->obj);
    DAP_DELETE(l_args);
}

int dap_global_db_remote_apply_obj(dap_store_obj_t *a_obj, dap_global_db_callback_results_raw_t a_callback, void *a_arg)
{
    struct gdb_apply_args *l_args =  DAP_NEW_Z(struct gdb_apply_args);
    if (!l_args) {
        log_it(L_ERROR, "Memory allocation error in s_gdb_in_pkt_proc_callback");
        return -1;
    }
    l_args->obj = dap_store_obj_copy(a_obj, 1);
    l_args->callback = a_callback;
    l_args->cb_arg = a_arg;
    return dap_global_db_context_exec(s_db_apply_obj, l_args);
}
