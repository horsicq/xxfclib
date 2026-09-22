/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zip/xx_zip.h"
#include "xx_zip_defs.h"
#include "xxfclib/io/xx_io.h"

/* Reserved device names and OS entropy live behind the io platform layer. */
#include "../../io/platforms/xx_io_platform.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/ppmd7/xx_ppmd7.h"
#include "xxfclib/algo/ppmd8/xx_ppmd8.h"
#include "xxfclib/algo/shrink/xx_shrink.h"
#include "xxfclib/algo/reduce/xx_reduce.h"
#include "xxfclib/algo/implode/xx_implode.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/algo/zipcrypto/xx_zipcrypto.h"
#include "xxfclib/algo/aes/xx_aes.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define XX_ZIP_LZMA2_VERSION 0x0409U
#define XX_ZIP_LZMA2_HEADER_SIZE 5U
#define XX_ZIP_LZMA2_MAX_PROP 40U

/* Forward declaration of vtable callbacks */
static void xx_zip_vtable_destroy(Abstractformat *self);
static bool xx_zip_read_exact_at(xx_io_device *device, int64_t offset,
                                 void *buffer, size_t size);

void xx_zip_init(xx_zip *zip, xx_io_device *dev, int64_t base_address) {
    if (!zip) {
        return;
    }
    xx_mem_zero(zip, sizeof(xx_zip));

    /* Initialize base Abstractformat */
    xx_format_init(&zip->format, dev, base_address);

    /* Setup default ZIP format attributes */
    zip->format.endian = XX_ENDIAN_LITTLE;
    zip->format.file_type = XX_FILE_TYPE_ZIP;
    zip->format.format_type = XX_TYPE_ARCHIVE;
    zip->format.is_archive = true;
    zip->format.is_executable = false;
    xx_format_set_mime_type(&zip->format, "application/zip");
    xx_format_set_extension(&zip->format, "zip");

    /* Setup vtable callbacks */
    zip->format.check_is_valid = xx_zip_check_is_valid;
    zip->format.handle_split_format = xx_zip_handle_split_format;
    zip->format.handle_base_info = xx_zip_handle_base_info;
    zip->format.get_format_size = xx_zip_get_format_size;
    zip->format.get_number_of_archive_records = xx_zip_get_number_of_archive_records;
    zip->format.create_archive_records_reading = xx_zip_create_archive_records_reading;
    zip->format.get_current_archive_record = xx_zip_get_current_archive_record;
    zip->format.unpack_current_archive_record = xx_zip_unpack_current_archive_record;
    zip->format.archive_record_move_to_next = xx_zip_archive_record_move_to_next;
    zip->format.free_archive_records_reading = xx_zip_free_archive_records_reading;
    zip->format.create_archive_records_writing = xx_zip_create_archive_records_writing;
    zip->format.pack_archive_record = xx_zip_pack_archive_record;
    zip->format.finalize_archive_records_writing = xx_zip_finalize_archive_records_writing;
    zip->format.free_archive_records_writing = xx_zip_free_archive_records_writing;
    zip->format.data_struct_id_to_string = xx_zip_data_struct_id_to_string;
    zip->format.data_struct_string_to_id = xx_zip_data_struct_string_to_id;
    zip->format.create_data_structs_reading = xx_zip_create_data_structs_reading;
    zip->format.get_current_data_struct = xx_zip_get_current_data_struct;
    zip->format.data_struct_move_to_next = xx_zip_data_struct_move_to_next;
    zip->format.free_data_structs_reading = xx_zip_free_data_structs_reading;
    zip->format.create_data_struct_records_reading = xx_zip_create_data_struct_records_reading;
    zip->format.get_current_data_struct_record = xx_zip_get_current_data_struct_record;
    zip->format.data_struct_record_move_to_next = xx_zip_data_struct_record_move_to_next;
    zip->format.free_data_struct_records_reading = xx_zip_free_data_struct_records_reading;
    zip->format.destroy = xx_zip_vtable_destroy;

    zip->is_zip64 = false;
    zip->number_of_records = 0;
    zip->cd_offset = -1;
    zip->cd_size = 0;
    zip->eocd_offset = -1;
    zip->zip64_eocd_offset = -1;
    zip->comment[0] = '\0';
}

xx_zip *xx_zip_create(xx_io_device *dev, int64_t base_address) {
    xx_zip *zip = (xx_zip *)xx_mem_alloc(sizeof(xx_zip));
    if (!zip) {
        return NULL;
    }
    xx_zip_init(zip, dev, base_address);
    return zip;
}

void xx_zip_destroy(xx_zip *zip) {
    if (!zip) {
        return;
    }
    if (zip->format.close) {
        zip->format.close(&zip->format);
    }
    xx_format_cleanup_extra_parameters(&zip->format);
}

static void xx_zip_vtable_destroy(Abstractformat *self) {
    if (self) {
        xx_zip *zip = (xx_zip *)self;
        xx_zip_destroy(zip);
    }
}

void xx_zip_free(xx_zip *zip) {
    if (!zip) {
        return;
    }
    xx_zip_destroy(zip);
    xx_mem_free(zip);
}

static uint16_t xx_zip_split_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t xx_zip_split_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t xx_zip_split_u64(const uint8_t *p) {
    return xx_zip_split_u32(p) | ((uint64_t)xx_zip_split_u32(p + 4) << 32);
}

static bool xx_zip_split_read(xx_io_device *device, int64_t offset,
                              void *buffer, size_t size) {
    size_t done = 0;
    int64_t total = xx_io_total_size(device);
    if (offset < 0 || total < offset || (uint64_t)size > (uint64_t)(total - offset) ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t count = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (count <= 0 || (size_t)count > size - done) return false;
        done += (size_t)count;
    }
    return true;
}

/* EOCD candidates must include their complete comment. For multi-volume input
 * also require exact EOF, so a stray appended disk cannot become an overlay. */
static int64_t xx_zip_split_find_eocd(xx_io_device *device, bool exact_end) {
    uint8_t *tail;
    int64_t total = xx_io_total_size(device), result = -1;
    size_t size;
    if (total < 22) return -1;
    size = total > 65557 ? 65557 : (size_t)total;
    tail = (uint8_t *)xx_mem_alloc(size);
    if (!tail) return -1;
    if (xx_zip_split_read(device, total - (int64_t)size, tail, size)) {
        int64_t i;
        for (i = (int64_t)size - 22; i >= 0; --i) {
            if (xx_zip_split_u32(tail + i) == XX_ZIP_EOCD_SIGNATURE) {
                size_t end = (size_t)i + 22 + xx_zip_split_u16(tail + i + 20);
                if (end <= size && (!exact_end || end == size)) {
                    result = total - (int64_t)size + i;
                    break;
                }
            }
        }
    }
    xx_mem_free(tail);
    return result;
}

static bool xx_zip_disk_offset(xx_zip *zip, uint32_t disk, uint64_t relative,
                               uint64_t header_size, int64_t *absolute) {
    xx_io_volume volume;
    int64_t start;
    if (!zip->is_split) {
        int64_t total = xx_io_total_size(zip->format.device);
        if (disk || total < 0 || relative > (uint64_t)total ||
            header_size > (uint64_t)total - relative) return false;
        *absolute = (int64_t)relative;
        return true;
    }
    if (disk >= zip->split_disk_count) return false;
    if (!xx_io_multivolume_get_volume(zip->format.device, disk, &volume, &start)) {
        /* The optional PK00 marker may occur in an otherwise ordinary file. */
        if (disk || zip->split_disk_count != 1 ||
            xx_io_multivolume_count(zip->format.device) != 0) return false;
        start = 0;
        volume.size = xx_io_total_size(zip->format.device);
    }
    if (start < 0 || volume.size < 0 || relative > (uint64_t)volume.size ||
        header_size > (uint64_t)volume.size - relative ||
        relative > (uint64_t)(INT64_MAX - start)) return false;
    *absolute = start + (int64_t)relative;
    return true;
}

static bool xx_zip_split_header_fits(xx_zip *zip, int64_t offset, int64_t size) {
    size_t i;
    if (!zip->is_split || !xx_io_multivolume_count(zip->format.device)) return true;
    for (i = 0; i < zip->split_disk_count; ++i) {
        xx_io_volume volume;
        int64_t start;
        if (!xx_io_multivolume_get_volume(zip->format.device, i, &volume, &start)) return false;
        if (offset >= start && offset - start < volume.size)
            return size >= 0 && size <= volume.size - (offset - start);
    }
    return false;
}

typedef struct xx_zip_entry_values {
    uint64_t unpacked, packed, relative;
    uint32_t disk;
} xx_zip_entry_values;

/* ZIP64 fields are conditional and ordered, not a fixed-width structure.
 * Keep values unsigned until range checks have proved they fit int64_t. */
static bool xx_zip_read_zip64_extra(xx_io_device *device, int64_t offset,
                                    uint16_t size, xx_zip_entry_values *values,
                                    bool local) {
    bool need_unpacked = values->unpacked == UINT32_MAX;
    bool need_packed = values->packed == UINT32_MAX;
    bool need_relative = !local && values->relative == UINT32_MAX;
    bool need_disk = !local && values->disk == UINT16_MAX;
    bool found = false;
    uint32_t left = size;
    if (!need_unpacked && !need_packed && !need_relative && !need_disk) return true;
    while (left) {
        uint8_t header[4], extended[28];
        uint16_t block;
        size_t needed = 0, position = 0;
        if (left < 4 || !xx_zip_split_read(device, offset, header, sizeof(header))) return false;
        block = xx_zip_split_u16(header + 2);
        if (block > left - 4) return false;
        if (xx_zip_split_u16(header) == 1) {
            if (found) return false;
            found = true;
            /* A local ZIP64 size pair always includes both size fields. */
            needed = local ? 16 : (need_unpacked ? 8 : 0) + (need_packed ? 8 : 0) +
                                 (need_relative ? 8 : 0) + (need_disk ? 4 : 0);
            if (block < needed || !xx_zip_split_read(device, offset + 4, extended, needed)) return false;
            if (local || need_unpacked) { values->unpacked = xx_zip_split_u64(extended + position); position += 8; }
            if (local || need_packed) { values->packed = xx_zip_split_u64(extended + position); position += 8; }
            if (need_relative) { values->relative = xx_zip_split_u64(extended + position); position += 8; }
            if (need_disk) values->disk = xx_zip_split_u32(extended + position);
        }
        offset += 4 + block;
        left -= 4 + block;
    }
    return found && values->packed <= INT64_MAX && values->unpacked <= INT64_MAX &&
           values->relative <= INT64_MAX;
}

static bool xx_zip_split_validate_cd(xx_zip *zip, uint64_t count,
                                     int64_t cd_offset, int64_t cd_size,
                                     uint32_t counted_disk, uint64_t entries_disk,
                                     uint64_t entries_last, xx_pd_struct *pd) {
    uint8_t central[46], local[30], left[256], right[256];
    int64_t cursor = cd_offset, cd_end = cd_offset + cd_size;
    int64_t last_start = 0, counted_start = 0, counted_end = INT64_MAX;
    uint64_t index, observed_last = 0, observed_disk = 0;
    if (xx_io_multivolume_count(zip->format.device) &&
        !xx_io_multivolume_get_volume(zip->format.device, zip->split_disk_count - 1,
                                      NULL, &last_start)) return false;
    if (xx_io_multivolume_count(zip->format.device)) {
        xx_io_volume counted_volume;
        if (!xx_io_multivolume_get_volume(zip->format.device, counted_disk,
                                          &counted_volume, &counted_start)) return false;
        counted_end = counted_start + counted_volume.size;
    }
    for (index = 0; index < count; ++index) {
        uint16_t name_size, extra_size, comment_size;
        xx_zip_entry_values entry, local_sizes;
        int64_t local_offset, header_size, entry_size, compared;
        if (xx_pd_is_stopped(pd) || cursor > cd_end || cd_end - cursor < 46 ||
            !xx_zip_split_read(zip->format.device, cursor, central, sizeof(central)) ||
            xx_zip_split_u32(central) != XX_ZIP_CENTRAL_DIRECTORY_HEADER_SIGNATURE) return false;
        if (cursor >= last_start) ++observed_last;
        if (cursor >= counted_start && cursor < counted_end) ++observed_disk;
        name_size = xx_zip_split_u16(central + 28);
        extra_size = xx_zip_split_u16(central + 30);
        comment_size = xx_zip_split_u16(central + 32);
        entry.disk = xx_zip_split_u16(central + 34);
        entry.packed = xx_zip_split_u32(central + 20);
        entry.unpacked = xx_zip_split_u32(central + 24);
        entry.relative = xx_zip_split_u32(central + 42);
        entry_size = 46 + (int64_t)name_size + extra_size + comment_size;
        if (entry_size > cd_end - cursor || !xx_zip_split_header_fits(zip, cursor, entry_size) ||
            !xx_zip_read_zip64_extra(zip->format.device, cursor + 46 + name_size, extra_size, &entry, false) ||
            !xx_zip_disk_offset(zip, entry.disk, entry.relative, sizeof(local), &local_offset) ||
            !xx_zip_split_read(zip->format.device, local_offset, local, sizeof(local)) ||
            xx_zip_split_u32(local) != XX_ZIP_LOCAL_FILE_HEADER_SIGNATURE ||
            xx_zip_split_u16(local + 26) != name_size ||
            xx_zip_split_u16(local + 6) != xx_zip_split_u16(central + 8) ||
            xx_zip_split_u16(local + 8) != xx_zip_split_u16(central + 10)) return false;
        header_size = 30 + (int64_t)name_size + xx_zip_split_u16(local + 28);
        if (!xx_zip_disk_offset(zip, entry.disk, entry.relative, (uint64_t)header_size, &local_offset) ||
            local_offset > cd_offset || header_size > cd_offset - local_offset ||
            entry.packed > (uint64_t)(cd_offset - local_offset - header_size)) return false;
        local_sizes.unpacked = xx_zip_split_u32(local + 22);
        local_sizes.packed = xx_zip_split_u32(local + 18);
        local_sizes.relative = 0;
        local_sizes.disk = 0;
        if (!xx_zip_read_zip64_extra(zip->format.device, local_offset + 30 + name_size,
                                     xx_zip_split_u16(local + 28), &local_sizes, true)) return false;
        if (!(xx_zip_split_u16(local + 6) & 8) &&
            (local_sizes.packed != entry.packed || local_sizes.unpacked != entry.unpacked ||
             xx_zip_split_u32(local + 14) != xx_zip_split_u32(central + 16))) return false;
        for (compared = 0; compared < name_size;) {
            size_t amount = (size_t)(name_size - compared);
            if (amount > sizeof(left)) amount = sizeof(left);
            if (!xx_zip_split_read(zip->format.device, cursor + 46 + compared, left, amount) ||
                !xx_zip_split_read(zip->format.device, local_offset + 30 + compared, right, amount) ||
                xx_rt_memcmp(left, right, amount) != 0) return false;
            compared += (int64_t)amount;
        }
        cursor += entry_size;
    }
    if (cursor < cd_end) {
        uint8_t signature[6];
        if (cd_end - cursor < 6 ||
            !xx_zip_split_read(zip->format.device, cursor, signature, sizeof(signature)) ||
            xx_zip_split_u32(signature) != 0x05054B50U ||
            cd_end - cursor != 6 + (int64_t)xx_zip_split_u16(signature + 4)) return false;
    }
    return observed_disk == entries_disk &&
           (entries_last == UINT64_MAX || observed_last == entries_last);
}

bool xx_zip_handle_split_format(Abstractformat *self, xx_pd_struct *pd) {
    xx_zip candidate;
    xx_zip *zip;
    uint8_t first[4], eocd[22], locator[20], end64[56];
    uint32_t last_disk, cd_disk, marker = 0, end64_disk = 0;
    uint64_t count, size, relative, entries_disk, entries_last;
    size_t supplied;
    int64_t total, eocd_offset, cd_offset, directory_limit, end64_offset = -1, end64_size = 0;
    bool native_split, has_zip64_locator;
    if (self && self->handle_split_format == xx_zip_handle_split_format &&
        !self->split_format_handling) return xx_format_handle_split_format(self, pd);
    if (!self || !self->device || xx_pd_is_stopped(pd)) return false;
    zip = (xx_zip *)self;
    supplied = xx_io_multivolume_count(self->device);
    total = xx_io_total_size(self->device);
    if (xx_zip_split_read(self->device, self->base_address, first, sizeof(first))) {
        uint32_t signature = xx_zip_split_u32(first);
        if (signature == 0x08074B50U || signature == 0x30304B50U) marker = 4;
    }
    eocd_offset = xx_zip_split_find_eocd(self->device, supplied > 1 || marker != 0);
    if (eocd_offset < 0) {
        if (supplied <= 1 && !marker) return true; /* includes an empty writer */
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Incomplete multi-volume ZIP: EOCD not found");
        return false;
    }
    if (!xx_zip_split_read(self->device, eocd_offset, eocd, sizeof(eocd))) return false;
    last_disk = xx_zip_split_u16(eocd + 4);
    cd_disk = xx_zip_split_u16(eocd + 6);
    count = xx_zip_split_u16(eocd + 10);
    size = xx_zip_split_u32(eocd + 12);
    relative = xx_zip_split_u32(eocd + 16);
    entries_disk = entries_last = xx_zip_split_u16(eocd + 8);
    directory_limit = eocd_offset;
    has_zip64_locator = eocd_offset >= 20 &&
        xx_zip_split_read(self->device, eocd_offset - 20, locator, sizeof(locator)) &&
        xx_zip_split_u32(locator) == XX_ZIP_ZIP64_EOCD_LOCATOR_SIGNATURE;
    native_split = marker || last_disk || cd_disk;
    if (has_zip64_locator) {
        uint32_t locator_disk = xx_zip_split_u32(locator + 4);
        uint32_t locator_disks = xx_zip_split_u32(locator + 16);
        if (locator_disks > 1 || locator_disk) native_split = true;
        else if (!marker && locator_disks == 1 &&
                 (last_disk == 0 || last_disk == UINT16_MAX) &&
                 (cd_disk == 0 || cd_disk == UINT16_MAX)) {
            uint64_t plain_end64_offset = xx_zip_split_u64(locator + 8);
            if (plain_end64_offset <= INT64_MAX &&
                xx_zip_split_read(self->device, (int64_t)plain_end64_offset, end64, sizeof(end64)) &&
                xx_zip_split_u32(end64) == XX_ZIP_ZIP64_EOCD_SIGNATURE &&
                !xx_zip_split_u32(end64 + 16) && !xx_zip_split_u32(end64 + 20)) native_split = false;
        }
    }
    if (!native_split) return true; /* ordinary ZIP or raw byte-split .zip.001 */
    if (self->base_address != 0) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Native split ZIP requires base offset zero");
        return false;
    }
    candidate = *zip;
    candidate.is_split = true;
    candidate.is_zip64 = has_zip64_locator;
    candidate.split_marker_size = marker;
    if (has_zip64_locator) {
        uint32_t disks = xx_zip_split_u32(locator + 16);
        uint64_t record_size;
        end64_disk = xx_zip_split_u32(locator + 4);
        if (!disks || disks == UINT32_MAX || (uint64_t)(supplied ? supplied : 1) != disks ||
            end64_disk >= disks || (last_disk != UINT16_MAX && last_disk != disks - 1)) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Inconsistent ZIP64 locator disk numbers");
            return false;
        }
        last_disk = disks - 1;
        candidate.split_disk_count = disks;
        if (!xx_zip_disk_offset(&candidate, end64_disk, xx_zip_split_u64(locator + 8),
                                sizeof(end64), &end64_offset) ||
            !xx_zip_split_read(self->device, end64_offset, end64, sizeof(end64)) ||
            xx_zip_split_u32(end64) != XX_ZIP_ZIP64_EOCD_SIGNATURE ||
            xx_zip_split_u32(end64 + 16) != end64_disk) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid disk-relative ZIP64 end record");
            return false;
        }
        record_size = xx_zip_split_u64(end64 + 4);
        if (record_size < 44 || record_size > INT64_MAX - 12 ||
            end64_offset > eocd_offset - 20 ||
            record_size + 12 != (uint64_t)(eocd_offset - 20 - end64_offset) ||
            !xx_zip_split_header_fits(&candidate, end64_offset, (int64_t)record_size + 12) ||
            !xx_zip_split_header_fits(&candidate, eocd_offset - 20, 20)) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid ZIP64 end record extent");
            return false;
        }
        end64_size = (int64_t)record_size + 12;
        directory_limit = end64_offset;
        /* Nonsentinel EOCD fields describe the same directory. The count on
         * the last disk is checked separately if the ZIP64 record is earlier. */
        if ((cd_disk != UINT16_MAX && cd_disk != xx_zip_split_u32(end64 + 20)) ||
            (count != UINT16_MAX && count != xx_zip_split_u64(end64 + 32)) ||
            (size != UINT32_MAX && size != xx_zip_split_u64(end64 + 40)) ||
            (relative != UINT32_MAX && relative != xx_zip_split_u64(end64 + 48))) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Conflicting ZIP and ZIP64 directory metadata");
            return false;
        }
        cd_disk = xx_zip_split_u32(end64 + 20);
        count = xx_zip_split_u64(end64 + 32);
        size = xx_zip_split_u64(end64 + 40);
        relative = xx_zip_split_u64(end64 + 48);
        entries_disk = xx_zip_split_u64(end64 + 24);
        if (entries_last == UINT16_MAX) entries_last = UINT64_MAX;
    } else {
        if (last_disk == UINT16_MAX || cd_disk == UINT16_MAX || count == UINT16_MAX ||
            entries_disk == UINT16_MAX || size == UINT32_MAX || relative == UINT32_MAX) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "ZIP64 sentinel has no ZIP64 end locator");
            return false;
        }
        candidate.split_disk_count = last_disk + 1;
        end64_disk = last_disk;
    }
    if ((uint64_t)(supplied ? supplied : 1) != (uint64_t)last_disk + 1 || cd_disk > end64_disk ||
        count > INT64_MAX || size > INT64_MAX || relative > INT64_MAX || entries_disk > count ||
        (entries_last != UINT64_MAX && entries_last > count) ||
        eocd_offset + 22 + xx_zip_split_u16(eocd + 20) != total) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Missing, extra, or inconsistent ZIP volumes");
        return false;
    }
    if (supplied) {
        size_t i;
        for (i = 0; i < supplied; ++i) {
            xx_io_volume volume;
            int64_t start;
            if (!xx_io_multivolume_get_volume(self->device, i, &volume, &start) || volume.size <= 0 ||
                (i == last_disk && (eocd_offset < start || total - eocd_offset > volume.size ||
                                   (has_zip64_locator && eocd_offset - 20 < start)))) {
                xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Empty or incorrectly ordered ZIP volume");
                return false;
            }
        }
    }
    if (!xx_zip_split_read(self->device, marker, first, sizeof(first)) ||
        (xx_zip_split_u32(first) != XX_ZIP_LOCAL_FILE_HEADER_SIGNATURE &&
         xx_zip_split_u32(first) != XX_ZIP_EOCD_SIGNATURE &&
         !(count == 0 && xx_zip_split_u32(first) == XX_ZIP_ZIP64_EOCD_SIGNATURE)) ||
        !xx_zip_disk_offset(&candidate, cd_disk, relative, count ? 46 : 0, &cd_offset) ||
        cd_offset > directory_limit || size > (uint64_t)(directory_limit - cd_offset) ||
        count > size / 46 ||
        !xx_zip_split_validate_cd(&candidate, count, cd_offset, (int64_t)size,
                                  end64_disk, entries_disk, entries_last, pd)) {
        if (!pd || !pd->last_error)
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid split ZIP directory or local-header mapping");
        return false;
    }
    if (xx_pd_is_stopped(pd)) return false;
    zip->is_split = true;
    zip->split_disk_count = candidate.split_disk_count;
    zip->split_marker_size = marker;
    zip->eocd_offset = eocd_offset;
    zip->is_zip64 = has_zip64_locator;
    zip->zip64_eocd_offset = end64_offset;
    zip->zip64_eocd_size = end64_size;
    zip->cd_offset = cd_offset;
    zip->cd_size = (int64_t)size;
    zip->number_of_records = count;
    return true;
}

bool xx_zip_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || !self->device || !xx_format_handle_split_format(self, pd)) {
        return false;
    }

    int64_t total_size = xx_io_total_size(self->device);
    if (total_size < 22) { /* Minimum size for an empty ZIP (EOCD is 22 bytes) */
        return false;
    }

    /* Check magic at base_address: must be local file header or EOCD */
    uint32_t magic = xx_io_get_u32(self->device, self->base_address + ((xx_zip *)self)->split_marker_size, false);
    if (magic != XX_ZIP_LOCAL_FILE_HEADER_SIGNATURE &&
        magic != XX_ZIP_EOCD_SIGNATURE &&
        !(magic == XX_ZIP_ZIP64_EOCD_SIGNATURE && ((xx_zip *)self)->is_split &&
          ((xx_zip *)self)->number_of_records == 0)) {
        return false;
    }

    if (magic == XX_ZIP_EOCD_SIGNATURE) {
        return true;
    }

    /* Scan tail for EOCD record signature PK\x05\x06 */
    size_t scan_size = (total_size > 65557) ? 65557 : (size_t)total_size;
    int64_t scan_offset = total_size - (int64_t)scan_size;
    if (scan_offset < 0) {
        scan_offset = 0;
    }

    uint8_t *buf = (uint8_t *)xx_mem_alloc(scan_size);
    if (!buf) {
        return false;
    }

    bool found = false;
    if (xx_io_seek64(self->device, scan_offset, SEEK_SET) == 0) {
        ssize_t nread = xx_io_read(self->device, buf, scan_size);
        if (nread >= 4) {
            for (int64_t i = (int64_t)nread - 4; i >= 0; --i) {
                if (buf[i] == 'P' && buf[i + 1] == 'K' &&
                    buf[i + 2] == 0x05 && buf[i + 3] == 0x06) {
                    found = true;
                    break;
                }
            }
        }
    }
    xx_mem_free(buf);

    return found;
}

bool xx_zip_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || !self->device || !xx_format_handle_split_format(self, pd)) {
        return false;
    }

    xx_zip *zip = (xx_zip *)self;
    int64_t total_size = xx_io_total_size(self->device);
    if (total_size < 22) {
        self->is_valid = false;
        return false;
    }

    /* Check magic at base_address: must be local file header or EOCD */
    uint32_t magic = xx_io_get_u32(self->device, self->base_address + zip->split_marker_size, false);
    if (magic != XX_ZIP_LOCAL_FILE_HEADER_SIGNATURE &&
        magic != XX_ZIP_EOCD_SIGNATURE &&
        !(magic == XX_ZIP_ZIP64_EOCD_SIGNATURE && zip->is_split && zip->number_of_records == 0)) {
        self->is_valid = false;
        return false;
    }

    /* Scan tail for EOCD record */
    size_t scan_size = (total_size > 65557) ? 65557 : (size_t)total_size;
    int64_t scan_offset = total_size - (int64_t)scan_size;
    if (scan_offset < 0) {
        scan_offset = 0;
    }

    uint8_t *buf = (uint8_t *)xx_mem_alloc(scan_size);
    if (!buf) {
        return false;
    }

    int64_t eocd_found_pos = -1;
    if (xx_io_seek64(self->device, scan_offset, SEEK_SET) == 0) {
        ssize_t nread = xx_io_read(self->device, buf, scan_size);
        if (nread >= 22) {
            for (int64_t i = (int64_t)nread - 22; i >= 0; --i) {
                if (buf[i] == 'P' && buf[i + 1] == 'K' &&
                    buf[i + 2] == 0x05 && buf[i + 3] == 0x06) {
                    uint16_t comment_len = (uint16_t)(buf[i + 20] | (buf[i + 21] << 8));
                    if ((int64_t)(i + 22 + comment_len) <= (int64_t)nread) {
                        eocd_found_pos = scan_offset + i;
                        break;
                    }
                    if (eocd_found_pos < 0) {
                        eocd_found_pos = scan_offset + i;
                    }
                }
            }
        }
    }
    xx_mem_free(buf);

    if (eocd_found_pos < 0) {
        self->is_valid = false;
        return false;
    }

    if (zip->is_split) eocd_found_pos = zip->eocd_offset;
    zip->eocd_offset = eocd_found_pos;

    /* Read standard EOCD record */
    uint16_t total_records = xx_io_get_u16(self->device, eocd_found_pos + 10, false);
    uint32_t cd_size = xx_io_get_u32(self->device, eocd_found_pos + 12, false);
    uint32_t cd_offset = xx_io_get_u32(self->device, eocd_found_pos + 16, false);
    uint16_t comment_len = xx_io_get_u16(self->device, eocd_found_pos + 20, false);

    if (!zip->is_split) {
        zip->number_of_records = total_records;
        zip->cd_size = cd_size;
        zip->cd_offset = cd_offset;
    }

    /* Read archive comment */
    if (comment_len > 0) {
        size_t to_copy = (comment_len < (uint16_t)(sizeof(zip->comment) - 1)) ?
                          comment_len : (sizeof(zip->comment) - 1);
        if (xx_io_seek64(self->device, eocd_found_pos + 22, SEEK_SET) == 0) {
            ssize_t cr = xx_io_read(self->device, zip->comment, to_copy);
            if (cr > 0) {
                zip->comment[cr] = '\0';
            } else {
                zip->comment[0] = '\0';
            }
        }
    } else {
        zip->comment[0] = '\0';
    }

    /* Calculate format size & overlays */
    int64_t format_end = eocd_found_pos + 22 + comment_len;
    self->format_size = format_end;
    if (total_size > format_end) {
        self->overlay_offset = format_end;
        self->overlay_size = total_size - format_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }

    /* Check ZIP64 locator */
    if (!zip->is_split) zip->is_zip64 = false;
    if (!zip->is_split && eocd_found_pos >= 20) {
        int64_t locator_offset = eocd_found_pos - 20;
        uint32_t loc_sig = xx_io_get_u32(self->device, locator_offset, false);
        if (loc_sig == XX_ZIP_ZIP64_EOCD_LOCATOR_SIGNATURE) {
            zip->is_zip64 = true;
            uint64_t zip64_eocd_offset = xx_io_get_u64(self->device, locator_offset + 8, false);
            if (zip64_eocd_offset > INT64_MAX || zip64_eocd_offset > (uint64_t)locator_offset ||
                (uint64_t)locator_offset - zip64_eocd_offset < 56) {
                self->is_valid = false;
                return false;
            }
            uint32_t eocd64_sig = xx_io_get_u32(self->device, (int64_t)zip64_eocd_offset, false);
            if (eocd64_sig == XX_ZIP_ZIP64_EOCD_SIGNATURE) {
                uint64_t directory_size = xx_io_get_u64(self->device, (int64_t)zip64_eocd_offset + 40, false);
                uint64_t directory_offset = xx_io_get_u64(self->device, (int64_t)zip64_eocd_offset + 48, false);
                uint64_t record_size = xx_io_get_u64(self->device, (int64_t)zip64_eocd_offset + 4, false);
                if (directory_size > INT64_MAX || directory_offset > INT64_MAX ||
                    record_size < 44 || record_size > INT64_MAX - 12 ||
                    record_size + 12 > (uint64_t)locator_offset - zip64_eocd_offset) {
                    self->is_valid = false;
                    return false;
                }
                zip->number_of_records = xx_io_get_u64(self->device, (int64_t)zip64_eocd_offset + 32, false);
                zip->cd_size = (int64_t)directory_size;
                zip->cd_offset = (int64_t)directory_offset;
                zip->zip64_eocd_offset = (int64_t)zip64_eocd_offset;
                zip->zip64_eocd_size = (int64_t)record_size + 12;
            }
        }
    }

    if (!zip->is_zip64 && (total_records == 0xFFFF || cd_size == 0xFFFFFFFF || cd_offset == 0xFFFFFFFF)) {
        zip->is_zip64 = true;
    }

    self->file_type = zip->is_zip64 ? XX_FILE_TYPE_ZIP64 : XX_FILE_TYPE_ZIP;
    self->endian = XX_ENDIAN_LITTLE;
    self->is_archive = true;
    self->is_executable = false;
    self->format_type = XX_TYPE_ARCHIVE;
    self->number_of_archive_records = zip->number_of_records;
    self->is_valid = true;

    return true;
}

int64_t xx_zip_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || !xx_format_handle_split_format(self, pd)) {
        return -1;
    }
    if (!self->base_info_handled) {
        xx_format_handle_base_info(self, pd);
    }
    return self->format_size;
}

uint64_t xx_zip_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || !xx_format_handle_split_format(self, pd)) {
        return 0;
    }
    if (!self->base_info_handled) {
        xx_format_handle_base_info(self, pd);
    }
    xx_zip *zip = (xx_zip *)self;
    return zip->number_of_records;
}

static bool xx_zip_parse_cd_entry(xx_zip *zip, xx_io_device *device, int64_t dev_size, int64_t curr_offset,
                                 xx_archive_record *rec, int64_t *entry_size_out) {
    uint8_t fixed[46];
    if (!device || !rec) {
        return false;
    }
    if (curr_offset < 0 || curr_offset > dev_size || dev_size - curr_offset < 46) {
        return false;
    }

    if (!xx_zip_read_exact_at(device, curr_offset, fixed, sizeof(fixed))) {
        return false;
    }
    uint32_t sig = (uint32_t)fixed[0] | ((uint32_t)fixed[1] << 8) |
                   ((uint32_t)fixed[2] << 16) | ((uint32_t)fixed[3] << 24);
    if (sig != XX_ZIP_CENTRAL_DIRECTORY_HEADER_SIGNATURE) {
        return false;
    }

#define XX_ZIP_FIXED_U16(position) ((uint16_t)((uint16_t)fixed[(position)] | \
    ((uint16_t)fixed[(position) + 1U] << 8)))
#define XX_ZIP_FIXED_U32(position) ((uint32_t)((uint32_t)fixed[(position)] | \
    ((uint32_t)fixed[(position) + 1U] << 8) | \
    ((uint32_t)fixed[(position) + 2U] << 16) | \
    ((uint32_t)fixed[(position) + 3U] << 24)))
    uint16_t version_made_by = XX_ZIP_FIXED_U16(4);
    uint16_t version_needed = XX_ZIP_FIXED_U16(6);
    uint16_t flags = XX_ZIP_FIXED_U16(8);
    uint16_t method = XX_ZIP_FIXED_U16(10);
    uint16_t last_mod_time = XX_ZIP_FIXED_U16(12);
    uint16_t last_mod_date = XX_ZIP_FIXED_U16(14);
    uint32_t crc32 = XX_ZIP_FIXED_U32(16);
    uint32_t comp_size_32 = XX_ZIP_FIXED_U32(20);
    uint32_t uncomp_size_32 = XX_ZIP_FIXED_U32(24);
    uint16_t fn_len = XX_ZIP_FIXED_U16(28);
    uint16_t extra_len = XX_ZIP_FIXED_U16(30);
    uint16_t comment_len = XX_ZIP_FIXED_U16(32);
    uint16_t disk_start = XX_ZIP_FIXED_U16(34);
    uint16_t internal_attrs = XX_ZIP_FIXED_U16(36);
    uint32_t external_attrs = XX_ZIP_FIXED_U32(38);
    uint32_t rel_offset_32 = XX_ZIP_FIXED_U32(42);
#undef XX_ZIP_FIXED_U16
#undef XX_ZIP_FIXED_U32

    int64_t total_entry_size = 46 + (int64_t)fn_len + (int64_t)extra_len + (int64_t)comment_len;
    if (total_entry_size > dev_size - curr_offset) {
        return false;
    }

    xx_zip_entry_values values;
    values.unpacked = uncomp_size_32;
    values.packed = comp_size_32;
    values.relative = rel_offset_32;
    values.disk = disk_start;
    if (!xx_zip_read_zip64_extra(device, curr_offset + 46 + fn_len, extra_len, &values, false) ||
        values.unpacked > INT64_MAX || values.packed > INT64_MAX || values.relative > INT64_MAX)
        return false;
    uint64_t uncomp_size = values.unpacked;
    uint64_t comp_size = values.packed;
    int64_t rel_offset = (int64_t)values.relative;
    uint32_t disk_number_start = values.disk;

    int64_t local_offset = rel_offset;
    if (zip->is_split && !xx_zip_disk_offset(zip, disk_number_start,
                                            (uint64_t)rel_offset, 30, &local_offset)) return false;
    xx_archive_record_init(rec);

    rec->header_offset = local_offset;
    rec->compressed_size = (int64_t)comp_size;

    /* Calculate local header size & data offset if local header exists */
    if (local_offset >= 0 && local_offset <= dev_size - 30) {
        uint8_t local_fixed[30];
        uint32_t loc_sig;
        if (!xx_zip_read_exact_at(device, local_offset, local_fixed,
                                  sizeof(local_fixed))) goto parse_failed;
        loc_sig = (uint32_t)local_fixed[0] |
                  ((uint32_t)local_fixed[1] << 8) |
                  ((uint32_t)local_fixed[2] << 16) |
                  ((uint32_t)local_fixed[3] << 24);
        if (loc_sig == XX_ZIP_LOCAL_FILE_HEADER_SIGNATURE) {
            uint16_t loc_fn_len = (uint16_t)((uint16_t)local_fixed[26] |
                                             ((uint16_t)local_fixed[27] << 8));
            uint16_t loc_extra_len = (uint16_t)((uint16_t)local_fixed[28] |
                                                ((uint16_t)local_fixed[29] << 8));
            int64_t loc_hdr_size = 30 + (int64_t)loc_fn_len + (int64_t)loc_extra_len;
            if (loc_hdr_size > dev_size - local_offset ||
                comp_size > (uint64_t)(dev_size - local_offset - loc_hdr_size))
                goto parse_failed;
            rec->header_size = loc_hdr_size;
            rec->data_offset = local_offset + loc_hdr_size;
        }
    }

    /* Read filename */
    if (fn_len > 0) {
        char *fn_buf = (char *)xx_mem_alloc(fn_len + 1);
        wchar_t *wfn = NULL;
        bool name_set;
        bool is_dir;
        if (!fn_buf ||
            !xx_zip_read_exact_at(device, curr_offset + 46, fn_buf, fn_len)) {
            xx_mem_free(fn_buf);
            goto parse_failed;
        }
        fn_buf[fn_len] = '\0';
        if (xx_rt_memchr(fn_buf, '\0', fn_len) != NULL) {
            xx_mem_free(fn_buf);
            goto parse_failed;
        }

        if ((flags & 0x0800) != 0) {
            wfn = xx_str_utf8_to_unicode(fn_buf);
        } else {
            wfn = xx_str_ansi_to_unicode(fn_buf);
        }

        name_set = wfn ? xx_archive_record_set_original_name_w(rec, wfn)
                       : xx_archive_record_set_original_name(rec, fn_buf);
        xx_str_wfree(wfn);
        is_dir = fn_buf[fn_len - 1] == '/' ||
                 fn_buf[fn_len - 1] == '\\' ||
                 (external_attrs & 0x10) != 0;
        xx_mem_free(fn_buf);
        if (!name_set || !xx_archive_record_set_meta_bool(
                             rec, XX_META_ID_IS_FOLDER, is_dir))
            goto parse_failed;
    } else {
        bool is_dir = ((external_attrs & 0x10) != 0);
        if (!xx_archive_record_set_meta_bool(rec, XX_META_ID_IS_FOLDER,
                                             is_dir))
            goto parse_failed;
    }

    /* Populate all ZIP metadata */
    if (!xx_archive_record_set_meta_u64(rec, XX_META_ID_UNCOMPRESSED_SIZE,
                                        uncomp_size) ||
        !xx_archive_record_set_meta_u64(rec, XX_META_ID_COMPRESSED_SIZE,
                                        comp_size) ||
        !xx_archive_record_set_meta_u64(rec, XX_META_ID_CRC32, crc32) ||
        !xx_archive_record_set_meta_u64(rec, XX_META_ID_COMPRESSION_METHOD,
                                        method) ||
        !xx_archive_record_set_meta_u64(rec, XX_META_ID_ATTRIBUTES,
                                        external_attrs) ||
        !xx_archive_record_set_meta_u64(rec, XX_META_ID_EXTERNAL_ATTRS,
                                        external_attrs) ||
        !xx_archive_record_set_meta_u64(rec, XX_META_ID_INTERNAL_ATTRS,
                                        internal_attrs) ||
        !xx_archive_record_set_meta_u64(rec, XX_META_ID_FLAGS, flags) ||
        !xx_archive_record_set_meta_u64(rec, XX_META_ID_VERSION_NEEDED,
                                        version_needed) ||
        !xx_archive_record_set_meta_u64(rec, XX_META_ID_VERSION_MADE_BY,
                                        version_made_by) ||
        !xx_archive_record_set_meta_u64(rec, XX_META_ID_DISK_NUMBER_START,
                                        disk_number_start) ||
        !xx_archive_record_set_meta_i64(
            rec, XX_META_ID_RELATIVE_OFFSET_LOCAL_HEADER, rel_offset) ||
        !xx_archive_record_set_meta_u64(rec, XX_META_ID_LAST_MOD_TIME,
                                        last_mod_time) ||
        !xx_archive_record_set_meta_u64(rec, XX_META_ID_LAST_MOD_DATE,
                                        last_mod_date) ||
        !xx_archive_record_set_meta_bool(rec, XX_META_ID_IS_ENCRYPTED,
                                         (flags & 1) != 0))
        goto parse_failed;

    /* Read extra field */
    if (extra_len > 0) {
        uint8_t *ebuf = (uint8_t *)xx_mem_alloc(extra_len);
        xx_var evar;
        bool extra_ok;
        if (!ebuf || !xx_zip_read_exact_at(
                         device, curr_offset + 46 + fn_len, ebuf, extra_len)) {
            xx_mem_free(ebuf);
            goto parse_failed;
        }
        xx_var_init(&evar);
        extra_ok = xx_var_set_bytes(&evar, ebuf, extra_len) &&
                   xx_archive_record_set_meta(rec, XX_META_ID_EXTRA_FIELD,
                                              &evar);
        xx_var_cleanup(&evar);
        xx_mem_free(ebuf);
        if (!extra_ok) goto parse_failed;
    }

    /* Read comment */
    if (comment_len > 0) {
        char *cbuf = (char *)xx_mem_alloc(comment_len + 1);
        bool comment_ok;
        if (!cbuf || !xx_zip_read_exact_at(
                         device, curr_offset + 46 + fn_len + extra_len,
                         cbuf, comment_len)) {
            xx_mem_free(cbuf);
            goto parse_failed;
        }
        cbuf[comment_len] = '\0';
        if (xx_rt_memchr(cbuf, '\0', comment_len) != NULL) {
            xx_mem_free(cbuf);
            goto parse_failed;
        }
        comment_ok = xx_archive_record_set_meta_str(
            rec, XX_META_ID_COMMENT, cbuf);
        xx_mem_free(cbuf);
        if (!comment_ok) goto parse_failed;
    }

    if (entry_size_out) *entry_size_out = total_entry_size;
    return true;

parse_failed:
    xx_archive_record_cleanup(rec);
    xx_archive_record_init(rec);
    return false;
}




static const xx_var *xx_zip_find_option(const xx_list_s *options, uint32_t meta_id) {
    if (!options || options->count == 0) {
        return NULL;
    }
    for (size_t i = 0; i < options->count; ++i) {
        const xx_meta *opt = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (opt && opt->meta_id == meta_id) {
            return &opt->var;
        }
    }
    return NULL;
}

static bool xx_zip_get_u64_limit(const Abstractformat *format,
                                 const xx_list_s *options, uint32_t meta_id,
                                 bool *present, uint64_t *limit) {
    const xx_var *value;
    int64_t signed_value;
    if (!present || !limit) {
        return false;
    }
    *present = false;
    *limit = 0U;
    value = xx_format_resolve_extra_parameter(format, options, meta_id);
    if (!value) {
        return true;
    }
    *present = true;
    switch ((xx_var_type_t)value->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64:
            *limit = xx_var_get_u64(value);
            return true;
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64:
            signed_value = xx_var_get_i64(value);
            if (signed_value < 0) {
                return false;
            }
            *limit = (uint64_t)signed_value;
            return true;
        default:
            return false;
    }
}

static bool xx_zip_add_memory(uint64_t *total, uint64_t amount) {
    if (!total || *total > UINT64_MAX - amount) {
        return false;
    }
    *total += amount;
    return true;
}

typedef struct xx_zip_stream_state {
    int64_t curr_offset;
    uint64_t current_index;
    uint64_t total_records;
} xx_zip_stream_state;

static void xx_zip_stream_state_free(void *ptr) {
    xx_zip_stream_state *state = (xx_zip_stream_state *)ptr;
    if (!state) return;
    xx_mem_free(state);
}

xx_archive_record_state *xx_zip_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    if (!self || !self->device || !xx_format_handle_split_format(self, pd)) {
        return NULL;
    }
    if (!self->base_info_handled) {
        if (!xx_format_handle_base_info(self, pd)) {
            return NULL;
        }
    }
    if (!self->is_valid) {
        return NULL;
    }

    xx_zip *zip = (xx_zip *)self;
    int pd_level = -1;
    xx_archive_record_state *state = (xx_archive_record_state *)xx_mem_alloc(sizeof(xx_archive_record_state));
    if (!state) {
        return NULL;
    }
    xx_archive_record_state_init(state, self);

    /* Copy options if supplied */
    if (options && options->count > 0) {
        for (size_t i = 0; i < options->count; ++i) {
            const xx_meta *opt_item = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
            if (opt_item) {
                xx_meta new_opt;
                xx_meta_init(&new_opt, opt_item->meta_id);
                if (!xx_var_copy(&new_opt.var, &opt_item->var) ||
                    !xx_list_append(&state->options, &new_opt)) {
                    xx_meta_cleanup(&new_opt);
                    xx_archive_record_state_free(state);
                    return NULL;
                }
            }
        }
    }

    xx_zip_stream_state *zstate = (xx_zip_stream_state *)xx_mem_alloc(sizeof(xx_zip_stream_state));
    if (!zstate) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    xx_mem_zero(zstate, sizeof(xx_zip_stream_state));
    zstate->curr_offset = zip->cd_offset;
    zstate->current_index = 0;
    zstate->total_records = zip->number_of_records;

    state->internal_state = zstate;
    state->free_internal = xx_zip_stream_state_free;
    state->total_records = (int64_t)zip->number_of_records;
    state->current_index = -1;
    state->has_record = false;

    if (pd) {
        pd_level = xx_pd_enter_level(pd, zip->number_of_records,
                                     "Reading ZIP records");
    }

    if (zip->number_of_records > 0 && zip->cd_offset >= 0) {
        int64_t dev_size = xx_io_total_size(self->device);
        int64_t entry_size = 0;
        if (!xx_zip_parse_cd_entry(zip, self->device, dev_size,
                                   zstate->curr_offset,
                                   &state->current_record, &entry_size)) {
            xx_pd_leave_level(pd, pd_level);
            xx_archive_record_state_free(state);
            return NULL;
        }
        if (xx_pd_is_stopped(pd)) {
            xx_pd_leave_level(pd, pd_level);
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
        state->current_index = 0;
        zstate->curr_offset += entry_size;
        if (pd_level >= 0) {
            xx_pd_set_current(pd, pd_level, 1);
        }
    }
    xx_pd_leave_level(pd, pd_level);

    return state;
}

const xx_archive_record *xx_zip_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    if (!state || !state->has_record) {
        return NULL;
    }
    return &state->current_record;
}

bool xx_zip_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!self || !self->device || !state || !state->internal_state ||
        xx_pd_is_stopped(pd)) {
        return false;
    }
    xx_zip_stream_state *zstate = (xx_zip_stream_state *)state->internal_state;
    int pd_level = xx_pd_enter_level(pd, zstate->total_records,
                                     "Reading ZIP records");
    if (pd_level >= 0) {
        xx_pd_set_current(pd, pd_level, zstate->current_index + 1U);
    }

    /* Clean up the current record */
    if (state->has_record) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
    }

    {
        uint64_t next_index = zstate->current_index + 1U;
        if (next_index >= zstate->total_records) {
            xx_pd_leave_level(pd, pd_level);
            return false;
        }

        if (pd && xx_pd_is_stopped(pd)) {
            xx_pd_leave_level(pd, pd_level);
            return false;
        }

        {
            int64_t dev_size = xx_io_total_size(self->device);
            int64_t entry_size = 0;
            if (!xx_zip_parse_cd_entry((xx_zip *)self, self->device,
                                       dev_size, zstate->curr_offset,
                                       &state->current_record, &entry_size)) {
                xx_pd_leave_level(pd, pd_level);
                return false;
            }
            if (xx_pd_is_stopped(pd)) {
                xx_archive_record_cleanup(&state->current_record);
                xx_archive_record_init(&state->current_record);
                xx_pd_leave_level(pd, pd_level);
                return false;
            }

            zstate->current_index = next_index;
            state->has_record = true;
            state->current_index = (int64_t)next_index;
            zstate->curr_offset += entry_size;
        }

        if (pd_level >= 0) {
            xx_pd_set_current(pd, pd_level, next_index + 1U);
        }
    }

    xx_pd_leave_level(pd, pd_level);
    return true;
}

typedef struct xx_zip_aes_info_s {
    uint16_t vendor_version;
    uint8_t strength;
    uint16_t compression_method;
} xx_zip_aes_info;

typedef struct xx_zip_password_s {
    const uint8_t *bytes;
    size_t size;
    char *owned_utf8;
} xx_zip_password;

typedef struct xx_zip_buffer_sink_s {
    uint8_t *buffer;
    size_t capacity;
    size_t position;
} xx_zip_buffer_sink;

typedef struct xx_zip_verify_sink_s {
    xx_crc_context crc;
    uint64_t position;
} xx_zip_verify_sink;

typedef struct xx_zip_limit_sink_s {
    xx_io_device *target;
    uint64_t limit;
    uint64_t position;
    xx_pd_struct *pd;
} xx_zip_limit_sink;

static uint16_t xx_zip_read_le16_bytes(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static bool xx_zip_parse_aes_extra(const xx_archive_record *record,
                                   xx_zip_aes_info *info) {
    const xx_var *extra_var;
    const uint8_t *extra;
    size_t extra_size = 0;
    size_t offset = 0;

    if (!record || !info) {
        return false;
    }
    extra_var = xx_archive_record_find_meta(record, XX_META_ID_EXTRA_FIELD);
    extra = extra_var
        ? (const uint8_t *)xx_var_get_bytes(extra_var, &extra_size)
        : NULL;

    while (extra && offset + 4U <= extra_size) {
        uint16_t tag = xx_zip_read_le16_bytes(extra + offset);
        uint16_t field_size = xx_zip_read_le16_bytes(extra + offset + 2U);
        const uint8_t *field;
        offset += 4U;
        if ((size_t)field_size > extra_size - offset) {
            return false;
        }
        field = extra + offset;
        if (tag == 0x9901U) {
            if (field_size < 7U || field[2] != 'A' || field[3] != 'E') {
                return false;
            }
            info->vendor_version = xx_zip_read_le16_bytes(field);
            info->strength = field[4];
            info->compression_method = xx_zip_read_le16_bytes(field + 5U);
            return (info->vendor_version == 1U || info->vendor_version == 2U) &&
                   xx_winzip_aes_key_size(info->strength) != 0U;
        }
        offset += field_size;
    }
    return false;
}

static bool xx_zip_get_password(const Abstractformat *format,
                                const xx_list_s *options,
                                xx_zip_password *password) {
    const xx_var *value;

    if (!password) {
        return false;
    }
    xx_mem_zero(password, sizeof(*password));
    value = xx_format_resolve_extra_parameter(
        format, options, XX_META_ID_OPT_PASSWORD);
    if (!value) {
        return false;
    }

    if (value->type == XX_VAR_TYPE_BYTES ||
        value->type == XX_VAR_TYPE_BYTES_VIEW) {
        password->bytes = (const uint8_t *)xx_var_get_bytes(value, &password->size);
        return password->bytes != NULL || password->size == 0U;
    }
    if (value->type == XX_VAR_TYPE_STRING ||
        value->type == XX_VAR_TYPE_STRING_VIEW) {
        password->bytes = (const uint8_t *)xx_var_get_str(value);
        password->size = value->val.str.len;
        return password->bytes != NULL || password->size == 0U;
    }
    if (value->type == XX_VAR_TYPE_WSTRING ||
        value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        const wchar_t *wide = xx_var_get_wstr(value);
        size_t wide_size = value->val.wstr.len;
        wchar_t *terminated;
        if ((!wide && wide_size != 0U) ||
            wide_size > (SIZE_MAX / sizeof(wchar_t)) - 1U) {
            return false;
        }
        terminated = (wchar_t *)xx_mem_alloc((wide_size + 1U) * sizeof(wchar_t));
        if (!terminated) {
            return false;
        }
        if (wide_size > 0U) {
            xx_mem_copy(terminated, wide, wide_size * sizeof(wchar_t));
        }
        terminated[wide_size] = L'\0';
        password->owned_utf8 = xx_str_unicode_to_utf8(terminated);
        xx_mem_zero(terminated, (wide_size + 1U) * sizeof(wchar_t));
        xx_mem_free(terminated);
        if (!password->owned_utf8) {
            return false;
        }
        password->bytes = (const uint8_t *)password->owned_utf8;
        password->size = xx_str_len(password->owned_utf8);
        return true;
    }
    return false;
}

static void xx_zip_cleanup_password(xx_zip_password *password) {
    if (!password) {
        return;
    }
    if (password->owned_utf8) {
        xx_mem_zero(password->owned_utf8, password->size);
        xx_str_free(password->owned_utf8);
    }
    xx_mem_zero(password, sizeof(*password));
}

static bool xx_zip_read_exact_at(xx_io_device *device, int64_t offset,
                                 void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t done = 0;

    if (!device || offset < 0 || (!buffer && size != 0U) ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(device, bytes + done, size - done);
        if (amount <= 0) {
            return false;
        }
        done += (size_t)amount;
    }
    return true;
}

static bool xx_zip_read_exact_at_progress(xx_io_device *device,
                                          int64_t offset, void *buffer,
                                          size_t size, xx_pd_struct *pd) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t done = 0;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_pd_is_stopped(pd) ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        size_t chunk = size - done;
        ssize_t amount;
        if (xx_pd_is_stopped(pd)) {
            return false;
        }
        if (chunk > 65536U) {
            chunk = 65536U;
        }
        amount = xx_io_read(device, bytes + done, chunk);
        if (amount <= 0 || (size_t)amount > chunk) {
            return false;
        }
        done += (size_t)amount;
    }
    return !xx_pd_is_stopped(pd);
}

static bool xx_zip_write_exact(xx_io_device *device, const void *buffer,
                               size_t size) {
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t done = 0;

    if (!device || (!buffer && size != 0U)) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_write(device, bytes + done, size - done);
        if (amount <= 0) {
            return false;
        }
        done += (size_t)amount;
    }
    return true;
}

static bool xx_zip_write_exact_progress(xx_io_device *device,
                                        const void *buffer, size_t size,
                                        xx_pd_struct *pd) {
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t done = 0;
    if (!device || (!buffer && size != 0U)) {
        return false;
    }
    while (done < size) {
        size_t chunk = size - done;
        ssize_t amount;
        if (xx_pd_is_stopped(pd)) {
            return false;
        }
        if (chunk > 65536U) {
            chunk = 65536U;
        }
        amount = xx_io_write(device, bytes + done, chunk);
        if (amount <= 0 || (size_t)amount > chunk) {
            return false;
        }
        done += (size_t)amount;
    }
    return !xx_pd_is_stopped(pd);
}

static bool xx_zip_crc32_progress(const void *buffer, size_t size,
                                  uint32_t *crc, xx_pd_struct *pd) {
    const uint8_t *bytes = (const uint8_t *)buffer;
    xx_crc_context context;
    size_t done = 0;
    if ((!buffer && size != 0U) || !crc ||
        !xx_crc_context_init_type(&context, XX_CRC_TYPE_CRC32)) {
        return false;
    }
    while (done < size) {
        size_t chunk = size - done;
        if (xx_pd_is_stopped(pd)) {
            return false;
        }
        if (chunk > 65536U) {
            chunk = 65536U;
        }
        xx_crc_context_update(&context, bytes + done, chunk);
        done += chunk;
    }
    if (xx_pd_is_stopped(pd)) {
        return false;
    }
    *crc = (uint32_t)xx_crc_context_final(&context);
    return true;
}

static ssize_t xx_zip_buffer_sink_write(xx_io_device *device,
                                        const void *buffer, size_t size) {
    xx_zip_buffer_sink *sink;
    if (!device || !device->priv || (!buffer && size != 0U)) {
        return -1;
    }
    sink = (xx_zip_buffer_sink *)device->priv;
    if (size > sink->capacity - sink->position) {
        return -1;
    }
    if (size > 0U) {
        xx_mem_copy(sink->buffer + sink->position, buffer, size);
        sink->position += size;
    }
    return (ssize_t)size;
}

static int64_t xx_zip_buffer_sink_size(xx_io_device *device) {
    const xx_zip_buffer_sink *sink;
    if (!device || !device->priv) {
        return -1;
    }
    sink = (const xx_zip_buffer_sink *)device->priv;
#if SIZE_MAX > INT64_MAX
    if (sink->position > (size_t)INT64_MAX) {
        return -1;
    }
#endif
    return (int64_t)sink->position;
}

static void xx_zip_init_buffer_sink(xx_io_device *device,
                                    xx_zip_buffer_sink *sink,
                                    uint8_t *buffer, size_t capacity) {
    xx_mem_zero(device, sizeof(*device));
    xx_mem_zero(sink, sizeof(*sink));
    sink->buffer = buffer;
    sink->capacity = capacity;
    device->write = xx_zip_buffer_sink_write;
    device->total_size = xx_zip_buffer_sink_size;
    device->get_total_size = xx_zip_buffer_sink_size;
    device->size = xx_zip_buffer_sink_size;
    device->priv = sink;
}

static ssize_t xx_zip_verify_sink_write(xx_io_device *device,
                                        const void *buffer, size_t size) {
    xx_zip_verify_sink *sink;
    if (!device || !device->priv || (!buffer && size != 0U) ||
        size > (size_t)PTRDIFF_MAX) {
        return -1;
    }
    sink = (xx_zip_verify_sink *)device->priv;
    if ((uint64_t)size > (uint64_t)INT64_MAX - sink->position) {
        return -1;
    }
    xx_crc_context_update(&sink->crc, buffer, size);
    sink->position += (uint64_t)size;
    return (ssize_t)size;
}

static int64_t xx_zip_verify_sink_size(xx_io_device *device) {
    const xx_zip_verify_sink *sink;
    if (!device || !device->priv) {
        return -1;
    }
    sink = (const xx_zip_verify_sink *)device->priv;
    return (int64_t)sink->position;
}

static bool xx_zip_init_verify_sink(xx_io_device *device,
                                    xx_zip_verify_sink *sink) {
    if (!device || !sink) {
        return false;
    }
    xx_mem_zero(device, sizeof(*device));
    xx_mem_zero(sink, sizeof(*sink));
    if (!xx_crc_context_init_type(&sink->crc, XX_CRC_TYPE_CRC32)) {
        return false;
    }
    device->write = xx_zip_verify_sink_write;
    device->total_size = xx_zip_verify_sink_size;
    device->get_total_size = xx_zip_verify_sink_size;
    device->size = xx_zip_verify_sink_size;
    device->priv = sink;
    return true;
}

static ssize_t xx_zip_limit_sink_write(xx_io_device *device,
                                       const void *buffer, size_t size) {
    xx_zip_limit_sink *sink;
    ssize_t written;
    if (!device || !device->priv || (!buffer && size != 0U) ||
        size > (size_t)PTRDIFF_MAX) {
        return -1;
    }
    sink = (xx_zip_limit_sink *)device->priv;
    if (!sink->target || xx_pd_is_stopped(sink->pd) ||
        (uint64_t)size > sink->limit - sink->position) {
        return -1;
    }
    written = xx_io_write(sink->target, buffer, size);
    if (written <= 0 || (size_t)written > size) {
        return written;
    }
    sink->position += (uint64_t)written;
    return written;
}

static int64_t xx_zip_limit_sink_size(xx_io_device *device) {
    const xx_zip_limit_sink *sink;
    if (!device || !device->priv) {
        return -1;
    }
    sink = (const xx_zip_limit_sink *)device->priv;
    return (int64_t)sink->position;
}

static void xx_zip_init_limit_sink(xx_io_device *device,
                                   xx_zip_limit_sink *sink,
                                   xx_io_device *target, uint64_t limit,
                                   xx_pd_struct *pd) {
    xx_mem_zero(device, sizeof(*device));
    xx_mem_zero(sink, sizeof(*sink));
    sink->target = target;
    sink->limit = limit;
    sink->pd = pd;
    device->write = xx_zip_limit_sink_write;
    device->total_size = xx_zip_limit_sink_size;
    device->get_total_size = xx_zip_limit_sink_size;
    device->size = xx_zip_limit_sink_size;
    device->priv = sink;
}

static bool xx_zip_method_is_supported(uint16_t method) {
    switch (method) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 8:
        case 9:
        case 12:
        case 14:
        case 33:
        case 93:
        case 98:
            return true;
        default:
            return false;
    }
}

static bool xx_zip_unpack_method_to_device_unchecked(
    xx_io_device *source, int64_t source_offset, int64_t compressed_size,
    uint16_t method, uint16_t flags, uint64_t uncompressed_size,
    xx_io_device *destination, xx_pd_struct *pd) {
    int64_t expected_size;

    if (!source || !destination || source_offset < 0 || compressed_size < 0 ||
        uncompressed_size > (uint64_t)INT64_MAX ||
        (!xx_zip_method_is_supported(method)) ||
        (compressed_size == 0 && uncompressed_size != 0U)) {
        return false;
    }
    expected_size = (int64_t)uncompressed_size;

    switch (method) {
        case 0:
            return (uint64_t)compressed_size == uncompressed_size &&
                   xx_store_unpack_device(source, source_offset, compressed_size,
                                          destination, pd);
        case 1:
            return xx_shrink_unpack_device(source, source_offset, compressed_size,
                                           destination, expected_size, pd);
        case 2:
        case 3:
        case 4:
        case 5:
            return xx_reduce_unpack_device(source, source_offset, compressed_size,
                                           destination, expected_size,
                                           (int)method - 1, pd);
        case 6:
            return xx_implode_unpack_device(source, source_offset, compressed_size,
                                            destination, expected_size,
                                            (flags & 0x0002U) != 0U,
                                            (flags & 0x0004U) != 0U, pd);
        case 8:
        case 9:
            return xx_deflate_unpack_device(source, source_offset, compressed_size,
                                            destination, method == 9U, pd);
        case 12:
            return xx_bzip2_unpack_device(source, source_offset, compressed_size,
                                          destination, pd);
        case 14: {
            uint8_t header[9];
            if (compressed_size < (int64_t)sizeof(header) ||
                !xx_zip_read_exact_at(source, source_offset, header, sizeof(header)) ||
                header[2] != XX_LZMA_PROPS_SIZE || header[3] != 0U) {
                return false;
            }
            return xx_lzma_unpack_device(source, source_offset + (int64_t)sizeof(header),
                                         compressed_size - (int64_t)sizeof(header),
                                         header + 4U, XX_LZMA_PROPS_SIZE,
                                         expected_size, destination, pd);
        }
        case 33: {
            uint8_t header[XX_ZIP_LZMA2_HEADER_SIZE];
            uint8_t end_marker;
            if (compressed_size < (int64_t)(sizeof(header) + 1U) ||
                source_offset > INT64_MAX - compressed_size ||
                !xx_zip_read_exact_at(source, source_offset, header,
                                      sizeof(header)) ||
                xx_zip_read_le16_bytes(header) != XX_ZIP_LZMA2_VERSION ||
                xx_zip_read_le16_bytes(header + 2U) != XX_LZMA2_PROPS_SIZE ||
                header[4] > XX_ZIP_LZMA2_MAX_PROP ||
                !xx_zip_read_exact_at(source,
                                      source_offset + compressed_size - 1,
                                      &end_marker, sizeof(end_marker)) ||
                end_marker != 0U) {
                return false;
            }
            return xx_lzma2_unpack_device(source, source_offset + (int64_t)sizeof(header),
                                          compressed_size - (int64_t)sizeof(header),
                                          header[4], destination, pd);
        }
        case 93:
            return xx_zstd_unpack_device_to_device(source, source_offset,
                                                   compressed_size, destination,
                                                   uncompressed_size, pd);
        case 98: {
            uint8_t header[2];
            uint16_t properties;
            int order = 0;
            uint32_t memory_mb = 0;
            int restore_method = 0;
            if (compressed_size < (int64_t)sizeof(header) ||
                !xx_zip_read_exact_at(source, source_offset, header, sizeof(header))) {
                return false;
            }
            properties = xx_zip_read_le16_bytes(header);
            if (!xx_ppmd8_parse_zip_props(properties, &order, &memory_mb,
                                          &restore_method)) {
                return false;
            }
            return xx_ppmd8_unpack_device(source, source_offset + 2,
                                          compressed_size - 2, expected_size,
                                          order, memory_mb, restore_method,
                                          destination, pd);
        }
        default:
            return false;
    }
}

static bool xx_zip_unpack_method_to_device(xx_io_device *source,
                                           int64_t source_offset,
                                           int64_t compressed_size,
                                           uint16_t method, uint16_t flags,
                                           uint64_t uncompressed_size,
                                           xx_io_device *destination,
                                           xx_pd_struct *pd) {
    xx_io_device limited_device;
    xx_zip_limit_sink limited;
    if (!destination || uncompressed_size > (uint64_t)INT64_MAX) {
        return false;
    }
    xx_zip_init_limit_sink(&limited_device, &limited, destination,
                           uncompressed_size, pd);
    return xx_zip_unpack_method_to_device_unchecked(
               source, source_offset, compressed_size, method, flags,
               uncompressed_size, &limited_device, pd) &&
           limited.position == uncompressed_size &&
           !xx_pd_is_stopped(pd);
}

static bool xx_zip_verify_unencrypted_record(
    Abstractformat *format, const xx_archive_record *record,
    xx_pd_struct *pd) {
    xx_io_device destination;
    xx_zip_verify_sink sink;
    uint16_t method;
    uint16_t flags;
    uint64_t uncompressed_size;
    uint64_t expected_crc;
    if (!format || !format->device || !record || record->data_offset < 0 ||
        record->compressed_size < 0 || xx_pd_is_stopped(pd)) {
        return false;
    }
    method = (uint16_t)xx_archive_record_get_meta_u64(
        record, XX_META_ID_COMPRESSION_METHOD, 0);
    flags = (uint16_t)xx_archive_record_get_meta_u64(
        record, XX_META_ID_FLAGS, 0);
    uncompressed_size = xx_archive_record_get_meta_u64(
        record, XX_META_ID_UNCOMPRESSED_SIZE, 0);
    expected_crc = xx_archive_record_get_meta_u64(
        record, XX_META_ID_CRC32, UINT64_MAX);
    if (uncompressed_size > (uint64_t)INT64_MAX ||
        expected_crc > UINT32_MAX ||
        !xx_zip_init_verify_sink(&destination, &sink) ||
        !xx_zip_unpack_method_to_device(
            format->device, record->data_offset, record->compressed_size,
            method, flags, uncompressed_size, &destination, pd)) {
        return false;
    }
    return sink.position == uncompressed_size &&
           xx_crc_context_final(&sink.crc) == expected_crc &&
           !xx_pd_is_stopped(pd);
}

static void xx_zip_normalize_record_path(char *path) {
    if (!path) {
        return;
    }
    while (*path) {
        if (*path == '\\') {
            *path = '/';
        }
        ++path;
    }
}

static bool xx_zip_derived_name_matches(const xx_archive_record *record,
                                        const char *prefix,
                                        const char *suffix,
                                        bool single_component,
                                        bool exact) {
    const char *name;
    const wchar_t *name_w;
    char *owned_name = NULL;
    size_t name_length;
    size_t prefix_length;
    size_t suffix_length;
    bool result = false;

    if (!record || !prefix) {
        return false;
    }
    name = xx_archive_record_get_original_name(record);
    name_w = xx_archive_record_get_original_name_w(record);
    if (name) {
        owned_name = xx_str_dup(name);
    } else if (name_w) {
        owned_name = xx_str_unicode_to_utf8(name_w);
    }
    if (!owned_name) {
        return false;
    }
    xx_zip_normalize_record_path(owned_name);
    if (exact) {
        result = xx_rt_strcmp(owned_name, prefix) == 0;
        xx_str_free(owned_name);
        return result;
    }

    name_length = xx_rt_strlen(owned_name);
    prefix_length = xx_rt_strlen(prefix);
    suffix_length = suffix ? xx_rt_strlen(suffix) : 0U;
    if (suffix && name_length > prefix_length + suffix_length &&
        xx_rt_memcmp(owned_name, prefix, prefix_length) == 0 &&
        xx_rt_memcmp(owned_name + name_length - suffix_length, suffix,
               suffix_length) == 0) {
        const char *middle = owned_name + prefix_length;
        size_t middle_length = name_length - prefix_length - suffix_length;
        result = true;
        if (single_component &&
            xx_rt_memchr(middle, '/', middle_length) != NULL) {
            result = false;
        }
    }
    xx_str_free(owned_name);
    return result;
}

static bool xx_zip_has_valid_file_impl(Abstractformat *self,
                                       const char *prefix,
                                       const char *suffix,
                                       bool single_component,
                                       bool exact,
                                       uint64_t max_size,
                                       xx_pd_struct *pd) {
    enum { XX_ZIP_DERIVED_RECORD_LIMIT = 20000 };
    xx_zip probe;
    xx_archive_record_state *state = NULL;
    bool result = false;
    uint64_t visited = 0;

    if (!self || !self->device || !prefix || !prefix[0] ||
        (!exact && (!suffix || !suffix[0])) || max_size == 0U) {
        return false;
    }

    /* Use a plain ZIP view so a derived handle_base_info callback cannot
     * recurse while the mandatory record is being located. */
    xx_zip_init(&probe, self->device, self->base_address);
    if (!xx_zip_handle_base_info(&probe.format, pd)) {
        xx_zip_destroy(&probe);
        return false;
    }
    probe.format.base_info_handled = true;

    /* Keep the caller's progress hierarchy untouched when a matching entry is
     * found before the end of the central directory. */
    state = xx_zip_create_archive_records_reading(&probe.format, NULL, NULL);
    while (state && state->has_record &&
           visited < (uint64_t)XX_ZIP_DERIVED_RECORD_LIMIT) {
        const xx_archive_record *record =
            xx_zip_get_current_archive_record(&probe.format, state);
        bool name_matches = xx_zip_derived_name_matches(
            record, prefix, suffix, single_component, exact);

        if (pd && xx_pd_is_stopped(pd)) {
            break;
        }
        ++visited;
        if (name_matches) {
            uint64_t unpacked_size = xx_archive_record_get_meta_u64(
                record, XX_META_ID_UNCOMPRESSED_SIZE, 0);
            uint64_t expected_crc = xx_archive_record_get_meta_u64(
                record, XX_META_ID_CRC32, UINT64_MAX);
            uint16_t method = (uint16_t)xx_archive_record_get_meta_u64(
                record, XX_META_ID_COMPRESSION_METHOD, UINT64_MAX);
            uint16_t flags = (uint16_t)xx_archive_record_get_meta_u64(
                record, XX_META_ID_FLAGS, 0);
            int64_t packed_size = record->compressed_size;
            int64_t device_size = xx_io_total_size(self->device);

            if (unpacked_size > 0U && unpacked_size <= max_size &&
                unpacked_size <= (uint64_t)SIZE_MAX && packed_size > 0 &&
                (uint64_t)packed_size <= max_size && record->data_offset >= 0 &&
                device_size >= 0 && record->data_offset <= device_size &&
                packed_size <= device_size - record->data_offset &&
                !xx_archive_record_get_meta_bool(
                    record, XX_META_ID_IS_ENCRYPTED, false) &&
                xx_zip_method_is_supported(method)) {
                uint8_t *decoded =
                    (uint8_t *)xx_mem_alloc((size_t)unpacked_size);
                if (decoded) {
                    xx_io_device destination;
                    xx_zip_buffer_sink sink;
                    xx_zip_init_buffer_sink(&destination, &sink, decoded,
                                            (size_t)unpacked_size);
                    result = xx_zip_unpack_method_to_device(
                                 self->device, record->data_offset, packed_size,
                                 method, flags, unpacked_size, &destination,
                                 pd) &&
                             sink.position == (size_t)unpacked_size &&
                             expected_crc <= UINT32_MAX &&
                             xx_crc32(XX_CRC_TYPE_CRC32, decoded,
                                      (size_t)unpacked_size) ==
                                 (uint32_t)expected_crc;
                    xx_mem_zero(decoded, (size_t)unpacked_size);
                    xx_mem_free(decoded);
                }
            }
            break;
        }
        if (!xx_zip_archive_record_move_to_next(&probe.format, state, NULL)) {
            break;
        }
    }

    if (state) {
        xx_zip_free_archive_records_reading(&probe.format, state);
    }
    xx_zip_destroy(&probe);
    return result;
}

bool xx_zip_has_valid_file(Abstractformat *self, const char *record_name,
                           uint64_t max_size, xx_pd_struct *pd) {
    return xx_zip_has_valid_file_impl(self, record_name, NULL, false, true,
                                      max_size, pd);
}

bool xx_zip_has_valid_file_pattern(Abstractformat *self,
                                   const char *prefix,
                                   const char *suffix,
                                   bool single_component,
                                   uint64_t max_size,
                                   xx_pd_struct *pd) {
    return xx_zip_has_valid_file_impl(self, prefix, suffix, single_component,
                                      false, max_size, pd);
}

static bool xx_zip_unpack_encrypted_to_device(
    Abstractformat *format, const xx_archive_record_state *state,
    const xx_archive_record *record, uint16_t outer_method, uint16_t flags,
    xx_io_device *destination, xx_pd_struct *pd) {
    xx_zip_password password;
    xx_zip_aes_info aes_info = {0};
    uint8_t *envelope = NULL;
    uint8_t *compressed = NULL;
    uint8_t *plain = NULL;
    size_t envelope_size = 0;
    size_t compressed_capacity = 0;
    size_t compressed_size = 0;
    size_t plain_size;
    int64_t record_compressed_size;
    uint64_t uncompressed_size;
    uint32_t expected_crc;
    uint16_t last_mod_time;
    uint16_t actual_method = outer_method;
    bool is_aes = outer_method == 99U;
    bool verify_crc = true;
    bool memory_limit_present = false;
    uint64_t memory_limit = 0U;
    uint64_t required_memory = 0U;
    bool success = false;
    xx_io_device *compressed_device = NULL;
    xx_io_device plain_device;
    xx_zip_buffer_sink plain_sink;

    if (!format || !format->device || !state || !record) {
        return false;
    }
    xx_mem_zero(&password, sizeof(password));
    record_compressed_size = record->compressed_size;
    uncompressed_size = xx_archive_record_get_meta_u64(
        record, XX_META_ID_UNCOMPRESSED_SIZE, 0);
    expected_crc = (uint32_t)xx_archive_record_get_meta_u64(
        record, XX_META_ID_CRC32, 0);
    last_mod_time = (uint16_t)xx_archive_record_get_meta_u64(
        record, XX_META_ID_LAST_MOD_TIME, 0);

    if (record->data_offset < 0 || record_compressed_size < 0 ||
        (uint64_t)record_compressed_size > (uint64_t)SIZE_MAX ||
        uncompressed_size > (uint64_t)SIZE_MAX) {
        goto cleanup;
    }
    envelope_size = (size_t)record_compressed_size;

    /* Derive all allocation sizes and enforce the optional budget before
     * reading payload bytes or allocating any whole-member buffer. */
    if (is_aes) {
        size_t salt_size;
        if (!xx_zip_parse_aes_extra(record, &aes_info)) {
            goto cleanup;
        }
        salt_size = xx_winzip_aes_salt_size(aes_info.strength);
        if (envelope_size < salt_size + XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE +
                                XX_WINZIP_AES_AUTH_CODE_SIZE) {
            goto cleanup;
        }
        compressed_capacity = envelope_size - salt_size -
                              XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE -
                              XX_WINZIP_AES_AUTH_CODE_SIZE;
        actual_method = aes_info.compression_method;
        verify_crc = aes_info.vendor_version == 1U;
    } else {
        if ((flags & 0x0040U) != 0U ||
            envelope_size < XX_ZIPCRYPTO_HEADER_SIZE) {
            goto cleanup;
        }
        compressed_capacity = envelope_size - XX_ZIPCRYPTO_HEADER_SIZE;
    }
    plain_size = (size_t)uncompressed_size;
    if (!xx_zip_get_u64_limit(format, &state->options,
                              XX_META_ID_OPT_MEMORY_LIMIT,
                              &memory_limit_present, &memory_limit)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG,
                        "ZIP extraction limits must be nonnegative integers");
        goto cleanup;
    }
    if (memory_limit_present &&
        (!xx_zip_add_memory(&required_memory,
                            envelope_size ? (uint64_t)envelope_size : 1U) ||
         !xx_zip_add_memory(&required_memory,
                            compressed_capacity
                                ? (uint64_t)compressed_capacity : 1U) ||
         !xx_zip_add_memory(&required_memory,
                            plain_size ? (uint64_t)plain_size : 1U) ||
         required_memory > memory_limit)) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS,
                        "ZIP extraction exceeds the configured memory limit");
        goto cleanup;
    }
    if (!xx_zip_get_password(format, &state->options, &password)) {
        goto cleanup;
    }
    envelope = (uint8_t *)xx_mem_alloc(envelope_size ? envelope_size : 1U);
    if (!envelope ||
        !xx_zip_read_exact_at_progress(format->device, record->data_offset,
                                       envelope, envelope_size, pd)) {
        goto cleanup;
    }

    if (is_aes) {
        compressed = (uint8_t *)xx_mem_alloc(compressed_capacity ?
                                             compressed_capacity : 1U);
        if (xx_pd_is_stopped(pd) || !compressed ||
            !xx_winzip_aes_decrypt_envelope_progress(
                envelope, envelope_size, password.bytes, password.size,
                aes_info.strength, compressed, compressed_capacity,
                &compressed_size, pd) ||
            xx_pd_is_stopped(pd)) {
            goto cleanup;
        }
    } else {
        compressed = (uint8_t *)xx_mem_alloc(compressed_capacity ?
                                             compressed_capacity : 1U);
        if (xx_pd_is_stopped(pd) || !compressed ||
            !xx_zipcrypto_decrypt_envelope_progress(
                envelope, envelope_size, password.bytes, password.size,
                expected_crc, last_mod_time,
                (flags & 0x0008U) != 0U, compressed, compressed_capacity,
                &compressed_size, pd) ||
            xx_pd_is_stopped(pd)) {
            goto cleanup;
        }
    }

    if (!xx_zip_method_is_supported(actual_method) ||
        (uint64_t)compressed_size > (uint64_t)INT64_MAX) {
        goto cleanup;
    }
    plain = (uint8_t *)xx_mem_alloc(plain_size ? plain_size : 1U);
    if (!plain) {
        goto cleanup;
    }
    compressed_device = xx_io_mem_open_ro(compressed, compressed_size);
    if (!compressed_device) {
        goto cleanup;
    }
    xx_zip_init_buffer_sink(&plain_device, &plain_sink, plain, plain_size);
    if (!xx_zip_unpack_method_to_device(compressed_device, 0,
                                        (int64_t)compressed_size,
                                        actual_method, flags,
                                        uncompressed_size,
                                        &plain_device, pd) ||
        plain_sink.position != plain_size) {
        goto cleanup;
    }
    if (verify_crc) {
        uint32_t actual_crc;
        if (!xx_zip_crc32_progress(plain, plain_size, &actual_crc, pd) ||
            actual_crc != expected_crc) {
            goto cleanup;
        }
    }

    if (!destination) {
        success = true;
    } else if (xx_zip_write_exact_progress(destination, plain, plain_size,
                                           pd)) {
        success = true;
    }

cleanup:
    if (compressed_device) {
        xx_io_close(compressed_device);
    }
    if (plain) {
        xx_mem_zero(plain, (size_t)uncompressed_size);
        xx_mem_free(plain);
    }
    if (compressed) {
        xx_mem_zero(compressed, compressed_capacity);
        xx_mem_free(compressed);
    }
    if (envelope) {
        xx_mem_zero(envelope, envelope_size);
        xx_mem_free(envelope);
    }
    xx_zip_cleanup_password(&password);
    return success;
}

static bool xx_zip_relative_name_is_safe(const wchar_t *name) {
    const wchar_t *segment;
    const wchar_t *position;
    if (!name || !name[0] || name[0] == L'/' || name[0] == L'\\' ||
        (name[0] && name[1] == L':')) {
        return false;
    }
    segment = name;
    position = name;
    for (;;) {
        bool at_end = *position == L'\0';
        bool separator = *position == L'/' || *position == L'\\';
        if (*position == L':') {
            return false;
        }
        if (at_end || separator) {
            size_t length = (size_t)(position - segment);
            if ((length == 1U && segment[0] == L'.') ||
                (length == 2U && segment[0] == L'.' &&
                 segment[1] == L'.')) {
                return false;
            }
            /* Win32 aliases leading spaces and trailing spaces/dots, which
             * could otherwise turn a lexically safe name into a different
             * on-disk path (including a reserved device name). The checks run
             * on every platform so an archive rejected on one is rejected on
             * all; xx_io_platform_wsegment_is_reserved is false off Windows. */
            if (length != 0U &&
                (segment[0] == L' ' || segment[length - 1U] == L'.' ||
                  segment[length - 1U] == L' ')) {
                return false;
            }
            if (length != 0U &&
                xx_io_platform_wsegment_is_reserved(segment, length)) {
                return false;
            }
            if (at_end) {
                return true;
            }
            segment = position + 1;
        }
        position++;
    }
}

static wchar_t *xx_zip_make_destination(const wchar_t *base,
                                        const wchar_t *name) {
    wchar_t *normalized;
    wchar_t *result;
    size_t base_length;
    bool separator_needed;
    if (!base || !name || !xx_zip_relative_name_is_safe(name)) {
        return NULL;
    }
    const wchar_t separator = xx_io_platform_wseparator();
    const wchar_t separator_text[2] = {separator, L'\0'};

    normalized = xx_str_wdup(name);
    if (!normalized) {
        return NULL;
    }
    for (size_t index = 0; normalized[index]; ++index) {
        if (normalized[index] == L'/' || normalized[index] == L'\\') {
            normalized[index] = separator;
        }
    }
    base_length = xx_str_wlen(base);
    separator_needed = base_length != 0U &&
                       base[base_length - 1U] != L'/' &&
                       base[base_length - 1U] != L'\\';
    result = separator_needed ? xx_str_wconcat3(base, separator_text, normalized)
                              : xx_str_wconcat(base, normalized);
    xx_str_wfree(normalized);
    return result;
}

static xx_io_device *xx_zip_open_stage_file(const char *destination,
                                             char **stage_path) {
    unsigned int attempt;
    if (!destination || !stage_path) {
        return NULL;
    }
    *stage_path = NULL;
    for (attempt = 0U; attempt < 10000U; ++attempt) {
        char suffix[48];
        char *candidate;
        xx_io_device *device;
        int length = xx_rt_snprintf(suffix, sizeof(suffix),
                              ".xxfclib.tmp.%u", attempt);
        if (length <= 0 || (size_t)length >= sizeof(suffix)) {
            return NULL;
        }
        candidate = xx_str_concat(destination, suffix);
        if (!candidate) {
            return NULL;
        }
        device = xx_io_file_open(candidate, "wbx");
        if (device) {
            *stage_path = candidate;
            return device;
        }
        xx_str_free(candidate);
    }
    return NULL;
}

bool xx_zip_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    if (!self || !self->device || !state || !state->has_record ||
        xx_pd_is_stopped(pd)) {
        return false;
    }

    const xx_archive_record *rec = &state->current_record;
    bool is_folder = xx_archive_record_get_meta_bool(rec, XX_META_ID_IS_FOLDER, false);
    const char *orig_name = xx_archive_record_get_original_name(rec);
    const wchar_t *orig_name_w = xx_archive_record_get_original_name_w(rec);
    bool member_limit_present;
    bool memory_limit_present;
    uint64_t member_limit;
    uint64_t memory_limit;
    uint64_t declared_size = xx_archive_record_get_meta_u64(
        rec, XX_META_ID_UNCOMPRESSED_SIZE, 0);

    if (!xx_zip_get_u64_limit(self, &state->options,
                              XX_META_ID_OPT_MAX_MEMBER_SIZE,
                              &member_limit_present, &member_limit) ||
        !xx_zip_get_u64_limit(self, &state->options,
                              XX_META_ID_OPT_MEMORY_LIMIT,
                              &memory_limit_present, &memory_limit)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG,
                        "ZIP extraction limits must be nonnegative integers");
        return false;
    }
    if (member_limit_present && declared_size > member_limit) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS,
                        "ZIP member exceeds the configured size limit");
        return false;
    }
    (void)memory_limit_present;
    (void)memory_limit;

    if (is_folder &&
        (rec->compressed_size != 0 ||
         declared_size != 0U ||
         xx_archive_record_get_meta_bool(
             rec, XX_META_ID_IS_ENCRYPTED, false))) {
        return false;
    }

    /* Look for unpack path option */
    const xx_var *unpack_path_var = xx_zip_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!unpack_path_var) {
        unpack_path_var = xx_zip_find_option(&state->options, XX_META_ID_UNPACK_PATH);
    }

    const char *unpack_path_str = NULL;
    const wchar_t *unpack_path_wstr = NULL;
    if (unpack_path_var) {
        if (unpack_path_var->type == XX_VAR_TYPE_WSTRING || unpack_path_var->type == XX_VAR_TYPE_WSTRING_VIEW) {
            unpack_path_wstr = xx_var_get_wstr(unpack_path_var);
        } else if (unpack_path_var->type == XX_VAR_TYPE_STRING || unpack_path_var->type == XX_VAR_TYPE_STRING_VIEW) {
            unpack_path_str = xx_var_get_str(unpack_path_var);
        }
    }

    /* If no unpack path is given, fully decode and authenticate the member. */
    if (!unpack_path_wstr && !unpack_path_str) {
        bool is_encrypted;
        uint16_t method;
        uint16_t flags;
        if (is_folder) {
            return true;
        }
        is_encrypted = xx_archive_record_get_meta_bool(
            rec, XX_META_ID_IS_ENCRYPTED, false);
        if (!is_encrypted) {
            return xx_zip_verify_unencrypted_record(self, rec, pd);
        }
        method = (uint16_t)xx_archive_record_get_meta_u64(
            rec, XX_META_ID_COMPRESSION_METHOD, 0);
        flags = (uint16_t)xx_archive_record_get_meta_u64(
            rec, XX_META_ID_FLAGS, 0);
        return xx_zip_unpack_encrypted_to_device(
            self, state, rec, method, flags, NULL, pd);
    }

    /* Convert unpack path to unicode wchar_t buffer */
    wchar_t *unpack_path_w = NULL;
    if (unpack_path_wstr) {
        unpack_path_w = xx_str_wdup(unpack_path_wstr);
    } else if (unpack_path_str) {
        unpack_path_w = xx_str_utf8_to_unicode(unpack_path_str);
    }

    if (!unpack_path_w) {
        return false;
    }

    /* Construct destination path: unpack_path_w + separator + orig_name_w */
    wchar_t *item_name_w = NULL;
    if (orig_name_w && orig_name_w[0]) {
        item_name_w = xx_str_wdup(orig_name_w);
    } else if (orig_name && orig_name[0]) {
        item_name_w = xx_str_utf8_to_unicode(orig_name);
    } else {
        item_name_w = xx_str_wdup(L"unnamed_file");
    }

    /* Reject absolute, drive-qualified and dot-segment names before joining
       them to the extraction root. */
    wchar_t *full_dest_w = xx_zip_make_destination(unpack_path_w, item_name_w);

    xx_str_wfree(unpack_path_w);
    xx_str_wfree(item_name_w);

    if (!full_dest_w) {
        return false;
    }

    bool success = false;
    if (is_folder) {
        success = xx_store_create_dirs_w(full_dest_w, true);
        if (success) {
            uint16_t mod_time = (uint16_t)xx_archive_record_get_meta_u64(rec, XX_META_ID_LAST_MOD_TIME, 0);
            uint16_t mod_date = (uint16_t)xx_archive_record_get_meta_u64(rec, XX_META_ID_LAST_MOD_DATE, 0);
            uint32_t attrs = (uint32_t)xx_archive_record_get_meta_u64(rec, XX_META_ID_EXTERNAL_ATTRS, 0);
            if (attrs == 0) {
                attrs = (uint32_t)xx_archive_record_get_meta_u64(rec, XX_META_ID_ATTRIBUTES, 0x10);
            }
            xx_store_apply_dos_time_and_attrs_w(full_dest_w, mod_date, mod_time, attrs);
        }
    } else {
        bool parent_ready = false;
        /* Ensure parent directory exists */
        parent_ready = xx_store_create_dirs_w(full_dest_w, false);
        /* Convert full path to UTF-8 for xx_io_file_open */
        char *dest_path_utf8 = xx_str_unicode_to_utf8(full_dest_w);
        if (parent_ready && dest_path_utf8) {
            uint16_t method = (uint16_t)xx_archive_record_get_meta_u64(
                rec, XX_META_ID_COMPRESSION_METHOD, 0);
            uint16_t flags = (uint16_t)xx_archive_record_get_meta_u64(
                rec, XX_META_ID_FLAGS, 0);
            uint64_t uncomp_size = xx_archive_record_get_meta_u64(
                rec, XX_META_ID_UNCOMPRESSED_SIZE, 0);
            int64_t comp_size = rec->compressed_size;
            int64_t data_offset = rec->data_offset;
            const xx_var *overwrite_var = xx_format_resolve_extra_parameter(
                self, &state->options, XX_META_ID_OPT_OVERWRITE);
            bool overwrite = overwrite_var && xx_var_get_bool(overwrite_var);
            char *stage_path = NULL;
            xx_io_device *stage = NULL;

            bool is_encrypted = xx_archive_record_get_meta_bool(rec, XX_META_ID_IS_ENCRYPTED, false);
            if (overwrite || !xx_io_file_exists_a(dest_path_utf8)) {
                stage = xx_zip_open_stage_file(dest_path_utf8, &stage_path);
            }
            if (stage && is_encrypted) {
                success = xx_zip_unpack_encrypted_to_device(
                    self, state, rec, method, flags, stage, pd);
            } else if (stage) {
                success = xx_zip_unpack_method_to_device(
                    self->device, data_offset, comp_size, method, flags,
                    uncomp_size, stage, pd);
            }
            if (stage && xx_io_close(stage) != 0) {
                success = false;
            }
            stage = NULL;
            if (success && !is_encrypted) {
                /* Validate every extracted member. For split sets this also
                 * detects reordered/corrupt payload-only continuation disks. */
                xx_io_device *verify = xx_io_file_open(stage_path, "rb");
                success = verify && uncomp_size <= INT64_MAX &&
                    xx_crc_verify_device(verify, 0, (int64_t)uncomp_size,
                        XX_CRC_TYPE_CRC32,
                        xx_archive_record_get_meta_u64(rec, XX_META_ID_CRC32,
                                                       UINT64_MAX), pd);
                if (verify && xx_io_close(verify) != 0) success = false;
                if (!success) {
                    xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG,
                                    "ZIP output CRC mismatch");
                }
            }
            if (success) {
                success = xx_io_file_replace_a(stage_path, dest_path_utf8,
                                               overwrite);
            }
            if (stage_path) {
                /* A successful replace has already moved the stage file. */
                if (xx_io_file_exists_a(stage_path)) {
                    (void)xx_io_file_remove_a(stage_path);
                }
                xx_str_free(stage_path);
            }
            if (success) {
                uint16_t mod_time = (uint16_t)xx_archive_record_get_meta_u64(rec, XX_META_ID_LAST_MOD_TIME, 0);
                uint16_t mod_date = (uint16_t)xx_archive_record_get_meta_u64(rec, XX_META_ID_LAST_MOD_DATE, 0);
                uint32_t attrs = (uint32_t)xx_archive_record_get_meta_u64(rec, XX_META_ID_EXTERNAL_ATTRS, 0);
                if (attrs == 0) {
                    attrs = (uint32_t)xx_archive_record_get_meta_u64(rec, XX_META_ID_ATTRIBUTES, 0);
                }
                xx_store_apply_dos_time_and_attrs_w(full_dest_w, mod_date, mod_time, attrs);
            }
            xx_str_free(dest_path_utf8);
        } else {
            xx_str_free(dest_path_utf8);
        }
    }

    xx_str_wfree(full_dest_w);
    return success;
}

void xx_zip_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    if (!state) {
        return;
    }
    xx_archive_record_state_free(state);
}

/* ========================================================================= */
/* --- Stream Archive Records Writing / Packing                          --- */
/* ========================================================================= */

#define XX_ZIP_AES_EXTRA_FIELD_SIZE 11U
#define XX_ZIP_AES_VENDOR_VERSION_AE2 2U

typedef struct xx_zip_grow_sink_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
} xx_zip_grow_sink;

static ssize_t xx_zip_grow_sink_write(xx_io_device *device,
                                      const void *buffer, size_t size) {
    xx_zip_grow_sink *sink;
    size_t required;
    size_t capacity;
    uint8_t *new_data;

    if (!device || !device->priv || (!buffer && size != 0U)) {
        return -1;
    }
    sink = (xx_zip_grow_sink *)device->priv;
    if (size > SIZE_MAX - sink->size) {
        return -1;
    }
    required = sink->size + size;
    if (required > sink->capacity) {
        capacity = sink->capacity ? sink->capacity : 4096U;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2U) {
                capacity = required;
                break;
            }
            capacity *= 2U;
        }
        new_data = (uint8_t *)xx_mem_realloc(sink->data, capacity);
        if (!new_data) {
            return -1;
        }
        sink->data = new_data;
        sink->capacity = capacity;
    }
    if (size > 0U) {
        xx_mem_copy(sink->data + sink->size, buffer, size);
        sink->size += size;
    }
    return (ssize_t)size;
}

static int64_t xx_zip_grow_sink_size(xx_io_device *device) {
    const xx_zip_grow_sink *sink;
    if (!device || !device->priv) {
        return -1;
    }
    sink = (const xx_zip_grow_sink *)device->priv;
#if SIZE_MAX > INT64_MAX
    if (sink->size > (size_t)INT64_MAX) {
        return -1;
    }
#endif
    return (int64_t)sink->size;
}

static void xx_zip_grow_sink_init(xx_io_device *device,
                                  xx_zip_grow_sink *sink) {
    xx_mem_zero(device, sizeof(*device));
    xx_mem_zero(sink, sizeof(*sink));
    device->write = xx_zip_grow_sink_write;
    device->total_size = xx_zip_grow_sink_size;
    device->get_total_size = xx_zip_grow_sink_size;
    device->size = xx_zip_grow_sink_size;
    device->priv = sink;
}

static void xx_zip_grow_sink_cleanup(xx_zip_grow_sink *sink) {
    if (!sink) {
        return;
    }
    if (sink->data) {
        xx_mem_zero(sink->data, sink->capacity);
        xx_mem_free(sink->data);
    }
    xx_mem_zero(sink, sizeof(*sink));
}

/*
 * ZIP method 33 is an XXFCLIB extension. Its entry data consists of this
 * five-byte header followed by a complete raw LZMA2 stream (including its
 * terminating zero control byte):
 *
 *   09 04 01 00 <one-byte LZMA2 property>
 */
static bool xx_zip_pack_lzma2_source_framed(
    xx_io_device *source, const char *source_path,
    int64_t *out_uncompressed_size, int64_t *out_compressed_size,
    uint32_t *out_crc32, xx_io_device *destination,
    xx_zip_grow_sink *sink, int level, xx_pd_struct *progress) {
    uint8_t header[XX_ZIP_LZMA2_HEADER_SIZE] = {
        0x09U, 0x04U, (uint8_t)XX_LZMA2_PROPS_SIZE, 0x00U, 0x00U
    };
    uint8_t props2 = 0U;
    int64_t raw_compressed_size = 0;

    if (!out_uncompressed_size || !out_compressed_size || !out_crc32 ||
        !destination || !sink) {
        return false;
    }
    *out_uncompressed_size = 0;
    *out_compressed_size = 0;
    *out_crc32 = 0U;

    if (!xx_zip_write_exact(destination, header, sizeof(header)) ||
        !xx_lzma2_pack_source(source, source_path, out_uncompressed_size,
                              &raw_compressed_size, out_crc32, destination,
                              level, &props2, progress) ||
        raw_compressed_size < 1 ||
        (uint64_t)raw_compressed_size >
            (uint64_t)(INT64_MAX - (int64_t)sizeof(header)) ||
        (uint64_t)raw_compressed_size >
            (uint64_t)(SIZE_MAX - sizeof(header)) ||
        props2 > XX_ZIP_LZMA2_MAX_PROP ||
        sink->size != (size_t)raw_compressed_size + sizeof(header) ||
        !sink->data || sink->data[sink->size - 1U] != 0U) {
        return false;
    }

    sink->data[4] = props2;
    *out_compressed_size = raw_compressed_size + (int64_t)sizeof(header);
    return true;
}

/* Encryption headers and AES salts must not use the C library PRNG. */
static bool xx_zip_secure_random(uint8_t *output, size_t size) {
    return xx_io_platform_secure_random(output, size);
}

static uint8_t xx_zip_encryption_aes_strength(uint32_t encryption_method) {
    switch (encryption_method) {
        case XX_ZIP_ENCRYPTION_AES_128:
            return XX_WINZIP_AES_STRENGTH_128;
        case XX_ZIP_ENCRYPTION_AES_192:
            return XX_WINZIP_AES_STRENGTH_192;
        case XX_ZIP_ENCRYPTION_AES_256:
            return XX_WINZIP_AES_STRENGTH_256;
        default:
            return 0U;
    }
}

static void xx_zip_build_aes_extra(uint8_t extra[XX_ZIP_AES_EXTRA_FIELD_SIZE],
                                   uint8_t strength,
                                   uint16_t compression_method) {
    extra[0] = 0x01U;
    extra[1] = 0x99U;
    extra[2] = 0x07U;
    extra[3] = 0x00U;
    extra[4] = (uint8_t)XX_ZIP_AES_VENDOR_VERSION_AE2;
    extra[5] = 0x00U;
    extra[6] = (uint8_t)'A';
    extra[7] = (uint8_t)'E';
    extra[8] = strength;
    extra[9] = (uint8_t)compression_method;
    extra[10] = (uint8_t)(compression_method >> 8U);
}



typedef struct xx_zip_cd_entry_record {
    char *filename;
    uint16_t filename_length;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
    uint16_t compression_method;
    uint16_t flags;
    uint16_t version_needed;
    uint16_t extra_field_length;
    uint8_t extra_field[XX_ZIP_AES_EXTRA_FIELD_SIZE];
    uint16_t last_mod_time;
    uint16_t last_mod_date;
    uint32_t external_attrs;
} xx_zip_cd_entry_record;

typedef struct xx_zip_write_internal_state {
    xx_zip_cd_entry_record *entries;
    size_t count;
    size_t capacity;
    int64_t current_offset;
    char comment[256];
} xx_zip_write_internal_state;

static void xx_zip_write_internal_state_free(void *ptr) {
    xx_zip_write_internal_state *wstate = (xx_zip_write_internal_state *)ptr;
    if (!wstate) return;
    if (wstate->entries) {
        for (size_t i = 0; i < wstate->count; ++i) {
            if (wstate->entries[i].filename) {
                xx_str_free(wstate->entries[i].filename);
            }
        }
        xx_mem_free(wstate->entries);
    }
    xx_mem_free(wstate);
}

xx_archive_write_state *xx_zip_create_archive_records_writing(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    (void)pd;
    if (!self || !self->device) {
        return NULL;
    }
    xx_archive_write_state *state = (xx_archive_write_state *)xx_mem_alloc(sizeof(xx_archive_write_state));
    if (!state) {
        return NULL;
    }
    xx_archive_write_state_init(state, self);

    if (options) {
        for (size_t i = 0; i < xx_list_size(options); ++i) {
            const xx_meta *m = (const xx_meta *)xx_list_at(options, i);
            if (m) {
                xx_meta copy;
                xx_meta_init(&copy, m->meta_id);
                xx_var_copy(&copy.var, &m->var);
                xx_list_append(&state->options, &copy);
            }
        }
    }

    xx_zip_write_internal_state *wstate = (xx_zip_write_internal_state *)xx_mem_alloc(sizeof(xx_zip_write_internal_state));
    if (!wstate) {
        xx_archive_write_state_free(state);
        return NULL;
    }
    xx_mem_zero(wstate, sizeof(xx_zip_write_internal_state));
    wstate->current_offset = 0;

    const xx_var *comment_var = xx_zip_find_option(&state->options, XX_META_ID_COMMENT);
    if (comment_var) {
        const char *c_str = xx_var_get_str(comment_var);
        if (c_str) {
            xx_rt_strncpy(wstate->comment, c_str, sizeof(wstate->comment) - 1);
        }
    }

    state->internal_state = wstate;
    state->free_internal = xx_zip_write_internal_state_free;
    state->current_index = 0;
    state->total_records = 0;
    return state;
}

bool xx_zip_pack_archive_record(Abstractformat *self, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev, xx_pd_struct *pd) {
    if (!self || !self->device || !state || !state->internal_state || !record) {
        return false;
    }
    xx_zip_write_internal_state *wstate = (xx_zip_write_internal_state *)state->internal_state;

    const char *orig_name = xx_archive_record_get_original_name(record);
    const wchar_t *orig_name_w = xx_archive_record_get_original_name_w(record);
    char *name_utf8 = NULL;
    if (orig_name && orig_name[0]) {
        name_utf8 = xx_str_dup(orig_name);
    } else if (orig_name_w && orig_name_w[0]) {
        name_utf8 = xx_str_unicode_to_utf8(orig_name_w);
    } else {
        name_utf8 = xx_str_dup("unnamed_file");
    }
    if (!name_utf8) {
        return false;
    }

    for (size_t i = 0; name_utf8[i]; ++i) {
        if (name_utf8[i] == '\\') {
            name_utf8[i] = '/';
        }
    }

    bool is_folder = xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER, false);
    size_t name_len = xx_rt_strlen(name_utf8);
    if (name_len > 0 && name_utf8[name_len - 1] == '/') {
        is_folder = true;
    } else if (is_folder) {
        char *with_slash = xx_str_concat(name_utf8, "/");
        xx_str_free(name_utf8);
        name_utf8 = with_slash;
        if (!name_utf8) {
            return false;
        }
        name_len = xx_rt_strlen(name_utf8);
    }

    if (name_len > UINT16_MAX || wstate->current_offset < 0 ||
        (uint64_t)wstate->current_offset > UINT32_MAX) {
        xx_str_free(name_utf8);
        return false;
    }

    uint16_t mod_time = (uint16_t)xx_archive_record_get_meta_u64(record, XX_META_ID_LAST_MOD_TIME, 0x6000);
    uint16_t mod_date = (uint16_t)xx_archive_record_get_meta_u64(record, XX_META_ID_LAST_MOD_DATE, 0x5D32);
    uint32_t ext_attrs = (uint32_t)xx_archive_record_get_meta_u64(record, XX_META_ID_EXTERNAL_ATTRS, 0);
    if (ext_attrs == 0) {
        ext_attrs = (uint32_t)xx_archive_record_get_meta_u64(record, XX_META_ID_ATTRIBUTES, is_folder ? 0x10 : 0x20);
    }

    uint16_t method = 8; /* Default for files is Deflate */
    const xx_var *opt_m = xx_zip_find_option(&state->options, XX_META_ID_COMPRESSION_METHOD);
    if (opt_m) {
        method = (uint16_t)xx_var_get_u64(opt_m);
    }
    const xx_var *rec_m = xx_archive_record_find_meta(record, XX_META_ID_COMPRESSION_METHOD);
    if (rec_m) {
        method = (uint16_t)xx_var_get_u64(rec_m);
    }
    if (is_folder) {
        method = 0;
    }

    int level = (method == 93U) ? XX_ZSTD_LEVEL_DEFAULT
                : ((method == 33U) ? XX_LZMA_LEVEL_DEFAULT
                                   : XX_DEFLATE_LEVEL_DEFAULT);
    const xx_var *opt_lvl = xx_zip_find_option(&state->options, XX_META_ID_COMPRESSION_LEVEL);
    if (opt_lvl) {
        level = (int)xx_var_get_i64(opt_lvl);
    }
    const xx_var *rec_lvl = xx_archive_record_find_meta(record, XX_META_ID_COMPRESSION_LEVEL);
    if (rec_lvl) {
        level = (int)xx_var_get_i64(rec_lvl);
    }

    uint32_t encryption_method = XX_ZIP_ENCRYPTION_NONE;
    bool has_encryption_selector = false;
    const xx_var *opt_enc = xx_zip_find_option(
        &state->options, XX_META_ID_ENCRYPTION_METHOD);
    if (opt_enc) {
        encryption_method = (uint32_t)xx_var_get_u64(opt_enc);
        has_encryption_selector = true;
    }
    const xx_var *rec_enc = xx_archive_record_find_meta(
        record, XX_META_ID_ENCRYPTION_METHOD);
    if (rec_enc) {
        encryption_method = (uint32_t)xx_var_get_u64(rec_enc);
        has_encryption_selector = true;
    }
    if (!has_encryption_selector &&
        xx_archive_record_get_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                        false)) {
        encryption_method = XX_ZIP_ENCRYPTION_ZIPCRYPTO;
    }
    if (is_folder) {
        encryption_method = XX_ZIP_ENCRYPTION_NONE;
    }
    if (encryption_method > XX_ZIP_ENCRYPTION_AES_256) {
        xx_str_free(name_utf8);
        return false;
    }

    uint32_t crc = 0;
    uint32_t uncomp_size = 0;
    uint32_t comp_size = 0;
    uint32_t local_header_offset = (uint32_t)wstate->current_offset;
    uint16_t stored_method = method;
    uint16_t stored_flags = 0x0800U; /* UTF-8 */
    uint16_t version_needed = (method == 33U || method == 93U) ? 63U
                               : ((method == 9U) ? 21U : 20U);
    uint8_t stored_extra[XX_ZIP_AES_EXTRA_FIELD_SIZE];
    uint16_t stored_extra_size = 0U;
    xx_mem_zero(stored_extra, sizeof(stored_extra));

    if (encryption_method != XX_ZIP_ENCRYPTION_NONE) {
        xx_zip_password password;
        xx_zip_grow_sink compressed;
        xx_io_device compressed_device;
        uint8_t random_data[16];
        uint8_t *envelope = NULL;
        size_t envelope_capacity = 0U;
        size_t envelope_size = 0U;
        int64_t staged_uncomp_size = 0;
        int64_t staged_comp_size = 0;
        uint32_t staged_crc = 0U;
        uint8_t aes_strength = xx_zip_encryption_aes_strength(encryption_method);
        bool packed = false;

        xx_mem_zero(&password, sizeof(password));
        xx_mem_zero(random_data, sizeof(random_data));
        xx_zip_grow_sink_init(&compressed_device, &compressed);

        if (!xx_zip_get_password(self, &state->options, &password)) {
            goto encrypted_cleanup;
        }
        if (method == 0U) {
            if (!xx_store_prepare_source(source_dev, name_utf8,
                                         &staged_uncomp_size, &staged_crc, pd) ||
                !xx_store_pack_source(source_dev, name_utf8,
                                      staged_uncomp_size,
                                      &compressed_device, pd)) {
                goto encrypted_cleanup;
            }
            staged_comp_size = (int64_t)compressed.size;
        } else if (method == 8U || method == 9U) {
            if (!xx_deflate_pack_source(source_dev, name_utf8,
                                        &staged_uncomp_size,
                                        &staged_comp_size, &staged_crc,
                                        &compressed_device, level,
                                        method == 9U, pd) ||
                staged_comp_size < 0 ||
                (uint64_t)staged_comp_size != (uint64_t)compressed.size) {
                goto encrypted_cleanup;
            }
        } else if (method == 33U) {
            if (!xx_zip_pack_lzma2_source_framed(
                    source_dev, name_utf8, &staged_uncomp_size,
                    &staged_comp_size, &staged_crc, &compressed_device,
                    &compressed, level, pd) ||
                staged_comp_size < 0 ||
                (uint64_t)staged_comp_size != (uint64_t)compressed.size) {
                goto encrypted_cleanup;
            }
        } else if (method == 93U) {
            if (!xx_zstd_pack_source(source_dev, name_utf8,
                                     &staged_uncomp_size,
                                     &staged_comp_size, &staged_crc,
                                     &compressed_device, level, pd) ||
                staged_comp_size < 0 ||
                (uint64_t)staged_comp_size != (uint64_t)compressed.size) {
                goto encrypted_cleanup;
            }
        } else {
            goto encrypted_cleanup;
        }

        if (staged_uncomp_size < 0 ||
            (uint64_t)staged_uncomp_size > UINT32_MAX ||
            compressed.size > UINT32_MAX) {
            goto encrypted_cleanup;
        }
        crc = staged_crc;
        uncomp_size = (uint32_t)staged_uncomp_size;

        if (encryption_method == XX_ZIP_ENCRYPTION_ZIPCRYPTO) {
            if (compressed.size > SIZE_MAX - XX_ZIPCRYPTO_HEADER_SIZE) {
                goto encrypted_cleanup;
            }
            envelope_capacity = compressed.size + XX_ZIPCRYPTO_HEADER_SIZE;
            envelope = (uint8_t *)xx_mem_alloc(
                envelope_capacity ? envelope_capacity : 1U);
            if (!envelope ||
                !xx_zip_secure_random(random_data,
                                      XX_ZIPCRYPTO_RANDOM_HEADER_SIZE) ||
                !xx_zipcrypto_encrypt_envelope_progress(
                    compressed.data, compressed.size,
                    password.bytes, password.size, crc, mod_time, false,
                    random_data, envelope, envelope_capacity,
                    &envelope_size, pd)) {
                goto encrypted_cleanup;
            }
        } else {
            size_t salt_size = xx_winzip_aes_salt_size(aes_strength);
            size_t overhead = salt_size +
                              XX_WINZIP_AES_PASSWORD_VERIFIER_SIZE +
                              XX_WINZIP_AES_AUTH_CODE_SIZE;
            if (salt_size == 0U || compressed.size > SIZE_MAX - overhead) {
                goto encrypted_cleanup;
            }
            envelope_capacity = compressed.size + overhead;
            envelope = (uint8_t *)xx_mem_alloc(
                envelope_capacity ? envelope_capacity : 1U);
            if (!envelope || !xx_zip_secure_random(random_data, salt_size) ||
                !xx_winzip_aes_encrypt_envelope_progress(
                    compressed.data, compressed.size,
                    password.bytes, password.size, aes_strength,
                    random_data, salt_size, envelope, envelope_capacity,
                    &envelope_size, pd)) {
                goto encrypted_cleanup;
            }
            stored_method = 99U;
            stored_extra_size = XX_ZIP_AES_EXTRA_FIELD_SIZE;
            xx_zip_build_aes_extra(stored_extra, aes_strength, method);
        }

        if (envelope_size != envelope_capacity ||
            envelope_size > UINT32_MAX ||
            name_len > UINT16_MAX ||
            wstate->current_offset < 0 ||
            (uint64_t)wstate->current_offset > UINT32_MAX) {
            goto encrypted_cleanup;
        }
        comp_size = (uint32_t)envelope_size;
        stored_flags |= 0x0001U;

        {
            xx_zip_local_header_t lh;
            uint64_t record_size = (uint64_t)sizeof(lh) + name_len +
                                   stored_extra_size + envelope_size;
            xx_mem_zero(&lh, sizeof(lh));
            if (record_size > UINT32_MAX - (uint64_t)local_header_offset) {
                goto encrypted_cleanup;
            }
            lh.signature = XX_ZIP_LOCAL_FILE_HEADER_SIGNATURE;
            lh.version_needed = version_needed;
            lh.flags = stored_flags;
            lh.compression_method = stored_method;
            lh.last_mod_time = mod_time;
            lh.last_mod_date = mod_date;
            /* AE-2 deliberately stores zero instead of the plaintext CRC. */
            lh.crc32 = aes_strength ? 0U : crc;
            lh.compressed_size = comp_size;
            lh.uncompressed_size = uncomp_size;
            lh.filename_length = (uint16_t)name_len;
            lh.extra_field_length = stored_extra_size;

            if (xx_io_seek64(self->device, local_header_offset,
                           SEEK_SET) != 0 ||
                !xx_zip_write_exact(self->device, &lh, sizeof(lh)) ||
                !xx_zip_write_exact(self->device, name_utf8, name_len) ||
                !xx_zip_write_exact(self->device, stored_extra,
                                    stored_extra_size) ||
                !xx_zip_write_exact(self->device, envelope,
                                    envelope_size)) {
                goto encrypted_cleanup;
            }
            wstate->current_offset += (int64_t)record_size;
        }

        if (aes_strength) {
            crc = 0U;
        }
        packed = true;

encrypted_cleanup:
        if (envelope) {
            xx_mem_free(envelope);
        }
        xx_mem_zero(random_data, sizeof(random_data));
        xx_zip_grow_sink_cleanup(&compressed);
        xx_zip_cleanup_password(&password);
        if (!packed) {
            xx_str_free(name_utf8);
            return false;
        }
    } else if (method == 0) {
        if (!is_folder) {
            int64_t src_size = 0;
            if (xx_store_prepare_source(source_dev, name_utf8, &src_size, &crc, pd)) {
                uncomp_size = (uint32_t)src_size;
                comp_size = (uint32_t)src_size;
            }
        }

        xx_zip_local_header_t lh;
        xx_mem_zero(&lh, sizeof(lh));
        lh.signature = XX_ZIP_LOCAL_FILE_HEADER_SIGNATURE;
        lh.version_needed = 20;
        lh.flags = 0x0800; /* UTF-8 */
        lh.compression_method = 0; /* STORE */
        lh.last_mod_time = mod_time;
        lh.last_mod_date = mod_date;
        lh.crc32 = crc;
        lh.compressed_size = comp_size;
        lh.uncompressed_size = uncomp_size;
        lh.filename_length = (uint16_t)name_len;
        lh.extra_field_length = 0;

        xx_io_seek64(self->device, local_header_offset, SEEK_SET);
        xx_io_write(self->device, &lh, sizeof(lh));
        xx_io_write(self->device, name_utf8, name_len);

        if (!is_folder && comp_size > 0) {
            if (!xx_store_pack_source(source_dev, name_utf8, (int64_t)comp_size, self->device, pd)) {
                xx_str_free(name_utf8);
                return false;
            }
        }

        wstate->current_offset += sizeof(lh) + name_len + comp_size;
    } else if (method == 33U || method == 93U) {
        xx_zip_grow_sink compressed;
        xx_io_device compressed_device;
        int64_t staged_uncomp_size = 0;
        int64_t staged_comp_size = 0;
        uint32_t staged_crc = 0U;
        bool packed = false;

        xx_zip_grow_sink_init(&compressed_device, &compressed);
        do {
            xx_zip_local_header_t lh;
            uint64_t record_size;
            uint64_t record_end;

            bool compression_ok;

            if (method == 33U) {
                compression_ok = xx_zip_pack_lzma2_source_framed(
                    source_dev, name_utf8, &staged_uncomp_size,
                    &staged_comp_size, &staged_crc, &compressed_device,
                    &compressed, level, pd);
            } else {
                compression_ok = xx_zstd_pack_source(
                    source_dev, name_utf8, &staged_uncomp_size,
                    &staged_comp_size, &staged_crc, &compressed_device,
                    level, pd);
            }
            if (!compression_ok ||
                 staged_uncomp_size < 0 || staged_comp_size < 0 ||
                (uint64_t)staged_uncomp_size > UINT32_MAX ||
                compressed.size > UINT32_MAX ||
                (uint64_t)staged_comp_size !=
                    (uint64_t)compressed.size) {
                break;
            }

            record_size = (uint64_t)sizeof(lh) + (uint64_t)name_len +
                          (uint64_t)compressed.size;
            if (record_size >
                    (uint64_t)UINT32_MAX - (uint64_t)local_header_offset) {
                break;
            }
            record_end = (uint64_t)local_header_offset + record_size;

            crc = staged_crc;
            uncomp_size = (uint32_t)staged_uncomp_size;
            comp_size = (uint32_t)compressed.size;

            xx_mem_zero(&lh, sizeof(lh));
            lh.signature = XX_ZIP_LOCAL_FILE_HEADER_SIGNATURE;
            lh.version_needed = version_needed;
            lh.flags = stored_flags;
            lh.compression_method = stored_method;
            lh.last_mod_time = mod_time;
            lh.last_mod_date = mod_date;
            lh.crc32 = crc;
            lh.compressed_size = comp_size;
            lh.uncompressed_size = uncomp_size;
            lh.filename_length = (uint16_t)name_len;
            lh.extra_field_length = 0U;

            if (xx_io_seek64(self->device, local_header_offset,
                           SEEK_SET) != 0 ||
                !xx_zip_write_exact(self->device, &lh, sizeof(lh)) ||
                !xx_zip_write_exact(self->device, name_utf8, name_len) ||
                !xx_zip_write_exact(self->device, compressed.data,
                                    compressed.size)) {
                break;
            }

            wstate->current_offset = (int64_t)record_end;
            packed = true;
        } while (false);

        xx_zip_grow_sink_cleanup(&compressed);
        if (!packed) {
            xx_str_free(name_utf8);
            return false;
        }
    } else if (method == 8 || method == 9) {
        xx_zip_local_header_t lh;
        xx_mem_zero(&lh, sizeof(lh));
        lh.signature = XX_ZIP_LOCAL_FILE_HEADER_SIGNATURE;
        lh.version_needed = version_needed;
        lh.flags = 0x0800; /* UTF-8 */
        lh.compression_method = method;
        lh.last_mod_time = mod_time;
        lh.last_mod_date = mod_date;
        lh.crc32 = 0;
        lh.compressed_size = 0;
        lh.uncompressed_size = 0;
        lh.filename_length = (uint16_t)name_len;
        lh.extra_field_length = 0;

        xx_io_seek64(self->device, local_header_offset, SEEK_SET);
        xx_io_write(self->device, &lh, sizeof(lh));
        xx_io_write(self->device, name_utf8, name_len);

        int64_t data_start = (int64_t)(local_header_offset + sizeof(lh) + name_len);
        int64_t out_uncomp = 0;
        int64_t out_comp = 0;
        uint32_t comp_crc = 0;

        if (!is_folder) {
            bool compression_ok = xx_deflate_pack_source(
                source_dev, name_utf8, &out_uncomp, &out_comp,
                &comp_crc, self->device, level, method == 9U, pd);
            if (!compression_ok || out_uncomp < 0 || out_comp < 0 ||
                (uint64_t)out_uncomp > UINT32_MAX ||
                (uint64_t)out_comp > UINT32_MAX) {
                xx_str_free(name_utf8);
                return false;
            }
        }

        crc = comp_crc;
        uncomp_size = (uint32_t)out_uncomp;
        comp_size = (uint32_t)out_comp;

        /* Rewrite local header with final CRC and sizes */
        lh.crc32 = crc;
        lh.compressed_size = comp_size;
        lh.uncompressed_size = uncomp_size;

        xx_io_seek64(self->device, local_header_offset, SEEK_SET);
        xx_io_write(self->device, &lh, sizeof(lh));

        int64_t data_end = data_start + comp_size;
        xx_io_seek64(self->device, data_end, SEEK_SET);
        wstate->current_offset = data_end;
    } else {
        xx_str_free(name_utf8);
        return false;
    }

    if (wstate->count >= wstate->capacity) {
        size_t new_cap = wstate->capacity == 0 ? 8 : wstate->capacity * 2;
        xx_zip_cd_entry_record *new_entries = (xx_zip_cd_entry_record *)xx_mem_realloc(
            wstate->entries, new_cap * sizeof(xx_zip_cd_entry_record));
        if (!new_entries) {
            xx_str_free(name_utf8);
            return false;
        }
        wstate->entries = new_entries;
        wstate->capacity = new_cap;
    }

    xx_zip_cd_entry_record *entry = &wstate->entries[wstate->count];
    entry->filename = name_utf8;
    entry->filename_length = (uint16_t)name_len;
    entry->crc32 = crc;
    entry->compressed_size = comp_size;
    entry->uncompressed_size = uncomp_size;
    entry->local_header_offset = local_header_offset;
    entry->compression_method = stored_method;
    entry->flags = stored_flags;
    entry->version_needed = version_needed;
    entry->extra_field_length = stored_extra_size;
    xx_mem_copy(entry->extra_field, stored_extra, stored_extra_size);
    entry->last_mod_time = mod_time;
    entry->last_mod_date = mod_date;
    entry->external_attrs = ext_attrs;

    wstate->count++;
    state->current_index = (int64_t)wstate->count;
    state->total_records = (int64_t)wstate->count;
    return true;
}

bool xx_zip_finalize_archive_records_writing(Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd) {
    (void)pd;
    if (!self || !self->device || !state || !state->internal_state) {
        return false;
    }
    xx_zip_write_internal_state *wstate = (xx_zip_write_internal_state *)state->internal_state;

    uint32_t cd_offset = (uint32_t)wstate->current_offset;
    xx_io_seek64(self->device, cd_offset, SEEK_SET);

    for (size_t i = 0; i < wstate->count; ++i) {
        const xx_zip_cd_entry_record *e = &wstate->entries[i];

        xx_zip_central_directory_header_t cdh;
        xx_mem_zero(&cdh, sizeof(cdh));
        cdh.signature = XX_ZIP_CENTRAL_DIRECTORY_HEADER_SIGNATURE;
        cdh.version_made_by = e->version_needed;
        cdh.version_needed = e->version_needed;
        cdh.flags = e->flags;
        cdh.compression_method = e->compression_method;
        cdh.last_mod_time = e->last_mod_time;
        cdh.last_mod_date = e->last_mod_date;
        cdh.crc32 = e->crc32;
        cdh.compressed_size = e->compressed_size;
        cdh.uncompressed_size = e->uncompressed_size;
        cdh.filename_length = e->filename_length;
        cdh.extra_field_length = e->extra_field_length;
        cdh.comment_length = 0;
        cdh.disk_number_start = 0;
        cdh.internal_attrs = 0;
        cdh.external_attrs = e->external_attrs;
        cdh.relative_offset_local_header = e->local_header_offset;

        if (!xx_zip_write_exact(self->device, &cdh, sizeof(cdh)) ||
            !xx_zip_write_exact(self->device, e->filename,
                                e->filename_length) ||
            !xx_zip_write_exact(self->device, e->extra_field,
                                e->extra_field_length)) {
            return false;
        }

        wstate->current_offset += sizeof(cdh) + e->filename_length +
                                  e->extra_field_length;
    }

    uint32_t cd_size = (uint32_t)(wstate->current_offset - cd_offset);

    uint16_t comment_len = (uint16_t)xx_rt_strlen(wstate->comment);
    xx_zip_eocd_t eocd;
    xx_mem_zero(&eocd, sizeof(eocd));
    eocd.signature = XX_ZIP_EOCD_SIGNATURE;
    eocd.disk_number = 0;
    eocd.cd_start_disk = 0;
    eocd.records_on_disk = (uint16_t)wstate->count;
    eocd.total_records = (uint16_t)wstate->count;
    eocd.cd_size = cd_size;
    eocd.cd_offset = cd_offset;
    eocd.comment_length = comment_len;

    xx_io_write(self->device, &eocd, sizeof(eocd));
    if (comment_len > 0) {
        xx_io_write(self->device, wstate->comment, comment_len);
    }
    wstate->current_offset += sizeof(eocd) + comment_len;

    self->format_size = wstate->current_offset;
    self->number_of_archive_records = wstate->count;
    self->is_valid = true;
    self->base_info_handled = true;

    return true;
}

void xx_zip_free_archive_records_writing(Abstractformat *self, xx_archive_write_state *state) {
    (void)self;
    if (!state) {
        return;
    }
    xx_archive_write_state_free(state);
}

bool xx_zip_is_zip64(const xx_zip *zip) {
    return zip ? zip->is_zip64 : false;
}

uint64_t xx_zip_get_number_of_records(const xx_zip *zip) {
    return zip ? zip->number_of_records : 0;
}

int64_t xx_zip_get_cd_offset(const xx_zip *zip) {
    return zip ? zip->cd_offset : -1;
}

int64_t xx_zip_get_cd_size(const xx_zip *zip) {
    return zip ? zip->cd_size : 0;
}

int64_t xx_zip_get_eocd_offset(const xx_zip *zip) {
    return zip ? zip->eocd_offset : -1;
}

const char *xx_zip_get_comment(const xx_zip *zip) {
    return zip ? zip->comment : "";
}

/* ========================================================================= */
/* --- Data Struct Id <-> String Conversion                              --- */
/* ========================================================================= */

typedef struct xx_zip_ds_name_entry {
    xx_zip_data_struct_id_t id;
    const char *name;
} xx_zip_ds_name_entry;

static const xx_zip_ds_name_entry _TABLE_XZip_DataStructNames[] = {
    {XX_ZIP_DS_UNKNOWN, "UNKNOWN"},
    {XX_ZIP_DS_LOCAL_FILE_HEADER, "LOCAL_FILE_HEADER"},
    {XX_ZIP_DS_DATA, "DATA"},
    {XX_ZIP_DS_DATA_DESCRIPTOR, "DATA_DESCRIPTOR"},
    {XX_ZIP_DS_CENTRAL_DIRECTORY_HEADER, "CENTRAL_DIRECTORY_HEADER"},
    {XX_ZIP_DS_END_OF_CENTRAL_DIRECTORY, "END_OF_CENTRAL_DIRECTORY"},
    {XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY, "ZIP64_END_OF_CENTRAL_DIRECTORY"},
    {XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR, "ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR"},
};

#define _XX_ZIP_DS_NAME_COUNT (sizeof(_TABLE_XZip_DataStructNames) / sizeof(_TABLE_XZip_DataStructNames[0]))

const char *xx_zip_data_struct_id_to_string(Abstractformat *self, uint32_t id) {
    (void)self;
    for (size_t i = 0; i < _XX_ZIP_DS_NAME_COUNT; ++i) {
        if ((uint32_t)_TABLE_XZip_DataStructNames[i].id == id) {
            return _TABLE_XZip_DataStructNames[i].name;
        }
    }
    return "UNKNOWN";
}

uint32_t xx_zip_data_struct_string_to_id(Abstractformat *self, const char *name) {
    (void)self;
    if (!name) {
        return (uint32_t)XX_ZIP_DS_UNKNOWN;
    }
    for (size_t i = 0; i < _XX_ZIP_DS_NAME_COUNT; ++i) {
        if (xx_str_cmp(name, _TABLE_XZip_DataStructNames[i].name) == 0) {
            return (uint32_t)_TABLE_XZip_DataStructNames[i].id;
        }
    }
    return (uint32_t)XX_ZIP_DS_UNKNOWN;
}

/* ========================================================================= */
/* --- Stream Data Structs Reading (headers/tables of the ZIP format)    --- */
/* ========================================================================= */

typedef struct xx_zip_ds_stream_state {
    xx_data_struct *list;
    size_t count;
} xx_zip_ds_stream_state;

static void xx_zip_ds_stream_state_free(void *ptr) {
    if (ptr) {
        xx_zip_ds_stream_state *dstate = (xx_zip_ds_stream_state *)ptr;
        if (dstate->list) {
            xx_mem_free(dstate->list);
        }
        xx_mem_free(dstate);
    }
}

static void xx_zip_ds_append(xx_zip_ds_stream_state *dstate, xx_zip_data_struct_id_t id,
                            int64_t offset, int64_t address, int64_t entry_size, int64_t total_size,
                            uint64_t count, xx_data_struct_type_t type) {
    xx_data_struct *item = &dstate->list[dstate->count++];
    item->id = (uint32_t)id;
    item->offset = offset;
    item->address = address;
    item->entry_size = entry_size;
    item->total_size = total_size;
    item->count = count;
    item->type = type;
}

xx_data_struct_state *xx_zip_create_data_structs_reading(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || !self->device || !xx_format_handle_split_format(self, pd)) {
        return NULL;
    }
    if (!self->base_info_handled) {
        if (!xx_format_handle_base_info(self, pd)) {
            return NULL;
        }
    }
    if (!self->is_valid) {
        return NULL;
    }

    xx_zip *zip = (xx_zip *)self;

    if (zip->number_of_records > ((SIZE_MAX / sizeof(xx_data_struct)) - 5) / 3) return NULL;
    xx_data_struct_state *state = (xx_data_struct_state *)xx_mem_alloc(sizeof(xx_data_struct_state));
    if (!state) {
        return NULL;
    }
    xx_data_struct_state_init(state, self);

    /* Max entries: (gap + LFH + DATA) per record, plus trailing gap, CD table, EOCD, ZIP64 EOCD/locator */
    size_t capacity = (size_t)(3 * zip->number_of_records) + 5;
    xx_zip_ds_stream_state *dstate = (xx_zip_ds_stream_state *)xx_mem_alloc(sizeof(xx_zip_ds_stream_state));
    if (!dstate) {
        xx_data_struct_state_free(state);
        return NULL;
    }
    dstate->count = 0;
    dstate->list = (xx_data_struct *)xx_mem_alloc(capacity * sizeof(xx_data_struct));
    if (!dstate->list) {
        xx_mem_free(dstate);
        xx_data_struct_state_free(state);
        return NULL;
    }

    bool is_mapped = self->is_mapped;
    int64_t dev_size = xx_io_total_size(self->device);

    /* Walk the central directory to recover each entry's local header + payload location,
     * emitting LOCAL_FILE_HEADER/DATA pairs in physical order, with RAW_DATA gaps between them */
    int64_t expected_next = self->base_address;
    int64_t cd_curr_offset = zip->cd_offset;

    for (uint64_t i = 0; i < zip->number_of_records && cd_curr_offset >= 0; ++i) {
        xx_archive_record rec;
        int64_t cd_entry_size = 0;
        xx_archive_record_init(&rec);
        if (!xx_zip_parse_cd_entry(zip, self->device, dev_size, cd_curr_offset, &rec, &cd_entry_size)) {
            xx_archive_record_cleanup(&rec);
            break;
        }
        cd_curr_offset += cd_entry_size;

        if (rec.header_offset >= 0) {
            if (rec.header_offset > expected_next) {
                int64_t gap_off = expected_next;
                int64_t gap_size = rec.header_offset - expected_next;
                int64_t gap_addr = is_mapped ? gap_off : -1;
                xx_zip_ds_append(dstate, (xx_zip_data_struct_id_t)XX_DATA_STRUCT_ID_RAW_DATA, gap_off, gap_addr,
                                gap_size, gap_size, 1, XX_DATA_STRUCT_TYPE_RAW_DATA);
            }

            int64_t lfh_addr = is_mapped ? rec.header_offset : -1;
            xx_zip_ds_append(dstate, XX_ZIP_DS_LOCAL_FILE_HEADER, rec.header_offset, lfh_addr,
                            rec.header_size, rec.header_size, 1, XX_DATA_STRUCT_TYPE_STRUCT);

            if (rec.data_offset >= 0) {
                int64_t data_addr = is_mapped ? rec.data_offset : -1;
                xx_zip_ds_append(dstate, XX_ZIP_DS_DATA, rec.data_offset, data_addr,
                                rec.compressed_size, rec.compressed_size, 1, XX_DATA_STRUCT_TYPE_RAW_DATA);
                expected_next = rec.data_offset + rec.compressed_size;
            } else {
                expected_next = rec.header_offset + rec.header_size;
            }
        }

        xx_archive_record_cleanup(&rec);
    }

    /* Gap between the last record's payload and the start of the central directory */
    if (zip->cd_offset >= 0 && expected_next < zip->cd_offset) {
        int64_t gap_off = expected_next;
        int64_t gap_size = zip->cd_offset - expected_next;
        int64_t gap_addr = is_mapped ? gap_off : -1;
        xx_zip_ds_append(dstate, (xx_zip_data_struct_id_t)XX_DATA_STRUCT_ID_RAW_DATA, gap_off, gap_addr,
                        gap_size, gap_size, 1, XX_DATA_STRUCT_TYPE_RAW_DATA);
    }

    if (zip->cd_offset >= 0 && zip->number_of_records > 0) {
        int64_t addr = is_mapped ? (self->base_address + zip->cd_offset) : -1;
        xx_zip_ds_append(dstate, XX_ZIP_DS_CENTRAL_DIRECTORY_HEADER, zip->cd_offset, addr, -1, zip->cd_size,
                        zip->number_of_records, XX_DATA_STRUCT_TYPE_STRUCT);
    }

    if (zip->is_zip64) {
        int64_t locator_offset = zip->eocd_offset - 20;
        if (zip->eocd_offset >= 0 && locator_offset >= 0) {
            uint32_t loc_sig = xx_io_get_u32(self->device, locator_offset, false);
            if (loc_sig == XX_ZIP_ZIP64_EOCD_LOCATOR_SIGNATURE) {
                int64_t addr = is_mapped ? (self->base_address + locator_offset) : -1;
                xx_zip_ds_append(dstate, XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR, locator_offset, addr,
                                sizeof(xx_zip_zip64_locator_t), sizeof(xx_zip_zip64_locator_t), 1, XX_DATA_STRUCT_TYPE_LOCATOR);

                int64_t zip64_eocd_offset = zip->zip64_eocd_offset;
                if (zip64_eocd_offset >= 0) {
                    int64_t addr64 = is_mapped ? (self->base_address + zip64_eocd_offset) : -1;
                    xx_zip_ds_append(dstate, XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY, zip64_eocd_offset,
                                    addr64, sizeof(xx_zip_zip64_eocd_t), zip->zip64_eocd_size, 1, XX_DATA_STRUCT_TYPE_STRUCT);
                }
            }
        }
    }

    if (zip->eocd_offset >= 0) {
        int64_t addr = is_mapped ? (self->base_address + zip->eocd_offset) : -1;
        int64_t eocd_total_size = (self->format_size >= zip->eocd_offset) ? (self->format_size - zip->eocd_offset) : (int64_t)sizeof(xx_zip_eocd_t);
        xx_zip_ds_append(dstate, XX_ZIP_DS_END_OF_CENTRAL_DIRECTORY, zip->eocd_offset, addr,
                        sizeof(xx_zip_eocd_t), eocd_total_size, 1, XX_DATA_STRUCT_TYPE_STRUCT);
    }

    state->internal_state = dstate;
    state->free_internal = xx_zip_ds_stream_state_free;
    state->total_structs = (int64_t)dstate->count;
    state->current_index = -1;
    state->has_struct = false;

    if (dstate->count > 0) {
        state->current_struct = dstate->list[0];
        state->has_struct = true;
        state->current_index = 0;
    }

    return state;
}

const xx_data_struct *xx_zip_get_current_data_struct(Abstractformat *self, xx_data_struct_state *state) {
    (void)self;
    if (!state || !state->has_struct) {
        return NULL;
    }
    return &state->current_struct;
}

bool xx_zip_data_struct_move_to_next(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd) {
    (void)self;
    (void)pd;
    if (!state || !state->internal_state) {
        return false;
    }
    xx_zip_ds_stream_state *dstate = (xx_zip_ds_stream_state *)state->internal_state;

    int64_t next_index = state->current_index + 1;
    if (next_index < 0 || (size_t)next_index >= dstate->count) {
        state->has_struct = false;
        return false;
    }

    state->current_struct = dstate->list[next_index];
    state->current_index = next_index;
    state->has_struct = true;
    return true;
}

void xx_zip_free_data_structs_reading(Abstractformat *self, xx_data_struct_state *state) {
    (void)self;
    xx_data_struct_state_free(state);
}

/* ========================================================================= */
/* --- Stream Data Struct Records Reading (named fields of a STRUCT)     --- */
/* ========================================================================= */

/* Fixed-layout ZIP struct field descriptors (uses global xx_data_struct_field_desc / xx_zip_field_desc) */
static const xx_data_struct_field_desc _TABLE_XZip_Fields_LocalFileHeader[] = {
    {L"signature", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"version_needed", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"flags", L"uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"compression_method", L"uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"last_mod_time", L"uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
    {L"last_mod_date", L"uint16", 12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
    {L"crc32", L"uint32", 14, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"compressed_size", L"uint32", 18, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"uncompressed_size", L"uint32", 22, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"filename_length", L"uint16", 26, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"extra_field_length", L"uint16", 28, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
};

static const xx_zip_field_desc _TABLE_XZip_Fields_CentralDirectoryHeader[] = {
    {L"signature", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"version_made_by", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"version_needed", L"uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"flags", L"uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"compression_method", L"uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"last_mod_time", L"uint16", 12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
    {L"last_mod_date", L"uint16", 14, 2, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
    {L"crc32", L"uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"compressed_size", L"uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"uncompressed_size", L"uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"filename_length", L"uint16", 28, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"extra_field_length", L"uint16", 30, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"comment_length", L"uint16", 32, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"disk_number_start", L"uint16", 34, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"internal_attrs", L"uint16", 36, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"external_attrs", L"uint32", 38, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"relative_offset_local_header", L"uint32", 42, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER},
};

static const xx_zip_field_desc _TABLE_XZip_Fields_Eocd[] = {
    {L"signature", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"disk_number", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"cd_start_disk", L"uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"records_on_disk", L"uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"total_records", L"uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"cd_size", L"uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"cd_offset", L"uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER},
    {L"comment_length", L"uint16", 20, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
};

static const xx_zip_field_desc _TABLE_XZip_Fields_Zip64Locator[] = {
    {L"signature", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"disk_with_zip64_eocd", L"uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"zip64_eocd_offset", L"uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER},
    {L"total_disks", L"uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
};

static const xx_zip_field_desc _TABLE_XZip_Fields_Zip64Eocd[] = {
    {L"signature", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"record_size", L"uint64", 4, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"version_made_by", L"uint16", 12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"version_needed", L"uint16", 14, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"disk_number", L"uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"cd_start_disk", L"uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"records_on_disk", L"uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"total_records", L"uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT},
    {L"cd_size", L"uint64", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"cd_offset", L"uint64", 48, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER},
};

#define _XX_ZIP_FIELDS_COUNT(table) (sizeof(table) / sizeof((table)[0]))

typedef struct xx_zip_record_stream_state {
    const xx_zip_field_desc *table;
    size_t count;
} xx_zip_record_stream_state;

static void xx_zip_record_stream_state_free(void *ptr) {
    if (ptr) {
        xx_mem_free(ptr);
    }
}

/* Reads the field's raw value from the device and populates name/type/value/display_value */
static bool xx_zip_populate_record(xx_io_device *device, const xx_data_struct *ds, const xx_data_struct_field_desc *field,
                                  xx_data_struct_record *rec) {
    return xx_data_struct_record_populate(rec, device, ds ? ds->offset : 0, field, false);
}

xx_data_struct_record_state *xx_zip_create_data_struct_records_reading(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd) {
    if (!self || !self->device || !ds || !xx_format_handle_split_format(self, pd)) {
        return NULL;
    }
    /* Only structured records have a known fixed field layout; raw data regions have none */
    if (ds->type == XX_DATA_STRUCT_TYPE_RAW_DATA) {
        return NULL;
    }

    const xx_zip_field_desc *table = NULL;
    size_t table_count = 0;

    switch ((xx_zip_data_struct_id_t)ds->id) {
        case XX_ZIP_DS_LOCAL_FILE_HEADER:
            table = _TABLE_XZip_Fields_LocalFileHeader;
            table_count = _XX_ZIP_FIELDS_COUNT(_TABLE_XZip_Fields_LocalFileHeader);
            break;
        case XX_ZIP_DS_CENTRAL_DIRECTORY_HEADER:
            table = _TABLE_XZip_Fields_CentralDirectoryHeader;
            table_count = _XX_ZIP_FIELDS_COUNT(_TABLE_XZip_Fields_CentralDirectoryHeader);
            break;
        case XX_ZIP_DS_END_OF_CENTRAL_DIRECTORY:
            table = _TABLE_XZip_Fields_Eocd;
            table_count = _XX_ZIP_FIELDS_COUNT(_TABLE_XZip_Fields_Eocd);
            break;
        case XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR:
            table = _TABLE_XZip_Fields_Zip64Locator;
            table_count = _XX_ZIP_FIELDS_COUNT(_TABLE_XZip_Fields_Zip64Locator);
            break;
        case XX_ZIP_DS_ZIP64_END_OF_CENTRAL_DIRECTORY:
            table = _TABLE_XZip_Fields_Zip64Eocd;
            table_count = _XX_ZIP_FIELDS_COUNT(_TABLE_XZip_Fields_Zip64Eocd);
            break;
        default:
            return NULL;
    }

    xx_data_struct_record_state *state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(xx_data_struct_record_state));
    if (!state) {
        return NULL;
    }
    xx_data_struct_record_state_init(state, self, ds);

    xx_zip_record_stream_state *rstate = (xx_zip_record_stream_state *)xx_mem_alloc(sizeof(xx_zip_record_stream_state));
    if (!rstate) {
        xx_data_struct_record_state_free(state);
        return NULL;
    }
    rstate->table = table;
    rstate->count = table_count;

    state->internal_state = rstate;
    state->free_internal = xx_zip_record_stream_state_free;
    state->total_records = (int64_t)table_count;
    state->current_index = -1;
    state->has_record = false;

    if (table_count > 0) {
        xx_zip_populate_record(self->device, ds, &table[0], &state->current_record);
        state->has_record = true;
        state->current_index = 0;
    }

    return state;
}

const xx_data_struct_record *xx_zip_get_current_data_struct_record(Abstractformat *self, xx_data_struct_record_state *state) {
    (void)self;
    if (!state || !state->has_record) {
        return NULL;
    }
    return &state->current_record;
}

bool xx_zip_data_struct_record_move_to_next(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd) {
    (void)pd;
    if (!self || !self->device || !state || !state->internal_state) {
        return false;
    }
    xx_zip_record_stream_state *rstate = (xx_zip_record_stream_state *)state->internal_state;

    int64_t next_index = state->current_index + 1;
    if (next_index < 0 || (size_t)next_index >= rstate->count) {
        state->has_record = false;
        return false;
    }

    xx_data_struct_record_cleanup(&state->current_record);
    xx_zip_populate_record(self->device, &state->parent_struct, &rstate->table[next_index], &state->current_record);
    state->current_index = next_index;
    state->has_record = true;

    return true;
}

void xx_zip_free_data_struct_records_reading(Abstractformat *self, xx_data_struct_record_state *state) {
    (void)self;
    xx_data_struct_record_state_free(state);
}
