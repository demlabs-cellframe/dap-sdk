# C_STRUCT Construct Definition
# Mock-specific extension for generating C typedef struct
# Usage: {{#c_struct StructName}}...{{/c_struct}}
# Generates: typedef struct {...} StructName_t;

BEGIN {
    # Register opening pattern
    register_opening_pattern("c_struct", "^#c_struct[ \t]+(.+)$", "parse_c_struct_construct")
    
    # Register closing pattern  
    register_closing_pattern("c_struct", "^/c_struct$")
    
    # Register evaluator
    register_evaluator("c_struct", "evaluate_c_struct")
}

