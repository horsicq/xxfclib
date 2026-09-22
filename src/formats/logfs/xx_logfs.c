/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LogFS identification. The on-disk layout is documented in
 * include/xxfclib/formats/logfs/xx_logfs.h; this file parses the superblock,
 * verifies both of its CRCs and reports the geometry. It does not walk the
 * journal or the inode file, so nothing is enumerated - see the SCOPE note in
 * the header.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/logfs/xx_logfs.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved through the alias macro that
 * the enumerator will define. Once XX_FILE_TYPE_LOGFS lands the alias is
 * defined and this picks it up with no further change. */
#ifdef LOGFS
#define XX_LOGFS_FILE_TYPE XX_FILE_TYPE_LOGFS
#else
#define XX_LOGFS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Geometry sanity bounds. The kernel refuses anything outside these too:
 * a segment must hold at least one block, and logfs_set_blocksize() only
 * accepts a page-sized or smaller block. */
#define XX_LOGFS_MIN_BLOCK_SHIFT 9U
#define XX_LOGFS_MAX_BLOCK_SHIFT 16U
#define XX_LOGFS_MIN_SEGMENT_SHIFT 12U
#define XX_LOGFS_MAX_SEGMENT_SHIFT 30U

typedef struct xx_logfs_super_s {
    uint32_t segment_number;
    uint32_t erase_count;
    uint64_t global_erase_count;
    uint8_t segment_type;
    uint8_t segment_level;

    uint8_t ifile_levels;
    uint8_t iblock_levels;
    uint8_t data_levels;
    uint8_t segment_shift;
    uint8_t block_shift;
    uint8_t write_shift;
    uint64_t filesystem_size;
    uint32_t segment_size;
    uint32_t bad_seg_reserve;
    uint64_t feature_incompat;
    uint64_t feature_ro_compat;
    uint64_t feature_compat;
    uint64_t feature_flags;
    uint64_t root_reserve;
    uint64_t speed_reserve;
    uint32_t journal_seg[XX_LOGFS_JOURNAL_SEGS];
    uint64_t super_ofs[2];
} xx_logfs_super;

typedef struct xx_logfs_private_s {
    xx_logfs_super super;
    int64_t input_size;
    int64_t super_offset;
    int64_t mirror_offset;
    bool mirror_valid;
} xx_logfs_private;

static void xx_logfs_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_logfs_read_at(xx_io_device *device, int64_t offset, void *data,
                             size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_logfs_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* The kernel's logfs_crc32() is cpu_to_be32(crc32(~0, data, len)), and
 * crc32() there is crc32_le(), which neither pre- nor post-complements. The
 * library's xx_crc32_calc() is the zlib form - it complements both ends - so
 * one final complement converts between the two. */
uint32_t xx_logfs_crc32(const void *data, size_t size) {
    return ~xx_crc32_calc(0U, data, size);
}

/* ------------------------------------------------------------- parsing -- */

/* Decode and validate one 0x100-byte superblock image. Returns false unless
 * the magic, both CRCs and the geometry all hold. */
static bool xx_logfs_decode_super(const uint8_t *raw, xx_logfs_super *out) {
    uint32_t stored_sh_crc;
    uint32_t stored_ds_crc;
    size_t index;
    if (!raw || !out) return false;
    xx_mem_zero(out, sizeof(*out));

    if (xx_data_get_u64(raw, XX_LOGFS_DISK_SUPER_SIZE, 24U, true) !=
        XX_LOGFS_MAGIC) {
        return false;
    }
    /* Segment header CRC covers bytes 4..0x18 of the header. */
    stored_sh_crc = xx_data_get_u32(raw, XX_LOGFS_DISK_SUPER_SIZE, 0U, true);
    if (stored_sh_crc !=
        xx_logfs_crc32(raw + 4, XX_LOGFS_SEGMENT_HEADER_SIZE - 4U)) {
        return false;
    }
    /* Superblock CRC skips the segment header plus the 8-byte magic and the
     * 4-byte CRC field itself, i.e. it starts at 0x18 + 12 = 0x24. */
    stored_ds_crc = xx_data_get_u32(raw, XX_LOGFS_DISK_SUPER_SIZE, 32U, true);
    if (stored_ds_crc !=
        xx_logfs_crc32(raw + 36, XX_LOGFS_DISK_SUPER_SIZE - 36U)) {
        return false;
    }

    out->segment_type = raw[6];
    out->segment_level = raw[7];
    out->segment_number = xx_data_get_u32(raw, XX_LOGFS_DISK_SUPER_SIZE, 8U, true);
    out->erase_count = xx_data_get_u32(raw, XX_LOGFS_DISK_SUPER_SIZE, 12U, true);
    out->global_erase_count =
        xx_data_get_u64(raw, XX_LOGFS_DISK_SUPER_SIZE, 16U, true);

    out->ifile_levels = raw[36];
    out->iblock_levels = raw[37];
    out->data_levels = raw[38];
    out->segment_shift = raw[39];
    out->block_shift = raw[40];
    out->write_shift = raw[41];
    out->filesystem_size =
        xx_data_get_u64(raw, XX_LOGFS_DISK_SUPER_SIZE, 48U, true);
    out->segment_size = xx_data_get_u32(raw, XX_LOGFS_DISK_SUPER_SIZE, 56U, true);
    out->bad_seg_reserve =
        xx_data_get_u32(raw, XX_LOGFS_DISK_SUPER_SIZE, 60U, true);
    out->feature_incompat =
        xx_data_get_u64(raw, XX_LOGFS_DISK_SUPER_SIZE, 64U, true);
    out->feature_ro_compat =
        xx_data_get_u64(raw, XX_LOGFS_DISK_SUPER_SIZE, 72U, true);
    out->feature_compat =
        xx_data_get_u64(raw, XX_LOGFS_DISK_SUPER_SIZE, 80U, true);
    out->feature_flags =
        xx_data_get_u64(raw, XX_LOGFS_DISK_SUPER_SIZE, 88U, true);
    out->root_reserve = xx_data_get_u64(raw, XX_LOGFS_DISK_SUPER_SIZE, 96U, true);
    out->speed_reserve =
        xx_data_get_u64(raw, XX_LOGFS_DISK_SUPER_SIZE, 104U, true);
    for (index = 0U; index < XX_LOGFS_JOURNAL_SEGS; ++index) {
        out->journal_seg[index] = xx_data_get_u32(
            raw, XX_LOGFS_DISK_SUPER_SIZE, 112U + index * 4U, true);
    }
    out->super_ofs[0] =
        xx_data_get_u64(raw, XX_LOGFS_DISK_SUPER_SIZE, 176U, true);
    out->super_ofs[1] =
        xx_data_get_u64(raw, XX_LOGFS_DISK_SUPER_SIZE, 184U, true);

    /* The CRCs already make a false positive improbable, so these checks are
     * about refusing a volume this reader would then misreport rather than
     * about detection. */
    if (out->segment_type != XX_LOGFS_SEG_SUPER) return false;
    if (out->block_shift < XX_LOGFS_MIN_BLOCK_SHIFT ||
        out->block_shift > XX_LOGFS_MAX_BLOCK_SHIFT) return false;
    if (out->segment_shift < XX_LOGFS_MIN_SEGMENT_SHIFT ||
        out->segment_shift > XX_LOGFS_MAX_SEGMENT_SHIFT) return false;
    if (out->segment_shift <= out->block_shift) return false;
    if (out->write_shift > out->block_shift) return false;
    if (out->segment_size != (UINT32_C(1) << out->segment_shift)) return false;
    if (out->filesystem_size == 0U ||
        (out->filesystem_size & (out->segment_size - 1U)) != 0U) return false;
    if (out->filesystem_size > (uint64_t)INT64_MAX) return false;
    return true;
}

static bool xx_logfs_load_super(xx_io_device *device, int64_t offset,
                                int64_t total_size, xx_logfs_super *out) {
    uint8_t raw[XX_LOGFS_DISK_SUPER_SIZE];
    if (!xx_logfs_range_within(total_size, offset,
                               (int64_t)XX_LOGFS_DISK_SUPER_SIZE) ||
        !xx_logfs_read_at(device, offset, raw, sizeof(raw))) {
        return false;
    }
    return xx_logfs_decode_super(raw, out);
}

/* The trailing copy sits one 4 KiB page below the 4 KiB-aligned end of the
 * device, per fs/logfs/dev_bdev.c. Returns -1 when the device is too small
 * for that position to exist at or after base_address. */
static int64_t xx_logfs_mirror_offset(int64_t base_address, int64_t total_size) {
    int64_t span;
    int64_t position;
    if (base_address < 0 || total_size < base_address) return -1;
    span = total_size - base_address;
    position = (span & ~(int64_t)0xFFF) - 0x1000;
    if (position <= 0) return -1;
    if (position > INT64_MAX - base_address) return -1;
    return base_address + position;
}

static void xx_logfs_private_cleanup(xx_logfs_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->super_offset = -1;
    parsed->mirror_offset = -1;
}

static bool xx_logfs_parse(Abstractformat *self, xx_logfs_private *parsed,
                           xx_pd_struct *pd) {
    int64_t total_size;
    int64_t mirror;
    xx_logfs_super mirror_super;
    if (parsed) xx_logfs_private_cleanup(parsed);
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    total_size = xx_io_total_size(self->device);
    parsed->input_size = total_size;

    /* The primary copy is the first thing in the volume. */
    if (!xx_logfs_load_super(self->device, self->base_address, total_size,
                             &parsed->super)) {
        xx_logfs_private_cleanup(parsed);
        return false;
    }
    parsed->super_offset = self->base_address;

    /* The trailing copy is informational: a truncated image still identifies
     * as LogFS, it just loses the second opinion. */
    mirror = xx_logfs_mirror_offset(self->base_address, total_size);
    parsed->mirror_offset = mirror;
    if (mirror > self->base_address &&
        xx_logfs_load_super(self->device, mirror, total_size, &mirror_super)) {
        parsed->mirror_valid = true;
    }
    return true;
}

/* ---------------------------------------------------------- public API -- */

void xx_logfs_init(xx_logfs *logfs, xx_io_device *dev, int64_t base_address) {
    if (!logfs) return;
    xx_mem_zero(logfs, sizeof(*logfs));
    xx_format_init(&logfs->format, dev, base_address);
    logfs->format.endian = XX_ENDIAN_BIG;
    logfs->format.file_type = XX_LOGFS_FILE_TYPE;
    logfs->format.format_type = XX_TYPE_ARCHIVE;
    /* Identification only - see the SCOPE note in the header. No archive
     * callbacks are installed, so a caller is told there is nothing to walk
     * instead of receiving a fabricated listing. */
    logfs->format.is_archive = false;
    xx_format_set_mime_type(&logfs->format, "application/x-logfs-image");
    xx_format_set_extension(&logfs->format, "logfs");
    logfs->format.check_is_valid = xx_logfs_check_is_valid;
    logfs->format.handle_base_info = xx_logfs_handle_base_info;
    logfs->format.get_format_size = xx_logfs_get_format_size;
    logfs->format.destroy = xx_logfs_vtable_destroy;
    logfs->super_offset = -1;
    logfs->mirror_offset = -1;
}

xx_logfs *xx_logfs_create(xx_io_device *dev, int64_t base_address) {
    xx_logfs *logfs = (xx_logfs *)xx_mem_alloc(sizeof(*logfs));
    if (logfs) xx_logfs_init(logfs, dev, base_address);
    return logfs;
}

void xx_logfs_destroy(xx_logfs *logfs) {
    if (!logfs) return;
    if (logfs->internal) {
        xx_logfs_private_cleanup((xx_logfs_private *)logfs->internal);
        xx_mem_free(logfs->internal);
        logfs->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&logfs->format);
}

static void xx_logfs_vtable_destroy(Abstractformat *self) {
    xx_logfs_destroy((xx_logfs *)self);
}

void xx_logfs_free(xx_logfs *logfs) {
    if (!logfs) return;
    xx_logfs_destroy(logfs);
    xx_mem_free(logfs);
}

bool xx_logfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_logfs_private parsed;
    bool result = xx_logfs_parse(self, &parsed, pd);
    xx_logfs_private_cleanup(&parsed);
    return result;
}

bool xx_logfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_logfs_private *parsed;
    xx_logfs *logfs = (xx_logfs *)self;
    int64_t total_size;
    int64_t claimed;
    size_t index;
    if (!self || !logfs) return false;
    parsed = (xx_logfs_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_logfs_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (logfs->internal) {
        xx_logfs_private_cleanup((xx_logfs_private *)logfs->internal);
        xx_mem_free(logfs->internal);
    }
    logfs->internal = parsed;

    logfs->segment_number = parsed->super.segment_number;
    logfs->erase_count = parsed->super.erase_count;
    logfs->global_erase_count = parsed->super.global_erase_count;
    logfs->segment_type = parsed->super.segment_type;
    logfs->segment_level = parsed->super.segment_level;
    logfs->ifile_levels = parsed->super.ifile_levels;
    logfs->iblock_levels = parsed->super.iblock_levels;
    logfs->data_levels = parsed->super.data_levels;
    logfs->segment_shift = parsed->super.segment_shift;
    logfs->block_shift = parsed->super.block_shift;
    logfs->write_shift = parsed->super.write_shift;
    logfs->filesystem_size = parsed->super.filesystem_size;
    logfs->segment_size = parsed->super.segment_size;
    logfs->bad_seg_reserve = parsed->super.bad_seg_reserve;
    logfs->feature_incompat = parsed->super.feature_incompat;
    logfs->feature_ro_compat = parsed->super.feature_ro_compat;
    logfs->feature_compat = parsed->super.feature_compat;
    logfs->feature_flags = parsed->super.feature_flags;
    logfs->root_reserve = parsed->super.root_reserve;
    logfs->speed_reserve = parsed->super.speed_reserve;
    for (index = 0U; index < XX_LOGFS_JOURNAL_SEGS; ++index) {
        logfs->journal_seg[index] = parsed->super.journal_seg[index];
    }
    logfs->super_ofs[0] = parsed->super.super_ofs[0];
    logfs->super_ofs[1] = parsed->super.super_ofs[1];
    logfs->super_offset = parsed->super_offset;
    logfs->mirror_offset = parsed->mirror_offset;
    logfs->mirror_valid = parsed->mirror_valid;

    total_size = xx_io_total_size(self->device);
    /* ds_filesystem_size is the size the volume was made for. Trust it only
     * as far as the device actually reaches; a short image is still readable
     * as an identification, it simply has no overlay. */
    claimed = (int64_t)parsed->super.filesystem_size;
    if (total_size >= 0 && claimed <= total_size - self->base_address) {
        self->format_size = claimed;
        if (total_size > self->base_address + claimed) {
            self->overlay_offset = self->base_address + claimed;
            self->overlay_size = total_size - self->overlay_offset;
        } else {
            self->overlay_offset = -1;
            self->overlay_size = 0;
        }
    } else {
        self->format_size = total_size >= 0 ? total_size - self->base_address : -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 0U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_logfs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_logfs_get_filesystem_size(const xx_logfs *logfs) {
    return logfs ? logfs->filesystem_size : 0U;
}
uint32_t xx_logfs_get_segment_size(const xx_logfs *logfs) {
    return logfs ? logfs->segment_size : 0U;
}
uint32_t xx_logfs_get_block_size(const xx_logfs *logfs) {
    return (logfs && logfs->block_shift < 32U)
               ? (UINT32_C(1) << logfs->block_shift) : 0U;
}
uint32_t xx_logfs_get_write_size(const xx_logfs *logfs) {
    return (logfs && logfs->write_shift < 32U)
               ? (UINT32_C(1) << logfs->write_shift) : 0U;
}
int64_t xx_logfs_get_super_offset(const xx_logfs *logfs) {
    return logfs ? logfs->super_offset : -1;
}
bool xx_logfs_get_mirror_valid(const xx_logfs *logfs) {
    return logfs ? logfs->mirror_valid : false;
}
