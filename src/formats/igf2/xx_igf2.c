/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IGF installer containers (*.igf), the multi-member sibling of the
 * single-member IGF compressed file.
 *
 * Header at offset 0:
 *
 *   0x00  u16 LE  magic 0x1324
 *   0x08  i32 LE  total size of the container, which must equal the file
 *   0x0c  i32 LE  a positive count field
 *   0x20  u32 LE  offset of the first member record
 *   0x24  u32 LE  one's complement of that offset
 *
 * Each member record is 0x38 bytes, starting with u16 LE 0xECDB -- the same
 * value that is the whole magic of the single-member IGF file, which is why
 * that magic sits at offset 0 there and never here. The chain ends when the
 * u16 at +0x02 of a record reads 0xFFFF; that terminator is frequently
 * truncated at EOF, so only its first four bytes are ever required.
 *
 * Two record shapes exist. The one the reference extractor hard codes puts
 * the time at +0x14, the sizes at +0x1c/+0x24 and the name at +0x38; a
 * second, otherwise identical shape drops twelve bytes of padding, moving
 * them to +0x10, +0x18/+0x20 and +0x2c. Nothing in the header distinguishes
 * them, so both are walked and the one that reaches the terminator with
 * every extent inside the file wins. The member bytes are identical either
 * way; only the record parse differs.
 *
 * Behind the record sits the NUL-terminated name, and the data then starts
 * at the next multiple of four measured from the record start -- strictly
 * past the terminator, so an already aligned position still advances.
 *
 * Names are absolute DOS paths ("C:\WIN95\~igf0F30.TMP"), so unlike most
 * containers here the raw bytes genuinely may carry a drive colon, a
 * backslash and high-bit codepage characters. They are folded rather than
 * rejected: backslash becomes '/', the colon and the characters a path
 * cannot carry become '_', leading slashes and "../" are neutered.
 *
 * The container states no method number. A member whose raw and packed
 * sizes are both zero is empty and stored; anything else is an LHA -lh4-
 * stream. Those two derived values are what land in member.method.
 *
 * The magic is two bytes, so detection rests on three other things: the
 * stored total size having to equal the real file size (which also rules
 * out an appended overlay), the directory offset having to be the exact
 * one's complement of the word behind it, and the first record carrying the
 * 0xECDB record magic. Those must not be loosened.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/igf2/xx_igf2.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzh/xx_lzh.h"

#include <stdio.h>

#define XX_IGF2_COPY_CHUNK (64 * 1024)

typedef struct xx_igf2_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_igf2_member;

typedef struct xx_igf2_stream_s {
    xx_igf2_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_igf2_stream;

static void xx_igf2_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_igf2_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_igf2_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_igf2_path_safe(const char *name) {
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

static void xx_igf2_stream_free(void *pointer) {
    xx_igf2_stream *stream = (xx_igf2_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_igf2_add(xx_igf2_stream *stream,
                          const xx_igf2_member *member) {
    xx_igf2_member *grown = (xx_igf2_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_IGF2_HEADER_SIZE 0x28
#define XX_IGF2_RECORD_SIZE 0x38
#define XX_IGF2_MAGIC 0x1324U
#define XX_IGF2_RECORD_MAGIC 0xECDBU
#define XX_IGF2_END_TAG 0xFFFFU
#define XX_IGF2_MAX_NAME 1024
#define XX_IGF2_MAX_MEMBERS 65536
#define XX_IGF2_MAX_DECODED (256 * 1024 * 1024)
#define XX_IGF2_METHOD_STORE 0U
#define XX_IGF2_METHOD_LZH4 4U

typedef struct {
    int32_t time_offset;
    int32_t raw_offset;
    int32_t packed_offset;
    int32_t name_offset;
} xx_igf2_layout;

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_igf2_le16(const uint8_t *data);
static uint32_t xx_igf2_le32(const uint8_t *data);
static void xx_igf2_fix_name(const uint8_t *raw, size_t raw_length, char *out, size_t index);
static bool xx_igf2_walk(Abstractformat *self, xx_pd_struct *pd, int64_t span, int64_t directory_offset, const xx_igf2_layout *layout, xx_igf2_stream *stream);
static xx_igf2_stream *xx_igf2_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_igf2_decode(Abstractformat *self, const xx_igf2_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);





/* Shape 0 is what the reference extractor hard codes; shape 1 drops twelve
 * bytes of padding. Nothing in the header says which is in use. */
static const xx_igf2_layout xx_igf2_layouts[2] = {
    {0x14, 0x1c, 0x24, 0x38},
    {0x10, 0x18, 0x20, 0x2c}};

static uint16_t xx_igf2_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_igf2_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Names are absolute DOS paths, so bytes outside 0x20..0x7E are NOT a
 * rejection here: the format genuinely stores a drive colon, backslashes
 * and OEM codepage characters. They are folded to something a destination
 * path can hold instead, exactly as the reference reader does. */
static void xx_igf2_fix_name(const uint8_t *raw, size_t raw_length,
                             char *out, size_t index) {
    size_t read_at;
    size_t write_at = 0U;
    size_t scan;

    for (read_at = 0U; read_at < raw_length; ++read_at) {
        uint8_t byte = raw[read_at];

        if (byte == (uint8_t)'\\') {
            out[write_at++] = '/';
        } else if (byte == (uint8_t)':' || byte < 0x20U ||
                   byte == (uint8_t)'*' || byte == (uint8_t)'?' ||
                   byte == (uint8_t)'"' || byte == (uint8_t)'<' ||
                   byte == (uint8_t)'>' || byte == (uint8_t)'|') {
            out[write_at++] = '_';
        } else {
            out[write_at++] = (char)byte;
        }
    }
    out[write_at] = '\0';

    /* An absolute path must not anchor at the destination root. */
    scan = 0U;
    while (out[scan] == '/') ++scan;
    if (scan != 0U) {
        size_t move = 0U;

        while (out[scan + move] != '\0') {
            out[move] = out[scan + move];
            ++move;
        }
        out[move] = '\0';
        write_at = move;
    }

    /* Neuter every component that could climb out of the destination. */
    scan = 0U;
    while ((scan + 2U) < (write_at + 1U) && out[scan] != '\0') {
        if (out[scan] == '.' && out[scan + 1] == '.' && out[scan + 2] == '/') {
            out[scan] = '_';
            out[scan + 1] = '_';
            scan += 3U;
        } else {
            ++scan;
        }
    }

    if (out[0] == '\0') {
        xx_rt_snprintf(out, 32U, "record%u", (unsigned)index);
    }
}

/* Walks the record chain under one candidate layout. Returns true only when
 * the chain ends on the 0xFFFF terminator; whatever could be parsed is left
 * in *stream either way, so a damaged walk is still comparable. */
static bool xx_igf2_walk(Abstractformat *self, xx_pd_struct *pd, int64_t span,
                         int64_t directory_offset,
                         const xx_igf2_layout *layout,
                         xx_igf2_stream *stream) {
    uint8_t record[XX_IGF2_RECORD_SIZE];
    uint8_t raw_name[XX_IGF2_MAX_NAME];
    char name[XX_IGF2_MAX_NAME + 32];
    int64_t position = directory_offset;

    while ((position + 4) <= span) {
        xx_igf2_member member;
        int64_t raw_size;
        int64_t packed_size;
        int64_t name_offset;
        int64_t name_room;
        int64_t terminator;
        int64_t relative;
        int64_t data_offset;
        int64_t scan;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_igf2_read_at(self, self->base_address + position, record, 4U)) {
            return false;
        }
        /* The terminator is often truncated at EOF, so its tag is checked
         * before a whole record is required. */
        if (xx_igf2_le16(record + 2) == XX_IGF2_END_TAG) return true;
        /* Every non-terminator record repeats the 0xECDB record magic; this
         * is what stops a wrong layout from walking off into data. */
        if (xx_igf2_le16(record) != XX_IGF2_RECORD_MAGIC) return false;
        if (!xx_igf2_range_within(span, position, (int64_t)XX_IGF2_RECORD_SIZE)) {
            return false;
        }
        if (!xx_igf2_read_at(self, self->base_address + position, record,
                             (size_t)XX_IGF2_RECORD_SIZE)) {
            return false;
        }

        raw_size = (int64_t)(int32_t)xx_igf2_le32(record + layout->raw_offset);
        packed_size =
            (int64_t)(int32_t)xx_igf2_le32(record + layout->packed_offset);
        if (raw_size < 0 || packed_size < 0) return false;

        name_offset = position + layout->name_offset;
        name_room = span - name_offset;
        if (name_room > XX_IGF2_MAX_NAME) name_room = XX_IGF2_MAX_NAME;
        if (name_room <= 0) return false;
        if (!xx_igf2_read_at(self, self->base_address + name_offset, raw_name,
                             (size_t)name_room)) {
            return false;
        }
        terminator = -1;
        for (scan = 0; scan < name_room; ++scan) {
            if (raw_name[scan] == 0) {
                terminator = scan;
                break;
            }
        }
        /* Terminator at 0 means an empty name, which is also how a wrong
         * layout usually shows itself: it lands on padding. */
        if (terminator <= 0) return false;

        /* The data starts at the next multiple of four measured from the
         * record start, strictly past the terminator, so an already aligned
         * position still advances by four. */
        relative = (name_offset + terminator + 1) - position;
        relative = (relative + 4) & ~(int64_t)3;
        data_offset = position + relative;
        if (data_offset < 0 || data_offset > span) return false;
        if (!xx_igf2_range_within(span, data_offset, packed_size)) return false;

        xx_mem_zero(&member, sizeof(member));
        xx_igf2_fix_name(raw_name, (size_t)terminator, name, stream->count);
        member.name = xx_str_dup(name);
        if (!member.name) return false;
        member.header_offset = self->base_address + position;
        member.header_size = data_offset - position;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = packed_size;
        if (raw_size == 0 && packed_size == 0) {
            /* The record states no method. Both sizes zero is how this
             * container spells an empty stored member. */
            member.method = XX_IGF2_METHOD_STORE;
            member.uncompressed_size = 0;
        } else {
            member.method = XX_IGF2_METHOD_LZH4;
            member.uncompressed_size = raw_size;
        }
        /* Seconds since the Unix epoch already; 0 means "no timestamp". */
        member.timestamp = (uint64_t)xx_igf2_le32(record + layout->time_offset);
        member.is_folder = false;
        if (!xx_igf2_add(stream, &member)) {
            xx_str_free(member.name);
            return false;
        }
        if (stream->count > (size_t)XX_IGF2_MAX_MEMBERS) return false;

        position = data_offset + packed_size;
    }
    /* Ran out of file without meeting a terminator. */
    return false;
}

static xx_igf2_stream *xx_igf2_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_igf2_stream *stream = NULL;
    xx_igf2_stream *other = NULL;
    uint8_t header[XX_IGF2_HEADER_SIZE];
    uint8_t first[XX_IGF2_RECORD_SIZE];
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    uint32_t directory_raw;
    uint32_t directory_not;
    bool terminated;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (XX_IGF2_HEADER_SIZE + XX_IGF2_RECORD_SIZE)) return NULL;

    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_igf2_read_at(self, self->base_address, header,
                         (size_t)XX_IGF2_HEADER_SIZE)) {
        return NULL;
    }
    if (xx_igf2_le16(header) != XX_IGF2_MAGIC) return NULL;
    /* The stored size must be the real size: this is what makes a two byte
     * magic safe to detect on, and it also rules out an appended overlay.
     * Loosening it to "<= span" would match on any file that happens to
     * start 24 13. */
    if ((int64_t)(int32_t)xx_igf2_le32(header + 8) != span) return NULL;
    if ((int32_t)xx_igf2_le32(header + 0x0c) <= 0) return NULL;

    directory_raw = xx_igf2_le32(header + 0x20);
    directory_not = xx_igf2_le32(header + 0x24);
    if (directory_raw == 0U || directory_raw > 0x7FFFFFFFU) return NULL;
    /* The one's-complement word at 0x24 is the second half of the
     * detection: 32 bits that must invert the directory offset exactly. */
    if (directory_raw != (uint32_t)~directory_not) return NULL;
    directory_offset = (int64_t)directory_raw;
    if (!xx_igf2_range_within(span, directory_offset,
                              (int64_t)XX_IGF2_RECORD_SIZE)) {
        return NULL;
    }

    /* The first record must carry the record magic before any walking is
     * attempted; this is the third leg of the detection. */
    if (!xx_igf2_read_at(self, self->base_address + directory_offset, first,
                         (size_t)XX_IGF2_RECORD_SIZE)) {
        return NULL;
    }
    if (xx_igf2_le16(first) != XX_IGF2_RECORD_MAGIC) return NULL;

    stream = (xx_igf2_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    terminated = xx_igf2_walk(self, pd, span, directory_offset,
                              &xx_igf2_layouts[0], stream);
    if (!terminated) {
        other = (xx_igf2_stream *)xx_mem_alloc(sizeof(*other));
        if (!other) goto fail;
        xx_mem_zero(other, sizeof(*other));
        if (xx_igf2_walk(self, pd, span, directory_offset,
                         &xx_igf2_layouts[1], other) ||
            (other->count > stream->count)) {
            /* Either the second shape reached the terminator, or neither
             * did and this damaged walk recovered more; the reference
             * layout wins a tie. */
            xx_igf2_stream_free(stream);
            stream = other;
            other = NULL;
        }
        xx_igf2_stream_free(other);
        other = NULL;
    }

    if (stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* The header's own size field is the archive size, and it was already
     * required to equal the span. */
    stream->archive_size = span;
    return stream;

fail:
    xx_igf2_stream_free(other);
    xx_igf2_stream_free(stream);
    return NULL;
}


/* Refuse to allocate more than this for one member, whatever the record
 * claims: the raw size is attacker-controlled. */

/* The container has no method field; parse derives these two from the
 * stated sizes, so they are the only values that can appear here. */

static bool xx_igf2_decode(Abstractformat *self, const xx_igf2_member *member,
                           uint8_t **out, size_t *out_size,
                           xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* Any method but the two parse can produce would mean a reader change
     * that forgot this switch; treating an unknown one as stored hands the
     * caller LZH-coded bytes dressed up as data. */
    if (member->method != XX_IGF2_METHOD_STORE &&
        member->method != XX_IGF2_METHOD_LZH4) {
        return false;
    }
    if (member->compressed_size < 0 || member->uncompressed_size < 0 ||
        member->uncompressed_size > XX_IGF2_MAX_DECODED ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    if (member->uncompressed_size == 0) {
        /* A member that claims nothing decodes to nothing; the caller still
         * gets a block it can free. No decoder is run, so no decoder can
         * disagree about a length of zero. */
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        plain[0] = 0U;
        *out = plain;
        *out_size = 0U;
        return true;
    }
    if (member->compressed_size <= 0) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_igf2_read_at(self, member->data_offset, packed,
                         (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == XX_IGF2_METHOD_STORE) {
        size_t index;
        /* parse only marks a member stored when both sizes are zero, so this
         * path is unreachable for a nonzero length; it is kept so that a
         * future stored variant cannot silently fall through to the LZH
         * decoder. */
        if (member->uncompressed_size != member->compressed_size) {
            xx_mem_free(packed);
            xx_mem_free(plain);
            return false;
        }
        for (index = 0U; index < (size_t)member->uncompressed_size; ++index) {
            plain[index] = packed[index];
        }
        written = (size_t)member->uncompressed_size;
    } else if (!xx_lzh5_decode_memory(packed, (size_t)member->compressed_size,
                                      plain,
                                      (size_t)member->uncompressed_size, 4,
                                      &written)) {
        written = 0U;
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* A short decode reported as success is the one failure a caller cannot
     * detect, so the decoded length must match the record exactly. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_igf2_init(xx_igf2 *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_IGF2;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-igf");
    xx_format_set_extension(&archive->format, "igf");
    archive->format.check_is_valid = xx_igf2_check_is_valid;
    archive->format.handle_base_info = xx_igf2_handle_base_info;
    archive->format.get_format_size = xx_igf2_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_igf2_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_igf2_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_igf2_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_igf2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_igf2_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_igf2_free_archive_records_reading;
    archive->format.destroy = xx_igf2_vtable_destroy;
}

xx_igf2 *xx_igf2_create(xx_io_device *device, int64_t base_address) {
    xx_igf2 *archive = (xx_igf2 *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_igf2_init(archive, device, base_address);
    return archive;
}

void xx_igf2_destroy(xx_igf2 *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_igf2_free(xx_igf2 *archive) {
    if (!archive) return;
    xx_igf2_destroy(archive);
    xx_mem_free(archive);
}

static void xx_igf2_vtable_destroy(Abstractformat *self) {
    xx_igf2_destroy((xx_igf2 *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_igf2_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_igf2_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_igf2_parse(self, pd);
    if (!stream) return false;
    xx_igf2_stream_free(stream);
    return true;
}

bool xx_igf2_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_igf2 *archive = (xx_igf2 *)self;
    xx_igf2_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_igf2_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_igf2_stream_free(stream);
    return true;
}

int64_t xx_igf2_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_igf2_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_igf2 *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_igf2_set_record(xx_archive_record *record,
                                 const xx_igf2_member *member) {
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

static bool xx_igf2_copy_options(xx_list_s *target,
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

static const xx_var *xx_igf2_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_igf2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_igf2_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_igf2_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_igf2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_igf2_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_igf2_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_igf2_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_igf2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_igf2_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_igf2_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_igf2_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_igf2_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_igf2_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_igf2_stream *stream;
    const xx_igf2_member *member;
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
    stream = (xx_igf2_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_igf2_path_safe(member->name)) return false;

    path_option = xx_igf2_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_igf2_decode(self, member, &plain, &plain_size, pd);
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
        !xx_igf2_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_igf2_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
