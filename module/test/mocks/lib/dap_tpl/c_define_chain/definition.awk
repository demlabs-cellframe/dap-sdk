# C_DEFINE_CHAIN Construct Definition
# Mock-specific extension for generating C #define chains
# Usage: {{#c_define_chain PREFIX ITEMS|separator}}...{{/c_define_chain}}
# Generates: #define PREFIX_ITEM1 0\n#define PREFIX_ITEM2 1\n...

BEGIN {
    # Register opening pattern
    register_opening_pattern("c_define_chain", "^#c_define_chain[ \t]+(.+)$", "parse_c_define_chain_construct")
    
    # Register closing pattern  
    register_closing_pattern("c_define_chain", "^/c_define_chain$")
    
    # Register evaluator
    register_evaluator("c_define_chain", "evaluate_c_define_chain")
}

