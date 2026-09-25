/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Micro Focus COBOL Library File (.lbr/.obr/.16/.32) archives.
 *
 * Fixed header, 0x100 bytes:
 *   +0x00  "Micro Focus COBOL Library File" then space padding
 *   +0x22  "MM/DD/YY  HH:MM:SS:hh" creation stamp, space padded, 21 bytes
 *   +0x90  u16be  block size, 0x0080 on everything seen
 *   +0x92  u16be  number of directory records
 *   +0x94  u32le  == 1
 *   +0x98  u32le  == 1
 *
 * Directory record, 18 bytes, the first at 0x100:
 *   +0x00  u32be  file offset of the NEXT record (may point backwards)
 *   +0x04  u32be  payload offset in 128-byte blocks
 *   +0x08  u32be  payload length in bytes
 *   +0x0c  u16be  DOS time
 *   +0x0e  u16be  DOS date
 *   +0x10  u16le  flags; only 0x0000 and 0x0001 occur
 *   +0x12  u8     name length, then that many name bytes (no terminator)
 *
 * Every numeric directory field is big-endian, as a COBOL runtime that began
 * life on IBM iron would have it; only the two header words at +0x94/+0x98
 * and the record flags are little-endian.
 *
 * Payloads are stored, never compressed: the payload begins at
 * (block << 7) and is copied out verbatim.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lbrcobol/xx_lbrcobol.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_LBRCOBOL_COPY_CHUNK (64 * 1024)

typedef struct xx_lbrcobol_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_lbrcobol_member;

typedef struct xx_lbrcobol_stream_s {
    xx_lbrcobol_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_lbrcobol_stream;

static void xx_lbrcobol_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_lbrcobol_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_lbrcobol_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_lbrcobol_path_safe(const char *name) {
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

static void xx_lbrcobol_stream_free(void *pointer) {
    xx_lbrcobol_stream *stream = (xx_lbrcobol_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_lbrcobol_add(xx_lbrcobol_stream *stream,
                          const xx_lbrcobol_member *member) {
    xx_lbrcobol_member *grown = (xx_lbrcobol_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_lbrcobol_decode(Abstractformat *self,
                             const xx_lbrcobol_member *member, uint8_t **out,
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
         !xx_lbrcobol_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_LBRCOBOL_HEADER_SIZE 0x100
#define XX_LBRCOBOL_RECORD_SIZE 18
#define XX_LBRCOBOL_COUNT_OFFSET 0x92
#define XX_LBRCOBOL_MAGIC1_OFFSET 0x94
#define XX_LBRCOBOL_MAGIC2_OFFSET 0x98
/* Payload offsets are counted in 128-byte blocks. */
#define XX_LBRCOBOL_BLOCK_SHIFT 7
/* The record count is a u16, so it can never exceed this; the cap is here so
 * that a later widening of the field cannot turn into an unbounded walk. */
#define XX_LBRCOBOL_MAX_MEMBERS 0x10000
#define XX_LBRCOBOL_MAX_NAME 255

static uint16_t xx_lbrcobol_be16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t xx_lbrcobol_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint32_t xx_lbrcobol_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Member names are plain COBOL module names plus '-', '.' and '_'. Anything
 * outside printable ASCII means the link field has walked off the directory
 * into payload bytes, which is the only way the chain can be seen to have
 * gone wrong: the links carry no ordering or termination invariant. */
static bool xx_lbrcobol_name_valid(const char *name, int32_t size) {
    int32_t index;

    for (index = 0; index < size; ++index) {
        uint8_t value = (uint8_t)name[index];
        if (value < 0x20U || value > 0x7eU) return false;
    }
    return true;
}

static xx_lbrcobol_stream *xx_lbrcobol_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    uint8_t header[XX_LBRCOBOL_HEADER_SIZE];
    xx_lbrcobol_stream *stream = NULL;
    char *name = NULL;
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t archive_end;
    int32_t count;
    int32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)(XX_LBRCOBOL_HEADER_SIZE + XX_LBRCOBOL_RECORD_SIZE + 2)) {
        return NULL;
    }
    if (!xx_lbrcobol_read_at(self, self->base_address, header,
                             sizeof(header))) {
        return NULL;
    }

    /* The 30-byte title plus the four spaces that follow it, and the two
     * little-endian ones at +0x94/+0x98. Together these are what separates a
     * library from any other file that happens to start with text; the two
     * constant words in particular are the cheap part of the signature and
     * are exactly what a later reader will be tempted to drop. */
    if (xx_rt_memcmp(header, "Micro Focus COBOL Library File", 30) != 0) {
        return NULL;
    }
    if (xx_rt_memcmp(header + 0x1e, "    ", 4) != 0) return NULL;
    if (xx_lbrcobol_le32(header + XX_LBRCOBOL_MAGIC1_OFFSET) != 1U) return NULL;
    if (xx_lbrcobol_le32(header + XX_LBRCOBOL_MAGIC2_OFFSET) != 1U) return NULL;

    count = (int32_t)xx_lbrcobol_be16(header + XX_LBRCOBOL_COUNT_OFFSET);
    /* An empty library is not a thing worth accepting: with no records the
     * header alone would match, and the directory could not be checked. */
    if (count <= 0 || count > XX_LBRCOBOL_MAX_MEMBERS) return NULL;

    stream = (xx_lbrcobol_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    archive_end = XX_LBRCOBOL_HEADER_SIZE;
    offset = XX_LBRCOBOL_HEADER_SIZE;

    for (index = 0; index < count; ++index) {
        uint8_t record[XX_LBRCOBOL_RECORD_SIZE + 1];
        xx_lbrcobol_member member;
        int64_t next;
        int64_t data_offset;
        int64_t data_size;
        int64_t name_offset;
        int32_t name_size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* The link field is free to point backwards, so there is no ordering
         * invariant to lean on: the header record count is the only bound and
         * every hop has to be re-validated against the span. */
        if (!xx_lbrcobol_range_within(span, offset,
                                      XX_LBRCOBOL_RECORD_SIZE + 1)) {
            goto fail;
        }
        if (!xx_lbrcobol_read_at(self, self->base_address + offset, record,
                                 sizeof(record))) {
            goto fail;
        }

        next = (int64_t)xx_lbrcobol_be32(record + 0);
        data_offset = (int64_t)xx_lbrcobol_be32(record + 4)
                      << XX_LBRCOBOL_BLOCK_SHIFT;
        data_size = (int64_t)xx_lbrcobol_be32(record + 8);

        if (!xx_lbrcobol_range_within(span, data_offset, data_size)) goto fail;
        /* A payload can never overlap the fixed header. This is the cheapest
         * structural test that keeps a random "Micro Focus"-prefixed blob
         * from parsing as a directory, because a bogus block number almost
         * always lands inside the first 128-byte block. */
        if (data_size > 0 && data_offset < XX_LBRCOBOL_HEADER_SIZE) goto fail;

        /* Pascal name: the length byte is the sole authority. The buffer is
         * not NUL-terminated and is space padded out to the next record. */
        name_size = (int32_t)record[XX_LBRCOBOL_RECORD_SIZE];
        if (name_size < 1 || name_size > XX_LBRCOBOL_MAX_NAME) goto fail;
        name_offset = offset + XX_LBRCOBOL_RECORD_SIZE + 1;
        if (!xx_lbrcobol_range_within(span, name_offset, name_size)) goto fail;

        name = (char *)xx_mem_alloc((size_t)name_size + 1U);
        if (!name) goto fail;
        if (!xx_lbrcobol_read_at(self, self->base_address + name_offset,
                                 (uint8_t *)name, (size_t)name_size)) {
            goto fail;
        }
        name[name_size] = '\0';
        if (!xx_lbrcobol_name_valid(name, name_size)) goto fail;
        {
            int32_t position;
            for (position = 0; position < name_size; ++position) {
                if (name[position] == '\\') name[position] = '/';
            }
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_LBRCOBOL_RECORD_SIZE + 1 + name_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        member.uncompressed_size = data_size;
        member.method = 0U;
        /* DOS date/time packed the usual way. The record stores the time
         * first and the date second, the reverse of the obvious order;
         * swapping them silently yields nonsense stamps rather than an
         * error. The flags word at +0x10 carries nothing this reader
         * publishes, so it is not read at all. */
        member.timestamp = ((uint64_t)xx_lbrcobol_be16(record + 14) << 16) |
                           (uint64_t)xx_lbrcobol_be16(record + 12);
        member.is_folder = false;

        if (!xx_lbrcobol_add(stream, &member)) goto fail;
        name = NULL; /* owned by the stream now */

        if (offset + member.header_size > archive_end) {
            archive_end = offset + member.header_size;
        }
        if (data_offset + data_size > archive_end) {
            archive_end = data_offset + data_size;
        }

        offset = next;
    }

    /* The final 128-byte block is padded, so a few slack bytes after the last
     * member are normal and are left as overlay rather than rejected. */
    stream->archive_size = archive_end < span ? archive_end : span;
    return stream;

fail:
    xx_str_free(name);
    xx_lbrcobol_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_lbrcobol_init(xx_lbrcobol *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_LBRCOBOL;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mf-cobol-library");
    xx_format_set_extension(&archive->format, "lbr");
    archive->format.check_is_valid = xx_lbrcobol_check_is_valid;
    archive->format.handle_base_info = xx_lbrcobol_handle_base_info;
    archive->format.get_format_size = xx_lbrcobol_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lbrcobol_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lbrcobol_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lbrcobol_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lbrcobol_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lbrcobol_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lbrcobol_free_archive_records_reading;
    archive->format.destroy = xx_lbrcobol_vtable_destroy;
}

xx_lbrcobol *xx_lbrcobol_create(xx_io_device *device, int64_t base_address) {
    xx_lbrcobol *archive = (xx_lbrcobol *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_lbrcobol_init(archive, device, base_address);
    return archive;
}

void xx_lbrcobol_destroy(xx_lbrcobol *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_lbrcobol_free(xx_lbrcobol *archive) {
    if (!archive) return;
    xx_lbrcobol_destroy(archive);
    xx_mem_free(archive);
}

static void xx_lbrcobol_vtable_destroy(Abstractformat *self) {
    xx_lbrcobol_destroy((xx_lbrcobol *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_lbrcobol_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_lbrcobol_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_lbrcobol_parse(self, pd);
    if (!stream) return false;
    xx_lbrcobol_stream_free(stream);
    return true;
}

bool xx_lbrcobol_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lbrcobol *archive = (xx_lbrcobol *)self;
    xx_lbrcobol_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_lbrcobol_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_lbrcobol_stream_free(stream);
    return true;
}

int64_t xx_lbrcobol_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_lbrcobol_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_lbrcobol *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_lbrcobol_set_record(xx_archive_record *record,
                                 const xx_lbrcobol_member *member) {
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

static bool xx_lbrcobol_copy_options(xx_list_s *target,
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

static const xx_var *xx_lbrcobol_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_lbrcobol_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_lbrcobol_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_lbrcobol_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_lbrcobol_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_lbrcobol_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_lbrcobol_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_lbrcobol_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_lbrcobol_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_lbrcobol_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_lbrcobol_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_lbrcobol_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_lbrcobol_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lbrcobol_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_lbrcobol_stream *stream;
    const xx_lbrcobol_member *member;
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
    stream = (xx_lbrcobol_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_lbrcobol_path_safe(member->name)) return false;

    path_option = xx_lbrcobol_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_lbrcobol_decode(self, member, &plain, &plain_size, pd);
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
        !xx_lbrcobol_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_lbrcobol_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
