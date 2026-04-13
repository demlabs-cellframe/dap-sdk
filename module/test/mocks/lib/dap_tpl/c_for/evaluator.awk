# C_FOR Construct Evaluator
# Evaluates c_for loops with C array initializer formatting
# Similar to for but generates C-style output with proper comma handling

@include "core/token_tree.awk"
@include "core/variable_context.awk"
@include "core/variable_functions.awk"
@include "core/metadata_helpers.awk"

# Evaluate c_for token (same as for but with C formatting context)
# Parameters:
#   token_id - c_for token ID
# Returns: evaluated token ID with loop iterations expanded
function evaluate_c_for(token_id,    condition) {
    condition = get_condition(token_id)
    if (condition == "") {
        return token_id
    }
    
    # Use the same logic as for evaluator but mark as C formatting mode
    # For now, delegate to for evaluator logic (can be extended later)
    # Set metadata to indicate C formatting
    set_token_metadata(token_id, "output_mode", "c_syntax")
    
    # Call for evaluator (reuse existing logic)
    return evaluate_for(token_id)
}

