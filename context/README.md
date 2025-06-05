# DAP SDK Context Management System

Development context management system for DAP SDK project.

## 🎯 Purpose

This folder contains a system for tracking development context across various tasks in the DAP SDK project. Particularly useful for:

- Tracking progress on cryptographic components
- Preserving context between development sessions
- Organizing files and documentation by tasks
- Preventing git conflicts when multiple developers are working

## 📁 Structure

```
context/
├── index.json           # Global index of all tasks
├── structure.json       # System structure description
├── context.json         # Common project context
├── code_templates.json  # Code templates and patterns
├── coding_guidelines.json # Coding guidelines and best practices
├── project_standards.json # Project documentation standards
├── README.md           # This file
├── scripts/            # Common scripts
│   └── update_context.sh
├── tests/              # Common tests
│   └── validate_context.sh
└── .local/             # Local files (not in git)
    ├── context.json    # Current task context
    ├── progress.json   # Current task progress
    ├── index.json      # Task file index
    ├── scripts/        # Task-specific scripts
    └── tests/          # Task-specific tests
```

## 🔧 Usage

### Viewing Status

```bash
# View global index
cat context/index.json

# View current task
cat context/.local/context.json

# View progress
cat context/.local/progress.json
```

### Updating Context

```bash
# Update timestamps and statistics
./context/scripts/update_context.sh

# Validate structure
./context/tests/validate_context.sh
```

### Working with New Task

1. Update `context/index.json` adding new task
2. Create `context/.local/context.json` for the task
3. Create `context/.local/progress.json` for tracking
4. Update common context in `context/context.json`

## 📋 Current Tasks

### ✅ chipmunk_hots (Completed)
- **Description**: Implementation of HOTS component for Chipmunk signature scheme
- **Status**: 100% completed
- **Files**: 8 files, 3500+ lines of code
- **Tests**: 100% passing

## 🔒 Git Policy

### Tracked in git:
- `context/index.json` - global task index
- `context/structure.json` - structure description
- `context/context.json` - common project context
- `context/code_templates.json` - code templates
- `context/coding_guidelines.json` - coding guidelines
- `context/project_standards.json` - project standards
- `context/scripts/` - common scripts
- `context/tests/` - common tests
- `context/README.md` - documentation

### NOT tracked in git:
- `context/.local/*` - local development files

This prevents conflicts between developers working on different tasks.

## ⚡ Quick Start

1. **System check**:
   ```bash
   ./context/tests/validate_context.sh
   ```

2. **Update context**:
   ```bash
   ./context/scripts/update_context.sh
   ```

3. **View current task**:
   ```bash
   cat context/.local/context.json | jq '.task'
   ```

## 🛠 Requirements

- **jq** (optional) - for convenient JSON handling
- **bash** - for script execution
- **python3** (optional) - for JSON validation

## 📚 Examples

### Adding New Task

```json
// In context/index.json
{
  "tasks": {
    "new_task_id": {
      "id": "new_task_id",
      "name": "Task description",
      "status": "active",
      "progress": 0,
      "start_date": "2025-01-03T15:00:00Z"
    }
  }
}
```

### Updating Progress

```json
// In context/.local/progress.json
{
  "overall_progress": {
    "percentage": 75,
    "status": "active"
  }
}
```

## 🤝 Contributing

1. Always update context when starting work
2. Record progress in local files
3. Update global index when completing tasks
4. Run validation before committing

## 📞 Support

If you have questions about the context system:

1. Check documentation in `context/structure.json`
2. Run validation `./context/tests/validate_context.sh`
3. Look at examples in existing files

---

*DAP SDK Context System - Organized development of cryptographic components* 🔐 