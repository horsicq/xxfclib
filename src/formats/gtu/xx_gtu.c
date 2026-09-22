/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * GTU OS/2 distribution kits (*.csd, *.001, *.002).
 *
 * The container has no magic. The only global field is at the base address:
 *
 *   0x00  u32 LE  the size of the whole file
 *
 * and it must equal the real file size exactly. Everything after it is the
 * index, a chain of 12-byte records each immediately followed by its name:
 *
 *   0x00  u32 LE  block offset, from the base address
 *   0x04  u16 LE  tag, always 1
 *   0x06  u16 LE  kind, 0 or 1
 *   0x08  u16 LE  zero
 *   0x0A  u16 LE  name length, in bytes, no terminator
 *   0x0C          the obfuscated name, name length bytes
 *
 * The next record starts right after the name. There is no member count and
 * no sentinel name: the chain ends at the record whose block offset is the
 * record's own offset, i.e. the record that points at itself.
 *
 * The name is stored under a running-difference cipher. The key starts as the
 * low byte of the stored name length and is then replaced by each plaintext
 * byte in turn, so plain[i] = (enc[i] - key) & 0xFF and key = plain[i].
 *
 * The data block at the block offset opens with a verbatim copy of the
 * 12-byte index record and of the still-obfuscated name, then an information
 * block of 28 bytes:
 *
 *   0x00  u16 LE  DOS date
 *   0x02  u16 LE  DOS time
 *   0x0C  i32 LE  uncompressed size
 *   0x10  i32 LE  allocated size
 *   0x14  u32 LE  attributes
 *   0x18  i32 LE  skip size, plaintext belonging to a previous volume
 *
 * and then the frame chain. A frame is
 *
 *   0x00  i32 LE  raw size, at most 0x10000
 *   0x04  i32 LE  packed size
 *   0x08          one complete Okumura LZARI stream, packed size bytes
 *
 * with the model, the ring buffer and the arithmetic interval all restarting
 * at every frame. The chain opens with frames whose plaintext belongs to a
 * previous volume of a multi-disk kit: their raw sizes must sum to exactly
 * the skip size, and only the frames after them are this member's data. The
 * member's frames must then sum to exactly the declared uncompressed size.
 *
 * With no magic anywhere, what makes this safe to detect is the combination
 * of the self-referential terminator, the block's verbatim copy of the record
 * and of the encoded name, and the frame arithmetic landing exactly on both
 * declared totals.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gtu/xx_gtu.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/gtu/xx_gtu.h"
#include <stdio.h>

#define XX_GTU_COPY_CHUNK (64 * 1024)

typedef struct xx_gtu_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_gtu_member;

typedef struct xx_gtu_stream_s {
    xx_gtu_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_gtu_stream;

static void xx_gtu_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_gtu_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_gtu_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_gtu_path_safe(const char *name) {
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

static void xx_gtu_stream_free(void *pointer) {
    xx_gtu_stream *stream = (xx_gtu_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_gtu_add(xx_gtu_stream *stream,
                          const xx_gtu_member *member) {
    xx_gtu_member *grown = (xx_gtu_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_GTU_SIZE_FIELD 4
#define XX_GTU_RECORD_SIZE 12
#define XX_GTU_INFO_SIZE 28
#define XX_GTU_FRAME_PRELUDE 8
#define XX_GTU_RECORD_TAG 1U
#define XX_GTU_MAX_RECORD_KIND 1U
#define XX_GTU_MAX_NAME 1024
#define XX_GTU_MAX_MEMBERS 100000
#define XX_GTU_MAX_FRAMES 1048576
#define XX_GTU_MAX_UNCOMPRESSED 0x40000000
#define XX_GTU_MAX_FRAME_RAW 0x10000
#define XX_GTU_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_gtu_le16(const uint8_t *data);
static uint32_t xx_gtu_le32(const uint8_t *data);
static int64_t xx_gtu_i32(const uint8_t *data);
static void xx_gtu_decode_name(uint8_t *data, int64_t length);
static bool xx_gtu_name_is_valid(const uint8_t *data, int64_t length);
static bool xx_gtu_measure_frames(Abstractformat *self, int64_t span, int64_t info_offset, int64_t skip_size, int64_t uncompressed_size, int64_t *data_offset, int64_t *compressed_size, xx_pd_struct *pd);
static xx_gtu_stream *xx_gtu_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_gtu_decode(Abstractformat *self, const xx_gtu_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The u32 file size at +0, then the index. */
/* The word at record +0x06 is 0 or 1. The reference detector read +0x04 as a
 * single u32 and demanded exactly 1, which abandoned every kit that mixes in
 * kind-1 members; the two kinds share the layout, the information block and
 * the frame chain, so only the tag half is constrained. */
/* A u16 field, so this only refuses the absurd; OS/2 paths are far shorter. */
/* Every frame but a member's last carries exactly 64 KiB of plaintext. */

static uint16_t xx_gtu_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_gtu_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The sizes in the information block are signed in the reference, which then
 * tests them for being negative. Sign extending here keeps that test
 * meaningful instead of turning a high bit into a four-billion-byte extent. */
static int64_t xx_gtu_i32(const uint8_t *data) {
    return (int64_t)(int32_t)xx_gtu_le32(data);
}

/* Running-difference cipher, decoded in place over `length` bytes. The seed is
 * the LOW BYTE of the stored length, so a 256-byte name decodes with key 0 --
 * that is the 8-bit subtraction the original performs, not a truncation
 * accident. */
static void xx_gtu_decode_name(uint8_t *data, int64_t length) {
    uint8_t key = (uint8_t)(length & 0xFF);
    int64_t index;

    for (index = 0; index < length; ++index) {
        uint8_t plain = (uint8_t)(data[index] - key);
        data[index] = plain;
        key = plain;
    }
}

/* Kit names are plain uppercase OS/2 paths. A byte outside the printable
 * range means the cipher was applied to something that is not a name at all,
 * which -- in a format with no magic -- is one of the few signals available
 * that this file is not a GTU kit. */
static bool xx_gtu_name_is_valid(const uint8_t *data, int64_t length) {
    int64_t index;

    if (length <= 0) return false;
    for (index = 0; index < length; ++index) {
        uint8_t value = data[index];
        if (value < 0x20U || value > 0x7EU) return false;
        if (value == '"' || value == '*' || value == '<' || value == '>' ||
            value == '?' || value == '|' || value == ':') {
            return false;
        }
    }
    return true;
}

/* Walk the member's frame chain twice: first over the frames whose plaintext
 * belongs to a previous volume, then over the member's own. Both walks must
 * land exactly on their declared total -- a chain that overshoots by even one
 * byte is not a frame chain, and that arithmetic is the strongest structural
 * check the format offers. */
static bool xx_gtu_measure_frames(Abstractformat *self, int64_t span,
                                  int64_t info_offset, int64_t skip_size,
                                  int64_t uncompressed_size,
                                  int64_t *data_offset,
                                  int64_t *compressed_size,
                                  xx_pd_struct *pd) {
    uint8_t frame[XX_GTU_FRAME_PRELUDE];
    int64_t offset = info_offset + XX_GTU_INFO_SIZE;
    int64_t remaining = skip_size;
    int64_t frames = 0;
    int64_t raw_size;
    int64_t packed_size;
    int phase;

    for (phase = 0; phase < 2; ++phase) {
        if (phase == 1) {
            *data_offset = offset;
            remaining = uncompressed_size;
        }
        while (remaining > 0) {
            if (++frames > XX_GTU_MAX_FRAMES) return false;
            if (pd && xx_pd_is_stopped(pd)) return false;
            if (!xx_gtu_range_within(span, offset, XX_GTU_FRAME_PRELUDE)) {
                return false;
            }
            if (!xx_gtu_read_at(self, self->base_address + offset, frame,
                                sizeof(frame))) {
                return false;
            }
            raw_size = xx_gtu_i32(frame);
            packed_size = xx_gtu_i32(frame + 4);
            if (raw_size <= 0 || raw_size > XX_GTU_MAX_FRAME_RAW) return false;
            /* A skipped frame may legitimately be empty; a frame this member
             * decodes may not, or the chain would not advance. */
            if (packed_size < 0 || (phase == 1 && packed_size <= 0)) {
                return false;
            }
            /* Overshooting the declared total means the chain and the
             * information block disagree about the member. */
            if (phase == 1 && raw_size > remaining) return false;
            offset += XX_GTU_FRAME_PRELUDE;
            if (!xx_gtu_range_within(span, offset, packed_size)) return false;
            offset += packed_size;
            remaining -= raw_size;
        }
        if (remaining != 0) return false;
    }

    *compressed_size = offset - *data_offset;
    return true;
}

static xx_gtu_stream *xx_gtu_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_gtu_stream *stream = NULL;
    uint8_t *encoded = NULL;
    uint8_t *echo = NULL;
    char *name = NULL;
    uint8_t size_field[XX_GTU_SIZE_FIELD];
    uint8_t record[XX_GTU_RECORD_SIZE];
    uint8_t info[XX_GTU_INFO_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t block_offset;
    int64_t name_offset;
    int64_t name_size;
    int64_t info_offset;
    int64_t uncompressed_size;
    int64_t allocated_size;
    int64_t skip_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t index;
    uint16_t kind;
    bool terminated = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_GTU_SIZE_FIELD + XX_GTU_RECORD_SIZE) return NULL;
    if (!xx_gtu_read_at(self, self->base_address, size_field,
                        sizeof(size_field))) {
        return NULL;
    }
    /* The container's only global field, and the cheapest thing standing
     * between an arbitrary file and an index walk: the u32 at +0 states the
     * size of the file it sits in, and must match it exactly. */
    if ((int64_t)xx_gtu_le32(size_field) != span) return NULL;

    stream = (xx_gtu_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_GTU_SIZE_FIELD;
    while (!terminated) {
        xx_gtu_member member;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_GTU_MAX_MEMBERS) goto fail;
        if (!xx_gtu_range_within(span, offset, XX_GTU_RECORD_SIZE)) goto fail;
        if (!xx_gtu_read_at(self, self->base_address + offset, record,
                            sizeof(record))) {
            goto fail;
        }
        block_offset = (int64_t)xx_gtu_le32(record);

        /* The chain ends on the record that points at itself. There is no
         * count and no sentinel name; this self-reference is the terminator,
         * and a file that never reaches one is not a kit. */
        if (block_offset == offset) {
            terminated = true;
            break;
        }

        kind = xx_gtu_le16(record + 6);
        if (xx_gtu_le16(record + 4) != (uint16_t)XX_GTU_RECORD_TAG ||
            kind > (uint16_t)XX_GTU_MAX_RECORD_KIND ||
            xx_gtu_le16(record + 8) != 0U) {
            goto fail;
        }
        name_size = (int64_t)xx_gtu_le16(record + 10);
        if (name_size <= 0 || name_size > XX_GTU_MAX_NAME) goto fail;
        if (block_offset <= 0 || block_offset >= span) goto fail;

        name_offset = offset + XX_GTU_RECORD_SIZE;
        if (!xx_gtu_range_within(span, name_offset, name_size)) goto fail;
        encoded = (uint8_t *)xx_mem_alloc((size_t)name_size);
        if (!encoded) goto fail;
        if (!xx_gtu_read_at(self, self->base_address + name_offset, encoded,
                            (size_t)name_size)) {
            goto fail;
        }

        name = (char *)xx_mem_alloc((size_t)name_size + 1U);
        if (!name) goto fail;
        for (index = 0; index < name_size; ++index) {
            name[index] = (char)encoded[index];
        }
        name[name_size] = '\0';
        xx_gtu_decode_name((uint8_t *)name, name_size);
        if (!xx_gtu_name_is_valid((const uint8_t *)name, name_size)) goto fail;
        for (index = 0; index < name_size; ++index) {
            if (name[index] == '\\') name[index] = '/';
        }

        /* The data block opens with a verbatim copy of the 12-byte record and
         * of the STILL-OBFUSCATED name. This is the format's substitute for a
         * magic number: a random 12-byte window would have to point at a copy
         * of itself, followed by a copy of its own encoded name, to get this
         * far. Loosening it makes GTU match arbitrary data. */
        if (!xx_gtu_range_within(span, block_offset,
                                 XX_GTU_RECORD_SIZE + name_size)) {
            goto fail;
        }
        echo = (uint8_t *)xx_mem_alloc((size_t)(XX_GTU_RECORD_SIZE +
                                                name_size));
        if (!echo) goto fail;
        if (!xx_gtu_read_at(self, self->base_address + block_offset, echo,
                            (size_t)(XX_GTU_RECORD_SIZE + name_size))) {
            goto fail;
        }
        if (xx_rt_memcmp(echo, record, (size_t)XX_GTU_RECORD_SIZE) != 0) {
            goto fail;
        }
        if (xx_rt_memcmp(echo + XX_GTU_RECORD_SIZE, encoded,
                         (size_t)name_size) != 0) {
            goto fail;
        }
        xx_mem_free(echo);
        echo = NULL;
        xx_mem_free(encoded);
        encoded = NULL;

        info_offset = block_offset + XX_GTU_RECORD_SIZE + name_size;
        if (!xx_gtu_range_within(span, info_offset, XX_GTU_INFO_SIZE)) {
            goto fail;
        }
        if (!xx_gtu_read_at(self, self->base_address + info_offset, info,
                            sizeof(info))) {
            goto fail;
        }
        uncompressed_size = xx_gtu_i32(info + 0x0C);
        allocated_size = xx_gtu_i32(info + 0x10);
        skip_size = xx_gtu_i32(info + 0x18);
        if (uncompressed_size < 0 || allocated_size < 0 || skip_size < 0 ||
            uncompressed_size > XX_GTU_MAX_UNCOMPRESSED ||
            skip_size > XX_GTU_MAX_UNCOMPRESSED) {
            goto fail;
        }

        data_offset = 0;
        compressed_size = 0;
        if (!xx_gtu_measure_frames(self, span, info_offset, skip_size,
                                   uncompressed_size, &data_offset,
                                   &compressed_size, pd)) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_GTU_RECORD_SIZE + name_size;
        /* The frames this member contributes, i.e. past the ones whose
         * plaintext belongs to a previous volume. */
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        /* The record's kind word, unchanged: the container has no method
         * field, and both kinds are the same framed LZARI. */
        member.method = (uint32_t)kind;
        member.timestamp = ((uint64_t)xx_gtu_le16(info) << 16) |
                           (uint64_t)xx_gtu_le16(info + 2);
        member.is_folder = false;

        if (!xx_gtu_add(stream, &member)) goto fail;
        name = NULL;

        offset = name_offset + name_size;
    }

    /* A kit with no members is not a kit: the terminator alone would make any
     * file whose first u32 equals its own size an empty archive. */
    if (!terminated || stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    stream->archive_size = span;
    return stream;

fail:
    xx_mem_free(echo);
    xx_mem_free(encoded);
    xx_str_free(name);
    xx_gtu_stream_free(stream);
    return NULL;
}


/* The information block's size field is attacker-controlled, so it is capped
 * before it becomes an allocation. */

static bool xx_gtu_decode(Abstractformat *self, const xx_gtu_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!member || (pd && xx_pd_is_stopped(pd))) return false;
    if (member->compressed_size < 0 ||
        member->compressed_size > (int64_t)XX_GTU_MAX_DECODED) {
        return false;
    }
    if (member->uncompressed_size < 0 ||
        member->uncompressed_size > (int64_t)XX_GTU_MAX_DECODED) {
        return false;
    }
    /* Only the two kinds the parse admits reach here; anything else would be
     * a member the reader never validated a frame chain for. */
    if (member->method > (uint32_t)XX_GTU_MAX_RECORD_KIND) return false;

    /* A zero-length member has no frames at all -- the chain stops before it
     * starts -- so there is nothing to hand the decoder. */
    if (member->uncompressed_size == 0) {
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        *out = plain;
        *out_size = 0U;
        return true;
    }
    if (member->compressed_size == 0) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_gtu_read_at(self, member->data_offset, packed,
                        (size_t)member->compressed_size) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* The chain of [i32 raw][i32 packed][LZARI] frames, starting at this
     * member's first frame. The entry point produces exactly output_size or
     * fails; the extra test keeps a future change to that contract from
     * publishing a short member as a complete one. */
    if (!xx_gtu_decode_memory(packed, (size_t)member->compressed_size, plain,
                              (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_gtu_init(xx_gtu *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_GTU;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-gtu");
    xx_format_set_extension(&archive->format, "csd");
    archive->format.check_is_valid = xx_gtu_check_is_valid;
    archive->format.handle_base_info = xx_gtu_handle_base_info;
    archive->format.get_format_size = xx_gtu_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_gtu_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_gtu_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_gtu_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_gtu_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_gtu_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_gtu_free_archive_records_reading;
    archive->format.destroy = xx_gtu_vtable_destroy;
}

xx_gtu *xx_gtu_create(xx_io_device *device, int64_t base_address) {
    xx_gtu *archive = (xx_gtu *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_gtu_init(archive, device, base_address);
    return archive;
}

void xx_gtu_destroy(xx_gtu *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_gtu_free(xx_gtu *archive) {
    if (!archive) return;
    xx_gtu_destroy(archive);
    xx_mem_free(archive);
}

static void xx_gtu_vtable_destroy(Abstractformat *self) {
    xx_gtu_destroy((xx_gtu *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_gtu_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_gtu_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_gtu_parse(self, pd);
    if (!stream) return false;
    xx_gtu_stream_free(stream);
    return true;
}

bool xx_gtu_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_gtu *archive = (xx_gtu *)self;
    xx_gtu_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_gtu_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_gtu_stream_free(stream);
    return true;
}

int64_t xx_gtu_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_gtu_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_gtu *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_gtu_set_record(xx_archive_record *record,
                                 const xx_gtu_member *member) {
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

static bool xx_gtu_copy_options(xx_list_s *target,
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

static const xx_var *xx_gtu_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_gtu_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_gtu_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_gtu_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_gtu_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_gtu_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_gtu_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_gtu_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_gtu_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_gtu_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_gtu_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_gtu_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_gtu_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_gtu_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_gtu_stream *stream;
    const xx_gtu_member *member;
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
    stream = (xx_gtu_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_gtu_path_safe(member->name)) return false;

    path_option = xx_gtu_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_gtu_decode(self, member, &plain, &plain_size, pd);
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
        !xx_gtu_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_gtu_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
