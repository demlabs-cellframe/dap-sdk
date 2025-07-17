# DAP SDK Context System - Quick Reference

## 🚀 Quick Start

### Load Everything
```bash
./context/scripts/load_full_context.sh
```

### Load Specific Module  
```bash
./context/scripts/load_module.sh crypto    # Cryptographic components
./context/scripts/load_module.sh core      # Core DAP functionality  
./context/scripts/load_module.sh net       # Networking components
./context/scripts/load_module.sh other     # Other utilities
```

## 📁 File Structure

```
context/
├── index.json              # Main index (references .local/)
├── context.json             # Project context (references structure.json)
├── structure.json           # Project structure (references modules/)
├── modules/                 # Module-specific structures
│   ├── crypto.json         # Cryptographic components
│   ├── core.json           # Core DAP functionality
│   ├── net.json            # Networking components
│   └── other.json          # Other utilities
├── .local/                  # Current task data (git-ignored)
│   ├── index.json          # Task summary
│   ├── context.json        # Task technical details  
│   └── progress.json       # Task progress tracking
└── scripts/                 # Utility scripts
    ├── load_full_context.sh # Load everything
    └── load_module.sh       # Load specific module
```

## 🔍 Common Queries

### Current Task Info
```bash
jq '.current_task.id' context/.local/index.json
jq '.overall_progress.percentage' context/.local/progress.json
jq '.next_immediate_steps' context/.local/progress.json
```

### Project Structure
```bash
jq '.module_references' context/structure.json
jq '.components.chipmunk.key_files[]' context/modules/crypto.json
jq '.components | keys[]' context/modules/crypto.json
```

### Task-Specific
```bash
jq '.components.chipmunk.current_task' context/modules/crypto.json
jq '.implementation_status.components' context/.local/context.json
```

## ⚡ Workflow

### 1. Starting Work
```bash
./context/scripts/load_full_context.sh  # Get full overview
./context/scripts/load_module.sh crypto # Focus on specific module
```

### 2. Task Updates
Edit only files in `.local/` directory:
- `.local/index.json` - Update task summary
- `.local/context.json` - Update technical details
- `.local/progress.json` - Update progress

### 3. Project Structure Updates  
Edit relevant `modules/*.json` files when project structure changes.

## 🎯 Benefits

- ✅ **No Data Duplication** - Information stored once
- ✅ **Modular Loading** - Load only what you need
- ✅ **Easy Task Switching** - Change only `.local/` files
- ✅ **Git Conflict Prevention** - `.local/` is git-ignored
- ✅ **Scalable** - Add new modules easily

## 🔧 Examples

### Find Chipmunk Files
```bash
jq '.components.chipmunk.key_files[]' context/modules/crypto.json
```

### Check Current Task Progress
```bash
jq '.overall_progress.percentage' context/.local/progress.json
```

### List All Available Modules
```bash
ls context/modules/*.json | sed 's/.*\///;s/\.json//'
```

### Load Current Task Context
```bash
jq '.task.name' context/.local/context.json
jq '.technical_context' context/.local/context.json
``` 