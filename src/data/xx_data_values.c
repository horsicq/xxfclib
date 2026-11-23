/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_data_values.c
 * @brief Extended binary data decoders and encoders for specialized numeric,
 * timestamp, identifier, color and floating point types.
 */

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"

/* ========================================================================= */
/* --- 1. Half Precision Floats (IEEE-754 float16 and bfloat16)          --- */
/* ========================================================================= */

double xx_data_f16_to_double(uint16_t u16)
{
    uint32_t sign = (uint32_t)(u16 >> 15);
    uint32_t exp = (uint32_t)((u16 >> 10) & 0x1FU);
    uint32_t frac = (uint32_t)(u16 & 0x3FFU);
    uint64_t sign64 = (uint64_t)sign << 63;
    uint64_t d_bits;
    double d = 0.0;

    if (exp == 0U) {
        if (frac == 0U) {
            d_bits = sign64;
        } else {
            /* Subnormal: rebias from half's -14 to double's -1022 */
            uint32_t e = 1023U - 14U;
            while ((frac & 0x400U) == 0U) {
                --e;
                frac <<= 1;
            }
            frac &= 0x3FFU;
            d_bits = sign64 | ((uint64_t)e << 52) | ((uint64_t)frac << (52 - 10));
        }
    } else if (exp == 0x1FU) {
        /* Infinity or NaN */
        d_bits = sign64 | (UINT64_C(0x7FF) << 52) | ((uint64_t)frac << (52 - 10));
    } else {
        /* Normal: rebias from 15 to 1023 */
        uint64_t e = (uint64_t)(exp + (1023U - 15U));
        d_bits = sign64 | (e << 52) | ((uint64_t)frac << (52 - 10));
    }

    xx_mem_copy(&d, &d_bits, sizeof(double));
    return d;
}

double xx_data_bf16_to_double(uint16_t u16)
{
    uint32_t f_bits = (uint32_t)u16 << 16;
    float f = 0.0f;
    xx_mem_copy(&f, &f_bits, sizeof(float));
    return (double)f;
}

/* True when [offset, offset + width) lies inside a buffer of data_size bytes.
 * Written so that no size_t addition can wrap. */
static bool xx_dv_range_ok(size_t data_size, size_t offset, size_t width)
{
    return offset <= data_size && width <= data_size - offset;
}

/* binary32 -> binary16, round to nearest, ties to even. Overflow gives an
 * infinity, a NaN stays a NaN (quiet bit set, upper payload bits kept). */
static uint16_t float_to_half(float f)
{
    uint32_t x = 0;
    xx_mem_copy(&x, &f, sizeof(float));
    uint32_t sign = (x >> 16) & 0x8000U;
    uint32_t biased = (x >> 23) & 0xFFU;
    uint32_t mant = x & 0x7FFFFFU;

    if (biased == 0xFFU) {
        if (mant == 0) return (uint16_t)(sign | 0x7C00U);
        return (uint16_t)(sign | 0x7E00U | (mant >> 13));
    }
    if (biased == 0) {
        /* Zero, or a binary32 subnormal (below 2^-126): far under half of
         * the smallest binary16 subnormal (2^-25). */
        return (uint16_t)sign;
    }

    int32_t exp = (int32_t)biased - 127;
    uint32_t sig = mant | 0x800000U; /* value = sig * 2^(exp - 23) */
    uint32_t shift;
    uint32_t half;

    if (exp > 15) return (uint16_t)(sign | 0x7C00U);

    if (exp >= -14) {
        /* Normal half: keep 10 fraction bits. */
        shift = 13;
        half = ((uint32_t)(exp + 15) << 10) | ((sig >> 13) & 0x3FFU);
    } else {
        /* Subnormal half: unit is 2^-24. */
        shift = (uint32_t)(13 + (-14 - exp));
        if (shift > 24) return (uint16_t)sign; /* below 2^-25: rounds to zero */
        half = sig >> shift;
    }

    uint32_t rem = sig & ((UINT32_C(1) << shift) - 1U);
    uint32_t tie = UINT32_C(1) << (shift - 1);
    if (rem > tie || (rem == tie && (half & 1U))) {
        /* A carry out of the fraction moves into the exponent field, which is
         * the right result (next binade, or 0x7C00 = infinity). */
        ++half;
    }
    return (uint16_t)(sign | half);
}

bool xx_data_set_f16(void *data, size_t data_size, size_t offset, float val, bool big_endian)
{
    if (!data || !xx_dv_range_ok(data_size, offset, 2)) return false;
    uint16_t half = float_to_half(val);
    return xx_data_set_u16(data, data_size, offset, half, big_endian);
}

float xx_data_get_bf16(const void *data, size_t data_size, size_t offset, bool big_endian)
{
    if (!data || !xx_dv_range_ok(data_size, offset, 2)) return 0.0f;
    uint16_t u = xx_data_get_u16(data, data_size, offset, big_endian);
    uint32_t bits = (uint32_t)u << 16;
    float f = 0.0f;
    xx_mem_copy(&f, &bits, sizeof(float));
    return f;
}

bool xx_data_set_bf16(void *data, size_t data_size, size_t offset, float val, bool big_endian)
{
    if (!data || !xx_dv_range_ok(data_size, offset, 2)) return false;
    uint32_t bits = 0;
    xx_mem_copy(&bits, &val, sizeof(float));
    uint32_t hi = bits >> 16;
    if ((bits & 0x7FFFFFFFU) > 0x7F800000U) {
        /* NaN: keep sign and upper payload, force the quiet bit. */
        hi |= 0x0040U;
    } else {
        /* Round to nearest, ties to even. A carry out of the largest finite
         * value gives the infinity, as IEEE rounding requires. */
        uint32_t rem = bits & 0xFFFFU;
        if (rem > 0x8000U || (rem == 0x8000U && (hi & 1U))) ++hi;
    }
    return xx_data_set_u16(data, data_size, offset, (uint16_t)hi, big_endian);
}

/* ========================================================================= */
/* --- 2. 48-bit Integers                                                --- */
/* ========================================================================= */

uint64_t xx_data_get_u48(const void *data, size_t data_size, size_t offset, bool big_endian)
{
    if (!data || !xx_dv_range_ok(data_size, offset, 6)) return 0;
    const uint8_t *b = (const uint8_t *)data + offset;
    if (big_endian) {
        return ((uint64_t)b[0] << 40) | ((uint64_t)b[1] << 32) |
               ((uint64_t)b[2] << 24) | ((uint64_t)b[3] << 16) |
               ((uint64_t)b[4] << 8)  | (uint64_t)b[5];
    }
    return (uint64_t)b[0] |
           ((uint64_t)b[1] << 8)  |
           ((uint64_t)b[2] << 16) |
           ((uint64_t)b[3] << 24) |
           ((uint64_t)b[4] << 32) |
           ((uint64_t)b[5] << 40);
}

int64_t xx_data_get_i48(const void *data, size_t data_size, size_t offset, bool big_endian)
{
    uint64_t u = xx_data_get_u48(data, data_size, offset, big_endian);
    if (u & (UINT64_C(1) << 47)) {
        u |= UINT64_C(0xFFFF000000000000);
    }
    return (int64_t)u;
}

bool xx_data_set_u48(void *data, size_t data_size, size_t offset, uint64_t val, bool big_endian)
{
    if (!data || !xx_dv_range_ok(data_size, offset, 6)) return false;
    uint8_t *b = (uint8_t *)data + offset;
    if (big_endian) {
        b[0] = (uint8_t)(val >> 40);
        b[1] = (uint8_t)(val >> 32);
        b[2] = (uint8_t)(val >> 24);
        b[3] = (uint8_t)(val >> 16);
        b[4] = (uint8_t)(val >> 8);
        b[5] = (uint8_t)val;
    } else {
        b[0] = (uint8_t)val;
        b[1] = (uint8_t)(val >> 8);
        b[2] = (uint8_t)(val >> 16);
        b[3] = (uint8_t)(val >> 24);
        b[4] = (uint8_t)(val >> 32);
        b[5] = (uint8_t)(val >> 40);
    }
    return true;
}

bool xx_data_set_i48(void *data, size_t data_size, size_t offset, int64_t val, bool big_endian)
{
    return xx_data_set_u48(data, data_size, offset, (uint64_t)val, big_endian);
}

/* ========================================================================= */
/* --- 3. Variable-length Integers (LEB128)                               --- */
/* ========================================================================= */

uint64_t xx_data_get_uleb128(const void *data, size_t data_size, size_t offset, size_t *consumed)
{
    if (consumed) *consumed = 0;
    if (!data || offset >= data_size) return 0;

    const uint8_t *b = (const uint8_t *)data + offset;
    size_t avail = data_size - offset;
    uint64_t val = 0;

    for (size_t i = 0; i < avail && i < 10; ++i) {
        uint8_t byte = b[i];
        if (i == 9 && (byte & 0xFEU) != 0) {
            /* 10th byte overflow */
            return 0;
        }
        val |= (uint64_t)(byte & 0x7FU) << (i * 7);
        if (!(byte & 0x80U)) {
            if (consumed) *consumed = i + 1;
            return val;
        }
    }
    return 0;
}

int64_t xx_data_get_sleb128(const void *data, size_t data_size, size_t offset, size_t *consumed)
{
    if (consumed) *consumed = 0;
    if (!data || offset >= data_size) return 0;

    const uint8_t *b = (const uint8_t *)data + offset;
    size_t avail = data_size - offset;
    uint64_t val = 0;

    for (size_t i = 0; i < avail && i < 10; ++i) {
        uint8_t byte = b[i];
        if (i == 9 && byte != 0x00U && byte != 0x7FU) {
            /* 10th byte overflow */
            return 0;
        }
        val |= (uint64_t)(byte & 0x7FU) << (i * 7);
        if (!(byte & 0x80U)) {
            unsigned shift = (unsigned)((i + 1) * 7);
            if ((byte & 0x40U) && shift < 64) {
                val |= UINT64_MAX << shift;
            }
            if (consumed) *consumed = i + 1;
            return (int64_t)val;
        }
    }
    return 0;
}

bool xx_data_set_uleb128(void *data, size_t data_size, size_t offset, uint64_t val, size_t *written)
{
    if (written) *written = 0;
    if (!data || offset >= data_size) return false;

    uint8_t buf[10];
    size_t count = 0;
    do {
        uint8_t byte = (uint8_t)(val & 0x7FU);
        val >>= 7;
        if (val != 0) byte |= 0x80U;
        buf[count++] = byte;
    } while (val != 0);

    if (!xx_dv_range_ok(data_size, offset, count)) return false;
    xx_mem_copy((uint8_t *)data + offset, buf, count);
    if (written) *written = count;
    return true;
}

bool xx_data_set_sleb128(void *data, size_t data_size, size_t offset, int64_t val, size_t *written)
{
    if (written) *written = 0;
    if (!data || offset >= data_size) return false;

    uint8_t buf[10];
    size_t count = 0;
    bool more = true;
    while (more) {
        uint8_t byte = (uint8_t)(val & 0x7FU);
        /* Arithmetic right shift, spelled out so it does not depend on how
         * the compiler shifts a negative value. */
        val = (val >= 0) ? (val >> 7) : ~(~val >> 7);
        if ((val == 0 && !(byte & 0x40U)) || (val == -1 && (byte & 0x40U))) {
            more = false;
        } else {
            byte |= 0x80U;
        }
        buf[count++] = byte;
    }

    if (!xx_dv_range_ok(data_size, offset, count)) return false;
    xx_mem_copy((uint8_t *)data + offset, buf, count);
    if (written) *written = count;
    return true;
}

/* ========================================================================= */
/* --- 4. Date and Time (Proleptic Gregorian UTC, no libc time functions) --- */
/* ========================================================================= */

static bool is_leap_year(int32_t y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int32_t y, int32_t m)
{
    static const int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && is_leap_year(y)) return 29;
    if (m >= 1 && m <= 12) return dim[m - 1];
    return 0;
}

/* Any int64 second count. The year is computed in 64 bits; an instant whose
 * year does not fit xx_data_datetime.year leaves *out zeroed (valid = false). */
static void unix_to_civil(int64_t seconds, int32_t nanoseconds, xx_data_datetime *out)
{
    if (!out) return;
    xx_mem_set(out, 0, sizeof(*out));

    int64_t days = seconds / 86400;
    int64_t rem = seconds % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }

    /* Howard Hinnant's civil date algorithm; |days| <= 2^63 / 86400, so
     * nothing below can overflow 64 bits. */
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = (int64_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t d = doy - (153 * mp + 2) / 5 + 1;
    uint32_t m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y += 1;

    if (y < INT32_MIN || y > INT32_MAX) return;

    out->year = (int32_t)y;
    out->month = (int32_t)m;
    out->day = (int32_t)d;
    out->hour = (int32_t)(rem / 3600);
    out->minute = (int32_t)((rem % 3600) / 60);
    out->second = (int32_t)(rem % 60);
    out->nanosecond = nanoseconds;
    out->valid = true;
}

/* Any int32 year: the arithmetic is 64-bit (|days| < 2^40, seconds < 2^57). */
static bool civil_to_unix(int32_t year, int32_t month, int32_t day, int32_t hour, int32_t minute, int32_t second, int64_t *out_seconds)
{
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > days_in_month(year, month)) return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return false;

    int64_t y = (int64_t)year;
    if (month <= 2) y -= 1;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t m = (uint32_t)(month > 2 ? month - 3 : month + 9);
    uint32_t doy = (153 * m + 2) / 5 + (uint32_t)day - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + (int64_t)doe - 719468;
    if (out_seconds) {
        *out_seconds = days * 86400 + (int64_t)hour * 3600 + (int64_t)minute * 60 + second;
    }
    return true;
}

xx_data_datetime xx_data_get_dos_date(const void *data, size_t data_size, size_t offset, bool big_endian)
{
    xx_data_datetime dt;
    xx_mem_set(&dt, 0, sizeof(dt));
    if (!data || !xx_dv_range_ok(data_size, offset, 2)) return dt;

    uint16_t w = xx_data_get_u16(data, data_size, offset, big_endian);
    int d = w & 0x1FU;
    int m = (w >> 5) & 0x0FU;
    int y = 1980 + ((w >> 9) & 0x7FU);

    if (m >= 1 && m <= 12 && d >= 1 && d <= days_in_month(y, m)) {
        dt.year = y;
        dt.month = m;
        dt.day = d;
        dt.valid = true;
    }
    return dt;
}

xx_data_datetime xx_data_get_dos_time(const void *data, size_t data_size, size_t offset, bool big_endian)
{
    xx_data_datetime dt;
    xx_mem_set(&dt, 0, sizeof(dt));
    if (!data || !xx_dv_range_ok(data_size, offset, 2)) return dt;

    uint16_t w = xx_data_get_u16(data, data_size, offset, big_endian);
    int s = (w & 0x1FU) * 2;
    int m = (w >> 5) & 0x3FU;
    int h = (w >> 11) & 0x1FU;

    if (h <= 23 && m <= 59 && s <= 59) {
        dt.year = 1980;
        dt.month = 1;
        dt.day = 1;
        dt.hour = h;
        dt.minute = m;
        dt.second = s;
        dt.valid = true;
    }
    return dt;
}

xx_data_datetime xx_data_get_dos_datetime(const void *data, size_t data_size, size_t offset, bool big_endian)
{
    /* FAT directory entry order: time word at offset, date word at offset + 2 */
    xx_data_datetime dt;
    xx_mem_set(&dt, 0, sizeof(dt));
    if (!data || !xx_dv_range_ok(data_size, offset, 4)) return dt;

    xx_data_datetime t = xx_data_get_dos_time(data, data_size, offset, big_endian);
    xx_data_datetime d = xx_data_get_dos_date(data, data_size, offset + 2, big_endian);

    if (t.valid && d.valid) {
        dt.year = d.year;
        dt.month = d.month;
        dt.day = d.day;
        dt.hour = t.hour;
        dt.minute = t.minute;
        dt.second = t.second;
        dt.valid = true;
    }
    return dt;
}

xx_data_datetime xx_data_get_unix_time32(const void *data, size_t data_size, size_t offset, bool big_endian)
{
    xx_data_datetime dt;
    xx_mem_set(&dt, 0, sizeof(dt));
    if (!data || !xx_dv_range_ok(data_size, offset, 4)) return dt;

    int32_t t32 = xx_data_get_i32(data, data_size, offset, big_endian);
    unix_to_civil((int64_t)t32, 0, &dt);
    return dt;
}

xx_data_datetime xx_data_get_unix_time64(const void *data, size_t data_size, size_t offset, bool big_endian)
{
    xx_data_datetime dt;
    xx_mem_set(&dt, 0, sizeof(dt));
    if (!data || !xx_dv_range_ok(data_size, offset, 8)) return dt;

    int64_t t64 = xx_data_get_i64(data, data_size, offset, big_endian);
    unix_to_civil(t64, 0, &dt);
    return dt;
}

xx_data_datetime xx_data_get_filetime(const void *data, size_t data_size, size_t offset, bool big_endian)
{
    xx_data_datetime dt;
    xx_mem_set(&dt, 0, sizeof(dt));
    if (!data || !xx_dv_range_ok(data_size, offset, 8)) return dt;

    uint64_t ft = xx_data_get_u64(data, data_size, offset, big_endian);
    int64_t sec = (int64_t)(ft / UINT64_C(10000000)) - INT64_C(11644473600);
    int32_t ns = (int32_t)((ft % UINT64_C(10000000)) * 100U);
    unix_to_civil(sec, ns, &dt);
    return dt;
}

bool xx_data_set_dos_date(void *data, size_t data_size, size_t offset, const xx_data_datetime *dt, bool big_endian)
{
    if (!data || !dt || !xx_dv_range_ok(data_size, offset, 2) || dt->year < 1980 || dt->year > 2107) return false;
    if (dt->month < 1 || dt->month > 12 || dt->day < 1 || dt->day > days_in_month(dt->year, dt->month)) return false;

    uint16_t w = (uint16_t)(((dt->year - 1980) << 9) | (dt->month << 5) | dt->day);
    return xx_data_set_u16(data, data_size, offset, w, big_endian);
}

bool xx_data_set_dos_time(void *data, size_t data_size, size_t offset, const xx_data_datetime *dt, bool big_endian)
{
    if (!data || !dt || !xx_dv_range_ok(data_size, offset, 2)) return false;
    if (dt->hour < 0 || dt->hour > 23 || dt->minute < 0 || dt->minute > 59 || dt->second < 0 || dt->second > 59) return false;

    uint16_t w = (uint16_t)((dt->hour << 11) | (dt->minute << 5) | (dt->second / 2));
    return xx_data_set_u16(data, data_size, offset, w, big_endian);
}

bool xx_data_set_dos_datetime(void *data, size_t data_size, size_t offset, const xx_data_datetime *dt, bool big_endian)
{
    /* FAT directory entry order: time word at offset, date word at offset + 2.
     * Both words are encoded into a scratch buffer first, so a date that
     * cannot be stored leaves the destination untouched. */
    uint8_t tmp[4];
    if (!data || !dt || !xx_dv_range_ok(data_size, offset, 4)) return false;
    if (!xx_data_set_dos_time(tmp, sizeof(tmp), 0, dt, big_endian)) return false;
    if (!xx_data_set_dos_date(tmp, sizeof(tmp), 2, dt, big_endian)) return false;
    xx_mem_copy((uint8_t *)data + offset, tmp, sizeof(tmp));
    return true;
}

bool xx_data_set_unix_time32(void *data, size_t data_size, size_t offset, const xx_data_datetime *dt, bool big_endian)
{
    if (!data || !dt || !xx_dv_range_ok(data_size, offset, 4)) return false;
    int64_t sec = 0;
    if (!civil_to_unix(dt->year, dt->month, dt->day, dt->hour, dt->minute, dt->second, &sec)) return false;
    if (sec < INT32_MIN || sec > INT32_MAX) return false;
    return xx_data_set_i32(data, data_size, offset, (int32_t)sec, big_endian);
}

bool xx_data_set_unix_time64(void *data, size_t data_size, size_t offset, const xx_data_datetime *dt, bool big_endian)
{
    if (!data || !dt || !xx_dv_range_ok(data_size, offset, 8)) return false;
    int64_t sec = 0;
    if (!civil_to_unix(dt->year, dt->month, dt->day, dt->hour, dt->minute, dt->second, &sec)) return false;
    return xx_data_set_i64(data, data_size, offset, sec, big_endian);
}

bool xx_data_set_filetime(void *data, size_t data_size, size_t offset, const xx_data_datetime *dt, bool big_endian)
{
    const int64_t ticks_per_sec = INT64_C(10000000);
    const uint64_t max_sec = UINT64_MAX / UINT64_C(10000000);

    if (!data || !dt || !xx_dv_range_ok(data_size, offset, 8)) return false;
    int64_t sec = 0;
    if (!civil_to_unix(dt->year, dt->month, dt->day, dt->hour, dt->minute, dt->second, &sec)) return false;

    /* The instant is seconds + nanosecond; the nanosecond field may be any
     * int32 (it is floored to 100 ns ticks and carried into the seconds).
     * |sec| < 2^57, so the sums below cannot overflow. */
    int64_t ft_sec = sec + INT64_C(11644473600);
    int64_t sub = (int64_t)dt->nanosecond / 100;
    if ((int64_t)dt->nanosecond % 100 < 0) sub -= 1;
    int64_t carry = sub / ticks_per_sec;
    sub -= carry * ticks_per_sec;
    if (sub < 0) {
        sub += ticks_per_sec;
        carry -= 1;
    }
    ft_sec += carry;

    if (ft_sec < 0) return false;                  /* before 1601-01-01 */
    if ((uint64_t)ft_sec > max_sec) return false;  /* beyond UINT64_MAX ticks */
    uint64_t ft = (uint64_t)ft_sec * UINT64_C(10000000);
    if ((uint64_t)sub > UINT64_MAX - ft) return false;
    ft += (uint64_t)sub;
    return xx_data_set_u64(data, data_size, offset, ft, big_endian);
}

static void append_two_digits(char *buf, int n)
{
    unsigned u = (unsigned)n % 100U;
    buf[0] = (char)('0' + u / 10U);
    buf[1] = (char)('0' + u % 10U);
}

/* Years 0..9999 are written as four digits. Any other year uses the ISO 8601
 * expanded form: a sign and at least four digits (+10000, -0001). Returns the
 * number of characters written (at most 11). */
static size_t format_year(int32_t year, char *out)
{
    char digits[10];
    size_t count = 0;
    size_t pos = 0;
    uint32_t mag = year < 0 ? (uint32_t)0 - (uint32_t)year : (uint32_t)year;

    if (year < 0 || year > 9999) out[pos++] = year < 0 ? '-' : '+';
    do {
        digits[count++] = (char)('0' + mag % 10U);
        mag /= 10U;
    } while (mag != 0);
    while (count < 4) digits[count++] = '0';
    while (count > 0) out[pos++] = digits[--count];
    return pos;
}

/* <year>-MM-DD<sep>HH:MM:SS<suffix>; false only when it does not fit in cap
 * (the buffer then holds an empty string when cap > 0). */
static bool format_datetime(const xx_data_datetime *dt, char sep, const char *suffix, char *buf, size_t cap)
{
    char tmp[40];
    size_t pos = 0;

    if (!dt || !buf) return false;
    if (cap > 0) buf[0] = '\0';

    if (!dt->valid) {
        tmp[pos++] = 'n';
        tmp[pos++] = '/';
        tmp[pos++] = 'a';
    } else {
        pos = format_year(dt->year, tmp);
        tmp[pos++] = '-';
        append_two_digits(tmp + pos, dt->month);
        pos += 2;
        tmp[pos++] = '-';
        append_two_digits(tmp + pos, dt->day);
        pos += 2;
        tmp[pos++] = sep;
        append_two_digits(tmp + pos, dt->hour);
        pos += 2;
        tmp[pos++] = ':';
        append_two_digits(tmp + pos, dt->minute);
        pos += 2;
        tmp[pos++] = ':';
        append_two_digits(tmp + pos, dt->second);
        pos += 2;
        while (*suffix) tmp[pos++] = *suffix++;
    }
    tmp[pos++] = '\0';

    if (pos > cap) return false;
    xx_mem_copy(buf, tmp, pos);
    return true;
}

bool xx_data_datetime_to_iso8601(const xx_data_datetime *dt, char *buf, size_t cap)
{
    /* YYYY-MM-DDTHH:MM:SSZ (21 bytes with the terminator; up to 28 for an
     * expanded year); "n/a" for an invalid value. */
    return format_datetime(dt, 'T', "Z", buf, cap);
}

bool xx_data_datetime_to_display(const xx_data_datetime *dt, char *buf, size_t cap)
{
    /* YYYY-MM-DD HH:MM:SS UTC (24 bytes with the terminator; up to 31 for an
     * expanded year); "n/a" for an invalid value. */
    return format_datetime(dt, ' ', " UTC", buf, cap);
}

/* ========================================================================= */
/* --- 5. GUID / UUID Formats                                            --- */
/* ========================================================================= */

xx_data_guid xx_data_get_guid(const void *data, size_t data_size, size_t offset, bool microsoft_mixed_endian)
{
    xx_data_guid guid;
    xx_mem_set(&guid, 0, sizeof(guid));
    if (!data || !xx_dv_range_ok(data_size, offset, 16)) return guid;

    const uint8_t *b = (const uint8_t *)data + offset;
    if (microsoft_mixed_endian) {
        guid.data1 = xx_data_get_le32(b);
        guid.data2 = xx_data_get_le16(b + 4);
        guid.data3 = xx_data_get_le16(b + 6);
    } else {
        guid.data1 = xx_data_get_be32(b);
        guid.data2 = xx_data_get_be16(b + 4);
        guid.data3 = xx_data_get_be16(b + 6);
    }
    xx_mem_copy(guid.data4, b + 8, 8);
    return guid;
}

bool xx_data_set_guid(void *data, size_t data_size, size_t offset, const xx_data_guid *guid, bool microsoft_mixed_endian)
{
    if (!data || !guid || !xx_dv_range_ok(data_size, offset, 16)) return false;
    uint8_t *b = (uint8_t *)data + offset;
    if (microsoft_mixed_endian) {
        xx_data_set_le32(b, guid->data1);
        xx_data_set_le16(b + 4, guid->data2);
        xx_data_set_le16(b + 6, guid->data3);
    } else {
        xx_data_set_be32(b, guid->data1);
        xx_data_set_be16(b + 4, guid->data2);
        xx_data_set_be16(b + 6, guid->data3);
    }
    xx_mem_copy(b + 8, guid->data4, 8);
    return true;
}

static void byte_to_hex(uint8_t b, char *out)
{
    static const char hex[] = "0123456789ABCDEF";
    out[0] = hex[(b >> 4) & 0x0FU];
    out[1] = hex[b & 0x0FU];
}

bool xx_data_guid_to_string(const xx_data_guid *guid, char *buf, size_t cap)
{
    if (!guid || !buf || cap < 37) return false;
    /* XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX */
    byte_to_hex((uint8_t)(guid->data1 >> 24), buf + 0);
    byte_to_hex((uint8_t)(guid->data1 >> 16), buf + 2);
    byte_to_hex((uint8_t)(guid->data1 >> 8),  buf + 4);
    byte_to_hex((uint8_t)(guid->data1),       buf + 6);
    buf[8] = '-';
    byte_to_hex((uint8_t)(guid->data2 >> 8),  buf + 9);
    byte_to_hex((uint8_t)(guid->data2),       buf + 11);
    buf[13] = '-';
    byte_to_hex((uint8_t)(guid->data3 >> 8),  buf + 14);
    byte_to_hex((uint8_t)(guid->data3),       buf + 16);
    buf[18] = '-';
    byte_to_hex(guid->data4[0], buf + 19);
    byte_to_hex(guid->data4[1], buf + 21);
    buf[23] = '-';
    for (int i = 0; i < 6; ++i) {
        byte_to_hex(guid->data4[2 + i], buf + 24 + i * 2);
    }
    buf[36] = '\0';
    return true;
}

static int hex_char_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool xx_data_guid_from_string(const char *str, xx_data_guid *guid)
{
    if (!str || !guid) return false;
    while (*str == ' ' || *str == '\t') ++str;
    bool has_brace = false;
    if (*str == '{') { has_brace = true; ++str; }

    uint8_t bytes[16];
    size_t byte_idx = 0;

    for (int group = 0; group < 5; ++group) {
        int expected_hex = (group == 0 ? 8 : (group == 4 ? 12 : 4));
        for (int i = 0; i < expected_hex; i += 2) {
            /* Stop at the first non-hex character: the terminator is one,
             * so nothing past the end of the string is ever read. */
            int h1 = hex_char_val(*str);
            if (h1 < 0) return false;
            ++str;
            int h2 = hex_char_val(*str);
            if (h2 < 0) return false;
            ++str;
            bytes[byte_idx++] = (uint8_t)((h1 << 4) | h2);
        }
        if (group < 4) {
            if (*str != '-') return false;
            ++str;
        }
    }
    if (has_brace) {
        if (*str != '}') return false;
    }

    guid->data1 = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
    guid->data2 = ((uint16_t)bytes[4] << 8) | (uint16_t)bytes[5];
    guid->data3 = ((uint16_t)bytes[6] << 8) | (uint16_t)bytes[7];
    xx_mem_copy(guid->data4, bytes + 8, 8);
    return true;
}

/* ========================================================================= */
/* --- 6. Color Formats (RGB565 and RGBA8)                               --- */
/* ========================================================================= */

uint32_t xx_data_get_rgb565(const void *data, size_t data_size, size_t offset, bool big_endian)
{
    if (!data || !xx_dv_range_ok(data_size, offset, 2)) return 0;
    uint16_t rgb565 = xx_data_get_u16(data, data_size, offset, big_endian);
    uint8_t r5 = (uint8_t)((rgb565 >> 11) & 0x1FU);
    uint8_t g6 = (uint8_t)((rgb565 >> 5) & 0x3FU);
    uint8_t b5 = (uint8_t)(rgb565 & 0x1FU);
    uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
    uint8_t g = (uint8_t)((g6 << 2) | (g6 >> 4));
    uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

bool xx_data_set_rgb565(void *data, size_t data_size, size_t offset, uint32_t rgb, bool big_endian)
{
    if (!data || !xx_dv_range_ok(data_size, offset, 2)) return false;
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFFU);
    uint8_t g = (uint8_t)((rgb >> 8) & 0xFFU);
    uint8_t b = (uint8_t)(rgb & 0xFFU);
    uint16_t r5 = (uint16_t)((r >> 3) & 0x1FU);
    uint16_t g6 = (uint16_t)((g >> 2) & 0x3FU);
    uint16_t b5 = (uint16_t)((b >> 3) & 0x1FU);
    uint16_t w = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
    return xx_data_set_u16(data, data_size, offset, w, big_endian);
}

bool xx_data_rgb565_to_hex(uint16_t rgb565, char *buf, size_t cap)
{
    if (!buf || cap < 8) return false;
    uint8_t r5 = (uint8_t)((rgb565 >> 11) & 0x1FU);
    uint8_t g6 = (uint8_t)((rgb565 >> 5) & 0x3FU);
    uint8_t b5 = (uint8_t)(rgb565 & 0x1FU);
    uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
    uint8_t g = (uint8_t)((g6 << 2) | (g6 >> 4));
    uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));
    buf[0] = '#';
    byte_to_hex(r, buf + 1);
    byte_to_hex(g, buf + 3);
    byte_to_hex(b, buf + 5);
    buf[7] = '\0';
    return true;
}

bool xx_data_rgba8_to_hex(const uint8_t bytes[4], char *buf, size_t cap)
{
    if (!bytes || !buf || cap < 10) return false;
    buf[0] = '#';
    byte_to_hex(bytes[0], buf + 1);
    byte_to_hex(bytes[1], buf + 3);
    byte_to_hex(bytes[2], buf + 5);
    byte_to_hex(bytes[3], buf + 7);
    buf[9] = '\0';
    return true;
}

/* ========================================================================= */
/* --- 7. IEEE-754 Floating Point Breakdown                              --- */
/* ========================================================================= */

const char *xx_data_float_class_name(xx_data_float_class cls)
{
    switch (cls) {
        case XX_DATA_FLOAT_ZERO: return "Zero";
        case XX_DATA_FLOAT_SUBNORMAL: return "Subnormal";
        case XX_DATA_FLOAT_NORMAL: return "Normal";
        case XX_DATA_FLOAT_INFINITY: return "Infinity";
        case XX_DATA_FLOAT_NAN: return "NaN";
        default: return "Unknown";
    }
}

bool xx_data_f16_parts(uint64_t raw_bits, xx_data_float_parts *parts)
{
    if (!parts) return false;
    uint16_t bits = (uint16_t)raw_bits;
    parts->sign = (bits >> 15) & 1;
    parts->exponent_raw = (bits >> 10) & 0x1FU;
    parts->mantissa = bits & 0x3FFU;

    if (parts->exponent_raw == 0) {
        if (parts->mantissa == 0) {
            parts->num_class = XX_DATA_FLOAT_ZERO;
            parts->exponent = 0;
        } else {
            parts->num_class = XX_DATA_FLOAT_SUBNORMAL;
            parts->exponent = -14;
        }
    } else if (parts->exponent_raw == 0x1F) {
        if (parts->mantissa == 0) {
            parts->num_class = XX_DATA_FLOAT_INFINITY;
        } else {
            parts->num_class = XX_DATA_FLOAT_NAN;
        }
        parts->exponent = 0;
    } else {
        parts->num_class = XX_DATA_FLOAT_NORMAL;
        parts->exponent = (int64_t)parts->exponent_raw - 15;
    }
    return true;
}

bool xx_data_f32_parts(uint64_t raw_bits, xx_data_float_parts *parts)
{
    if (!parts) return false;
    uint32_t bits = (uint32_t)raw_bits;
    parts->sign = (int)((bits >> 31) & 1);
    parts->exponent_raw = (bits >> 23) & 0xFFU;
    parts->mantissa = bits & 0x7FFFFFU;

    if (parts->exponent_raw == 0) {
        if (parts->mantissa == 0) {
            parts->num_class = XX_DATA_FLOAT_ZERO;
            parts->exponent = 0;
        } else {
            parts->num_class = XX_DATA_FLOAT_SUBNORMAL;
            parts->exponent = -126;
        }
    } else if (parts->exponent_raw == 0xFF) {
        if (parts->mantissa == 0) {
            parts->num_class = XX_DATA_FLOAT_INFINITY;
        } else {
            parts->num_class = XX_DATA_FLOAT_NAN;
        }
        parts->exponent = 0;
    } else {
        parts->num_class = XX_DATA_FLOAT_NORMAL;
        parts->exponent = (int64_t)parts->exponent_raw - 127;
    }
    return true;
}

bool xx_data_f64_parts(uint64_t raw_bits, xx_data_float_parts *parts)
{
    if (!parts) return false;
    parts->sign = (int)((raw_bits >> 63) & 1);
    parts->exponent_raw = (raw_bits >> 52) & 0x7FFU;
    parts->mantissa = raw_bits & UINT64_C(0xFFFFFFFFFFFFF);

    if (parts->exponent_raw == 0) {
        if (parts->mantissa == 0) {
            parts->num_class = XX_DATA_FLOAT_ZERO;
            parts->exponent = 0;
        } else {
            parts->num_class = XX_DATA_FLOAT_SUBNORMAL;
            parts->exponent = -1022;
        }
    } else if (parts->exponent_raw == 0x7FF) {
        if (parts->mantissa == 0) {
            parts->num_class = XX_DATA_FLOAT_INFINITY;
        } else {
            parts->num_class = XX_DATA_FLOAT_NAN;
        }
        parts->exponent = 0;
    } else {
        parts->num_class = XX_DATA_FLOAT_NORMAL;
        parts->exponent = (int64_t)parts->exponent_raw - 1023;
    }
    return true;
}

/* Exact decimal expansion of M * 2^E.
 *
 * Supported exponents cover binary16/32/64 and the x87 80-bit format with a
 * full 64-bit significand: E in [-16445, 16383]. The work number is kept in
 * base 10^9 limbs:
 *   - E >= 0: the integer M * 2^E, below 2^(64 + 16383), at most 4952 digits;
 *   - E < 0:  the fraction digits are the integer F' * 5^K' (F' odd, below
 *     2^64, K' <= 16445), at most 20 + 11495 digits.
 * 1284 limbs hold 11556 digits. */
#define XX_EXACT_MIN_E (-16445)
#define XX_EXACT_MAX_E 16383
#define XX_EXACT_LIMBS 1284
#define XX_EXACT_BASE UINT32_C(1000000000)

static size_t exact_load_u64(uint32_t *limbs, uint64_t v)
{
    size_t count = 0;
    do {
        limbs[count++] = (uint32_t)(v % XX_EXACT_BASE);
        v /= XX_EXACT_BASE;
    } while (v != 0);
    return count;
}

/* limbs *= mult, mult < 10^9 (so the carry always fits one limb). */
static void exact_mul_small(uint32_t *limbs, size_t *count, uint32_t mult)
{
    uint64_t carry = 0;
    for (size_t i = 0; i < *count; ++i) {
        uint64_t v = (uint64_t)limbs[i] * mult + carry;
        limbs[i] = (uint32_t)(v % XX_EXACT_BASE);
        carry = v / XX_EXACT_BASE;
    }
    if (carry != 0) limbs[(*count)++] = (uint32_t)carry;
}

/* limbs *= base^power, where chunk = base^chunk_power < 10^9. */
static void exact_mul_pow(uint32_t *limbs, size_t *count, uint32_t base, uint32_t chunk, unsigned chunk_power, unsigned power)
{
    while (power >= chunk_power) {
        exact_mul_small(limbs, count, chunk);
        power -= chunk_power;
    }
    if (power > 0) {
        uint32_t mult = 1;
        while (power-- > 0) mult *= base;
        exact_mul_small(limbs, count, mult);
    }
}

static size_t exact_digit_count(const uint32_t *limbs, size_t count)
{
    uint32_t top = limbs[count - 1];
    size_t n = 1;
    while (top >= 10U) {
        top /= 10U;
        ++n;
    }
    return (count - 1) * 9 + n;
}

/* Writes the number right-aligned into out[0 .. ndigits), zero-padded on the
 * left; ndigits must be at least exact_digit_count(). */
static void exact_write_digits(const uint32_t *limbs, size_t count, char *out, size_t ndigits)
{
    size_t pos = ndigits;
    for (size_t i = 0; i < count && pos > 0; ++i) {
        uint32_t v = limbs[i];
        for (int j = 0; j < 9 && pos > 0; ++j) {
            out[--pos] = (char)('0' + v % 10U);
            v /= 10U;
        }
    }
    while (pos > 0) out[--pos] = '0';
}

bool xx_data_float_exact_decimal(int sign, uint64_t M, int E, char *buf, size_t cap)
{
    uint32_t limbs[XX_EXACT_LIMBS];
    size_t count = 0;
    size_t sign_len = sign ? 1 : 0;
    size_t pos = 0;

    if (!buf || cap == 0) return false;
    buf[0] = '\0';

    if (M == 0) {
        if (cap < sign_len + 4) return false;
        if (sign) buf[pos++] = '-';
        buf[pos++] = '0';
        buf[pos++] = '.';
        buf[pos++] = '0';
        buf[pos] = '\0';
        return true;
    }

    if (E < XX_EXACT_MIN_E || E > XX_EXACT_MAX_E) return false;

    if (E >= 0) {
        /* Integer M * 2^E, printed as "<digits>.0". */
        count = exact_load_u64(limbs, M);
        exact_mul_pow(limbs, &count, 2U, UINT32_C(1) << 29, 29U, (unsigned)E);

        size_t ndigits = exact_digit_count(limbs, count);
        if (cap < sign_len + ndigits + 3) return false;
        if (sign) buf[pos++] = '-';
        exact_write_digits(limbs, count, buf + pos, ndigits);
        pos += ndigits;
        buf[pos++] = '.';
        buf[pos++] = '0';
        buf[pos] = '\0';
        return true;
    }

    /* M / 2^K, K = -E in [1, 16445]. */
    unsigned K = (unsigned)(-E);
    uint64_t int_part = 0;
    uint64_t frac = M; /* numerator of the fraction frac / 2^K */
    if (K < 64) {
        int_part = M >> K;
        frac = M & ((UINT64_C(1) << K) - 1U);
    }

    char int_digits[20];
    size_t int_len = 0;
    do {
        int_digits[int_len++] = (char)('0' + (int_part % 10U));
        int_part /= 10U;
    } while (int_part != 0);

    size_t frac_digits = 1; /* ".0" when there is no fraction */
    if (frac != 0) {
        /* Reduce to an odd numerator: frac / 2^K then has exactly K decimal
         * digits, and they are the integer frac * 5^K padded to K places. */
        while ((frac & 1U) == 0) {
            frac >>= 1;
            --K;
        }
        count = exact_load_u64(limbs, frac);
        exact_mul_pow(limbs, &count, 5U, UINT32_C(244140625), 12U, K); /* 5^12 */
        frac_digits = K;
    }

    if (cap < sign_len + int_len + 1 + frac_digits + 1) return false;
    if (sign) buf[pos++] = '-';
    while (int_len > 0) buf[pos++] = int_digits[--int_len];
    buf[pos++] = '.';
    if (frac != 0) {
        exact_write_digits(limbs, count, buf + pos, frac_digits);
    } else {
        buf[pos] = '0';
    }
    pos += frac_digits;
    buf[pos] = '\0';
    return true;
}
