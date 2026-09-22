/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * POVLAB LZH archives.
 *
 * These are LHA level-1 archives with the method tag renamed, so the header
 * layout is LHA's and only the tag spelling differs.
 *
 *   base header, (header_size_byte + 2) bytes, one per member:
 *     0x00  u8  base header size, NOT counting these first two bytes
 *     0x01  u8  additive checksum of every byte from 0x02 to the end
 *     0x02  5 bytes method tag: "-ARS-" = stored, "-ARA-" = -lh5-
 *     0x07  u32 LE skip size: compressed payload PLUS all extended headers
 *     0x0b  u32 LE original (uncompressed) size
 *     0x0f  u32 LE packed DOS date/time, date in the high word
 *     0x13  u8  DOS attributes
 *     0x14  u8  header level, must be 1
 *     0x15  u8  name length, 1..255
 *     0x16  name, exactly nameLen bytes
 *     0x16+n  u16 LE CRC-16/ARC over the UNPACKED member
 *     0x18+n  u8  originating OS byte
 *     0x19+n  u16 LE size of the first extended header (0 = none)
 *
 * The final word of the base header is the first link of the extended-header
 * chain, so the base header is never shorter than 27 + nameLen bytes. Each
 * extended header is its own length word followed by its body, and the last
 * two bytes of each are the length of the next; a zero length ends the chain.
 * Level 1 counts those headers inside the skip-size field, so the payload
 * starts where the chain ends and its length is skipSize minus the bytes the
 * chain consumed. Walking the chain is the only way to find the payload.
 *
 * The member chain ends at a single 0x00 byte where a header-size byte would
 * be; anything past it is overlay.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/povlablzh/xx_povlablzh.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzh/xx_lzh.h"

#include <stdio.h>

#define XX_POVLABLZH_COPY_CHUNK (64 * 1024)

typedef struct xx_povlablzh_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_povlablzh_member;

typedef struct xx_povlablzh_stream_s {
    xx_povlablzh_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_povlablzh_stream;

static void xx_povlablzh_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_povlablzh_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_povlablzh_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_povlablzh_path_safe(const char *name) {
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

static void xx_povlablzh_stream_free(void *pointer) {
    xx_povlablzh_stream *stream = (xx_povlablzh_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_povlablzh_add(xx_povlablzh_stream *stream,
                          const xx_povlablzh_member *member) {
    xx_povlablzh_member *grown = (xx_povlablzh_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_POVLABLZH_MAX_MEMBERS 65536
#define XX_POVLABLZH_MIN_EXT_SIZE 3
#define XX_POVLABLZH_MAX_EXT_TOTAL ((int64_t)1024 * 1024)
#define XX_POVLABLZH_NAME_OFFSET 22
#define XX_POVLABLZH_MIN_BASE_SIZE 27
#define XX_POVLABLZH_MAX_BASE_SIZE 257  /* the size byte is one byte, plus 2 */
#define XX_POVLABLZH_LEVEL 1
#define XX_POVLABLZH_MAX_NAME 255
#define XX_POVLABLZH_METHOD_STORED 0U
#define XX_POVLABLZH_METHOD_LH5 5U
#define XX_POVLABLZH_MAX_UNCOMPRESSED ((int64_t)256 * 1024 * 1024)
#define XX_POVLABLZH_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_povlablzh_le16(const uint8_t *data);
static uint32_t xx_povlablzh_le32(const uint8_t *data);
static bool xx_povlablzh_checksum_valid(const uint8_t *header, int64_t base_size);
static bool xx_povlablzh_name_valid(const uint8_t *name, int64_t length);
static bool xx_povlablzh_read_member(Abstractformat *self, int64_t span, int64_t offset, xx_povlablzh_member *member, xx_pd_struct *pd);
static xx_povlablzh_stream *xx_povlablzh_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_povlablzh_decode(Abstractformat *self, const xx_povlablzh_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* No member count is stored, so this is a runaway guard rather than a format
 * limit; it matches the reference implementation's bound. */
/* An extended header is at least its own length word plus a type byte. */
/* The extended-header chain is attacker-controlled and self-referential; cap
 * the bytes it may consume so a crafted chain cannot walk the whole file. */

static uint16_t xx_povlablzh_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_povlablzh_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The additive header checksum: every base-header byte from index 2 to the
 * end, modulo 256. This one byte is what makes a random "-ARA-" pair inside
 * unrelated data almost impossible to mistake for a member, so it is the
 * check a later reader must not loosen. */
static bool xx_povlablzh_checksum_valid(const uint8_t *header,
                                        int64_t base_size) {
    uint32_t sum = 0U;
    int64_t index;

    if (base_size < 3) return false;
    for (index = 2; index < base_size; ++index) {
        sum += (uint32_t)header[index];
    }
    return (sum & 0xFFU) == (uint32_t)header[1];
}

/* DOS 8.3 member names. Path separators are rejected outright: the format is
 * flat, so a separator here would only ever be a mis-parse walking into
 * payload bytes. */
static bool xx_povlablzh_name_valid(const uint8_t *name, int64_t length) {
    int64_t index;

    if (length < 1) return false;
    for (index = 0; index < length; ++index) {
        if (name[index] < 0x20U || name[index] > 0x7EU) return false;
        if (name[index] == '/' || name[index] == '\\' || name[index] == ':') {
            return false;
        }
    }
    return true;
}

/* One checked member parser. Fills *member (name allocated) or returns false
 * having allocated nothing. Offsets in and out are relative to base_address. */
static bool xx_povlablzh_read_member(Abstractformat *self, int64_t span,
                                     int64_t offset,
                                     xx_povlablzh_member *member,
                                     xx_pd_struct *pd) {
    uint8_t header[XX_POVLABLZH_MAX_BASE_SIZE];
    uint8_t name_buffer[XX_POVLABLZH_MAX_NAME + 1];
    uint8_t length_word[2];
    uint32_t method;
    int64_t base_size;
    int64_t name_length;
    int64_t skip_size;
    int64_t original_size;
    int64_t ext_total = 0;
    int64_t ext_offset;
    int64_t next_size;
    int64_t compressed_size;
    int64_t data_offset;
    int64_t index;
    char *name;

    if (!xx_povlablzh_range_within(span, offset, XX_POVLABLZH_MIN_BASE_SIZE)) {
        return false;
    }
    if (!xx_povlablzh_read_at(self, self->base_address + offset, header,
                              (size_t)XX_POVLABLZH_NAME_OFFSET)) {
        return false;
    }

    /* The retagged method string is the only magic this format has. */
    if (header[2] != '-' || header[3] != 'A' || header[6] != '-') return false;
    if (header[4] == 'R' && header[5] == 'S') {
        method = XX_POVLABLZH_METHOD_STORED;
    } else if (header[4] == 'R' && header[5] == 'A') {
        method = XX_POVLABLZH_METHOD_LH5;
    } else {
        return false;
    }

    /* Only level 1 exists here; the extended-header walk below assumes the
     * level-1 skip-size semantics and would mis-locate the payload for any
     * other level. */
    if (header[20] != (uint8_t)XX_POVLABLZH_LEVEL) return false;

    /* The size byte does not count itself or the checksum byte. */
    base_size = (int64_t)header[0] + 2;
    name_length = (int64_t)header[21];
    if (name_length < 1 || name_length > XX_POVLABLZH_MAX_NAME) return false;
    /* The CRC, OS byte and first extended-header length word must fit behind
     * the name inside the base header. */
    if (base_size < XX_POVLABLZH_MIN_BASE_SIZE + name_length) return false;
    if (!xx_povlablzh_range_within(span, offset, base_size)) return false;
    if (!xx_povlablzh_read_at(self, self->base_address + offset, header,
                              (size_t)base_size)) {
        return false;
    }
    if (!xx_povlablzh_checksum_valid(header, base_size)) return false;

    skip_size = (int64_t)xx_povlablzh_le32(header + 7);
    original_size = (int64_t)xx_povlablzh_le32(header + 11);
    if (original_size > XX_POVLABLZH_MAX_UNCOMPRESSED) return false;

    if (!xx_povlablzh_name_valid(header + XX_POVLABLZH_NAME_OFFSET,
                                 name_length)) {
        return false;
    }

    /* Level 1 counts the extended headers inside the skip-size field, and the
     * first of their length words is the FINAL word of the base header.
     * Walking the chain is the only way to learn where the payload starts. */
    ext_offset = offset + base_size;
    length_word[0] = header[base_size - 2];
    length_word[1] = header[base_size - 1];
    for (;;) {
        if (pd && xx_pd_is_stopped(pd)) return false;
        next_size = (int64_t)xx_povlablzh_le16(length_word);
        if (next_size == 0) break;
        if (next_size < XX_POVLABLZH_MIN_EXT_SIZE) return false;
        /* The chain lives inside skip_size; a header claiming more than what
         * remains of it would eat into the payload. */
        if (next_size > skip_size - ext_total) return false;
        if (ext_total + next_size > XX_POVLABLZH_MAX_EXT_TOTAL) return false;
        if (!xx_povlablzh_range_within(span, ext_offset, next_size)) {
            return false;
        }
        if (!xx_povlablzh_read_at(self,
                                  self->base_address + ext_offset + next_size -
                                      2,
                                  length_word, sizeof(length_word))) {
            return false;
        }
        ext_total += next_size;
        ext_offset += next_size;
    }

    compressed_size = skip_size - ext_total;
    if (compressed_size < 0) return false;
    /* A stored member is the one place the container states both sizes for
     * the same bytes; a mismatch means this is not really an "-ARS-" record,
     * which is what keeps the two-tag gate honest. */
    if (method == XX_POVLABLZH_METHOD_STORED &&
        compressed_size != original_size) {
        return false;
    }
    if (method != XX_POVLABLZH_METHOD_STORED && compressed_size == 0 &&
        original_size != 0) {
        return false;
    }

    data_offset = ext_offset;
    /* A member extending past EOF is a rejection, not a short read. */
    if (!xx_povlablzh_range_within(span, data_offset, compressed_size)) {
        return false;
    }

    for (index = 0; index < name_length; ++index) {
        name_buffer[index] = header[XX_POVLABLZH_NAME_OFFSET + index];
    }
    name_buffer[name_length] = 0U;
    name = xx_str_dup((const char *)name_buffer);
    if (!name) return false;
    if (!xx_povlablzh_path_safe(name)) {
        xx_str_free(name);
        return false;
    }

    xx_mem_zero(member, sizeof(*member));
    member->name = name;
    member->header_offset = self->base_address + offset;
    member->header_size = data_offset - offset;
    member->data_offset = self->base_address + data_offset;
    member->compressed_size = compressed_size;
    member->uncompressed_size = original_size;
    member->method = method;
    /* Already packed date-high / time-low by the container, so it is stored
     * verbatim rather than re-assembled. */
    member->timestamp = (uint64_t)xx_povlablzh_le32(header + 15);
    /* The format is flat: there are no directory entries. */
    member->is_folder = false;
    /* The CRC-16 behind the name covers the UNPACKED member, so verifying it
     * here would decompress the whole archive on every format probe. */
    return true;
}

static xx_povlablzh_stream *xx_povlablzh_parse(Abstractformat *self,
                                               xx_pd_struct *pd) {
    xx_povlablzh_stream *stream;
    uint8_t lead;
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t count = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* The smallest possible archive is one empty stored member with a
     * one-character name, plus the end marker. */
    if (span < XX_POVLABLZH_MIN_BASE_SIZE + 1 + 1) return NULL;

    stream = (xx_povlablzh_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = 0;
    for (;;) {
        xx_povlablzh_member member;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* Running out of file without meeting the marker is a rejection: the
         * terminator is mandatory, and accepting its absence would let any
         * truncated archive through. */
        if (offset >= span) goto fail;
        if (!xx_povlablzh_read_at(self, self->base_address + offset, &lead,
                                  1U)) {
            goto fail;
        }
        if (lead == 0U) break;  /* end-of-archive marker */

        if (count >= XX_POVLABLZH_MAX_MEMBERS) goto fail;
        if (!xx_povlablzh_read_member(self, span, offset, &member, pd)) {
            goto fail;
        }
        if (!xx_povlablzh_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        ++count;
        /* Members are contiguous: the next header starts where this payload
         * ends. */
        offset = (member.data_offset - self->base_address) +
                 member.compressed_size;
    }

    if (stream->count == 0U) goto fail;
    /* The chain terminates on the 0x00 marker byte and the archive is
     * everything through it; anything after that is overlay, not part of the
     * format. */
    stream->archive_size = offset + 1;
    if (stream->archive_size > span) goto fail;
    return stream;

fail:
    xx_povlablzh_stream_free(stream);
    return NULL;
}


/* 22 = the fixed prefix up to the name; 27 adds the CRC-16 (2), the OS byte
 * (1) and the first extended-header length word (2) that must sit behind it. */

/* The container's method is a five-character tag, not a number. These are the
 * LHA method digits the two tags rename, so a listing still shows which
 * decoder the member needs: "-ARS-" is "-lh0-" and "-ARA-" is "-lh5-". */

/* The stored original size is attacker-controlled: refuse rather than attempt
 * the allocation. */

/* "-ARA-" is a bare -lh5- bitstream. The 5 handed to xx_lzh5_decode_memory is
 * the LHA method digit the tag renames, and it selects the 13-bit sliding
 * window; 4, 6 or 7 would select a different window and decode the same bytes
 * into plausible garbage instead of failing. */
static bool xx_povlablzh_decode(Abstractformat *self,
                                const xx_povlablzh_member *member,
                                uint8_t **out, size_t *out_size,
                                xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_POVLABLZH_MAX_DECODED ||
        member->uncompressed_size > XX_POVLABLZH_MAX_DECODED) {
        return false;
    }
    /* A tag the format defines but this reader does not implement must fail
     * here: silently treating it as stored produces garbage that looks like
     * data. */
    if (member->method != XX_POVLABLZH_METHOD_STORED &&
        member->method != XX_POVLABLZH_METHOD_LH5) {
        return false;
    }

    if (member->uncompressed_size == 0) {
        /* An empty member is a real, empty file. Parse allows an empty
         * payload only for an empty member. */
        if (member->compressed_size != 0) return false;
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }
    if (member->compressed_size == 0) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_povlablzh_read_at(self, member->data_offset, input,
                              (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    if (member->method == XX_POVLABLZH_METHOD_STORED) {
        if (member->compressed_size != member->uncompressed_size) {
            xx_mem_free(input);
            return false;
        }
        *out = input;
        *out_size = (size_t)member->compressed_size;
        return true;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* Exactly the stored original length, or nothing: a short decode reported
     * as success is the one failure the caller cannot detect. */
    if (!xx_lzh5_decode_memory(input, (size_t)member->compressed_size, output,
                               (size_t)member->uncompressed_size, 5,
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

void xx_povlablzh_init(xx_povlablzh *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_POVLABLZH;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-povlab-lzh");
    xx_format_set_extension(&archive->format, "lzh");
    archive->format.check_is_valid = xx_povlablzh_check_is_valid;
    archive->format.handle_base_info = xx_povlablzh_handle_base_info;
    archive->format.get_format_size = xx_povlablzh_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_povlablzh_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_povlablzh_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_povlablzh_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_povlablzh_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_povlablzh_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_povlablzh_free_archive_records_reading;
    archive->format.destroy = xx_povlablzh_vtable_destroy;
}

xx_povlablzh *xx_povlablzh_create(xx_io_device *device, int64_t base_address) {
    xx_povlablzh *archive = (xx_povlablzh *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_povlablzh_init(archive, device, base_address);
    return archive;
}

void xx_povlablzh_destroy(xx_povlablzh *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_povlablzh_free(xx_povlablzh *archive) {
    if (!archive) return;
    xx_povlablzh_destroy(archive);
    xx_mem_free(archive);
}

static void xx_povlablzh_vtable_destroy(Abstractformat *self) {
    xx_povlablzh_destroy((xx_povlablzh *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_povlablzh_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_povlablzh_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_povlablzh_parse(self, pd);
    if (!stream) return false;
    xx_povlablzh_stream_free(stream);
    return true;
}

bool xx_povlablzh_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_povlablzh *archive = (xx_povlablzh *)self;
    xx_povlablzh_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_povlablzh_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_povlablzh_stream_free(stream);
    return true;
}

int64_t xx_povlablzh_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_povlablzh_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_povlablzh *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_povlablzh_set_record(xx_archive_record *record,
                                 const xx_povlablzh_member *member) {
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

static bool xx_povlablzh_copy_options(xx_list_s *target,
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

static const xx_var *xx_povlablzh_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_povlablzh_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_povlablzh_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_povlablzh_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_povlablzh_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_povlablzh_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_povlablzh_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_povlablzh_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_povlablzh_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_povlablzh_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_povlablzh_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_povlablzh_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_povlablzh_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_povlablzh_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_povlablzh_stream *stream;
    const xx_povlablzh_member *member;
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
    stream = (xx_povlablzh_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_povlablzh_path_safe(member->name)) return false;

    path_option = xx_povlablzh_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_povlablzh_decode(self, member, &plain, &plain_size, pd);
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
        !xx_povlablzh_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_povlablzh_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
