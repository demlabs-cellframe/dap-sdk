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
        log_it(L_CRITICAL, "Memory allocation error");
        return;
    }
    l_item->net_name = dap_strdup(a_net_name);
    l_item->group_mask = dap_strdup_printf("%s.*", a_group_mask);
    dap_global_db_add_notify_group_mask(dap_global_db_context_get_default()->instance, l_item->group_mask, a_callback, a_arg, 0);
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
        log_it(L_CRITICAL, "Memory allocation error");
        return;
    }
    l_item->net_name = dap_strdup(a_net_name);
    l_item->group_mask = dap_strdup(a_group_mask);
    s_db_add_sync_group(&s_sync_group_extra_items, l_item);
    dap_global_db_add_notify_group_mask(dap_global_db_context_get_default()->instance, a_group_mask, a_callback, a_arg, 0);
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

    dap_list_t *l_list_out = NULL, *l_item;
    DL_FOREACH(s_sync_group_items, l_item) {
        if(!dap_strcmp(a_net_name, ((dap_sync_group_item_t*)l_item->data)->net_name)) {
            l_list_out = dap_list_append(l_list_out, l_item->data);
        }
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

    dap_list_t *l_list_out = NULL, *l_item;
    DL_FOREACH(s_sync_group_extra_items, l_item) {
        if(!dap_strcmp(a_net_name, ((dap_sync_group_item_t*)l_item->data)->net_name)) {
            l_list_out = dap_list_append(l_list_out, l_item->data);
        }
    }
    return l_list_out;
}

// New notificators & sync mechanics (cluster architecture)

int dap_global_db_add_notify_group_mask(dap_global_db_instance_t *a_dbi, const char *a_group_mask, dap_store_obj_callback_notify_t a_callback, void *a_arg, uint64_t a_ttl)
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
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_item_new->group_mask = dap_strdup(a_group_mask);
    l_item_new->callback_notify = a_callback;
    l_item_new->callback_arg = a_arg;
    l_item_new->ttl = a_ttl;
    a_dbi->notify_groups = dap_list_append(a_dbi->notify_groups, l_item_new);
    return 0;
}

dap_global_db_notify_item_t *dap_global_db_get_notify_group(dap_global_db_instance_t *a_dbi, const char *a_group_name)
{
    for (dap_list_t *it = a_dbi->notify_groups; it; it = it->next) {
        dap_global_db_notify_item_t *l_notify_item = it->data;
        if (!dap_fnmatch(l_notify_item->group_mask, a_group_name, 0))
            return l_notify_item;
    }
    return NULL;
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

static int s_cb_cmp_items(const void *a_list_elem, const void *a_item_elem) {
    dap_sync_group_item_t   *l_item1 = (dap_sync_group_item_t*)((dap_list_t*)a_list_elem)->data,
                            *l_item2 = (dap_sync_group_item_t*)((dap_list_t*)a_item_elem)->data;
    if (!l_item1 || !l_item2) {
        log_it(L_CRITICAL, "Invalid arg");
        return -1;
    }
    return dap_strcmp(l_item1->group_mask, l_item2->group_mask) || dap_strcmp(l_item1->net_name, l_item2->net_name);
}

/**
 * @brief s_db_add_sync_group
 * @param a_grp_list
 * @param a_item
 * @return
 */
static int s_db_add_sync_group(dap_list_t **a_grp_list, dap_sync_group_item_t *a_item)
{
    dap_list_t *l_item = dap_list_find(*a_grp_list, a_item, s_cb_cmp_items);
    if (l_item) {
        log_it(L_WARNING, "Group mask '%s' already present in the list, ignore it", a_item->group_mask);
        s_clear_sync_grp(a_item);
        return -1;
    }
    *a_grp_list = dap_list_append(*a_grp_list, a_item);
    return 0;
}

struct dap_store_obj_t_multi {
    dap_store_obj_t *objs;
    size_t objs_count;
};

static void s_log_list_delete_filtered(UNUSED_ARG dap_global_db_context_t *a_context, void *a_arg) {
    struct dap_store_obj_t_multi *l_store_objs = (struct dap_store_obj_t_multi*)a_arg;
    dap_global_db_driver_delete(l_store_objs->objs, l_store_objs->objs_count);
    dap_store_obj_free(l_store_objs->objs, l_store_objs->objs_count);
    DAP_DELETE(a_arg);
}

static void *s_list_thread_proc2(void *arg) {
    dap_db_log_list_t *l_dap_db_log_list = (dap_db_log_list_t*)arg;
    uint32_t l_time_store_lim_hours = l_dap_db_log_list->db_context->instance->store_time_limit;
    dap_list_t *l_group_elem;
    DL_FOREACH(l_dap_db_log_list->groups, l_group_elem) {
        dap_db_log_list_group_t *l_group = (dap_db_log_list_group_t*)l_group_elem->data;
        char l_obj_type = dap_fnmatch("*.del", l_group->name, 0) ? DAP_DB$K_OPTYPE_ADD : DAP_DB$K_OPTYPE_DEL;
        size_t l_item_count = 0;
        int l_placed_count = 0, l_unprocessed_count = 0;
        dap_store_obj_t *l_objs = dap_global_db_get_all_raw_sync(l_group->name, 0, &l_item_count);
        if (!l_objs)
            continue;
        if (l_item_count != l_group->count) {
            debug_if(g_dap_global_db_debug_more, L_WARNING, "Record count mismatch: actually extracted %zu != %zu previously count",
                     l_item_count, l_group->count);
            l_group->count = l_item_count;
        }
        debug_if(g_dap_global_db_debug_more, L_INFO, "Group %s: put %zu records into log_list", l_group->name, l_item_count);
        for (dap_store_obj_t *l_obj_cur = l_objs, *l_obj_last = l_objs + l_item_count - 1; l_obj_cur <= l_obj_last; ++l_obj_cur) {
            if (!l_obj_cur || !l_obj_cur->group || !(l_obj_cur->timestamp >> 32))
                continue;   // broken or derelict object
            l_obj_cur->type = l_obj_type;
            dap_nanotime_t l_limit_time = l_time_store_lim_hours
                    ? dap_nanotime_now() - dap_nanotime_from_sec(l_time_store_lim_hours * 3600)
                    : 0;
            bool group_HALed = strstr(l_obj_cur->group, ".orders")
                    || !dap_strncmp(l_obj_cur->group, "cdb.", 4)
                    || strstr(l_obj_cur->group, ".nodes.v2")
                    || ( strstr(l_obj_cur->group, "round.new") && !dap_strncmp(l_obj_cur->key, "round_current", 13) );

            switch (l_obj_type) {
            case DAP_DB$K_OPTYPE_ADD:
                if ( (l_obj_cur->timestamp < l_limit_time || (l_obj_cur->timestamp > dap_nanotime_now())) )
                {
                    if ( !group_HALed && !(l_obj_cur->flags & RECORD_PINNED) )
                        continue;
                    l_obj_cur->timestamp = dap_nanotime_now();
                }
                break;
            case DAP_DB$K_OPTYPE_DEL:
                if ( (l_obj_cur->timestamp < l_limit_time) || (l_obj_cur->timestamp > dap_nanotime_now()) )
                     continue;
                else {
                    char *l_dot = strrchr(l_obj_cur->group, '.');
                    *l_dot = '\0';
                    l_obj_cur->group_len = l_dot - l_obj_cur->group + 1;
                }
                break;
            default: break;
            }

            pthread_mutex_lock(&l_dap_db_log_list->list_mutex);
            if (l_dap_db_log_list->is_process) {
                while (l_dap_db_log_list->size > DAP_DB_LOG_LIST_MAX_SIZE)
                    pthread_cond_wait(&l_dap_db_log_list->cond, &l_dap_db_log_list->list_mutex);
            } else {
                pthread_mutex_unlock(&l_dap_db_log_list->list_mutex);
                while (l_obj_cur <= l_obj_last) {
                    dap_store_obj_clear_one(l_obj_cur);
                    ++l_obj_cur;
                    ++l_unprocessed_count;
                }
                debug_if(g_dap_global_db_debug_more, L_MSG, "Group \"%s\" not processed completely, %d / %zu records skipped",
                         l_group->name, l_unprocessed_count, l_item_count);
                l_group->count -= l_unprocessed_count;
                l_group_elem = dap_list_last(l_dap_db_log_list->groups);
                break;
            }
            dap_db_log_list_obj_t *l_list_obj = DAP_NEW_Z(dap_db_log_list_obj_t);
            if (!l_list_obj) {
                log_it(L_CRITICAL, "Memory allocation error");
                l_dap_db_log_list->is_process = false;
                pthread_mutex_unlock(&l_dap_db_log_list->list_mutex);
                dap_store_obj_free(l_objs, l_item_count);
                return NULL;
            }
            uint64_t l_cur_id = l_obj_cur->id;
            l_obj_cur->id = 0;
            dap_global_db_pkt_t *l_pkt = dap_global_db_pkt_serialize(l_obj_cur);
            dap_global_db_pkt_change_id(l_pkt, l_cur_id);
            dap_hash_fast(l_pkt->data, l_pkt->data_size, &l_list_obj->hash);
            l_list_obj->pkt = l_pkt;
            l_dap_db_log_list->items_list = dap_list_append(l_dap_db_log_list->items_list, l_list_obj);
            l_dap_db_log_list->size += dap_db_log_list_obj_get_size(l_list_obj);                
            pthread_mutex_unlock(&l_dap_db_log_list->list_mutex);
            dap_store_obj_clear_one(l_obj_cur);
            if (l_obj_cur < l_obj_last) {
                *l_obj_cur-- = *l_obj_last;
            }
            l_obj_last->group = NULL; l_obj_last->key = NULL; l_obj_last->value = NULL;
            --l_obj_last;
            ++l_placed_count;
            --l_group->count;
        }

        debug_if(g_dap_global_db_debug_more, L_MSG, "Placed %d / %zu records of group \"%s\" into log list, %zu deleted, %d skipped",
                 l_placed_count, l_item_count, l_group->name, l_group->count, l_unprocessed_count);

        if (l_group->count) {
            struct dap_store_obj_t_multi *l_arg = DAP_NEW_Z(struct dap_store_obj_t_multi);
            *l_arg = (struct dap_store_obj_t_multi){
                    .objs = l_objs,
                    .objs_count = l_group->count
            };
            dap_global_db_context_exec(s_log_list_delete_filtered, l_arg);
            l_group->count = 0;
        } else {
            DAP_DELETE(l_objs);
        }
    }
    l_dap_db_log_list->is_process = false;
    return NULL;
}

#if 0
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
        char l_del_group_name_replace[DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX];
        char l_obj_type;
        if (!dap_fnmatch("*.del", l_group_cur->name, 0)) {
            l_obj_type = DAP_DB$K_OPTYPE_DEL;
            size_t l_del_name_len = strlen(l_group_cur->name) - 4; //strlen(".del");
            memcpy(l_del_group_name_replace, l_group_cur->name, l_del_name_len);
            l_del_group_name_replace[l_del_name_len] = '\0';
        } else {
            l_obj_type = DAP_DB$K_OPTYPE_ADD;
        }
        uint64_t l_item_start = l_group_cur->last_id_synced + 1;
        dap_nanotime_t l_time_allowed = dap_nanotime_now() + dap_nanotime_from_sec(3600 * 24); // to be sure the timestamp is invalid
        while (l_group_cur->count && l_dap_db_log_list->is_process) {
            // Number of records to be synchronized
            size_t l_item_count = 0;//min(64, l_group_cur->count);
            size_t l_objs_total_size = 0;
            dap_store_obj_t *l_objs = dap_global_db_get_all_raw_sync(l_group_cur->name, 0, &l_item_count);
            /*if (!l_dap_db_log_list->is_process) {
                dap_store_obj_free(l_objs, l_item_count);
                return NULL;
            }*/
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
                    log_it(L_CRITICAL, "Memory allocation error");
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
        if (!l_dap_db_log_list->is_process)
            return NULL;
    }

    pthread_mutex_lock(&l_dap_db_log_list->list_mutex);
    l_dap_db_log_list->is_process = false;
    pthread_mutex_unlock(&l_dap_db_log_list->list_mutex);
    return NULL;
}

#endif

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
        log_it(L_CRITICAL, "Memory allocation error");
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
    dap_list_t *l_group, *l_tmp;
    // Check for banned/whitelisted groups
    dap_global_db_instance_t *l_dbi = l_dap_db_log_list->db_context->instance;
    if (l_dbi->whitelist || l_dbi->blacklist) {
        dap_list_t *l_used_list = l_dbi->whitelist ? l_dbi->whitelist : l_dbi->blacklist;
        DL_FOREACH_SAFE(l_groups_names, l_group, l_tmp) {
            dap_list_t *l_used_el;
            bool l_match = false;
            DL_FOREACH(l_used_list, l_used_el) {
                if (!dap_fnmatch(l_used_el->data, l_group->data, FNM_NOESCAPE)) {
                    l_match = true;
                    break;
                }
            }
            if (l_used_list == l_dbi->whitelist ? !l_match : l_match) {
                l_groups_names = dap_list_delete_link(l_groups_names, l_group);
            }
        }
    }

    l_dap_db_log_list->groups = l_groups_names; // repalce name of group with group item
    DL_FOREACH_SAFE(l_dap_db_log_list->groups, l_group, l_tmp) {
        dap_db_log_list_group_t *l_sync_group = DAP_NEW_Z(dap_db_log_list_group_t);
        if (!l_sync_group) {
            log_it(L_CRITICAL, "Memory allocation error");
            DAP_DEL_Z(l_dap_db_log_list);
            return NULL;
        }

        l_sync_group->name = (char*)l_group->data;
        l_sync_group->last_id_synced = a_flags & F_DB_LOG_SYNC_FROM_ZERO ? 0 : dap_db_get_last_id_remote(a_node_addr, l_sync_group->name);
        l_sync_group->count = dap_global_db_driver_count(l_sync_group->name, l_sync_group->last_id_synced + 1);
        if (!l_sync_group->count) {
            debug_if(g_dap_global_db_debug_more, L_DEBUG, "Group %s is empty on our side, skip it", l_sync_group->name);
            l_dap_db_log_list->groups = dap_list_delete_link(l_dap_db_log_list->groups, l_group);
            DAP_DELETE(l_sync_group);
            continue;
        }
        l_dap_db_log_list->items_number += l_sync_group->count;
        l_group->data = l_sync_group;
    }
    l_dap_db_log_list->items_rest = l_dap_db_log_list->items_number;
    if (!l_dap_db_log_list->items_number) {
        DAP_DELETE(l_dap_db_log_list);
        return NULL;
    }
    l_dap_db_log_list->is_process = true;
    pthread_mutex_init(&l_dap_db_log_list->list_mutex, NULL);
    pthread_cond_init(&l_dap_db_log_list->cond, NULL);
    pthread_create(&l_dap_db_log_list->thread, NULL, s_list_thread_proc2, l_dap_db_log_list);
    return l_dap_db_log_list;
}

#if 0
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
    dap_db_log_list_obj_t *l_ret = NULL;
    dap_list_t *l_first_elem = a_db_log_list->items_list;
    if (l_first_elem) {
        l_ret = l_first_elem->data;
        size_t l_old_size = a_db_log_list->size;
        a_db_log_list->items_list = dap_list_delete_link(a_db_log_list->items_list, l_first_elem);
        a_db_log_list->items_rest--;
        a_db_log_list->size -= dap_db_log_list_obj_get_size(l_ret);

        if (l_old_size > DAP_DB_LOG_LIST_MAX_SIZE && a_db_log_list->size <= DAP_DB_LOG_LIST_MAX_SIZE)
            pthread_cond_signal(&a_db_log_list->cond);
    }
    pthread_mutex_unlock(&a_db_log_list->list_mutex);
    return l_ret ? l_ret : DAP_INT_TO_POINTER(l_is_process);
}
#endif

dap_db_log_list_obj_t **dap_db_log_list_get_multiple(dap_db_log_list_t *a_db_log_list, size_t a_size_limit, size_t *a_count) {
    if (!a_db_log_list || !a_count)
        return NULL;
    pthread_mutex_lock(&a_db_log_list->list_mutex);
    size_t l_count = a_db_log_list->items_list
            ? *a_count
              ? dap_min(*a_count, dap_list_length(a_db_log_list->items_list))
              : dap_list_length(a_db_log_list->items_list)
            : 0;
    size_t l_old_size = a_db_log_list->size, l_out_size = 0;
    dap_db_log_list_obj_t **l_ret = DAP_NEW_Z_COUNT(dap_db_log_list_obj_t*, l_count);
    if (l_ret) {
        *a_count = l_count;
        dap_list_t *l_elem, *l_tmp;
        DL_FOREACH_SAFE(a_db_log_list->items_list, l_elem, l_tmp) {
            l_out_size += dap_db_log_list_obj_get_size(l_elem->data);
            if (a_size_limit && l_out_size > a_size_limit)
                break;
            l_ret[*a_count - l_count] = l_elem->data;
            --a_db_log_list->items_rest;
            a_db_log_list->size -= dap_db_log_list_obj_get_size(l_elem->data);
            a_db_log_list->items_list = dap_list_delete_link(a_db_log_list->items_list, l_elem);
            if (!(--l_count))
                break;
        }
        if (l_count) {
            *a_count -= l_count;
            l_ret = DAP_REALLOC_COUNT(l_ret, *a_count);
        }
        log_it(L_MSG, "[!] Extracted %zu records from log_list (size %zu), left %zu", *a_count, l_out_size, a_db_log_list->size);
        if (l_old_size > DAP_DB_LOG_LIST_MAX_SIZE && a_db_log_list->size <= DAP_DB_LOG_LIST_MAX_SIZE)
            pthread_cond_signal(&a_db_log_list->cond);
    }
    pthread_mutex_unlock(&a_db_log_list->list_mutex);
    return l_ret ? l_ret : a_db_log_list->is_process ? DAP_INT_TO_POINTER(0x1) : NULL;
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
    dap_list_free_full(a_db_log_list->items_list, (dap_callback_destroyed_t)s_dap_db_log_list_delete_item);
    pthread_mutex_destroy(&a_db_log_list->list_mutex);
    dap_list_free_full(a_db_log_list->groups, NULL);
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
    return sizeof(uint32_t)
            + 2 * sizeof(uint16_t)
            + 3 * sizeof(uint64_t)
            + dap_strlen(store_obj->group) /* + 1 */
            + dap_strlen(store_obj->key) /* + 1 */
            + store_obj->value_len;
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
    a_old_pkt = a_old_pkt
            ? DAP_REALLOC(a_old_pkt, sizeof(dap_global_db_pkt_t) + a_old_pkt->data_size + a_new_pkt->data_size)
            : DAP_NEW_Z_SIZE(dap_global_db_pkt_t, sizeof(dap_global_db_pkt_t) + a_new_pkt->data_size);
    memcpy(a_old_pkt->data + a_old_pkt->data_size, a_new_pkt->data, a_new_pkt->data_size);
    a_old_pkt->data_size += a_new_pkt->data_size;
    ++a_old_pkt->obj_count;
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
    memcpy(a_pkt->data + l_id_offset, &a_id, sizeof(a_id));
}

/**
 * @brief Serializes an object into a packed structure.
 * @param a_store_obj a pointer to the object to be serialized
 * @return Returns a pointer to the packed sructure if successful, otherwise NULL.
 */
dap_global_db_pkt_t *dap_global_db_pkt_serialize(dap_store_obj_t *a_store_obj)
{
    byte_t *pdata;

    if (!a_store_obj)
        return NULL;

    uint32_t l_data_size_out = dap_db_get_size_pdap_store_obj_t(a_store_obj);
    dap_global_db_pkt_t *l_pkt = DAP_NEW_Z_SIZE(dap_global_db_pkt_t, l_data_size_out + sizeof(dap_global_db_pkt_t));

    /* Fill packet header */
    l_pkt->data_size = l_data_size_out;
    l_pkt->obj_count = 1;
    l_pkt->timestamp = 0;
    uint16_t l_group_len = dap_strlen(a_store_obj->group), l_key_len = dap_strlen(a_store_obj->key);
    /* Put serialized data into the payload part of the packet */
    pdata = l_pkt->data;
    memcpy(pdata,   &a_store_obj->type,     sizeof(uint32_t));      pdata += sizeof(uint32_t);
    memcpy(pdata,   &l_group_len,           sizeof(uint16_t));      pdata += sizeof(uint16_t);
    memcpy(pdata,   a_store_obj->group,     l_group_len /* + 1 */); pdata += l_group_len /* + 1 */;
    memcpy(pdata,   &a_store_obj->id,       sizeof(uint64_t));      pdata += sizeof(uint64_t);
    memcpy(pdata,   &a_store_obj->timestamp,sizeof(uint64_t));      pdata += sizeof(uint64_t);
    memcpy(pdata,   &l_key_len,             sizeof(uint16_t));      pdata += sizeof(uint16_t);
    memcpy(pdata,   a_store_obj->key,       l_key_len /* + 1 */);   pdata += l_key_len /* + 1 */;
    memcpy(pdata,   &a_store_obj->value_len,sizeof(uint64_t));      pdata += sizeof(uint64_t);
    if (a_store_obj->value && a_store_obj->value_len) {
        memcpy(pdata, a_store_obj->value, a_store_obj->value_len);  pdata += a_store_obj->value_len;
    }
    if ((uint32_t)(pdata - l_pkt->data) != l_data_size_out) {
        log_it(L_MSG, "! Inconsistent global_db packet! %u != %u", (uint32_t)(pdata - l_pkt->data), l_data_size_out);
    }
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
uint32_t l_count;
byte_t *pdata, *pdata_end;
dap_store_obj_t *l_store_obj_arr, *l_obj;

    if(!a_pkt || a_pkt->data_size < sizeof(dap_global_db_pkt_t))
        return NULL;

    if ( !(l_store_obj_arr = DAP_NEW_Z_COUNT(dap_store_obj_t, a_pkt->obj_count)) ) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

    pdata = (byte_t*)a_pkt->data; pdata_end = pdata + a_pkt->data_size;
    l_obj = l_store_obj_arr;

    for (l_count = 0; l_count < a_pkt->obj_count; ++l_count, ++l_obj) {
        if ( pdata + sizeof (uint32_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'type' field"); break;
        }
        memcpy(&l_obj->type, pdata, sizeof(uint32_t)); pdata += sizeof(uint32_t);

        if ( pdata + sizeof (uint16_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'group_length' field"); break;
        }
        memcpy(&l_obj->group_len, pdata, sizeof(uint16_t)); pdata += sizeof(uint16_t);

        if (!l_obj->group_len || pdata + l_obj->group_len > pdata_end) {
            log_it(L_ERROR, "Broken GDB element: can't read 'group' field"); break;
        }
        l_obj->group = DAP_NEW_Z_SIZE(char, l_obj->group_len + 1);
        memcpy(l_obj->group, pdata, l_obj->group_len); pdata += l_obj->group_len;

        if ( pdata + sizeof (uint64_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'id' field");
            DAP_DELETE(l_obj->group); break;
        }
        memcpy(&l_obj->id, pdata, sizeof(uint64_t)); pdata += sizeof(uint64_t);

        if ( pdata + sizeof (uint64_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'timestamp' field");
            DAP_DELETE(l_obj->group); break;
        }
        memcpy(&l_obj->timestamp, pdata, sizeof(uint64_t)); pdata += sizeof(uint64_t);

        if ( pdata + sizeof (uint16_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'key_length' field");
            DAP_DELETE(l_obj->group); break;
        }
        memcpy(&l_obj->key_len, pdata, sizeof(uint16_t)); pdata += sizeof(uint16_t);

        if ( !l_obj->key_len || pdata + l_obj->key_len > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: 'key' field");
            DAP_DELETE(l_obj->group); break;
        }
        l_obj->key = DAP_NEW_Z_SIZE(char, l_obj->key_len + 1);
        memcpy((char*)l_obj->key, pdata, l_obj->key_len); pdata += l_obj->key_len;

        if ( pdata + sizeof (uint64_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'value_length' field");
            DAP_DELETE(l_obj->group);
            DAP_DELETE(l_obj->key); break;
        }
        memcpy(&l_obj->value_len, pdata, sizeof(uint64_t)); pdata += sizeof(uint64_t);

        if (l_obj->value_len && pdata + l_obj->value_len > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'value' field");
            DAP_DELETE(l_obj->group);
            DAP_DELETE(l_obj->key); break;
        }
        l_obj->value = DAP_DUP_SIZE(pdata, l_obj->value_len); pdata += l_obj->value_len;
    }

    if ( pdata < pdata_end ) {
        log_it(L_WARNING, "Unprocessed %zu bytes left in GDB packet", pdata_end - pdata);
        l_store_obj_arr = DAP_REALLOC_COUNT(l_store_obj_arr, l_count);
    }

    if (a_store_obj_count)
        *a_store_obj_count = l_count;

    return l_store_obj_arr;
}

int dap_global_db_remote_apply_obj_unsafe(dap_global_db_context_t *a_global_db_context, dap_store_obj_t *a_obj, size_t a_count,
                                          dap_global_db_callback_results_raw_t a_callback, void *a_arg)
{
    dap_nanotime_t l_timestamp_cur = 0;
    dap_store_obj_t *l_obj, *l_last_obj = a_obj + a_count - 1;
    size_t l_count = a_count;
    dap_nanotime_t l_now = dap_nanotime_now();
    for (l_obj = a_obj; l_obj <= l_last_obj; ++l_obj) {
        bool l_match_mask = false, l_is_pinned_cur = false;
        uint64_t l_ttl = 0;
        for (dap_list_t *it = a_global_db_context->instance->notify_groups; it; it = it->next) {
            dap_global_db_notify_item_t *l_item = it->data;
            if (!dap_fnmatch(l_item->group_mask, l_obj->group, 0)) {
                debug_if(g_dap_global_db_debug_more, L_DEBUG, "Group %s match mask %s.", l_obj->group, l_item->group_mask);
                l_match_mask = true;
                l_ttl = l_item->ttl;
                break;
            }
        }
        if (!l_match_mask) {
            log_it(L_WARNING, "An entry in the group %s was rejected because the group name did not match any of the masks.", l_obj->group);
            dap_store_obj_clear_one(l_obj);
            if (l_obj < l_last_obj) {
                *l_obj-- = *l_last_obj;
            }
            l_last_obj->group = NULL; l_last_obj->key = NULL; l_last_obj->value = NULL;
            --l_last_obj;
            --l_count;
            continue;
        }

        if (g_dap_global_db_debug_more) {
            char l_ts_str[64] = { '\0' };
            dap_time_to_str_rfc822(l_ts_str, sizeof(l_ts_str), dap_nanotime_to_sec(l_obj->timestamp));
            log_it(L_DEBUG, "Unpacked log history: type='%c' (0x%02hhX) group=\"%s\" key=\"%s\""
                    " timestamp=\"%s\" value_len=%" DAP_UINT64_FORMAT_U,
                    (char)l_obj->type, (char)l_obj->type, l_obj->group,
                    l_obj->key, l_ts_str, l_obj->value_len);
        }

        bool l_broken = !dap_global_db_isalnum_group_key(l_obj);
        dap_store_obj_t *l_read_obj = dap_global_db_driver_read(l_obj->group, l_obj->key, NULL);
        if (l_read_obj) {
            l_timestamp_cur = l_read_obj->timestamp;
            if (l_read_obj->flags & RECORD_PINNED)
                l_is_pinned_cur = true;
            else {
                dap_store_obj_free_one(l_read_obj);
                l_read_obj = NULL;
            }
        }

        // Deleted time
        dap_nanotime_t l_timestamp_del = dap_global_db_get_del_ts_unsafe(a_global_db_context, l_obj->group, l_obj->key);
        // Limit time
        dap_nanotime_t l_time_store_lim_hours = l_ttl ? l_ttl : a_global_db_context->instance->store_time_limit;
        dap_nanotime_t l_limit_time = l_time_store_lim_hours
                ? l_now - dap_nanotime_from_sec(l_time_store_lim_hours * 3600)
                : 0;
        bool l_apply = !l_broken;

        if (l_apply) {
            if ( l_obj->timestamp > l_now ) {
                l_apply = false;
                if (g_dap_global_db_debug_more) {
                    char l_ts[64] = { '\0' };
                    dap_gbd_time_to_str_rfc822(l_ts, sizeof(l_ts), l_obj->timestamp);
                    log_it(L_INFO, "Skip \"%s : %s\", record is from the future: %s",
                           l_obj->group, l_obj->key, l_ts);
                }
            }
            if ( l_obj->timestamp <= (uint64_t)l_timestamp_del ) {
                l_apply = false;
                if (g_dap_global_db_debug_more) {
                    char l_ts[64] = { '\0' };
                    dap_gbd_time_to_str_rfc822(l_ts, sizeof(l_ts), l_timestamp_del);
                    log_it(L_INFO, "Skip \"%s : %s\", record already deleted at %s",
                           l_obj->group, l_obj->key, l_ts);
                }
            } else if (l_obj->timestamp <= (uint64_t)l_timestamp_cur) {
                l_apply = false;
                if (g_dap_global_db_debug_more) {
                    char l_ts[64] = { '\0' };
                    dap_gbd_time_to_str_rfc822(l_ts, sizeof(l_ts), l_timestamp_cur);
                    log_it(L_INFO, "Skip \"%s : %s\", record already added at %s",
                           l_obj->group, l_obj->key, l_ts);
                }
            }

            switch (l_obj->type) {
            case DAP_DB$K_OPTYPE_ADD:
                if ( (l_obj->timestamp < l_limit_time) && !l_is_pinned_cur) {
                    l_apply = false;
                    debug_if(g_dap_global_db_debug_more, L_INFO, "Skip \"%s : %s\", record is too old",
                             l_obj->group, l_obj->key);
                }
                break;
            case DAP_DB$K_OPTYPE_DEL:
                if (l_obj->timestamp < l_limit_time) {
                    l_apply = false;
                    debug_if(g_dap_global_db_debug_more, L_INFO, "Skip deleting \"%s : %s\", record is too old",
                             l_obj->group, l_obj->key);
                }
                break;
            default: break;
            }
        } else {
            debug_if(g_dap_global_db_debug_more, L_WARNING, "Skip \"%s : %s\", record is corrupted", l_obj->group, l_obj->key);
        }

        if (!l_apply) {
            dap_store_obj_free(l_read_obj, (int)l_is_pinned_cur);
            dap_store_obj_clear_one(l_obj);
            if (l_obj < l_last_obj) {
                *l_obj-- = *l_last_obj;
            }
            l_last_obj->group = NULL; l_last_obj->key = NULL; l_last_obj->value = NULL;
            --l_last_obj;
            --l_count;
            continue;
        }
        // Do not overwrite pinned records
        if (l_is_pinned_cur) {
            if ( (l_obj->timestamp - l_read_obj->timestamp == 1) && l_obj->type == DAP_DB$K_OPTYPE_ADD ) {
                debug_if(g_dap_global_db_debug_more, L_INFO, "Record \"%s : %s\" was repinned, unpin it", l_obj->group, l_obj->key);
            } else {
                debug_if(g_dap_global_db_debug_more, L_WARNING, "Can't %s record \"%s : %s\" - it's pinned",
                                        l_obj->type != DAP_DB$K_OPTYPE_DEL ? "delete" : "rewrite", l_obj->group, l_obj->key);
                l_read_obj->timestamp = l_obj->timestamp + 1;
                l_read_obj->type = DAP_DB$K_OPTYPE_ADD;
                dap_store_obj_clear_one(l_obj);
                *l_obj = *l_read_obj;
                l_read_obj->group = NULL; l_read_obj->key = NULL; l_read_obj->value = NULL;
            }
            dap_store_obj_free_one(l_read_obj);
        }
    }    
    return l_count ? dap_global_db_set_raw(a_obj, l_count, a_callback, a_arg) : ({ DAP_DELETE(a_obj); DAP_DELETE(a_arg); -1; });
}

struct gdb_apply_args {
    dap_store_obj_t *obj;
    size_t objs_count;
    dap_global_db_callback_results_raw_t callback;
    void *cb_arg;
};

static void s_db_apply_obj(dap_global_db_context_t *a_global_db_context, void *a_arg)
{
    struct gdb_apply_args *l_args = a_arg;
    dap_global_db_remote_apply_obj_unsafe(a_global_db_context, l_args->obj, l_args->objs_count, l_args->callback, l_args->cb_arg);
    DAP_DELETE(l_args);
}

int dap_global_db_remote_apply_obj(dap_store_obj_t *a_obj, size_t a_count, dap_global_db_callback_results_raw_t a_callback, void *a_arg)
{
    struct gdb_apply_args *l_args =  DAP_NEW_Z(struct gdb_apply_args);
    if (!l_args) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }
    l_args->obj = a_obj;
    l_args->objs_count = a_count;
    l_args->callback = a_callback;
    l_args->cb_arg = a_arg;
    return dap_global_db_context_exec(s_db_apply_obj, l_args);
}
