# Replace placeholder with content from file
# Usage: awk -v placeholder="{{VAR}}" -f replace_template_file.awk -v val_file="file.txt" input.txt
BEGIN {
    val = ""
    while ((getline line < val_file) > 0) {
        if (val != "") val = val "\n"
        val = val line
    }
    close(val_file)
    # Escape special regex characters in placeholder for safe matching
    escaped_placeholder = placeholder
    gsub(/[{}]/, "\\\\&", escaped_placeholder)
}
{
    # Use manual replacement to avoid regex issues with special characters
    # Find placeholder position and replace manually
    pos = index($0, placeholder)
    if (pos > 0) {
        before = substr($0, 1, pos - 1)
        after = substr($0, pos + length(placeholder))
        $0 = before val after
    }
    print
}

