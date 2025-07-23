/*
 * Python DAP Extension Module
 * Main module initialization with flexible plugin-compatible initialization
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include "python_dap.h"
#include "python_dap_network.h"
#include "dap_common.h"
#include "dap_config.h"
#include "dap_events.h"
#include "dap_client.h"

// Forward declarations
static int ensure_dap_sdk_initialized_internal(void);
static int ensure_dap_sdk_initialized(void);
PyMethodDef* py_dap_plugin_get_methods(void);
static PyObject* py_dap_sdk_init_wrapper(PyObject* self, PyObject* args);
static PyObject* py_dap_sdk_deinit_wrapper(PyObject* self, PyObject* args);

// Global DAP SDK initialization state and parameters
static bool s_dap_sdk_initialized = false;
static pthread_mutex_t s_dap_init_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialization parameters (plugin-configurable)
typedef struct {
    char app_name[256];              // Application name (default: "dap")
    char working_dir[1024];          // Working directory (default: "/opt/dap")
    char config_dir[1024];           // Config directory (default: working_dir/etc)
    char temp_dir[1024];             // Temp directory (default: working_dir/tmp)
    char log_file[1024];             // Log file (default: working_dir/var/log/app_name.log)
    uint32_t events_threads;         // Events threads count (default: 1)
    uint32_t events_timeout;         // Events timeout (default: 10000)
    bool debug_mode;                 // Debug mode (default: false)
} dap_sdk_init_params_t;

static dap_sdk_init_params_t s_init_params = {
    .app_name = "dap",
    .working_dir = "/opt/dap",    // Production default path
    .config_dir = "",  // Will be constructed from working_dir
    .temp_dir = "",    // Will be constructed from working_dir
    .log_file = "",    // Will be constructed from working_dir + app_name
    .events_threads = 1,
    .events_timeout = 10000,
    .debug_mode = false
};

// Public API for external initialization (for plugins)
int dap_sdk_init(const char* a_app_name, 
                const char* a_working_dir,
                const char* a_config_dir,
                const char* a_temp_dir,
                const char* a_log_file,
                uint32_t a_events_threads,
                             uint32_t a_events_timeout,
                             bool a_debug_mode) {
    pthread_mutex_lock(&s_dap_init_mutex);
    
    if (s_dap_sdk_initialized) {
        printf("WARNING: DAP SDK already initialized, ignoring re-initialization\n");
        pthread_mutex_unlock(&s_dap_init_mutex);
        return 0;
    }
    
    // Set initialization parameters
    if (a_app_name) {
        strncpy(s_init_params.app_name, a_app_name, sizeof(s_init_params.app_name) - 1);
    }
    if (a_working_dir) {
        strncpy(s_init_params.working_dir, a_working_dir, sizeof(s_init_params.working_dir) - 1);
    }
    
    // Construct default paths if not provided
    if (a_config_dir && strlen(a_config_dir) > 0) {
        strncpy(s_init_params.config_dir, a_config_dir, sizeof(s_init_params.config_dir) - 1);
    } else {
        snprintf(s_init_params.config_dir, sizeof(s_init_params.config_dir), 
                "%s/etc", s_init_params.working_dir);
    }
    
    if (a_temp_dir && strlen(a_temp_dir) > 0) {
        strncpy(s_init_params.temp_dir, a_temp_dir, sizeof(s_init_params.temp_dir) - 1);
    } else {
        snprintf(s_init_params.temp_dir, sizeof(s_init_params.temp_dir), 
                "%s/tmp", s_init_params.working_dir);
    }
    
    if (a_log_file && strlen(a_log_file) > 0) {
        strncpy(s_init_params.log_file, a_log_file, sizeof(s_init_params.log_file) - 1);
    } else {
        snprintf(s_init_params.log_file, sizeof(s_init_params.log_file), 
                "%s/var/log/%s.log", s_init_params.working_dir, s_init_params.app_name);
    }
    
    s_init_params.events_threads = a_events_threads > 0 ? a_events_threads : 1;
    s_init_params.events_timeout = a_events_timeout > 0 ? a_events_timeout : 10000;
    s_init_params.debug_mode = a_debug_mode;
    
    int result = ensure_dap_sdk_initialized_internal();
    pthread_mutex_unlock(&s_dap_init_mutex);
    return result;
}

// Default initialization (backwards compatibility)
static int ensure_dap_sdk_initialized(void) {
    return dap_sdk_init(NULL, NULL, NULL, NULL, NULL, 0, 0, false);
}

// Internal initialization with current parameters
static int ensure_dap_sdk_initialized_internal(void) {
    if (s_dap_sdk_initialized) {
        return 0;
    }
    
    printf("DEBUG: Starting DAP SDK initialization with params:\n");
    printf("  App name: %s\n", s_init_params.app_name);
    printf("  Working dir: %s\n", s_init_params.working_dir);
    printf("  Config dir: %s\n", s_init_params.config_dir);
    printf("  Temp dir: %s\n", s_init_params.temp_dir);
    printf("  Log file: %s\n", s_init_params.log_file);
    printf("  Events threads: %u\n", s_init_params.events_threads);
    printf("  Debug mode: %s\n", s_init_params.debug_mode ? "true" : "false");
    
    // Create necessary directories
    printf("DEBUG: Creating directories...\n");
    char mkdir_cmd[2048];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s %s %s/var/log", 
             s_init_params.config_dir, s_init_params.temp_dir, s_init_params.working_dir);
    system(mkdir_cmd);
    
    // Initialize DAP common systems FIRST
    printf("DEBUG: Calling dap_common_init...\n");
    int result = dap_common_init(s_init_params.app_name, s_init_params.log_file);
    printf("DEBUG: dap_common_init returned: %d\n", result);
    if (result != 0) {
        printf("ERROR: dap_common_init failed with code %d\n", result);
        return result;
    }
    
    // Initialize DAP config system SECOND
    printf("DEBUG: Calling dap_config_init with dir: %s\n", s_init_params.config_dir);
    result = dap_config_init(s_init_params.config_dir);
    printf("DEBUG: dap_config_init returned: %d\n", result);
    if (result != 0) {
        printf("ERROR: dap_config_init failed with code %d\n", result);
        return result;
    }
    
    // Initialize DAP events system THIRD
    printf("DEBUG: Calling dap_events_init with %u threads, %u timeout...\n", 
           s_init_params.events_threads, s_init_params.events_timeout);
    result = dap_events_init(s_init_params.events_threads, s_init_params.events_timeout);
    printf("DEBUG: dap_events_init returned: %d\n", result);
    if (result != 0) {
        printf("ERROR: dap_events_init failed with code %d\n", result);
        return result;
    }
    
    // Start events system
    printf("DEBUG: Calling dap_events_start...\n");
    dap_events_start();
    printf("DEBUG: dap_events_start completed\n");
    
    // Initialize client system after events are ready
    printf("DEBUG: Calling dap_client_init...\n");
    result = dap_client_init();
    printf("DEBUG: dap_client_init returned: %d\n", result);
    if (result != 0) {
        printf("WARNING: dap_client_init failed with code %d, continuing anyway\n", result);
        // Don't fail the whole SDK init if client init fails
    }
    
    printf("DEBUG: DAP SDK initialization completed successfully!\n");
    s_dap_sdk_initialized = true;
    return 0;
}

// Public API for cleanup (for plugins)
void dap_sdk_deinit(void) {
    pthread_mutex_lock(&s_dap_init_mutex);
    
    if (!s_dap_sdk_initialized) {
        pthread_mutex_unlock(&s_dap_init_mutex);
        return;
    }
    
    printf("DEBUG: Deinitializing DAP SDK...\n");
    
    // Proper cleanup sequence for DAP SDK
    // Note: Some functions may not be available or needed depending on DAP SDK version
    // dap_events_stop();
    // dap_events_deinit();
    // dap_config_deinit();
    // dap_common_deinit();
    
    s_dap_sdk_initialized = false;
    pthread_mutex_unlock(&s_dap_init_mutex);
    
    printf("DEBUG: DAP SDK deinitialized\n");
}

// Helper function to count methods in a PyMethodDef array
static int count_methods(PyMethodDef* methods) {
    if (!methods) return 0;
    
    int count = 0;
    while (methods[count].ml_name != NULL) {
        count++;
    }
    return count;
}

// Helper function to concatenate method arrays
static PyMethodDef* concatenate_methods(void) {
    fprintf(stderr, "DEBUG: concatenate_methods started\n");
    fflush(stderr);
    
    // Get method arrays from all modules
    PyMethodDef* common_methods = py_dap_common_get_methods();
    PyMethodDef* config_methods = py_dap_config_get_methods();
    PyMethodDef* system_methods = py_dap_system_get_methods();
    PyMethodDef* logging_methods = py_dap_logging_get_methods();
    PyMethodDef* time_methods = py_dap_time_get_methods();
    PyMethodDef* server_methods = py_dap_server_get_methods();
    PyMethodDef* client_methods = py_dap_client_get_methods();
    PyMethodDef* events_methods = py_dap_events_get_methods();
    PyMethodDef* network_methods = py_dap_network_get_methods();
    
    fprintf(stderr, "DEBUG: About to call py_dap_plugin_get_methods\n");
    fflush(stderr);
    PyMethodDef* plugin_methods = py_dap_plugin_get_methods();
    fprintf(stderr, "DEBUG: py_dap_plugin_get_methods returned %p\n", plugin_methods);
    fflush(stderr);
    
    if (plugin_methods && plugin_methods[0].ml_name) {
        fprintf(stderr, "DEBUG: first plugin method name = '%s'\n", plugin_methods[0].ml_name);
        fflush(stderr);
    } else {
        fprintf(stderr, "DEBUG: plugin_methods is NULL or empty\n");
        fflush(stderr);
    }
    
    // Count total methods needed
    size_t total_count = 0;
    
    #define COUNT_METHODS(methods) \
        if (methods) { \
            size_t count = count_methods(methods); \
            fprintf(stderr, "DEBUG: %s has %zu methods\n", #methods, count); \
            total_count += count; \
        }
        
    COUNT_METHODS(common_methods);
    COUNT_METHODS(config_methods);
    COUNT_METHODS(system_methods);
    COUNT_METHODS(logging_methods);
    COUNT_METHODS(time_methods);
    COUNT_METHODS(server_methods);
    COUNT_METHODS(client_methods);
    COUNT_METHODS(events_methods);
    COUNT_METHODS(network_methods);
    COUNT_METHODS(plugin_methods);
    
    fprintf(stderr, "DEBUG: Total methods count: %zu\n", total_count);
    fflush(stderr);
    
    // Allocate memory for all methods + sentinel
    PyMethodDef* all_methods = malloc((total_count + 1) * sizeof(PyMethodDef));
    if (!all_methods) {
        fprintf(stderr, "DEBUG: malloc failed!\n");
        fflush(stderr);
        return NULL;
    }
    
    size_t current_index = 0;
    
    #define COPY_METHODS(methods) \
        if (methods) { \
            size_t count = count_methods(methods); \
            fprintf(stderr, "DEBUG: copying %zu methods from %s at index %zu\n", count, #methods, current_index); \
            memcpy(&all_methods[current_index], methods, count * sizeof(PyMethodDef)); \
            current_index += count; \
        }
    
    COPY_METHODS(common_methods);
    COPY_METHODS(config_methods);
    COPY_METHODS(system_methods);
    COPY_METHODS(logging_methods);
    COPY_METHODS(time_methods);
    COPY_METHODS(server_methods);
    COPY_METHODS(client_methods);
    COPY_METHODS(events_methods);
    COPY_METHODS(network_methods);
    COPY_METHODS(plugin_methods);
    
    // Add sentinel
    all_methods[current_index].ml_name = NULL;
    all_methods[current_index].ml_meth = NULL;
    all_methods[current_index].ml_flags = 0;
    all_methods[current_index].ml_doc = NULL;
    
    fprintf(stderr, "DEBUG: concatenate_methods completed, returning %p with %zu methods\n", all_methods, current_index);
    fflush(stderr);
    
    return all_methods;
}

// Module definition
static struct PyModuleDef python_dap_module = {
    PyModuleDef_HEAD_INIT,
    "python_dap",                                       // Module name
    "Python DAP SDK bindings",       // Module documentation
    -1,                                                 // Size of per-interpreter state
    NULL                                                // Method table (will be set dynamically)
};

// Module initialization function
PyMODINIT_FUNC PyInit_python_dap(void) {
    fprintf(stderr, "DEBUG: PyInit_python_dap started\n");
    fflush(stderr);
    
    // Try to initialize DAP SDK, but don't fail module loading on error
    fprintf(stderr, "DEBUG: About to call ensure_dap_sdk_initialized()\n");
    fflush(stderr);
    int init_result = ensure_dap_sdk_initialized();
    if (init_result != 0) {
        fprintf(stderr, "WARNING: DAP SDK initialization failed with code %d, but continuing module load\n", init_result);
        fflush(stderr);
        // Don't return NULL - continue with module loading
    } else {
        fprintf(stderr, "DEBUG: ensure_dap_sdk_initialized() succeeded\n");
        fflush(stderr);
    }
    
    // Dynamically build method table
    fprintf(stderr, "DEBUG: About to call concatenate_methods()\n");
    fflush(stderr);
    PyMethodDef* all_methods = concatenate_methods();
    fprintf(stderr, "DEBUG: concatenate_methods() returned %p\n", all_methods);
    fflush(stderr);
    if (!all_methods) {
        fprintf(stderr, "DEBUG: concatenate_methods() failed - returned NULL\n");
        fflush(stderr);
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate method table");
        return NULL;
    }
    fprintf(stderr, "DEBUG: concatenate_methods() succeeded\n");
    fflush(stderr);
    
    // Create module definition
    static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "python_dap",           // Module name
        "DAP SDK Python bindings",  // Module docstring
        -1,                     // Module size
        NULL,                   // Placeholder for methods (will be set below)
        NULL,                   // Slots
        NULL,                   // Traverse
        NULL,                   // Clear
        NULL                    // Free
    };
    
    // Set the methods dynamically
    moduledef.m_methods = all_methods;
    
    fprintf(stderr, "DEBUG: About to call PyModule_Create\n");
    fflush(stderr);
    PyObject* module = PyModule_Create(&moduledef);
    if (!module) {
        fprintf(stderr, "DEBUG: PyModule_Create failed\n");
        fflush(stderr);
        free(all_methods);
        return NULL;
    }
    fprintf(stderr, "DEBUG: PyModule_Create succeeded\n");
    fflush(stderr);
    
    // Call module-specific init functions
    fprintf(stderr, "DEBUG: Calling module init functions\n");
    fflush(stderr);
    if (py_dap_common_module_init(module) != 0 ||
        py_dap_config_module_init(module) != 0 ||
        py_dap_system_module_init(module) != 0 ||
        py_dap_logging_module_init(module) != 0 ||
        py_dap_time_module_init(module) != 0 ||
        py_dap_server_module_init(module) != 0 ||
        py_dap_client_module_init(module) != 0 ||
        py_dap_events_module_init(module) != 0 ||
        py_dap_network_module_init(module) != 0) {
        
        fprintf(stderr, "WARNING: Some module init functions failed, but continuing\n");
        fflush(stderr);
        // Don't fail the entire module load
    }
    
    fprintf(stderr, "DEBUG: PyInit_python_dap completed successfully\n");
    fflush(stderr);
    return module;
}

// ============================================================================
// Python wrapper functions for plugin-compatible DAP SDK API
// ============================================================================

static PyObject* py_dap_sdk_init_wrapper(PyObject* self, PyObject* args) {
    const char* app_name = NULL;
    const char* working_dir = NULL; 
    const char* config_dir = NULL;
    const char* temp_dir = NULL;
    const char* log_file = NULL;
    uint32_t events_threads = 0;
    uint32_t events_timeout = 0;
    int debug_mode = 0;
    
    if (!PyArg_ParseTuple(args, "|sssssiip", 
                         &app_name, &working_dir, &config_dir, &temp_dir, 
                         &log_file, &events_threads, &events_timeout, &debug_mode)) {
        return NULL;
    }
    
    int result = dap_sdk_init(app_name, working_dir, config_dir, temp_dir,
                             log_file, events_threads, events_timeout, 
                             debug_mode != 0);
    
    if (result != 0) {
        PyErr_Format(PyExc_RuntimeError, "DAP SDK initialization failed with code %d", result);
        return NULL;
    }
    
    Py_RETURN_NONE;
}

static PyObject* py_dap_sdk_deinit_wrapper(PyObject* self, PyObject* args) {
    dap_sdk_deinit();
    Py_RETURN_NONE;
}

// Method definitions for plugin API
static PyMethodDef plugin_api_methods[] = {
    {"dap_sdk_init", py_dap_sdk_init_wrapper, METH_VARARGS,
     "Initialize DAP SDK with custom parameters for plugin use"},
    {"dap_sdk_deinit", py_dap_sdk_deinit_wrapper, METH_NOARGS,
     "Deinitialize DAP SDK (for plugin cleanup)"},
    {NULL, NULL, 0, NULL}  // Sentinel
};

// Function to get plugin API methods (for concatenate_methods)
PyMethodDef* py_dap_plugin_get_methods(void) {
    // Simple test - print address and first method name
    static bool debug_printed = false;
    if (!debug_printed) {
        printf("DEBUG: py_dap_plugin_get_methods called\n");
        fflush(stdout);
        printf("DEBUG: plugin_api_methods address = %p\n", plugin_api_methods);
        fflush(stdout);
        if (plugin_api_methods && plugin_api_methods[0].ml_name) {
            printf("DEBUG: first method name = %s\n", plugin_api_methods[0].ml_name);
            fflush(stdout);
        }
        debug_printed = true;
    }
    
    return plugin_api_methods;
} 