/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Powerboard BBS (DOS, 1993-1996) library files - PBBS.BBS, EXPRESS.EXP,
 * LETTERS.LTR and friends.
 *
 * The container is HEADERLESS: no magic, no index, no trailer, no version
 * word.  It is a bare chain of stored records, each laid out as
 *
 *     +0x00  u8   lead
 *     +0x01  name field, (lead base length) + 3 bytes
 *     +...   size field, 1, 2 or 4 bytes little-endian
 *     +...   raw member data, `size` bytes
 *
 * The lead byte encodes two things at once:
 *
 *     lead 11..18  ->  base name length = lead - 10, size field is 1 byte
 *     lead  1..8   ->  base name length = lead,      size field is 2 or 4
 *
 * The name field is the unpadded base name followed by a fixed 3-byte
 * extension field, blank-filled when there is no extension: "LAUGH" is stored
 * as "LAUGH" + "   " and "PWRMAIL.EXE" as "PWRMAIL" + "EXE".  A space inside
 * a field - rather than only trailing it - is not a name and ends the parse.
 *
 * For the lead 1..8 form the writer used a Turbo Pascal Integer when the
 * member fitted in one (<= 0x7fff bytes) and a LongInt otherwise, with no
 * flag bit to tell the two apart; the low word of a LongInt size is very
 * often below 0x8000 (a 335556 byte member stores C4 1E 05 00).  Only the
 * chain itself disambiguates them, so the walk keeps a bounded backtracking
 * stack: it takes the 2-byte reading first and falls back to the 4-byte one
 * when the tail stops chaining.
 *
 * With no magic to lean on, acceptance is the whole defence: the entire span
 * must parse as an unbroken chain landing exactly on EOF, every record
 * carrying a non-empty DOS-legal 8.3 name and a non-zero payload.
 *
 * Members are STORED; the format has no compression at all.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/powerboardbbs/xx_powerboardbbs.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_POWERBOARDBBS_COPY_CHUNK (64 * 1024)

typedef struct xx_powerboardbbs_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_powerboardbbs_member;

typedef struct xx_powerboardbbs_stream_s {
    xx_powerboardbbs_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_powerboardbbs_stream;

static void xx_powerboardbbs_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_powerboardbbs_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_powerboardbbs_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_powerboardbbs_path_safe(const char *name) {
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

static void xx_powerboardbbs_stream_free(void *pointer) {
    xx_powerboardbbs_stream *stream = (xx_powerboardbbs_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_powerboardbbs_add(xx_powerboardbbs_stream *stream,
                          const xx_powerboardbbs_member *member) {
    xx_powerboardbbs_member *grown = (xx_powerboardbbs_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_powerboardbbs_decode(Abstractformat *self,
                             const xx_powerboardbbs_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *buffer;

    *out = NULL;
    *out_size = 0U;
    if (member->compressed_size < 0 ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!buffer) return false;
    if (member->compressed_size != 0 &&
        ((pd && xx_pd_is_stopped(pd)) ||
         !xx_powerboardbbs_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


/* The extension field is a fixed three bytes; the base name in front of it is
 * unpadded and 1..8 bytes, so the whole name field is 4..11 bytes. */
#define XX_POWERBOARDBBS_EXT_FIELD 3
#define XX_POWERBOARDBBS_MIN_BASE 1
#define XX_POWERBOARDBBS_MAX_BASE 8
/* Adding 10 to the base length is the writer's flag for "the size field is a
 * single byte"; without it the size is a 2- or 4-byte little-endian value. */
#define XX_POWERBOARDBBS_LEAD_BIAS 10
/* A Turbo Pascal Integer tops out here; anything larger was a LongInt. */
#define XX_POWERBOARDBBS_INTEGER_MAX 0x7fff
/* The largest header is 1 lead + 11 name + 4 size. */
#define XX_POWERBOARDBBS_MAX_HEADER 16
/* Smallest record: lead + 4-byte name field + 1-byte size + 1 byte payload. */
#define XX_POWERBOARDBBS_MIN_RECORD 7
/* "8" + "." + "3" + NUL, rounded up. */
#define XX_POWERBOARDBBS_NAME_BUFFER 16
#define XX_POWERBOARDBBS_MAX_MEMBERS 65536
#define XX_POWERBOARDBBS_MAX_BACKTRACKS 4096
#define XX_POWERBOARDBBS_MAX_MEMBER_SIZE 0x40000000 /* 1 GB sanity cap */

typedef struct {
    int32_t count; /* how many readings of the size field are live: 1 or 2 */
    int32_t name_length;
    int32_t width[2];
    int64_t size[2];
    char name[XX_POWERBOARDBBS_NAME_BUFFER];
} xx_powerboardbbs_header;

/* A record whose size field had a second reading, plus how many members were
 * already accepted when we chose the first one. */
typedef struct {
    int64_t header_offset;
    size_t member_count;
    int32_t width;
    int32_t name_length;
    int64_t size;
    char name[XX_POWERBOARDBBS_NAME_BUFFER];
} xx_powerboardbbs_choice;

static uint16_t xx_powerboardbbs_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_powerboardbbs_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* DOS-legal 8.3 characters.  The corpus only ever uses letters, digits and
 * " ! $ - _ ", but the rest of the DOS set is accepted so that a legitimate
 * member cannot be rejected over punctuation.  Every character here is inside
 * 0x20..0x7E, so this is strictly narrower than the printable-ASCII rule. */
static bool xx_powerboardbbs_is_name_char(uint8_t character) {
    if (character >= 'A' && character <= 'Z') return true;
    if (character >= 'a' && character <= 'z') return true;
    if (character >= '0' && character <= '9') return true;
    switch (character) {
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case '\'':
        case '(':
        case ')':
        case '-':
        case '@':
        case '^':
        case '_':
        case '`':
        case '{':
        case '}':
        case '~':
            return true;
        default:
            return false;
    }
}

/* One field of the name: printable DOS characters, then blank padding.  A
 * space in the middle of a field means this is not a record header at all,
 * which is most of what keeps a random byte stream from chaining. */
static bool xx_powerboardbbs_check_field(const uint8_t *data, int32_t size,
                                         int32_t *used) {
    int32_t index;
    int32_t last = 0;
    bool padding = false;

    for (index = 0; index < size; ++index) {
        const uint8_t character = data[index];

        if (character == ' ') {
            padding = true;
            continue;
        }
        if (padding) return false;
        if (!xx_powerboardbbs_is_name_char(character)) return false;
        last = index + 1;
    }
    if (used) *used = last;
    return true;
}

/* Decode the record header at @p offset (relative to base_address).  Returns
 * false when this is not a record header; on success header->count is 1 or 2
 * and the readings are ordered narrow first. */
static bool xx_powerboardbbs_read_header(Abstractformat *self, int64_t span,
                                         int64_t offset,
                                         xx_powerboardbbs_header *header) {
    uint8_t buffer[XX_POWERBOARDBBS_MAX_HEADER];
    const uint8_t *name_field;
    int64_t available;
    int64_t size_offset;
    int64_t size;
    int32_t base_length;
    int32_t name_length;
    int32_t size_index;
    int32_t base_used = 0;
    int32_t ext_used = 0;
    int32_t written = 0;
    int32_t index;
    uint8_t lead;
    bool single_byte_size = false;

    xx_mem_zero(header, sizeof(*header));

    if (!xx_powerboardbbs_range_within(span, offset,
                                       XX_POWERBOARDBBS_MIN_RECORD)) {
        return false;
    }
    /* The last record's header is allowed to sit closer to EOF than the
     * widest possible header, so read only what is there. */
    available = span - offset;
    if (available > XX_POWERBOARDBBS_MAX_HEADER) {
        available = XX_POWERBOARDBBS_MAX_HEADER;
    }
    if (!xx_powerboardbbs_read_at(self, self->base_address + offset, buffer,
                                  (size_t)available)) {
        return false;
    }

    lead = buffer[0];
    if (lead >= (XX_POWERBOARDBBS_LEAD_BIAS + XX_POWERBOARDBBS_MIN_BASE) &&
        lead <= (XX_POWERBOARDBBS_LEAD_BIAS + XX_POWERBOARDBBS_MAX_BASE)) {
        base_length = (int32_t)lead - XX_POWERBOARDBBS_LEAD_BIAS;
        single_byte_size = true;
    } else if (lead >= XX_POWERBOARDBBS_MIN_BASE &&
               lead <= XX_POWERBOARDBBS_MAX_BASE) {
        base_length = (int32_t)lead;
    } else {
        /* Only 16 of the 256 lead values mean anything.  This is the first
         * gate a random byte stream has to pass, and the one a later reader
         * will be tempted to widen. */
        return false;
    }

    name_length = base_length + XX_POWERBOARDBBS_EXT_FIELD;
    if ((int64_t)(1 + name_length) > available) return false;
    name_field = buffer + 1;

    if (!xx_powerboardbbs_check_field(name_field, base_length, &base_used)) {
        return false;
    }
    if (base_used == 0) return false; /* a member with no name at all */
    if (!xx_powerboardbbs_check_field(name_field + base_length,
                                      XX_POWERBOARDBBS_EXT_FIELD,
                                      &ext_used)) {
        return false;
    }

    for (index = 0; index < base_used; ++index) {
        header->name[written++] = (char)name_field[index];
    }
    if (ext_used > 0) {
        header->name[written++] = '.';
        for (index = 0; index < ext_used; ++index) {
            header->name[written++] = (char)name_field[base_length + index];
        }
    }
    header->name[written] = '\0';
    header->name_length = name_length;

    size_index = 1 + name_length;
    size_offset = offset + size_index;

    if (single_byte_size) {
        if ((int64_t)(size_index + 1) > available) return false;
        size = (int64_t)buffer[size_index];
        /* A zero-length member never occurs and would let the chain stall on
         * a run of identical bytes; treat it as a non-header. */
        if (size >= 1 &&
            xx_powerboardbbs_range_within(span, size_offset + 1, size)) {
            header->width[0] = 1;
            header->size[0] = size;
            header->count = 1;
        }
        return header->count > 0;
    }

    /* Prefer the narrow reading: it is the common case, and the walk falls
     * back to the wide one when the tail stops chaining. */
    if ((int64_t)(size_index + 2) <= available) {
        size = (int64_t)xx_powerboardbbs_le16(buffer + size_index);
        if (size >= 1 && size <= XX_POWERBOARDBBS_INTEGER_MAX &&
            xx_powerboardbbs_range_within(span, size_offset + 2, size)) {
            header->width[header->count] = 2;
            header->size[header->count] = size;
            header->count++;
        }
    }
    if ((int64_t)(size_index + 4) <= available) {
        size = (int64_t)xx_powerboardbbs_le32(buffer + size_index);
        /* The two readings are made disjoint on purpose: a value that fits a
         * Pascal Integer would have been written as one, so the LongInt
         * reading only ever offers sizes the narrow one cannot express. */
        if (size > XX_POWERBOARDBBS_INTEGER_MAX &&
            size <= XX_POWERBOARDBBS_MAX_MEMBER_SIZE &&
            xx_powerboardbbs_range_within(span, size_offset + 4, size)) {
            header->width[header->count] = 4;
            header->size[header->count] = size;
            header->count++;
        }
    }
    return header->count > 0;
}

static bool xx_powerboardbbs_push_choice(xx_powerboardbbs_choice **stack,
                                         size_t *count, size_t *capacity,
                                         const xx_powerboardbbs_choice *choice) {
    if (*count == *capacity) {
        size_t grown_capacity = (*capacity == 0U) ? 16U : (*capacity * 2U);
        xx_powerboardbbs_choice *grown = (xx_powerboardbbs_choice *)
            xx_mem_realloc(*stack, sizeof(*grown) * grown_capacity);

        if (!grown) return false;
        *stack = grown;
        *capacity = grown_capacity;
    }
    (*stack)[(*count)++] = *choice;
    return true;
}

/* Drop members accepted after a backtrack point, freeing their names. */
static void xx_powerboardbbs_rewind(xx_powerboardbbs_stream *stream,
                                    size_t member_count) {
    while (stream->count > member_count) {
        stream->count--;
        xx_str_free(stream->items[stream->count].name);
        stream->items[stream->count].name = NULL;
    }
}

static bool xx_powerboardbbs_append(xx_powerboardbbs_stream *stream,
                                    Abstractformat *self, int64_t offset,
                                    int32_t name_length, int32_t width,
                                    int64_t size, const char *name,
                                    int64_t *next_offset) {
    xx_powerboardbbs_member member;
    const int64_t header_size = 1 + (int64_t)name_length + (int64_t)width;
    char *copy = xx_str_dup(name);

    if (!copy) return false;
    xx_mem_zero(&member, sizeof(member));
    member.name = copy;
    member.header_offset = self->base_address + offset;
    member.header_size = header_size;
    member.data_offset = self->base_address + offset + header_size;
    member.compressed_size = size;
    member.uncompressed_size = size;
    if (!xx_powerboardbbs_add(stream, &member)) {
        xx_str_free(copy);
        return false;
    }
    *next_offset = offset + header_size + size;
    return true;
}

static xx_powerboardbbs_stream *xx_powerboardbbs_parse(Abstractformat *self,
                                                       xx_pd_struct *pd) {
    xx_powerboardbbs_stream *stream = NULL;
    xx_powerboardbbs_choice *choices = NULL;
    size_t choice_count = 0U;
    size_t choice_capacity = 0U;
    int64_t total;
    int64_t span;
    int64_t offset = 0;
    int32_t backtracks = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_POWERBOARDBBS_MIN_RECORD) return NULL;

    stream = (xx_powerboardbbs_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (true) {
        xx_powerboardbbs_header header;
        bool advanced = false;
        bool resumed = false;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (offset == span) break; /* the chain landed exactly on EOF */

        if (offset < span &&
            xx_powerboardbbs_read_header(self, span, offset, &header)) {
            if (stream->count >= (size_t)XX_POWERBOARDBBS_MAX_MEMBERS) {
                goto fail;
            }
            if (header.count > 1) {
                xx_powerboardbbs_choice choice;
                int32_t index;

                xx_mem_zero(&choice, sizeof(choice));
                choice.header_offset = offset;
                choice.member_count = stream->count;
                choice.width = header.width[1];
                choice.size = header.size[1];
                choice.name_length = header.name_length;
                for (index = 0; index < XX_POWERBOARDBBS_NAME_BUFFER;
                     ++index) {
                    choice.name[index] = header.name[index];
                }
                if (!xx_powerboardbbs_push_choice(&choices, &choice_count,
                                                  &choice_capacity, &choice)) {
                    goto fail;
                }
            }
            if (!xx_powerboardbbs_append(stream, self, offset,
                                         header.name_length, header.width[0],
                                         header.size[0], header.name,
                                         &offset)) {
                goto fail;
            }
            advanced = true;
        }
        if (advanced) continue;

        /* Dead end: retry the most recent record whose size field had a
         * second reading, discarding everything accepted after it. */
        while (choice_count > 0U) {
            xx_powerboardbbs_choice choice;

            if (backtracks >= XX_POWERBOARDBBS_MAX_BACKTRACKS) goto fail;
            backtracks++;
            choice = choices[--choice_count];
            if (choice.member_count > stream->count) goto fail;
            xx_powerboardbbs_rewind(stream, choice.member_count);

            /* The alternative was bounds-checked when the header was decoded,
             * so it can be applied without re-reading it. */
            if (!xx_powerboardbbs_append(stream, self, choice.header_offset,
                                         choice.name_length, choice.width,
                                         choice.size, choice.name, &offset)) {
                goto fail;
            }
            resumed = true;
            break;
        }
        if (!resumed) goto fail;
    }

    /* Landing on EOF with nothing accepted means the span was not a chain. */
    if (stream->count == 0U) goto fail;
    xx_mem_free(choices);
    stream->archive_size = span;
    return stream;

fail:
    xx_mem_free(choices);
    xx_powerboardbbs_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_powerboardbbs_init(xx_powerboardbbs *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_POWERBOARDBBS;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-powerboard-bbs");
    xx_format_set_extension(&archive->format, "bbs");
    archive->format.check_is_valid = xx_powerboardbbs_check_is_valid;
    archive->format.handle_base_info = xx_powerboardbbs_handle_base_info;
    archive->format.get_format_size = xx_powerboardbbs_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_powerboardbbs_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_powerboardbbs_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_powerboardbbs_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_powerboardbbs_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_powerboardbbs_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_powerboardbbs_free_archive_records_reading;
    archive->format.destroy = xx_powerboardbbs_vtable_destroy;
}

xx_powerboardbbs *xx_powerboardbbs_create(xx_io_device *device, int64_t base_address) {
    xx_powerboardbbs *archive = (xx_powerboardbbs *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_powerboardbbs_init(archive, device, base_address);
    return archive;
}

void xx_powerboardbbs_destroy(xx_powerboardbbs *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_powerboardbbs_free(xx_powerboardbbs *archive) {
    if (!archive) return;
    xx_powerboardbbs_destroy(archive);
    xx_mem_free(archive);
}

static void xx_powerboardbbs_vtable_destroy(Abstractformat *self) {
    xx_powerboardbbs_destroy((xx_powerboardbbs *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_powerboardbbs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_powerboardbbs_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_powerboardbbs_parse(self, pd);
    if (!stream) return false;
    xx_powerboardbbs_stream_free(stream);
    return true;
}

bool xx_powerboardbbs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_powerboardbbs *archive = (xx_powerboardbbs *)self;
    xx_powerboardbbs_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_powerboardbbs_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_powerboardbbs_stream_free(stream);
    return true;
}

int64_t xx_powerboardbbs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_powerboardbbs_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_powerboardbbs *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_powerboardbbs_set_record(xx_archive_record *record,
                                 const xx_powerboardbbs_member *member) {
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

static bool xx_powerboardbbs_copy_options(xx_list_s *target,
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

static const xx_var *xx_powerboardbbs_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_powerboardbbs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_powerboardbbs_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_powerboardbbs_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_powerboardbbs_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_powerboardbbs_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_powerboardbbs_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_powerboardbbs_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_powerboardbbs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_powerboardbbs_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_powerboardbbs_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_powerboardbbs_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_powerboardbbs_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_powerboardbbs_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_powerboardbbs_stream *stream;
    const xx_powerboardbbs_member *member;
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
    stream = (xx_powerboardbbs_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_powerboardbbs_path_safe(member->name)) return false;

    path_option = xx_powerboardbbs_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_powerboardbbs_decode(self, member, &plain, &plain_size, pd);
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
        !xx_powerboardbbs_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_powerboardbbs_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
