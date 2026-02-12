# DAP SDK Context Management System

This directory contains the context management system for DAP SDK development tasks.

## 📁 Architecture Overview

### **Global Files (Project-Level)**
- `index.json` - Main index with references to local task data
- `context.json` - General project information
- `structure.json` - Project structure documentation
- `code_templates.json` - Development standards (auto-loaded)
- `coding_guidelines.json` - Coding guidelines (auto-loaded)  
- `project_standards.json` - Quality standards (auto-loaded)

### **Local Files (Task-Specific in `.local/`)**
- `.local/index.json` - Current task summary
- `.local/context.json` - Detailed task context and technical details
- `.local/progress.json` - Task progress tracking and milestones

### **Utilities**
- `scripts/` - Common scripts
- `tests/` - Validation tests

## 🔄 Data Flow

1. **Global files** contain only project-wide information
2. **Local files** contain all task-specific data
3. **Global files reference** local files (no duplication)
4. **Task changes** require only `.local/` updates

## 🚀 Usage

### Load Full Context
```bash
# Load all context files
./context/scripts/load_full_context.sh

# Or manually:
cat context/index.json           # Project index
cat context/context.json         # Project context  
cat context/.local/index.json    # Current task summary
cat context/.local/context.json  # Current task details
cat context/.local/progress.json # Current task progress
```

### Update Task
To switch or update tasks:
1. Update files in `.local/` directory only
2. No changes needed outside `.local/`
3. Global files automatically reference new data

### Query Specific Data
```bash
# Get current task ID
jq '.current_task.id' context/.local/index.json

# Get task progress
jq '.overall_progress.percentage' context/.local/progress.json

# Get next steps
jq '.next_immediate_steps' context/.local/progress.json
```

## 🎯 Benefits

1. **No Data Duplication** - Each piece of information stored once
2. **Easy Task Switching** - Change only `.local/` files
3. **Git Isolation** - `.local/` is git-ignored for conflict prevention
4. **Consistent References** - Global files always point to current data
5. **Automatic Loading** - Development standards auto-loaded

## 🔧 Git Integration

The `.local/` directory is git-ignored to prevent conflicts between developers working on different tasks. Each developer maintains their own local task context.

---

For questions about the context system, refer to the DAP development team. 