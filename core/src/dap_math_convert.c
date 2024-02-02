#include "dap_math_convert.h"

#define LOG_TAG "dap_math_convert"

uint256_t dap_uint256_scan_uninteger(const char *a_str_uninteger){ //dap_chain_balance_print
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
//        if (l_ret.hi == 0 && l_ret.lo == 0) {
//            return l_nul;
//        }
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

uint256_t dap_uint256_scan_decimal(const char *a_str_decimal){ //dap_chain_coins_to_balance256, dap_chain_coins_to_balance
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

char *dap_uint256_uninteger_to_char(uint256_t a_uint256) {
    char *l_buf = DAP_NEW_Z_SIZE(char, DATOSHI_POW256 + 2); // for decimal dot and trailing zero
    if (!l_buf) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    int l_pos = 0;
    uint256_t l_value = a_uint256;
    uint256_t uint256_ten = GET_256_FROM_64(10);
    uint256_t rem;
    do {
        divmod_impl_256(l_value, uint256_ten, &l_value, &rem);
#ifdef DAP_GLOBAL_IS_INT128
        l_buf[l_pos++] = rem.lo + '0';
#else
        l_buf[l_pos++] = rem.lo.lo + (unsigned long long) '0';
#endif
    } while (!IS_ZERO_256(l_value));
    int l_strlen = strlen(l_buf) - 1;
    for (int i = 0; i < (l_strlen + 1) / 2; i++) {
        char c = l_buf[i];
        l_buf[i] = l_buf[l_strlen - i];
        l_buf[l_strlen - i] = c;
    }
    return l_buf;
}

char *dap_uint256_decimal_to_char(uint256_t a_uint256){ //dap_chain_balance_to_coins256, dap_chain_balance_to_coins
    char *l_buf, *l_cp;
    int l_strlen, l_len;

    /* 123000...456 -> "123000...456" */
    if ( !(l_buf = dap_uint256_uninteger_to_char(a_uint256)) )
        return NULL;

    l_strlen = strlen(l_buf);

    if ( 0 < (l_len = (l_strlen - DATOSHI_DEGREE)) )
    {
        l_cp = l_buf + l_len;                                               /* Move last 18 symbols to one position right */
        memmove(l_cp + 1, l_cp, DATOSHI_DEGREE);
        *l_cp = '.';                                                        /* Insert '.' separator */

        l_strlen++;                                                         /* Adjust string len in the buffer */
    } else {
        l_len = DATOSHI_DEGREE - l_strlen;                           /* Add leading "0." */
        l_cp = l_buf;
        memmove(l_cp + l_len + 2, l_cp, DATOSHI_DEGREE - l_len);                                     /* Move last 18 symbols to 2 positions right */
        memset(l_cp, '0', l_len + 2);
        *(++l_cp) = '.';
        l_strlen += 2;                                                      /* Adjust string len in the buffer */
    }

    if ( *(l_cp = l_buf) == '0' )                                           /* Is there lead zeroes ? */
    {
        /* 000000000000000000000.000000000000000001 */
        /* 000000000000000000123.000000000000000001 */
        for ( l_cp += 1; *l_cp == '0'; l_cp++);                             /* Skip all '0' symbols */

        if ( *l_cp == '.' )                                                 /* l_cp point to separator - then step back */
            l_cp--;

        if ( (l_len = (l_cp - l_buf)) )
        {
            l_len = l_strlen - l_len;                                       /* A part of the buffer to be moved to begin */
            memmove(l_buf, l_cp, l_len);                                    /* Move and terminated by zero */
            l_buf[l_len] = '\0';
        }

        l_strlen = l_len;                                                   /* Adjust string len in the buffer */
    }

    for ( l_cp = l_buf + strlen(l_buf) - 1; *l_cp == '0' && l_cp >= l_buf; l_cp--)
        if (*(l_cp - 1) != '.')
            *l_cp = '\0';

    return l_buf;
}

char *dap_uint256_decimal_to_round_char(uint256_t a_uint256, uint8_t a_round_position, bool is_round)
{
    char *result = dap_uint256_decimal_to_char(a_uint256);

    size_t l_str_len = strlen(result);
    char*  l_dot_pos = strstr(result, ".");

    if (l_dot_pos && (l_str_len - (l_dot_pos - result)) > a_round_position){
        size_t l_new_size = l_dot_pos - result + a_round_position;
        char *l_res = DAP_DUP_SIZE(result, l_new_size + 1);
        DAP_DELETE(result);
        return l_res;
    }

    return result;
}

char *dap_uint256_char_to_round_char(char* a_str_decimal, uint8_t a_round_position, bool is_round)
{
    char *result = a_str_decimal;

    size_t l_str_len = strlen(result);
    char*  l_dot_pos = strstr(result, ".");

    if (l_dot_pos && (l_str_len - (l_dot_pos - result)) > a_round_position){
        *(char*)(l_dot_pos + a_round_position + 1) = '\0';
    }

    return result;
}
