/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native PFDC v0/v1/v2/v4 sector extraction. The original PCE writer and
 * loader are used solely as independent fixture tools.
 */
#include "xxfclib/formats/pce_pfdc/xx_pce_pfdc.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include <string.h>
#include "xxfclib/data/xx_data.h"

#define PFDC_MAX_FILE (32U * 1024U * 1024U)
#define PFDC_MAX_OUTPUT (64U * 1024U * 1024U)
#define PFDC_MAX_SECTORS 4096U
#define PFDC_MAX_CHUNKS 8192U
#define PFDC_TRANSFER 32768U

static bool pf_crc_range(Abstractformat *format, int64_t offset, uint32_t length, uint32_t seed, uint32_t polynomial, uint32_t *value, xx_pd_struct *pd)
{
    xx_crc_model model = {32U, polynomial, seed, false, false, 0U, "PCE PFDC"};
    xx_crc_context crc;
    uint8_t buffer[PFDC_TRANSFER];
    uint32_t remaining = length;
    if (!xx_crc_context_init(&crc, &model)) return false;
    while (remaining) {
        size_t take = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(format, offset, buffer, take)) return false;
        xx_crc_context_update(&crc, buffer, take);
        offset += (int64_t)take;
        remaining -= (uint32_t)take;
    }
    *value = (uint32_t)xx_crc_context_final(&crc);
    return true;
}
static bool pf_verify_chunk(Abstractformat *format, int64_t cursor, uint32_t header_size, uint32_t payload_size, uint32_t polynomial, xx_pd_struct *pd)
{
    uint32_t crc;
    uint8_t stored[4];
    return pf_crc_range(format, cursor, header_size + payload_size, 0U, polynomial, &crc, pd) &&
           pm_read(format, cursor + header_size + payload_size, stored, sizeof(stored)) && crc == xx_data_get_u32(stored, 4, 0, true);
}

/* The numeric member prefix preserves duplicate/alternate sectors. */
static bool pf_sector(Abstractformat *format, pm_stream *stream, uint32_t pc, uint32_t ph, uint32_t ls, uint32_t size, int64_t at, bool fill, uint8_t filler,
                      uint32_t *sectors, uint64_t *output, size_t *member_index)
{
    char name[64];
    pm_member *member;
    if (pc >= 1024U || ph >= 16U || ls >= 65536U || *sectors >= PFDC_MAX_SECTORS || size > PFDC_MAX_OUTPUT - *output) return false;
    (void)xx_rt_snprintf(name, sizeof(name), "c%03u_h%u_s%03u.bin", pc, ph, ls);
    if (!pm_add(format, stream, name, fill ? 0 : at, fill ? 0 : (int64_t)size)) return false;
    member = &stream->items[stream->count - 1U];
    if (fill) {
        uint8_t *bytes = (uint8_t *)xx_mem_alloc(size ? size : 1U);
        if (!bytes) return false;
        memset(bytes, filler, size);
        member->memory = bytes;
        member->offset = -1;
        member->size = (int64_t)size;
        member->packed_size = size ? 1 : 0;
    }
    *member_index = stream->count - 1U;
    *output += size;
    ++*sectors;
    return true;
}

static bool pf_legacy(Abstractformat *format, pm_stream *stream, unsigned version, int64_t available, xx_pd_struct *pd)
{
    uint8_t header[16];
    uint32_t count, offset, sectors = 0U;
    uint64_t output = 0U;
    int64_t cursor;
    bool have_previous = false;
    if (available < 16 || !pm_read(format, 0, header, sizeof(header))) return false;
    count = xx_data_get_u32(header + 8U, 4, 0, true);
    offset = xx_data_get_u32(header + 12U, 4, 0, true);
    if (count == 0U || count > PFDC_MAX_SECTORS || offset < 16U || offset > (uint64_t)available) return false;
    cursor = offset;
    while (sectors < count) {
        uint8_t fields[16];
        uint32_t pc, ph, ls, size, flags, header_size;
        size_t member_index;
        bool compressed;
        int64_t data_at;
        uint8_t filler = 0U;
        header_size = version == 0U ? 12U : 16U;
        if (available - cursor < header_size || !pm_read(format, cursor, fields, header_size)) return false;
        if (version == 0U) {
            pc = fields[0];
            ph = fields[1];
            ls = fields[4];
            flags = fields[5];
            size = xx_data_get_u16(fields + 6U, 2, 0, true);
            if (flags & ~0x9fU) return false;
        } else {
            if (fields[0] != 'S') return false;
            pc = fields[1];
            ph = fields[2];
            ls = fields[5];
            flags = xx_data_get_u32(fields + 12U, 4, 0, true);
            size = xx_data_get_u16(fields + 6U, 2, 0, true);
            if (flags & ~0x9dU || ((flags & 1U) && !have_previous)) return false;
        }
        compressed = (flags & 0x80U) != 0U;
        data_at = cursor + header_size;
        if ((uint64_t)(compressed ? 1U : size) > (uint64_t)(available - data_at)) return false;
        if (compressed && !pm_read(format, data_at, &filler, 1U)) return false;
        if (!pf_sector(format, stream, pc, ph, ls, size, data_at, compressed, filler, &sectors, &output, &member_index)) return false;
        (void)member_index;
        have_previous = true;
        cursor = data_at + (compressed ? 1 : (int64_t)size);
        if (pd && xx_pd_is_stopped(pd)) return false;
    }
    if (cursor != available) return false;
    stream->size = available;
    return true;
}

static bool pf_v2(Abstractformat *format, pm_stream *stream, int64_t available, xx_pd_struct *pd)
{
    uint8_t header[16], stored[4];
    uint32_t actual, offset, chunks = 0U, sectors = 0U;
    uint64_t output = 0U;
    int64_t cursor;
    bool have_previous = false;
    if (available < 28 || !pm_read(format, 0, header, sizeof(header))) return false;
    offset = xx_data_get_u32(header + 12U, 4, 0, true);
    if (offset < 16U || offset > (uint64_t)available - 12U) return false;
    if (!pf_crc_range(format, 0, (uint32_t)available - 4U, UINT32_C(0xffffffff), UINT32_C(0x04c11db7), &actual, pd) || !pm_read(format, available - 4, stored, 4U) ||
        actual != xx_data_get_u32(stored, 4, 0, true))
        return false;
    cursor = offset;
    while (cursor < available - 4) {
        uint8_t chunk[4];
        uint32_t id, length;
        int64_t next;
        if (++chunks > PFDC_MAX_CHUNKS || available - 4 - cursor < 8 || !pm_read(format, cursor, chunk, 4U)) return false;
        id = xx_data_get_u16(chunk, 2, 0, true);
        length = xx_data_get_u16(chunk + 2U, 2, 0, true);
        if (length > (uint64_t)(available - 4 - cursor - 8)) return false;
        next = cursor + 8 + length;
        if (!pf_verify_chunk(format, cursor, 4U, length, UINT32_C(0x04c11db7), pd)) return false;
        if (id == UINT32_C(0x5343)) { /* SC: sector and inline data */
            uint8_t fields[13];
            uint32_t flags, size;
            bool compressed;
            size_t member_index;
            int64_t data_at = cursor + 4 + 12;
            if (length < 12U || !pm_read(format, cursor + 4, fields, 12U)) return false;
            flags = fields[0];
            size = xx_data_get_u16(fields + 6U, 2, 0, true);
            compressed = (flags & 0x80U) != 0U;
            if (flags & ~0xcfU || ((flags & 0x40U) && !have_previous) || (uint32_t)(12U + (compressed ? 1U : size)) > length) return false;
            if (compressed && !pm_read(format, data_at, fields + 12U, 1U)) return false;
            if (!pf_sector(format, stream, fields[1], fields[2], fields[5], size, data_at, compressed, compressed ? fields[12] : 0U, &sectors, &output, &member_index))
                return false;
            (void)member_index;
            have_previous = true;
        } else if (id == UINT32_C(0x5447)) { /* TG: sector tags */
            if (!have_previous || length > 256U || !pm_add(format, stream, "sector-tags.bin", cursor + 4, length)) return false;
        } else if (id == UINT32_C(0x434d)) { /* CM: comment */
            if (!pm_add(format, stream, "comment.txt", cursor + 4, length)) return false;
        } else if (id == UINT32_C(0x454e)) { /* EN: end */
            if (length != 0U || sectors == 0U || next != available - 4) return false;
            stream->size = available;
            return true;
        }
        cursor = next;
    }
    return false;
}

static bool pf_v4(Abstractformat *format, pm_stream *stream, int64_t available, xx_pd_struct *pd)
{
    uint8_t header[8], version[4];
    uint32_t chunks = 0U, sectors = 0U, expected = 0U;
    uint64_t output = 0U;
    int64_t cursor = 16;
    size_t current = 0U;
    bool have_previous = false, pending = false;
    if (available < 28 || !pm_read(format, 0, header, 8U) || !pm_read(format, 8, version, 4U) || xx_data_get_u32(version, 4, 0, true) != UINT32_C(0x00040000))
        return false;
    if (!pf_verify_chunk(format, 0, 8U, 4U, UINT32_C(0x1edc6f41), pd)) return false;
    while (cursor < available) {
        uint32_t length;
        int64_t next;
        if (++chunks > PFDC_MAX_CHUNKS || available - cursor < 12 || !pm_read(format, cursor, header, 8U)) return false;
        length = xx_data_get_u32(header + 4U, 4, 0, true);
        if (length > (uint64_t)(available - cursor - 12)) return false;
        next = cursor + 12 + length;
        if (!pf_verify_chunk(format, cursor, 8U, length, UINT32_C(0x1edc6f41), pd)) return false;
        if (memcmp(header, "SECT", 4U) == 0) {
            uint8_t fields[18];
            uint32_t flags, size;
            bool compressed;
            if (pending || length != 18U || !pm_read(format, cursor + 8, fields, 18U)) return false;
            flags = xx_data_get_u16(fields + 14U, 2, 0, true);
            size = xx_data_get_u16(fields + 10U, 2, 0, true);
            compressed = (flags & 0x8000U) != 0U;
            if (flags & ~0xc00fU || ((flags & 0x4000U) && !have_previous) || (!compressed && (flags & 8U) && size != 0U)) return false;
            if (!pf_sector(format, stream, xx_data_get_u16(fields, 2, 0, true), xx_data_get_u16(fields + 2U, 2, 0, true), xx_data_get_u16(fields + 8U, 2, 0, true), size,
                           0, compressed, fields[13], &sectors, &output, &current))
                return false;
            have_previous = true;
            pending = !compressed && size != 0U;
            expected = size;
        } else if (memcmp(header, "DATA", 4U) == 0) {
            pm_member *member;
            if (!pending || length != expected) return false;
            member = &stream->items[current];
            member->offset = format->base_address + cursor + 8;
            member->size = member->packed_size = length;
            pending = false;
        } else if (memcmp(header, "TAGS", 4U) == 0) {
            if (!have_previous || length > 256U || !pm_add(format, stream, "sector-tags.bin", cursor + 8, length)) return false;
        } else if (memcmp(header, "TEXT", 4U) == 0) {
            if (length > 1024U * 1024U || !pm_add(format, stream, "comment.txt", cursor + 8, length)) return false;
        } else if (memcmp(header, "END ", 4U) == 0) {
            if (length != 0U || pending || sectors == 0U || next != available) return false;
            stream->size = available;
            return true;
        } else if (memcmp(header, "PFDC", 4U) == 0) {
            return false;
        }
        cursor = next;
    }
    return false;
}

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    xx_pce_pfdc *reader = (xx_pce_pfdc *)format;
    int64_t available = pm_available(format);
    uint8_t header[8];
    uint32_t marker;
    if (!reader || available < 16 || available > PFDC_MAX_FILE || (pd && xx_pd_is_stopped(pd)) || !pm_read(format, 0, header, 8U) || memcmp(header, "PFDC", 4U) != 0)
        return false;
    marker = xx_data_get_u32(header + 4U, 4, 0, true);
    if (reader->version == 0U && marker == 0U) return pf_legacy(format, stream, 0U, available, pd);
    if (reader->version == 1U && marker == UINT32_C(0x00010000)) return pf_legacy(format, stream, 1U, available, pd);
    if (reader->version == 2U && marker == UINT32_C(0x00020000)) return pf_v2(format, stream, available, pd);
    if (reader->version == 4U && marker == 4U) return pf_v4(format, stream, available, pd);
    return false;
}

void xx_pce_pfdc_init(xx_pce_pfdc *reader, xx_io_device *device, int64_t base_address, unsigned version)
{
    xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
    if (!reader) return;
#ifdef PCE_PFDC
    type = version == 0U   ? XX_FILE_TYPE_PCE_PFDC_V0
           : version == 1U ? XX_FILE_TYPE_PCE_PFDC_V1
           : version == 2U ? XX_FILE_TYPE_PCE_PFDC_V2
           : version == 4U ? XX_FILE_TYPE_PCE_PFDC_V4
                           : XX_FILE_TYPE_UNKNOWN;
#endif
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, type, "pfdc");
    reader->version = version;
}

xx_pce_pfdc *xx_pce_pfdc_create(xx_io_device *device, int64_t base_address, unsigned version)
{
    xx_pce_pfdc *reader = (xx_pce_pfdc *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_pce_pfdc_init(reader, device, base_address, version);
    return reader;
}

void xx_pce_pfdc_destroy(xx_pce_pfdc *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_pce_pfdc_free(xx_pce_pfdc *reader)
{
    if (reader) {
        xx_pce_pfdc_destroy(reader);
        xx_mem_free(reader);
    }
}
