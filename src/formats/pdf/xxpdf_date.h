/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_PDF_DATE_H
#define XXFCLIB_PDF_DATE_H

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"

static inline bool xx_pdf_date_space(unsigned char c)
{
    return c == ' ' || (c >= 9U && c <= 13U);
}

/* Legacy QString-toInt semantics for the short fixed-width date fields. */
static inline int xx_pdf_date_field(const char *text, size_t start, size_t size)
{
    size_t i = 0U;
    int sign = 1, value = 0;
    bool digit = false;
    while (i < size && text[start + i] == ' ') ++i;
    if (i < size && (text[start + i] == '+' || text[start + i] == '-')) {
        if (text[start + i] == '-') sign = -1;
        ++i;
    }
    while (i < size && text[start + i] >= '0' && text[start + i] <= '9') {
        value = value * 10 + text[start + i] - '0';
        ++i;
        digit = true;
    }
    while (i < size && text[start + i] == ' ') ++i;
    return digit && i == size ? sign * value : 0;
}

/* Render a decoded PDF literal with the legacy metadata-date convention.
 * Short D: strings and other strings are copied unchanged. Invalid long
 * dates return an allocated empty string. The caller uses xx_str_free().
 * Kept private and inline so no extra build-system registration is needed. */
static inline char *xx_pdf_date_text(const char *decoded)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    char clean[15], timezone[8], formatted[64];
    size_t length, begin = 2U, end, marker = 0U, i, count = 0U, tz_count = 0U;
    int year, month = 1, day = 1, hour = 0, minute = 0, second = 0;
    int tz_sign = 0, tz_hour = 0, tz_minute = 0, offset = 0, month_days;
    bool has_timezone = false;
    if (!decoded) return xx_str_create("");
    length = xx_rt_strlen(decoded);
    if (length < 16U || decoded[0] != 'D' || decoded[1] != ':') return xx_str_create(decoded);
    end = begin;
    while (end < length && decoded[end] != ')') ++end;
    while (begin < end && xx_pdf_date_space((unsigned char)decoded[begin])) ++begin;
    while (end > begin && xx_pdf_date_space((unsigned char)decoded[end - 1U])) --end;
    for (i = begin; i < end; ++i) {
        char c = decoded[i];
        if (c == '+' || c == '-' || c == 'Z' || c == 'z') {
            marker = i;
            has_timezone = true;
            break;
        }
    }
    if (has_timezone) {
        if (decoded[marker] == '+') tz_sign = 1;
        else if (decoded[marker] == '-') tz_sign = -1;
        for (i = marker + 1U; i < end && tz_count + 1U < sizeof(timezone); ++i)
            if (decoded[i] != '\'') timezone[tz_count++] = decoded[i];
        timezone[tz_count] = 0;
        if (tz_count >= 2U) tz_hour = xx_pdf_date_field(timezone, 0U, 2U);
        if (tz_count >= 4U) tz_minute = xx_pdf_date_field(timezone, 2U, 2U);
        end = marker;
    }
    for (i = begin; i < end && count < 14U; ++i)
        if (decoded[i] >= '0' && decoded[i] <= '9') clean[count++] = decoded[i];
    clean[count] = 0;
    if (count < 4U) return xx_str_create("");
    year = xx_pdf_date_field(clean, 0U, 4U);
    if (count >= 6U) month = xx_pdf_date_field(clean, 4U, 2U);
    if (count >= 8U) day = xx_pdf_date_field(clean, 6U, 2U);
    if (count >= 10U) hour = xx_pdf_date_field(clean, 8U, 2U);
    if (count >= 12U) minute = xx_pdf_date_field(clean, 10U, 2U);
    if (count >= 14U) second = xx_pdf_date_field(clean, 12U, 2U);
    if (month < 1) month = 1;
    if (month > 12) month = 12;
    if (day < 1) day = 1;
    if (day > 31) day = 31;
    if (hour > 23) hour = 23;
    if (minute > 59) minute = 59;
    if (second > 59) second = 59;
    month_days = days[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) month_days = 29;
    if (!year || day > month_days) return xx_str_create("");
    offset = tz_sign * (tz_hour * 3600 + tz_minute * 60);
    if (offset < -14 * 3600 || offset > 14 * 3600) return xx_str_create("");
    (void)xx_rt_snprintf(formatted, sizeof(formatted), "%04d-%02d-%02dT%02d:%02d:%02d.000%c%02d:%02d", year, month, day, hour, minute, second, offset >= 0 ? '+' : '-',
                         (offset < 0 ? -offset : offset) / 3600, ((offset < 0 ? -offset : offset) / 60) % 60);
    return xx_str_create(formatted);
}

#endif
