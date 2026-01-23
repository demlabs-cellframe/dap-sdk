#!/bin/bash
# Common utilities and variables for DAP Mock Auto-Wrapper modules

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo "============================================================"
    echo "$1"
    echo "============================================================"
}

print_info() {
    echo -e "${BLUE}📋 $1${NC}"
}

print_success() {
    echo -e "${GREEN}✅ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠️  $1${NC}"
}

print_error() {
    echo -e "${RED}❌ $1${NC}"
}

# Initialize paths and load dependencies
init_mock_common() {
    # Get script directory (where dap_mock_autowrap.sh is located)
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    
    # dap_tpl is in test-framework/dap_tpl (one level up from mocks)
    # Templates are in test-framework/templates (same level as dap_tpl)
    DAP_TPL_DIR="$(cd "${script_dir}/../dap_tpl" && pwd)"
    TEMPLATES_DIR="$(cd "${script_dir}/../templates" && pwd)"
    SCRIPTS_DIR="${DAP_TPL_DIR}"
    
    # Mocking extensions for dap_tpl are in mocks/lib/dap_tpl
    MOCKING_EXTENSIONS_DIR="$(cd "${script_dir}/lib/dap_tpl" && pwd)"
    
    # AWK scripts directory for mock processing
    MOCK_AWK_DIR="$(cd "${script_dir}/lib/awk" && pwd)"
    
    # Load template processing functions
    source "${DAP_TPL_DIR}/dap_tpl.sh"
    source "${DAP_TPL_DIR}/dap_tpl_lib.sh"
    
    # Export for child processes
    export SCRIPTS_DIR
    export TEMPLATES_DIR
    export DAP_TPL_DIR
    export MOCKING_EXTENSIONS_DIR
    export MOCK_AWK_DIR
}

# Wrapper for replace_template_placeholders that automatically includes mocking extensions
# Usage: same as replace_template_placeholders, but mocking extensions are included automatically
replace_template_placeholders_with_mocking() {
    local template_file="$1"
    local output_file="$2"
    shift 2
    
    # Call replace_template_placeholders with --extensions-dir pointing to mocking extensions
    replace_template_placeholders "$template_file" "$output_file" --extensions-dir "${MOCKING_EXTENSIONS_DIR}" "$@"
}

# Create temporary file with proper cleanup tracking
# Returns the path to the temporary file
# Note: Caller should add the file to TEMP_FILES array for cleanup
create_temp_file() {
    local prefix="${1:-dap_mock}"
    local tmp_file=$(mktemp -t "${prefix}.XXXXXX")
    echo "$tmp_file"
}

# Cleanup temporary files
cleanup_temp_files() {
    local files=("$@")
    for file in "${files[@]}"; do
        [ -f "$file" ] && rm -f "$file"
    done
}

