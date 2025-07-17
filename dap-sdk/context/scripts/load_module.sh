#!/bin/bash

# Load Module Script
# Load specific module structure

if [ $# -eq 0 ]; then
    echo "Usage: $0 <module_name>"
    echo
    echo "Available modules:"
    if [ -d "context/modules" ]; then
        for module_file in context/modules/*.json; do
            if [ -f "$module_file" ]; then
                module_name=$(basename "$module_file" .json)
                echo "  - $module_name"
            fi
        done
    fi
    exit 1
fi

MODULE=$1
MODULE_FILE="context/modules/${MODULE}.json"

if [ ! -f "$MODULE_FILE" ]; then
    echo "Error: Module '$MODULE' not found"
    echo "File: $MODULE_FILE"
    exit 1
fi

echo "=== Loading Module: $MODULE ==="
echo "Source: $MODULE_FILE"
echo

cat "$MODULE_FILE" | jq .

echo
echo "✅ Module '$MODULE' loaded!"

# Show some useful queries for this module
case $MODULE in
    "crypto")
        echo
        echo "🔍 Useful queries for crypto module:"
        echo "  Chipmunk files:     jq '.components.chipmunk.key_files[]' $MODULE_FILE"
        echo "  All components:     jq '.components | keys[]' $MODULE_FILE"
        echo "  Current task:       jq '.components.chipmunk.current_task' $MODULE_FILE"
        ;;
    "core")
        echo
        echo "🔍 Useful queries for core module:"
        echo "  Platform variants:  jq '.components.platform.variants' $MODULE_FILE"
        echo "  Common files:       jq '.components.common.key_files[]' $MODULE_FILE"
        ;;
    "net")
        echo
        echo "🔍 Useful queries for net module:"
        echo "  Server modules:     jq '.components.server.submodules | keys[]' $MODULE_FILE"
        echo "  Stream modules:     jq '.components.streaming.submodules' $MODULE_FILE"
        ;;
esac 