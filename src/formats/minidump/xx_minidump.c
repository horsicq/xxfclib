/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Windows MiniDump (.dmp) crash dumps.
 *
 *   MINIDUMP_HEADER at offset 0, 32 bytes:
 *     0x00  u32 LE Signature, 'MDMP' stored as 0x504D444D
 *     0x04  u32 LE Version; only the low word is the format version and it
 *           is 0xA793.  The high word is a producer-defined build number
 *           and carries no meaning for the reader.
 *     0x08  u32 LE NumberOfStreams, the directory entry count
 *     0x0c  u32 LE StreamDirectoryRva, file offset of the directory
 *     0x10  u32 LE CheckSum, usually zero and never verified
 *     0x14  u32 LE TimeDateStamp, unix time of the dump
 *     0x18  u64 LE Flags, MINIDUMP_TYPE bits
 *
 *   directory at StreamDirectoryRva, one 12 byte MINIDUMP_DIRECTORY per
 *   stream:
 *     0x00  u32 LE StreamType
 *     0x04  u32 LE DataSize
 *     0x08  u32 LE LocationRva
 *
 * Note the ordering: size precedes offset, as in a MINIDUMP_LOCATION_DESCRIPTOR.
 *
 * A dump's members are its directory entries.  Every stream is stored
 * verbatim - a minidump has no compression method field anywhere - so each
 * entry maps one to one onto a stored member whose name is its stream type.
 * Stream payloads have internal structure (thread lists, module lists,
 * memory ranges), but that is content, not container: nothing below the
 * directory changes where a member begins or ends.
 *
 * "RVA" here is a misnomer inherited from the Windows API: the values are
 * plain file offsets from the start of the dump, not virtual addresses.
 *
 * The directory need not be the last thing before the streams, and streams
 * may appear in any order, so the dump ends at the furthest stream end.
 *
 * Four magic bytes are cheap to hit by accident; the 0xA793 version word is
 * what makes a stray 'MDMP' fail, with the directory containment check
 * behind it.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/minidump/xx_minidump.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_MINIDUMP_COPY_CHUNK (64 * 1024)

typedef struct xx_minidump_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_minidump_member;

typedef struct xx_minidump_stream_s {
    xx_minidump_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_minidump_stream;

static void xx_minidump_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_minidump_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_minidump_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_minidump_path_safe(const char *name) {
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

static void xx_minidump_stream_free(void *pointer) {
    xx_minidump_stream *stream = (xx_minidump_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_minidump_add(xx_minidump_stream *stream,
                          const xx_minidump_member *member) {
    xx_minidump_member *grown = (xx_minidump_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_minidump_decode(Abstractformat *self,
                             const xx_minidump_member *member, uint8_t **out,
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
         !xx_minidump_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_MINIDUMP_HEADER_SIZE 32
#define XX_MINIDUMP_ENTRY_SIZE 12
/* The reference reader refuses anything at or above this, and a real dump
 * carries a few dozen streams; the bound is loose on purpose. */
#define XX_MINIDUMP_MAX_MEMBERS 10000
#define XX_MINIDUMP_NAME_SIZE 48

typedef struct {
    uint32_t type;
    const char *name;
} xx_minidump_stream_name;

/* MINIDUMP_STREAM_TYPE.  Names are the SDK spellings so a member name is
 * recognisable; unlisted types fall back to "Stream_<n>". */
static const xx_minidump_stream_name xx_minidump_stream_names[] = {
    {0U, "UnusedStream"},
    {3U, "ThreadListStream"},
    {4U, "ModuleListStream"},
    {5U, "MemoryListStream"},
    {6U, "ExceptionStream"},
    {7U, "SystemInfoStream"},
    {8U, "ThreadExListStream"},
    {9U, "Memory64ListStream"},
    {10U, "CommentStreamA"},
    {11U, "CommentStreamW"},
    {12U, "HandleDataStream"},
    {13U, "FunctionTableStream"},
    {14U, "UnloadedModuleListStream"},
    {15U, "MiscInfoStream"},
    {16U, "MemoryInfoListStream"},
    {17U, "ThreadInfoListStream"},
    {18U, "HandleOperationListStream"},
    {19U, "TokenStream"},
    {20U, "JavaScriptDataStream"},
    {21U, "SystemMemoryInfoStream"},
    {22U, "ProcessVmCountersStream"},
    {23U, "IptTraceStream"},
    {24U, "ThreadNamesStream"}
};

static uint32_t xx_minidump_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static const char *xx_minidump_type_name(uint32_t type) {
    size_t index;
    size_t total = sizeof(xx_minidump_stream_names) /
                   sizeof(xx_minidump_stream_names[0]);

    for (index = 0U; index < total; ++index) {
        if (xx_minidump_stream_names[index].type == type) {
            return xx_minidump_stream_names[index].name;
        }
    }
    return NULL;
}

static xx_minidump_stream *xx_minidump_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_minidump_stream *stream;
    uint8_t header[XX_MINIDUMP_HEADER_SIZE];
    uint8_t entry[XX_MINIDUMP_ENTRY_SIZE];
    int64_t total;
    int64_t span;
    int64_t count;
    int64_t index;
    int64_t directory_offset;
    int64_t archive_end;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_MINIDUMP_HEADER_SIZE) return NULL;
    if (!xx_minidump_read_at(self, self->base_address, header,
                             sizeof(header))) {
        return NULL;
    }

    /* 'MDMP', little-endian 0x504D444D. */
    if (xx_minidump_le32(header + 0x00) != 0x504D444DU) return NULL;
    /* Only the low word is the version; the high word is a build number that
     * varies between producers.  Four ASCII magic bytes are easy to hit by
     * accident inside arbitrary data, so this constant is the check that
     * actually keeps a stray 'MDMP' from being claimed as a dump - do not
     * loosen it to a bare magic test. */
    if ((xx_minidump_le32(header + 0x04) & 0xFFFFU) != 0xA793U) return NULL;

    count = (int64_t)xx_minidump_le32(header + 0x08);
    if (count <= 0 || count >= XX_MINIDUMP_MAX_MEMBERS) return NULL;

    /* A directory at offset 0 would overlap the header it was named by. */
    directory_offset = (int64_t)xx_minidump_le32(header + 0x0c);
    if (directory_offset < XX_MINIDUMP_HEADER_SIZE ||
        directory_offset >= span) {
        return NULL;
    }
    /* Second defence, behind the version word: the declared directory must
     * fit in the file before a single entry is trusted.  Written as a
     * division so count * 12 cannot overflow. */
    if (count > (span - directory_offset) / XX_MINIDUMP_ENTRY_SIZE) {
        return NULL;
    }

    stream = (xx_minidump_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    archive_end = directory_offset + (count * XX_MINIDUMP_ENTRY_SIZE);

    for (index = 0; index < count; ++index) {
        xx_minidump_member member;
        char name_buffer[XX_MINIDUMP_NAME_SIZE];
        const char *type_name;
        char *name;
        int64_t entry_offset;
        int64_t data_offset;
        int64_t data_size;
        uint32_t stream_type;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        entry_offset = directory_offset + (index * XX_MINIDUMP_ENTRY_SIZE);
        if (!xx_minidump_range_within(span, entry_offset,
                                      XX_MINIDUMP_ENTRY_SIZE) ||
            !xx_minidump_read_at(self, self->base_address + entry_offset,
                                 entry, sizeof(entry))) {
            goto fail;
        }

        stream_type = xx_minidump_le32(entry + 0x00);
        /* Size first, offset second, as in MINIDUMP_LOCATION_DESCRIPTOR. */
        data_size = (int64_t)xx_minidump_le32(entry + 0x04);
        data_offset = (int64_t)xx_minidump_le32(entry + 0x08);
        /* A stream running past EOF is a rejection, not a short read.  An
         * all-zero entry (UnusedStream, size 0, rva 0) passes this and is
         * kept: writers pad the directory with them deliberately. */
        if (!xx_minidump_range_within(span, data_offset, data_size)) goto fail;
        /* A non-empty stream may not start inside the 32 byte header. */
        if (data_size > 0 && data_offset < XX_MINIDUMP_HEADER_SIZE) goto fail;

        /* Names are synthesised, never read from the file, so they are ASCII
         * and path-safe by construction.  The index prefix keeps them unique:
         * dumps legitimately repeat a stream type, and UnusedStream padding
         * repeats it many times over. */
        type_name = xx_minidump_type_name(stream_type);
        if (type_name) {
            (void)xx_rt_snprintf(name_buffer, sizeof(name_buffer), "%04u_%s",
                                 (unsigned)index, type_name);
        } else {
            (void)xx_rt_snprintf(name_buffer, sizeof(name_buffer),
                                 "%04u_Stream_%u", (unsigned)index,
                                 (unsigned)stream_type);
        }
        name = xx_str_dup(name_buffer);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + entry_offset;
        member.header_size = XX_MINIDUMP_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        member.uncompressed_size = data_size;
        if (!xx_minidump_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        /* Streams are not required to follow the directory, nor to be in
         * directory order, so the dump ends at the furthest stream end. */
        if (data_offset + data_size > archive_end) {
            archive_end = data_offset + data_size;
        }
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = (archive_end < span) ? archive_end : span;
    return stream;

fail:
    xx_minidump_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_minidump_init(xx_minidump *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_MINIDUMP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-dmp");
    xx_format_set_extension(&archive->format, "dmp");
    archive->format.check_is_valid = xx_minidump_check_is_valid;
    archive->format.handle_base_info = xx_minidump_handle_base_info;
    archive->format.get_format_size = xx_minidump_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_minidump_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_minidump_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_minidump_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_minidump_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_minidump_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_minidump_free_archive_records_reading;
    archive->format.destroy = xx_minidump_vtable_destroy;
}

xx_minidump *xx_minidump_create(xx_io_device *device, int64_t base_address) {
    xx_minidump *archive = (xx_minidump *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_minidump_init(archive, device, base_address);
    return archive;
}

void xx_minidump_destroy(xx_minidump *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_minidump_free(xx_minidump *archive) {
    if (!archive) return;
    xx_minidump_destroy(archive);
    xx_mem_free(archive);
}

static void xx_minidump_vtable_destroy(Abstractformat *self) {
    xx_minidump_destroy((xx_minidump *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_minidump_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_minidump_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_minidump_parse(self, pd);
    if (!stream) return false;
    xx_minidump_stream_free(stream);
    return true;
}

bool xx_minidump_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_minidump *archive = (xx_minidump *)self;
    xx_minidump_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_minidump_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_minidump_stream_free(stream);
    return true;
}

int64_t xx_minidump_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_minidump_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_minidump *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_minidump_set_record(xx_archive_record *record,
                                 const xx_minidump_member *member) {
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

static bool xx_minidump_copy_options(xx_list_s *target,
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

static const xx_var *xx_minidump_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_minidump_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_minidump_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_minidump_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_minidump_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_minidump_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_minidump_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_minidump_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_minidump_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_minidump_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_minidump_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_minidump_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_minidump_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_minidump_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_minidump_stream *stream;
    const xx_minidump_member *member;
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
    stream = (xx_minidump_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_minidump_path_safe(member->name)) return false;

    path_option = xx_minidump_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_minidump_decode(self, member, &plain, &plain_size, pd);
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
        !xx_minidump_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_minidump_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
