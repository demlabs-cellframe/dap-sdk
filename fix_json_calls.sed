s/dap_json_object_add_int_direct(/dap_json_object_new(); dap_json_object_add_int(obj, "value", /g
s/dap_json_object_add_string_direct(/dap_json_object_new(); dap_json_object_add_string(obj, "value", /g
s/json_object_new_int(/dap_json_object_new_int_value(/g
s/json_object_new_string(/dap_json_object_new_string_value(/g
s/json_type_array/DAP_JSON_TYPE_ARRAY/g
s/json_type_object/DAP_JSON_TYPE_OBJECT/g
