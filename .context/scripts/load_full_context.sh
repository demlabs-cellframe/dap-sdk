#!/bin/bash

# Load Full Context Script
# Combines all context files for complete project understanding

echo "=== DAP SDK Context Loader ==="
echo

# Check if we're in the right directory
if [ ! -f "context/index.json" ]; then
    echo "Error: Run this script from the project root directory"
    exit 1
fi

echo "📋 Loading project index..."
echo "Source: context/index.json"
cat context/index.json | jq .
echo

echo "🌍 Loading general project context..."
echo "Source: context/context.json"
cat context/context.json | jq .
echo

echo "🏗️  Loading project structure..."
echo "Source: context/structure.json"
cat context/structure.json | jq .
echo

echo "📁 Loading current task index..."
echo "Source: context/.local/index.json"
if [ -f "context/.local/index.json" ]; then
    cat context/.local/index.json | jq .
else
    echo "No current task index found"
fi
echo

echo "🎯 Loading current task context..."
echo "Source: context/.local/context.json"
if [ -f "context/.local/context.json" ]; then
    cat context/.local/context.json | jq .
else
    echo "No current task context found"
fi
echo

echo "📊 Loading current task progress..."
echo "Source: context/.local/progress.json"
if [ -f "context/.local/progress.json" ]; then
    cat context/.local/progress.json | jq .
else
    echo "No current task progress found"
fi
echo

echo "🔧 Available modules:"
if [ -d "context/modules" ]; then
    for module_file in context/modules/*.json; do
        if [ -f "$module_file" ]; then
            module_name=$(basename "$module_file" .json)
            echo "  - $module_name (load with: jq . $module_file)"
        fi
    done
else
    echo "No modules directory found"
fi
echo

echo "✅ Full context loaded!"
echo 
echo "🚀 Quick commands:"
echo "  Load crypto module:    jq . context/modules/crypto.json"
echo "  Load core module:      jq . context/modules/core.json" 
echo "  Load net module:       jq . context/modules/net.json"
echo "  Current task progress: jq '.overall_progress.percentage' context/.local/progress.json"
echo "  Chipmunk files:        jq '.components.chipmunk.key_files[]' context/modules/crypto.json" 