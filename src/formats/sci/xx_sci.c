/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SCI archives.
 *
 * Header, 46 bytes:
 *
 *   0x00  "SCI1" or "SCI2"   -- the version digit is the only variable byte
 *   0x04  "00 -"             -- so the banner opens "SCI100 - " / "SCI200 - "
 *   0x08  banner text, 32 bytes, differs between the two writers
 *   0x28  "nd.\r\n\x1a"       -- the tail of the banner's "...reserved.\r\n^Z"
 *
 * then, from 0x2E, a chain of records of 55 bytes each:
 *
 *   0x00  u8 kind
 *   0x01  name, 50 bytes, NUL terminated when shorter
 *   0x33  i32 LE member size
 *
 * The member's bytes follow the record directly and the next record starts
 * where they end, so there is no directory to index and no member count: the
 * chain is walked until it reaches the end of the file. A record whose data
 * runs past EOF ends the walk and the members before it are still published,
 * which is how a truncated archive stays readable.
 *
 * Names are DOS paths separated by backslashes; they are normalised to '/'
 * here. Every member is stored, never compressed.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sci/xx_sci.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_SCI_COPY_CHUNK (64 * 1024)

typedef struct xx_sci_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_sci_member;

typedef struct xx_sci_stream_s {
    xx_sci_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_sci_stream;

static void xx_sci_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_sci_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_sci_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_sci_path_safe(const char *name) {
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

static void xx_sci_stream_free(void *pointer) {
    xx_sci_stream *stream = (xx_sci_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_sci_add(xx_sci_stream *stream,
                          const xx_sci_member *member) {
    xx_sci_member *grown = (xx_sci_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_sci_decode(Abstractformat *self,
                             const xx_sci_member *member, uint8_t **out,
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
         !xx_sci_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_SCI_HEADER_SIZE 46
#define XX_SCI_NAME_SIZE 50
#define XX_SCI_RECORD_SIZE 55 /* 1 kind + 50 name + 4 size */
/* The chain carries no member count, so this only bounds a runaway walk. */
#define XX_SCI_MAX_MEMBERS 65536

static uint32_t xx_sci_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Only the bytes before the first NUL are the name: the rest of the 50-byte
 * field is uninitialised writer scratch and must never be inspected.
 * Backslashes become '/' and any leading separator is dropped; returns NULL
 * for a name this reader refuses. */
static char *xx_sci_name_dup(const uint8_t *field) {
    char *name;
    size_t raw = 0U;
    size_t length = 0U;
    size_t index;
    size_t start;
    size_t part;

    while (raw < (size_t)XX_SCI_NAME_SIZE && field[raw] != 0U) ++raw;
    /* An empty name field is the cheapest way to tell a real record from a
     * run of arbitrary bytes that happens to sit after a matching header. */
    if (raw == 0U) return NULL;

    name = (char *)xx_mem_alloc((size_t)XX_SCI_NAME_SIZE + 1U);
    if (!name) return NULL;
    for (index = 0U; index < raw; ++index) {
        const uint8_t byte = field[index];
        /* Control bytes and DEL are never part of a DOS name, and the
         * punctuation below is what DOS and Windows forbid outright. Bytes
         * above 0x7E are deliberately kept: the format stores OEM-codepage
         * names verbatim and the reference decodes them as Latin-1. */
        if (byte < 0x20U || byte == 0x7FU || byte == (uint8_t)'/' ||
            byte == (uint8_t)':' || byte == (uint8_t)'*' ||
            byte == (uint8_t)'?' || byte == (uint8_t)'"' ||
            byte == (uint8_t)'<' || byte == (uint8_t)'>' ||
            byte == (uint8_t)'|') {
            xx_str_free(name);
            return NULL;
        }
        name[length++] = (byte == (uint8_t)'\\') ? '/' : (char)byte;
    }
    name[length] = '\0';

    start = 0U;
    while (name[start] == '/') ++start;
    if (name[start] == '\0') {
        xx_str_free(name);
        return NULL;
    }
    if (start > 0U) {
        for (index = 0U; name[start + index] != '\0'; ++index) {
            name[index] = name[start + index];
        }
        name[index] = '\0';
    }

    /* A component that is empty, "." or ".." would climb out of the output
     * directory once joined, so the whole member is refused rather than
     * sanitised -- a sanitised name would silently extract somewhere else. */
    start = 0U;
    for (index = 0U;; ++index) {
        if (name[index] != '/' && name[index] != '\0') continue;
        part = index - start;
        if (part == 0U ||
            (part == 1U && name[start] == '.') ||
            (part == 2U && name[start] == '.' && name[start + 1U] == '.')) {
            xx_str_free(name);
            return NULL;
        }
        if (name[index] == '\0') break;
        start = index + 1U;
    }
    return name;
}

static xx_sci_stream *xx_sci_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_sci_stream *stream;
    uint8_t header[XX_SCI_HEADER_SIZE];
    uint8_t record[XX_SCI_RECORD_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A header with nothing behind it is not an archive: at least one whole
     * record has to fit. */
    if (span < (int64_t)XX_SCI_HEADER_SIZE + (int64_t)XX_SCI_RECORD_SIZE) {
        return NULL;
    }
    if (!xx_sci_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    /* These three fixed runs are the format's whole identity. The banner
     * between them differs between the 1.00 and 2.00 writers, so only the
     * version digit at +3 varies; loosening any of the three would let an
     * arbitrary file with "SCI" in it walk a 55-byte chain of garbage. */
    if (header[0] != 'S' || header[1] != 'C' || header[2] != 'I' ||
        (header[3] != '1' && header[3] != '2')) {
        return NULL;
    }
    if (header[4] != '0' || header[5] != '0' || header[6] != ' ' ||
        header[7] != '-') {
        return NULL;
    }
    if (header[0x28] != 'n' || header[0x29] != 'd' || header[0x2A] != '.' ||
        header[0x2B] != '\r' || header[0x2C] != '\n' || header[0x2D] != 0x1A) {
        return NULL;
    }

    stream = (xx_sci_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = (int64_t)XX_SCI_HEADER_SIZE;
    for (index = 0; index < (int64_t)XX_SCI_MAX_MEMBERS; ++index) {
        xx_sci_member member;
        char *name;
        int64_t size;
        int64_t data_offset;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (offset >= span) break;
        /* Trailing bytes too short to be a record: the archive was cut off,
         * so stop and keep what was already whole. */
        if (!xx_sci_range_within(span, offset, (int64_t)XX_SCI_RECORD_SIZE)) {
            break;
        }
        if (!xx_sci_read_at(self, self->base_address + offset, record,
                            sizeof(record))) {
            goto fail;
        }

        name = xx_sci_name_dup(record + 1);
        /* A bad name is a hard rejection, not an end of chain: the walk has
         * no other way to tell a real record from noise. */
        if (!name) goto fail;

        size = (int64_t)(int32_t)xx_sci_le32(record + 1 + XX_SCI_NAME_SIZE);
        if (size < 0) {
            xx_str_free(name);
            goto fail;
        }
        data_offset = offset + (int64_t)XX_SCI_RECORD_SIZE;
        if (!xx_sci_range_within(span, data_offset, size)) {
            /* Cut off by the end of the file: publish only intact members. */
            xx_str_free(name);
            break;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = (int64_t)XX_SCI_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = size;
        member.uncompressed_size = size;
        if (!xx_sci_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        offset = data_offset + size;
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = offset;
    return stream;

fail:
    xx_sci_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_sci_init(xx_sci *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_SCI;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sci-sixxac");
    xx_format_set_extension(&archive->format, "sxd");
    archive->format.check_is_valid = xx_sci_check_is_valid;
    archive->format.handle_base_info = xx_sci_handle_base_info;
    archive->format.get_format_size = xx_sci_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sci_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sci_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sci_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sci_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sci_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sci_free_archive_records_reading;
    archive->format.destroy = xx_sci_vtable_destroy;
}

xx_sci *xx_sci_create(xx_io_device *device, int64_t base_address) {
    xx_sci *archive = (xx_sci *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_sci_init(archive, device, base_address);
    return archive;
}

void xx_sci_destroy(xx_sci *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_sci_free(xx_sci *archive) {
    if (!archive) return;
    xx_sci_destroy(archive);
    xx_mem_free(archive);
}

static void xx_sci_vtable_destroy(Abstractformat *self) {
    xx_sci_destroy((xx_sci *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_sci_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_sci_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_sci_parse(self, pd);
    if (!stream) return false;
    xx_sci_stream_free(stream);
    return true;
}

bool xx_sci_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_sci *archive = (xx_sci *)self;
    xx_sci_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_sci_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_sci_stream_free(stream);
    return true;
}

int64_t xx_sci_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_sci_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_sci *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_sci_set_record(xx_archive_record *record,
                                 const xx_sci_member *member) {
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

static bool xx_sci_copy_options(xx_list_s *target,
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

static const xx_var *xx_sci_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_sci_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_sci_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_sci_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_sci_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_sci_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_sci_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_sci_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_sci_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sci_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_sci_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_sci_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_sci_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_sci_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_sci_stream *stream;
    const xx_sci_member *member;
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
    stream = (xx_sci_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_sci_path_safe(member->name)) return false;

    path_option = xx_sci_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_sci_decode(self, member, &plain, &plain_size, pd);
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
        !xx_sci_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_sci_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
