#!/bin/bash

# Fix remaining warnings in dap_json.c - replace direct json-c calls with helper functions

DAP_JSON_C="module/core/src/dap_json.c"

echo "Fixing remaining warnings in dap_json.c..."

# Fix direct json_object_object_add calls that need first parameter conversion
sed -i 's/json_object_object_add(a_json,/json_object_object_add(_dap_json_to_json_c(a_json),/g' "$DAP_JSON_C"

# Fix json_object_get calls
sed -i 's/json_object_get(a_value)/json_object_get(_dap_json_to_json_c(a_value))/g' "$DAP_JSON_C"
sed -i 's/json_object_get(a_array)/json_object_get(_dap_json_to_json_c(a_array))/g' "$DAP_JSON_C"

# Fix json_object_object_get_ex calls with dap_json_t* first parameter
sed -i 's/json_object_object_get_ex(a_json,/json_object_object_get_ex(_dap_json_to_json_c(a_json),/g' "$DAP_JSON_C"

# Fix json_object_to_json_string calls
sed -i 's/json_object_to_json_string(a_json)/json_object_to_json_string(_dap_json_to_json_c(a_json))/g' "$DAP_JSON_C"

# Fix json_object_to_json_string_ext calls
sed -i 's/json_object_to_json_string_ext(a_json,/json_object_to_json_string_ext(_dap_json_to_json_c(a_json),/g' "$DAP_JSON_C"

# Fix json_object_is_type calls
sed -i 's/json_object_is_type(a_json,/json_object_is_type(_dap_json_to_json_c(a_json),/g' "$DAP_JSON_C"

# Fix json_object_object_del calls
sed -i 's/json_object_object_del(a_json,/json_object_object_del(_dap_json_to_json_c(a_json),/g' "$DAP_JSON_C"

# Fix json_object_get_type calls
sed -i 's/json_object_get_type(a_json)/json_object_get_type(_dap_json_to_json_c(a_json))/g' "$DAP_JSON_C"

# Fix remaining return statements that return raw json_object*
sed -i 's/return l_obj;/return _json_c_to_dap_json(l_obj);/g' "$DAP_JSON_C"

# Fix specific functions that return raw json_object*
sed -i 's/return json_object_get(/return _json_c_to_dap_json(json_object_get(_dap_json_to_json_c(/g' "$DAP_JSON_C"
sed -i 's/return json_object_new_int(/return _json_c_to_dap_json(json_object_new_int(/g' "$DAP_JSON_C"
sed -i 's/return json_object_new_string(/return _json_c_to_dap_json(json_object_new_string(/g' "$DAP_JSON_C"
sed -i 's/return json_object_new_double(/return _json_c_to_dap_json(json_object_new_double(/g' "$DAP_JSON_C"
sed -i 's/return json_object_new_boolean(/return _json_c_to_dap_json(json_object_new_boolean(/g' "$DAP_JSON_C"

# Fix closing parenthesis for modified function calls
sed -i 's/json_object_get(_dap_json_to_json_c(a_json));/json_object_get(_dap_json_to_json_c(a_json)));/g' "$DAP_JSON_C"

echo "Fixed remaining warnings!"
