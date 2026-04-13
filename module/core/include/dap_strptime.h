/*
 * dap_strptime.h - Cross-platform strptime implementation
 *
 * Provides dap_strptime() function that works identically on all platforms.
 * On POSIX systems, strptime() is available in libc, but has implementation
 * differences between platforms. On Windows, strptime() doesn't exist at all.
 *
 * dap_strptime() guarantees consistent behavior across:
 * - Linux (glibc, musl)
 * - Windows (MSVC, MinGW)
 * - macOS
 * - FreeBSD/OpenBSD
 * - Android
 *
 * Usage:
 *   struct tm tm = {0};
 *   char *end = dap_strptime("2024-12-25 10:30:00", "%Y-%m-%d %H:%M:%S", &tm);
 *   if (end && *end == '\0') {
 *       // Successfully parsed entire string
 *       time_t t = mktime(&tm);
 *   }
 */

#pragma once

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parse time string according to format (like POSIX strptime)
 *
 * This function is modeled after the POSIX strptime() function but provides
 * consistent cross-platform behavior.
 *
 * Supported format specifiers:
 *   %% - Literal '%'
 *   %a, %A - Day of week (abbreviated/full): Sun-Sat, Sunday-Saturday
 *   %b, %B, %h - Month name (abbreviated/full): Jan-Dec, January-December
 *   %C - Century number (00-99)
 *   %d, %e - Day of month (01-31 or 1-31 space padded)
 *   %D - Date as %m/%d/%y
 *   %F - Date as %Y-%m-%d (ISO 8601)
 *   %H, %k - Hour 24-hour (00-23)
 *   %I, %l - Hour 12-hour (01-12)
 *   %j - Day of year (001-366)
 *   %m - Month number (01-12)
 *   %M - Minute (00-59)
 *   %n, %t - Any whitespace
 *   %p - AM/PM (case insensitive)
 *   %r - Time in 12-hour notation (%I:%M:%S %p)
 *   %R - Time as %H:%M
 *   %s - Seconds since epoch (Unix timestamp)
 *   %S - Second (00-60, allows leap second)
 *   %T - Time as %H:%M:%S
 *   %u - Day of week (Monday = 1, Sunday = 7)
 *   %w - Day of week (Sunday = 0, Saturday = 6)
 *   %U - Week number with Sunday as first day (00-53)
 *   %W - Week number with Monday as first day (00-53)
 *   %g - ISO week year (2 digits)
 *   %G - ISO week year (4 digits)
 *   %V - ISO week number (01-53)
 *   %y - Year without century (00-99, 69-99 = 1969-1999, 00-68 = 2000-2068)
 *   %Y - Year with century (e.g., 2024)
 *   %z - Timezone offset (+/-hhmm, +/-hh:mm, UTC, GMT, Z, EST/EDT, etc.)
 *   %Z - Timezone name (UTC, GMT, or system timezone)
 *   %E, %O - Alternative format modifiers (accepted but ignored)
 *
 * @param a_buf Input string to parse
 * @param a_fmt Format string with conversion specifiers
 * @param a_tm  Output struct tm to fill
 *
 * @return Pointer to first character not processed, or NULL on error
 *
 * @note The struct tm should be initialized to zeros before calling.
 * @note This function does NOT set tm_wday and tm_yday unless explicitly
 *       parsed. Use mktime() after parsing to normalize the structure.
 *
 * @example
 *   // Parse ISO 8601 date
 *   struct tm tm = {0};
 *   dap_strptime("2024-12-25", "%Y-%m-%d", &tm);
 *
 * @example
 *   // Parse RFC 822 date
 *   struct tm tm = {0};
 *   dap_strptime("25 Dec 2024 10:30:00 +0300", "%d %b %Y %T %z", &tm);
 */
char *dap_strptime(const char *a_buf, const char *a_fmt, struct tm *a_tm);

#ifdef __cplusplus
}
#endif
