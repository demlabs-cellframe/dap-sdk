#include "dap_process_memory.h"
#include "dap_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef DAP_OS_DARWIN
#include <mach/mach.h>
#include <mach/task_info.h>
#include <mach/mach_init.h>
#endif

#define LOG_TAG "dap_process_mem"

#ifndef DAP_OS_DARWIN
#define MAX_LINE_LENGTH 128
#endif

#ifdef DAP_OS_DARWIN
/**
 * @brief Get process memory information on macOS using Mach API
 * @param task Mach task port
 * @return dap_process_memory_t structure with memory info
 */
static dap_process_memory_t _get_process_memory_mach(mach_port_t task)
{
    dap_process_memory_t proc_mem = {0, 0};
    struct mach_task_basic_info info;
    mach_msg_type_number_t info_count = MACH_TASK_BASIC_INFO_COUNT;
    
    kern_return_t kr = task_info(task,
                                  MACH_TASK_BASIC_INFO,
                                  (task_info_t)&info,
                                  &info_count);
    
    if (kr != KERN_SUCCESS) {
        log_it(L_WARNING, "task_info failed: %s", mach_error_string(kr));
        return proc_mem;
    }
    
    // Convert bytes to KB
    proc_mem.vsz = info.virtual_size / 1024;
    proc_mem.rss = info.resident_size / 1024;
    
    return proc_mem;
}

/**
 * @brief Get process memory by PID on macOS
 * @param pid Process ID
 * @return dap_process_memory_t structure with memory info
 */
static dap_process_memory_t _get_process_memory_by_pid_mach(pid_t pid)
{
    dap_process_memory_t proc_mem = {0, 0};
    mach_port_t task;
    
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
    if (kr != KERN_SUCCESS) {
        log_it(L_WARNING, "task_for_pid failed for pid %d: %s", pid, mach_error_string(kr));
        return proc_mem;
    }
    
    proc_mem = _get_process_memory_mach(task);
    
    // Clean up
    mach_port_deallocate(mach_task_self(), task);
    
    return proc_mem;
}
#else
// Linux implementation using /proc filesystem

static size_t _parse_size_line(char *line) {
    // This assumes that a digit will be found and the line ends in " Kb".
    size_t i = strlen(line);
    const char *p = line;
    while (*p < '0' || *p > '9') p++;
    line[i - 3] = '\0';
    i = (size_t)atol(p);
    return i;
}

static dap_process_memory_t _get_process_memory(const char* proc_file_path)
{
    FILE *file = fopen(proc_file_path, "r");

    if(file == NULL) {
        log_it(L_WARNING, "Cant open proc file");
        return (dap_process_memory_t){0,0};
    }

    char line[MAX_LINE_LENGTH];
    dap_process_memory_t proc_mem = {0};

    while (fgets(line, MAX_LINE_LENGTH, file) != NULL) {
        if (strncmp(line, "VmSize:", 7) == 0) {
            proc_mem.vsz = _parse_size_line(line);
        }

        if (strncmp(line, "VmRSS:", 6) == 0) {
            proc_mem.rss = _parse_size_line(line);
        }

        if (proc_mem.rss != 0 && proc_mem.vsz != 0)
            break;
    }

    fclose(file);

    if(proc_mem.vsz == 0 || proc_mem.rss == 0)
        log_it(L_WARNING, "Getting memory statistics failed");

    return proc_mem;
}
#endif // !DAP_OS_DARWIN

dap_process_memory_t get_proc_mem_current(void)
{
#ifdef DAP_OS_DARWIN
    return _get_process_memory_mach(mach_task_self());
#else
    return _get_process_memory("/proc/self/status");
#endif
}

dap_process_memory_t get_proc_mem_by_pid(pid_t pid)
{
#ifdef DAP_OS_DARWIN
    return _get_process_memory_by_pid_mach(pid);
#else
    char buf[64];
    snprintf(buf, sizeof(buf), "/proc/%d/status", pid);
    return _get_process_memory(buf);
#endif
}
