#ifdef _WIN32
#include <windows.h>
#include <sys/time.h>
#endif
#include <errno.h>
#include <string.h>
#include <time.h>
#include "dap_common.h"
#include "dap_time.h"

#define LOG_TAG "dap_common"

#ifdef DAP_OS_WINDOWS

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

#ifdef DAP_OS_WINDOWS
// Constants for Windows FILETIME (100-ns ticks since 1601-01-01) conversions
#define WINDOWS_TICKS_PER_SEC                  10000000ULL
#define EPOCH_DIFF_WINDOWS_TO_UNIX_SECS        11644473600ULL   // seconds between 1601-01-01 and 1970-01-01
#define EPOCH_DIFF_WINDOWS_TO_UNIX_TICKS       (EPOCH_DIFF_WINDOWS_TO_UNIX_SECS * WINDOWS_TICKS_PER_SEC)
#define WINDOWS_TICKS_PER_MIN                  (60ULL * WINDOWS_TICKS_PER_SEC)
// Build local calendar time for a given epoch using Windows TZ rules without localtime(),
// and compute RFC offset in minutes (east of UTC).
static bool s_win_local_tm_and_offset(time_t a_time, struct tm *a_out_tm, int *a_out_offset_min)
{
    if ( !a_out_tm && !a_out_offset_min ) return false;

    ULARGE_INTEGER l_ui_utc = { .QuadPart = (ULONGLONG)a_time * WINDOWS_TICKS_PER_SEC + EPOCH_DIFF_WINDOWS_TO_UNIX_TICKS };
    SYSTEMTIME l_st_utc = { };
    if (!FileTimeToSystemTime(&(FILETIME){ l_ui_utc.LowPart, l_ui_utc.HighPart }, &l_st_utc))
        return false;

    SYSTEMTIME l_st_local = { };
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0602
    DYNAMIC_TIME_ZONE_INFORMATION l_dtzi = { };
    DWORD l_tzid = GetDynamicTimeZoneInformation(&l_dtzi);
    if ( l_tzid == TIME_ZONE_ID_INVALID || !SystemTimeToTzSpecificLocalTimeEx(&l_dtzi, &l_st_utc, &l_st_local) )
        return false;
#else
    DYNAMIC_TIME_ZONE_INFORMATION l_dtzi = { };
    DWORD l_tzid = GetDynamicTimeZoneInformation(&l_dtzi);
    TIME_ZONE_INFORMATION l_tzi = { };
    if ( l_tzid == TIME_ZONE_ID_INVALID
        || !GetTimeZoneInformationForYear(l_st_utc.wYear, &l_dtzi, &l_tzi)
        || !SystemTimeToTzSpecificLocalTime(&l_tzi, &l_st_utc, &l_st_local) )
        return false;
#endif
    if ( a_out_tm ) {
        *a_out_tm = (struct tm) { l_st_local.wSecond, l_st_local.wMinute, l_st_local.wHour, l_st_local.wDay,
            (l_st_local.wMonth ? (l_st_local.wMonth - 1) : 0), (l_st_local.wYear ? (l_st_local.wYear - 1900) : 0),
            l_st_local.wDayOfWeek,
            .tm_isdst = l_tzid == TIME_ZONE_ID_DAYLIGHT ? 1 : l_tzid == TIME_ZONE_ID_STANDARD ? 0 : -1
        };
    }
    if ( a_out_offset_min ) {
        FILETIME l_ft_local = { };
        if (!SystemTimeToFileTime(&l_st_local, &l_ft_local) )
            return false; // treats local as UTC → intentional
        ULARGE_INTEGER l_ui_local = { .LowPart = l_ft_local.dwLowDateTime, .HighPart = l_ft_local.dwHighDateTime };
        *a_out_offset_min = (int)( (l_ui_local.QuadPart - l_ui_utc.QuadPart) / WINDOWS_TICKS_PER_MIN );
    }
    return true;
}
#endif

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
    struct tm l_tm = { };
#ifdef DAP_OS_WINDOWS
    int l_off = 0;
    if ( !s_win_local_tm_and_offset(a_time, &l_tm, &l_off) )
        return log_it(L_ERROR, "Can't convert UNIX timestamp %"DAP_UINT64_FORMAT_U, a_time), -2;
#else
    if ( !localtime_r(&(const time_t){ a_time }, &l_tm) )
#endif
        return log_it(L_ERROR, "Can't convert UNIX timestamp %"DAP_UINT64_FORMAT_U, a_time), -2;
    int l_ret = strftime(a_out, a_out_size_max, "%a, %d %b %Y %H:%M:%S"
                     #ifndef DAP_OS_WINDOWS
                                                " %z"
                     #endif
                         , &l_tm);
    if (!l_ret)
        return log_it(L_ERROR, "Can't print formatted time in string"), -1;
    
    
#ifdef DAP_OS_WINDOWS
    if ((size_t)l_ret < a_out_size_max) {
        int l_off_abs = l_off < 0 ? -l_off : l_off;
        l_ret += snprintf(a_out + l_ret, a_out_size_max - l_ret,
                          " %c%02d%02d", l_off >= 0 ? '+' : '-', l_off_abs / 60, l_off_abs % 60);
    }
#endif
    return l_ret;
}

/**
 * @brief Get time_t from string with RFC822 formatted
 * @brief "%d %b %y %T %z" == "02 Aug 22 19:50:41 +0300"
 * @param[out] a_time_str
 * @return time from string or 0 if bad time forma
 */
dap_time_t dap_time_from_str_rfc822(const char *a_time_str)
{
    dap_return_val_if_fail(a_time_str, 0);
    struct tm l_tm = { };
    char *ret = strptime(a_time_str, "%d %b %Y %T", &l_tm);
    if ( !ret )
        return log_it(L_ERROR, "Invalid timestamp \"%s\", expected RFC822 string", a_time_str), 0;
    time_t l_off = 0, l_ret;
    char l_sign;
    int l_hour, l_min;
    if ( sscanf(ret, " %c%2d%2d", &l_sign, &l_hour, &l_min) == 3
        && l_hour >= 0 && ( ( l_sign == '+' && l_hour <= 14 ) || ( l_sign == '-' && l_hour <= 12 ) )
        && l_min >= 0 && l_min <= 59 )
        l_off = l_hour * 3600 + l_min * 60;
    else
        return log_it(L_ERROR, "Invalid timestamp \"%s\", expected RFC822 string", a_time_str), 0;
    if (l_sign == '-')
        l_off = -l_off;
#ifdef DAP_OS_WINDOWS
    l_ret = _mkgmtime(&l_tm);
    return l_ret > 0 ? l_ret - l_off : ( log_it(L_ERROR, "Invalid timestamp \"%s\", expected RFC822 string", a_time_str), 0 );
#else
    // Interpret tm as local time, then adjust by (local_offset_at_date - parsed_offset)
    l_ret = mktime(&l_tm);
    if ( l_ret <= 0 )
        return log_it(L_ERROR, "Invalid timestamp \"%s\", expected RFC822 string", a_time_str), 0;
    struct tm l_tm_local = { };
    localtime_r(&l_ret, &l_tm_local);
    return l_ret + l_tm_local.tm_gmtoff - l_off;
#endif
}

/**
 * @brief Get time_t from string simplified formatted [%y%m%d = 220610 = 10 june 2022 00:00]
 * @param[out] a_time_str
 * @return time from string or 0 if bad time format
 */
dap_time_t dap_time_from_str_simplified(const char *a_time_str)
{
    dap_return_val_if_fail(a_time_str, 0);
    struct tm l_tm = {};
    char *ret = strptime(a_time_str, "%y%m%d", &l_tm);
    if ( !ret || *ret )
        return log_it(L_ERROR, "Invalid timestamp \"%s\", expected simplified string \"yy\"mm\"dd", a_time_str), 0;
    l_tm.tm_sec++;
    time_t tmp = mktime(&l_tm);
    return tmp > 0 ? (dap_time_t)tmp : 0;
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

/**
 * @brief Convert time str to dap_time_t by custom format
 * @param a_time_str
 * @param a_format_str
 * @return time from string or 0 if bad time format
 */
dap_time_t dap_time_from_str_custom(const char *a_time_str, const char *a_format_str)
{
    dap_return_val_if_pass(!a_time_str || !a_format_str, 0);
    struct tm l_tm = {};
    char *ret = strptime(a_time_str, a_format_str, &l_tm);
    if ( !ret || *ret )
        return log_it(L_ERROR, "Invalid timestamp \"%s\" by format \"%s\"", a_time_str, a_format_str), 0;
    time_t tmp = mktime(&l_tm);
    return tmp > 0 ? (dap_time_t)tmp : 0;
}