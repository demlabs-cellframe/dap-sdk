/*
 * dap_strptime.c - Cross-platform strptime implementation
 *
 * Based on NetBSD strptime.c (Klaus Klein, David Laight)
 * Adapted for DAP SDK with following changes:
 * - Named dap_strptime() to avoid conflicts with system strptime()
 * - Works identically on all platforms (Linux, Windows, macOS, etc.)
 * - Cleaned up Windows-specific hacks
 * - Added DAP SDK coding style
 *
 * Original NetBSD license:
 *   Copyright (c) 1997, 1998, 2005, 2008 The NetBSD Foundation, Inc.
 *   All rights reserved.
 *   BSD-2-Clause License
 */

#include "dap_strptime.h"
#include <ctype.h>
#include <string.h>
#include <stdint.h>

#ifdef DAP_OS_WINDOWS
#include <windows.h>
#define dap_tzset() _tzset()
#else
#include <strings.h>  // strncasecmp on POSIX
#define dap_tzset() tzset()
#endif

// ============================================================================
// Constants
// ============================================================================

#define TM_YEAR_BASE 1900

// Modifiers for format specifiers
#define ALT_E   0x01
#define ALT_O   0x02
#define LEGAL_ALT(x) { if (alt_format & ~(x)) return NULL; }

// Time zone strings
static const char s_gmt[] = "GMT";
static const char s_utc[] = "UTC";

// North American time zones (RFC-822/RFC-2822)
static const char * const s_nast[5] = { "EST", "CST", "MST", "PST", "\0\0\0" };
static const char * const s_nadt[5] = { "EDT", "CDT", "MDT", "PDT", "\0\0\0" };

// AM/PM strings
static const char * const s_am_pm[2] = { "am", "pm" };

// Full day names (lowercase for case-insensitive matching)
static const char * const s_day[7] = {
    "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"
};

// Abbreviated day names
static const char * const s_abday[7] = {
    "sun", "mon", "tue", "wed", "thu", "fri", "sat"
};

// Full month names
static const char * const s_mon[12] = {
    "january", "february", "march", "april", "may", "june",
    "july", "august", "september", "october", "november", "december"
};

// Abbreviated month names
static const char * const s_abmon[12] = {
    "jan", "feb", "mar", "apr", "may", "jun",
    "jul", "aug", "sep", "oct", "nov", "dec"
};

// ============================================================================
// Helper functions (static)
// ============================================================================

#ifdef DAP_OS_WINDOWS
/**
 * @brief Case-insensitive string comparison for Windows
 */
static int s_strncasecmp(const char *a, const char *b, size_t n)
{
    return _strnicmp(a, b, n);
}
#else
#define s_strncasecmp strncasecmp
#endif

/**
 * @brief Convert decimal number from string
 * @param buf Input buffer pointer
 * @param dest Output value
 * @param llim Lower limit
 * @param ulim Upper limit
 * @return Pointer after parsed number or NULL on error
 */
static const unsigned char *
s_conv_num(const unsigned char *buf, int *dest, unsigned int llim, unsigned int ulim)
{
    unsigned int result = 0;
    unsigned char ch;
    unsigned int rulim = ulim;

    ch = *buf;
    if (ch < '0' || ch > '9')
        return NULL;

    do {
        result *= 10;
        result += ch - '0';
        rulim /= 10;
        ch = *++buf;
    } while ((result * 10 <= ulim) && rulim && ch >= '0' && ch <= '9');

    if (result < llim || result > ulim)
        return NULL;

    *dest = (int)result;
    return buf;
}

/**
 * @brief Find string in array (case-insensitive)
 * @param bp Input buffer pointer
 * @param tgt Output index
 * @param n1 First array to search (full names)
 * @param n2 Second array to search (abbreviated names)
 * @param c Number of elements in arrays
 * @return Pointer after matched string or NULL
 */
static const unsigned char *
s_find_string(const unsigned char *bp, int *tgt,
              const char * const *n1, const char * const *n2, int c)
{
    int i;
    size_t len;

    // Check full names first, then abbreviated
    for (; n1 != NULL; n1 = n2, n2 = NULL) {
        for (i = 0; i < c; i++, n1++) {
            len = strlen(*n1);
            if (s_strncasecmp(*n1, (const char *)bp, len) == 0) {
                *tgt = i;
                return bp + len;
            }
        }
    }

    return NULL;
}

// ============================================================================
// Main function
// ============================================================================

/**
 * @brief Parse time string according to format (like POSIX strptime)
 * 
 * Supported format specifiers:
 *   %% - Literal '%'
 *   %a, %A - Day of week (abbreviated/full)
 *   %b, %B, %h - Month name (abbreviated/full)
 *   %C - Century number
 *   %d, %e - Day of month
 *   %D - Date as %m/%d/%y
 *   %F - Date as %Y-%m-%d
 *   %H, %k - Hour (24-hour clock)
 *   %I, %l - Hour (12-hour clock)
 *   %j - Day of year
 *   %m - Month number
 *   %M - Minute
 *   %n, %t - Whitespace
 *   %p - AM/PM
 *   %r - Time in 12-hour notation
 *   %R - Time as %H:%M
 *   %s - Seconds since epoch
 *   %S - Second
 *   %T - Time as %H:%M:%S
 *   %u - Day of week (Monday = 1)
 *   %w - Day of week (Sunday = 0)
 *   %U, %W - Week number
 *   %g, %G, %V - ISO week numbering
 *   %y - Year within century
 *   %Y - Full year
 *   %z - Timezone offset
 *   %Z - Timezone name
 *   %E, %O - Alternative format modifiers (accepted but ignored)
 *
 * @param a_buf Input time string
 * @param a_fmt Format string
 * @param a_tm Output struct tm
 * @return Pointer to first unparsed character or NULL on error
 */
char *dap_strptime(const char *a_buf, const char *a_fmt, struct tm *a_tm)
{
    unsigned char c;
    const unsigned char *bp, *ep;
    int alt_format, i, split_year = 0, neg = 0, offs;
    const char *new_fmt;

    bp = (const unsigned char *)a_buf;

    while (bp != NULL && (c = *a_fmt++) != '\0') {
        // Clear alternate modifier prior to new conversion
        alt_format = 0;
        i = 0;

        // Eat up whitespace
        if (isspace(c)) {
            while (isspace(*bp))
                bp++;
            continue;
        }

        if (c != '%')
            goto literal;

again:
        switch (c = *a_fmt++) {
        case '%':   // "%%" is converted to "%"
literal:
            if (c != *bp++)
                return NULL;
            LEGAL_ALT(0);
            continue;

        // Alternative modifiers - set flag and restart
        case 'E':   // "%E?" alternative conversion modifier
            LEGAL_ALT(0);
            alt_format |= ALT_E;
            goto again;

        case 'O':   // "%O?" alternative conversion modifier
            LEGAL_ALT(0);
            alt_format |= ALT_O;
            goto again;

        // Complex conversion rules (recursive)
        case 'D':   // Date as "%m/%d/%y"
            new_fmt = "%m/%d/%y";
            LEGAL_ALT(0);
            goto recurse;

        case 'F':   // Date as "%Y-%m-%d"
            new_fmt = "%Y-%m-%d";
            LEGAL_ALT(0);
            goto recurse;

        case 'R':   // Time as "%H:%M"
            new_fmt = "%H:%M";
            LEGAL_ALT(0);
            goto recurse;

        case 'r':   // Time in 12-hour representation
            new_fmt = "%I:%M:%S %p";
            LEGAL_ALT(0);
            goto recurse;

        case 'T':   // Time as "%H:%M:%S"
            new_fmt = "%H:%M:%S";
            LEGAL_ALT(0);
recurse:
            bp = (const unsigned char *)dap_strptime((const char *)bp, new_fmt, a_tm);
            LEGAL_ALT(ALT_E);
            continue;

        // Elementary conversion rules
        case 'A':   // Day of week (full name)
        case 'a':   // Day of week (abbreviated)
            bp = s_find_string(bp, &a_tm->tm_wday, s_day, s_abday, 7);
            LEGAL_ALT(0);
            continue;

        case 'B':   // Month name (full)
        case 'b':   // Month name (abbreviated)
        case 'h':   // Month name (same as %b)
            bp = s_find_string(bp, &a_tm->tm_mon, s_mon, s_abmon, 12);
            LEGAL_ALT(0);
            continue;

        case 'C':   // Century number
            i = 20;
            bp = s_conv_num(bp, &i, 0, 99);
            i = i * 100 - TM_YEAR_BASE;
            if (split_year)
                i += a_tm->tm_year % 100;
            split_year = 1;
            a_tm->tm_year = i;
            LEGAL_ALT(ALT_E);
            continue;

        case 'd':   // Day of month (01-31)
        case 'e':   // Day of month (1-31, space padded)
            bp = s_conv_num(bp, &a_tm->tm_mday, 1, 31);
            LEGAL_ALT(ALT_O);
            continue;

        case 'k':   // Hour (24-hour clock, space padded)
            LEGAL_ALT(0);
            /* FALLTHROUGH */
        case 'H':   // Hour (24-hour clock, zero padded)
            bp = s_conv_num(bp, &a_tm->tm_hour, 0, 23);
            LEGAL_ALT(ALT_O);
            continue;

        case 'l':   // Hour (12-hour clock, space padded)
            LEGAL_ALT(0);
            /* FALLTHROUGH */
        case 'I':   // Hour (12-hour clock, zero padded)
            bp = s_conv_num(bp, &a_tm->tm_hour, 1, 12);
            if (a_tm->tm_hour == 12)
                a_tm->tm_hour = 0;
            LEGAL_ALT(ALT_O);
            continue;

        case 'j':   // Day of year (001-366)
            i = 1;
            bp = s_conv_num(bp, &i, 1, 366);
            a_tm->tm_yday = i - 1;
            LEGAL_ALT(0);
            continue;

        case 'M':   // Minute (00-59)
            bp = s_conv_num(bp, &a_tm->tm_min, 0, 59);
            LEGAL_ALT(ALT_O);
            continue;

        case 'm':   // Month (01-12)
            i = 1;
            bp = s_conv_num(bp, &i, 1, 12);
            a_tm->tm_mon = i - 1;
            LEGAL_ALT(ALT_O);
            continue;

        case 'p':   // AM/PM
            bp = s_find_string(bp, &i, s_am_pm, NULL, 2);
            if (a_tm->tm_hour > 11)
                return NULL;
            a_tm->tm_hour += i * 12;
            LEGAL_ALT(0);
            continue;

        case 'S':   // Second (00-60, allows leap second)
            bp = s_conv_num(bp, &a_tm->tm_sec, 0, 61);
            LEGAL_ALT(ALT_O);
            continue;

        case 's':   // Seconds since epoch
            {
                time_t sse = 0;
                uint64_t rulim = INT64_MAX;

                if (*bp < '0' || *bp > '9') {
                    bp = NULL;
                    continue;
                }

                do {
                    sse *= 10;
                    sse += *bp++ - '0';
                    rulim /= 10;
                } while ((sse * 10 <= INT64_MAX) &&
                         rulim && *bp >= '0' && *bp <= '9');

                if (sse < 0 || (uint64_t)sse > INT64_MAX) {
                    bp = NULL;
                    continue;
                }

                struct tm *l_tm = localtime(&sse);
                if (l_tm) {
                    *a_tm = *l_tm;
                } else {
                    bp = NULL;
                }
            }
            continue;

        case 'U':   // Week number (Sunday first day)
        case 'W':   // Week number (Monday first day)
            // Just validate range, can't compute without other fields
            bp = s_conv_num(bp, &i, 0, 53);
            LEGAL_ALT(ALT_O);
            continue;

        case 'w':   // Day of week (Sunday = 0)
            bp = s_conv_num(bp, &a_tm->tm_wday, 0, 6);
            LEGAL_ALT(ALT_O);
            continue;

        case 'u':   // Day of week (Monday = 1)
            bp = s_conv_num(bp, &i, 1, 7);
            a_tm->tm_wday = i % 7;
            LEGAL_ALT(ALT_O);
            continue;

        case 'g':   // ISO week year (2 digits)
            bp = s_conv_num(bp, &i, 0, 99);
            continue;

        case 'G':   // ISO week year (4 digits)
            do {
                bp++;
            } while (isdigit(*bp));
            continue;

        case 'V':   // ISO week number
            bp = s_conv_num(bp, &i, 0, 53);
            continue;

        case 'Y':   // Year with century
            i = TM_YEAR_BASE;
            bp = s_conv_num(bp, &i, 0, 9999);
            a_tm->tm_year = i - TM_YEAR_BASE;
            LEGAL_ALT(ALT_E);
            continue;

        case 'y':   // Year without century
            bp = s_conv_num(bp, &i, 0, 99);
            if (split_year) {
                // Preserve century
                i += (a_tm->tm_year / 100) * 100;
            } else {
                split_year = 1;
                if (i <= 68)
                    i = i + 2000 - TM_YEAR_BASE;
                else
                    i = i + 1900 - TM_YEAR_BASE;
            }
            a_tm->tm_year = i;
            continue;

        case 'Z':   // Timezone name
            dap_tzset();
            if (s_strncasecmp((const char *)bp, s_gmt, 3) == 0 ||
                s_strncasecmp((const char *)bp, s_utc, 3) == 0) {
                a_tm->tm_isdst = 0;
                bp += 3;
            } else {
#ifdef DAP_OS_WINDOWS
                // Windows has _tzname instead of tzname
                extern char *_tzname[2];
                const char *const tzn[2] = { _tzname[0], _tzname[1] };
#else
                extern char *tzname[2];
                const char *const tzn[2] = { tzname[0], tzname[1] };
#endif
                ep = s_find_string(bp, &i, tzn, NULL, 2);
                if (ep != NULL) {
                    a_tm->tm_isdst = i;
                }
                bp = ep;
            }
            continue;

        case 'z':   // Timezone offset
            // ISO 8601: Z, [+-]hhmm, [+-]hh:mm, [+-]hh
            // RFC-822: UT, GMT, EST/EDT, CST/CDT, MST/MDT, PST/PDT
            // Military: A-I, L-Y (excluding J)
            while (isspace(*bp))
                bp++;

            switch (*bp++) {
            case 'G':
                if (*bp++ != 'M')
                    return NULL;
                /* FALLTHROUGH */
            case 'U':
                if (*bp++ != 'T')
                    return NULL;
                /* FALLTHROUGH */
            case 'Z':
                a_tm->tm_isdst = 0;
                continue;

            case '+':
                neg = 0;
                break;

            case '-':
                neg = 1;
                break;

            default:
                --bp;
                ep = s_find_string(bp, &i, s_nast, NULL, 4);
                if (ep != NULL) {
                    bp = ep;
                    continue;
                }
                ep = s_find_string(bp, &i, s_nadt, NULL, 4);
                if (ep != NULL) {
                    a_tm->tm_isdst = 1;
                    bp = ep;
                    continue;
                }

                // Military time zones
                if ((*bp >= 'A' && *bp <= 'I') ||
                    (*bp >= 'L' && *bp <= 'Y')) {
                    bp++;
                    continue;
                }
                return NULL;
            }

            // Parse numeric offset
            offs = 0;
            for (i = 0; i < 4; ) {
                if (isdigit(*bp)) {
                    offs = offs * 10 + (*bp++ - '0');
                    i++;
                    continue;
                }
                if (i == 2 && *bp == ':') {
                    bp++;
                    continue;
                }
                break;
            }

            switch (i) {
            case 2:
                offs *= 100;
                break;
            case 4:
                i = offs % 100;
                if (i >= 60)
                    return NULL;
                offs = (offs / 100) * 100 + (i * 50) / 30;
                break;
            default:
                return NULL;
            }

            if (neg)
                offs = -offs;
            a_tm->tm_isdst = 0;
            continue;

        // Whitespace
        case 'n':
        case 't':
            while (isspace(*bp))
                bp++;
            LEGAL_ALT(0);
            continue;

        default:
            // Unknown conversion
            return NULL;
        }
    }

    return (char *)bp;
}
