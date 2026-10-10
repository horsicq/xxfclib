/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_EXECUTABLE_INSPECT_INTERNAL_H
#define XX_EXECUTABLE_INSPECT_INTERNAL_H
#include "xxfclib/formats/xx_executable_inspect.h"
#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/crc/xx_crc.h"

#define XX_EXEC_MAX_STRING_BYTES (16U * 1024U * 1024U)
#define XX_EXEC_MAX_READ_WORK (64U * 1024U * 1024U)
static inline bool xx_exec_account_string(xx_executable_input *input, size_t size)
{
    if (!input || !input->parsing) return true;
    if (input->string_bytes > XX_EXEC_MAX_STRING_BYTES || size > XX_EXEC_MAX_STRING_BYTES - input->string_bytes) {
        input->failed = true;
        return false;
    }
    input->string_bytes += size;
    return true;
}

static inline char *xx_exec_copy_string(xx_executable_input *input, const char *string)
{
    char *result;
    if (!string || !xx_exec_account_string(input, xx_rt_strlen(string))) return NULL;
    result = xx_str_create(string);
    if (!result && input) input->failed = true;
    return result;
}

static inline bool xx_exec_read(xx_executable_input *input, int64_t offset, void *output, size_t size)
{
    int64_t saved;
    xx_io_device *parent = NULL;
    int64_t parent_saved = -1;
    bool okay = false;
    if (!input || !input->device || offset < 0 || offset > input->size || (uint64_t)size > (uint64_t)(input->size - offset) || xx_pd_is_stopped(input->pd)) return false;
    if (size > XX_EXEC_MAX_READ_WORK - input->read_work) {
        input->failed = true;
        return false;
    }
    input->read_work += size;
    saved = xx_io_tell(input->device);
    if (xx_io_sub_get_range(input->device, &parent, NULL, NULL)) parent_saved = xx_io_tell(parent);
    if (xx_io_seek64(input->device, offset, XX_RT_SEEK_SET) == 0) {
        size_t done = 0;
        while (done < size) {
            ssize_t count = xx_io_read(input->device, (uint8_t *)output + done, size - done);
            if (count <= 0) break;
            done += (size_t)count;
        }
        okay = done == size;
    }
    if (saved >= 0 && xx_io_seek64(input->device, saved, XX_RT_SEEK_SET) != 0) okay = false;
    if (parent_saved >= 0 && xx_io_seek64(parent, parent_saved, XX_RT_SEEK_SET) != 0) okay = false;
    if (!okay) input->failed = true;
    return okay;
}

static inline bool xx_exec_match(xx_executable_input *input, int64_t offset, const void *expected, size_t size)
{
    uint8_t bytes[256];
    const uint8_t *wanted = (const uint8_t *)expected;
    if (!expected) return false;
    while (size) {
        size_t count = size < sizeof(bytes) ? size : sizeof(bytes);
        if (!xx_exec_read(input, offset, bytes, count) || xx_rt_memcmp(bytes, wanted, count)) return false;
        offset += (int64_t)count;
        wanted += count;
        size -= count;
    }
    return true;
}
static inline uint8_t xx_exec_u8(xx_executable_input *input, int64_t offset)
{
    uint8_t value = 0;
    xx_exec_read(input, offset, &value, 1);
    return value;
}
static inline uint64_t xx_exec_scalar(xx_executable_input *input, int64_t offset, size_t width, bool big_endian)
{
    uint8_t bytes[8];
    uint64_t value = 0;
    size_t i;
    if (!xx_exec_read(input, offset, bytes, width)) return 0;
    for (i = 0; i < width; ++i) value = (value << 8) | bytes[big_endian ? i : width - i - 1];
    return value;
}
static inline uint16_t xx_exec_u16(xx_executable_input *input, int64_t offset, bool big_endian)
{
    return (uint16_t)xx_exec_scalar(input, offset, 2, big_endian);
}
static inline uint32_t xx_exec_u32(xx_executable_input *input, int64_t offset, bool big_endian)
{
    return (uint32_t)xx_exec_scalar(input, offset, 4, big_endian);
}
static inline uint64_t xx_exec_u64(xx_executable_input *input, int64_t offset, bool big_endian)
{
    return xx_exec_scalar(input, offset, 8, big_endian);
}

static inline char *xx_exec_string(xx_executable_input *input, int64_t offset, int64_t maximum)
{
    xx_buf_t buffer;
    uint8_t bytes[512];
    xx_buf_init(&buffer);
    if (maximum <= 0 || maximum > 0x10000) maximum = 0x10000;
    if (!input || offset < 0 || offset > input->size) return xx_str_create("");
    if (maximum > input->size - offset) maximum = input->size - offset;
    while (maximum > 0) {
        size_t count = maximum < (int64_t)sizeof(bytes) ? (size_t)maximum : sizeof(bytes);
        size_t i = 0;
        if (!xx_exec_read(input, offset, bytes, count)) {
            xx_buf_free(&buffer);
            return xx_str_create("");
        }
        while (i < count && bytes[i]) ++i;
        if (!xx_exec_account_string(input, i) || !xx_buf_append(&buffer, bytes, i)) {
            input->failed = true;
            break;
        }
        if (i < count) break;
        maximum -= (int64_t)count;
        offset += (int64_t)count;
    }
    {
        char *result = xx_buf_detach(&buffer, NULL);
        if (!result && input) input->failed = true;
        return result;
    }
}

static inline bool xx_exec_append_codepoint(xx_buf_t *buffer, uint32_t code)
{
    if (code < 0x80) return xx_buf_append_char(buffer, (char)code);
    if (code < 0x800) return xx_buf_append_char(buffer, (char)(0xc0 | (code >> 6))) && xx_buf_append_char(buffer, (char)(0x80 | (code & 63)));
    if (code < 0x10000)
        return xx_buf_append_char(buffer, (char)(0xe0 | (code >> 12))) && xx_buf_append_char(buffer, (char)(0x80 | ((code >> 6) & 63))) &&
               xx_buf_append_char(buffer, (char)(0x80 | (code & 63)));
    return xx_buf_append_char(buffer, (char)(0xf0 | (code >> 18))) && xx_buf_append_char(buffer, (char)(0x80 | ((code >> 12) & 63))) &&
           xx_buf_append_char(buffer, (char)(0x80 | ((code >> 6) & 63))) && xx_buf_append_char(buffer, (char)(0x80 | (code & 63)));
}

/* Exact lengths are used for counted resource names and the CLI #US heap.
 * Unpaired UTF-16 surrogates are represented by the Unicode replacement
 * character; supplementary characters are emitted as one UTF-8 sequence. */
static inline char *xx_exec_decode_unicode(xx_executable_input *input, int64_t offset, int64_t maximum, int big_endian, bool terminated, int64_t *units)
{
    xx_buf_t buffer;
    int64_t i = 0;
    xx_buf_init(&buffer);
    if (!input || offset < 0 || offset > input->size || maximum < 0 || maximum > (input->size - offset) / 2) {
        if (input) input->failed = true;
        if (units) *units = 0;
        return NULL;
    }
    while (i < maximum && !input->failed) {
        uint32_t code = xx_exec_u16(input, offset + i * 2, big_endian != 0);
        if (!code && terminated) break;
        ++i;
        if (code >= 0xd800 && code <= 0xdbff) {
            uint32_t low = i < maximum ? xx_exec_u16(input, offset + i * 2, big_endian != 0) : 0;
            if (low >= 0xdc00 && low <= 0xdfff) {
                code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                ++i;
            } else {
                code = 0xfffd;
            }
        } else if (code >= 0xdc00 && code <= 0xdfff) {
            code = 0xfffd;
        }
        if (!xx_exec_append_codepoint(&buffer, code)) input->failed = true;
    }
    if (units) *units = i;
    if (!xx_buf_ok(&buffer) || !xx_exec_account_string(input, buffer.size)) input->failed = true;
    if (input->failed) {
        xx_buf_free(&buffer);
        return NULL;
    }
    {
        char *result = xx_buf_detach(&buffer, NULL);
        if (!result) input->failed = true;
        return result;
    }
}
static inline char *xx_exec_unicode_units(xx_executable_input *input, int64_t offset, int64_t count, int big_endian)
{
    return xx_exec_decode_unicode(input, offset, count, big_endian, false, NULL);
}
static inline char *xx_exec_unicode_n(xx_executable_input *input, int64_t offset, int64_t maximum, int big_endian, int64_t *units)
{
    if (maximum <= 0 || maximum > 0x10000) maximum = 0x10000;
    if (input && offset >= 0 && offset <= input->size && maximum > (input->size - offset) / 2) maximum = (input->size - offset) / 2;
    return xx_exec_decode_unicode(input, offset, maximum, big_endian, true, units);
}
static inline char *xx_exec_unicode(xx_executable_input *input, int64_t offset, int64_t maximum, int big_endian)
{
    return xx_exec_unicode_n(input, offset, maximum, big_endian, NULL);
}
static inline int64_t xx_exec_find_string(xx_executable_input *input, int64_t offset, int64_t size, const char *needle)
{
    if (!input || offset < 0 || offset > input->size) return -1;
    if (size < 0 || size > input->size - offset) size = input->size - offset;
    {
        xx_io_device *parent = NULL;
        int64_t saved = -1, result;
        if (xx_io_sub_get_range(input->device, &parent, NULL, NULL)) saved = xx_io_tell(parent);
        result = xx_io_find_ansi_string(input->device, offset, size, needle, input->pd);
        if (saved >= 0 && xx_io_seek64(parent, saved, XX_RT_SEEK_SET) != 0) input->failed = true;
        return result;
    }
}
static inline uint32_t xx_exec_string_crc32c(const char *string)
{
    return xx_crc32c_calc(UINT32_MAX, string, string ? xx_rt_strlen(string) : 0);
}
static inline bool xx_exec_map_part(xx_memory_map *map, int64_t offset, int64_t size, uint64_t address, uint64_t virtual_size, xx_file_part_t part, const char *name)
{
    if (!virtual_size) address = XX_INVALID_ADDRESS;
    return xx_memory_map_add_part(map, offset, size, address, (int64_t)virtual_size, part, 0, name, false);
}
static inline xx_executable_input *xx_exec_input_create(Abstractformat *format, xx_pd_struct *pd)
{
    int64_t size;
    xx_executable_input *input;
    if (!format || !format->device || format->base_address < 0 || xx_pd_is_stopped(pd)) return NULL;
    size = xx_io_total_size(format->device);
    if (size < format->base_address) return NULL;
    input = (xx_executable_input *)xx_mem_calloc(1, sizeof(*input));
    if (!input) return NULL;
    input->size = size - format->base_address;
    input->pd = pd;
    input->parsing = true;
    input->device = xx_io_sub_open_ro(format->device, format->base_address, input->size);
    if (!input->device) {
        xx_mem_free(input);
        return NULL;
    }
    return input;
}
static inline void xx_exec_input_free(xx_executable_input *input)
{
    if (input) {
        xx_io_close(input->device);
        xx_mem_free(input);
    }
}
#endif
