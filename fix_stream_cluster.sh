#!/bin/bash

# Fix dap_stream_cluster.c to use dap_json API

FILE="module/net/stream/stream/dap_stream_cluster.c"

echo "Fixing $FILE to use dap_json API..."

# Replace json_object* declarations with dap_json_t*
sed -i 's/json_object \*/dap_json_t */g' "$FILE"

# Replace json_object_new_object() calls
sed -i 's/json_object_new_object()/dap_json_object_new()/g' "$FILE"

# Replace json_object_new_array() calls  
sed -i 's/json_object_new_array()/dap_json_array_new()/g' "$FILE"

# Replace json_object_new_string() calls
sed -i 's/json_object_new_string(/dap_json_object_new_string(/g' "$FILE"

# Replace json_object_new_int() calls
sed -i 's/json_object_new_int(/dap_json_object_new_int(/g' "$FILE"

# Replace json_object_object_add() calls  
sed -i 's/json_object_object_add(/dap_json_object_add_string(/g' "$FILE"

# Replace json_object_array_add() calls
sed -i 's/json_object_array_add(/dap_json_array_add(/g' "$FILE"

# Fix specific error handling - remove dap_json_rpc_allocation_put calls and replace with proper cleanup
sed -i 's/return dap_json_rpc_allocation_put(l_jobj_ret);/{ dap_json_object_free(l_jobj_ret); return NULL; }/g' "$FILE"

# Fix cases where we're adding objects/arrays to other objects
sed -i 's/dap_json_object_add_string(l_jobj_ret, "downlinks", l_jobj_downlinks);/dap_json_object_add_array(l_jobj_ret, "downlinks", l_jobj_downlinks);/g' "$FILE"
sed -i 's/dap_json_object_add_string(l_jobj_ret, "uplinks", l_jobj_uplinks);/dap_json_object_add_array(l_jobj_ret, "uplinks", l_jobj_uplinks);/g' "$FILE"

# Fix cases where we're adding objects to other objects (not strings)
sed -i 's/dap_json_object_add_string(l_jobj_info, "addr", l_jobj_node_addr);/dap_json_object_add_object(l_jobj_info, "addr", l_jobj_node_addr);/g' "$FILE"
sed -i 's/dap_json_object_add_string(l_jobj_info, "ip", l_jobj_ip);/dap_json_object_add_object(l_jobj_info, "ip", l_jobj_ip);/g' "$FILE"
sed -i 's/dap_json_object_add_string(l_jobj_info, "port", l_jobj_port);/dap_json_object_add_object(l_jobj_info, "port", l_jobj_port);/g' "$FILE"

echo "Fixed $FILE!"
