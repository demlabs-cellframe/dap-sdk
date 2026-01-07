@include "core/variable_functions.awk"
@include "variable_functions.awk"
BEGIN {
    # Test 1: Escape * to _STAR
    result = process_escape_char("dap_list_t*", "*|_STAR")
    if (result != "dap_list_t_STAR") {
        print "FAIL: Test 1 - Expected 'dap_list_t_STAR', got '" result "'"
        exit 1
    }
    
    # Test 2: Escape @ to _AT
    result = process_escape_char("test@name", "@|_AT")
    if (result != "test_ATname") {
        print "FAIL: Test 2 - Expected 'test_ATname', got '" result "'"
        exit 1
    }
    
    # Test 3: Empty replacement
    result = process_escape_char("test-name", "-|")
    if (result != "testname") {
        print "FAIL: Test 3 - Expected 'testname', got '" result "'"
        exit 1
    }
    
    # Test 4: Multiple occurrences
    result = process_escape_char("a*b*c", "*|_STAR")
    if (result != "a_STARb_STARc") {
        print "FAIL: Test 4 - Expected 'a_STARb_STARc', got '" result "'"
        exit 1
    }
    
    # Test 5: Empty func_arg (should return unchanged)
    result = process_escape_char("test*name", "")
    if (result != "test*name") {
        print "FAIL: Test 5 - Expected 'test*name', got '" result "'"
        exit 1
    }
    
    print "PASS"
}

