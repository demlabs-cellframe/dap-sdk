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

#include "dap_chain_node_cli_cmd_tx.h"


/**
 *
 * return history json
 */
json_object * dap_db_history_tx_rpc(dap_chain_hash_fast_t* a_tx_hash, dap_chain_t * a_chain, const char *a_hash_out_type, dap_chain_net_t * l_net);
json_object * dap_db_history_addr_rpc(dap_chain_addr_t * a_addr, dap_chain_t * a_chain, const char *a_hash_out_type, const char * l_addr_str);
json_object * dap_db_tx_history_to_json_rpc(dap_chain_hash_fast_t* a_tx_hash,
                                        dap_hash_fast_t * l_atom_hash,
                                        dap_chain_datum_tx_t * l_tx,
                                        dap_chain_t * a_chain, 
                                        const char *a_hash_out_type, 
                                        dap_chain_net_t * l_net,
                                        int l_ret_code,
                                        bool *accepted_tx);
json_object* dap_db_history_tx_all_rpc(dap_chain_t *l_chain, dap_chain_net_t* l_net, const char *l_hash_out_type, json_object * json_obj_summary);
