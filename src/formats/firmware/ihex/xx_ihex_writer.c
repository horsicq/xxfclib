/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ihex_writer.c @brief Intel HEX (IHex) binary-to-text encoder. */

#include "xxfclib/formats/firmware/ihex/xx_ihex_writer.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>

static bool write_line(xx_io_device *dest, const char *line, size_t len)
{
    if (!dest || !line || len == 0) return false;
    ssize_t written = xx_io_write(dest, (const uint8_t *)line, len);
    return written == (ssize_t)len;
}

static bool emit_type04(xx_io_device *dest, uint16_t upper)
{
    char line[32];
    uint32_t sum = 2U + 0U + 4U + (uint32_t)(upper >> 8) + (uint32_t)(upper & 0xFFU);
    uint8_t chk = (uint8_t)(-(int)(sum & 0xFFU));
    int n = snprintf(line, sizeof(line), ":02000004%04X%02X\r\n", (unsigned)upper, (unsigned)chk);
    if (n <= 0) return false;
    return write_line(dest, line, (size_t)n);
}

static bool emit_eof(xx_io_device *dest)
{
    static const char eof_line[] = ":00000001FF\r\n";
    return write_line(dest, eof_line, sizeof(eof_line) - 1U);
}

static bool emit_data(xx_io_device *dest, uint16_t addr_16, const uint8_t *data, size_t chunk)
{
    char line[600];
    static const char hex_digits[] = "0123456789ABCDEF";
    uint32_t sum = (uint32_t)chunk + (uint32_t)(addr_16 >> 8) + (uint32_t)(addr_16 & 0xFFU) + 0x00U;
    size_t pos = 0;

    line[pos++] = ':';
    line[pos++] = hex_digits[(chunk >> 4) & 0x0FU];
    line[pos++] = hex_digits[chunk & 0x0FU];
    line[pos++] = hex_digits[(addr_16 >> 12) & 0x0FU];
    line[pos++] = hex_digits[(addr_16 >> 8) & 0x0FU];
    line[pos++] = hex_digits[(addr_16 >> 4) & 0x0FU];
    line[pos++] = hex_digits[addr_16 & 0x0FU];
    line[pos++] = '0';
    line[pos++] = '0';

    for (size_t i = 0; i < chunk; ++i) {
        uint8_t b = data[i];
        sum += b;
        line[pos++] = hex_digits[(b >> 4) & 0x0FU];
        line[pos++] = hex_digits[b & 0x0FU];
    }

    uint8_t chk = (uint8_t)(-(int)(sum & 0xFFU));
    line[pos++] = hex_digits[(chk >> 4) & 0x0FU];
    line[pos++] = hex_digits[chk & 0x0FU];
    line[pos++] = '\r';
    line[pos++] = '\n';
    line[pos] = '\0';

    return write_line(dest, line, pos);
}

XXFC_API bool xx_ihex_write_buffer(const uint8_t *data, size_t size,
                                   uint64_t base_address, unsigned line_length,
                                   xx_io_device *dest, xx_pd_struct *pd)
{
    if (!dest) return false;
    if (line_length == 0U) line_length = XX_IHEX_DEFAULT_LINE_LENGTH;
    if (line_length > XX_IHEX_MAX_DATA_PER_LINE) line_length = XX_IHEX_MAX_DATA_PER_LINE;

    if (size == 0U) {
        return emit_eof(dest);
    }
    if (!data) return false;

    uint64_t current_addr = base_address;
    uint32_t active_linear = 0xFFFFFFFFU;
    bool needs_linear = (base_address >= 0x10000ULL) || ((base_address + size) > 0x10000ULL);
    size_t remaining = size;
    const uint8_t *ptr = data;

    while (remaining > 0U) {
        if (pd && xx_pd_is_stopped(pd)) return false;

        uint32_t upper = (uint32_t)(current_addr >> 16);
        if (needs_linear && upper != active_linear) {
            if (!emit_type04(dest, (uint16_t)upper)) return false;
            active_linear = upper;
        }

        uint16_t addr_16 = (uint16_t)(current_addr & 0xFFFFU);
        size_t max_in_segment = (size_t)(0x10000U - addr_16);
        size_t chunk = line_length;
        if (chunk > max_in_segment) chunk = max_in_segment;
        if (chunk > remaining) chunk = remaining;

        if (!emit_data(dest, addr_16, ptr, chunk)) return false;

        ptr += chunk;
        remaining -= chunk;
        current_addr += chunk;
    }

    return emit_eof(dest);
}

XXFC_API bool xx_ihex_write_device(xx_io_device *src, int64_t src_offset, size_t count,
                                   uint64_t base_address, unsigned line_length,
                                   xx_io_device *dest, xx_pd_struct *pd)
{
    if (!src || !dest || src_offset < 0) return false;
    if (count == 0U) return emit_eof(dest);

    uint8_t buffer[65536];
    size_t remaining = count;
    int64_t cur_src = src_offset;
    uint64_t cur_base = base_address;

    if (line_length == 0U) line_length = XX_IHEX_DEFAULT_LINE_LENGTH;
    if (line_length > XX_IHEX_MAX_DATA_PER_LINE) line_length = XX_IHEX_MAX_DATA_PER_LINE;

    uint32_t active_linear = 0xFFFFFFFFU;
    bool needs_linear = (base_address >= 0x10000ULL) || ((base_address + count) > 0x10000ULL);

    while (remaining > 0U) {
        if (pd && xx_pd_is_stopped(pd)) return false;
        size_t to_read = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        if (xx_io_seek64(src, cur_src, SEEK_SET) != 0) return false;
        ssize_t bytes_read = xx_io_read(src, buffer, to_read);
        if (bytes_read <= 0 || (size_t)bytes_read != to_read) return false;

        const uint8_t *ptr = buffer;
        size_t buf_remain = to_read;

        while (buf_remain > 0U) {
            uint32_t upper = (uint32_t)(cur_base >> 16);
            if (needs_linear && upper != active_linear) {
                if (!emit_type04(dest, (uint16_t)upper)) return false;
                active_linear = upper;
            }

            uint16_t addr_16 = (uint16_t)(cur_base & 0xFFFFU);
            size_t max_in_segment = (size_t)(0x10000U - addr_16);
            size_t chunk = line_length;
            if (chunk > max_in_segment) chunk = max_in_segment;
            if (chunk > buf_remain) chunk = buf_remain;

            if (!emit_data(dest, addr_16, ptr, chunk)) return false;

            ptr += chunk;
            buf_remain -= chunk;
            cur_base += chunk;
        }

        cur_src += to_read;
        remaining -= to_read;
    }

    return emit_eof(dest);
}
