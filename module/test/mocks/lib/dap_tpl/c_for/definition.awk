# C_FOR Construct Definition
# Mock-specific extension for C-formatted for loops
# Usage: {{#c_for key,val in PAIRS}}...{{/c_for}}
# Generates C array initializers with proper comma handling

BEGIN {
    # Register opening pattern
    register_opening_pattern("c_for", "^#c_for[ \t]+(.+)$", "parse_c_for_construct")
    
    # Register closing pattern  
    register_closing_pattern("c_for", "^/c_for$")
    
    # Register evaluator
    register_evaluator("c_for", "evaluate_c_for")
}

