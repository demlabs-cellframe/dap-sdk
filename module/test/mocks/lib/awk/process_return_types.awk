#!/usr/bin/awk -f
# Process RETURN_TYPES_PAIRS and create RETURN_TYPES and ORIGINAL_TYPES
# Input: newline-separated pairs in format "normalized|original"
# Output: space-separated RETURN_TYPES and ORIGINAL_TYPES associative array format

BEGIN {
    # Read all pairs from stdin
    while ((getline line) > 0) {
        if (line == "" || line ~ /^[ \t]*$/) {
            continue
        }
        
        # Split by pipe separator
        split(line, parts, "|")
        if (length(parts) < 2) {
            continue
        }
        
        normalized = parts[1]
        original = parts[2]
        
        # Remove leading/trailing whitespace
        gsub(/^[ \t]+|[ \t]+$/, "", normalized)
        gsub(/^[ \t]+|[ \t]+$/, "", original)
        
        if (normalized == "" || original == "") {
            continue
        }
        
        # Store unique normalized types
        if (!(normalized in seen_normalized)) {
            seen_normalized[normalized] = 1
            if (return_types == "") {
                return_types = normalized
            } else {
                return_types = return_types " " normalized
            }
        }
        
        # Store original type for normalized type
        original_types[normalized] = original
    }
    
    # Output RETURN_TYPES (space-separated)
    print return_types
}

