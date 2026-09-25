/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * RID archives (OS2YOU / LANTERM / TERM2 / DRIVERS packages, 1998-99).
 *
 *   member header, 0x2b bytes, all words little endian:
 *     0x00  u16      tag; non-zero, otherwise unconstrained
 *     0x02  19 bytes reserved; every byte MUST be zero
 *     0x15  u8       DOS attributes; only 0x00 and 0x20 occur
 *     0x16  u16      DOS time
 *     0x18  u16      DOS date; MUST be non-zero
 *     0x1a  u32      uncompressed size, never past 24 bits
 *     0x1e  char[13] name, NUL terminated INSIDE the field
 *
 *   then the member's block chain, each block introduced by a 3-byte frame:
 *     0x00  u16      block size
 *     0x02  u8       block type: 0x00 stored, 0x01 PKWARE DCL, 0xff end
 *   followed by block_size payload bytes. A packed block is one COMPLETE DCL
 *   stream, prelude and end code included: blocks share no window, and the
 *   plaintext is the concatenation. The end frame always carries size 0.
 *
 *   The next member header starts immediately after the end frame. There is
 *   no central directory, no member count and no end-of-archive record: the
 *   last member's end frame must land exactly on EOF.
 *
 * The format has no magic of any kind. Three things stand in for one, and
 * all three are load bearing:
 *
 *   - the 19 reserved zero bytes, repeated on EVERY member header, so a
 *     spliced or truncated chain fails at the first bad member;
 *   - the name field's DOS 8.3 grammar, including the requirement that the
 *     NUL falls inside the 13 bytes;
 *   - the chain landing exactly on EOF, which is what turns a lucky prefix
 *     into a rejection.
 *
 * On top of those, parse trial-decodes the first member's chain through the
 * codec's scan entry point and requires the plaintext length it measures to
 * equal the header's own size field. The reference does the same in its deep
 * check; here the parse IS the detector, so it always runs.
 *
 * The name field is a writer buffer that is NOT cleared between members:
 * "ZIP.DLL\0EXE\0\0" occurs. The NUL is the only authority and the bytes
 * behind it are ignored, never validated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rid/xx_rid.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/rid/xx_rid.h"

#include <stdio.h>

#define XX_RID_COPY_CHUNK (64 * 1024)

typedef struct xx_rid_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_rid_member;

typedef struct xx_rid_stream_s {
    xx_rid_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_rid_stream;

static void xx_rid_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_rid_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_rid_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_rid_path_safe(const char *name) {
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

static void xx_rid_stream_free(void *pointer) {
    xx_rid_stream *stream = (xx_rid_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_rid_add(xx_rid_stream *stream,
                          const xx_rid_member *member) {
    xx_rid_member *grown = (xx_rid_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_RID_HEADER_SIZE 0x2b
#define XX_RID_RESERVED_OFFSET 0x02
#define XX_RID_RESERVED_SIZE 19
#define XX_RID_ATTRIBUTES_OFFSET 0x15
#define XX_RID_DOSTIME_OFFSET 0x16
#define XX_RID_DOSDATE_OFFSET 0x18
#define XX_RID_SIZE_OFFSET 0x1a
#define XX_RID_NAME_OFFSET 0x1e
#define XX_RID_NAME_FIELD_SIZE 13
#define XX_RID_MAX_BASE_CHARS 8
#define XX_RID_MAX_EXT_CHARS 3
#define XX_RID_ATTRIBUTE_NONE 0x00U
#define XX_RID_ATTRIBUTE_ARCHIVE 0x20U
#define XX_RID_FRAME_SIZE 3
#define XX_RID_BLOCK_TYPE_STORED 0x00U
#define XX_RID_BLOCK_TYPE_PACKED 0x01U
#define XX_RID_BLOCK_TYPE_END 0xffU
#define XX_RID_MAX_UNCOMPRESSED ((int64_t)0x00ffffff)
#define XX_RID_MAX_CHAIN ((int64_t)0x02000000)
#define XX_RID_MAX_MEMBERS 65536
#define XX_RID_MAX_BLOCKS 65536
#define XX_RID_METHOD_CHAIN 0U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_rid_le16(const uint8_t *data);
static uint32_t xx_rid_le32(const uint8_t *data);
static bool xx_rid_is_name_character(uint8_t character);
static char *xx_rid_read_name(const uint8_t *field);
static bool xx_rid_probe(Abstractformat *self, int64_t data_offset, int64_t chain_size, int64_t uncompressed_size);
static xx_rid_stream *xx_rid_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_rid_decode(Abstractformat *self, const xx_rid_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* 2 + 19 + 1 + 2 + 2 + 4 + 13 = 43; every byte of the header is accounted
 * for, which is why there is no room for a magic. */
/* DOS 8.3: 8 + '.' + 3 = 12 characters plus the NUL exactly fill the
 * field. */
/* The only attribute bit the writer ever sets is the archive bit; any other
 * value is a mis-parse, not a supported variation. */
/* u16 size + u8 type. */
/* The size field is a u32 the writer never fills past 24 bits. */
/* With a 24-bit plaintext ceiling and 3 bytes of frame per 65535 payload
 * bytes, no genuine chain comes near this; it bounds the probe read and the
 * decode allocation. */
/* RID stores no method per member: the type lives per block inside the
 * chain. This constant is what member->method carries. */

static uint16_t xx_rid_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_rid_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_rid_is_name_character(uint8_t character) {
    if (character < 0x20U || character > 0x7eU) return false;
    /* Everything DOS itself refuses inside a directory entry. '.' is handled
     * by the caller because its position carries meaning. */
    if (character == (uint8_t)'/' || character == (uint8_t)'\\' ||
        character == (uint8_t)':' || character == (uint8_t)'*' ||
        character == (uint8_t)'?' || character == (uint8_t)'"' ||
        character == (uint8_t)'<' || character == (uint8_t)'>' ||
        character == (uint8_t)'|' || character == (uint8_t)' ') {
        return false;
    }
    return true;
}

/* Validate the 13-byte name field and copy the name out. Returns NULL for
 * anything that is not a DOS 8.3 name, which together with the reserved
 * zeros is the closest this format comes to having a magic. */
static char *xx_rid_read_name(const uint8_t *field) {
    char *name;
    int32_t length = 0;
    int32_t dot = -1;
    int32_t index;
    int32_t base_chars;
    int32_t ext_chars;

    while (length < XX_RID_NAME_FIELD_SIZE && field[length] != 0U) {
        ++length;
    }
    if (length <= 0) return NULL;
    /* The name must terminate INSIDE the field: 13 printable bytes with no
     * NUL means this is not a RID name field at all. Everything behind the
     * NUL is the previous member's name left in an uncleared buffer and is
     * deliberately not examined. */
    if (length >= XX_RID_NAME_FIELD_SIZE) return NULL;

    for (index = 0; index < length; ++index) {
        uint8_t character = field[index];

        if (character == (uint8_t)'.') {
            /* Exactly one dot, never leading, never trailing. */
            if (dot >= 0 || index == 0 || index == length - 1) return NULL;
            dot = index;
            continue;
        }
        if (!xx_rid_is_name_character(character)) return NULL;
    }

    base_chars = (dot >= 0) ? dot : length;
    ext_chars = (dot >= 0) ? (length - dot - 1) : 0;
    if (base_chars < 1 || base_chars > XX_RID_MAX_BASE_CHARS) return NULL;
    if (ext_chars > XX_RID_MAX_EXT_CHARS) return NULL;

    name = (char *)xx_mem_alloc((size_t)length + 1U);
    if (!name) return NULL;
    xx_rt_memcpy(name, field, (size_t)length);
    name[length] = '\0';
    return name;
}

/* Trial-decode a chain and check the plaintext length against the header's
 * own field. Nothing is kept; the scan only counts. */
static bool xx_rid_probe(Abstractformat *self, int64_t data_offset,
                         int64_t chain_size, int64_t uncompressed_size) {
    uint8_t *chain;
    size_t consumed = 0U;
    size_t produced = 0U;
    bool result;

    if (chain_size < XX_RID_FRAME_SIZE || chain_size > XX_RID_MAX_CHAIN) {
        return false;
    }
    chain = (uint8_t *)xx_mem_alloc((size_t)chain_size);
    if (!chain) return false;
    if (!xx_rid_read_at(self, data_offset, chain, (size_t)chain_size)) {
        xx_mem_free(chain);
        return false;
    }
    result = xx_rid_scan_memory(chain, (size_t)chain_size,
                                (size_t)XX_RID_MAX_UNCOMPRESSED, &consumed,
                                &produced);
    xx_mem_free(chain);
    if (!result) return false;
    /* The frame walk already proved the chain's extent; the scan must agree
     * with it and with the size field. This is the check that separates a
     * real package from a run of bytes that merely happens to frame. */
    if ((int64_t)consumed != chain_size) return false;
    if ((int64_t)produced != uncompressed_size) return false;
    return true;
}

static xx_rid_stream *xx_rid_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_rid_stream *stream = NULL;
    uint8_t header[XX_RID_HEADER_SIZE];
    uint8_t frame[XX_RID_FRAME_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t count = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* The shortest legal archive is one empty member: header plus a bare
     * terminator frame. */
    if (span < (int64_t)XX_RID_HEADER_SIZE + XX_RID_FRAME_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    stream = (xx_rid_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = 0;
    for (;;) {
        xx_rid_member member;
        int64_t chain_offset;
        int64_t data_offset;
        int64_t blocks = 0;
        uint32_t raw_size;
        uint16_t dos_time;
        uint16_t dos_date;
        uint8_t attributes;
        int32_t reserved;
        bool terminated = false;
        char *name;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (count >= XX_RID_MAX_MEMBERS) goto fail;
        if (!xx_rid_range_within(span, offset, XX_RID_HEADER_SIZE)) goto fail;
        if (!xx_rid_read_at(self, self->base_address + offset, header,
                            sizeof(header))) {
            goto fail;
        }

        /* There is no magic anywhere in this format; the reserved run is
         * what takes its place, and because it is repeated on every member
         * header a spliced or truncated chain fails closed. Loosening this
         * loop -- checking a few of the nineteen bytes, or only the first
         * member's -- is what would turn this reader into a matcher for
         * arbitrary zero-padded data. */
        if (xx_rid_le16(header) == 0U) goto fail;
        for (reserved = 0; reserved < XX_RID_RESERVED_SIZE; ++reserved) {
            if (header[XX_RID_RESERVED_OFFSET + reserved] != 0U) goto fail;
        }

        attributes = header[XX_RID_ATTRIBUTES_OFFSET];
        if (attributes != (uint8_t)XX_RID_ATTRIBUTE_NONE &&
            attributes != (uint8_t)XX_RID_ATTRIBUTE_ARCHIVE) {
            goto fail;
        }

        dos_time = xx_rid_le16(header + XX_RID_DOSTIME_OFFSET);
        dos_date = xx_rid_le16(header + XX_RID_DOSDATE_OFFSET);
        /* The writer always stamps a date; a zero one means these bytes are
         * not a header. */
        if (dos_date == 0U) goto fail;

        raw_size = xx_rid_le32(header + XX_RID_SIZE_OFFSET);
        if ((int64_t)raw_size > XX_RID_MAX_UNCOMPRESSED) goto fail;

        name = xx_rid_read_name(header + XX_RID_NAME_OFFSET);
        if (!name) goto fail;

        data_offset = offset + XX_RID_HEADER_SIZE;
        chain_offset = data_offset;
        /* Walk the chain frame by frame. Three bytes per frame and no
         * payload is ever read, so measuring a member costs the same
         * whether it is stored or packed. */
        for (;;) {
            int64_t block_size;
            uint8_t block_type;

            if (blocks > XX_RID_MAX_BLOCKS) {
                xx_str_free(name);
                goto fail;
            }
            if (pd && xx_pd_is_stopped(pd)) {
                xx_str_free(name);
                goto fail;
            }
            if (!xx_rid_range_within(span, chain_offset, XX_RID_FRAME_SIZE)) {
                xx_str_free(name);
                goto fail;
            }
            if (!xx_rid_read_at(self, self->base_address + chain_offset,
                                frame, sizeof(frame))) {
                xx_str_free(name);
                goto fail;
            }
            block_size = (int64_t)xx_rid_le16(frame);
            block_type = frame[2];
            chain_offset += XX_RID_FRAME_SIZE;

            if (block_type == (uint8_t)XX_RID_BLOCK_TYPE_END) {
                /* The terminator carries no payload; a non-zero size here
                 * means the byte only looked like a terminator. */
                if (block_size != 0) {
                    xx_str_free(name);
                    goto fail;
                }
                terminated = true;
                break;
            }
            if (block_type != (uint8_t)XX_RID_BLOCK_TYPE_STORED &&
                block_type != (uint8_t)XX_RID_BLOCK_TYPE_PACKED) {
                xx_str_free(name);
                goto fail;
            }
            if (block_size <= 0) {
                xx_str_free(name);
                goto fail;
            }
            if (!xx_rid_range_within(span, chain_offset, block_size)) {
                xx_str_free(name);
                goto fail;
            }
            chain_offset += block_size;
            ++blocks;
        }
        if (!terminated) {
            xx_str_free(name);
            goto fail;
        }

        /* A non-empty member cannot be carried by an empty chain, and an
         * empty member cannot carry blocks. */
        if (raw_size == 0U && blocks != 0) {
            xx_str_free(name);
            goto fail;
        }
        if (raw_size != 0U && blocks == 0) {
            xx_str_free(name);
            goto fail;
        }
        if (chain_offset - data_offset > XX_RID_MAX_CHAIN) {
            xx_str_free(name);
            goto fail;
        }

        /* Structure alone is not proof in a format with no magic, so the
         * first member's payload is decoded for real -- counted, not kept --
         * and its length held against the header's size field. Running it on
         * the first member only keeps the cost bounded while still refusing
         * a file whose framing is accidental. */
        if (count == 0 &&
            !xx_rid_probe(self, self->base_address + data_offset,
                          chain_offset - data_offset, (int64_t)raw_size)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_RID_HEADER_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = chain_offset - data_offset;
        member.uncompressed_size = (int64_t)raw_size;
        member.method = XX_RID_METHOD_CHAIN;
        member.timestamp = ((uint64_t)dos_date << 16) | (uint64_t)dos_time;
        member.is_folder = false;
        if (!xx_rid_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        ++count;
        offset = chain_offset;

        /* No end-of-archive record exists: the chain has to land exactly on
         * EOF. Accepting a short tail would turn any prefix match into a hit
         * on a format that has no magic at all. */
        if (offset == span) break;
    }

    if (count < 1) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;
    stream->archive_size = offset;
    return stream;

fail:
    xx_rid_stream_free(stream);
    return NULL;
}


/* A member is the whole block chain, terminator included. The codec restarts
 * per block, so this is one call however many blocks there are. */
static bool xx_rid_decode(Abstractformat *self, const xx_rid_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* RID has no method field: every member is a block chain, and the
     * per-block type lives inside the chain where the codec reads it. The
     * constant is published so that a member reaching decode with anything
     * else -- a hand-built struct, a future variant -- is refused instead of
     * being copied out raw. */
    if (member->method != XX_RID_METHOD_CHAIN) return false;
    if (member->uncompressed_size < 0 ||
        member->uncompressed_size > XX_RID_MAX_UNCOMPRESSED) {
        return false;
    }
    /* Even an empty member carries its terminator frame. */
    if (member->compressed_size < XX_RID_FRAME_SIZE ||
        member->compressed_size > XX_RID_MAX_CHAIN) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_rid_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    /* A zero-length member still gets a real block so the caller always has
     * something to free. */
    output = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* The codec succeeds only when the chain terminates exactly at the end
     * of the input AND produces exactly the declared length, so a chain that
     * stops early, runs long, or has a block the decoder chokes on all land
     * here as false rather than as a partial member. */
    if (!xx_rid_decode_memory(input, (size_t)member->compressed_size, output,
                              (size_t)member->uncompressed_size, &written) ||
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

void xx_rid_init(xx_rid *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_RID;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-rid");
    xx_format_set_extension(&archive->format, "rid");
    archive->format.check_is_valid = xx_rid_check_is_valid;
    archive->format.handle_base_info = xx_rid_handle_base_info;
    archive->format.get_format_size = xx_rid_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rid_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rid_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rid_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rid_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rid_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rid_free_archive_records_reading;
    archive->format.destroy = xx_rid_vtable_destroy;
}

xx_rid *xx_rid_create(xx_io_device *device, int64_t base_address) {
    xx_rid *archive = (xx_rid *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_rid_init(archive, device, base_address);
    return archive;
}

void xx_rid_destroy(xx_rid *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_rid_free(xx_rid *archive) {
    if (!archive) return;
    xx_rid_destroy(archive);
    xx_mem_free(archive);
}

static void xx_rid_vtable_destroy(Abstractformat *self) {
    xx_rid_destroy((xx_rid *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_rid_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_rid_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_rid_parse(self, pd);
    if (!stream) return false;
    xx_rid_stream_free(stream);
    return true;
}

bool xx_rid_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_rid *archive = (xx_rid *)self;
    xx_rid_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_rid_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_rid_stream_free(stream);
    return true;
}

int64_t xx_rid_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_rid_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_rid *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_rid_set_record(xx_archive_record *record,
                                 const xx_rid_member *member) {
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

static bool xx_rid_copy_options(xx_list_s *target,
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

static const xx_var *xx_rid_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_rid_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_rid_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_rid_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_rid_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_rid_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_rid_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_rid_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_rid_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_rid_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_rid_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_rid_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_rid_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_rid_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_rid_stream *stream;
    const xx_rid_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_rid_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_rid_path_safe(member->name)) return false;

    path_option = xx_rid_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_rid_decode(self, member, &plain, &plain_size, pd);
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
        !xx_rid_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
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
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_rid_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
