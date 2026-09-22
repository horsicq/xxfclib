/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Microsoft Virtual Hard Disk, DYNAMIC variant.  Everything in a VHD is BIG
 * endian, which is unusual enough to be a detection aid in itself.
 *
 * Footer (the LAST 512 bytes, mirrored at offset 0)
 *   +0x00  "conectix"
 *   +0x08  u32  features; bit 1 is reserved-and-always-set
 *   +0x0c  u32  file format version, 0x00010000
 *   +0x10  u64  offset of the dynamic-disk header
 *   +0x18  u32  creation time
 *   +0x30  u64  current disk size
 *   +0x3c  u32  disk type; 2 fixed, 3 dynamic, 4 differencing
 *   +0x40  u32  one's-complement checksum over the footer
 *
 * Dynamic-disk header (1024 bytes at that offset)
 *   +0x00  "cxsparse"
 *   +0x08  u64  0xffffffffffffffff (no next header)
 *   +0x10  u64  BAT offset
 *   +0x18  u32  header version, 0x00010000
 *   +0x1c  u32  max table entries   +0x20  u32  block size
 *   +0x24  u32  one's-complement checksum
 *   +0x28  16   parent UUID; non-zero means a differencing image
 *
 * Each BAT entry is the 512-byte sector at which a block starts, or
 * 0xffffffff for an unallocated block.  A block opens with a sector
 * allocation bitmap rounded up to 512 bytes, MSB first, and only the
 * sectors whose bit is set carry data; the rest read back as zero.
 *
 * Ported from XArchive diskimages/xvirtualdiskarchive.cpp (parseVHD); U3
 * implements the same format as archive/12 (class mfa, VMT 0x0049bef8).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vhddynamic/xx_vhddynamic.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef VHDDYNAMIC
#define XX_VHDDYNAMIC_FILE_TYPE XX_FILE_TYPE_VHDDYNAMIC
#else
#define XX_VHDDYNAMIC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define VHDDYNAMIC_MAX_MEMBERS 16U

/* One enumerated member.  The aux slots carry whatever the format needs to
 * rebuild the member later without re-parsing the container. */
typedef struct vhddynamic_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
    uint64_t timestamp;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
    uint32_t method;
    uint32_t crc32;
    uint32_t attributes;
    uint32_t flags;
    bool has_crc;
    bool encrypted;
    bool folder;
} vhddynamic_member;

typedef struct vhddynamic_stream_s {
    vhddynamic_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint64_t aux0;
    uint64_t aux1;
    uint64_t aux2;
} vhddynamic_stream;

static uint16_t vhddynamic_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t vhddynamic_le32(const uint8_t *b) {
    return (uint32_t)vhddynamic_le16(b) | ((uint32_t)vhddynamic_le16(b + 2U) << 16U);
}

static uint64_t vhddynamic_le64(const uint8_t *b) {
    return (uint64_t)vhddynamic_le32(b) | ((uint64_t)vhddynamic_le32(b + 4U) << 32U);
}

static uint32_t vhddynamic_be32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24U) | ((uint32_t)b[1] << 16U) |
           ((uint32_t)b[2] << 8U) | (uint32_t)b[3];
}

static uint64_t vhddynamic_be64(const uint8_t *b) {
    return ((uint64_t)vhddynamic_be32(b) << 32U) | (uint64_t)vhddynamic_be32(b + 4U);
}

static bool vhddynamic_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool vhddynamic_write_all(xx_io_device *device, const void *data, size_t size,
                          xx_pd_struct *pd) {
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
static bool vhddynamic_copy_range(xx_io_device *source, int64_t offset, uint64_t size,
                           xx_io_device *destination, xx_pd_struct *pd) {
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

/* Emit `size` zero bytes: the filler every sparse disk image needs. */
static bool vhddynamic_write_zeros(xx_io_device *destination, uint64_t size,
                            xx_pd_struct *pd) {
    uint8_t buffer[0x8000];
    uint64_t left = size;
    if (!destination) return true;
    xx_mem_zero(buffer, sizeof(buffer));
    while (left != 0U) {
        size_t want = left < sizeof(buffer) ? (size_t)left : sizeof(buffer);
        if (!vhddynamic_write_all(destination, buffer, want, pd)) return false;
        left -= want;
    }
    return true;
}

/* Reader-owned names are built here, never taken from the container, so they
 * are safe by construction. */
static char *vhddynamic_make_name(const char *prefix, int64_t index,
                           const char *suffix) {
    char buffer[96];
    size_t used = 0U;
    size_t at;
    char *result;
    for (at = 0U; prefix && prefix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = prefix[at];
    }
    if (index >= 0) {
        char digits[24];
        size_t count = 0U;
        int64_t value = index;
        do {
            digits[count++] = (char)('0' + (int)(value % 10));
            value /= 10;
        } while (value != 0 && count < sizeof(digits));
        while (count < 4U && count < sizeof(digits)) digits[count++] = '0';
        while (count != 0U) {
            if (used >= sizeof(buffer) - 1U) return NULL;
            buffer[used++] = digits[--count];
        }
    }
    for (at = 0U; suffix && suffix[at]; ++at) {
        if (used >= sizeof(buffer) - 1U) return NULL;
        buffer[used++] = suffix[at];
    }
    buffer[used] = 0;
    result = (char *)xx_mem_alloc(used + 1U);
    if (!result) return NULL;
    xx_mem_copy(result, buffer, used + 1U);
    return result;
}

/* Names that DO come from the container are normalized here: separators are
 * unified, traversal components are removed and anything a filesystem would
 * choke on becomes '_'. */
static char *vhddynamic_clean_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U;
    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (input < size) {
        size_t start, end, component_start;
        while (input < size && (bytes[input] == '/' || bytes[input] == '\\'))
            ++input;
        start = input;
        while (input < size && bytes[input] != '/' && bytes[input] != '\\')
            ++input;
        end = input;
        if (end == start || (end - start == 1U && bytes[start] == '.'))
            continue;
        if (end - start == 2U && bytes[start] == '.' &&
            bytes[start + 1U] == '.') {
            if (output != 0U) {
                while (output != 0U && name[output - 1U] != '/') --output;
                if (output != 0U) --output;
            }
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component_start = output;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|' || c == 0U)
                name[output++] = '_';
            else
                name[output++] = (char)c;
        }
        while (output > component_start &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component_start) name[output++] = '_';
    }
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static bool vhddynamic_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void vhddynamic_stream_free(void *opaque) {
    vhddynamic_stream *stream = (vhddynamic_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool vhddynamic_add_member(vhddynamic_stream *stream, const vhddynamic_member *member) {
    vhddynamic_member *grown;
    if (!stream || !member || stream->count >= VHDDYNAMIC_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (vhddynamic_member *)xx_mem_realloc(
        stream->items, (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define VHDDYNAMIC_FOOTER_SIZE 512
#define VHDDYNAMIC_DYNHDR_SIZE 1024
#define VHDDYNAMIC_MAX_DISK ((uint64_t)1U << 42U)
#define VHDDYNAMIC_MAX_ENTRIES (1U << 22U)

/* One's-complement checksum over the structure with its own checksum field
 * treated as zero.  Both the footer and the dynamic-disk header carry one and
 * both are verified; they are the only thing separating a real VHD from a
 * file whose last sector happens to start with "conectix". */
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

static bool vhddynamic_power_of_two(uint64_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static bool vhddynamic_parse(Abstractformat *format,
                             vhddynamic_stream **result) {
    uint8_t footer[VHDDYNAMIC_FOOTER_SIZE];
    uint8_t front[VHDDYNAMIC_FOOTER_SIZE];
    uint8_t dynamic[VHDDYNAMIC_DYNHDR_SIZE];
    vhddynamic_stream *stream = NULL;
    vhddynamic_member member;
    int64_t total, size;
    uint64_t disk_size, header_offset, bat_offset, bat_bytes;
    uint32_t features, disk_type, entries, block_size, needed;
    size_t at;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < VHDDYNAMIC_FOOTER_SIZE * 3 || (size % VHDDYNAMIC_FOOTER_SIZE))
        return false;
    if (!vhddynamic_read_at(format->device,
                            format->base_address + size -
                                VHDDYNAMIC_FOOTER_SIZE,
                            footer, sizeof(footer)) ||
        xx_rt_memcmp(footer, "conectix", 8U) != 0)
        return false;

    features = vhddynamic_be32(footer + 8U);
    if ((features & ~UINT32_C(3)) != 0U || (features & 2U) == 0U) return false;
    if (vhddynamic_be32(footer + 12U) != 0x00010000U) return false;
    if (!vhddynamic_checksum_ok(footer, sizeof(footer), 64U)) return false;

    disk_size = vhddynamic_be64(footer + 48U);
    if (disk_size == 0U || disk_size > VHDDYNAMIC_MAX_DISK ||
        (disk_size % 512U) != 0U)
        return false;
    disk_type = vhddynamic_be32(footer + 60U);
    /* Fixed images are a different reader and differencing images need a
     * separately supplied parent, so only type 3 belongs here. */
    if (disk_type != 3U) return false;

    header_offset = vhddynamic_be64(footer + 16U);
    if (header_offset < 512U || (header_offset % 512U) != 0U ||
        header_offset > (uint64_t)(size - VHDDYNAMIC_FOOTER_SIZE) ||
        VHDDYNAMIC_DYNHDR_SIZE >
            (uint64_t)(size - VHDDYNAMIC_FOOTER_SIZE) - header_offset)
        return false;
    if (!vhddynamic_read_at(format->device,
                            format->base_address + (int64_t)header_offset,
                            dynamic, sizeof(dynamic)) ||
        xx_rt_memcmp(dynamic, "cxsparse", 8U) != 0 ||
        vhddynamic_be64(dynamic + 8U) != UINT64_C(0xffffffffffffffff) ||
        vhddynamic_be32(dynamic + 24U) != 0x00010000U ||
        !vhddynamic_checksum_ok(dynamic, sizeof(dynamic), 36U))
        return false;
    /* A non-zero parent UUID means a differencing image. */
    for (at = 40U; at < 56U; ++at)
        if (dynamic[at] != 0U) return false;
    /* The footer is mirrored at offset 0; a file where it is not is either
     * truncated or not a VHD at all. */
    if (!vhddynamic_read_at(format->device, format->base_address, front,
                            sizeof(front)) ||
        xx_rt_memcmp(front, footer, sizeof(footer)) != 0)
        return false;

    bat_offset = vhddynamic_be64(dynamic + 16U);
    entries = vhddynamic_be32(dynamic + 28U);
    block_size = vhddynamic_be32(dynamic + 32U);
    if (!vhddynamic_power_of_two(block_size) || block_size < 512U ||
        block_size > 32U * 1024U * 1024U || entries == 0U ||
        entries > VHDDYNAMIC_MAX_ENTRIES || (bat_offset % 512U) != 0U)
        return false;
    needed = (uint32_t)((disk_size - 1U) / block_size + 1U);
    if (entries < needed) return false;
    bat_bytes = (((uint64_t)entries * 4U) + 511U) & ~(uint64_t)511U;
    if (bat_offset > (uint64_t)(size - VHDDYNAMIC_FOOTER_SIZE) ||
        bat_bytes > (uint64_t)(size - VHDDYNAMIC_FOOTER_SIZE) - bat_offset)
        return false;

    stream = (vhddynamic_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->aux0 = bat_offset;
    stream->aux1 = block_size;
    stream->aux2 = entries;

    xx_mem_zero(&member, sizeof(member));
    member.name = vhddynamic_make_name("disk", -1, ".img");
    if (!member.name) goto fail;
    member.header_offset = format->base_address + size - VHDDYNAMIC_FOOTER_SIZE;
    member.header_size = VHDDYNAMIC_FOOTER_SIZE;
    member.data_offset = format->base_address;
    member.packed_size = size;
    member.unpacked_size = disk_size;
    member.method = 1U; /* dynamic: BAT + per-block allocation bitmap */
    member.timestamp = vhddynamic_be32(footer + 24U);
    if (!vhddynamic_add_member(stream, &member)) {
        xx_mem_free(member.name);
        goto fail;
    }
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    vhddynamic_stream_free(stream);
    return false;
}

static bool vhddynamic_write_member(Abstractformat *format,
                                    vhddynamic_stream *stream,
                                    const vhddynamic_member *member,
                                    xx_io_device *destination,
                                    xx_pd_struct *pd) {
    uint8_t *table = NULL;
    uint8_t *bitmap = NULL;
    uint64_t block_size, entries, bat_offset, produced = 0U;
    uint64_t sectors, bitmap_bytes, index;
    int64_t limit;
    bool result = false;

    if (!format || !stream || !member) return false;
    bat_offset = stream->aux0;
    block_size = stream->aux1;
    entries = stream->aux2;
    if (block_size < 512U || entries == 0U) return false;
    limit = member->packed_size - VHDDYNAMIC_FOOTER_SIZE;
    sectors = block_size / 512U;
    bitmap_bytes = (((sectors + 7U) / 8U) + 511U) & ~(uint64_t)511U;
    if (entries > (uint64_t)SIZE_MAX / 4U ||
        bitmap_bytes > (uint64_t)SIZE_MAX)
        return false;

    table = (uint8_t *)xx_mem_alloc((size_t)entries * 4U);
    bitmap = (uint8_t *)xx_mem_alloc((size_t)bitmap_bytes);
    if (!table || !bitmap ||
        !vhddynamic_read_at(format->device,
                            member->data_offset + (int64_t)bat_offset, table,
                            (size_t)entries * 4U))
        goto done;

    for (index = 0U; index < entries && produced < member->unpacked_size;
         ++index) {
        uint32_t entry = vhddynamic_be32(table + index * 4U);
        uint64_t left = member->unpacked_size - produced;
        uint64_t output = left < block_size ? left : block_size;
        uint64_t offset, sector;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (entry == 0xffffffffU) {
            if (!vhddynamic_write_zeros(destination, output, pd)) goto done;
            produced += output;
            continue;
        }
        offset = (uint64_t)entry * 512U;
        if (offset > (uint64_t)limit ||
            bitmap_bytes + block_size > (uint64_t)limit - offset)
            goto done;
        if (!vhddynamic_read_at(format->device,
                                member->data_offset + (int64_t)offset, bitmap,
                                (size_t)bitmap_bytes))
            goto done;
        for (sector = 0U; sector * 512U < output; ++sector) {
            bool present = (bitmap[sector / 8U] &
                            (uint8_t)(0x80U >> (sector % 8U))) != 0U;
            if (present) {
                if (!vhddynamic_copy_range(
                        format->device,
                        member->data_offset + (int64_t)(offset + bitmap_bytes +
                                                        sector * 512U),
                        512U, destination, pd))
                    goto done;
            } else if (!vhddynamic_write_zeros(destination, 512U, pd)) {
                goto done;
            }
        }
        produced += output;
    }
    if (produced != member->unpacked_size) goto done;
    result = true;
done:
    if (table) xx_mem_free(table);
    if (bitmap) xx_mem_free(bitmap);
    return result;
}

static bool vhddynamic_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *vhddynamic_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool vhddynamic_set_record(xx_archive_record *record,
                           const vhddynamic_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc32) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           member->encrypted) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->folder);
}

void xx_vhddynamic_init(xx_vhddynamic *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
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

xx_vhddynamic *xx_vhddynamic_create(xx_io_device *device, int64_t base_address) {
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
    vhddynamic_stream *stream;
    (void)pd;
    if (!vhddynamic_parse(format, &stream)) return false;
    vhddynamic_stream_free(stream);
    return true;
}

bool xx_vhddynamic_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    vhddynamic_stream *stream;
    xx_vhddynamic *archive;
    (void)pd;
    if (!format || !vhddynamic_parse(format, &stream)) {
        if (format) {
            format->format_size = -1;
            format->number_of_archive_records = 0U;
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_vhddynamic *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_VHDDYNAMIC_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    vhddynamic_stream_free(stream);
    return true;
}

int64_t xx_vhddynamic_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
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
    if (!vhddynamic_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        vhddynamic_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = vhddynamic_stream_free;
    state->total_records = stream->count;
    if (!vhddynamic_copy_options(&state->options, options) ||
        !vhddynamic_set_record(&state->current_record, &stream->items[0])) {
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
        !(stream = (vhddynamic_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        vhddynamic_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_vhddynamic_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    vhddynamic_stream *stream;
    vhddynamic_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (vhddynamic_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!vhddynamic_safe_output_name(member->name)) return false;
    path_option = vhddynamic_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return vhddynamic_write_member(format, stream, member, NULL, pd);
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path) goto done;
    if (member->folder) {
        result = xx_store_create_dirs_a(path, true);
        goto done;
    }
    if (!xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    if (!destination) goto done;
    result = vhddynamic_write_member(format, stream, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    destination = NULL;
done:
    if (!result && path && !member->folder) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_vhddynamic_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
