# C_STRUCT Construct Evaluator
# Generates C typedef struct: typedef struct {...} StructName_t;

@include "core/token_tree.awk"
@include "core/variable_context.awk"
@include "core/variable_functions.awk"
@include "core/metadata_helpers.awk"
@include "core/renderer.awk"

# Evaluate c_struct token
# Parameters:
#   token_id - c_struct token ID
# Returns: evaluated token ID with typedef struct generated
function evaluate_c_struct(token_id,    condition, struct_name, child_count, i, child_id, result_token_id, metadata, header_line, footer_line, text_token_id) {
    condition = get_condition(token_id)
    if (condition == "") {
        return token_id
    }
    
    struct_name = condition
    gsub(/^[ \t]+|[ \t]+$/, "", struct_name)
    
    # Create result token
    delete metadata
    result_token_id = create_empty_result()
    
    # Generate header: typedef struct {
    header_line = "typedef struct {\n"
    text_token_id = create_text_result(header_line)
    add_child(result_token_id, text_token_id)
    
    # Add children (struct fields)
    child_count = get_children_count(token_id)
    for (i = 1; i <= child_count; i++) {
        child_id = get_child(token_id, i)
        evaluated_child_id = evaluate_token(child_id)
        if (evaluated_child_id > 0) {
            add_child(result_token_id, evaluated_child_id)
        }
    }
    
    # Generate footer: } StructName_t;
    footer_line = "} " struct_name "_t;\n"
    text_token_id = create_text_result(footer_line)
    add_child(result_token_id, text_token_id)
    
    return result_token_id
}

