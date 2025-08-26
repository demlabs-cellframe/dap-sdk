#!/bin/bash

# Script to fix dap_json.c to use opaque types properly

DAP_JSON_C="module/core/src/dap_json.c"

echo "Fixing dap_json.c to use opaque types..."

# Backup original file
cp "$DAP_JSON_C" "$DAP_JSON_C.backup"

# Function to replace direct json-c calls with conversion helper calls
# This script systematically updates all function calls

# 1. Fix return statements that return raw json_object* to use conversion helpers
sed -i 's/return json_object_new_array()/return _json_c_to_dap_json_array(json_object_new_array())/g' "$DAP_JSON_C"
sed -i 's/return json_object_array_get_idx(\([^,]*\), \([^)]*\))/return _json_c_to_dap_json(json_object_array_get_idx(_dap_json_array_to_json_c(\1), \2))/g' "$DAP_JSON_C"
sed -i 's/return json_object_from_file(\([^)]*\))/return _json_c_to_dap_json(json_object_from_file(\1))/g' "$DAP_JSON_C"
sed -i 's/return json_object_new_int(\([^)]*\))/return _json_c_to_dap_json(json_object_new_int(\1))/g' "$DAP_JSON_C"
sed -i 's/return json_object_new_string(\([^)]*\))/return _json_c_to_dap_json(json_object_new_string(\1))/g' "$DAP_JSON_C"
sed -i 's/return json_object_new_double(\([^)]*\))/return _json_c_to_dap_json(json_object_new_double(\1))/g' "$DAP_JSON_C"
sed -i 's/return json_object_new_boolean(\([^)]*\))/return _json_c_to_dap_json(json_object_new_boolean(\1))/g' "$DAP_JSON_C"
sed -i 's/return json_object_get(\([^)]*\))/return _json_c_to_dap_json(json_object_get(_dap_json_to_json_c(\1)))/g' "$DAP_JSON_C"

# 2. Fix function calls that receive dap_json_t* as first parameter
sed -i 's/json_object_array_add(\([^,]*\), \([^)]*\))/json_object_array_add(_dap_json_array_to_json_c(\1), _dap_json_to_json_c(\2))/g' "$DAP_JSON_C"
sed -i 's/json_object_array_length(\([^)]*\))/json_object_array_length(_dap_json_array_to_json_c(\1))/g' "$DAP_JSON_C"
sed -i 's/json_object_object_add(\([^,]*\), \([^,]*\), \([^)]*\))/json_object_object_add(_dap_json_to_json_c(\1), \2, \3)/g' "$DAP_JSON_C"

# 3. Fix type checking functions  
sed -i 's/json_object_is_type(\([^,]*\), \([^)]*\))/json_object_is_type(_dap_json_to_json_c(\1), \2)/g' "$DAP_JSON_C"

# 4. Fix string conversion functions
sed -i 's/json_object_to_json_string(\([^)]*\))/json_object_to_json_string(_dap_json_to_json_c(\1))/g' "$DAP_JSON_C"
sed -i 's/json_object_to_json_string_ext(\([^,]*\), \([^)]*\))/json_object_to_json_string_ext(_dap_json_to_json_c(\1), \2)/g' "$DAP_JSON_C"

# 5. Fix object operations
sed -i 's/json_object_object_get_ex(\([^,]*\), \([^,]*\), \([^)]*\))/json_object_object_get_ex(_dap_json_to_json_c(\1), \2, \3)/g' "$DAP_JSON_C"
sed -i 's/json_object_object_del(\([^,]*\), \([^)]*\))/json_object_object_del(_dap_json_to_json_c(\1), \2)/g' "$DAP_JSON_C"

# 6. Fix get_type function
sed -i 's/json_object_get_type(\([^)]*\))/json_object_get_type(_dap_json_to_json_c(\1))/g' "$DAP_JSON_C"

# 7. Fix reference counting functions
sed -i 's/json_object_get(\([^)]*\))/json_object_get(_dap_json_to_json_c(\1))/g' "$DAP_JSON_C"

echo "Fixed basic patterns. Now fixing special cases..."

# Fix return values that need conversion
sed -i 's/return l_obj;/return _json_c_to_dap_json(l_obj);/g' "$DAP_JSON_C"

# Fix object adding with array/object parameters - need special handling
sed -i 's/json_object_object_add(_dap_json_to_json_c(\([^)]*\)), \([^,]*\), \(a_value\))/json_object_object_add(_dap_json_to_json_c(\1), \2, _dap_json_to_json_c(\3))/g' "$DAP_JSON_C"
sed -i 's/json_object_object_add(_dap_json_to_json_c(\([^)]*\)), \([^,]*\), \(a_array\))/json_object_object_add(_dap_json_to_json_c(\1), \2, _dap_json_array_to_json_c(\3))/g' "$DAP_JSON_C"

echo "Done! Check the results and test compilation."
