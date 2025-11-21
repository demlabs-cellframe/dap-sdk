@include "core/variable_functions.awk"
@include "variable_functions.awk"
BEGIN {
    # Test 1: Basic normalization (default * -> _STAR)
    result = process_normalize_name("dap_list_t*", "")
    if (result != "dap_list_t_STAR") {
        print "FAIL: Test 1 - Expected 'dap_list_t_STAR', got '" result "'"
        exit 1
    }
    
    # Test 2: Custom replacement for *
    result = process_normalize_name("dap_list_t*", "*|_STAR")
    if (result != "dap_list_t_STAR") {
        print "FAIL: Test 2 - Expected 'dap_list_t_STAR', got '" result "'"
        exit 1
    }
    
    # Test 3: Remove invalid characters
    result = process_normalize_name("test-type@name", "")
    if (result != "test_type_name") {
        print "FAIL: Test 3 - Expected 'test_type_name', got '" result "'"
        exit 1
    }
    
    # Test 4: No special characters
    result = process_normalize_name("simple_name", "")
    if (result != "simple_name") {
        print "FAIL: Test 4 - Expected 'simple_name', got '" result "'"
        exit 1
    }
    
    # Test 5: Multiple asterisks
    result = process_normalize_name("type**", "")
    if (result != "type_STAR_STAR") {
        print "FAIL: Test 5 - Expected 'type_STAR_STAR', got '" result "'"
        exit 1
    }
    
    print "PASS"
}

