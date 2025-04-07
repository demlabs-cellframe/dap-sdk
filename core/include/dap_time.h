#pragma once
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef DAP_OS_WINDOWS
#define localtime_r(a, b) localtime_s((b), (a))
#endif

#define DAP_TIME_STR_SIZE 32

#define DAP_END_OF_DAYS 4102444799
// Constant to convert seconds to nanoseconds
#define DAP_NSEC_PER_SEC 1000000000
// Constant to convert msec to nanoseconds
#define DAP_NSEC_PER_MSEC 1000000
// Constant to convert seconds to microseconds
#define DAP_USEC_PER_SEC 1000000
// Seconds per day
#define DAP_SEC_PER_DAY 86400

// time in seconds
typedef uint64_t dap_time_t;
// time in nanoseconds
typedef uint64_t dap_nanotime_t;
// time in milliseconds
typedef uint64_t dap_millitime_t;

#ifdef __cplusplus
extern "C" {
#endif

// Create nanotime from second
static inline dap_nanotime_t dap_nanotime_from_sec(dap_time_t a_time) {
    return (dap_nanotime_t)a_time * DAP_NSEC_PER_SEC;
}
// Get seconds from nanotime
static inline dap_time_t dap_nanotime_to_sec(dap_nanotime_t a_time) {
    return a_time / DAP_NSEC_PER_SEC;
}

typedef union dap_time_simpl_str {
    const char s[7];
} dap_time_simpl_str_t;

/**
 * @brief Convert dap_time_t to string in simplified format [%y%m%d = 220610 = 10 june 2022 00:00]
 * @param[in] a_time Time to convert
 * @return Pointer to the string or NULL if error
 */
static inline dap_time_simpl_str_t s_dap_time_to_str_simplified (dap_time_t a_time) {
    time_t time = (time_t)a_time;
    struct tm *l_tm = localtime(&time);
    dap_time_simpl_str_t res = { };
    if ( l_tm )
        strftime( (char*)res.s, sizeof(res.s), "%y%m%d", l_tm );
    return res;
}

#define dap_time_to_str_simplified(t) s_dap_time_to_str_simplified(t).s

static inline dap_millitime_t dap_nanotime_to_millitime(dap_nanotime_t a_time) {
    return a_time / DAP_NSEC_PER_MSEC;
}

static inline dap_nanotime_t dap_millitime_to_nanotime(dap_millitime_t a_time) {
    return (dap_nanotime_t)a_time * DAP_NSEC_PER_MSEC;
}

/**
 * @brief dap_chain_time_now Get current time in seconds since January 1, 1970 (UTC)
 * @return Returns current UTC time in seconds.
 */
static inline dap_time_t dap_time_now() {
    return (dap_time_t)time(NULL);
}
/**
 * @brief dap_clock_gettime Get current time in nanoseconds since January 1, 1970 (UTC)
 * @return Returns current UTC time in nanoseconds.
 */
static inline dap_nanotime_t dap_nanotime_now(void) {
    struct timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);
    return (dap_nanotime_t)cur_time.tv_sec * DAP_NSEC_PER_SEC + cur_time.tv_nsec;
}

// crossplatform usleep
void dap_usleep(uint64_t a_microseconds);

/**
 * @brief dap_time_to_str_rfc822 This function converts time to stirng format.
 * @param a_time
 * @param a_buf
 * @return 0 if success, overwise returns a error code
 */
int dap_time_to_str_rfc822(char * out, size_t out_size_max, dap_time_t a_time);
dap_time_t dap_time_from_str_rfc822(const char *a_time_str);
const char* dap_time_to_str_simplified(dap_time_t a_time);
int dap_nanotime_to_str_rfc822(char *a_out, size_t a_out_size_max, dap_nanotime_t a_chain_time);
int timespec_diff(struct timespec *a_start, struct timespec *a_stop, struct timespec *a_result);

dap_time_t dap_time_from_str_simplified(const char *a_time_str);
dap_time_t dap_time_from_str_custom(const char *a_time_str, const char *a_format_str);

#ifdef __cplusplus
}
#endif
