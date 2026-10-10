/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OS/2 RSFX contains a complete RAR 1.5 stream after its LX executable.
 * Some original RAR 1.5 writers checksum only the fixed 13-byte main header
 * when a comment follows it, omit the per-file solid bit even though the
 * main header declares a solid archive, and leave a bad CRC on the terminal
 * authenticity record.  Validate the original carrier and every file header,
 * then normalize those metadata fields in a private memory view for the
 * ordinary RAR reader.  The compressed members and their CRC32s stay intact.
 */
#include "xxfclib/formats/sfx_rsfx/xx_sfx_rsfx.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/formats/rar/xx_rar.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#include <limits.h>
#include "xxfclib/data/xx_data.h"

#define RS_MAX_FILE (64U * 1024U * 1024U)
#define RS_SCAN_MAX (1024U * 1024U)
#define RS_MAX_HEADER 4096U
#define RS_MAX_PACKED (16U * 1024U * 1024U)
#define RS_MAX_PLAIN (64U * 1024U * 1024U)
#define RS_MAX_MEMBERS 65536U

static uint16_t rs_header_crc(const uint8_t *p, size_t size)
{
    return (uint16_t)xx_crc32_calc(0U, p, size);
}

static bool rs_read(xx_io_device *device, int64_t at, void *out, size_t size, xx_pd_struct *pd)
{
    size_t done = 0U;
    if (!device || at < 0 || (!out && size) || xx_io_seek64(device, at, SEEK_SET) != 0) return false;
    while (done < size) {
        size_t take = size - done > 65536U ? 65536U : size - done;
        ssize_t got;
        if (pd && xx_pd_is_stopped(pd)) return false;
        got = xx_io_read(device, (uint8_t *)out + done, take);
        if (got <= 0 || (size_t)got > take) return false;
        done += (size_t)got;
    }
    return true;
}

/* All arithmetic is within RS_MAX_FILE.  The only accepted checksum changes
 * are a fixed-part main CRC and a terminal AV CRC; file CRCs must be valid in
 * the original input before a solid-continuation bit is synthesized. */
static bool rs_normalize(uint8_t *bytes, size_t size, size_t *member_count, xx_pd_struct *pd)
{
    static const uint8_t signature[7] = {'R', 'a', 'r', '!', 0x1a, 0x07, 0x00};
    size_t at, count = 0U;
    uint16_t main_flags, main_size;
    bool saw_av = false;
    if (!bytes || !member_count || size < 20U || xx_rt_memcmp(bytes, signature, sizeof(signature))) return false;
    at = sizeof(signature);
    main_flags = xx_data_get_u16(bytes + at + 3U, 2, 0, false);
    main_size = xx_data_get_u16(bytes + at + 5U, 2, 0, false);
    if (bytes[at + 2U] != 0x73U || (main_flags & ~0x002eU) || main_size < 13U || main_size > RS_MAX_HEADER || main_size > size - at ||
        (main_size != 13U && !(main_flags & 0x0002U)) || xx_data_get_u16(bytes + at, 2, 0, false) != rs_header_crc(bytes + at + 2U, 11U))
        return false;
    if (main_size != 13U) xx_data_set_u16(bytes + at, 2, 0, rs_header_crc(bytes + at + 2U, main_size - 2U), false);
    at += main_size;

    while (at < size) {
        uint8_t type;
        uint16_t flags, header_size, stored_crc;
        uint32_t packed_size = 0U;
        size_t next;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (size - at < 7U) return false;
        stored_crc = xx_data_get_u16(bytes + at, 2, 0, false);
        type = bytes[at + 2U];
        flags = xx_data_get_u16(bytes + at + 3U, 2, 0, false);
        header_size = xx_data_get_u16(bytes + at + 5U, 2, 0, false);
        if (header_size < 7U || header_size > RS_MAX_HEADER || header_size > size - at) return false;
        if (flags & 0x8000U) {
            if (header_size < 11U) return false;
            packed_size = xx_data_get_u32(bytes + at + 7U, 4, 0, false);
        }
        if (packed_size > size - at - header_size || packed_size > RS_MAX_PACKED) return false;
        next = at + header_size + packed_size;

        if (type == 0x74U) {
            uint16_t name_size;
            if (flags != 0x8000U || header_size < 33U || header_size < 28U) return false;
            name_size = xx_data_get_u16(bytes + at + 26U, 2, 0, false);
            if (!name_size || header_size != (size_t)32U + name_size || bytes[at + 24U] != 15U || bytes[at + 25U] < 0x30U || bytes[at + 25U] > 0x35U ||
                xx_data_get_u32(bytes + at + 11U, 4, 0, false) > RS_MAX_PLAIN || stored_crc != rs_header_crc(bytes + at + 2U, header_size - 2U) ||
                count >= RS_MAX_MEMBERS)
                return false;
            if (count && (main_flags & 0x0008U)) {
                xx_data_set_u16(bytes + at + 3U, 2, 0, flags | 0x0010U, false);
                xx_data_set_u16(bytes + at, 2, 0, rs_header_crc(bytes + at + 2U, header_size - 2U), false);
            }
            ++count;
        } else if (type == 0x77U) {
            if (flags != 0xc000U || header_size != 24U || stored_crc != rs_header_crc(bytes + at + 2U, header_size - 2U)) return false;
        } else if (type == 0x76U) {
            if (flags || packed_size || !count || next != size || saw_av) return false;
            saw_av = true;
            if (stored_crc != rs_header_crc(bytes + at + 2U, header_size - 2U))
                xx_data_set_u16(bytes + at, 2, 0, rs_header_crc(bytes + at + 2U, header_size - 2U), false);
        } else {
            return false;
        }
        at = next;
    }
    *member_count = count;
    return count != 0U && at == size;
}

static bool rs_prepare(xx_sfx_rsfx *archive, xx_pd_struct *pd)
{
    static const uint8_t signature[7] = {'R', 'a', 'r', '!', 0x1a, 0x07, 0x00};
    Abstractformat *f;
    uint8_t mz[64], lx[2], *source = NULL, *data = NULL;
    xx_io_device *view = NULL;
    xx_rar *inner = NULL;
    int64_t total, length, old_position, marker = -1;
    uint32_t lx_offset;
    size_t scan_size, i, count = 0U;
    bool okay = false;
    if (!archive || !archive->format.device) return false;
    f = &archive->format;
    if (archive->inner && f->base_info_handled) return true;
    total = xx_io_size(f->device);
    if (f->base_address < 0 || total < f->base_address) return false;
    length = total - f->base_address;
    if (length < 128 || length > RS_MAX_FILE || (pd && xx_pd_is_stopped(pd))) return false;
    old_position = xx_io_tell(f->device);
    if (!rs_read(f->device, f->base_address, mz, sizeof(mz), pd) || xx_rt_memcmp(mz, "MZ", 2U) || xx_rt_memcmp(mz + 28U, "RSFX", 4U)) goto done;
    lx_offset = xx_data_get_u32(mz + 60U, 4, 0, false);
    if (lx_offset < 64U || lx_offset > RS_SCAN_MAX - 2U || (int64_t)lx_offset + 2 > length || !rs_read(f->device, f->base_address + lx_offset, lx, sizeof(lx), pd) ||
        xx_rt_memcmp(lx, "LX", 2U))
        goto done;

    scan_size = (size_t)(length > RS_SCAN_MAX ? RS_SCAN_MAX : length);
    source = (uint8_t *)xx_mem_alloc(scan_size);
    if (!source || !rs_read(f->device, f->base_address, source, scan_size, pd)) goto done;
    for (i = (size_t)lx_offset + 2U; i + sizeof(signature) <= scan_size; ++i) {
        if ((i & 4095U) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        if (!xx_rt_memcmp(source + i, signature, sizeof(signature))) {
            marker = (int64_t)i;
            break;
        }
    }
    if (marker < 0) goto done;
    data = (uint8_t *)xx_mem_alloc((size_t)(length - marker));
    if (!data || !rs_read(f->device, f->base_address + marker, data, (size_t)(length - marker), pd) || !rs_normalize(data, (size_t)(length - marker), &count, pd))
        goto done;
    view = xx_io_mem_open(data, (size_t)(length - marker));
    inner = view ? xx_rar_create(view, 0) : NULL;
    if (!inner || !xx_format_handle_base_info(&inner->format, pd) || inner->format.number_of_archive_records != count || inner->format.format_size != length - marker)
        goto done;

    archive->normalized_data = data;
    archive->normalized_device = view;
    archive->inner = inner;
    f->format_size = length;
    f->number_of_archive_records = count;
    f->overlay_size = 0;
    f->overlay_offset = -1;
    f->is_valid = true;
    f->base_info_handled = true;
    data = NULL;
    view = NULL;
    inner = NULL;
    okay = true;
done:
    if (old_position >= 0) (void)xx_io_seek64(f->device, old_position, SEEK_SET);
    xx_rar_free(inner);
    if (view) (void)xx_io_close(view);
    xx_mem_free(data);
    xx_mem_free(source);
    return okay;
}

static int64_t rs_size(Abstractformat *f, xx_pd_struct *pd)
{
    return rs_prepare((xx_sfx_rsfx *)f, pd) ? f->format_size : -1;
}

static uint64_t rs_count(Abstractformat *f, xx_pd_struct *pd)
{
    return rs_prepare((xx_sfx_rsfx *)f, pd) ? f->number_of_archive_records : 0U;
}

static xx_archive_record_state *rs_records(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_sfx_rsfx *archive = (xx_sfx_rsfx *)f;
    if (!rs_prepare(archive, pd)) return NULL;
    return xx_format_create_archive_records_reading(&archive->inner->format, options, pd);
}

static const xx_archive_record *rs_current(Abstractformat *f, xx_archive_record_state *state)
{
    xx_sfx_rsfx *archive = (xx_sfx_rsfx *)f;
    return archive->inner ? xx_format_get_current_archive_record(&archive->inner->format, state) : NULL;
}

static bool rs_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_sfx_rsfx *archive = (xx_sfx_rsfx *)f;
    return archive->inner && xx_format_archive_record_move_to_next(&archive->inner->format, state, pd);
}

static bool rs_unpack(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_sfx_rsfx *archive = (xx_sfx_rsfx *)f;
    return archive->inner && xx_format_unpack_current_archive_record(&archive->inner->format, state, pd);
}

static void rs_free_records(Abstractformat *f, xx_archive_record_state *state)
{
    xx_sfx_rsfx *archive = (xx_sfx_rsfx *)f;
    if (archive->inner) xx_format_free_archive_records_reading(&archive->inner->format, state);
}

void xx_sfx_rsfx_init(xx_sfx_rsfx *archive, xx_io_device *device, int64_t base_address)
{
    Abstractformat *f;
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    f = &archive->format;
    xx_format_init(f, device, base_address);
    f->file_type = XX_FILE_TYPE_SFX_RSFX;
    f->format_type = XX_TYPE_ARCHIVE;
    f->is_archive = true;
    f->is_executable = true;
    xx_format_set_extension(f, "exe");
    f->check_is_valid = xx_sfx_rsfx_check_is_valid;
    f->handle_base_info = xx_sfx_rsfx_handle_base_info;
    f->get_format_size = rs_size;
    f->get_number_of_archive_records = rs_count;
    f->create_archive_records_reading = rs_records;
    f->get_current_archive_record = rs_current;
    f->archive_record_move_to_next = rs_next;
    f->unpack_current_archive_record = rs_unpack;
    f->free_archive_records_reading = rs_free_records;
}

xx_sfx_rsfx *xx_sfx_rsfx_create(xx_io_device *device, int64_t base_address)
{
    xx_sfx_rsfx *archive = (xx_sfx_rsfx *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_rsfx_init(archive, device, base_address);
    return archive;
}

void xx_sfx_rsfx_destroy(xx_sfx_rsfx *archive)
{
    if (!archive) return;
    xx_rar_free(archive->inner);
    if (archive->normalized_device) (void)xx_io_close(archive->normalized_device);
    xx_mem_free(archive->normalized_data);
    xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_rsfx_free(xx_sfx_rsfx *archive)
{
    if (!archive) return;
    xx_sfx_rsfx_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_rsfx_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return rs_prepare((xx_sfx_rsfx *)f, pd);
}

bool xx_sfx_rsfx_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return rs_prepare((xx_sfx_rsfx *)f, pd);
}
