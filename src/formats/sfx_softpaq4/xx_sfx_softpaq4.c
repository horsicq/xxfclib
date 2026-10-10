/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SoftPaq 4 carries three stored named records after a 64-byte "!3PS"
 * descriptor. Two descriptor variants have an unused stub-name or reserved
 * slot between the first member and the ZIP. Every declared record position
 * must tile the payload, so no arbitrary embedded-byte carving is needed.
 */
#include "xxfclib/formats/sfx_softpaq4/xx_sfx_softpaq4.h"
#include "../common/xx_carrier_helpers.h"
#include "xxfclib/io/xx_io.h"

#include <limits.h>

#ifdef SFX_SOFTPAQ4
#define SP4_FILE_TYPE XX_FILE_TYPE_SFX_SOFTPAQ4
#else
#define SP4_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SP4_SCAN_MAX (1024U * 1024U)
#define SP4_DESCRIPTOR_SIZE 64U
#define SP4_MAX_NAME 64U

typedef struct sp4_extent_s {
    uint64_t size;
    uint64_t offset;
} sp4_extent;

/* Several SoftPaq volumes were made by a ZIP writer that put Deflate option
 * bit 1 in local headers but omitted it from the central directory.  That
 * bit does not affect decoding.  Correct only this mismatch in a private
 * view, then let the complete shared ZIP validator check every local extent,
 * name, size, CRC field and central-directory boundary. */
static bool sp4_zip_legacy(Abstractformat *f, int64_t start, int64_t end,
                           xx_pd_struct *pd) {
    uint8_t *bytes = NULL;
    xx_io_device *view = NULL;
    Abstractformat nested;
    size_t size, eocd, lower, dir, cursor;
    uint32_t dir_size, dir_offset, count, i;
    int64_t bias;
    bool okay = false, found = false, changed = false;
    if (start < 0 || end <= start || end - start > 64 * 1024 * 1024)
        return false;
    size = (size_t)(end - start);
    if (size < 22U) return false;
    bytes = (uint8_t *)xx_mem_alloc(size);
    if (!bytes || !pm_read(f, start, bytes, size)) goto done;
    lower = size > 65557U ? size - 65557U : 0U;
    for (eocd = size - 22U;; --eocd) {
        if (!xx_rt_memcmp(bytes + eocd, "PK\x05\x06", 4U) &&
            eocd + 22U + xx_data_get_u16(bytes + eocd + 20U, 2, 0, false) == size) {
            found = true;
            break;
        }
        if (eocd == lower || (pd && xx_pd_is_stopped(pd))) break;
    }
    if (!found || xx_data_get_u16(bytes + eocd + 4U, 2, 0, false) != 0U ||
        xx_data_get_u16(bytes + eocd + 6U, 2, 0, false) != 0U ||
        (count = xx_data_get_u16(bytes + eocd + 8U, 2, 0, false)) == 0U ||
        count != xx_data_get_u16(bytes + eocd + 10U, 2, 0, false)) goto done;
    dir_size = xx_data_get_u32(bytes + eocd + 12U, 4, 0, false);
    dir_offset = xx_data_get_u32(bytes + eocd + 16U, 4, 0, false);
    if (dir_size > eocd) goto done;
    dir = eocd - dir_size;
    bias = (int64_t)dir - (int64_t)dir_offset;
    cursor = dir;
    for (i = 0U; i < count; ++i) {
        const uint8_t *central;
        int64_t local;
        uint16_t flags, local_flags, method;
        size_t record;
        if (cursor > eocd || eocd - cursor < 46U ||
            xx_rt_memcmp(bytes + cursor, "PK\x01\x02", 4U)) goto done;
        central = bytes + cursor;
        record = 46U + xx_data_get_u16(central + 28U, 2, 0, false) +
                 xx_data_get_u16(central + 30U, 2, 0, false) + xx_data_get_u16(central + 32U, 2, 0, false);
        if (!xx_data_get_u16(central + 28U, 2, 0, false) || record > eocd - cursor ||
            bias < 0 ||
            (int64_t)xx_data_get_u32(central + 42U, 4, 0, false) > INT64_MAX - bias)
            goto done;
        local = bias + xx_data_get_u32(central + 42U, 4, 0, false);
        if (local < 0 || local > (int64_t)dir ||
            (int64_t)dir - local < 30 ||
            xx_rt_memcmp(bytes + local, "PK\x03\x04", 4U)) goto done;
        flags = xx_data_get_u16(central + 8U, 2, 0, false);
        local_flags = xx_data_get_u16(bytes + local + 6U, 2, 0, false);
        method = xx_data_get_u16(central + 10U, 2, 0, false);
        if (local_flags != flags) {
            if (method != 8U || (flags & 2U) ||
                local_flags != (uint16_t)(flags | 2U)) goto done;
            bytes[local + 6U] = (uint8_t)(flags & 0xffU);
            bytes[local + 7U] = (uint8_t)(flags >> 8U);
            changed = true;
        }
        cursor += record;
    }
    if (!changed || cursor != eocd) goto done;
    view = xx_io_mem_open(bytes, size);
    if (!view) goto done;
    xx_mem_zero(&nested, sizeof(nested));
    nested.device = view;
    okay = carrier_zip(&nested, 0, (int64_t)size, pd);
done:
    if (view) (void)xx_io_close(view);
    xx_mem_free(bytes);
    return okay;
}

static bool sp4_name(Abstractformat *f, sp4_extent extent, char *name,
                     int64_t *data_at, int64_t *data_size) {
    uint8_t length, bytes[SP4_MAX_NAME];
    size_t i;
    if (extent.size < 3U || extent.offset > (uint64_t)INT64_MAX ||
        !pm_read(f, (int64_t)extent.offset, &length, 1U) ||
        length == 0U || length > SP4_MAX_NAME ||
        extent.size <= 1U + length ||
        !pm_read(f, (int64_t)extent.offset + 1, bytes, length))
        return false;
    for (i = 0U; i < length; ++i) {
        uint8_t c = bytes[i];
        if (c <= 0x20U || c >= 0x7fU || c == '/' || c == '\\' ||
            c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            return false;
        name[i] = (char)c;
    }
    name[length] = '\0';
    if (!xx_rt_strcmp(name, ".") || !xx_rt_strcmp(name, "..") ||
        name[length - 1U] == '.') return false;
    *data_at = (int64_t)(extent.offset + 1U + length);
    *data_size = (int64_t)(extent.size - 1U - length);
    return true;
}

static bool sp4_candidate(Abstractformat *f, pm_stream *stream, int64_t marker,
                          int64_t limit, xx_pd_struct *pd) {
    uint8_t descriptor[SP4_DESCRIPTOR_SIZE], dummy[32], footer[8];
    sp4_extent records[5];
    char names[3][SP4_MAX_NAME + 1U];
    int64_t data_at[3], data_size[3];
    uint32_t flags;
    size_t count, first, selected[3], i, j;
    uint64_t end;
    if (marker < 0 || limit - marker < SP4_DESCRIPTOR_SIZE ||
        !pm_read(f, marker, descriptor, sizeof(descriptor)) ||
        xx_rt_memcmp(descriptor, "!3PS", 4U))
        return false;
    flags = xx_data_get_u32(descriptor + 4U, 4, 0, false);
    if (flags == 0U) {
        count = 4U;
        first = 8U;
        selected[0] = 0U; selected[1] = 2U; selected[2] = 3U;
    } else if (flags == 8U) {
        count = 5U;
        first = 12U;
        selected[0] = 0U; selected[1] = 3U; selected[2] = 4U;
    } else return false;
    for (i = 0U; i < count; ++i) {
        const uint8_t *entry = descriptor + first + i * 8U;
        records[i].size = xx_data_get_u32(entry, 4, 0, false);
        records[i].offset = xx_data_get_u32(entry + 4U, 4, 0, false);
        if (records[i].offset > (uint64_t)limit ||
            records[i].size > (uint64_t)limit - records[i].offset)
            return false;
        if (i && records[i - 1U].offset + records[i - 1U].size !=
                     records[i].offset)
            return false;
    }
    if (records[0].offset != (uint64_t)marker + SP4_DESCRIPTOR_SIZE)
        return false;
    end = records[count - 1U].offset + records[count - 1U].size;
    if (end != (uint64_t)limit) {
        if (end + 8U != (uint64_t)limit ||
            !pm_read(f, (int64_t)end, footer, sizeof(footer)) ||
            footer[0] != 0U || footer[3] != 0U ||
            xx_rt_memcmp(footer + 4U, "\x12\xef\xcd\xab", 4U))
            return false;
    }

    if (flags == 0U) {
        /* The second span is a plain stub-name marker, not a member. */
        if (records[1].size == 0U || records[1].size > sizeof(dummy) ||
            !pm_read(f, (int64_t)records[1].offset, dummy,
                     (size_t)records[1].size)) return false;
        for (i = 0U; i < records[1].size; ++i)
            if (dummy[i] <= 0x20U || dummy[i] >= 0x7fU ||
                dummy[i] == '/' || dummy[i] == '\\') return false;
    } else {
        /* Version 8 has two reserved spans; the optional byte is a dot. */
        if (records[1].size || records[2].size > 1U ||
            (records[2].size &&
             (!pm_read(f, (int64_t)records[2].offset, dummy, 1U) ||
              dummy[0] != '.'))) return false;
    }

    for (i = 0U; i < 3U; ++i) {
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!sp4_name(f, records[selected[i]], names[i], &data_at[i],
                      &data_size[i])) return false;
        for (j = 0U; j < i; ++j)
            if (!xx_str_icmp(names[i], names[j])) return false;
    }
    /* The middle record is an intact ZIP distribution volume.  Authenticate
     * its central directory and every local-header extent before publishing
     * the stored ZIP bytes. */
    if (xx_str_len(names[1]) < 4U ||
        xx_str_icmp(names[1] + xx_str_len(names[1]) - 4U, ".ZIP") ||
        data_size[1] < 22 ||
        !(carrier_zip(f, data_at[1], data_at[1] + data_size[1], pd) ||
          sp4_zip_legacy(f, data_at[1], data_at[1] + data_size[1], pd)))
        return false;

    for (i = 0U; i < 3U; ++i) {
        if (!pm_add(f, stream, names[i], data_at[i], data_size[i]))
            return false;
        xx_rt_snprintf(stream->items[stream->count - 1U].name,
                       sizeof(stream->items[stream->count - 1U].name),
                       "%s", names[i]);
    }
    stream->size = limit;
    return carrier_members(stream, pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *stream, xx_pd_struct *pd) {
    uint8_t mz[64], *scan = NULL;
    int64_t limit = pm_available(f);
    uint64_t image, headers, relocation_end;
    size_t scan_size, i;
    unsigned candidates = 0U;
    bool okay = false;
    if (limit < 128 || limit > UINT32_MAX ||
        !pm_read(f, 0, mz, sizeof(mz)) ||
        xx_rt_memcmp(mz, "MZ", 2U) || xx_data_get_u16(mz + 4U, 2, 0, false) == 0U ||
        xx_data_get_u16(mz + 2U, 2, 0, false) > 511U)
        return false;
    headers = (uint64_t)xx_data_get_u16(mz + 8U, 2, 0, false) * 16U;
    image = ((uint64_t)xx_data_get_u16(mz + 4U, 2, 0, false) - 1U) * 512U +
            (xx_data_get_u16(mz + 2U, 2, 0, false) ? xx_data_get_u16(mz + 2U, 2, 0, false) : 512U);
    relocation_end = (uint64_t)xx_data_get_u16(mz + 24U, 2, 0, false) +
                     4U * xx_data_get_u16(mz + 6U, 2, 0, false);
    if (headers < 28U || headers > image || image >= (uint64_t)limit ||
        relocation_end > headers || image > SP4_SCAN_MAX)
        return false;
    scan_size = (size_t)(limit > SP4_SCAN_MAX ? SP4_SCAN_MAX : limit);
    scan = (uint8_t *)xx_mem_alloc(scan_size);
    if (!scan || !pm_read(f, 0, scan, scan_size)) goto done;
    for (i = (size_t)image; i + SP4_DESCRIPTOR_SIZE <= scan_size; ++i) {
        if ((i & 4095U) == 0U && pd && xx_pd_is_stopped(pd)) break;
        if (xx_rt_memcmp(scan + i, "!3PS", 4U)) continue;
        if (++candidates > 128U) break;
        if (sp4_candidate(f, stream, (int64_t)i, limit, pd)) {
            okay = true;
            break;
        }
        if (stream->count) break;
    }
done:
    xx_mem_free(scan);
    return okay;
}

void xx_sfx_softpaq4_init(xx_sfx_softpaq4 *archive, xx_io_device *device,
                           int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    pm_init(&archive->format, device, base_address, SP4_FILE_TYPE, "exe");
    archive->format.is_executable = true;
}

xx_sfx_softpaq4 *xx_sfx_softpaq4_create(xx_io_device *device,
                                         int64_t base_address) {
    xx_sfx_softpaq4 *archive =
        (xx_sfx_softpaq4 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_softpaq4_init(archive, device, base_address);
    return archive;
}

void xx_sfx_softpaq4_destroy(xx_sfx_softpaq4 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_softpaq4_free(xx_sfx_softpaq4 *archive) {
    if (!archive) return;
    xx_sfx_softpaq4_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_softpaq4_check_is_valid(Abstractformat *f, xx_pd_struct *pd) {
    return pm_valid(f, pd);
}

bool xx_sfx_softpaq4_handle_base_info(Abstractformat *f,
                                       xx_pd_struct *pd) {
    return pm_handle(f, pd);
}
