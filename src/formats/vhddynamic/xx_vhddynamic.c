/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft Virtual Hard Disk (VHD; Connectix / Virtual PC): fixed, dynamic
 * and differencing images.  Everything in a VHD is BIG endian.
 *
 * Hard disk footer: the LAST 512 bytes of the file.  Writers older than
 * Virtual PC 2004 leave out the final (reserved, zero) byte and write 511.
 *   +0x00  "conectix"
 *   +0x08  u32  features; bit 1 is reserved-and-always-set
 *   +0x0c  u32  file format version, 0x00010000
 *   +0x10  u64  offset of the dynamic-disk header; all ones for a fixed disk
 *   +0x18  u32  creation time, seconds since 2000-01-01 00:00 UTC
 *   +0x30  u64  current disk size
 *   +0x3c  u32  disk type; 2 fixed, 3 dynamic, 4 differencing
 *   +0x40  u32  one's-complement checksum over the footer
 *   +0x44  16   unique id
 *
 * A FIXED image is the disk itself, current-size bytes from offset 0, and
 * then the footer.  Nothing else identifies it, so the footer must sit
 * exactly at current-size.
 *
 * A DYNAMIC or DIFFERENCING image starts with a copy of the footer; the
 * footer's data offset points at the dynamic-disk header (1024 bytes)
 *   +0x00  "cxsparse"
 *   +0x08  u64  0xffffffffffffffff (no next header)
 *   +0x10  u64  BAT offset
 *   +0x18  u32  header version, 0x00010000
 *   +0x1c  u32  max table entries   +0x20  u32  block size
 *   +0x24  u32  one's-complement checksum
 *   +0x28  16   parent unique id    +0x38  u32  parent time stamp
 *   +0x40  512  parent file name, UTF-16 big endian
 *   +0x240 8 x 24 parent locators
 * Each BAT entry is the 512-byte sector at which a block starts, or
 * 0xffffffff for an unallocated block.  A block opens with a sector
 * allocation bitmap rounded up to 512 bytes, MSB first; only the sectors
 * whose bit is set are stored in the block.
 *
 * The reader publishes one member, the guest disk ("disk.img", current-size
 * bytes).  In a dynamic disk a clear bit or an unallocated block reads as
 * zeros.  A differencing disk takes those sectors from its parent, which is
 * a separate file a single-device reader cannot open; like the qcow reader
 * with a backing file and the vdi reader with a differencing image, this
 * reader publishes the image's own sectors with zeros where the parent
 * would supply data, and names the parent in the record comment.
 *
 * The checks are ported from XArchive diskimages/xvirtualdiskarchive.cpp
 * (parseVHD; MIT licence, same author), widened to fixed and differencing
 * images, 511-byte footers, the 32-bit 0xFFFFFFFF data offset the
 * specification text gives for fixed disks, and a front footer copy that
 * may differ from the tail one in fields that do not describe the layout
 * (time stamp, saved-state flag).  U3 implements the dynamic variant as
 * archive/12 (class mfa, VMT 0x0049bef8).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vhddynamic/xx_vhddynamic.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef VHDDYNAMIC
#define XX_VHDDYNAMIC_FILE_TYPE XX_FILE_TYPE_VHDDYNAMIC
#else
#define XX_VHDDYNAMIC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define VHDDYNAMIC_FOOTER_SIZE 512U
#define VHDDYNAMIC_DYNHDR_SIZE 1024U
/* The format tops out at 2040 GiB; anything past 4 TiB is not a VHD. */
#define VHDDYNAMIC_MAX_DISK ((uint64_t)1U << 42U)
/* 4 M entries: 2040 GiB at 512 KiB blocks, the smallest block size any
 * known writer uses for a disk that large. */
#define VHDDYNAMIC_MAX_ENTRIES (UINT32_C(1) << 22U)
#define VHDDYNAMIC_MAX_BLOCK (UINT32_C(32) * 1024U * 1024U)
/* The table is read in slices of this many entries (16 KB). */
#define VHDDYNAMIC_BAT_SLICE 4096U
#define VHDDYNAMIC_TYPE_FIXED 2U
#define VHDDYNAMIC_TYPE_DYNAMIC 3U
#define VHDDYNAMIC_TYPE_DIFFERENCING 4U
#define VHDDYNAMIC_MEMBER_NAME "disk.img"
/* 256 UTF-16 units of parent name, up to 4 UTF-8 bytes each, plus text. */
#define VHDDYNAMIC_COMMENT_SIZE 1152U

typedef struct vhddynamic_info_s {
    int64_t base;
    int64_t size;          /* bytes from base through the end of the footer */
    int64_t footer_offset; /* relative to base */
    uint32_t footer_size;  /* 512, or 511 for pre-2004 Virtual PC */
    uint32_t disk_type;
    uint64_t disk_size;
    uint32_t timestamp;
    uint64_t header_offset;
    uint64_t bat_offset;
    uint32_t entries;
    uint32_t block_size;
    uint8_t dynamic[VHDDYNAMIC_DYNHDR_SIZE]; /* types 3 and 4 only */
} vhddynamic_info;

typedef struct vhddynamic_stream_s {
    vhddynamic_info info;
    size_t index; /* 0 while the single record is current */
} vhddynamic_stream;

static void vhddynamic_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static uint32_t vhddynamic_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t vhddynamic_be64(const uint8_t *b) {
    return ((uint64_t)vhddynamic_be32(b) << 32U) |
           (uint64_t)vhddynamic_be32(b + 4U);
}

static bool vhddynamic_read_at(xx_io_device *device, int64_t offset,
                               void *buffer, size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool vhddynamic_write_all(xx_io_device *device, const void *data,
                                 size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!data && size != 0U) return false;
    if (!device) return true; /* verify-only pass: nothing is materialized */
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Copy a run of source bytes straight through to the destination. */
static bool vhddynamic_copy_range(xx_io_device *source, int64_t offset,
                                  uint64_t size, xx_io_device *destination,
                                  xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!source || offset < 0) return false;
    if (!destination) return true;
    if (xx_io_seek64(source, offset, SEEK_SET) != 0) return false;
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < want) {
            ssize_t amount = xx_io_read(source, buffer + done, want - done);
            if (amount <= 0 || (size_t)amount > want - done) return false;
            done += (size_t)amount;
        }
        if (!vhddynamic_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Emit `size` zero bytes: unallocated blocks and sectors. */
static bool vhddynamic_write_zeros(xx_io_device *destination, uint64_t size,
                                   xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!destination) return true;
    xx_rt_memset(buffer, 0, sizeof(buffer));
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        if (!vhddynamic_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* One's-complement checksum over the structure with its own checksum field
 * treated as zero.  Together with the cookie it is what separates a real
 * VHD from a file whose last sector happens to start with "conectix". */
static bool vhddynamic_checksum_ok(const uint8_t *data, size_t size,
                                   size_t field) {
    uint32_t sum = 0U;
    size_t at;
    if (field + 4U > size) return false;
    for (at = 0U; at < size; ++at) {
        if (at >= field && at < field + 4U) continue;
        sum += data[at];
    }
    return vhddynamic_be32(data + field) == (uint32_t)~sum;
}

static bool vhddynamic_footer_ok(const uint8_t *footer) {
    uint32_t features = vhddynamic_be32(footer + 8U);
    return xx_rt_memcmp(footer, "conectix", 8U) == 0 &&
           (features & ~UINT32_C(3)) == 0U && (features & 2U) != 0U &&
           vhddynamic_be32(footer + 12U) == 0x00010000U &&
           vhddynamic_checksum_ok(footer, VHDDYNAMIC_FOOTER_SIZE, 64U);
}

static bool vhddynamic_power_of_two(uint64_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

/* Blocks needed to cover the disk; disk_size is known to be non-zero. */
static uint64_t vhddynamic_needed_blocks(const vhddynamic_info *info) {
    return (info->disk_size - 1U) / info->block_size + 1U;
}

/* Everything here is bounded: at most three reads (512, 512 and 1024
 * bytes) and no allocation.  This is also the detector's probe, and a file
 * whose size is not 0 or 511 modulo 512 costs no read at all. */
static bool vhddynamic_parse(Abstractformat *format, vhddynamic_info *info) {
    uint8_t footer[VHDDYNAMIC_FOOTER_SIZE];
    uint8_t front[VHDDYNAMIC_FOOTER_SIZE];
    uint8_t *dynamic;
    int64_t total, size;
    uint64_t data_offset, limit;

    if (!format || !format->device || !info || format->base_address < 0)
        return false;
    xx_rt_memset(info, 0, sizeof(*info));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    /* The smallest image is one sector of disk and a 511-byte footer. */
    if (size < (int64_t)(512U + 511U)) return false;
    if (size % 512 == 0)
        info->footer_size = VHDDYNAMIC_FOOTER_SIZE;
    else if (size % 512 == 511)
        info->footer_size = VHDDYNAMIC_FOOTER_SIZE - 1U;
    else
        return false;
    info->base = format->base_address;
    info->size = size;
    info->footer_offset = size - (int64_t)info->footer_size;
    /* A 511-byte footer is the 512-byte one without its last reserved (zero)
     * byte, so the zero padding keeps the checksum the same. */
    xx_rt_memset(footer, 0, sizeof(footer));
    if (!vhddynamic_read_at(format->device,
                            info->base + info->footer_offset, footer,
                            info->footer_size) ||
        !vhddynamic_footer_ok(footer))
        return false;

    info->disk_size = vhddynamic_be64(footer + 48U);
    info->disk_type = vhddynamic_be32(footer + 60U);
    info->timestamp = vhddynamic_be32(footer + 24U);
    data_offset = vhddynamic_be64(footer + 16U);
    if (info->disk_size == 0U || info->disk_size > VHDDYNAMIC_MAX_DISK ||
        (info->disk_size % 512U) != 0U)
        return false;

    if (info->disk_type == VHDDYNAMIC_TYPE_FIXED) {
        /* The specification says "0xFFFFFFFF"; every writer seen so far
         * stores all 64 bits set.  Accept both. */
        if (data_offset != UINT64_MAX && data_offset != UINT64_C(0xffffffff))
            return false;
        /* No header at the front: the disk fills the file up to the
         * footer, exactly. */
        return info->disk_size == (uint64_t)info->footer_offset;
    }
    if (info->disk_type != VHDDYNAMIC_TYPE_DYNAMIC &&
        info->disk_type != VHDDYNAMIC_TYPE_DIFFERENCING)
        return false;

    /* Dynamic structures all live between the front copy and the footer. */
    limit = (uint64_t)info->footer_offset;
    if (limit < VHDDYNAMIC_FOOTER_SIZE + VHDDYNAMIC_DYNHDR_SIZE) return false;
    /* The front copy must describe the same disk.  Time stamp and saved
     * state are allowed to differ; the layout fields are not. */
    if (!vhddynamic_read_at(format->device, info->base, front,
                            sizeof(front)) ||
        !vhddynamic_footer_ok(front) ||
        vhddynamic_be32(front + 60U) != info->disk_type ||
        vhddynamic_be64(front + 48U) != info->disk_size ||
        vhddynamic_be64(front + 16U) != data_offset)
        return false;

    info->header_offset = data_offset;
    if (data_offset < VHDDYNAMIC_FOOTER_SIZE || (data_offset % 512U) != 0U ||
        data_offset > limit || VHDDYNAMIC_DYNHDR_SIZE > limit - data_offset)
        return false;
    dynamic = info->dynamic;
    if (!vhddynamic_read_at(format->device,
                            info->base + (int64_t)data_offset, dynamic,
                            VHDDYNAMIC_DYNHDR_SIZE) ||
        xx_rt_memcmp(dynamic, "cxsparse", 8U) != 0 ||
        vhddynamic_be64(dynamic + 8U) != UINT64_MAX ||
        vhddynamic_be32(dynamic + 24U) != 0x00010000U ||
        !vhddynamic_checksum_ok(dynamic, VHDDYNAMIC_DYNHDR_SIZE, 36U))
        return false;

    info->bat_offset = vhddynamic_be64(dynamic + 16U);
    info->entries = vhddynamic_be32(dynamic + 28U);
    info->block_size = vhddynamic_be32(dynamic + 32U);
    if (!vhddynamic_power_of_two(info->block_size) ||
        info->block_size < 512U || info->block_size > VHDDYNAMIC_MAX_BLOCK ||
        info->entries == 0U || info->entries > VHDDYNAMIC_MAX_ENTRIES ||
        (info->bat_offset % 512U) != 0U ||
        info->bat_offset < VHDDYNAMIC_FOOTER_SIZE)
        return false;
    if ((uint64_t)info->entries < vhddynamic_needed_blocks(info)) return false;
    /* Only the entries that cover the disk are ever read, but the whole
     * table is declared and must fit before the footer. */
    if (info->bat_offset > limit ||
        (uint64_t)info->entries * 4U > limit - info->bat_offset)
        return false;
    return true;
}

/* Write (or, with no destination, walk) the guest disk. */
static bool vhddynamic_emit(Abstractformat *format,
                            const vhddynamic_info *info,
                            xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *table = NULL;
    uint8_t *bitmap = NULL;
    uint64_t needed, sectors, bitmap_bytes, index, produced = 0U;
    uint64_t slice_start = 0U, slice_count = 0U;
    uint64_t limit;
    bool result = false;

    if (!format || !format->device || !info) return false;
    if (info->disk_type == VHDDYNAMIC_TYPE_FIXED)
        return vhddynamic_copy_range(format->device, info->base,
                                     info->disk_size, destination, pd);

    limit = (uint64_t)info->footer_offset;
    needed = vhddynamic_needed_blocks(info);
    sectors = info->block_size / 512U;
    /* parse() capped blocks at 32 MB: at most 8 KB of bitmap. */
    bitmap_bytes = (((sectors + 7U) / 8U) + 511U) & ~(uint64_t)511U;
    if (needed == 0U || needed > info->entries) return false;
    table = (uint8_t *)xx_mem_alloc(VHDDYNAMIC_BAT_SLICE * 4U);
    bitmap = (uint8_t *)xx_mem_alloc((size_t)bitmap_bytes);
    if (!table || !bitmap) goto done;

    for (index = 0U; index < needed; ++index) {
        uint32_t entry;
        uint64_t left = info->disk_size - produced;
        uint64_t output = left < info->block_size ? left : info->block_size;
        uint64_t offset, count, sector;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (index - slice_start >= slice_count) {
            slice_start = index;
            slice_count = needed - index < VHDDYNAMIC_BAT_SLICE
                              ? needed - index
                              : VHDDYNAMIC_BAT_SLICE;
            if (!vhddynamic_read_at(format->device,
                                    info->base +
                                        (int64_t)(info->bat_offset +
                                                  index * 4U),
                                    table, (size_t)slice_count * 4U))
                goto done;
        }
        entry = vhddynamic_be32(table + (index - slice_start) * 4U);
        if (entry == 0xffffffffU) {
            if (!vhddynamic_write_zeros(destination, output, pd)) goto done;
            produced += output;
            continue;
        }
        /* The block, as far as the disk needs it, must lie between the
         * front footer copy and the tail footer. */
        offset = (uint64_t)entry * 512U;
        if (offset < VHDDYNAMIC_FOOTER_SIZE || offset > limit ||
            bitmap_bytes + output > limit - offset)
            goto done;
        if (!vhddynamic_read_at(format->device,
                                info->base + (int64_t)offset, bitmap,
                                (size_t)bitmap_bytes))
            goto done;
        /* Present and absent sectors come in runs; copy or zero-fill each
         * run in one go. */
        count = output / 512U;
        sector = 0U;
        while (sector < count) {
            bool present = (bitmap[sector / 8U] &
                            (uint8_t)(0x80U >> (sector % 8U))) != 0U;
            uint64_t end = sector + 1U;
            while (end < count &&
                   ((bitmap[end / 8U] & (uint8_t)(0x80U >> (end % 8U))) !=
                    0U) == present)
                ++end;
            if (present) {
                if (!vhddynamic_copy_range(
                        format->device,
                        info->base + (int64_t)(offset + bitmap_bytes +
                                               sector * 512U),
                        (end - sector) * 512U, destination, pd))
                    goto done;
            } else if (!vhddynamic_write_zeros(destination,
                                               (end - sector) * 512U, pd)) {
                goto done;
            }
            sector = end;
        }
        produced += output;
    }
    result = produced == info->disk_size;
done:
    if (table) xx_mem_free(table);
    if (bitmap) xx_mem_free(bitmap);
    return result;
}

/* Append one code point as UTF-8; control characters, surrogates and
 * anything out of range become '?', so the comment is always printable. */
static size_t vhddynamic_put_utf8(char *out, size_t used, size_t size,
                                  uint32_t cp) {
    if (cp < 0x20U || cp == 0x7fU || (cp >= 0xd800U && cp <= 0xdfffU) ||
        cp > 0x10ffffU)
        cp = '?';
    if (cp < 0x80U) {
        if (used + 1U >= size) return used;
        out[used++] = (char)cp;
    } else if (cp < 0x800U) {
        if (used + 2U >= size) return used;
        out[used++] = (char)(0xc0U | (cp >> 6U));
        out[used++] = (char)(0x80U | (cp & 0x3fU));
    } else if (cp < 0x10000U) {
        if (used + 3U >= size) return used;
        out[used++] = (char)(0xe0U | (cp >> 12U));
        out[used++] = (char)(0x80U | ((cp >> 6U) & 0x3fU));
        out[used++] = (char)(0x80U | (cp & 0x3fU));
    } else {
        if (used + 4U >= size) return used;
        out[used++] = (char)(0xf0U | (cp >> 18U));
        out[used++] = (char)(0x80U | ((cp >> 12U) & 0x3fU));
        out[used++] = (char)(0x80U | ((cp >> 6U) & 0x3fU));
        out[used++] = (char)(0x80U | (cp & 0x3fU));
    }
    return used;
}

/* "differencing image; parent {id} name" from the dynamic-disk header.  The
 * id is printed in stored byte order; the name is the header's UTF-16BE
 * parent file name.  The text is metadata only and never becomes a path. */
static void vhddynamic_parent_comment(const vhddynamic_info *info, char *out,
                                      size_t size) {
    static const char hex[] = "0123456789abcdef";
    static const char prefix[] = "differencing image; parent";
    const uint8_t *id = info->dynamic + 40U;
    const uint8_t *name = info->dynamic + 64U;
    size_t used, at;
    bool any_id = false, any_name = false;
    if (!out || size < 128U) return;
    for (at = 0U; at < 16U; ++at)
        if (id[at] != 0U) any_id = true;
    any_name = name[0] != 0U || name[1] != 0U;
    xx_rt_memcpy(out, prefix, sizeof(prefix) - 1U);
    used = sizeof(prefix) - 1U;
    if (!any_id && !any_name) {
        static const char none[] = " not recorded";
        xx_rt_memcpy(out + used, none, sizeof(none));
        return;
    }
    if (any_id) {
        out[used++] = ' ';
        out[used++] = '{';
        for (at = 0U; at < 16U; ++at) {
            if (at == 4U || at == 6U || at == 8U || at == 10U)
                out[used++] = '-';
            out[used++] = hex[id[at] >> 4U];
            out[used++] = hex[id[at] & 15U];
        }
        out[used++] = '}';
    }
    if (any_name) {
        out[used++] = ' ';
        for (at = 0U; at + 1U < 512U; at += 2U) {
            uint32_t unit = ((uint32_t)name[at] << 8U) | name[at + 1U];
            if (unit == 0U) break;
            if (unit >= 0xd800U && unit <= 0xdbffU && at + 3U < 512U) {
                uint32_t low = ((uint32_t)name[at + 2U] << 8U) |
                               name[at + 3U];
                if (low >= 0xdc00U && low <= 0xdfffU) {
                    unit = 0x10000U + ((unit - 0xd800U) << 10U) +
                           (low - 0xdc00U);
                    at += 2U;
                }
            }
            used = vhddynamic_put_utf8(out, used, size, unit);
        }
    }
    out[used] = 0;
}

static bool vhddynamic_set_record(xx_archive_record *record,
                                  const vhddynamic_info *info) {
    uint64_t stored = info->disk_type == VHDDYNAMIC_TYPE_FIXED
                          ? info->disk_size
                          : (uint64_t)info->size;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = info->base + info->footer_offset;
    record->header_size = info->footer_size;
    record->data_offset = info->base;
    record->compressed_size = (int64_t)stored;
    if (!xx_archive_record_set_original_name(record, VHDDYNAMIC_MEMBER_NAME) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        stored) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        info->disk_size) ||
        /* The method slot carries the VHD disk type (2, 3 or 4). */
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        info->disk_type) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        info->timestamp) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (info->disk_type == VHDDYNAMIC_TYPE_DIFFERENCING) {
        char comment[VHDDYNAMIC_COMMENT_SIZE];
        vhddynamic_parent_comment(info, comment, sizeof(comment));
        if (!xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                            comment))
            return false;
    }
    return true;
}

static bool vhddynamic_copy_options(xx_list_s *destination,
                                    const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *vhddynamic_option(const xx_list_s *options,
                                       uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

/* XX_META_ID_OPT_MAX_MEMBER_SIZE, when given, caps the guest disk size an
 * unpack will write.  Absent means unlimited (parse() caps at 4 TiB), as in
 * the vdi reader. */
static bool vhddynamic_size_allowed(Abstractformat *format,
                                    const xx_list_s *options, uint64_t size) {
    const xx_var *limit = xx_format_resolve_extra_parameter(
        format, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (!limit) return true;
    switch (limit->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return size <= xx_var_get_u64(limit);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(limit);
            return value < 0 || size <= (uint64_t)value;
        }
        default: return true;
    }
}

void xx_vhddynamic_init(xx_vhddynamic *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_rt_memset(archive, 0, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_VHDDYNAMIC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-vhd");
    xx_format_set_extension(&archive->format, "vhd");
    archive->format.check_is_valid = xx_vhddynamic_check_is_valid;
    archive->format.handle_base_info = xx_vhddynamic_handle_base_info;
    archive->format.get_format_size = xx_vhddynamic_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_vhddynamic_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_vhddynamic_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_vhddynamic_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_vhddynamic_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_vhddynamic_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_vhddynamic_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_vhddynamic *xx_vhddynamic_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_vhddynamic *archive = (xx_vhddynamic *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_vhddynamic_init(archive, device, base_address);
    return archive;
}

void xx_vhddynamic_destroy(xx_vhddynamic *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_vhddynamic_free(xx_vhddynamic *archive) {
    if (!archive) return;
    xx_vhddynamic_destroy(archive);
    xx_mem_free(archive);
}

bool xx_vhddynamic_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    vhddynamic_info info;
    (void)pd;
    return vhddynamic_parse(format, &info);
}

bool xx_vhddynamic_handle_base_info(Abstractformat *format,
                                    xx_pd_struct *pd) {
    vhddynamic_info info;
    xx_vhddynamic *archive;
    (void)pd;
    if (!format || !vhddynamic_parse(format, &info)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_vhddynamic *)format;
    archive->number_of_records = 1U;
    archive->archive_end = info.base + info.size;
    format->number_of_archive_records = 1U;
    format->format_size = info.size;
    format->file_type = XX_VHDDYNAMIC_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_vhddynamic_get_format_size(Abstractformat *format,
                                      xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vhddynamic_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_vhddynamic_get_number_of_archive_records(Abstractformat *format,
                                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vhddynamic_handle_base_info(format, pd))
               ? ((xx_vhddynamic *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_vhddynamic_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    vhddynamic_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    stream = (vhddynamic_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_rt_memset(stream, 0, sizeof(*stream));
    if (!vhddynamic_parse(format, &stream->info)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = vhddynamic_stream_free;
    state->total_records = 1;
    if (!vhddynamic_copy_options(&state->options, options) ||
        !vhddynamic_set_record(&state->current_record, &stream->info)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_vhddynamic_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_vhddynamic_archive_record_move_to_next(Abstractformat *format,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    vhddynamic_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (vhddynamic_stream *)state->internal_state)) {
        if (state) state->has_record = false;
        return false;
    }
    /* There is exactly one member, so the first step is always the last. */
    stream->index = 1U;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_vhddynamic_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    vhddynamic_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (vhddynamic_stream *)state->internal_state) ||
        stream->index != 0U || (pd && xx_pd_is_stopped(pd)))
        return false;
    if (!vhddynamic_size_allowed(format, &state->options,
                                 stream->info.disk_size))
        return false;
    path_option = vhddynamic_option(&state->options,
                                    XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return vhddynamic_emit(format, &stream->info, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* The member name is the reader's own constant, never file data. */
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", VHDDYNAMIC_MEMBER_NAME)
               : xx_str_concat(base, VHDDYNAMIC_MEMBER_NAME);
    if (!path) goto done;
    if (!xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    created = destination != NULL;
    if (!destination) goto done;
    result = vhddynamic_emit(format, &stream->info, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_vhddynamic_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
