/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * FrontPage theme packages (.elm).
 *
 * Everything in front of the payload is text, one item per line, LF only:
 *
 *   line 0        dotted-decimal generator version, e.g. "3.0.2.1330"
 *   line 1        member count, decimal, no leading zero
 *   lines 2..n+1  one directory entry per member: "<name>,<size>"
 *
 * The payload follows immediately after the last directory line, members in
 * directory order:
 *
 *   "<==MS-Theme==>"   14 bytes, a PREFIX of every member
 *   <size> bytes       the member itself, stored verbatim
 *
 * The marker is a prefix rather than a separator, so the zero-length member
 * every theme carries puts two markers back to back. Nothing terminates the
 * chain: it is accepted only when the last member ends exactly on EOF, and
 * that -- together with a marker in front of every single member -- is what
 * keeps a text file that happens to open with a version line and a number
 * from parsing as a theme. There is no binary magic at offset 0 to lean on.
 *
 * The size in the directory is the only place a member's length is recorded,
 * so the payload offsets cannot be read; they are accumulated by walking the
 * chain, which is why the directory is parsed in full before any payload is
 * touched.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/frontpagetheme/xx_frontpagetheme.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_FRONTPAGETHEME_COPY_CHUNK (64 * 1024)

typedef struct xx_frontpagetheme_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_frontpagetheme_member;

typedef struct xx_frontpagetheme_stream_s {
    xx_frontpagetheme_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_frontpagetheme_stream;

static void xx_frontpagetheme_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_frontpagetheme_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_frontpagetheme_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_frontpagetheme_path_safe(const char *name) {
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

static void xx_frontpagetheme_stream_free(void *pointer) {
    xx_frontpagetheme_stream *stream = (xx_frontpagetheme_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_frontpagetheme_add(xx_frontpagetheme_stream *stream,
                          const xx_frontpagetheme_member *member) {
    xx_frontpagetheme_member *grown = (xx_frontpagetheme_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_frontpagetheme_decode(Abstractformat *self,
                             const xx_frontpagetheme_member *member, uint8_t **out,
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
         !xx_frontpagetheme_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_FRONTPAGETHEME_MARKER_SIZE 14
/* "3.0.2.1330\n" + "1\n" + "a,0\n" + one marker: nothing shorter can be a
 * theme, and the preamble reads below rely on this floor. */
#define XX_FRONTPAGETHEME_MIN_SIZE 24
#define XX_FRONTPAGETHEME_MAX_VERSION 32
#define XX_FRONTPAGETHEME_MAX_COUNT_DIGITS 7
#define XX_FRONTPAGETHEME_MAX_SIZE_DIGITS 10
#define XX_FRONTPAGETHEME_MAX_LINE 300
#define XX_FRONTPAGETHEME_MAX_NAME 255
#define XX_FRONTPAGETHEME_MAX_MEMBERS 65536
#define XX_FRONTPAGETHEME_MAX_MEMBER_SIZE 0x10000000 /* 256 MB sanity cap */
/* Cheapest possible member: a four-byte directory line ("a,0\n") plus the
 * fourteen-byte marker. Used to reject an inflated count before the
 * directory is walked at all. */
#define XX_FRONTPAGETHEME_MIN_MEMBER_COST 18
#define XX_FRONTPAGETHEME_CHUNK 512

static const uint8_t XX_FRONTPAGETHEME_MARKER[XX_FRONTPAGETHEME_MARKER_SIZE] =
    {'<', '=', '=', 'M', 'S', '-', 'T', 'h', 'e', 'm', 'e', '=', '=', '>'};

/* The whole preamble and directory are text of unbounded total length, so
 * they are consumed through a small refilling window rather than read into
 * one buffer sized from the member count -- a count of 65536 would otherwise
 * justify a ~20 MB allocation before a single byte has been validated. */
typedef struct xx_frontpagetheme_reader_s {
    Abstractformat *self;
    int64_t span;
    int64_t offset; /* next byte to pull from the device, relative to base */
    size_t fill;
    size_t position;
    uint8_t data[XX_FRONTPAGETHEME_CHUNK];
} xx_frontpagetheme_reader;

/* Bytes handed out so far: what is left in the window has been read from the
 * device but not yet consumed. */
static int64_t xx_frontpagetheme_consumed(
    const xx_frontpagetheme_reader *reader) {
    return reader->offset - (int64_t)(reader->fill - reader->position);
}

static bool xx_frontpagetheme_next(xx_frontpagetheme_reader *reader,
                                   uint8_t *value) {
    if (reader->position >= reader->fill) {
        int64_t remaining = reader->span - reader->offset;
        size_t want;

        if (remaining <= 0) return false;
        want = (size_t)XX_FRONTPAGETHEME_CHUNK;
        if ((int64_t)want > remaining) want = (size_t)remaining;
        if (!xx_frontpagetheme_read_at(reader->self,
                                       reader->self->base_address +
                                           reader->offset,
                                       reader->data, want)) {
            return false;
        }
        reader->offset += (int64_t)want;
        reader->fill = want;
        reader->position = 0U;
    }
    *value = reader->data[reader->position++];
    return true;
}

/* Reads up to and including the LF, which is not stored. Fails on EOF and on
 * a line longer than @p capacity -- an unterminated first line is how a
 * binary file that opens with digits gets thrown out cheaply. */
static bool xx_frontpagetheme_read_line(xx_frontpagetheme_reader *reader,
                                        char *buffer, size_t capacity,
                                        size_t *length) {
    size_t used = 0U;

    for (;;) {
        uint8_t value;

        if (!xx_frontpagetheme_next(reader, &value)) return false;
        if (value == (uint8_t)'\n') {
            buffer[used] = '\0';
            *length = used;
            return true;
        }
        if (used >= capacity) return false;
        buffer[used++] = (char)value;
    }
}

/* The version line is a dotted decimal ("3.0.2.1330", "3.0.2.926"). It sits
 * alone in front of the member count, so it carries the whole weight of the
 * first-pass rejection; loosening it to "any digits" lets every text file
 * beginning with a number reach the directory walk. */
static bool xx_frontpagetheme_is_version(const char *text, size_t length) {
    size_t index;
    int dots = 0;
    char previous = 0;

    if (length == 0U || length > XX_FRONTPAGETHEME_MAX_VERSION) return false;
    if (text[0] < '0' || text[0] > '9') return false;
    if (text[length - 1U] == '.') return false;
    for (index = 0U; index < length; ++index) {
        char value = text[index];

        if (value == '.') {
            if (previous == '.') return false;
            ++dots;
        } else if (value < '0' || value > '9') {
            return false;
        }
        previous = value;
    }
    return dots >= 1;
}

/* A member name is a bare file name: printable ASCII, no path separators, no
 * drive letters, no "..". The corpus only uses lowercase letters, digits,
 * '.' and '_'; the check is deliberately a little wider so an unusual theme
 * is not rejected outright. The comma exclusion is load-bearing: the size
 * follows the LAST comma on the line, and barring commas from names is what
 * keeps that split unambiguous in both directions. */
static bool xx_frontpagetheme_is_name(const char *text, size_t length) {
    size_t index;

    if (length == 0U || length > XX_FRONTPAGETHEME_MAX_NAME) return false;
    if (length == 1U && text[0] == '.') return false;
    if (length == 2U && text[0] == '.' && text[1] == '.') return false;
    for (index = 0U; index < length; ++index) {
        uint8_t value = (uint8_t)text[index];

        if (value < 0x20U || value > 0x7EU) return false;
        if (value == (uint8_t)'/' || value == (uint8_t)'\\' ||
            value == (uint8_t)':' || value == (uint8_t)'*' ||
            value == (uint8_t)'?' || value == (uint8_t)'"' ||
            value == (uint8_t)'<' || value == (uint8_t)'>' ||
            value == (uint8_t)'|' || value == (uint8_t)',') {
            return false;
        }
    }
    return true;
}

/* Strict unsigned decimal: no sign, no whitespace, and no leading zero --
 * the FrontPage generator never emits one, so accepting "007" would widen
 * the text preamble for nothing. */
static bool xx_frontpagetheme_decimal(const char *text, size_t length,
                                      size_t max_digits, int64_t max_value,
                                      int64_t *result) {
    size_t index;
    int64_t value = 0;

    if (length == 0U || length > max_digits) return false;
    if (length > 1U && text[0] == '0') return false;
    for (index = 0U; index < length; ++index) {
        if (text[index] < '0' || text[index] > '9') return false;
        value = value * 10 + (int64_t)(text[index] - '0');
        if (value > max_value) return false;
    }
    *result = value;
    return true;
}

static xx_frontpagetheme_stream *xx_frontpagetheme_parse(Abstractformat *self,
                                                         xx_pd_struct *pd) {
    xx_frontpagetheme_stream *stream = NULL;
    xx_frontpagetheme_reader reader;
    char line[XX_FRONTPAGETHEME_MAX_LINE + 1];
    uint8_t marker[XX_FRONTPAGETHEME_MARKER_SIZE];
    int64_t total;
    int64_t span;
    int64_t count = 0;
    int64_t index;
    int64_t payload = 0;
    int64_t directory_size;
    int64_t offset;
    size_t length = 0U;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_FRONTPAGETHEME_MIN_SIZE) return NULL;

    xx_mem_zero(&reader, sizeof(reader));
    reader.self = self;
    reader.span = span;

    if (!xx_frontpagetheme_read_line(&reader, line,
                                     XX_FRONTPAGETHEME_MAX_VERSION, &length) ||
        !xx_frontpagetheme_is_version(line, length)) {
        return NULL;
    }
    if (!xx_frontpagetheme_read_line(&reader, line,
                                     XX_FRONTPAGETHEME_MAX_COUNT_DIGITS,
                                     &length) ||
        !xx_frontpagetheme_decimal(line, length,
                                   XX_FRONTPAGETHEME_MAX_COUNT_DIGITS,
                                   XX_FRONTPAGETHEME_MAX_MEMBERS, &count) ||
        count < 1) {
        return NULL;
    }

    offset = xx_frontpagetheme_consumed(&reader);
    /* Every member costs at least a directory line and a marker, so a count
     * the file cannot possibly hold is thrown out here, before the walk. */
    if (count > (span - offset) / XX_FRONTPAGETHEME_MIN_MEMBER_COST) {
        return NULL;
    }

    stream = (xx_frontpagetheme_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* Pass 1: the directory. Names and sizes only -- a payload offset
     * depends on every size in front of it, so none of them is known yet. */
    for (index = 0; index < count; ++index) {
        xx_frontpagetheme_member member;
        int64_t size = 0;
        size_t comma;
        size_t cursor;
        bool found = false;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_frontpagetheme_read_line(&reader, line,
                                         XX_FRONTPAGETHEME_MAX_LINE,
                                         &length)) {
            goto fail;
        }
        comma = 0U;
        for (cursor = 0U; cursor < length; ++cursor) {
            if (line[cursor] == ',') {
                comma = cursor;
                found = true;
            }
        }
        if (!found || comma == 0U) goto fail;
        line[comma] = '\0';
        if (!xx_frontpagetheme_is_name(line, comma) ||
            !xx_frontpagetheme_decimal(line + comma + 1U,
                                       length - comma - 1U,
                                       XX_FRONTPAGETHEME_MAX_SIZE_DIGITS,
                                       XX_FRONTPAGETHEME_MAX_MEMBER_SIZE,
                                       &size)) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(line);
        if (!member.name) goto fail;
        member.header_size = XX_FRONTPAGETHEME_MARKER_SIZE;
        member.compressed_size = size;
        member.uncompressed_size = size;
        if (!xx_frontpagetheme_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        /* Running total of what the directory claims the payload weighs; a
         * set of sizes that cannot fit is caught without reading any of it. */
        payload += size + XX_FRONTPAGETHEME_MARKER_SIZE;
        if (payload > span) goto fail;
    }

    directory_size = xx_frontpagetheme_consumed(&reader);
    if (!xx_frontpagetheme_range_within(span, 0, directory_size)) goto fail;

    /* Pass 2: walk the payload chain and confirm the marker sits in front of
     * every member. This is the real gate -- the format has no magic at
     * offset 0, so a file that merely opens with a version line and a number
     * is only rejected here. */
    offset = directory_size;
    for (index = 0; index < (int64_t)stream->count; ++index) {
        xx_frontpagetheme_member *member = &stream->items[(size_t)index];

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_frontpagetheme_range_within(span, offset,
                                            XX_FRONTPAGETHEME_MARKER_SIZE) ||
            !xx_frontpagetheme_read_at(self, self->base_address + offset,
                                       marker, sizeof(marker)) ||
            xx_rt_memcmp(marker, XX_FRONTPAGETHEME_MARKER,
                         XX_FRONTPAGETHEME_MARKER_SIZE) != 0) {
            goto fail;
        }
        member->header_offset = self->base_address + offset;
        offset += XX_FRONTPAGETHEME_MARKER_SIZE;
        if (!xx_frontpagetheme_range_within(span, offset,
                                            member->compressed_size)) {
            goto fail;
        }
        member->data_offset = self->base_address + offset;
        offset += member->compressed_size;
    }

    /* The chain carries no terminator; it ends by landing exactly on EOF.
     * Accepting a short landing would make every prefix of a theme -- and a
     * good deal else -- look like a valid archive. */
    if (offset != span) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_frontpagetheme_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_frontpagetheme_init(xx_frontpagetheme *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_FRONTPAGETHEME;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-frontpage-theme");
    xx_format_set_extension(&archive->format, "elm");
    archive->format.check_is_valid = xx_frontpagetheme_check_is_valid;
    archive->format.handle_base_info = xx_frontpagetheme_handle_base_info;
    archive->format.get_format_size = xx_frontpagetheme_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_frontpagetheme_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_frontpagetheme_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_frontpagetheme_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_frontpagetheme_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_frontpagetheme_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_frontpagetheme_free_archive_records_reading;
    archive->format.destroy = xx_frontpagetheme_vtable_destroy;
}

xx_frontpagetheme *xx_frontpagetheme_create(xx_io_device *device, int64_t base_address) {
    xx_frontpagetheme *archive = (xx_frontpagetheme *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_frontpagetheme_init(archive, device, base_address);
    return archive;
}

void xx_frontpagetheme_destroy(xx_frontpagetheme *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_frontpagetheme_free(xx_frontpagetheme *archive) {
    if (!archive) return;
    xx_frontpagetheme_destroy(archive);
    xx_mem_free(archive);
}

static void xx_frontpagetheme_vtable_destroy(Abstractformat *self) {
    xx_frontpagetheme_destroy((xx_frontpagetheme *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_frontpagetheme_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_frontpagetheme_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_frontpagetheme_parse(self, pd);
    if (!stream) return false;
    xx_frontpagetheme_stream_free(stream);
    return true;
}

bool xx_frontpagetheme_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_frontpagetheme *archive = (xx_frontpagetheme *)self;
    xx_frontpagetheme_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_frontpagetheme_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_frontpagetheme_stream_free(stream);
    return true;
}

int64_t xx_frontpagetheme_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_frontpagetheme_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_frontpagetheme *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_frontpagetheme_set_record(xx_archive_record *record,
                                 const xx_frontpagetheme_member *member) {
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

static bool xx_frontpagetheme_copy_options(xx_list_s *target,
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

static const xx_var *xx_frontpagetheme_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_frontpagetheme_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_frontpagetheme_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_frontpagetheme_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_frontpagetheme_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_frontpagetheme_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_frontpagetheme_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_frontpagetheme_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_frontpagetheme_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_frontpagetheme_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_frontpagetheme_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_frontpagetheme_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_frontpagetheme_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_frontpagetheme_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_frontpagetheme_stream *stream;
    const xx_frontpagetheme_member *member;
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
    stream = (xx_frontpagetheme_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_frontpagetheme_path_safe(member->name)) return false;

    path_option = xx_frontpagetheme_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_frontpagetheme_decode(self, member, &plain, &plain_size, pd);
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
        !xx_frontpagetheme_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_frontpagetheme_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
