/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_srec_writer.c @brief Motorola S-record binary-to-text encoder. */

#include "xxfclib/formats/firmware/srec/xx_srec_writer.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include <string.h>

static bool write_line(xx_io_device *dest, const char *line, size_t len)
{
    if (!dest || !line || len == 0) return false;
    ssize_t written = xx_io_write(dest, (const uint8_t *)line, len);
    return written == (ssize_t)len;
}

static bool emit_s0(xx_io_device *dest, const char *header_text)
{
    if (!header_text) header_text = "HDR";
    size_t text_len = strlen(header_text);
    if (text_len > 64U) text_len = 64U;

    char line[200];
    static const char hex_digits[] = "0123456789ABCDEF";
    uint32_t count = (uint32_t)text_len + 3U; /* 2 addr bytes + 1 chk */
    uint32_t sum = count; /* addr is 0000 */
    size_t pos = 0;

    line[pos++] = 'S';
    line[pos++] = '0';
    line[pos++] = hex_digits[(count >> 4) & 0x0FU];
    line[pos++] = hex_digits[count & 0x0FU];
    line[pos++] = '0';
    line[pos++] = '0';
    line[pos++] = '0';
    line[pos++] = '0';

    for (size_t i = 0; i < text_len; ++i) {
        uint8_t b = (uint8_t)header_text[i];
        sum += b;
        line[pos++] = hex_digits[(b >> 4) & 0x0FU];
        line[pos++] = hex_digits[b & 0x0FU];
    }

    uint8_t chk = (uint8_t)(0xFFU - (sum & 0xFFU));
    line[pos++] = hex_digits[(chk >> 4) & 0x0FU];
    line[pos++] = hex_digits[chk & 0x0FU];
    line[pos++] = '\r';
    line[pos++] = '\n';
    line[pos] = '\0';

    return write_line(dest, line, pos);
}

static bool emit_srec_data(xx_io_device *dest, int type, uint64_t addr, const uint8_t *data, size_t chunk)
{
    char line[600];
    static const char hex_digits[] = "0123456789ABCDEF";
    unsigned addr_bytes = (type == 1) ? 2U : (type == 2) ? 3U : 4U;
    uint32_t count = (uint32_t)chunk + addr_bytes + 1U;
    uint32_t sum = count;
    size_t pos = 0;

    line[pos++] = 'S';
    line[pos++] = (char)('0' + type);
    line[pos++] = hex_digits[(count >> 4) & 0x0FU];
    line[pos++] = hex_digits[count & 0x0FU];

    for (int i = (int)addr_bytes - 1; i >= 0; --i) {
        uint8_t ab = (uint8_t)((addr >> (i * 8)) & 0xFFU);
        sum += ab;
        line[pos++] = hex_digits[(ab >> 4) & 0x0FU];
        line[pos++] = hex_digits[ab & 0x0FU];
    }

    for (size_t i = 0; i < chunk; ++i) {
        uint8_t b = data[i];
        sum += b;
        line[pos++] = hex_digits[(b >> 4) & 0x0FU];
        line[pos++] = hex_digits[b & 0x0FU];
    }

    uint8_t chk = (uint8_t)(0xFFU - (sum & 0xFFU));
    line[pos++] = hex_digits[(chk >> 4) & 0x0FU];
    line[pos++] = hex_digits[chk & 0x0FU];
    line[pos++] = '\r';
    line[pos++] = '\n';
    line[pos] = '\0';

    return write_line(dest, line, pos);
}

static bool emit_termination(xx_io_device *dest, int type, uint64_t entry_point)
{
    char line[64];
    static const char hex_digits[] = "0123456789ABCDEF";
    int term_type = (type == 1) ? 9 : (type == 2) ? 8 : 7;
    unsigned addr_bytes = (type == 1) ? 2U : (type == 2) ? 3U : 4U;
    uint32_t count = addr_bytes + 1U;
    uint32_t sum = count;
    size_t pos = 0;

    line[pos++] = 'S';
    line[pos++] = (char)('0' + term_type);
    line[pos++] = hex_digits[(count >> 4) & 0x0FU];
    line[pos++] = hex_digits[count & 0x0FU];

    for (int i = (int)addr_bytes - 1; i >= 0; --i) {
        uint8_t ab = (uint8_t)((entry_point >> (i * 8)) & 0xFFU);
        sum += ab;
        line[pos++] = hex_digits[(ab >> 4) & 0x0FU];
        line[pos++] = hex_digits[ab & 0x0FU];
    }

    uint8_t chk = (uint8_t)(0xFFU - (sum & 0xFFU));
    line[pos++] = hex_digits[(chk >> 4) & 0x0FU];
    line[pos++] = hex_digits[chk & 0x0FU];
    line[pos++] = '\r';
    line[pos++] = '\n';
    line[pos] = '\0';

    return write_line(dest, line, pos);
}

static int determine_type(xx_srec_format format, uint64_t max_addr)
{
    if (format == XX_SREC_FORMAT_S19) return 1;
    if (format == XX_SREC_FORMAT_S28) return 2;
    if (format == XX_SREC_FORMAT_S37) return 3;
    if (max_addr <= 0xFFFFULL) return 1;
    if (max_addr <= 0xFFFFFFULL) return 2;
    return 3;
}

XXFC_API bool xx_srec_write_buffer(const uint8_t *data, size_t size,
                                   uint64_t base_address, unsigned line_length,
                                   xx_srec_format format, const char *header_text,
                                   xx_io_device *dest, xx_pd_struct *pd)
{
    if (!dest) return false;
    if (line_length == 0U) line_length = XX_SREC_DEFAULT_LINE_LENGTH;
    if (line_length > XX_SREC_MAX_DATA_PER_LINE) line_length = XX_SREC_MAX_DATA_PER_LINE;

    uint64_t max_addr = size > 0U ? (base_address + size - 1ULL) : base_address;
    int type = determine_type(format, max_addr);

    if (!emit_s0(dest, header_text)) return false;

    if (size > 0U && data) {
        uint64_t cur_addr = base_address;
        size_t remaining = size;
        const uint8_t *ptr = data;

        while (remaining > 0U) {
            if (pd && xx_pd_is_stopped(pd)) return false;
            size_t chunk = remaining > line_length ? line_length : remaining;
            if (!emit_srec_data(dest, type, cur_addr, ptr, chunk)) return false;

            ptr += chunk;
            cur_addr += chunk;
            remaining -= chunk;
        }
    }

    return emit_termination(dest, type, 0ULL);
}

XXFC_API bool xx_srec_write_device(xx_io_device *src, int64_t src_offset, size_t count,
                                   uint64_t base_address, unsigned line_length,
                                   xx_srec_format format, const char *header_text,
                                   xx_io_device *dest, xx_pd_struct *pd)
{
    if (!src || !dest || src_offset < 0) return false;
    if (line_length == 0U) line_length = XX_SREC_DEFAULT_LINE_LENGTH;
    if (line_length > XX_SREC_MAX_DATA_PER_LINE) line_length = XX_SREC_MAX_DATA_PER_LINE;

    uint64_t max_addr = count > 0U ? (base_address + count - 1ULL) : base_address;
    int type = determine_type(format, max_addr);

    if (!emit_s0(dest, header_text)) return false;

    if (count > 0U) {
        uint8_t buffer[65536];
        size_t remaining = count;
        int64_t cur_src = src_offset;
        uint64_t cur_addr = base_address;

        while (remaining > 0U) {
            if (pd && xx_pd_is_stopped(pd)) return false;
            size_t to_read = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
            if (xx_io_seek64(src, cur_src, SEEK_SET) != 0) return false;
            ssize_t bytes_read = xx_io_read(src, buffer, to_read);
            if (bytes_read <= 0 || (size_t)bytes_read != to_read) return false;

            const uint8_t *ptr = buffer;
            size_t buf_remain = to_read;

            while (buf_remain > 0U) {
                size_t chunk = buf_remain > line_length ? line_length : buf_remain;
                if (!emit_srec_data(dest, type, cur_addr, ptr, chunk)) return false;

                ptr += chunk;
                cur_addr += chunk;
                buf_remain -= chunk;
            }

            cur_src += to_read;
            remaining -= to_read;
        }
    }

    return emit_termination(dest, type, 0ULL);
}
