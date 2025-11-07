# Replace placeholder with simple value
# Usage: awk -v placeholder="{{VAR}}" -v val="value" -f replace_template_value.awk input.txt
BEGIN {
    # Escape backslashes for gsub (forward slashes don't need escaping in gsub)
    gsub(/\\/, "\\\\", val)
}
{
    # Replace all occurrences of placeholder using manual replacement
    # to avoid regex issues with special characters
    result = ""
    remaining = $0
    while ((pos = index(remaining, placeholder)) > 0) {
        result = result substr(remaining, 1, pos - 1) val
        remaining = substr(remaining, pos + length(placeholder))
    }
    $0 = result remaining
    print
}

