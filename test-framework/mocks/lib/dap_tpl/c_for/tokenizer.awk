# C_FOR Construct Tokenizer
# Parses c_for constructs: {{#c_for key,val in PAIRS}}...{{/c_for}}

@include "core/token_tree.awk"
@include "core/tokenizer_helpers.awk"

function parse_c_for_construct(marker_text, pos, start_line) {
    return tokenize_simple_construct(marker_text, "c_for", 5, "c_for", start_line)
}

