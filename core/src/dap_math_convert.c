#include "dap_math_convert.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_math_convert"

uint256_t dap_uint256_scan_uninteger(const char *a_str_uninteger)
{
    uint256_t l_ret = uint256_0, l_nul = uint256_0;
    int  l_strlen;
    char l_256bit_num[DAP_CHAIN$SZ_MAX256DEC + 1];
    int overflow_flag = 0;

    if (!a_str_uninteger) {
        return log_it(L_ERROR, "NULL as an argument"), l_nul;
    }

    /* Convert number from xxx.yyyyE+zz to xxxyyyy0000... */
    char *l_eptr = strchr(a_str_uninteger, 'e');
    if (!l_eptr)
        l_eptr = strchr(a_str_uninteger, 'E');
    if (l_eptr) {
        /* Compute & check length */
        if ( (l_strlen = strnlen(a_str_uninteger, DAP_SZ_MAX256SCINOT + 1) ) > DAP_SZ_MAX256SCINOT)
            return  log_it(L_ERROR, "Too many digits in `%s` (%d > %d)", a_str_uninteger, l_strlen, DAP_SZ_MAX256SCINOT), l_nul;

        char *l_exp_ptr = l_eptr + 1;
        if (*l_exp_ptr == '+')
            l_exp_ptr++;
        int l_exp = atoi(l_exp_ptr);
        if (!l_exp)
            return  log_it(L_ERROR, "Invalid exponent %s", l_eptr), uint256_0;
        char *l_dot_ptr = strchr(a_str_uninteger, '.');
        if (!l_dot_ptr || l_dot_ptr > l_eptr)
            return  log_it(L_ERROR, "Invalid number format with exponent %d", l_exp), uint256_0;
        int l_dot_len = l_dot_ptr - a_str_uninteger;
        if (l_dot_len >= DATOSHI_POW256)
            return log_it(L_ERROR, "Too many digits in '%s'", a_str_uninteger), uint256_0;
        int l_exp_len = l_eptr - a_str_uninteger - l_dot_len - 1;
        if (l_exp_len + l_dot_len + 1 >= DAP_SZ_MAX256SCINOT)
            return log_it(L_ERROR, "Too many digits in '%s'", a_str_uninteger), uint256_0;
        if (l_exp < l_exp_len) {
            //todo: we need to handle numbers like 1.23456789000000e9
            return log_it(L_ERROR, "Invalid number format with exponent %d and number count after dot %d", l_exp,
                          l_exp_len), uint256_0;
        }
        memcpy(l_256bit_num, a_str_uninteger, l_dot_len);
        memcpy(l_256bit_num + l_dot_len, a_str_uninteger + l_dot_len + 1, l_exp_len);
        int l_zero_cnt = l_exp - l_exp_len;
        if (l_zero_cnt > DATOSHI_POW256) {
            //todo: need to handle leading zeroes, like 0.000...123e100
            return log_it(L_ERROR, "Too long number for 256 bit: `%s` (%d > %d)", a_str_uninteger, l_strlen, DAP_CHAIN$SZ_MAX256DEC), l_nul;
        }
        size_t l_pos = l_dot_len + l_exp_len;
        for (int i = l_zero_cnt; i && l_pos < DATOSHI_POW256; i--)
            l_256bit_num[l_pos++] = '0';
        l_256bit_num[l_pos] = '\0';
        l_strlen = l_pos;

    } else {
        // We have a decimal string, not sci notation
        /* Compute & check length */
        if ( (l_strlen = strnlen(a_str_uninteger, DATOSHI_POW256 + 1) ) > DATOSHI_POW256)
            return  log_it(L_ERROR, "Too many digits in `%s` (%d > %d)", a_str_uninteger, l_strlen, DATOSHI_POW256), l_nul;
        memcpy(l_256bit_num, a_str_uninteger, l_strlen);
        l_256bit_num[l_strlen] = '\0';
    }

    for (int i = 0; i < l_strlen ; i++) {
        char c = l_256bit_num[l_strlen - i - 1];
        if (!isdigit(c)) {
            log_it(L_WARNING, "Incorrect input number");
            return l_nul;
        }
        uint8_t l_digit = c - '0';
        if (!l_digit)
            continue;
#ifdef DAP_GLOBAL_IS_INT128
        uint256_t l_tmp;
        l_tmp.hi = 0;
        l_tmp.lo = (uint128_t)c_pow10_double[i].u64[3] * (uint128_t) l_digit;
        overflow_flag = SUM_256_256(l_ret, l_tmp, &l_ret);
        if (overflow_flag) {
            //todo: change string to uint256_max after implementation
            return log_it(L_ERROR, "Too big number '%s', max number is '%s'", a_str_uninteger, "115792089237316195423570985008687907853269984665640564039457584007913129639935"), l_nul;
        }
        uint128_t l_mul = (uint128_t) c_pow10_double[i].u64[2] * (uint128_t) l_digit;
        l_tmp.lo = l_mul << 64;
        l_tmp.hi = l_mul >> 64;
        overflow_flag = SUM_256_256(l_ret, l_tmp, &l_ret);
        if (overflow_flag) {
            //todo: change string to uint256_max after implementation
            return log_it(L_ERROR, "Too big number '%s', max number is '%s'", a_str_uninteger, "115792089237316195423570985008687907853269984665640564039457584007913129639935"), l_nul;
        }

        if (l_ret.hi == 0 && l_ret.lo == 0) {
            return l_nul;
        }

        l_tmp.lo = 0;
        l_tmp.hi = (uint128_t) c_pow10_double[i].u64[1] * (uint128_t) l_digit;
        overflow_flag = SUM_256_256(l_ret, l_tmp, &l_ret);
        if (overflow_flag) {
            //todo: change string to uint256_max after implementation
            return log_it(L_ERROR, "Too big number '%s', max number is '%s'", a_str_uninteger, "115792089237316195423570985008687907853269984665640564039457584007913129639935"), l_nul;
        }
        if (l_ret.hi == 0 && l_ret.lo == 0) {
            return l_nul;
        }

        l_mul = (uint128_t) c_pow10_double[i].u64[0] * (uint128_t) l_digit;
        if (l_mul >> 64) {
            log_it(L_WARNING, "Input number is too big");
            return l_nul;
        }
        l_tmp.hi = l_mul << 64;
        overflow_flag = SUM_256_256(l_ret, l_tmp, &l_ret);
        if (overflow_flag) {
            //todo: change string to uint256_max after implementation
            return log_it(L_ERROR, "Too big number '%s', max number is '%s'", a_str_uninteger, "115792089237316195423570985008687907853269984665640564039457584007913129639935"), l_nul;
        }
        if (l_ret.hi == 0 && l_ret.lo == 0) {
            return l_nul;
        }
#else
        uint256_t l_tmp;
        for (int j = 7; j>=0; j--) {
            l_tmp = GET_256_FROM_64((uint64_t) c_pow10_double[i].u32[j]);
            if (IS_ZERO_256(l_tmp)) {
                if (j < 6) { // in table, we have only 7 and 6 position with 0-es but 5..0 non-zeroes, so if we have zero on 5 or less, there is no significant position anymore
                    break;
                }
                else {
                    continue;
                }
            }
            LEFT_SHIFT_256(l_tmp, &l_tmp, 32 * (7-j));
            overflow_flag = MULT_256_256(l_tmp, GET_256_FROM_64(l_digit), &l_tmp);
            if (overflow_flag) {
                //todo: change string to uint256_max after implementation
                return log_it(L_ERROR, "Too big number '%s', max number is '%s'", a_str_uninteger, "115792089237316195423570985008687907853269984665640564039457584007913129639935"), l_nul;
            }
            overflow_flag = SUM_256_256(l_ret, l_tmp, &l_ret);
            if (overflow_flag) {
                //todo: change string to uint256_max after implementation
                return log_it(L_ERROR, "Too big number '%s', max number is '%s'", a_str_uninteger, "115792089237316195423570985008687907853269984665640564039457584007913129639935"), l_nul;
            }
        }
#endif
    }
    return l_ret;
}

uint256_t dap_uint256_scan_decimal(const char *a_str_decimal)
{
    int l_len, l_pos;
    char    l_buf  [DAP_CHAIN$SZ_MAX256DEC + 8] = {0}, *l_point;

    /* "12300000000.0000456" */
    if ( (l_len = strnlen(a_str_decimal, DATOSHI_POW256 + 2)) > DATOSHI_POW256 + 1)/* Check for legal length */ /* 1 symbol for \0, one for '.', if more, there is an error */
        return  log_it(L_WARNING, "Incorrect balance format of '%s' - too long (%d > %d)", a_str_decimal,
                       l_len, DATOSHI_POW256 + 1), uint256_0;

    /* Find , check and remove 'precision' dot symbol */
    memcpy (l_buf, a_str_decimal, l_len);                                         /* Make local copy */
    if ( !(l_point = memchr(l_buf, '.', l_len)) )                           /* Is there 'dot' ? */
        return  log_it(L_WARNING, "Incorrect balance format of '%s' - no precision mark", a_str_decimal),
                uint256_0;

    l_pos = l_len - (l_point - l_buf);                                      /* Check number of decimals after dot */
    l_pos--;
    if ( (l_pos ) >  DATOSHI_DEGREE )
        return  log_it(L_WARNING, "Incorrect balance format of '%s' - too much precision", l_buf), uint256_0;

    /* "123.456" -> "123456" */
    memmove(l_point, l_point + 1, l_pos);                                   /* Shift left a right part of the decimal string
                                                                              to dot symbol place */
    *(l_point + l_pos) = '\0';

    /* Add trailer zeros:
     *                pos
     *                 |
     * 123456 -> 12345600...000
     *           ^            ^
     *           |            |
     *           +-18 digits--+
     */
    memset(l_point + l_pos, '0', DATOSHI_DEGREE - l_pos);

    return dap_uint256_scan_uninteger(l_buf);
}

char *dap_uint256_to_char(uint256_t a_uint256, char **a_frac) {
    _Thread_local static char   s_buf       [DATOSHI_POW256 + 2],
                                s_buf_frac  [DATOSHI_POW256 + 2]; // Space for decimal dot and trailing zero
    char l_c, *l_c1 = s_buf, *l_c2 = s_buf;
    uint256_t l_value = a_uint256, uint256_ten = GET_256_FROM_64(10), rem;
    do {
        divmod_impl_256(l_value, uint256_ten, &l_value, &rem);
#ifdef DAP_GLOBAL_IS_INT128
        *l_c1++ = rem.lo + '0';
#else
        *l_c1++ = rem.lo.lo + (unsigned long long) '0';
#endif
    } while (!IS_ZERO_256(l_value));
    *l_c1 = '\0';
    int l_strlen = l_c1 - s_buf;
    --l_c1;

    do {
        l_c = *l_c2; *l_c2++ = *l_c1; *l_c1-- = l_c;
    } while (l_c2 < l_c1);
    if (!a_frac)
        return s_buf;

    int l_len;
    
    if ( 0 < (l_len = (l_strlen - DATOSHI_DEGREE)) ) {
        memcpy(s_buf_frac, s_buf, l_len);
        memcpy(s_buf_frac + l_len + 1, s_buf + l_len, DATOSHI_DEGREE);
        s_buf_frac[l_len] = '.';
        ++l_strlen;
    } else {
        memcpy(s_buf_frac, "0.", 2);
        if (l_len)
            memset(s_buf_frac + 2, '0', -l_len);
        memcpy(s_buf_frac - l_len + 2, s_buf, l_strlen);
        l_strlen += 2 - l_len;
    }
    l_c1 = s_buf_frac + l_strlen - 1;
    while (*l_c1-- == '0' && *l_c1 != '.')
        --l_strlen; 
    s_buf_frac[l_strlen] = '\0';
    *a_frac = s_buf_frac;
    return s_buf;
}

char *dap_uint256_uninteger_to_char(uint256_t a_uninteger) {
    return strdup(dap_uint256_to_char(a_uninteger, NULL));
}

char *dap_uint256_decimal_to_char(uint256_t a_decimal){ //dap_chain_balance_to_coins256, dap_chain_balance_to_coins
    char *l_frac = NULL;
    dap_uint256_to_char(a_decimal, &l_frac);
    return strdup(l_frac);
}

char *dap_uint256_decimal_to_round_char(uint256_t a_uint256, uint8_t a_round_position, bool is_round)
{
    return dap_uint256_char_to_round_char(dap_uint256_decimal_to_char(a_uint256), a_round_position, is_round);
}

char *dap_uint256_char_to_round_char(char* a_str_decimal, uint8_t a_round_pos, bool is_round)
{
    _Thread_local static char s_buf[DATOSHI_POW256 + 3];
    char *l_dot_pos = strchr(a_str_decimal, '.'), *l_res = s_buf;
    int l_len = strlen(a_str_decimal);
    if (!l_dot_pos || a_round_pos >= DATOSHI_DEGREE || ( l_len - (l_dot_pos - a_str_decimal) <= a_round_pos )) {
        memcpy(l_res, a_str_decimal, l_len + 1);
        return l_res;
    }

    int l_new_len = (l_dot_pos - a_str_decimal) + a_round_pos + 1;
    *l_res = '0';
    char    *l_src_c = a_str_decimal + l_new_len,
            *l_dst_c = l_res + l_new_len,
            l_inc = *l_src_c >= '5';
    
    while ( l_src_c > a_str_decimal && (*l_src_c >= '5' || l_inc) ) {
        if (*--l_src_c == '9') {
            l_inc = 1;
            *l_dst_c = *l_dst_c == '.' ? '.' : '0';
            --l_dst_c;
        } else if (*l_src_c == '.') {
            *l_dst_c-- = '.';
        } else {
            *l_dst_c-- = *l_src_c + 1;
            l_inc = 0;
            break;
        }
    }
    if (l_src_c > a_str_decimal)
        memcpy(l_res + 1, a_str_decimal, l_src_c - a_str_decimal);
    if (!a_round_pos)
        --l_new_len;
    if (l_inc) {
        *l_res = '1';
        ++l_new_len;
    } else {
        ++l_res;
    }
    
    *(l_res + l_new_len) = '\0';
    return l_res;
}

int dap_id_uint64_parse(const char *a_id_str, uint64_t *a_id)
{
    if (!a_id_str || !a_id || (sscanf(a_id_str, "0x%16"DAP_UINT64_FORMAT_X, a_id) != 1 &&
            sscanf(a_id_str, "0x%16"DAP_UINT64_FORMAT_x, a_id) != 1 &&
            sscanf(a_id_str, "%"DAP_UINT64_FORMAT_U, a_id) != 1)) {
        log_it (L_ERROR, "Can't recognize '%s' string as 64-bit id, hex or dec", a_id_str);
        return -1;
    }
    return 0;
}

uint64_t dap_uint128_to_uint64(uint128_t a_from)
{
#ifdef DAP_GLOBAL_IS_INT128
    if (a_from > UINT64_MAX) {
        log_it(L_ERROR, "Can't convert balance to uint64_t. It's too big.");
    }
    return (uint64_t)a_from;
#else
    if (a_from.hi) {
        log_it(L_ERROR, "Can't convert balance to uint64_t. It's too big.");
    }
    return a_from.lo;
#endif
}

uint64_t dap_uint256_to_uint64(uint256_t a_from)
{
#ifdef DAP_GLOBAL_IS_INT128
    if (a_from.hi || a_from.lo > UINT64_MAX) {
        log_it(L_ERROR, "Can't convert balance to uint64_t. It's too big.");
    }
    return (uint64_t)a_from.lo;
#else
    if (!IS_ZERO_128(a_from.hi) || a_from.lo.hi) {
        log_it(L_ERROR, "Can't convert balance to uint64_t. It's too big.");
    }
    return a_from.lo.lo;
#endif
}

// 256
uint128_t dap_uint256_to_uint128(uint256_t a_from)
{
    if ( !( EQUAL_128(a_from.hi, uint128_0) ) ) {
        log_it(L_ERROR, "Can't convert to uint128_t. It's too big.");
    }
    return a_from.lo;
}

char *dap_uint128_uninteger_to_char(uint128_t a_uninteger)
{
    char *l_buf = DAP_NEW_Z_SIZE(char, DATOSHI_POW + 2);
    if (!l_buf) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    int l_pos = 0;
    uint128_t l_value = a_uninteger;
#ifdef DAP_GLOBAL_IS_INT128
    do {
        l_buf[l_pos++] = (l_value % 10) + '0';
        l_value /= 10;
    } while (l_value);
#else
    uint32_t l_tmp[4] = {l_value.u32.a, l_value.u32.b, l_value.u32.c, l_value.u32.d};
    uint64_t t, q;
    do {
        q = 0;
        // Byte order is 1, 0, 3, 2 for little endian
        for (int i = 1; i <= 3; ) {
            t = q << 32 | l_tmp[i];
            q = t % 10;
            l_tmp[i] = t / 10;
            if (i == 2) i = 4; // end of cycle
            if (i == 3) i = 2;
            if (i == 0) i = 3;
            if (i == 1) i = 0;
        }
        l_buf[l_pos++] = q + '0';
    } while (l_tmp[2]);
#endif
    int l_strlen = strlen(l_buf) - 1;
    for (int i = 0; i < (l_strlen + 1) / 2; i++) {
        char c = l_buf[i];
        l_buf[i] = l_buf[l_strlen - i];
        l_buf[l_strlen - i] = c;
    }
    return l_buf;
}

char *dap_uint128_decimal_to_char(uint128_t a_decimal)
{
    char *l_buf = dap_uint128_uninteger_to_char(a_decimal);
    int l_strlen = strlen(l_buf);
    int l_pos;
    if (l_strlen > DATOSHI_DEGREE) {
        for (l_pos = l_strlen; l_pos > l_strlen - DATOSHI_DEGREE; l_pos--) {
            l_buf[l_pos] = l_buf[l_pos - 1];
        }
        l_buf[l_pos] = '.';
    } else {
        int l_sub = DATOSHI_DEGREE - l_strlen + 2;
        for (l_pos = DATOSHI_DEGREE + 1; l_pos >= 0; l_pos--) {
            l_buf[l_pos] = (l_pos >= l_sub) ? l_buf[l_pos - l_sub] : '0';
        }
        l_buf[1] = '.';
    }
    return l_buf;
}

uint128_t dap_uint128_scan_uninteger(const char *a_str_uninteger)
{
    int l_strlen = strlen(a_str_uninteger);
    uint128_t l_ret = uint128_0, l_nul = uint128_0;
    if (l_strlen > DATOSHI_POW)
        return l_nul;
    for (int i = 0; i < l_strlen ; i++) {
        char c = a_str_uninteger[l_strlen - i - 1];
        if (!isdigit(c)) {
            log_it(L_WARNING, "Incorrect input number");
            return l_nul;
        }
        uint8_t l_digit = c - '0';
        if (!l_digit)
            continue;
#ifdef DAP_GLOBAL_IS_INT128
        uint128_t l_tmp = (uint128_t)c_pow10[i].u64[0] * l_digit;
        if (l_tmp >> 64) {
            log_it(L_WARNING, "Input number is too big");
            return l_nul;
        }
        l_tmp = (l_tmp << 64) + (uint128_t)c_pow10[i].u64[1] * l_digit;
        SUM_128_128(l_ret, l_tmp, &l_ret);
        if (l_ret == l_nul)
            return l_nul;
#else
        uint128_t l_tmp;
        l_tmp.hi = 0;
        l_tmp.lo = (uint64_t)c_pow10[i].u32[2] * (uint64_t)l_digit;
        SUM_128_128(l_ret, l_tmp, &l_ret);
        if (l_ret.hi == 0 && l_ret.lo == 0)
            return l_nul;
        uint64_t l_mul = (uint64_t)c_pow10[i].u32[3] * (uint64_t)l_digit;
        l_tmp.lo = l_mul << 32;
        l_tmp.hi = l_mul >> 32;
        SUM_128_128(l_ret, l_tmp, &l_ret);
        if (l_ret.hi == 0 && l_ret.lo == 0)
            return l_nul;
        l_tmp.lo = 0;
        l_tmp.hi = (uint64_t)c_pow10[i].u32[0] * (uint64_t)l_digit;
        SUM_128_128(l_ret, l_tmp, &l_ret);
        if (l_ret.hi == 0 && l_ret.lo == 0)
            return l_nul;
        l_mul = (uint64_t)c_pow10[i].u32[1] * (uint64_t)l_digit;
        if (l_mul >> 32) {
            log_it(L_WARNING, "Input number is too big");
            return l_nul;
        }
        l_tmp.hi = l_mul << 32;
        SUM_128_128(l_ret, l_tmp, &l_ret);
        if (l_ret.hi == 0 && l_ret.lo == 0)
            return l_nul;
#endif
    }
    return l_ret;
}

uint128_t dap_uint128_scan_decimal(const char *a_str_decimal)
{
    char l_buf [DATOSHI_POW + 2] = {0};
    uint128_t l_ret = uint128_0, l_nul = uint128_0;

    if (strlen(a_str_decimal) > DATOSHI_POW + 1) {
        log_it(L_WARNING, "Incorrect balance format - too long");
        return l_nul;
    }

    strcpy(l_buf, a_str_decimal);
    char *l_point = strchr(l_buf, '.');
    int l_tail = 0;
    int l_pos = strlen(l_buf);
    if (l_point) {
        l_tail = l_pos - 1 - (l_point - l_buf);
        l_pos = l_point - l_buf;
        if (l_tail > DATOSHI_DEGREE) {
            log_it(L_WARNING, "Incorrect balance format - too much precision");
            return l_nul;
        }
        while (l_buf[l_pos]) {
            l_buf[l_pos] = l_buf[l_pos + 1];
            l_pos++;
        }
        l_pos--;
    }
    if (l_pos + DATOSHI_DEGREE - l_tail > DATOSHI_POW) {
        log_it(L_WARNING, "Incorrect balance format - too long with point");
        return l_nul;
    }
    int i;
    for (i = 0; i < DATOSHI_DEGREE - l_tail; i++) {
        l_buf[l_pos + i] = '0';
    }
    l_buf[l_pos + i] = '\0';
    l_ret = dap_uint128_scan_uninteger(l_buf);

    return l_ret;
}
