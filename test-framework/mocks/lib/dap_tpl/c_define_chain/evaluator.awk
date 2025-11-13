# C_DEFINE_CHAIN Construct Evaluator
# Generates C #define chains: #define PREFIX_ITEM1 0\n#define PREFIX_ITEM2 1\n...

@include "core/token_tree.awk"
@include "core/variable_context.awk"
@include "core/variable_functions.awk"
@include "core/metadata_helpers.awk"
@include "core/renderer.awk"

# Evaluate c_define_chain token
# Parameters:
#   token_id - c_define_chain token ID
# Returns: evaluated token ID with #define chain generated
function evaluate_c_define_chain(token_id,    condition, prefix, items_str, separator, items, item_count, i, item, result_token_id, metadata, define_line) {
    condition = get_condition(token_id)
    if (condition == "") {
        return token_id
    }
    
    # Parse condition: "PREFIX ITEMS|separator" or "PREFIX ITEMS"
    separator = "|"  # Default separator
    if (match(condition, /^(.+)[ \t]+(.+)$/)) {
        prefix = substr(condition, 1, RSTART - 1)
        gsub(/^[ \t]+|[ \t]+$/, "", prefix)
        items_str = substr(condition, RSTART + RLENGTH)
        gsub(/^[ \t]+|[ \t]+$/, "", items_str)
        
        # Check for explicit separator: ITEMS|separator
        if (match(items_str, /^(.+)\|(.+)$/)) {
            items_str = substr(items_str, 1, RSTART - 1)
            separator = substr(items_str, RSTART + length(items_str) + 2)
            gsub(/^[ \t]+|[ \t]+$/, "", separator)
        }
    } else {
        return token_id
    }
    
    # Get items from variable if items_str is a variable name, otherwise use as literal
    if (items_str in ENVIRON || items_str in vars) {
        items_str = get_var(items_str)
    }
    
    # Split items by separator
    if (separator == "|") {
        item_count = split(items_str, items, /\|/)
    } else {
        escaped_sep = separator
        gsub(/[.*+?^${}()|[\]\\]/, "\\\\&", escaped_sep)
        item_count = split(items_str, items, escaped_sep)
    }
    
    # Create result token
    delete metadata
    result_token_id = create_empty_result()
    
    # Generate #define lines
    for (i = 1; i <= item_count; i++) {
        item = items[i]
        gsub(/^[ \t]+|[ \t]+$/, "", item)
        if (item == "") {
            continue
        }
        
        # Generate: #define PREFIX_ITEM (i-1)
        define_line = "#define " prefix "_" item " " (i - 1)
        
        # Add as text token
        text_token_id = create_text_result(define_line "\n")
        add_child(result_token_id, text_token_id)
    }
    
    return result_token_id
}

