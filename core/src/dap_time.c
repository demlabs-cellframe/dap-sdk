#ifdef _WIN32
#include <windows.h>
#include <sys/time.h>
#endif
#include <errno.h>
#include <string.h>
#include <time.h>
#include "dap_common.h"
#include "dap_time.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_common"

#ifdef _WIN32

extern char *strptime(const char *s, const char *format, struct tm *tm);

/* Identifier for system-wide realtime clock.  */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME              0

#ifndef clockid_t
typedef int clockid_t;
#endif

struct timespec {
    uint64_t tv_sec; // seconds
    uint64_t tv_nsec;// nanoseconds
};

int clock_gettime(clockid_t clock_id, struct timespec *spec)
{
//    __int64 wintime;
//    GetSystemTimeAsFileTime((FILETIME*) &wintime);
//    spec->tv_sec = wintime / 10000000i64; //seconds
//    spec->tv_nsec = wintime % 10000000i64 * 100; //nano-seconds
//    return 0;
    uint64_t ft;
    GetSystemTimeAsFileTime(FILETIME*)&ft); //return the number of 100-nanosecond intervals since January 1, 1601 (UTC)
    // from 1 jan 1601 to 1 jan 1970
    ft -= 116444736000000000i64;
    spec->tv_sec = ft / 10000000i64; //seconds
    spec->tv_nsec = ft % 10000000i64 * 100; //nano-seconds
    return 0;
}
#endif
#endif


// Create time from second
dap_nanotime_t dap_nanotime_from_sec(dap_time_t a_time)
{
    return (dap_nanotime_t)a_time * DAP_NSEC_PER_SEC;
}

// Get seconds from time
dap_time_t dap_nanotime_to_sec(dap_nanotime_t a_time)
{
    return a_time / DAP_NSEC_PER_SEC;
}

/**
 * @brief dap_chain_time_now Get current time in seconds since January 1, 1970 (UTC)
 * @return Returns current UTC time in seconds.
 */
dap_time_t dap_time_now(void)
{
    return (dap_time_t)time(NULL);
}

/**
 * @brief dap_chain_time_now Get current time in nanoseconds since January 1, 1970 (UTC)
 * @return Returns current UTC time in nanoseconds.
 */
dap_nanotime_t dap_nanotime_now(void)
{
    dap_nanotime_t l_time_nsec;
    struct timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);
    l_time_nsec = (dap_nanotime_t)cur_time.tv_sec * DAP_NSEC_PER_SEC + cur_time.tv_nsec;
    return l_time_nsec;
}

/**
 * dap_usleep:
 * @a_microseconds: number of microseconds to pause
 *
 * Pauses the current thread for the given number of microseconds.
 */
void dap_usleep(uint64_t a_microseconds)
{
#ifdef DAP_OS_WINDOWS
    Sleep (a_microseconds / 1000);
#else
    struct timespec l_request, l_remaining;
    l_request.tv_sec = a_microseconds / DAP_USEC_PER_SEC;
    l_request.tv_nsec = 1000 * (a_microseconds % DAP_USEC_PER_SEC);
    while(nanosleep(&l_request, &l_remaining) == -1 && errno == EINTR)
        l_request = l_remaining;
#endif
}

/**
 * @brief Calculate diff of two struct timespec
 * @param[in] a_start - first time
 * @param[in] a_stop - second time
 * @param[out] a_result -  diff time, may be NULL
 * @return diff time in millisecond
 */
int timespec_diff(struct timespec *a_start, struct timespec *a_stop, struct timespec *a_result)
{
    if(!a_start || !a_stop)
        return 0;
    struct timespec l_time_tmp = {};
    struct timespec *l_result = a_result ? a_result : &l_time_tmp;
    if ((a_stop->tv_nsec - a_start->tv_nsec) < 0) {
        l_result->tv_sec = a_stop->tv_sec - a_start->tv_sec - 1;
        l_result->tv_nsec = a_stop->tv_nsec - a_start->tv_nsec + 1000000000;
    } else {
        l_result->tv_sec = a_stop->tv_sec - a_start->tv_sec;
        l_result->tv_nsec = a_stop->tv_nsec - a_start->tv_nsec;
    }

    return (l_result->tv_sec * 1000 + l_result->tv_nsec / 1000000);
}

/**
 * @brief time_to_rfc822 Convert time_t to string with RFC2822 formatted date and time
 * @param[out] out Output buffer
 * @param[out] out_size_mac Maximum size of output buffer
 * @param[in] t UNIX time
 * @return Length of resulting string if ok or lesser than zero if not
 */
int dap_time_to_str_rfc822(char *a_out, size_t a_out_size_max, dap_time_t a_time)
{
    struct tm *l_tmp;
    time_t l_time = a_time;
    l_tmp = localtime(&l_time);
    if (!l_tmp) {
        log_it(L_ERROR, "Can't convert data from unix format to structured one");
        return -2;
    }
    int l_ret = strftime(a_out, a_out_size_max, "%a, %d %b %Y %H:%M:%S"
                     #ifndef DAP_OS_WINDOWS
                                                " %z"
                     #endif
                         , l_tmp);
    if (!l_ret) {
        log_it(L_ERROR, "Can't print formatted time in string");
        return -1;
    }
#ifdef DAP_OS_WINDOWS
    // %z is unsupported on Windows platform
    TIME_ZONE_INFORMATION l_tz_info;
    GetTimeZoneInformation(&l_tz_info);
    char l_tz_str[8];
    snprintf(l_tz_str, sizeof(l_tz_str), " +%02d%02d", -(l_tz_info.Bias / 60), l_tz_info.Bias % 60);
    if (l_ret < a_out_size_max)
        l_ret += snprintf(a_out + l_ret, a_out_size_max - l_ret, l_tz_str);
#endif
    a_out[l_ret] = '\0';
    return l_ret;
}

/**
 * @brief Get time_t from string with RFC822 formatted
 * @brief (not WIN32) "%d %b %y %T %z" == "02 Aug 22 19:50:41 +0300"
 * @brief (WIN32) !DOES NOT WORK! please, use dap_time_from_str_simplified()
 * @param[out] a_time_str
 * @return time from string or 0 if bad time forma
 */
dap_time_t dap_time_from_str_rfc822(const char *a_time_str)
{
    dap_time_t l_time = 0;
    if(!a_time_str) {
        return l_time;
    }
    struct tm l_tm = {};
    strptime(a_time_str, "%d %b %Y %T %z", &l_tm);

    time_t tmp = mktime(&l_tm);
    l_time = (tmp <= 0) ? 0 : tmp;
    return l_time;
}

/**
 * @brief Get time_t from string simplified formatted [%y%m%d = 220610 = 10 june 2022 00:00]
 * @param[out] a_time_str
 * @return time from string or 0 if bad time format
 */
dap_time_t dap_time_from_str_simplified(const char *a_time_str)
{
    dap_time_t l_time = 0;
    if(!a_time_str) {
        return l_time;
    }
    struct tm l_tm = {};
    strptime(a_time_str, "%y%m%d", &l_tm);
    l_tm.tm_sec++;
    time_t tmp = mktime(&l_tm);
    l_time = (tmp <= 0) ? 0 : tmp;
    return l_time;
}

/**
 * @brief time_to_rfc822 Convert dap_chain_time_t to string with RFC822 formatted date and time
 * @param[out] out Output buffer
 * @param[out] out_size_mac Maximum size of output buffer
 * @param[in] t UNIX time
 * @return Length of resulting string if ok or lesser than zero if not
 */
int dap_nanotime_to_str_rfc822(char *a_out, size_t a_out_size_max, dap_nanotime_t a_chain_time)
{
    time_t l_time = dap_nanotime_to_sec(a_chain_time);
    return dap_time_to_str_rfc822(a_out, a_out_size_max, l_time);
}
