#include "dap_cpu_monitor.h"
#include "dap_common.h"

#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

#ifdef DAP_OS_ANDROID
// Android-compatible getline implementation
static ssize_t android_getline(char **a_lineptr, size_t *a_n, FILE *a_stream) {
    if (!a_lineptr || !a_n || !a_stream) return -1;
    
    if (*a_lineptr == NULL) {
        *a_n = 256;
        *a_lineptr = DAP_NEW_SIZE(char, *a_n);
        if (!*a_lineptr) return -1;
    }
    
    if (fgets(*a_lineptr, *a_n, a_stream) == NULL) {
        return -1;
    }
    
    return strlen(*a_lineptr);
}
#define getline android_getline
#endif
#include <unistd.h>
#include <string.h>

#define LOG_TAG "dap_cpu_monitor"

static FILE * _proc_stat = NULL;
static dap_cpu_stats_t _cpu_stats = {0};
static dap_cpu_t _cpu_old_stats[MAX_CPU_COUNT] = {0};
static dap_cpu_t _cpu_summary_old = {0};

typedef struct proc_stat_line
{
    /* http://man7.org/linux/man-pages/man5/proc.5.html */
    char cpu[10];
    uint64_t user;
    uint64_t nice;
    uint64_t system;
    uint64_t idle;
    uint64_t iowait;
    uint64_t irq;
    uint64_t softirq;
    uint64_t steal;
    uint64_t guest;
    uint64_t guest_nice;
    uint64_t total; // summary all parameters
} proc_stat_line_t;

int dap_cpu_monitor_init()
{
    long l_cpu_cores_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (l_cpu_cores_count <= 0) {
        log_it(L_WARNING, "Failed to detect CPU count, fallback to 1");
        l_cpu_cores_count = 1;
    }
    if (l_cpu_cores_count > MAX_CPU_COUNT) {
        log_it(L_WARNING, "CPU core count %ld exceeds MAX_CPU_COUNT=%d, clamping",
               l_cpu_cores_count, MAX_CPU_COUNT);
        l_cpu_cores_count = MAX_CPU_COUNT;
    }
    _cpu_stats.cpu_cores_count = (unsigned)l_cpu_cores_count;

    log_it(L_DEBUG, "Cpu core count: %u", _cpu_stats.cpu_cores_count);

    dap_cpu_get_stats(); // init prev parameters

    return 0;
}

void dap_cpu_monitor_deinit()
{

}

static bool _deserialize_proc_stat(const char *a_line, proc_stat_line_t *a_stat)
{
    memset(a_stat, 0, sizeof(*a_stat));
    int l_scanned = sscanf(a_line, "%9s %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                                   " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                                   " %" SCNu64 " %" SCNu64,
                           a_stat->cpu,
                           &a_stat->user, &a_stat->nice, &a_stat->system, &a_stat->idle,
                           &a_stat->iowait, &a_stat->irq, &a_stat->softirq, &a_stat->steal,
                           &a_stat->guest, &a_stat->guest_nice);
    if (l_scanned < 5) {
        return false;
    }
    // Linux total jiffies: user + nice + system + idle + iowait + irq + softirq + steal.
    a_stat->total = a_stat->user + a_stat->nice + a_stat->system + a_stat->idle +
                    a_stat->iowait + a_stat->irq + a_stat->softirq + a_stat->steal;
    return true;
}

static float _calculate_load(uint64_t a_idle_time, uint64_t a_prev_idle_time,
                             uint64_t a_total_time, uint64_t a_prev_total_time)
{
    if (a_total_time <= a_prev_total_time || a_idle_time < a_prev_idle_time) {
        return 0.0f;
    }
    uint64_t l_total_diff = a_total_time - a_prev_total_time;
    uint64_t l_idle_diff = a_idle_time - a_prev_idle_time;
    if (l_total_diff == 0) {
        return 0.0f;
    }
    if (l_idle_diff > l_total_diff) {
        l_idle_diff = l_total_diff;
    }
    float l_load = (1.0f - (float)l_idle_diff / (float)l_total_diff) * 100.0f;
    if (l_load < 0.0f) {
        return 0.0f;
    }
    if (l_load > 100.0f) {
        return 100.0f;
    }
    return l_load;
}

dap_cpu_stats_t dap_cpu_get_stats()
{
    _proc_stat = fopen("/proc/stat", "r");

    if(_proc_stat == NULL){
        log_it(L_ERROR, "Сan't open /proc/stat file");
        return (dap_cpu_stats_t){0};
    }

    char *line = NULL;
    proc_stat_line_t l_stat = {0};

    /** get summary cpu stat **/
    size_t mem_size = 0;
    ssize_t l_line_len = getline(&line, &mem_size, _proc_stat);
    if (l_line_len < 0) {
        log_it(L_ERROR, "Failed to read /proc/stat");
        fclose(_proc_stat);
        if (line) {
            DAP_FREE(line);
            line = NULL;
        }
        return (dap_cpu_stats_t){0};
    }
    if (!_deserialize_proc_stat(line, &l_stat)) {
        log_it(L_ERROR, "Failed to parse /proc/stat summary line");
        fclose(_proc_stat);
        if (line) {
            DAP_FREE(line);
            line = NULL;
        }
        return (dap_cpu_stats_t){0};
    }

    _cpu_stats.cpu_summary.idle_time = l_stat.idle;
    _cpu_stats.cpu_summary.total_time = l_stat.total;
    /*********************************************/

    for(unsigned i = 0; i < _cpu_stats.cpu_cores_count; i++) {
        l_line_len = getline(&line, &mem_size, _proc_stat);
        if (l_line_len < 0) {
            log_it(L_ERROR, "Failed to read /proc/stat cpu line");
            fclose(_proc_stat);
            if (line) {
                DAP_FREE(line);
                line = NULL;
            }
            return (dap_cpu_stats_t){0};
        }
        if (!_deserialize_proc_stat(line, &l_stat)) {
            log_it(L_ERROR, "Failed to parse /proc/stat cpu line");
            fclose(_proc_stat);
            if (line) {
                DAP_FREE(line);
                line = NULL;
            }
            return (dap_cpu_stats_t){0};
        }
        _cpu_stats.cpus[i].idle_time = l_stat.idle;
        _cpu_stats.cpus[i].total_time = l_stat.total;
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
    if (line) {
        DAP_FREE(line);
        line = NULL;
    }

    return _cpu_stats;
}
