@include "core/variable_functions.awk"
@include "variable_functions.awk"
BEGIN {
    # Test 1: Remove invalid characters (both - and @ become _)
    result = process_sanitize_name("test-type@name", "")
    if (result != "test_type_name") {
        print "FAIL: Test 1 - Expected 'test_type_name', got '" result "'"
        exit 1
    }
    
    # Test 2: Keep valid characters
    result = process_sanitize_name("valid_name123", "")
    if (result != "valid_name123") {
        print "FAIL: Test 2 - Expected 'valid_name123', got '" result "'"
        exit 1
    }
    
    # Test 3: Replace spaces
    result = process_sanitize_name("test name", "")
    if (result != "test_name") {
        print "FAIL: Test 3 - Expected 'test_name', got '" result "'"
        exit 1
    }
    
    # Test 4: Multiple invalid characters
    result = process_sanitize_name("a-b@c#d$e", "")
    if (result != "a_b_c_d_e") {
        print "FAIL: Test 4 - Expected 'a_b_c_d_e', got '" result "'"
        exit 1
    }
    
    print "PASS"
}

