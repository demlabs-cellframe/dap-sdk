# C_IFDEF Construct Definition
# Mock-specific extension for C preprocessor #ifdef directives
# Usage: {{#c_ifdef FEATURE_NAME}}...{{#c_elif OTHER_FEATURE}}...{{#c_else}}...{{/c_ifdef}}

BEGIN {
    # Register opening pattern
    register_opening_pattern("c_ifdef", "^#c_ifdef[ \t]+(.+)$", "parse_c_ifdef_construct")
    
    # Register closing pattern  
    register_closing_pattern("c_ifdef", "^/c_ifdef$")
    
    # Register special markers (branch markers)
    register_special_marker("c_ifdef", "^#c_elif[ \t]+(.+)$", "c_elif")
    register_special_marker("c_ifdef", "^#c_else$", "c_else")
    
    # Register branch parser function
    register_branch_parser("c_ifdef", "parse_c_ifdef_branches")
    
    # Register evaluator
    register_evaluator("c_ifdef", "evaluate_c_ifdef")
}

