# C_IFDEF Construct Tokenizer
# Parses c_ifdef constructs: {{#c_ifdef FEATURE}}...{{/c_ifdef}}

@include "core/token_tree.awk"

function parse_c_ifdef_construct(marker_text, pos, start_line,    condition, metadata, token_id) {
    if (match(marker_text, /^#c_ifdef[ \t]+(.+)$/)) {
        condition = substr(marker_text, RSTART + 9)
        gsub(/^[ \t]+|[ \t]+$/, "", condition)
        
        delete metadata
        metadata["condition"] = condition
        metadata["start_line"] = start_line
        
        return create_token("c_ifdef", "", metadata)
    }
    return 0
}

