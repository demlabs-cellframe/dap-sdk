# C_IFDEF Construct Evaluator
# Evaluates c_ifdef constructs and generates C preprocessor directives
# Outputs: #ifdef FEATURE ... #elif OTHER ... #else ... #endif

@include "core/token_tree.awk"
@include "core/variable_context.awk"
@include "core/condition_parser.awk"
@include "core/renderer.awk"

function evaluate_c_ifdef(token_id,    condition, condition_result, child_count, i, child_id, child_type, elif_tokens, elif_conditions, elif_count, else_token_id, result_token_id, metadata, elif_condition, elif_result, elif_found, j, elif_token_id, elif_child_count, else_child_count, output) {
    condition = get_token_metadata(token_id, "condition")
    if (condition == "") {
        return token_id
    }
    
    # Evaluate condition using if condition evaluator (reuse logic)
    # Check if variable is defined and truthy
    condition_result = evaluate_if_condition(condition)
    
    # Find elif and else tokens
    child_count = get_children_count(token_id)
    delete elif_tokens
    delete elif_conditions
    elif_count = 0
    else_token_id = 0
    
    for (i = 1; i <= child_count; i++) {
        child_id = get_child(token_id, i)
        child_type = get_token_type(child_id)
        if (child_type == "c_elif") {
            elif_count++
            elif_tokens[elif_count] = child_id
            elif_conditions[elif_count] = get_token_metadata(child_id, "condition")
        } else if (child_type == "c_else") {
            else_token_id = child_id
        }
    }
    
    # Build C preprocessor output
    output = ""
    
    if (condition_result) {
        # True - output #ifdef and if branch content
        output = output "#ifdef " condition "\n"
        # Render if branch children
        first_child = 1
        for (i = 1; i <= child_count; i++) {
            child_id = get_child(token_id, i)
            child_type = get_token_type(child_id)
            if (child_type != "c_elif" && child_type != "c_else") {
                child_output = render_token(child_id)
                # Remove leading newline from first child to avoid double newline after #ifdef
                if (first_child && child_output ~ /^\n/) {
                    child_output = substr(child_output, 2)
                }
                output = output child_output
                first_child = 0
            }
        }
        # Close with #endif
        output = output "#endif\n"
    } else {
        # False - check elif branches
        elif_found = 0
        for (i = 1; i <= elif_count; i++) {
            elif_condition = elif_conditions[i]
            elif_result = evaluate_if_condition(elif_condition)
            if (elif_result) {
                # This elif is true
                output = output "#ifdef " condition "\n"
                output = output "#elifdef " elif_condition "\n"
                elif_token_id = elif_tokens[i]
                elif_child_count = get_children_count(elif_token_id)
                first_child = 1
                for (j = 1; j <= elif_child_count; j++) {
                    child_id = get_child(elif_token_id, j)
                    child_output = render_token(child_id)
                    # Remove leading newline from first child to avoid double newline after #elifdef
                    if (first_child && child_output ~ /^\n/) {
                        child_output = substr(child_output, 2)
                    }
                    output = output child_output
                    first_child = 0
                }
                output = output "#endif\n"
                elif_found = 1
                break
            }
        }
        
        # If no elif matched, use else if present
        if (!elif_found) {
            if (else_token_id > 0) {
                output = output "#ifdef " condition "\n"
                output = output "#else\n"
                else_child_count = get_children_count(else_token_id)
                first_child = 1
                for (i = 1; i <= else_child_count; i++) {
                    child_id = get_child(else_token_id, i)
                    child_output = render_token(child_id)
                    # Remove leading newline from first child to avoid double newline after #else
                    if (first_child && child_output ~ /^\n/) {
                        child_output = substr(child_output, 2)
                    }
                    output = output child_output
                    first_child = 0
                }
                output = output "#endif\n"
            } else {
                # No else - output #ifdef with empty content
                output = output "#ifdef " condition "\n"
                output = output "#endif\n"
            }
        }
    }
    
    # Note: output already ends with \n from "#endif\n" above
    # The test expects the output to end with exactly one \n
    # (the one from #endif\n), so we don't add another one here
    
    # Create text token with C preprocessor output
    delete metadata
    result_token_id = create_token("text", output, metadata)
    return result_token_id
}

