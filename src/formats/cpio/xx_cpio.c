/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CPIO records are deliberately parsed from their wire fields rather than
 * compiler-packed structs.  This keeps the old ASCII, afio, and binary
 * variants portable and makes every offset/bounds check explicit.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cpio/xx_cpio.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define XX_CPIO_NEWC_HEADER_SIZE 110U
#define XX_CPIO_ODC_HEADER_SIZE 76U
#define XX_CPIO_AFIO_HEADER_SIZE 116U
#define XX_CPIO_BINARY_HEADER_SIZE 26U
#define XX_CPIO_MAX_RECORDS 100000U
#define XX_CPIO_MAX_NAME_SIZE 65536U
#define XX_CPIO_COPY_BUFFER_SIZE 65536U
#define XX_CPIO_MODE_IFMT 0170000U
#define XX_CPIO_MODE_IFREG 0100000U
#define XX_CPIO_MODE_IFDIR 0040000U

/* --- Solaris block-compressed CPIO wrapper -------------------------------
 * Solaris installation media stores its CPIO archives inside a small
 * block-compression container.  Layout, recovered from the samples:
 *
 *   0x000  19 9E 'T' 'L'   u32 le  payload_size        (rest of the 512-byte
 *                                                       block is zero)
 *   0x200  19 9E 'T' 'G'   u32 le  block_count
 *                          u32 le  block_size (0x8000)
 *          block_count x { u32 le uncompressed
 *                          u32 le compressed
 *                          u32 le offset-from-'TG' }
 *          raw Deflate (RFC1951) data for each block
 *
 * The 'TG' object is padded to a 512-byte boundary and another 'TL'/'TG'
 * pair may follow; trailing zero blocks pad the medium.  Every object
 * decompresses to a self-contained CPIO stream of its own, so each one is
 * walked separately and the members are merged. */
#define XX_CPIO_SOLARIS_BLOCK 512U
#define XX_CPIO_SOLARIS_TL_SIZE 8U
#define XX_CPIO_SOLARIS_TG_SIZE 12U
#define XX_CPIO_SOLARIS_ENTRY_SIZE 12U
#define XX_CPIO_SOLARIS_MAX_BLOCKS 262144U
#define XX_CPIO_SOLARIS_MAX_BLOCK_SIZE (16U * 1024U * 1024U)
#define XX_CPIO_SOLARIS_MAX_IMAGE ((uint64_t)256U * 1024U * 1024U)
#define XX_CPIO_SOLARIS_MAX_OBJECTS 4096U

typedef struct cpio_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t next_offset;
    uint64_t data_size;
    uint64_t mtime;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t nlink;
    uint32_t rdev;
    uint32_t check;
    xx_cpio_variant_t variant;
    bool directory;
    bool regular;
} cpio_member;

typedef struct cpio_stream_s {
    cpio_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
    xx_cpio_variant_t first_variant;
    /* Only used by the Solaris wrapper: the decompressed CPIO image, the
     * read-only device that exposes it, and where each contained CPIO
     * stream begins inside it. */
    uint8_t *image;
    size_t image_size;
    xx_io_device *image_device;
    int64_t *segments;
    size_t segment_count;
    size_t segment_capacity;
    bool wrapped;
} cpio_stream;

typedef struct cpio_writer_s {
    int64_t position;
    uint64_t count;
    bool finalized;
    bool failed;
} cpio_writer;

static void cpio_stream_free(void *pointer);
static void cpio_writer_free(void *pointer);
static void cpio_vtable_destroy(Abstractformat *self);

static bool cpio_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static uint16_t cpio_read16(const uint8_t *data, bool big_endian) {
    return big_endian ? (uint16_t)(((uint16_t)data[0] << 8U) | data[1])
                      : (uint16_t)(data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t cpio_read32(const uint8_t *data, bool big_endian) {
    uint16_t high = cpio_read16(data, big_endian);
    uint16_t low = cpio_read16(data + 2U, big_endian);
    return ((uint32_t)high << 16U) | low;
}

static bool cpio_parse_hex(const uint8_t *data, size_t size,
                           uint64_t *result) {
    uint64_t value = 0U;
    size_t index;
    if (!data || !result || size == 0U || size > 16U) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t c = data[index];
        uint64_t digit;
        if (c >= '0' && c <= '9') digit = (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (uint64_t)(c - 'a' + 10U);
        else if (c >= 'A' && c <= 'F') digit = (uint64_t)(c - 'A' + 10U);
        else return false;
        if (value > (UINT64_MAX - digit) / 16U) return false;
        value = value * 16U + digit;
    }
    *result = value;
    return true;
}

static bool cpio_parse_octal(const uint8_t *data, size_t size,
                             uint64_t *result) {
    uint64_t value = 0U;
    size_t index;
    if (!data || !result || size == 0U || size > 22U) return false;
    for (index = 0U; index < size; ++index) {
        uint64_t digit;
        if (data[index] < '0' || data[index] > '7') return false;
        digit = (uint64_t)(data[index] - '0');
        if (value > (UINT64_MAX - digit) / 8U) return false;
        value = value * 8U + digit;
    }
    *result = value;
    return true;
}

static bool cpio_add_i64(int64_t value, int64_t increment,
                         int64_t *result) {
    if (!result || value < 0 || increment < 0 ||
        value > INT64_MAX - increment) {
        return false;
    }
    *result = value + increment;
    return true;
}

static bool cpio_align_i64(int64_t value, unsigned alignment,
                           int64_t *result) {
    int64_t remainder;
    int64_t padding;
    if (!result || value < 0 || alignment == 0U) return false;
    remainder = value % (int64_t)alignment;
    padding = remainder ? (int64_t)alignment - remainder : 0;
    return cpio_add_i64(value, padding, result);
}

static bool cpio_variant_at(Abstractformat *format, int64_t relative_offset,
                            xx_cpio_variant_t *variant) {
    uint8_t magic[6];
    int64_t absolute_offset;
    if (!format || !format->device || !variant || relative_offset < 0 ||
        !cpio_add_i64(format->base_address, relative_offset,
                      &absolute_offset) ||
        !cpio_read_at(format->device, absolute_offset, magic, sizeof(magic))) {
        return false;
    }
    if (xx_rt_memcmp(magic, "070701", 6U) == 0) *variant = XX_CPIO_VARIANT_NEWC;
    else if (xx_rt_memcmp(magic, "070702", 6U) == 0) *variant = XX_CPIO_VARIANT_CRC;
    else if (xx_rt_memcmp(magic, "070707", 6U) == 0) *variant = XX_CPIO_VARIANT_ODC;
    else if (xx_rt_memcmp(magic, "070727", 6U) == 0) *variant = XX_CPIO_VARIANT_AFIO;
    else if (magic[0] == 0xC7U && magic[1] == 0x71U)
        *variant = XX_CPIO_VARIANT_BINARY_LE;
    else if (magic[0] == 0x71U && magic[1] == 0xC7U)
        *variant = XX_CPIO_VARIANT_BINARY_BE;
    else
        *variant = XX_CPIO_VARIANT_UNKNOWN;
    return true;
}

/* The newc "CRC" field is a plain byte sum.  GNU cpio accumulates it in
 * unsigned char, while the System V tools (Solaris among them) accumulate
 * in a plain, signed char, so both totals are produced and either one is
 * accepted as a match. */
static bool cpio_checksum(Abstractformat *format, int64_t offset,
                          uint64_t size, uint32_t *result,
                          uint32_t *signed_result, xx_pd_struct *pd) {
    uint8_t buffer[XX_CPIO_COPY_BUFFER_SIZE];
    uint64_t remaining = size;
    int64_t position = offset;
    uint32_t sum = 0U;
    uint32_t signed_sum = 0U;
    if (!format || !format->device || !result || !signed_result ||
        offset < 0) {
        return false;
    }
    while (remaining != 0U) {
        size_t amount = remaining > sizeof(buffer) ? sizeof(buffer) :
                                                     (size_t)remaining;
        size_t index;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !cpio_read_at(format->device, position, buffer, amount)) {
            return false;
        }
        for (index = 0U; index < amount; ++index) {
            sum += buffer[index];
            signed_sum += buffer[index] < 0x80U
                              ? (uint32_t)buffer[index]
                              : (uint32_t)0U - (uint32_t)(0x100U -
                                                          buffer[index]);
        }
        remaining -= amount;
        if (!cpio_add_i64(position, (int64_t)amount, &position)) return false;
    }
    *result = sum;
    *signed_result = signed_sum;
    return true;
}

static bool cpio_read_name(Abstractformat *format, int64_t offset,
                           uint64_t size, char **result) {
    char *name;
    size_t index;
    if (!format || !result || size == 0U || size > XX_CPIO_MAX_NAME_SIZE ||
        size > SIZE_MAX - 1U || offset < 0) {
        return false;
    }
    name = (char *)xx_mem_alloc((size_t)size + 1U);
    if (!name || !cpio_read_at(format->device, offset, name, (size_t)size)) {
        xx_mem_free(name);
        return false;
    }
    if (name[size - 1U] != '\0') {
        xx_mem_free(name);
        return false;
    }
    for (index = 0U; index + 1U < (size_t)size; ++index) {
        if (name[index] == '\0') {
            xx_mem_free(name);
            return false;
        }
    }
    name[size] = '\0';
    *result = name;
    return true;
}

static bool cpio_parse_record(Abstractformat *format, int64_t relative_offset,
                              cpio_member *member, xx_pd_struct *pd) {
    uint8_t header[XX_CPIO_AFIO_HEADER_SIZE];
    xx_cpio_variant_t variant;
    int64_t total_size;
    int64_t absolute_offset;
    int64_t header_size;
    int64_t name_offset;
    int64_t name_end;
    int64_t data_relative;
    int64_t data_end;
    int64_t next_relative;
    uint64_t name_size = 0U;
    uint64_t data_size = 0U;
    uint64_t value;
    uint64_t expected_check = 0U;
    bool has_check = false;
    bool binary = false;
    bool padded = false;
    size_t field;

    if (!format || !format->device || !member || relative_offset < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_mem_zero(member, sizeof(*member));
    total_size = xx_io_total_size(format->device);
    if (format->base_address < 0 || total_size < format->base_address ||
        relative_offset >= total_size - format->base_address ||
        !cpio_add_i64(format->base_address, relative_offset,
                      &absolute_offset) ||
        !cpio_variant_at(format, relative_offset, &variant) ||
        variant == XX_CPIO_VARIANT_UNKNOWN) {
        return false;
    }
    header_size = variant == XX_CPIO_VARIANT_NEWC ||
                          variant == XX_CPIO_VARIANT_CRC
                      ? (int64_t)XX_CPIO_NEWC_HEADER_SIZE
                      : variant == XX_CPIO_VARIANT_ODC
                            ? (int64_t)XX_CPIO_ODC_HEADER_SIZE
                            : variant == XX_CPIO_VARIANT_AFIO
                                  ? (int64_t)XX_CPIO_AFIO_HEADER_SIZE
                                  : (int64_t)XX_CPIO_BINARY_HEADER_SIZE;
    if (header_size > total_size - absolute_offset ||
        !cpio_read_at(format->device, absolute_offset, header,
                      (size_t)header_size)) {
        return false;
    }

    member->variant = variant;
    member->header_offset = absolute_offset;
    if (variant == XX_CPIO_VARIANT_NEWC || variant == XX_CPIO_VARIANT_CRC) {
        for (field = 0U; field < 13U; ++field) {
            if (!cpio_parse_hex(header + 6U + field * 8U, 8U, &value)) {
                return false;
            }
            if (field == 1U) member->mode = (uint32_t)value;
            else if (field == 2U) member->uid = (uint32_t)value;
            else if (field == 3U) member->gid = (uint32_t)value;
            else if (field == 4U) member->nlink = (uint32_t)value;
            else if (field == 5U) member->mtime = value;
            else if (field == 6U) data_size = value;
            else if (field == 11U) name_size = value;
            else if (field == 12U) expected_check = value;
        }
        has_check = variant == XX_CPIO_VARIANT_CRC;
        padded = true;
    } else if (variant == XX_CPIO_VARIANT_ODC) {
        static const size_t offsets[] = {6U, 12U, 18U, 24U, 30U,
                                         36U, 42U, 48U, 59U, 65U};
        static const size_t widths[] = {6U, 6U, 6U, 6U, 6U,
                                        6U, 6U, 11U, 6U, 11U};
        for (field = 0U; field < 10U; ++field) {
            if (!cpio_parse_octal(header + offsets[field], widths[field],
                                  &value)) return false;
            if (field == 2U) member->mode = (uint32_t)value;
            else if (field == 3U) member->uid = (uint32_t)value;
            else if (field == 4U) member->gid = (uint32_t)value;
            else if (field == 5U) member->nlink = (uint32_t)value;
            else if (field == 6U) member->rdev = (uint32_t)value;
            else if (field == 7U) member->mtime = value;
            else if (field == 8U) name_size = value;
            else if (field == 9U) data_size = value;
        }
    } else if (variant == XX_CPIO_VARIANT_AFIO) {
        if (header[30U] != 'm' || header[85U] != 'n' ||
            header[98U] != 's' || header[115U] != ':') {
            return false;
        }
        if (!cpio_parse_hex(header + 6U, 8U, &value) ||
            !cpio_parse_hex(header + 14U, 16U, &value) ||
            !cpio_parse_octal(header + 31U, 6U, &value)) return false;
        member->mode = (uint32_t)value;
        if (!cpio_parse_hex(header + 37U, 8U, &value)) return false;
        member->uid = (uint32_t)value;
        if (!cpio_parse_hex(header + 45U, 8U, &value)) return false;
        member->gid = (uint32_t)value;
        if (!cpio_parse_hex(header + 53U, 8U, &value)) return false;
        member->nlink = (uint32_t)value;
        if (!cpio_parse_hex(header + 61U, 8U, &value)) return false;
        member->rdev = (uint32_t)value;
        if (!cpio_parse_hex(header + 69U, 16U, &value)) return false;
        member->mtime = value;
        if (!cpio_parse_hex(header + 86U, 4U, &name_size) ||
            !cpio_parse_hex(header + 90U, 4U, &value) ||
            !cpio_parse_hex(header + 94U, 4U, &value) ||
            !cpio_parse_hex(header + 99U, 16U, &data_size)) return false;
    } else {
        bool big_endian = variant == XX_CPIO_VARIANT_BINARY_BE;
        if (cpio_read16(header, big_endian) != UINT16_C(0x71C7)) return false;
        member->mode = cpio_read16(header + 6U, big_endian);
        member->uid = cpio_read16(header + 8U, big_endian);
        member->gid = cpio_read16(header + 10U, big_endian);
        member->nlink = cpio_read16(header + 12U, big_endian);
        member->rdev = cpio_read16(header + 14U, big_endian);
        member->mtime = cpio_read32(header + 16U, big_endian);
        name_size = cpio_read16(header + 20U, big_endian);
        data_size = cpio_read32(header + 22U, big_endian);
        binary = true;
    }
    if (name_size == 0U || name_size > XX_CPIO_MAX_NAME_SIZE ||
        data_size > (uint64_t)INT64_MAX ||
        (has_check && expected_check > UINT32_MAX)) {
        return false;
    }
    if (!cpio_add_i64(relative_offset, header_size, &name_offset) ||
        !cpio_add_i64(name_offset, (int64_t)name_size, &name_end) ||
        name_end > total_size - format->base_address ||
        !cpio_add_i64(format->base_address, name_offset, &absolute_offset) ||
        !cpio_read_name(format, absolute_offset, name_size, &member->name)) {
        return false;
    }
    data_relative = name_end;
    if (padded && !cpio_align_i64(data_relative, 4U, &data_relative)) {
        goto fail;
    }
    if (binary && !cpio_align_i64(data_relative, 2U, &data_relative)) goto fail;
    if (!cpio_add_i64(data_relative, (int64_t)data_size, &data_end) ||
        data_end > total_size - format->base_address) {
        goto fail;
    }
    next_relative = data_end;
    if (padded && !cpio_align_i64(next_relative, 4U, &next_relative)) goto fail;
    if (binary && !cpio_align_i64(next_relative, 2U, &next_relative)) goto fail;
    if (next_relative <= relative_offset ||
        next_relative > total_size - format->base_address ||
        !cpio_add_i64(format->base_address, data_relative,
                      &member->data_offset)) {
        goto fail;
    }
    member->header_size = data_relative - relative_offset;
    member->data_size = data_size;
    member->next_offset = next_relative;
    member->directory =
        (member->mode & XX_CPIO_MODE_IFMT) == XX_CPIO_MODE_IFDIR ||
        (member->name[0] &&
         member->name[xx_str_len(member->name) - 1U] == '/');
    member->regular = !member->directory &&
                      ((member->mode & XX_CPIO_MODE_IFMT) == 0U ||
                       (member->mode & XX_CPIO_MODE_IFMT) == XX_CPIO_MODE_IFREG);
    if (has_check) {
        uint32_t calculated;
        uint32_t calculated_signed;
        if (!cpio_checksum(format, member->data_offset, data_size, &calculated,
                           &calculated_signed, pd) ||
            (calculated != (uint32_t)expected_check &&
             calculated_signed != (uint32_t)expected_check)) {
            goto fail;
        }
        member->check = (uint32_t)expected_check;
    }
    return true;
fail:
    xx_mem_free(member->name);
    member->name = NULL;
    return false;
}

static bool cpio_add_member(cpio_stream *stream, cpio_member *member) {
    cpio_member *items;
    size_t capacity;
    if (!stream || !member || !member->name) return false;
    if (stream->count == stream->capacity) {
        capacity = stream->capacity ? stream->capacity * 2U : 16U;
        if (capacity < stream->capacity ||
            capacity > SIZE_MAX / sizeof(*items)) return false;
        items = (cpio_member *)xx_mem_realloc(stream->items,
                                              capacity * sizeof(*items));
        if (!items) return false;
        stream->items = items;
        stream->capacity = capacity;
    }
    stream->items[stream->count++] = *member;
    member->name = NULL;
    return true;
}

static void cpio_stream_free(void *pointer) {
    cpio_stream *stream = (cpio_stream *)pointer;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_mem_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    if (stream->image_device) xx_io_close(stream->image_device);
    xx_mem_free(stream->image);
    xx_mem_free(stream->segments);
    xx_mem_free(stream);
}

static uint32_t cpio_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static bool cpio_solaris_present(Abstractformat *format) {
    uint8_t magic[4];
    return format && format->device && format->base_address >= 0 &&
           cpio_read_at(format->device, format->base_address, magic,
                        sizeof(magic)) &&
           magic[0] == 0x19U && magic[1] == 0x9eU && magic[2] == 'T' &&
           magic[3] == 'L';
}

static bool cpio_solaris_add_segment(cpio_stream *stream, int64_t offset) {
    int64_t *items;
    size_t capacity;
    if (!stream) return false;
    if (stream->segment_count == stream->segment_capacity) {
        capacity = stream->segment_capacity ? stream->segment_capacity * 2U
                                            : 8U;
        if (capacity < stream->segment_capacity ||
            capacity > SIZE_MAX / sizeof(*items)) return false;
        items = (int64_t *)xx_mem_realloc(stream->segments,
                                          capacity * sizeof(*items));
        if (!items) return false;
        stream->segments = items;
        stream->segment_capacity = capacity;
    }
    stream->segments[stream->segment_count++] = offset;
    return true;
}

/* Decompresses every 'TL'/'TG' object into a single in-memory image and
 * records the image offset each object starts at.  Every declared size is
 * checked against the real file size before it is used. */
static bool cpio_solaris_load(Abstractformat *format, cpio_stream *stream,
                              xx_pd_struct *pd) {
    uint8_t header[XX_CPIO_SOLARIS_TG_SIZE];
    uint8_t *table = NULL;
    int64_t total_size;
    int64_t available;
    int64_t position = 0;
    int64_t last_end = 0;
    size_t objects = 0U;
    if (!format || !format->device || !stream || format->base_address < 0) {
        return false;
    }
    total_size = xx_io_total_size(format->device);
    if (total_size < format->base_address) return false;
    available = total_size - format->base_address;
    while (position <= available - (int64_t)XX_CPIO_SOLARIS_BLOCK) {
        int64_t body;
        int64_t payload_size;
        int64_t table_size;
        int64_t padded;
        uint32_t block_count;
        uint32_t block_size;
        uint32_t entry;
        uint64_t object_output = 0U;
        uint8_t *image;
        size_t written = stream->image_size;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!cpio_read_at(format->device, format->base_address + position,
                          header, XX_CPIO_SOLARIS_TL_SIZE)) {
            goto fail;
        }
        if (header[0] == 0U && header[1] == 0U && header[2] == 0U &&
            header[3] == 0U) {
            /* Objects sit on 512-byte boundaries with zero-filled gaps. */
            position += (int64_t)XX_CPIO_SOLARIS_BLOCK;
            continue;
        }
        if (header[0] != 0x19U || header[1] != 0x9eU || header[2] != 'T' ||
            header[3] != 'L') {
            break;
        }
        payload_size = (int64_t)cpio_le32(header + 4U);
        body = position + (int64_t)XX_CPIO_SOLARIS_BLOCK;
        if (payload_size < (int64_t)XX_CPIO_SOLARIS_TG_SIZE ||
            body > available || payload_size > available - body) {
            goto fail;
        }
        if (!cpio_read_at(format->device, format->base_address + body, header,
                          XX_CPIO_SOLARIS_TG_SIZE) ||
            header[0] != 0x19U || header[1] != 0x9eU || header[2] != 'T' ||
            header[3] != 'G') {
            goto fail;
        }
        block_count = cpio_le32(header + 4U);
        block_size = cpio_le32(header + 8U);
        if (block_count == 0U || block_count > XX_CPIO_SOLARIS_MAX_BLOCKS ||
            block_size == 0U || block_size > XX_CPIO_SOLARIS_MAX_BLOCK_SIZE) {
            goto fail;
        }
        table_size = (int64_t)block_count * (int64_t)XX_CPIO_SOLARIS_ENTRY_SIZE;
        if (table_size > payload_size - (int64_t)XX_CPIO_SOLARIS_TG_SIZE) {
            goto fail;
        }
        table = (uint8_t *)xx_mem_alloc((size_t)table_size);
        if (!table ||
            !cpio_read_at(format->device,
                          format->base_address + body +
                              (int64_t)XX_CPIO_SOLARIS_TG_SIZE,
                          table, (size_t)table_size)) {
            goto fail;
        }
        for (entry = 0U; entry < block_count; ++entry) {
            const uint8_t *item = table + (size_t)entry *
                                              XX_CPIO_SOLARIS_ENTRY_SIZE;
            uint32_t plain = cpio_le32(item);
            uint32_t packed = cpio_le32(item + 4U);
            uint32_t offset = cpio_le32(item + 8U);
            if (plain == 0U || plain > block_size || packed == 0U ||
                (int64_t)offset < (int64_t)XX_CPIO_SOLARIS_TG_SIZE +
                                      table_size ||
                (int64_t)offset > payload_size ||
                (int64_t)packed > payload_size - (int64_t)offset) {
                goto fail;
            }
            object_output += plain;
            if (object_output > XX_CPIO_SOLARIS_MAX_IMAGE ||
                object_output + (uint64_t)stream->image_size >
                    XX_CPIO_SOLARIS_MAX_IMAGE) {
                goto fail;
            }
        }
        if (object_output == 0U ||
            object_output > (uint64_t)(SIZE_MAX - stream->image_size)) {
            goto fail;
        }
        image = (uint8_t *)xx_mem_realloc(stream->image,
                                          stream->image_size +
                                              (size_t)object_output);
        if (!image) goto fail;
        stream->image = image;
        if (!cpio_solaris_add_segment(stream, (int64_t)stream->image_size)) {
            goto fail;
        }
        for (entry = 0U; entry < block_count; ++entry) {
            const uint8_t *item = table + (size_t)entry *
                                              XX_CPIO_SOLARIS_ENTRY_SIZE;
            uint32_t plain = cpio_le32(item);
            uint32_t packed = cpio_le32(item + 4U);
            uint32_t offset = cpio_le32(item + 8U);
            size_t produced = 0U;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !xx_deflate_unpack_device_to_memory(
                    format->device,
                    format->base_address + body + (int64_t)offset,
                    (int64_t)packed, stream->image + written, (size_t)plain,
                    &produced, false, pd) ||
                produced != (size_t)plain) {
                goto fail;
            }
            written += (size_t)plain;
        }
        xx_mem_free(table);
        table = NULL;
        stream->image_size = written;
        if (!cpio_align_i64(payload_size, XX_CPIO_SOLARIS_BLOCK, &padded) ||
            !cpio_add_i64(body, padded, &position)) {
            goto fail;
        }
        last_end = position;
        if (++objects > XX_CPIO_SOLARIS_MAX_OBJECTS) goto fail;
    }
    if (objects == 0U || stream->image_size == 0U) goto fail;
    stream->image_device = xx_io_mem_open_ro(stream->image,
                                             stream->image_size);
    if (!stream->image_device) goto fail;
    stream->wrapped = true;
    stream->archive_size = last_end;
    return true;
fail:
    xx_mem_free(table);
    return false;
}

static bool cpio_is_trailer(const cpio_member *member) {
    return member && member->name && xx_str_cmp(member->name, "TRAILER!!!") == 0;
}

static bool cpio_parse(Abstractformat *format, cpio_stream **result,
                       xx_pd_struct *pd) {
    cpio_stream *stream;
    Abstractformat view;
    Abstractformat *source;
    int64_t total_size;
    int64_t archive_size;
    size_t segment;
    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) return false;
    total_size = xx_io_total_size(format->device);
    if (total_size < format->base_address) return false;
    stream = (cpio_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (cpio_solaris_present(format)) {
        if (!cpio_solaris_load(format, stream, pd)) goto fail;
        /* A bare view over the decompressed image: cpio_parse_record only
         * ever looks at `device` and `base_address`. */
        xx_mem_zero(&view, sizeof(view));
        view.device = stream->image_device;
        view.base_address = 0;
        source = &view;
        archive_size = (int64_t)stream->image_size;
        stream->first_variant = XX_CPIO_VARIANT_SOLARIS;
    } else {
        source = format;
        archive_size = total_size - format->base_address;
        if (archive_size < (int64_t)XX_CPIO_BINARY_HEADER_SIZE ||
            !cpio_solaris_add_segment(stream, 0)) {
            goto fail;
        }
    }
    for (segment = 0U; segment < stream->segment_count; ++segment) {
        int64_t offset = stream->segments[segment];
        int64_t limit = segment + 1U < stream->segment_count
                            ? stream->segments[segment + 1U]
                            : archive_size;
        bool trailer_seen = false;
        if (offset < 0 || offset >= limit || limit > archive_size) goto fail;
        while (offset < limit) {
            cpio_member member;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !cpio_parse_record(source, offset, &member, pd)) {
                goto fail;
            }
            if (stream->first_variant == XX_CPIO_VARIANT_UNKNOWN) {
                stream->first_variant = member.variant;
            }
            offset = member.next_offset;
            if (cpio_is_trailer(&member)) {
                bool valid = member.data_size == 0U;
                xx_mem_free(member.name);
                if (!valid) goto fail;
                trailer_seen = true;
                if (!stream->wrapped) stream->archive_size = offset;
                break;
            }
            if (stream->count >= XX_CPIO_MAX_RECORDS) {
                xx_mem_free(member.name);
                goto fail;
            }
            if (!cpio_add_member(stream, &member)) {
                xx_mem_free(member.name);
                goto fail;
            }
        }
        if (!trailer_seen) goto fail;
    }
    if (stream->archive_size <= 0) goto fail;
    *result = stream;
    return true;
fail:
    cpio_stream_free(stream);
    return false;
}

static bool cpio_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
    size_t index;
    if (!destination) return false;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *cpio_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

/* Returns an independently allocated, slash-normalized safe extraction name.
 * CPIO is historically permissive about filenames; keeping the stored name
 * visible while rejecting unsafe filesystem mappings avoids traversal through
 * absolute paths, drive names, and dot components. */
static char *cpio_safe_output_name(const char *source) {
    size_t length;
    size_t index;
    size_t component_start = 0U;
    char *name;
    if (!source || !source[0]) return NULL;
    length = xx_str_len(source);
    if (length == 0U || source[0] == '/' || source[0] == '\\' ||
        (length >= 2U && source[1] == ':') || length > XX_CPIO_MAX_NAME_SIZE) {
        return NULL;
    }
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index) {
        unsigned char c = (unsigned char)source[index];
        if (c < 0x20U || c == 0x7fU || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*') {
            xx_mem_free(name);
            return NULL;
        }
        name[index] = source[index] == '\\' ? '/' : source[index];
    }
    name[length] = '\0';
    for (index = 0U; index <= length; ++index) {
        if (index != length && name[index] != '/') continue;
        if (index == component_start) {
            /* One final slash is accepted for a directory entry. */
            if (index == length && index != 0U) break;
            xx_mem_free(name);
            return NULL;
        }
        if ((index - component_start == 1U && name[component_start] == '.') ||
            (index - component_start == 2U && name[component_start] == '.' &&
             name[component_start + 1U] == '.')) {
            xx_mem_free(name);
            return NULL;
        }
        component_start = index + 1U;
    }
    return name;
}

static bool cpio_set_record(xx_archive_record *record,
                            const cpio_member *member) {
    if (!record || !member || !member->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size <= (uint64_t)INT64_MAX
                                  ? (int64_t)member->data_size
                                  : -1;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record,
                                        XX_META_ID_COMPRESSED_SIZE,
                                        member->data_size) ||
        !xx_archive_record_set_meta_u64(record,
                                        XX_META_ID_UNCOMPRESSED_SIZE,
                                        member->data_size) ||
        !xx_archive_record_set_meta_u64(record,
                                        XX_META_ID_COMPRESSION_METHOD, 0U) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                        member->mode) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        member->mtime) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                        (uint64_t)member->variant) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                         member->directory) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false)) {
        return false;
    }
    return member->variant != XX_CPIO_VARIANT_CRC ||
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->check);
}

static bool cpio_write_exact(xx_io_device *device, const void *data,
                             size_t size, xx_pd_struct *pd) {
    const uint8_t *bytes = (const uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, bytes + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool cpio_write_padding(xx_io_device *device, int64_t position,
                               unsigned alignment, int64_t *new_position,
                               xx_pd_struct *pd) {
    static const uint8_t zeros[4] = {0U, 0U, 0U, 0U};
    int64_t aligned;
    size_t padding;
    if (!new_position || !cpio_align_i64(position, alignment, &aligned)) {
        return false;
    }
    padding = (size_t)(aligned - position);
    if (padding != 0U && !cpio_write_exact(device, zeros, padding, pd)) {
        return false;
    }
    *new_position = aligned;
    return true;
}

static bool cpio_put_hex(uint8_t *destination, size_t width, uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    size_t index;
    if (!destination || width == 0U || width > 16U ||
        (width < 16U && value >= (UINT64_C(1) << (width * 4U)))) {
        return false;
    }
    for (index = width; index != 0U; --index) {
        destination[index - 1U] = (uint8_t)digits[value & 0x0fU];
        value >>= 4U;
    }
    return value == 0U;
}

static bool cpio_copy_source(xx_io_device *source, uint64_t size,
                             xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t buffer[XX_CPIO_COPY_BUFFER_SIZE];
    uint64_t remaining = size;
    if (!source || !destination) return false;
    if (xx_io_seek64(source, 0, SEEK_SET) != 0) return false;
    while (remaining != 0U) {
        size_t amount = remaining > sizeof(buffer) ? sizeof(buffer) :
                                                     (size_t)remaining;
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (xx_io_read(source, buffer, amount) != (ssize_t)amount) {
            return false;
        }
        while (done < amount) {
            ssize_t written = xx_io_write(destination, buffer + done,
                                           amount - done);
            if (written <= 0 || (size_t)written > amount - done) return false;
            done += (size_t)written;
        }
        remaining -= amount;
    }
    return true;
}

static bool cpio_write_newc_member(Abstractformat *format,
                                   cpio_writer *writer, const char *name,
                                   uint32_t mode, uint64_t mtime,
                                   xx_io_device *source, uint64_t source_size,
                                   bool trailer, xx_pd_struct *pd) {
    uint8_t header[XX_CPIO_NEWC_HEADER_SIZE];
    uint64_t name_size;
    int64_t position;
    int64_t next;
    if (!format || !format->device || !writer || !name ||
        source_size > UINT32_MAX || writer->position < format->base_address ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    name_size = (uint64_t)xx_str_len(name) + 1U;
    if (name_size > UINT32_MAX ||
        !cpio_add_i64(writer->position, (int64_t)XX_CPIO_NEWC_HEADER_SIZE,
                      &position) ||
        !cpio_add_i64(position, (int64_t)name_size, &next)) {
        return false;
    }
    xx_mem_zero(header, sizeof(header));
    xx_mem_copy(header, "070701", 6U);
    if (!cpio_put_hex(header + 6U, 8U, trailer ? 0U : writer->count + 1U) ||
        !cpio_put_hex(header + 14U, 8U, mode) ||
        !cpio_put_hex(header + 22U, 8U, 0U) ||
        !cpio_put_hex(header + 30U, 8U, 0U) ||
        !cpio_put_hex(header + 38U, 8U, 1U) ||
        !cpio_put_hex(header + 46U, 8U, mtime) ||
        !cpio_put_hex(header + 54U, 8U, source_size) ||
        !cpio_put_hex(header + 62U, 8U, 0U) ||
        !cpio_put_hex(header + 70U, 8U, 0U) ||
        !cpio_put_hex(header + 78U, 8U, 0U) ||
        !cpio_put_hex(header + 86U, 8U, 0U) ||
        !cpio_put_hex(header + 94U, 8U, name_size) ||
        !cpio_put_hex(header + 102U, 8U, 0U) ||
        !cpio_write_exact(format->device, header, sizeof(header), pd) ||
        !cpio_write_exact(format->device, name, (size_t)name_size, pd)) {
        return false;
    }
    writer->position = next;
    if (!cpio_write_padding(format->device, writer->position, 4U,
                            &writer->position, pd)) {
        return false;
    }
    if (source_size != 0U &&
        !cpio_copy_source(source, source_size, format->device, pd)) {
        return false;
    }
    if (!cpio_add_i64(writer->position, (int64_t)source_size,
                      &writer->position) ||
        !cpio_write_padding(format->device, writer->position, 4U,
                            &writer->position, pd)) {
        return false;
    }
    return true;
}

static void cpio_writer_free(void *pointer) {
    xx_mem_free(pointer);
}

static char *cpio_record_name_utf8(const xx_archive_record *record) {
    const char *name;
    const wchar_t *wide_name;
    char *converted = NULL;
    char *safe_name;
    if (!record) return NULL;
    name = xx_archive_record_get_original_name(record);
    wide_name = xx_archive_record_get_original_name_w(record);
    if (name && name[0]) {
        converted = xx_str_dup(name);
    } else if (wide_name && wide_name[0]) {
        converted = xx_str_unicode_to_utf8(wide_name);
    }
    if (!converted) return NULL;
    safe_name = cpio_safe_output_name(converted);
    xx_str_free(converted);
    return safe_name;
}

void xx_cpio_init(xx_cpio *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_UNKNOWN;
    archive->format.file_type = XX_FILE_TYPE_CPIO;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cpio");
    xx_format_set_extension(&archive->format, "cpio");
    archive->format.check_is_valid = xx_cpio_check_is_valid;
    archive->format.handle_base_info = xx_cpio_handle_base_info;
    archive->format.get_format_size = xx_cpio_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_cpio_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_cpio_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_cpio_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_cpio_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_cpio_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_cpio_free_archive_records_reading;
    archive->format.create_archive_records_writing =
        xx_cpio_create_archive_records_writing;
    archive->format.pack_archive_record = xx_cpio_pack_archive_record;
    archive->format.finalize_archive_records_writing =
        xx_cpio_finalize_archive_records_writing;
    archive->format.free_archive_records_writing =
        xx_cpio_free_archive_records_writing;
    archive->format.destroy = cpio_vtable_destroy;
    archive->archive_end = -1;
    archive->variant = XX_CPIO_VARIANT_UNKNOWN;
}

xx_cpio *xx_cpio_create(xx_io_device *device, int64_t base_address) {
    xx_cpio *archive = (xx_cpio *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_cpio_init(archive, device, base_address);
    return archive;
}

void xx_cpio_destroy(xx_cpio *archive) {
    if (!archive) return;
    xx_format_cleanup_extra_parameters(&archive->format);
}

static void cpio_vtable_destroy(Abstractformat *self) {
    xx_cpio_destroy((xx_cpio *)self);
}

void xx_cpio_free(xx_cpio *archive) {
    if (!archive) return;
    xx_cpio_destroy(archive);
    xx_mem_free(archive);
}

bool xx_cpio_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    cpio_stream *stream = NULL;
    bool result = cpio_parse(self, &stream, pd);
    cpio_stream_free(stream);
    return result;
}

bool xx_cpio_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    cpio_stream *stream = NULL;
    xx_cpio *archive;
    int64_t archive_end;
    int64_t total_size;
    if (!self || !cpio_parse(self, &stream, pd) ||
        !cpio_add_i64(self->base_address, stream->archive_size,
                      &archive_end)) {
        if (stream) cpio_stream_free(stream);
        if (self) self->is_valid = false;
        return false;
    }
    archive = (xx_cpio *)self;
    total_size = xx_io_total_size(self->device);
    archive->number_of_records = (uint64_t)stream->count;
    archive->archive_end = archive_end;
    archive->variant = stream->first_variant;
    self->file_type = XX_FILE_TYPE_CPIO;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->number_of_archive_records = (uint64_t)stream->count;
    self->format_size = stream->archive_size;
    self->overlay_offset = archive_end < total_size ? archive_end : -1;
    self->overlay_size = archive_end < total_size ? total_size - archive_end : 0;
    self->is_valid = true;
    self->base_info_handled = true;
    cpio_stream_free(stream);
    return true;
}

int64_t xx_cpio_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_cpio_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_cpio_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_cpio_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_cpio *)self)->number_of_records;
}

xx_archive_record_state *xx_cpio_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    cpio_stream *stream = NULL;
    xx_archive_record_state *state;
    if (!self || (!self->base_info_handled &&
                  !xx_cpio_handle_base_info(self, pd)) ||
        !self->is_valid || !cpio_parse(self, &stream, pd)) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        cpio_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = cpio_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!cpio_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !cpio_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->count != 0U) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_cpio_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_cpio_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    cpio_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (cpio_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    ++stream->index;
    if (stream->index >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!cpio_set_record(&state->current_record, &stream->items[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_cpio_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    cpio_stream *stream;
    const cpio_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *safe_name = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (cpio_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    member = &stream->items[stream->index];
    if (!member->regular && !member->directory) return false;
    safe_name = cpio_safe_output_name(member->name);
    if (!safe_name) goto cleanup;
    path_option = cpio_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto cleanup;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    destination = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
                   base[xx_str_len(base) - 1U] != '\\')
                      ? xx_str_concat3(base, "/", safe_name)
                      : xx_str_concat(base, safe_name);
    if (!destination) goto cleanup;
    if (member->directory) {
        result = xx_store_create_dirs_a(destination, true);
    } else if (xx_store_create_dirs_a(destination, false)) {
        /* Wrapped members live in the decompressed image, not in the file. */
        result = xx_store_unpack_device_to_file(
            stream->image_device ? stream->image_device : self->device,
            member->data_offset, (int64_t)member->data_size, destination, pd);
    }
cleanup:
    if (destination) xx_str_free(destination);
    if (safe_name) xx_str_free(safe_name);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_cpio_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

xx_archive_write_state *xx_cpio_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_write_state *state;
    cpio_writer *writer;
    xx_cpio *archive;
    if (!self || !self->device || !self->device->write ||
        !self->device->seek || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)) ||
        xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        return NULL;
    }
    state = (xx_archive_write_state *)xx_mem_alloc(sizeof(*state));
    writer = (cpio_writer *)xx_mem_calloc(1U, sizeof(*writer));
    if (!state || !writer) {
        xx_mem_free(state);
        xx_mem_free(writer);
        return NULL;
    }
    xx_archive_write_state_init(state, self);
    if (!cpio_copy_options(&state->options, options)) {
        xx_mem_free(writer);
        xx_archive_write_state_free(state);
        return NULL;
    }
    writer->position = self->base_address;
    state->internal_state = writer;
    state->free_internal = cpio_writer_free;
    state->total_records = 0;
    archive = (xx_cpio *)self;
    archive->number_of_records = 0U;
    archive->archive_end = -1;
    archive->variant = XX_CPIO_VARIANT_NEWC;
    self->number_of_archive_records = 0U;
    self->format_size = -1;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->is_valid = false;
    self->base_info_handled = false;
    return state;
}

bool xx_cpio_pack_archive_record(Abstractformat *self,
                                 xx_archive_write_state *state,
                                 const xx_archive_record *record,
                                 xx_io_device *source_dev,
                                 xx_pd_struct *pd) {
    cpio_writer *writer;
    const xx_var *declared_size;
    const xx_var *method;
    char *name;
    int64_t source_size_i64 = 0;
    uint64_t source_size = 0U;
    uint32_t mode;
    uint64_t mtime;
    bool directory;
    if (!self || !state || state->format != self || !record ||
        !(writer = (cpio_writer *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    if (writer->finalized || writer->failed || state->has_record ||
        writer->count >= XX_CPIO_MAX_RECORDS) {
        return false;
    }
    method = xx_archive_record_find_meta(record,
                                         XX_META_ID_COMPRESSION_METHOD);
    if ((method && xx_var_get_u64(method) != 0U) ||
        xx_archive_record_get_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                        false)) {
        return false;
    }
    name = cpio_record_name_utf8(record);
    if (!name) return false;
    directory = xx_archive_record_get_meta_bool(record,
                                                 XX_META_ID_IS_FOLDER, false) ||
                name[xx_str_len(name) - 1U] == '/';
    if (directory && source_dev) {
        xx_str_free(name);
        return false;
    }
    declared_size = xx_archive_record_find_meta(
        record, XX_META_ID_UNCOMPRESSED_SIZE);
    if (!directory && source_dev) {
        source_size_i64 = xx_io_total_size(source_dev);
        if (source_size_i64 < 0 ||
            xx_io_seek64(source_dev, 0, SEEK_SET) != 0) {
            xx_str_free(name);
            return false;
        }
        source_size = (uint64_t)source_size_i64;
        if (declared_size && xx_var_get_u64(declared_size) != source_size) {
            xx_str_free(name);
            return false;
        }
    } else if (!directory && declared_size &&
               xx_var_get_u64(declared_size) != 0U) {
        xx_str_free(name);
        return false;
    }
    if (source_size > UINT32_MAX) {
        xx_str_free(name);
        return false;
    }
    mode = (uint32_t)xx_archive_record_get_meta_u64(
        record, XX_META_ID_ATTRIBUTES, directory ? 0755U : 0644U);
    mode = (mode & 07777U) |
           (directory ? XX_CPIO_MODE_IFDIR : XX_CPIO_MODE_IFREG);
    mtime = xx_archive_record_get_meta_u64(record, XX_META_ID_TIMESTAMP, 0U);
    state->has_record = true;
    if (!cpio_write_newc_member(self, writer, name, mode, mtime,
                                directory ? NULL : source_dev, source_size,
                                false, pd)) {
        state->has_record = false;
        writer->failed = true;
        xx_str_free(name);
        return false;
    }
    state->has_record = false;
    ++writer->count;
    state->current_index = (int64_t)writer->count - 1;
    state->total_records = (int64_t)writer->count;
    xx_str_free(name);
    return true;
}

bool xx_cpio_finalize_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd) {
    cpio_writer *writer;
    xx_cpio *archive;
    int64_t total_size;
    if (!self || !state || state->format != self || state->has_record ||
        !(writer = (cpio_writer *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd)) || writer->finalized || writer->failed) {
        return false;
    }
    if (!cpio_write_newc_member(self, writer, "TRAILER!!!", 0U, 0U, NULL,
                                0U, true, pd)) {
        writer->failed = true;
        return false;
    }
    writer->finalized = true;
    archive = (xx_cpio *)self;
    archive->number_of_records = writer->count;
    archive->archive_end = writer->position;
    archive->variant = XX_CPIO_VARIANT_NEWC;
    self->number_of_archive_records = writer->count;
    self->format_size = writer->position - self->base_address;
    total_size = xx_io_total_size(self->device);
    self->overlay_offset = writer->position < total_size ? writer->position : -1;
    self->overlay_size = writer->position < total_size ?
                             total_size - writer->position : 0;
    self->is_valid = true;
    /* A subsequent read pass validates the emitted archive and rebuilds the
     * ordinary reader state from the finalized stream. */
    self->base_info_handled = false;
    return true;
}

void xx_cpio_free_archive_records_writing(Abstractformat *self,
                                          xx_archive_write_state *state) {
    (void)self;
    xx_archive_write_state_free(state);
}
