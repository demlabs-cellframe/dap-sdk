# C_DEFINE_CHAIN Construct Tokenizer
# Parses c_define_chain constructs: {{#c_define_chain PREFIX ITEMS}}...{{/c_define_chain}}

@include "core/token_tree.awk"
@include "core/tokenizer_helpers.awk"

function parse_c_define_chain_construct(marker_text, pos, start_line) {
    return tokenize_simple_construct(marker_text, "c_define_chain", 14, "c_define_chain", start_line)
}

