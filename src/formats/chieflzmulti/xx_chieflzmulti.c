/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ChiefLZ "Multiple" archives. The codec is the one ChiefLZ Single uses.
 *
 *   header, 0x2b bytes at offset 0:
 *     0x00  u8       0x0c, the length of the magic that follows
 *     0x01  12       "\x04\x0dChfLZ_2\x05\x06\x04"
 *     0x13  u32 LE   unused
 *     0x17  u32 LE   entry count, 1 .. 0xfffff
 *     0x1b  u32 LE   unused
 *     0x1f  u32 LE   unused
 *     0x23  u32 LE   total name bytes, 1 .. 0xffffff
 *
 *   0x53 bytes are then SKIPPED, so the directory starts at 0x7e:
 *     count entries of 0x29 bytes, immediately followed by every name packed
 *     end to end. Entry i consumes name_length bytes in entry order and the
 *     lengths must sum to the declared total exactly.
 *
 *   entry, 0x29 bytes:
 *     0x00  u8       kind; 0 selects the parent index at 0x01, else 0x03
 *     0x01  u16 LE   parent index (kind 0)
 *     0x03  u16 LE   parent index (kind != 0)
 *     0x0f  u32 LE   packed size
 *     0x13  u32 LE   unpacked size
 *     0x17  u16 LE   DOS time
 *     0x19  u16 LE   DOS date
 *     0x1b  u32 LE   attributes (kind 0 only)
 *     0x1f  u32 LE   ~CRC32 of the unpacked data
 *     0x23  u8       name length, never zero
 *     0x28  u8       method: 2 stored, 3 undocumented, 4 ChiefLZ
 *
 *   member data starts at 0x7e + count * 0x29 + total name bytes; each member
 *   advances the cursor by its PACKED size.
 *
 * A parent index of 0 means the root; any other value is the index plus one,
 * so paths are resolved by walking the parent chain. Method 3 exists in the
 * reference implementation but appears in no known archive, so it is reported
 * in the listing and refused at extraction rather than guessed at.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/chieflzmulti/xx_chieflzmulti.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/chieflz/xx_chieflz.h"

#include <stdio.h>

#define XX_CHIEFLZMULTI_COPY_CHUNK (64 * 1024)

typedef struct xx_chieflzmulti_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_chieflzmulti_member;

typedef struct xx_chieflzmulti_stream_s {
    xx_chieflzmulti_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_chieflzmulti_stream;

static void xx_chieflzmulti_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_chieflzmulti_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_chieflzmulti_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_chieflzmulti_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_chieflzmulti_stream_free(void *pointer) {
    xx_chieflzmulti_stream *stream = (xx_chieflzmulti_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_chieflzmulti_add(xx_chieflzmulti_stream *stream,
                          const xx_chieflzmulti_member *member) {
    xx_chieflzmulti_member *grown = (xx_chieflzmulti_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_CHIEFLZMULTI_HEADER_SIZE 0x2b
#define XX_CHIEFLZMULTI_DIRECTORY_OFFSET 0x7e
#define XX_CHIEFLZMULTI_ENTRY_SIZE 0x29
#define XX_CHIEFLZMULTI_METHOD_STORED 2U
#define XX_CHIEFLZMULTI_METHOD_UNKNOWN3 3U
#define XX_CHIEFLZMULTI_METHOD_CHIEFLZ 4U
#define XX_CHIEFLZMULTI_MAX_MEMBERS 0x100000
#define XX_CHIEFLZMULTI_MAX_NAME_BYTES 0x1000000
#define XX_CHIEFLZMULTI_MAX_PATH_DEPTH 64
#define XX_CHIEFLZMULTI_MAX_DECODED ((int64_t)0x10000000)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_chieflzmulti_le16(const uint8_t *data);
static uint32_t xx_chieflzmulti_le32(const uint8_t *data);
static char *xx_chieflzmulti_build_path(const uint8_t *directory, const uint32_t *name_offset, const uint8_t *name_length, const int32_t *parents, int32_t count, int32_t index);
static xx_chieflzmulti_stream *xx_chieflzmulti_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_chieflzmulti_decode(Abstractformat *self, const xx_chieflzmulti_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


static uint16_t xx_chieflzmulti_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_chieflzmulti_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Build "parent/.../own" for entry index by walking the parent chain twice:
 * once to measure, once to fill from the end. The depth cap makes a cyclic
 * chain terminate rather than loop, and the "parent != self" test rejects the
 * one-step cycle the cap alone would let run to 64. */
static char *xx_chieflzmulti_build_path(const uint8_t *directory,
                                        const uint32_t *name_offset,
                                        const uint8_t *name_length,
                                        const int32_t *parents, int32_t count,
                                        int32_t index) {
    char *path;
    int64_t total = 0;
    int64_t cursor;
    int32_t walk;
    int32_t depth;
    uint32_t copy;

    walk = index;
    depth = 0;
    for (;;) {
        total += (int64_t)name_length[walk];
        walk = parents[walk];
        if (walk < 0 || walk >= count || walk == index) break;
        if (depth >= XX_CHIEFLZMULTI_MAX_PATH_DEPTH) break;
        ++depth;
        total += 1; /* the '/' separator */
    }
    if (total <= 0) return NULL;

    path = (char *)xx_mem_alloc((size_t)total + 1U);
    if (!path) return NULL;
    path[total] = '\0';

    cursor = total;
    walk = index;
    depth = 0;
    for (;;) {
        copy = (uint32_t)name_length[walk];
        cursor -= (int64_t)copy;
        if (cursor < 0) {
            xx_str_free(path);
            return NULL;
        }
        xx_rt_memcpy(path + cursor, directory + name_offset[walk],
                     (size_t)copy);
        walk = parents[walk];
        if (walk < 0 || walk >= count || walk == index) break;
        if (depth >= XX_CHIEFLZMULTI_MAX_PATH_DEPTH) break;
        ++depth;
        --cursor;
        if (cursor < 0) {
            xx_str_free(path);
            return NULL;
        }
        path[cursor] = '/';
    }
    if (cursor != 0) {
        xx_str_free(path);
        return NULL;
    }
    return path;
}

static xx_chieflzmulti_stream *xx_chieflzmulti_parse(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    static const uint8_t magic[13] = {0x0cU, 0x04U, 0x0dU, (uint8_t)'C',
                                      (uint8_t)'h', (uint8_t)'f',
                                      (uint8_t)'L', (uint8_t)'Z',
                                      (uint8_t)'_', (uint8_t)'2',
                                      0x05U, 0x06U, 0x04U};
    xx_chieflzmulti_stream *stream = NULL;
    xx_chieflzmulti_member member;
    uint8_t header[XX_CHIEFLZMULTI_HEADER_SIZE];
    uint8_t *directory = NULL;
    uint32_t *name_offset = NULL;
    uint8_t *name_length = NULL;
    int32_t *parents = NULL;
    int64_t total;
    int64_t span;
    int64_t entries_size;
    int64_t directory_size;
    int64_t name_position;
    int64_t name_remaining;
    int64_t data_offset;
    int64_t packed;
    int64_t unpacked;
    int64_t count64;
    int64_t name_bytes;
    int32_t count;
    int32_t index;
    uint32_t raw;
    uint16_t parent_field;
    const uint8_t *entry;
    char *name = NULL;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)XX_CHIEFLZMULTI_DIRECTORY_OFFSET) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_chieflzmulti_read_at(self, self->base_address, header,
                                 sizeof(header))) {
        return NULL;
    }

    /* Thirteen bytes of magic, the first of which is the length of the twelve
     * that follow. This is the format's whole defence against a false
     * positive: nothing else in the header is constrained to a literal value,
     * and the two counts below only bound a range. Shortening the compare to
     * the "ChfLZ_2" text would match any file that merely mentions it. */
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;

    raw = xx_chieflzmulti_le32(header + 0x17);
    if (raw > 0x7fffffffU) return NULL;
    count64 = (int64_t)raw;
    raw = xx_chieflzmulti_le32(header + 0x23);
    if (raw > 0x7fffffffU) return NULL;
    name_bytes = (int64_t)raw;
    if (count64 <= 0 || count64 >= (int64_t)XX_CHIEFLZMULTI_MAX_MEMBERS) {
        return NULL;
    }
    if (name_bytes <= 0 ||
        name_bytes >= (int64_t)XX_CHIEFLZMULTI_MAX_NAME_BYTES) {
        return NULL;
    }
    count = (int32_t)count64;

    entries_size = count64 * (int64_t)XX_CHIEFLZMULTI_ENTRY_SIZE;
    directory_size = entries_size + name_bytes;
    if (!xx_chieflzmulti_range_within(
            span, (int64_t)XX_CHIEFLZMULTI_DIRECTORY_OFFSET,
            directory_size)) {
        return NULL;
    }

    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!directory) return NULL;
    if (!xx_chieflzmulti_read_at(
            self, self->base_address + XX_CHIEFLZMULTI_DIRECTORY_OFFSET,
            directory, (size_t)directory_size)) {
        goto fail;
    }

    name_offset = (uint32_t *)xx_mem_alloc((size_t)count64 * sizeof(uint32_t));
    name_length = (uint8_t *)xx_mem_alloc((size_t)count64);
    parents = (int32_t *)xx_mem_alloc((size_t)count64 * sizeof(int32_t));
    if (!name_offset || !name_length || !parents) goto fail;

    stream = (xx_chieflzmulti_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));

    name_position = entries_size;
    name_remaining = name_bytes;
    data_offset = (int64_t)XX_CHIEFLZMULTI_DIRECTORY_OFFSET + directory_size;

    /* First pass: validate every entry and record where its own name sits.
     * Paths cannot be resolved yet because an entry may name a parent that
     * appears later in the table. */
    for (index = 0; index < count; ++index) {
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        entry = directory + (int64_t)index * XX_CHIEFLZMULTI_ENTRY_SIZE;

        /* A zero-length name is impossible in this format, and a length that
         * overruns what is left of the name block means the table and the
         * block disagree - either is a rejection. */
        if (entry[0x23] == 0U) goto fail;
        if ((int64_t)entry[0x23] > name_remaining) goto fail;
        name_offset[index] = (uint32_t)name_position;
        name_length[index] = entry[0x23];
        {
            uint32_t scan;
            for (scan = 0U; scan < (uint32_t)entry[0x23]; ++scan) {
                /* Control bytes never appear in a stored name. Bytes above
                 * 0x7e are accepted: the names are DOS OEM text and accented
                 * characters are ordinary there. */
                if (directory[name_position + (int64_t)scan] < 0x20U) {
                    goto fail;
                }
            }
        }
        name_position += (int64_t)entry[0x23];
        name_remaining -= (int64_t)entry[0x23];

        raw = xx_chieflzmulti_le32(entry + 0x0f);
        if (raw > 0x7fffffffU) goto fail;
        packed = (int64_t)raw;
        raw = xx_chieflzmulti_le32(entry + 0x13);
        if (raw > 0x7fffffffU) goto fail;
        unpacked = (int64_t)raw;

        /* Members are laid end to end with no length field to resynchronise
         * on, so one member claiming more bytes than the file holds makes
         * every later member's offset meaningless. Reject the archive. */
        if (!xx_chieflzmulti_range_within(span, data_offset, packed)) {
            goto fail;
        }

        /* kind 0 keeps the parent index at 0x01, every other kind at 0x03. */
        parent_field = (entry[0] == 0U)
                           ? xx_chieflzmulti_le16(entry + 0x01)
                           : xx_chieflzmulti_le16(entry + 0x03);
        /* 0 is the root; anything else is stored one greater than the index. */
        parents[index] = (parent_field == 0U)
                             ? -1
                             : ((int32_t)parent_field - 1);

        data_offset += packed;
    }

    /* The name lengths must consume the declared block exactly. A block with
     * bytes left over means the reader and the writer disagree about the
     * table, which is the cheapest way to catch a near-miss container. */
    if (name_remaining != 0) goto fail;

    /* Second pass: resolve paths and publish. */
    data_offset = (int64_t)XX_CHIEFLZMULTI_DIRECTORY_OFFSET + directory_size;
    for (index = 0; index < count; ++index) {
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        entry = directory + (int64_t)index * XX_CHIEFLZMULTI_ENTRY_SIZE;
        packed = (int64_t)xx_chieflzmulti_le32(entry + 0x0f);
        unpacked = (int64_t)xx_chieflzmulti_le32(entry + 0x13);

        name = xx_chieflzmulti_build_path(directory, name_offset, name_length,
                                          parents, count, index);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address +
                               XX_CHIEFLZMULTI_DIRECTORY_OFFSET +
                               (int64_t)index * XX_CHIEFLZMULTI_ENTRY_SIZE;
        member.header_size = (int64_t)XX_CHIEFLZMULTI_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = packed;
        member.uncompressed_size = unpacked;
        member.method = (uint32_t)entry[0x28];
        member.timestamp =
            ((uint64_t)xx_chieflzmulti_le16(entry + 0x19) << 16) |
            (uint64_t)xx_chieflzmulti_le16(entry + 0x17);
        /* The container marks no entry as a directory: a folder exists only
         * as something another entry names as its parent. */
        member.is_folder = false;

        if (!xx_chieflzmulti_add(stream, &member)) goto fail;
        name = NULL;

        data_offset += packed;
    }

    /* The last member's end, clamped: trailing bytes past it are not ours. */
    stream->archive_size = (data_offset < span) ? data_offset : span;

    xx_mem_free(parents);
    xx_mem_free(name_length);
    xx_mem_free(name_offset);
    xx_mem_free(directory);
    return stream;

fail:
    if (name) xx_str_free(name);
    if (parents) xx_mem_free(parents);
    if (name_length) xx_mem_free(name_length);
    if (name_offset) xx_mem_free(name_offset);
    if (directory) xx_mem_free(directory);
    xx_chieflzmulti_stream_free(stream);
    return NULL;
}



/* The container's own numbers, published unchanged in member->method. */
/* Defined by the reference implementation but present in no known archive and
 * never described. Refusing it is deliberate: guessing would emit noise that
 * a caller cannot tell from data. */


/* Both sizes come from the directory and are attacker-controlled. */

static bool xx_chieflzmulti_decode(Abstractformat *self,
                                   const xx_chieflzmulti_member *member,
                                   uint8_t **out, size_t *out_size,
                                   xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* Method 3 and every value the format does not define land here and
     * fail. Falling through to a stored copy would produce compressed bytes
     * presented as plaintext. */
    if (member->method != XX_CHIEFLZMULTI_METHOD_STORED &&
        member->method != XX_CHIEFLZMULTI_METHOD_CHIEFLZ) {
        return false;
    }
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_CHIEFLZMULTI_MAX_DECODED ||
        member->uncompressed_size > XX_CHIEFLZMULTI_MAX_DECODED) {
        return false;
    }
    /* A stored member whose two sizes disagree is malformed, not a member to
     * be truncated or padded into shape. */
    if (member->method == XX_CHIEFLZMULTI_METHOD_STORED &&
        member->compressed_size != member->uncompressed_size) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_chieflzmulti_read_at(self, member->data_offset, input,
                                 (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    if (member->method == XX_CHIEFLZMULTI_METHOD_STORED) {
        *out = input;
        *out_size = (size_t)member->uncompressed_size;
        return true;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (!xx_chieflz_decode_memory(input, (size_t)member->compressed_size,
                                  output, (size_t)member->uncompressed_size,
                                  &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_chieflzmulti_init(xx_chieflzmulti *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_CHIEFLZMULTI;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-chieflz");
    xx_format_set_extension(&archive->format, "clz");
    archive->format.check_is_valid = xx_chieflzmulti_check_is_valid;
    archive->format.handle_base_info = xx_chieflzmulti_handle_base_info;
    archive->format.get_format_size = xx_chieflzmulti_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_chieflzmulti_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_chieflzmulti_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_chieflzmulti_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_chieflzmulti_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_chieflzmulti_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_chieflzmulti_free_archive_records_reading;
    archive->format.destroy = xx_chieflzmulti_vtable_destroy;
}

xx_chieflzmulti *xx_chieflzmulti_create(xx_io_device *device, int64_t base_address) {
    xx_chieflzmulti *archive = (xx_chieflzmulti *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_chieflzmulti_init(archive, device, base_address);
    return archive;
}

void xx_chieflzmulti_destroy(xx_chieflzmulti *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_chieflzmulti_free(xx_chieflzmulti *archive) {
    if (!archive) return;
    xx_chieflzmulti_destroy(archive);
    xx_mem_free(archive);
}

static void xx_chieflzmulti_vtable_destroy(Abstractformat *self) {
    xx_chieflzmulti_destroy((xx_chieflzmulti *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_chieflzmulti_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_chieflzmulti_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_chieflzmulti_parse(self, pd);
    if (!stream) return false;
    xx_chieflzmulti_stream_free(stream);
    return true;
}

bool xx_chieflzmulti_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_chieflzmulti *archive = (xx_chieflzmulti *)self;
    xx_chieflzmulti_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_chieflzmulti_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_chieflzmulti_stream_free(stream);
    return true;
}

int64_t xx_chieflzmulti_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_chieflzmulti_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_chieflzmulti *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_chieflzmulti_set_record(xx_archive_record *record,
                                 const xx_chieflzmulti_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_chieflzmulti_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_chieflzmulti_get_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_chieflzmulti_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_chieflzmulti_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_chieflzmulti_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_chieflzmulti_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_chieflzmulti_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_chieflzmulti_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_chieflzmulti_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_chieflzmulti_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_chieflzmulti_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_chieflzmulti_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_chieflzmulti_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_chieflzmulti_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_chieflzmulti_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_chieflzmulti_stream *stream;
    const xx_chieflzmulti_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_chieflzmulti_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_chieflzmulti_path_safe(member->name)) return false;

    path_option = xx_chieflzmulti_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_chieflzmulti_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_chieflzmulti_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_chieflzmulti_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
