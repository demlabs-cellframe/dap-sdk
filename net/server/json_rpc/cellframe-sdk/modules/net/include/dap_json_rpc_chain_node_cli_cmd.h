/*
 * Authors:
 * Alexey V. Stratulat <alexey.stratulat@demlabs.net>
 * Olzhas Zharasbaev <oljas.jarasbaev@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe/cellframe-sdk
 * Copyright  (c) 2017-2023
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
*/
#pragma once

#include "dap_chain_node_cli_cmd.h"
#include "dap_json_rpc_errors.h"
#include "json.h"

int dap_chain_node_cli_cmd_values_parse_net_chain_for_json(int *a_arg_index, int a_argc,
                                                           char **a_argv,
                                                           dap_chain_t **a_chain, dap_chain_net_t **a_net);

                                                           /**
 * tx_history command
 *
 * Transaction history for an address
 */
int com_tx_history_rpc(int a_argc, char ** a_argv, json_object** json_arr_reply);

int com_mempool_delete_rpc(int a_argc, char **a_argv, json_object **a_json_reply);
int com_mempool_list_rpc(int a_argc, char **a_argv, json_object **a_json_reply);
int com_mempool_proc_rpc(int a_argc, char **a_argv, json_object **a_json_reply);
int com_mempool_proc_all_rpc(int argc, char ** argv, json_object ** a_json_reply);
int com_mempool_check_rpc(int a_argc, char **a_argv, json_object **a_json_reply);

void s_com_mempool_list_print_for_chain_rpc(dap_chain_net_t * a_net, dap_chain_t * a_chain, const char * a_add, json_object *a_json_obj, const char *a_hash_out_type, bool a_fast);


/**
 * Place public CA into the mempool
 */
int com_mempool_add_ca_rpc( int a_argc,  char **a_argv, json_object **a_json_reply);

int com_chain_ca_copy_rpc( int a_argc,  char **a_argv, json_object **a_json_reply);
