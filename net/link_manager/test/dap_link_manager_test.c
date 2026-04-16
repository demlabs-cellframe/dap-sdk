/*
 * Authors:
 * Copyright (c) DeM Labs Inc.
 * License: GNU General Public License
 */

#include "dap_common.h"
#include "dap_config.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_stream_ch_proc.h"
#include "dap_link_manager.h"
#include "dap_test.h"

#define LOG_TAG "dap_link_manager_test"

static void s_dummy_ch_new(dap_stream_ch_t *a_ch, void *a_arg) { (void)a_ch; (void)a_arg; }
static void s_dummy_ch_delete(dap_stream_ch_t *a_ch, void *a_arg) { (void)a_ch; (void)a_arg; }
static bool s_dummy_ch_pkt_in(dap_stream_ch_t *a_ch, void *a_arg) { (void)a_ch; (void)a_arg; return true; }
static bool s_dummy_ch_pkt_out(dap_stream_ch_t *a_ch, void *a_arg) { (void)a_ch; (void)a_arg; return false; }
static int s_dummy_fill_net_info(dap_link_t *a_link) { (void)a_link; return 0; }

static void s_test_active_channels_default(void)
{
    dap_print_module_name("dap_link_manager_active_channels_default");

    const char *l_channels = dap_link_manager_get_active_channels();
    dap_assert(l_channels != NULL, "get_active_channels returns non-NULL");
    dap_assert(dap_str_equals(l_channels, "RCGEND"), "Default active channels are \"RCGEND\"");
    dap_assert(strlen(l_channels) == 6, "Default channels length is 6");
}

static void s_test_add_channel(void)
{
    dap_print_module_name("dap_link_manager_add_active_channel");

    dap_stream_ch_proc_add('T', s_dummy_ch_new, s_dummy_ch_delete, s_dummy_ch_pkt_in, s_dummy_ch_pkt_out);

    int l_ret = dap_link_manager_add_active_channel('T');
    dap_assert(l_ret == 0, "Add channel 'T' returns 0");

    const char *l_channels = dap_link_manager_get_active_channels();
    dap_assert(strlen(l_channels) == 7, "Channels length is now 7");
    dap_assert(l_channels[6] == 'T', "Channel 'T' appended at end");
    dap_assert(dap_str_equals(l_channels, "RCGENDT"), "Active channels are \"RCGENDT\"");
}

static void s_test_add_duplicate(void)
{
    dap_print_module_name("dap_link_manager_add_duplicate_channel");

    int l_ret = dap_link_manager_add_active_channel('T');
    dap_assert(l_ret == 0, "Add duplicate channel 'T' returns 0 (idempotent)");

    const char *l_channels = dap_link_manager_get_active_channels();
    dap_assert(dap_str_equals(l_channels, "RCGENDT"), "Channels unchanged after duplicate add");
}

static void s_test_add_unregistered(void)
{
    dap_print_module_name("dap_link_manager_add_unregistered_channel");

    int l_ret = dap_link_manager_add_active_channel('Z');
    dap_assert(l_ret == -3, "Add unregistered channel 'Z' returns -3");

    const char *l_channels = dap_link_manager_get_active_channels();
    dap_assert(dap_str_equals(l_channels, "RCGENDT"), "Channels unchanged after failed add");
}

static void s_test_add_existing_default(void)
{
    dap_print_module_name("dap_link_manager_add_existing_default_channel");

    int l_ret = dap_link_manager_add_active_channel('R');
    dap_assert(l_ret == 0, "Add already-present default channel 'R' returns 0");

    const char *l_channels = dap_link_manager_get_active_channels();
    dap_assert(dap_str_equals(l_channels, "RCGENDT"), "Channels unchanged, no duplicate 'R'");
}

static void s_test_remove_channel(void)
{
    dap_print_module_name("dap_link_manager_remove_active_channel");

    int l_ret = dap_link_manager_remove_active_channel('T');
    dap_assert(l_ret == 0, "Remove channel 'T' returns 0");

    const char *l_channels = dap_link_manager_get_active_channels();
    dap_assert(dap_str_equals(l_channels, "RCGEND"), "Channels back to \"RCGEND\"");
}

static void s_test_remove_nonexistent(void)
{
    dap_print_module_name("dap_link_manager_remove_nonexistent_channel");

    int l_ret = dap_link_manager_remove_active_channel('X');
    dap_assert(l_ret == -2, "Remove nonexistent channel 'X' returns -2");

    const char *l_channels = dap_link_manager_get_active_channels();
    dap_assert(dap_str_equals(l_channels, "RCGEND"), "Channels unchanged after failed remove");
}

static void s_test_remove_middle(void)
{
    dap_print_module_name("dap_link_manager_remove_middle_channel");

    int l_ret = dap_link_manager_remove_active_channel('G');
    dap_assert(l_ret == 0, "Remove middle channel 'G' returns 0");

    const char *l_channels = dap_link_manager_get_active_channels();
    dap_assert(dap_str_equals(l_channels, "RCEND"), "Channels are \"RCEND\" after removing 'G'");
    dap_assert(strlen(l_channels) == 5, "Channels length is 5");
}

static void s_test_remove_first(void)
{
    dap_print_module_name("dap_link_manager_remove_first_channel");

    int l_ret = dap_link_manager_remove_active_channel('R');
    dap_assert(l_ret == 0, "Remove first channel 'R' returns 0");

    const char *l_channels = dap_link_manager_get_active_channels();
    dap_assert(dap_str_equals(l_channels, "CEND"), "Channels are \"CEND\" after removing 'R'");
}

static void s_test_remove_last(void)
{
    dap_print_module_name("dap_link_manager_remove_last_channel");

    int l_ret = dap_link_manager_remove_active_channel('D');
    dap_assert(l_ret == 0, "Remove last channel 'D' returns 0");

    const char *l_channels = dap_link_manager_get_active_channels();
    dap_assert(dap_str_equals(l_channels, "CEN"), "Channels are \"CEN\" after removing 'D'");
}

static void s_test_add_multiple(void)
{
    dap_print_module_name("dap_link_manager_add_multiple_channels");

    dap_stream_ch_proc_add('X', s_dummy_ch_new, s_dummy_ch_delete, s_dummy_ch_pkt_in, s_dummy_ch_pkt_out);
    dap_stream_ch_proc_add('Y', s_dummy_ch_new, s_dummy_ch_delete, s_dummy_ch_pkt_in, s_dummy_ch_pkt_out);

    int l_ret1 = dap_link_manager_add_active_channel('X');
    int l_ret2 = dap_link_manager_add_active_channel('Y');
    dap_assert(l_ret1 == 0 && l_ret2 == 0, "Add channels 'X' and 'Y' both return 0");

    const char *l_channels = dap_link_manager_get_active_channels();
    dap_assert(dap_str_equals(l_channels, "CENXY"), "Channels are \"CENXY\"");
}

int main(void)
{
    dap_log_level_set(L_ERROR);

    dap_config_init("/tmp");
    dap_events_init(0, 0);
    dap_events_start();

    dap_link_manager_callbacks_t l_callbacks = {0};
    l_callbacks.fill_net_info = s_dummy_fill_net_info;
    int l_ret = dap_link_manager_init(&l_callbacks);
    if (l_ret) {
        printf("Failed to init link manager: %d\n", l_ret);
        return 1;
    }

    s_test_active_channels_default();
    s_test_add_channel();
    s_test_add_duplicate();
    s_test_add_unregistered();
    s_test_add_existing_default();
    s_test_remove_channel();
    s_test_remove_nonexistent();
    s_test_remove_middle();
    s_test_remove_first();
    s_test_remove_last();
    s_test_add_multiple();

    printf("\nAll link manager active channels tests passed.\n");
    return 0;
}
