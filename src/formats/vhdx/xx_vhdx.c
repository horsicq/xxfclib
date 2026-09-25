/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft VHDX virtual hard disk, format version 1 (MS-VHDX).  Everything
 * is little endian; GUIDs are compared in their on-disk byte order.
 *
 * Header section (the first 1 MiB)
 *   0x00000  file type identifier: "vhdxfile", then a UTF-16 creator string
 *   0x10000  header 1 (4 KiB)          0x20000  header 2 (4 KiB)
 *   0x30000  region table 1 (64 KiB)   0x40000  region table 2 (64 KiB)
 * Header: +0 "head", +4 CRC-32C of the 4 KiB with this field zero,
 *   +8 u64 sequence number, +16 file write GUID, +32 data write GUID,
 *   +48 log GUID, +64 u16 log version (0), +66 u16 version (1),
 *   +68 u32 log length, +72 u64 log offset (both 1 MiB multiples).  The
 *   current header is the valid one with the larger sequence number; two
 *   valid headers with the same number are accepted only when they agree
 *   (Disk2vhd writes such files).
 * Region table: +0 "regi", +4 CRC-32C of the 64 KiB, +8 u32 entry count
 *   (<= 2047), +12 u32 reserved (0); entries of 32 bytes at +16: GUID,
 *   u64 file offset, u32 length (1 MiB multiples), u32 bit 0 = required.
 *   The BAT (2DC27766-...) and metadata (8B7CA206-...) regions must exist;
 *   an unknown region marked required is refused.  qemu-img writes the two
 *   known regions with the required bit clear, so it is not insisted on.
 *   The first table that is valid is used.
 * Metadata region: a 64 KiB table, "metadata", u16 reserved, u16 entry count
 *   (<= 2047), entries of 32 bytes at +32: GUID, u32 offset (>= 64 KiB,
 *   relative to the region), u32 length (<= 1 MiB), u32 flags (1 user,
 *   2 virtual disk, 4 required), u32 reserved.  Items used: file parameters
 *   (u32 block size, a power of two from 1 to 256 MiB; u32 flags: 1 leave
 *   blocks allocated = fixed, 2 has parent = differencing), virtual disk size
 *   (u64, a multiple of the logical sector size, <= 64 TiB), logical and
 *   physical sector size (512 or 4096), the parent locator (only its
 *   "parent_linkage" value is read, for the record comment).  An unknown item
 *   marked required is refused.
 * BAT: u64 entries, bits 0-2 state, bits 3-19 reserved (0), bits 20-63 file
 *   offset in MiB.  After every chunk ratio = 2^23 * logical sector size /
 *   block size payload entries comes one sector bitmap entry.  Payload
 *   states: 0 not present, 1 undefined, 2 zero, 3 unmapped (all read as
 *   zeros), 6 fully present, 7 partially present (differencing images only:
 *   a set bit in the chunk's 1 MiB sector bitmap marks a logical sector held
 *   in this file).  Sector bitmap states: 0 not present, 6 present.
 * Log: a circular run of 4 KiB sectors at the header's log offset.  When the
 *   current header's log GUID is not zero, the active sequence is replayed
 *   in memory, never on the file: entries ("loge" + CRC-32C over the whole
 *   entry) whose sequence numbers follow one another in log order form a
 *   sequence; it is valid when its head entry names a tail entry inside it,
 *   and the valid sequence with the largest head sequence number is the
 *   active one.  Its data descriptors ("desc", with a matching "data"
 *   sector) replace 4 KiB of the file, its zero descriptors ("zero") clear a
 *   range, in order from the tail to the head.  Everything after the headers
 *   is read through that overlay.  No valid sequence means there is nothing
 *   to replay (as in qemu).
 *
 * A differencing image holds only the sectors that changed relative to its
 * parent.  Like the VDI and qcow readers, this reader publishes the image's
 * own data, with zeros where the parent would supply it, and says so in the
 * record comment.
 *
 * Written from the MS-VHDX specification.  The structural checks follow
 * XArchive diskimages/xvirtualdiskarchive.cpp (parseVHDX, MIT licence, same
 * author), relaxed where real images differ from it (qemu-img region entries,
 * equal header sequence numbers) and extended with differencing images and
 * log replay.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vhdx/xx_vhdx.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef VHDX
#define XX_VHDX_FILE_TYPE XX_FILE_TYPE_VHDX
#else
#define XX_VHDX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define VHDX_MIB UINT64_C(0x100000)
#define VHDX_MIB_MASK UINT64_C(0xFFFFF)
#define VHDX_HEADER1 0x10000U
#define VHDX_HEADER2 0x20000U
#define VHDX_HEADER_SIZE 4096U
#define VHDX_REGION1 0x30000U
#define VHDX_REGION2 0x40000U
#define VHDX_TABLE_SIZE 0x10000U /* region table and metadata table */
#define VHDX_MIN_FILE 0x50000U   /* identifier, headers, region tables */
#define VHDX_MAX_ENTRIES 2047U
#define VHDX_MAX_ITEM 0x100000U
#define VHDX_MIN_BLOCK 0x100000U
#define VHDX_MAX_BLOCK 0x10000000U
/* The specification's limit on the virtual disk size. */
#define VHDX_MAX_DISK (UINT64_C(64) << 40)
/* No structure offset (or a log-extended file size) beyond 256 TiB. */
#define VHDX_MAX_FILE (UINT64_C(1) << 48)
#define VHDX_BITMAP_SIZE 0x100000U
#define VHDX_LOG_SECTOR 4096U
/* The log is read whole only when it must be replayed; Hyper-V and qemu
 * write 1 MiB logs. */
#define VHDX_LOG_MAX (32U * 0x100000U)
#define VHDX_LOG_MAX_OPS 65536U
#define VHDX_IO_BUFFER (256U * 1024U)
#define VHDX_MEMBER_NAME "disk.img"

#define VHDX_SIG_HEAD UINT32_C(0x64616568) /* "head" */
#define VHDX_SIG_REGI UINT32_C(0x69676572) /* "regi" */
#define VHDX_SIG_LOGE UINT32_C(0x65676F6C) /* "loge" */
#define VHDX_SIG_ZERO UINT32_C(0x6F72657A) /* "zero" */
#define VHDX_SIG_DESC UINT32_C(0x63736564) /* "desc" */
#define VHDX_SIG_DATA UINT32_C(0x61746164) /* "data" */

#define VHDX_STATE_FULLY_PRESENT 6U
#define VHDX_STATE_PARTIALLY_PRESENT 7U
#define VHDX_STATE_SB_PRESENT 6U

static const uint8_t vhdx_guid_bat[16] = {
    0x66, 0x77, 0xC2, 0x2D, 0x23, 0xF6, 0x00, 0x42,
    0x9D, 0x64, 0x11, 0x5E, 0x9B, 0xFD, 0x4A, 0x08};
static const uint8_t vhdx_guid_metadata[16] = {
    0x06, 0xA2, 0x7C, 0x8B, 0x90, 0x47, 0x9A, 0x4B,
    0xB8, 0xFE, 0x57, 0x5F, 0x05, 0x0F, 0x88, 0x6E};
static const uint8_t vhdx_guid_file_parameters[16] = {
    0x37, 0x67, 0xA1, 0xCA, 0x36, 0xFA, 0x43, 0x4D,
    0xB3, 0xB6, 0x33, 0xF0, 0xAA, 0x44, 0xE7, 0x6B};
static const uint8_t vhdx_guid_disk_size[16] = {
    0x24, 0x42, 0xA5, 0x2F, 0x1B, 0xCD, 0x76, 0x48,
    0xB2, 0x11, 0x5D, 0xBE, 0xD8, 0x3B, 0xF4, 0xB8};
static const uint8_t vhdx_guid_disk_id[16] = {
    0xAB, 0x12, 0xCA, 0xBE, 0xE6, 0xB2, 0x23, 0x45,
    0x93, 0xEF, 0xC3, 0x09, 0xE0, 0x00, 0xC7, 0x46};
static const uint8_t vhdx_guid_logical_sector[16] = {
    0x1D, 0xBF, 0x41, 0x81, 0x6F, 0xA9, 0x09, 0x47,
    0xBA, 0x47, 0xF2, 0x33, 0xA8, 0xFA, 0xAB, 0x5F};
static const uint8_t vhdx_guid_physical_sector[16] = {
    0xC7, 0x48, 0xA3, 0xCD, 0x5D, 0x44, 0x71, 0x44,
    0x9C, 0xC9, 0xE9, 0x88, 0x52, 0x51, 0xC5, 0x56};
static const uint8_t vhdx_guid_parent_locator[16] = {
    0x2D, 0x5F, 0xD3, 0xA8, 0x0B, 0xB3, 0x4D, 0x45,
    0xAB, 0xF7, 0xD3, 0xD8, 0x48, 0x34, 0xAB, 0x0C};

/* One replayed write: [start, end) of the file, from a 4 KiB sector of the
 * log buffer or (data == NULL) zeros. */
typedef struct vhdx_op_s {
    uint64_t start;
    uint64_t end;
    const uint8_t *data;
} vhdx_op;

/* The replayed log: the ops in replay order and, for reads, the disjoint
 * segments of the file each op finally owns (later ops win). */
typedef struct vhdx_overlay_s {
    uint8_t *log;
    vhdx_op *ops;
    size_t op_count;
    uint64_t *seg_start;
    uint64_t *seg_end;
    uint32_t *seg_op;
    size_t seg_count;
} vhdx_overlay;

typedef struct vhdx_info_s {
    int64_t base;
    uint64_t size;           /* bytes from base to the end of the device */
    uint64_t vsize;          /* size once a replayed log has extended it */
    vhdx_overlay *overlay;   /* NULL when no log was replayed */
    uint32_t log_entries;
    uint32_t log_length;
    uint64_t log_offset;
    uint64_t bat_offset;
    uint64_t bat_length;
    uint64_t meta_offset;
    uint64_t meta_length;
    uint64_t struct_end;     /* end of the header section, log and regions */
    uint64_t disk_size;
    uint32_t block_size;
    uint32_t logical;
    uint32_t physical;
    uint32_t file_flags;
    bool differencing;
    uint64_t chunk_ratio;
    uint64_t blocks;
    uint64_t bat_entries;
    char parent[40];         /* "{...}" from parent_linkage, or empty */
    /* filled by the BAT walk */
    uint64_t present;
    uint64_t first_data;
    uint64_t format_size;
} vhdx_info;

typedef struct vhdx_stream_s {
    vhdx_info info;
    size_t index;
} vhdx_stream;

typedef struct vhdx_log_slot_s {
    uint64_t seq;
    uint32_t length;
    uint32_t tail;
    uint32_t link;           /* slot of the next entry in sequence, or ~0 */
    uint8_t valid;
} vhdx_log_slot;

#define VHDX_NO_SLOT UINT32_C(0xFFFFFFFF)

static uint16_t vhdx_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t vhdx_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U) | ((uint32_t)b[2] << 16U) |
           ((uint32_t)b[3] << 24U);
}

static uint64_t vhdx_le64(const uint8_t *b) {
    return (uint64_t)vhdx_le32(b) | ((uint64_t)vhdx_le32(b + 4U) << 32U);
}

static bool vhdx_is_zero(const uint8_t *b, size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index)
        if (b[index] != 0U) return false;
    return true;
}

static bool vhdx_dev_read(xx_io_device *device, int64_t offset, void *buffer,
                          size_t size) {
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

static bool vhdx_write_all(xx_io_device *device, const uint8_t *data,
                           size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device) return true; /* verify-only pass */
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* CRC-32C of a structure whose own checksum sits at +4, taken as zero. */
static bool vhdx_checksum_ok(const uint8_t *data, size_t size) {
    static const uint8_t zero4[4] = {0U, 0U, 0U, 0U};
    uint32_t crc;
    if (size < 8U) return false;
    crc = xx_crc32c_calc(0U, data, 4U);
    crc = xx_crc32c_calc(crc, zero4, 4U);
    crc = xx_crc32c_calc(crc, data + 8U, size - 8U);
    return crc == vhdx_le32(data + 4U);
}

/* ---- log overlay ------------------------------------------------------ */

static void vhdx_overlay_free(vhdx_overlay *overlay) {
    if (!overlay) return;
    if (overlay->log) xx_mem_free(overlay->log);
    if (overlay->ops) xx_mem_free(overlay->ops);
    if (overlay->seg_start) xx_mem_free(overlay->seg_start);
    if (overlay->seg_end) xx_mem_free(overlay->seg_end);
    if (overlay->seg_op) xx_mem_free(overlay->seg_op);
    xx_mem_free(overlay);
}

static void vhdx_info_cleanup(vhdx_info *info) {
    if (!info) return;
    vhdx_overlay_free(info->overlay);
    info->overlay = NULL;
}

static void vhdx_sift(uint64_t *values, size_t root, size_t count) {
    for (;;) {
        size_t child = root * 2U + 1U;
        uint64_t swap;
        if (child >= count) return;
        if (child + 1U < count && values[child] < values[child + 1U]) ++child;
        if (values[root] >= values[child]) return;
        swap = values[root];
        values[root] = values[child];
        values[child] = swap;
        root = child;
    }
}

static void vhdx_sort(uint64_t *values, size_t count) {
    size_t index, end;
    if (count < 2U) return;
    for (index = count / 2U; index-- > 0U;) vhdx_sift(values, index, count);
    for (end = count - 1U; end > 0U; --end) {
        uint64_t swap = values[0];
        values[0] = values[end];
        values[end] = swap;
        vhdx_sift(values, 0U, end);
    }
}

static uint32_t vhdx_find(uint32_t *next, uint32_t index) {
    uint32_t root = index;
    while (next[root] != root) root = next[root];
    while (next[index] != root) {
        uint32_t step = next[index];
        next[index] = root;
        index = step;
    }
    return root;
}

/* Turn the ops into disjoint segments, each owned by the LAST op that
 * covers it: the op boundaries split the file into elementary segments, the
 * ops are painted newest first, and a "next unpainted segment" forest skips
 * what a newer op already owns, so the work is near linear in the op
 * count. */
static bool vhdx_overlay_build(vhdx_overlay *overlay) {
    size_t count = overlay->op_count, points = 0U, index, painted = 0U;
    uint64_t *point = NULL;
    uint32_t *next = NULL;
    uint32_t *owner = NULL;
    bool result = false;

    if (count == 0U) return true;
    point = (uint64_t *)xx_mem_alloc(count * 2U * sizeof(uint64_t));
    if (!point) goto done;
    for (index = 0U; index < count; ++index) {
        point[points++] = overlay->ops[index].start;
        point[points++] = overlay->ops[index].end;
    }
    vhdx_sort(point, points);
    {
        size_t unique = 1U;
        for (index = 1U; index < points; ++index)
            if (point[index] != point[unique - 1U]) point[unique++] = point[index];
        points = unique;
    }
    next = (uint32_t *)xx_mem_alloc(points * sizeof(uint32_t));
    owner = (uint32_t *)xx_mem_alloc(points * sizeof(uint32_t));
    if (!next || !owner) goto done;
    for (index = 0U; index < points; ++index) {
        next[index] = (uint32_t)index;
        owner[index] = VHDX_NO_SLOT;
    }
    for (index = count; index-- > 0U;) {
        const vhdx_op *op = &overlay->ops[index];
        size_t low = 0U, high = points;
        uint32_t at;
        while (low < high) {
            size_t middle = low + (high - low) / 2U;
            if (point[middle] < op->start) low = middle + 1U;
            else high = middle;
        }
        at = vhdx_find(next, (uint32_t)low);
        while ((size_t)at + 1U < points && point[at] < op->end) {
            owner[at] = (uint32_t)index;
            next[at] = at + 1U;
            ++painted;
            at = vhdx_find(next, at + 1U);
        }
    }
    if (painted != 0U) {
        size_t segment = 0U;
        overlay->seg_start = (uint64_t *)xx_mem_alloc(painted * sizeof(uint64_t));
        overlay->seg_end = (uint64_t *)xx_mem_alloc(painted * sizeof(uint64_t));
        overlay->seg_op = (uint32_t *)xx_mem_alloc(painted * sizeof(uint32_t));
        if (!overlay->seg_start || !overlay->seg_end || !overlay->seg_op)
            goto done;
        for (index = 0U; index + 1U < points; ++index) {
            if (owner[index] == VHDX_NO_SLOT) continue;
            overlay->seg_start[segment] = point[index];
            overlay->seg_end[segment] = point[index + 1U];
            overlay->seg_op[segment] = owner[index];
            ++segment;
        }
        overlay->seg_count = segment;
    }
    result = true;
done:
    if (point) xx_mem_free(point);
    if (next) xx_mem_free(next);
    if (owner) xx_mem_free(owner);
    return result;
}

static void vhdx_overlay_apply(const vhdx_overlay *overlay, uint64_t offset,
                               uint8_t *buffer, size_t size) {
    size_t low = 0U, high = overlay->seg_count;
    uint64_t end = offset + size;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (overlay->seg_end[middle] <= offset) low = middle + 1U;
        else high = middle;
    }
    for (; low < overlay->seg_count && overlay->seg_start[low] < end; ++low) {
        const vhdx_op *op = &overlay->ops[overlay->seg_op[low]];
        uint64_t from = overlay->seg_start[low] > offset ? overlay->seg_start[low]
                                                         : offset;
        uint64_t to = overlay->seg_end[low] < end ? overlay->seg_end[low] : end;
        if (op->data)
            xx_rt_memcpy(buffer + (size_t)(from - offset),
                         op->data + (size_t)(from - op->start),
                         (size_t)(to - from));
        else
            xx_rt_memset(buffer + (size_t)(from - offset), 0,
                         (size_t)(to - from));
    }
}

/* Read [offset, offset + size) of the image (offsets relative to its
 * start) as it is after log replay. */
static bool vhdx_read(Abstractformat *format, const vhdx_info *info,
                      uint64_t offset, uint8_t *buffer, size_t size) {
    uint64_t real = 0U;
    if (size == 0U) return true;
    if (offset > info->vsize || (uint64_t)size > info->vsize - offset)
        return false;
    if (offset < info->size) {
        uint64_t left = info->size - offset;
        real = left < size ? left : (uint64_t)size;
        if (!vhdx_dev_read(format->device, info->base + (int64_t)offset,
                           buffer, (size_t)real))
            return false;
    }
    if (real < size) xx_rt_memset(buffer + real, 0, size - (size_t)real);
    if (info->overlay) vhdx_overlay_apply(info->overlay, offset, buffer, size);
    return true;
}

/* Validate the log entry that starts at byte `pos` of the log.  Returns 1
 * for a valid entry, 0 for none, -1 when the checksum budget is spent (a
 * log crafted so that entries overlap would otherwise cost quadratic
 * time). */
static int vhdx_log_entry(const uint8_t *log, uint32_t length, uint32_t pos,
                          const uint8_t *guid, uint64_t *budget,
                          vhdx_log_slot *slot) {
    static const uint8_t zero4[4] = {0U, 0U, 0U, 0U};
    const uint8_t *h = log + pos;
    uint32_t entry_length, tail, count, index, first, crc;
    uint64_t seq, descriptor_bytes, descriptor_area, data = 0U;

    if (vhdx_le32(h) != VHDX_SIG_LOGE) return 0;
    entry_length = vhdx_le32(h + 8U);
    tail = vhdx_le32(h + 12U);
    seq = vhdx_le64(h + 16U);
    count = vhdx_le32(h + 24U);
    if (entry_length < VHDX_LOG_SECTOR || (entry_length % VHDX_LOG_SECTOR) != 0U ||
        entry_length > length || (tail % VHDX_LOG_SECTOR) != 0U ||
        tail >= length || seq == 0U || xx_rt_memcmp(h + 32U, guid, 16U) != 0)
        return 0;
    descriptor_bytes = 64U + (uint64_t)count * 32U;
    descriptor_area = (descriptor_bytes + VHDX_LOG_SECTOR - 1U) /
                      VHDX_LOG_SECTOR * VHDX_LOG_SECTOR;
    if (descriptor_area > entry_length) return 0;
    if (entry_length > *budget) return -1;
    *budget -= entry_length;
    /* The entry may wrap around the end of the log; its first sector never
     * does (pos and length are 4 KiB multiples). */
    first = length - pos < entry_length ? length - pos : entry_length;
    crc = xx_crc32c_calc(0U, h, 4U);
    crc = xx_crc32c_calc(crc, zero4, 4U);
    crc = xx_crc32c_calc(crc, h + 8U, first - 8U);
    if (entry_length > first)
        crc = xx_crc32c_calc(crc, log, entry_length - first);
    if (crc != vhdx_le32(h + 4U)) return 0;
    for (index = 0U; index < count; ++index) {
        /* 32-byte descriptors never straddle the wrap point. */
        const uint8_t *d = log + (size_t)(((uint64_t)pos + 64U +
                                           (uint64_t)index * 32U) % length);
        uint32_t signature = vhdx_le32(d);
        uint64_t file_offset = vhdx_le64(d + 16U);
        if (vhdx_le64(d + 24U) != seq ||
            (file_offset % VHDX_LOG_SECTOR) != 0U ||
            file_offset > VHDX_MAX_FILE)
            return 0;
        if (signature == VHDX_SIG_ZERO) {
            uint64_t zero_length = vhdx_le64(d + 8U);
            if ((zero_length % VHDX_LOG_SECTOR) != 0U ||
                zero_length > VHDX_MAX_FILE)
                return 0;
        } else if (signature == VHDX_SIG_DESC) {
            uint64_t sector = descriptor_area + data * VHDX_LOG_SECTOR;
            const uint8_t *s;
            if (sector + VHDX_LOG_SECTOR > entry_length) return 0;
            s = log + (size_t)(((uint64_t)pos + sector) % length);
            if (vhdx_le32(s) != VHDX_SIG_DATA ||
                vhdx_le32(s + 4U) != (uint32_t)(seq >> 32U) ||
                vhdx_le32(s + 4092U) != (uint32_t)seq)
                return 0;
            ++data;
        } else {
            return 0;
        }
    }
    slot->seq = seq;
    slot->length = entry_length;
    slot->tail = tail;
    return 1;
}

/* Walk from the tail slot of `head` along the links; true when it reaches
 * `head` inside one pass over the log. */
static bool vhdx_log_sequence_ok(const vhdx_log_slot *slot, uint32_t slots,
                                 uint32_t length, uint32_t head) {
    uint32_t at = slot[head].tail / VHDX_LOG_SECTOR, steps;
    uint64_t total = 0U;
    for (steps = 0U; steps < slots; ++steps) {
        if (!slot[at].valid) return false;
        total += slot[at].length;
        if (total > length) return false;
        if (at == head) return true;
        if (slot[at].link == VHDX_NO_SLOT) return false;
        at = slot[at].link;
    }
    return false;
}

/* Find the active log sequence and build the in-memory overlay.  False
 * means the file must be refused. */
static bool vhdx_log_replay(Abstractformat *format, vhdx_info *info,
                            const uint8_t *guid, uint16_t log_version) {
    uint32_t length = info->log_length, slots, index, head = VHDX_NO_SLOT;
    uint32_t at, entries = 0U;
    uint64_t budget, ops = 0U, flushed, last;
    uint8_t *log = NULL;
    vhdx_log_slot *slot = NULL;
    vhdx_overlay *overlay = NULL;
    bool result = false;

    if (log_version != 0U || length == 0U || length > VHDX_LOG_MAX ||
        info->log_offset > info->size || length > info->size - info->log_offset)
        return false;
    log = (uint8_t *)xx_mem_alloc(length);
    slots = length / VHDX_LOG_SECTOR;
    slot = (vhdx_log_slot *)xx_mem_calloc(slots, sizeof(*slot));
    if (!log || !slot ||
        !vhdx_dev_read(format->device, info->base + (int64_t)info->log_offset,
                       log, length))
        goto done;
    budget = (uint64_t)length * 4U;
    for (index = 0U; index < slots; ++index) {
        int valid = vhdx_log_entry(log, length, index * VHDX_LOG_SECTOR, guid,
                                   &budget, &slot[index]);
        if (valid < 0) goto done;
        slot[index].valid = valid > 0 ? 1U : 0U;
        slot[index].link = VHDX_NO_SLOT;
    }
    for (index = 0U; index < slots; ++index) {
        uint32_t follow;
        if (!slot[index].valid || slot[index].seq == UINT64_MAX) continue;
        follow = (uint32_t)(((uint64_t)index * VHDX_LOG_SECTOR +
                             slot[index].length) % length / VHDX_LOG_SECTOR);
        if (slot[follow].valid && slot[follow].seq == slot[index].seq + 1U)
            slot[index].link = follow;
    }
    /* A head ends its chain; the valid one with the largest sequence
     * number is the active sequence. */
    for (index = 0U; index < slots; ++index) {
        if (!slot[index].valid || slot[index].link != VHDX_NO_SLOT) continue;
        if (head != VHDX_NO_SLOT && slot[index].seq <= slot[head].seq) continue;
        if (vhdx_log_sequence_ok(slot, slots, length, index)) head = index;
    }
    if (head == VHDX_NO_SLOT) {
        result = true; /* nothing to replay */
        goto done;
    }
    flushed = vhdx_le64(log + (size_t)head * VHDX_LOG_SECTOR + 48U);
    last = vhdx_le64(log + (size_t)head * VHDX_LOG_SECTOR + 56U);
    /* The file must hold everything that was flushed before the log was
     * written; the log may extend it up to LastFileOffset. */
    if (flushed > info->size || last > VHDX_MAX_FILE) goto done;

    /* Count, then collect, the descriptors from the tail to the head. */
    at = slot[head].tail / VHDX_LOG_SECTOR;
    for (;;) {
        ops += vhdx_le32(log + (size_t)at * VHDX_LOG_SECTOR + 24U);
        ++entries;
        if (ops > VHDX_LOG_MAX_OPS) goto done;
        if (at == head) break;
        at = slot[at].link;
    }
    overlay = (vhdx_overlay *)xx_mem_calloc(1U, sizeof(*overlay));
    if (!overlay) goto done;
    if (ops != 0U) {
        overlay->ops = (vhdx_op *)xx_mem_alloc((size_t)ops * sizeof(vhdx_op));
        if (!overlay->ops) goto done;
    }
    at = slot[head].tail / VHDX_LOG_SECTOR;
    for (;;) {
        uint32_t pos = at * VHDX_LOG_SECTOR;
        const uint8_t *h = log + pos;
        uint32_t count = vhdx_le32(h + 24U), k;
        uint64_t area = (64U + (uint64_t)count * 32U + VHDX_LOG_SECTOR - 1U) /
                        VHDX_LOG_SECTOR * VHDX_LOG_SECTOR;
        uint64_t data = 0U;
        for (k = 0U; k < count; ++k) {
            const uint8_t *d = log + (size_t)(((uint64_t)pos + 64U +
                                               (uint64_t)k * 32U) % length);
            vhdx_op *op = &overlay->ops[overlay->op_count];
            op->start = vhdx_le64(d + 16U);
            if (vhdx_le32(d) == VHDX_SIG_DESC) {
                uint8_t *s = log + (size_t)(((uint64_t)pos + area +
                                             data * VHDX_LOG_SECTOR) % length);
                /* The sector as written: the descriptor's 8 leading bytes,
                 * 4084 bytes of the data sector, the 4 trailing bytes. */
                xx_rt_memcpy(s, d + 8U, 8U);
                xx_rt_memcpy(s + 4092U, d + 4U, 4U);
                op->end = op->start + VHDX_LOG_SECTOR;
                op->data = s;
                ++data;
            } else {
                op->end = op->start + vhdx_le64(d + 8U);
                op->data = NULL;
            }
            if (op->end > op->start) ++overlay->op_count;
        }
        if (at == head) break;
        at = slot[at].link;
    }
    if (!vhdx_overlay_build(overlay)) goto done;
    overlay->log = log;
    log = NULL;
    info->overlay = overlay;
    overlay = NULL;
    info->log_entries = entries;
    if (last > info->vsize) info->vsize = last;
    result = true;
done:
    if (log) xx_mem_free(log);
    if (slot) xx_mem_free(slot);
    vhdx_overlay_free(overlay);
    return result;
}

/* ---- header, regions, metadata --------------------------------------- */

static bool vhdx_header_ok(const uint8_t *h) {
    return vhdx_le32(h) == VHDX_SIG_HEAD &&
           vhdx_checksum_ok(h, VHDX_HEADER_SIZE) &&
           vhdx_le16(h + 66U) == 1U &&
           (vhdx_le32(h + 68U) & VHDX_MIB_MASK) == 0U &&
           (vhdx_le64(h + 72U) & VHDX_MIB_MASK) == 0U;
}

static bool vhdx_regions(const uint8_t *t, vhdx_info *info) {
    uint32_t count, index;
    bool have_bat = false, have_meta = false;
    uint64_t end = 0U;
    info->bat_offset = info->bat_length = 0U;
    info->meta_offset = info->meta_length = 0U;
    if (vhdx_le32(t) != VHDX_SIG_REGI || !vhdx_checksum_ok(t, VHDX_TABLE_SIZE))
        return false;
    count = vhdx_le32(t + 8U);
    if (count > VHDX_MAX_ENTRIES || vhdx_le32(t + 12U) != 0U) return false;
    for (index = 0U; index < count; ++index) {
        const uint8_t *e = t + 16U + (size_t)index * 32U;
        uint64_t offset = vhdx_le64(e + 16U);
        uint64_t length = vhdx_le32(e + 24U);
        uint32_t required = vhdx_le32(e + 28U);
        if ((offset & VHDX_MIB_MASK) != 0U || (length & VHDX_MIB_MASK) != 0U ||
            offset < VHDX_MIB || offset > VHDX_MAX_FILE)
            return false;
        if (xx_rt_memcmp(e, vhdx_guid_bat, 16U) == 0) {
            if (have_bat || length == 0U) return false;
            have_bat = true;
            info->bat_offset = offset;
            info->bat_length = length;
        } else if (xx_rt_memcmp(e, vhdx_guid_metadata, 16U) == 0) {
            if (have_meta || length == 0U) return false;
            have_meta = true;
            info->meta_offset = offset;
            info->meta_length = length;
        } else if ((required & 1U) != 0U) {
            return false; /* a region this reader does not understand */
        }
        if (offset + length > end) end = offset + length;
    }
    if (!have_bat || !have_meta) return false;
    if (end > info->struct_end) info->struct_end = end;
    return true;
}

/* The "parent_linkage" value of a VHDX parent locator, when it is a short
 * ASCII GUID string; anything else leaves `out` empty. */
static void vhdx_parent_linkage(const uint8_t *p, uint32_t length, char *out,
                                size_t out_size) {
    static const char key[] = "parent_linkage";
    uint32_t count, k;
    out[0] = 0;
    if (length < 20U) return;
    count = vhdx_le16(p + 18U);
    if (20U + (uint64_t)count * 12U > length) return;
    for (k = 0U; k < count; ++k) {
        const uint8_t *e = p + 20U + (size_t)k * 12U;
        uint32_t key_offset = vhdx_le32(e), value_offset = vhdx_le32(e + 4U);
        uint32_t key_length = vhdx_le16(e + 8U), value_length = vhdx_le16(e + 10U);
        uint32_t c;
        if (key_offset > length || key_length > length - key_offset ||
            value_offset > length || value_length > length - value_offset ||
            key_length != (sizeof(key) - 1U) * 2U)
            continue;
        for (c = 0U; c < sizeof(key) - 1U; ++c)
            if (p[key_offset + 2U * c] != (uint8_t)key[c] ||
                p[key_offset + 2U * c + 1U] != 0U)
                break;
        if (c != sizeof(key) - 1U) continue;
        if (value_length == 0U || (value_length & 1U) != 0U ||
            value_length / 2U + 1U > out_size)
            return;
        for (c = 0U; c < value_length / 2U; ++c) {
            uint8_t ch = p[value_offset + 2U * c];
            bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
                       (ch >= 'A' && ch <= 'F');
            if (p[value_offset + 2U * c + 1U] != 0U ||
                !(hex || ch == '{' || ch == '}' || ch == '-')) {
                out[0] = 0;
                return;
            }
            out[c] = (char)ch;
        }
        out[value_length / 2U] = 0;
        return;
    }
}

static bool vhdx_metadata(Abstractformat *format, vhdx_info *info,
                          const uint8_t *t) {
    uint32_t count, index;
    bool have_parameters = false, have_size = false, have_logical = false;
    bool have_physical = false, have_locator = false;
    uint8_t value[16];

    if (xx_rt_memcmp(t, "metadata", 8U) != 0) return false;
    count = vhdx_le16(t + 10U);
    if (count > VHDX_MAX_ENTRIES) return false;
    for (index = 0U; index < count; ++index) {
        const uint8_t *e = t + 32U + (size_t)index * 32U;
        uint32_t offset = vhdx_le32(e + 16U), length = vhdx_le32(e + 20U);
        uint32_t flags = vhdx_le32(e + 24U);
        uint64_t at = info->meta_offset + offset;
        if ((flags & ~7U) != 0U) return false;
        if (length == 0U) {
            if (offset != 0U) return false;
        } else if (offset < VHDX_TABLE_SIZE || length > VHDX_MAX_ITEM ||
                   offset > info->meta_length ||
                   length > info->meta_length - offset) {
            return false;
        }
        if ((flags & 1U) != 0U) {
            /* user metadata is never interpreted */
            if ((flags & 4U) != 0U) return false;
            continue;
        }
        if (xx_rt_memcmp(e, vhdx_guid_file_parameters, 16U) == 0) {
            if (have_parameters || length != 8U ||
                !vhdx_read(format, info, at, value, 8U))
                return false;
            info->block_size = vhdx_le32(value);
            info->file_flags = vhdx_le32(value + 4U);
            have_parameters = true;
        } else if (xx_rt_memcmp(e, vhdx_guid_disk_size, 16U) == 0) {
            if (have_size || length != 8U ||
                !vhdx_read(format, info, at, value, 8U))
                return false;
            info->disk_size = vhdx_le64(value);
            have_size = true;
        } else if (xx_rt_memcmp(e, vhdx_guid_logical_sector, 16U) == 0) {
            if (have_logical || length != 4U ||
                !vhdx_read(format, info, at, value, 4U))
                return false;
            info->logical = vhdx_le32(value);
            have_logical = true;
        } else if (xx_rt_memcmp(e, vhdx_guid_physical_sector, 16U) == 0) {
            if (have_physical || length != 4U ||
                !vhdx_read(format, info, at, value, 4U))
                return false;
            info->physical = vhdx_le32(value);
            have_physical = true;
        } else if (xx_rt_memcmp(e, vhdx_guid_disk_id, 16U) == 0) {
            if (length != 16U) return false;
        } else if (xx_rt_memcmp(e, vhdx_guid_parent_locator, 16U) == 0) {
            if (have_locator || length < 20U) return false;
            have_locator = true;
            if (length <= VHDX_TABLE_SIZE) {
                uint8_t *locator = (uint8_t *)xx_mem_alloc(length);
                if (!locator) return false;
                if (vhdx_read(format, info, at, locator, length))
                    vhdx_parent_linkage(locator, length, info->parent,
                                        sizeof(info->parent));
                xx_mem_free(locator);
            }
        } else if ((flags & 4U) != 0U) {
            return false; /* required system metadata this reader lacks */
        }
    }
    if (!have_parameters || !have_size || !have_logical) return false;
    if (info->block_size < VHDX_MIN_BLOCK || info->block_size > VHDX_MAX_BLOCK ||
        (info->block_size & (info->block_size - 1U)) != 0U ||
        (info->file_flags & ~3U) != 0U)
        return false;
    if (info->logical != 512U && info->logical != 4096U) return false;
    if (have_physical && info->physical != 512U && info->physical != 4096U)
        return false;
    if (info->disk_size == 0U || info->disk_size > VHDX_MAX_DISK ||
        (info->disk_size % info->logical) != 0U)
        return false;
    return true;
}

/* Header section, log replay, region table and metadata.  No BAT entry is
 * read.  On success the caller owns info->overlay (vhdx_info_cleanup). */
static bool vhdx_parse(Abstractformat *format, vhdx_info *info) {
    uint8_t *buffer = NULL;
    uint8_t *table;
    const uint8_t *head = NULL;
    int64_t total;
    bool first_ok, second_ok, result = false;

    if (!format || !format->device || !info || format->base_address < 0)
        return false;
    xx_rt_memset(info, 0, sizeof(*info));
    total = xx_io_total_size(format->device);
    if (total < 0 || total < format->base_address) return false;
    info->base = format->base_address;
    info->size = (uint64_t)(total - format->base_address);
    if (info->size < VHDX_MIN_FILE) return false;
    info->vsize = info->size;
    info->struct_end = VHDX_MIB;

    buffer = (uint8_t *)xx_mem_alloc(2U * VHDX_HEADER_SIZE + VHDX_TABLE_SIZE);
    if (!buffer) return false;
    table = buffer + 2U * VHDX_HEADER_SIZE;
    if (!vhdx_dev_read(format->device, info->base, buffer, 8U) ||
        xx_rt_memcmp(buffer, "vhdxfile", 8U) != 0)
        goto done;
    if (!vhdx_dev_read(format->device, info->base + VHDX_HEADER1, buffer,
                       VHDX_HEADER_SIZE) ||
        !vhdx_dev_read(format->device, info->base + VHDX_HEADER2,
                       buffer + VHDX_HEADER_SIZE, VHDX_HEADER_SIZE))
        goto done;
    first_ok = vhdx_header_ok(buffer);
    second_ok = vhdx_header_ok(buffer + VHDX_HEADER_SIZE);
    if (first_ok && second_ok) {
        uint64_t first_seq = vhdx_le64(buffer + 8U);
        uint64_t second_seq = vhdx_le64(buffer + VHDX_HEADER_SIZE + 8U);
        if (first_seq > second_seq) head = buffer;
        else if (second_seq > first_seq) head = buffer + VHDX_HEADER_SIZE;
        else if (xx_rt_memcmp(buffer + 8U, buffer + VHDX_HEADER_SIZE + 8U,
                              72U) == 0)
            head = buffer; /* identical copies (Disk2vhd) */
        else
            goto done;
    } else if (first_ok) {
        head = buffer;
    } else if (second_ok) {
        head = buffer + VHDX_HEADER_SIZE;
    } else {
        goto done;
    }

    info->log_length = vhdx_le32(head + 68U);
    info->log_offset = vhdx_le64(head + 72U);
    if (info->log_length != 0U) {
        if (info->log_offset < VHDX_MIB || info->log_offset > VHDX_MAX_FILE)
            goto done;
        if (info->log_offset + info->log_length > info->struct_end)
            info->struct_end = info->log_offset + info->log_length;
    }
    if (!vhdx_is_zero(head + 48U, 16U) &&
        !vhdx_log_replay(format, info, head + 48U, vhdx_le16(head + 64U)))
        goto done;

    if (!(vhdx_read(format, info, VHDX_REGION1, table, VHDX_TABLE_SIZE) &&
          vhdx_regions(table, info)) &&
        !(vhdx_read(format, info, VHDX_REGION2, table, VHDX_TABLE_SIZE) &&
          vhdx_regions(table, info)))
        goto done;
    if (info->bat_offset > info->vsize ||
        info->bat_length > info->vsize - info->bat_offset ||
        info->meta_offset > info->vsize ||
        info->meta_length > info->vsize - info->meta_offset)
        goto done;

    if (!vhdx_read(format, info, info->meta_offset, table, VHDX_TABLE_SIZE) ||
        !vhdx_metadata(format, info, table))
        goto done;
    info->differencing = (info->file_flags & 2U) != 0U;
    /* logical <= 4096 and block >= 1 MiB, so the ratio is 16..32768 */
    info->chunk_ratio = ((uint64_t)info->logical << 23U) / info->block_size;
    info->blocks = (info->disk_size - 1U) / info->block_size + 1U;
    if (info->differencing)
        info->bat_entries = (info->blocks + info->chunk_ratio - 1U) /
                            info->chunk_ratio * (info->chunk_ratio + 1U);
    else
        info->bat_entries = info->blocks + (info->blocks - 1U) / info->chunk_ratio;
    if (info->bat_entries > info->bat_length / 8U) goto done;
    result = true;
done:
    xx_mem_free(buffer);
    if (!result) vhdx_info_cleanup(info);
    return result;
}

/* ---- BAT walk ---------------------------------------------------------- */

static bool vhdx_write_zeros(xx_io_device *destination, const uint8_t *zeros,
                             uint64_t size, xx_pd_struct *pd) {
    while (size != 0U) {
        size_t step = size < VHDX_IO_BUFFER ? (size_t)size : VHDX_IO_BUFFER;
        if (!vhdx_write_all(destination, zeros, step, pd)) return false;
        size -= step;
    }
    return true;
}

static bool vhdx_copy(Abstractformat *format, const vhdx_info *info,
                      uint64_t offset, uint64_t size, uint8_t *data,
                      xx_io_device *destination, xx_pd_struct *pd) {
    while (size != 0U) {
        size_t step = size < VHDX_IO_BUFFER ? (size_t)size : VHDX_IO_BUFFER;
        if (!vhdx_read(format, info, offset, data, step) ||
            !vhdx_write_all(destination, data, step, pd))
            return false;
        offset += step;
        size -= step;
    }
    return true;
}

/* A partially present block: sectors whose bit is set in the chunk's
 * sector bitmap come from the block, the others (held by the parent) are
 * zeros. */
static bool vhdx_emit_partial(Abstractformat *format, const vhdx_info *info,
                              uint64_t offset, uint64_t output,
                              const uint8_t *bitmap, uint8_t *data,
                              const uint8_t *zeros, xx_io_device *destination,
                              xx_pd_struct *pd) {
    uint64_t sectors = output / info->logical, sector = 0U;
    while (sector < sectors) {
        uint64_t run = sector + 1U;
        bool present = ((bitmap[sector >> 3U] >> (sector & 7U)) & 1U) != 0U;
        while (run < sectors &&
               (((bitmap[run >> 3U] >> (run & 7U)) & 1U) != 0U) == present)
            ++run;
        if (present) {
            if (!vhdx_copy(format, info, offset + sector * info->logical,
                           (run - sector) * info->logical, data, destination,
                           pd))
                return false;
        } else if (!vhdx_write_zeros(destination, zeros,
                                     (run - sector) * info->logical, pd)) {
            return false;
        }
        sector = run;
    }
    return true;
}

/* Walk every BAT entry, chunk by chunk, checking each one; with a
 * destination also write the guest disk.  Without one no payload byte is
 * read: this is the validation used by handle_base_info and before a record
 * is published.  Fills present / first_data / format_size. */
static bool vhdx_walk(Abstractformat *format, vhdx_info *info,
                      xx_io_device *destination, xx_pd_struct *pd) {
    uint64_t ratio = info->chunk_ratio;
    uint64_t chunks = (info->blocks + ratio - 1U) / ratio;
    uint64_t chunk, produced = 0U, end = info->struct_end;
    uint64_t present = 0U, first_data = 0U;
    size_t bitmap_bytes = info->block_size / info->logical / 8U;
    uint8_t *bat = NULL, *data = NULL, *zeros = NULL, *bitmap = NULL;
    bool result = false;

    if (info->bat_offset + info->bat_length > end)
        end = info->bat_offset + info->bat_length;
    if (info->meta_offset + info->meta_length > end)
        end = info->meta_offset + info->meta_length;
    bat = (uint8_t *)xx_mem_alloc((size_t)(ratio + 1U) * 8U);
    if (!bat) goto done;
    if (destination) {
        data = (uint8_t *)xx_mem_alloc(VHDX_IO_BUFFER);
        zeros = (uint8_t *)xx_mem_calloc(1U, VHDX_IO_BUFFER);
        bitmap = (uint8_t *)xx_mem_alloc(bitmap_bytes);
        if (!data || !zeros || !bitmap) goto done;
    }
    for (chunk = 0U; chunk < chunks; ++chunk) {
        uint64_t first = chunk * (ratio + 1U);
        uint64_t count = info->bat_entries - first;
        uint64_t payload, k, bitmap_offset = 0U;
        uint32_t bitmap_state = 0U;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (count > ratio + 1U) count = ratio + 1U;
        if (!vhdx_read(format, info, info->bat_offset + first * 8U, bat,
                       (size_t)count * 8U))
            goto done;
        payload = count > ratio ? ratio : count;
        if (count > ratio) {
            uint64_t entry = vhdx_le64(bat + (size_t)ratio * 8U);
            if ((entry & UINT64_C(0xFFFF8)) != 0U) goto done;
            bitmap_state = (uint32_t)(entry & 7U);
            bitmap_offset = entry & ~VHDX_MIB_MASK;
            if (bitmap_state == VHDX_STATE_SB_PRESENT && info->differencing) {
                if (bitmap_offset == 0U || bitmap_offset > info->vsize ||
                    VHDX_BITMAP_SIZE > info->vsize - bitmap_offset)
                    goto done;
                if (bitmap_offset + VHDX_BITMAP_SIZE > end)
                    end = bitmap_offset + VHDX_BITMAP_SIZE;
            } else if (bitmap_state != 0U) {
                goto done;
            }
        }
        for (k = 0U; k < payload; ++k) {
            uint64_t entry = vhdx_le64(bat + (size_t)k * 8U);
            uint64_t offset = entry & ~VHDX_MIB_MASK;
            uint64_t block = chunk * ratio + k;
            uint64_t output = 0U;
            uint32_t state = (uint32_t)(entry & 7U);
            bool stored = false;
            if ((entry & UINT64_C(0xFFFF8)) != 0U) goto done;
            if (block < info->blocks) {
                output = info->disk_size - produced;
                if (output > info->block_size) output = info->block_size;
            }
            switch (state) {
                case 0U: case 1U: case 2U: case 3U:
                    break; /* not present / undefined / zero / unmapped */
                case VHDX_STATE_PARTIALLY_PRESENT:
                    if (!info->differencing ||
                        bitmap_state != VHDX_STATE_SB_PRESENT)
                        goto done;
                    /* fall through */
                case VHDX_STATE_FULLY_PRESENT:
                    if (block >= info->blocks) break; /* past the disk */
                    if (offset == 0U || offset > info->vsize ||
                        output > info->vsize - offset)
                        goto done;
                    if (offset + info->block_size > end)
                        end = offset + info->block_size;
                    if (first_data == 0U || offset < first_data)
                        first_data = offset;
                    ++present;
                    stored = true;
                    break;
                default:
                    goto done; /* reserved states 4 and 5 */
            }
            if (block >= info->blocks) continue;
            if (destination) {
                if (!stored) {
                    if (!vhdx_write_zeros(destination, zeros, output, pd))
                        goto done;
                } else if (state == VHDX_STATE_FULLY_PRESENT) {
                    if (!vhdx_copy(format, info, offset, output, data,
                                   destination, pd))
                        goto done;
                } else {
                    if (!vhdx_read(format, info,
                                   bitmap_offset + k * bitmap_bytes, bitmap,
                                   bitmap_bytes) ||
                        !vhdx_emit_partial(format, info, offset, output,
                                           bitmap, data, zeros, destination,
                                           pd))
                        goto done;
                }
            }
            produced += output;
        }
    }
    if (produced != info->disk_size) goto done;
    info->present = present;
    info->first_data = first_data;
    info->format_size = end < info->size ? end : info->size;
    result = true;
done:
    if (bat) xx_mem_free(bat);
    if (data) xx_mem_free(data);
    if (zeros) xx_mem_free(zeros);
    if (bitmap) xx_mem_free(bitmap);
    return result;
}

/* ---- records ----------------------------------------------------------- */

static void vhdx_stream_free(void *opaque) {
    vhdx_stream *stream = (vhdx_stream *)opaque;
    if (!stream) return;
    vhdx_info_cleanup(&stream->info);
    xx_mem_free(stream);
}

static bool vhdx_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *vhdx_option(const xx_list_s *options, uint32_t id) {
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
 * unpack will write.  Absent means unlimited (parse() caps at 64 TiB). */
static bool vhdx_size_allowed(Abstractformat *format, const xx_list_s *options,
                              uint64_t size) {
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

static void vhdx_append(char *out, size_t size, size_t *used,
                        const char *text) {
    while (*text && *used + 1U < size) out[(*used)++] = *text++;
    out[*used] = 0;
}

static void vhdx_append_u32(char *out, size_t size, size_t *used,
                            uint32_t value) {
    char digits[12];
    size_t count = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (count != 0U && *used + 1U < size) out[(*used)++] = digits[--count];
    out[*used] = 0;
}

static uint32_t vhdx_disk_type(const vhdx_info *info) {
    if (info->differencing) return 4U;
    return (info->file_flags & 1U) != 0U ? 2U : 3U;
}

static bool vhdx_set_record(xx_archive_record *record, const vhdx_info *info) {
    char comment[160];
    size_t used = 0U;
    uint64_t stored = info->present * info->block_size;
    if (stored > (uint64_t)info->format_size) stored = info->format_size;
    comment[0] = 0;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = info->base;
    record->header_size = (int64_t)VHDX_MIB;
    record->data_offset =
        info->base + (int64_t)(info->first_data ? info->first_data : VHDX_MIB);
    record->compressed_size = (int64_t)stored;
    if (!xx_archive_record_set_original_name(record, VHDX_MEMBER_NAME) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        stored) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        info->disk_size) ||
        /* The method slot carries the disk type: 2 fixed, 3 dynamic,
         * 4 differencing (the VHD numbering). */
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        vhdx_disk_type(info)) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (info->differencing) {
        vhdx_append(comment, sizeof(comment), &used, "differencing image; parent ");
        vhdx_append(comment, sizeof(comment), &used,
                    info->parent[0] ? info->parent : "not recorded");
        vhdx_append(comment, sizeof(comment), &used,
                    "; sectors held by the parent read as zeros");
    }
    if (info->log_entries != 0U) {
        if (used) vhdx_append(comment, sizeof(comment), &used, "; ");
        vhdx_append(comment, sizeof(comment), &used, "log replayed: ");
        vhdx_append_u32(comment, sizeof(comment), &used, info->log_entries);
        vhdx_append(comment, sizeof(comment), &used,
                    info->log_entries == 1U ? " entry" : " entries");
    }
    if (used &&
        !xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, comment))
        return false;
    return true;
}

void xx_vhdx_init(xx_vhdx *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_rt_memset(archive, 0, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_VHDX_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-vhdx");
    xx_format_set_extension(&archive->format, "vhdx");
    archive->format.check_is_valid = xx_vhdx_check_is_valid;
    archive->format.handle_base_info = xx_vhdx_handle_base_info;
    archive->format.get_format_size = xx_vhdx_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_vhdx_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_vhdx_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_vhdx_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_vhdx_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_vhdx_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_vhdx_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_vhdx *xx_vhdx_create(xx_io_device *device, int64_t base_address) {
    xx_vhdx *archive = (xx_vhdx *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_vhdx_init(archive, device, base_address);
    return archive;
}

void xx_vhdx_destroy(xx_vhdx *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_vhdx_free(xx_vhdx *archive) {
    if (!archive) return;
    xx_vhdx_destroy(archive);
    xx_mem_free(archive);
}

bool xx_vhdx_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    vhdx_info info;
    (void)pd;
    if (!vhdx_parse(format, &info)) return false;
    vhdx_info_cleanup(&info);
    return true;
}

bool xx_vhdx_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    vhdx_info info;
    xx_vhdx *archive;
    bool ok;
    if (!format) return false;
    ok = vhdx_parse(format, &info);
    if (ok) {
        ok = vhdx_walk(format, &info, NULL, pd);
        vhdx_info_cleanup(&info);
    }
    if (!ok) {
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        format->is_valid = false;
        format->base_info_handled = false;
        return false;
    }
    archive = (xx_vhdx *)format;
    archive->number_of_records = 1U;
    archive->archive_end = format->base_address + (int64_t)info.format_size;
    archive->disk_size = info.disk_size;
    archive->disk_type = vhdx_disk_type(&info);
    archive->block_size = info.block_size;
    archive->logical_sector_size = info.logical;
    archive->physical_sector_size = info.physical;
    archive->blocks = info.blocks;
    archive->blocks_present = info.present;
    archive->log_entries_replayed = info.log_entries;
    format->number_of_archive_records = 1U;
    format->format_size = (int64_t)info.format_size;
    format->file_type = XX_VHDX_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_vhdx_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vhdx_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_vhdx_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vhdx_handle_base_info(format, pd))
               ? ((xx_vhdx *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_vhdx_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    vhdx_stream *stream;
    xx_archive_record_state *state;
    stream = (vhdx_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!vhdx_parse(format, &stream->info)) {
        xx_mem_free(stream);
        return NULL;
    }
    /* Every BAT entry is checked here, before anything is published. */
    if (!vhdx_walk(format, &stream->info, NULL, pd)) {
        vhdx_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        vhdx_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = vhdx_stream_free;
    state->total_records = 1U;
    if (!vhdx_copy_options(&state->options, options) ||
        !vhdx_set_record(&state->current_record, &stream->info)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_vhdx_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_vhdx_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    vhdx_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (vhdx_stream *)state->internal_state))
        return false;
    /* There is exactly one member, so the first step is always the last. */
    stream->index = 1U;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_vhdx_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    vhdx_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (vhdx_stream *)state->internal_state) ||
        stream->index != 0U || (pd && xx_pd_is_stopped(pd)))
        return false;
    if (!vhdx_size_allowed(format, &state->options, stream->info.disk_size))
        return false;
    path_option = vhdx_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return vhdx_walk(format, &stream->info, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", VHDX_MEMBER_NAME)
               : xx_str_concat(base, VHDX_MEMBER_NAME);
    if (!path) goto done;
    if (!xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    created = destination != NULL;
    if (!destination) goto done;
    result = vhdx_walk(format, &stream->info, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_vhdx_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
