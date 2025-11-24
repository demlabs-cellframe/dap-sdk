#include "dap_cpu_monitor.h"
#include "dap_common.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#ifdef DAP_OS_DARWIN
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <mach/host_info.h>
#include <mach/processor_info.h>
#include <mach/vm_map.h>
#include <mach/mach_error.h>
#endif

#define LOG_TAG "dap_cpu_monitor"

#ifndef DAP_OS_DARWIN
static FILE * _proc_stat = NULL;
#endif

static dap_cpu_stats_t _cpu_stats = {0};
static dap_cpu_t _cpu_old_stats[MAX_CPU_COUNT] = {0};
static dap_cpu_t _cpu_summary_old = {0};

#ifdef DAP_OS_DARWIN
// macOS-specific: store previous CPU ticks
static processor_cpu_load_info_t _prev_cpu_load = NULL;
static mach_msg_type_number_t _prev_cpu_count = 0;
#endif

#ifndef DAP_OS_DARWIN
typedef struct proc_stat_line
{
    /* http://man7.org/linux/man-pages/man5/proc.5.html */
    char cpu[10];
    size_t user;
    size_t nice;
    size_t system;
    size_t idle;
    size_t iowait;
    size_t irq;
    size_t softirq;
    size_t steal;
    size_t guest;
    size_t guest_nice;
    size_t total; // summary all parameters
} proc_stat_line_t;
#endif

int dap_cpu_monitor_init()
{
    _cpu_stats.cpu_cores_count = (unsigned) sysconf(_SC_NPROCESSORS_ONLN);

    log_it(L_DEBUG, "Cpu core count: %d", _cpu_stats.cpu_cores_count);

    dap_cpu_get_stats(); // init prev parameters

    return 0;
}

void dap_cpu_monitor_deinit()
{
#ifdef DAP_OS_DARWIN
    if (_prev_cpu_load) {
        vm_deallocate(mach_task_self(), (vm_address_t)_prev_cpu_load, 
                     _prev_cpu_count * sizeof(processor_cpu_load_info_data_t));
        _prev_cpu_load = NULL;
        _prev_cpu_count = 0;
    }
#endif
}

#ifndef DAP_OS_DARWIN
static void _deserialize_proc_stat(char *line, proc_stat_line_t *stat)
{
    sscanf(line,"%s %zu %zu %zu %zu %zu %zu %zu %zu %zu %zu",
           stat->cpu, &stat->user, &stat->nice, &stat->system, &stat->idle,
           &stat->iowait, &stat->irq, &stat->softirq, &stat->steal,
           &stat->guest, &stat->guest_nice);
    stat->total = stat->user + stat->system + stat->idle +
            stat->iowait + stat->irq + stat->softirq +
            stat->steal + stat->guest + stat->guest_nice;
}
#endif

static float _calculate_load(size_t idle_time, size_t prev_idle_time,
                      size_t total_time, size_t prev_total_time)
{
    if (total_time == prev_total_time) {
        return 0.0; // Avoid division by zero
    }
    return (1 - (1.0*idle_time - prev_idle_time) /
            (total_time - prev_total_time)) * 100.0;
}

#ifdef DAP_OS_DARWIN
dap_cpu_stats_t dap_cpu_get_stats()
{
    processor_cpu_load_info_t cpu_load;
    mach_msg_type_number_t cpu_count;
    natural_t num_cpus;
    
    kern_return_t kr = host_processor_info(mach_host_self(),
                                           PROCESSOR_CPU_LOAD_INFO,
                                           &num_cpus,
                                           (processor_info_array_t *)&cpu_load,
                                           &cpu_count);
    
    if (kr != KERN_SUCCESS) {
        log_it(L_ERROR, "host_processor_info failed: %s", mach_error_string(kr));
        return (dap_cpu_stats_t){0};
    }
    
    _cpu_stats.cpu_cores_count = num_cpus;
    
    // Calculate summary statistics
    size_t total_user = 0, total_system = 0, total_idle = 0, total_nice = 0;
    
    for (unsigned i = 0; i < num_cpus && i < MAX_CPU_COUNT; i++) {
        size_t user = cpu_load[i].cpu_ticks[CPU_STATE_USER];
        size_t system = cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM];
        size_t idle = cpu_load[i].cpu_ticks[CPU_STATE_IDLE];
        size_t nice = cpu_load[i].cpu_ticks[CPU_STATE_NICE];
        size_t total = user + system + idle + nice;
        
        _cpu_stats.cpus[i].ncpu = i;
        _cpu_stats.cpus[i].idle_time = idle;
        _cpu_stats.cpus[i].total_time = total;
        
        // Calculate load if we have previous data
        if (_prev_cpu_load && i < _prev_cpu_count) {
            size_t prev_idle = _prev_cpu_load[i].cpu_ticks[CPU_STATE_IDLE];
            size_t prev_total = _prev_cpu_load[i].cpu_ticks[CPU_STATE_USER] +
                               _prev_cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM] +
                               _prev_cpu_load[i].cpu_ticks[CPU_STATE_IDLE] +
                               _prev_cpu_load[i].cpu_ticks[CPU_STATE_NICE];
            
            _cpu_stats.cpus[i].load = _calculate_load(idle, prev_idle, total, prev_total);
        } else {
            _cpu_stats.cpus[i].load = 0.0;
        }
        
        total_user += user;
        total_system += system;
        total_idle += idle;
        total_nice += nice;
    }
    
    // Update summary statistics
    _cpu_stats.cpu_summary.idle_time = total_idle;
    _cpu_stats.cpu_summary.total_time = total_user + total_system + total_idle + total_nice;
    _cpu_stats.cpu_summary.load = _calculate_load(_cpu_stats.cpu_summary.idle_time,
                                                   _cpu_summary_old.idle_time,
                                                   _cpu_stats.cpu_summary.total_time,
                                                   _cpu_summary_old.total_time);
    
    // Save current values for next calculation
    memcpy(&_cpu_summary_old, &_cpu_stats.cpu_summary, sizeof(dap_cpu_t));
    memcpy(_cpu_old_stats, _cpu_stats.cpus, sizeof(dap_cpu_t) * num_cpus);
    
    // Free previous CPU load data and save current
    if (_prev_cpu_load) {
        vm_deallocate(mach_task_self(), (vm_address_t)_prev_cpu_load,
                     _prev_cpu_count * sizeof(processor_cpu_load_info_data_t));
    }
    _prev_cpu_load = cpu_load;
    _prev_cpu_count = cpu_count;
    
    return _cpu_stats;
}
#else
// Linux implementation
dap_cpu_stats_t dap_cpu_get_stats()
{
    _proc_stat = fopen("/proc/stat", "r");

    if(_proc_stat == NULL){
        log_it(L_ERROR, "Сan't open /proc/stat file");
        return (dap_cpu_stats_t){0};
    }

    char *line = NULL;
    proc_stat_line_t stat = {0};

    /** get summary cpu stat **/
    size_t mem_size;
    getline(&line, &mem_size, _proc_stat);
    _deserialize_proc_stat(line, &stat);

    _cpu_stats.cpu_summary.idle_time = stat.idle;
    _cpu_stats.cpu_summary.total_time = stat.total;
    /*********************************************/

    for(unsigned i = 0; i < _cpu_stats.cpu_cores_count; i++) {
        getline(&line, &mem_size, _proc_stat);
        _deserialize_proc_stat(line, &stat);
        _cpu_stats.cpus[i].idle_time = stat.idle;
        _cpu_stats.cpus[i].total_time = stat.total;
        _cpu_stats.cpus[i].ncpu = i;

        _cpu_stats.cpus[i].load = _calculate_load(_cpu_stats.cpus[i].idle_time,
                                                  _cpu_old_stats[i].idle_time,
                                                  _cpu_stats.cpus[i].total_time,
                                                  _cpu_old_stats[i].total_time);
    }

    _cpu_stats.cpu_summary.load = _calculate_load(_cpu_stats.cpu_summary.idle_time,
                    _cpu_summary_old.idle_time,
                    _cpu_stats.cpu_summary.total_time,
                    _cpu_summary_old.total_time);

    memcpy(&_cpu_summary_old, &_cpu_stats.cpu_summary, sizeof (dap_cpu_t));

    memcpy(_cpu_old_stats, _cpu_stats.cpus,
           sizeof (dap_cpu_t) * _cpu_stats.cpu_cores_count);

    fclose(_proc_stat);

    return _cpu_stats;
}
#endif // !DAP_OS_DARWIN
