/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * KolibriOS kpack ("KPCK") containers.
 *
 *   header, 12 bytes at offset 0, little-endian:
 *     0x00  4 bytes  magic, "KPCK"
 *     0x04  u32 LE   unpacked size
 *     0x08  u32 LE   method flags
 *                      0x01  LZMA, always set
 *                      0x40  call-trick filter 1
 *                      0x80  call-trick filter 2
 *                    No other bit may be set, and the two call-trick bits
 *                    are mutually exclusive - kpack's own unpacker accepts
 *                    nothing else.
 *
 *   payload: from offset 12 to end of file. LZMA1 with hard-wired
 *     lc=3 lp=0 pb=2 and NO 13-byte LZMA header; the range coder is
 *     initialised from the first four payload bytes read little-endian.
 *
 *   trailer: when a call-trick bit is set, the last five bytes of the
 *     container are the filter's parameters.
 *
 * That trailer is why the member's data extent is the WHOLE container from
 * the magic, not the payload alone: xx_kolibrikpack_decode_memory() is
 * documented to take the container from byte zero and to read the trailer
 * from the very end of the buffer it is given. Handing it only the bytes
 * after the header would silently decode a call-tricked stream without its
 * filter.
 *
 * The container stores no name; kpack relies on the file name and strips a
 * ".kpack" or ".kpck" suffix from it. That name is not available here, so
 * the single member is listed as "kpack_data", matching the reference
 * fallback.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/kolibrikpack/xx_kolibrikpack.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/kolibrikpack/xx_kolibrikpack.h"

#include <stdio.h>

#define XX_KOLIBRIKPACK_COPY_CHUNK (64 * 1024)

typedef struct xx_kolibrikpack_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_kolibrikpack_member;

typedef struct xx_kolibrikpack_stream_s {
    xx_kolibrikpack_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_kolibrikpack_stream;

static void xx_kolibrikpack_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_kolibrikpack_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_kolibrikpack_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_kolibrikpack_path_safe(const char *name) {
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

static void xx_kolibrikpack_stream_free(void *pointer) {
    xx_kolibrikpack_stream *stream = (xx_kolibrikpack_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_kolibrikpack_add(xx_kolibrikpack_stream *stream,
                          const xx_kolibrikpack_member *member) {
    xx_kolibrikpack_member *grown = (xx_kolibrikpack_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_KOLIBRIKPACK_MAX_MEMBERS 1
#define XX_KOLIBRIKPACK_HEADER_SIZE 12
#define XX_KOLIBRIKPACK_MIN_SIZE (XX_KOLIBRIKPACK_HEADER_SIZE + 4)
#define XX_KOLIBRIKPACK_TRAILER_SIZE 5
#define XX_KOLIBRIKPACK_FLAG_LZMA 0x01U
#define XX_KOLIBRIKPACK_FLAG_CALLTRICK1 0x40U
#define XX_KOLIBRIKPACK_FLAG_CALLTRICK2 0x80U
#define XX_KOLIBRIKPACK_METHOD_MASK                                          \
    (XX_KOLIBRIKPACK_FLAG_LZMA | XX_KOLIBRIKPACK_FLAG_CALLTRICK1 |           \
     XX_KOLIBRIKPACK_FLAG_CALLTRICK2)
#define XX_KOLIBRIKPACK_MAX_COMPRESSED ((int64_t)0x10000000)
#define XX_KOLIBRIKPACK_MAX_DECODED ((int64_t)0x20000000)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_kolibrikpack_le32(const uint8_t *data);
static xx_kolibrikpack_stream *xx_kolibrikpack_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_kolibrikpack_decode(Abstractformat *self, const xx_kolibrikpack_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The container holds exactly one stream; the cap keeps the shared shape of
 * these readers. */

static uint32_t xx_kolibrikpack_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_kolibrikpack_stream *xx_kolibrikpack_parse(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    static const uint8_t magic[4] = {(uint8_t)'K', (uint8_t)'P', (uint8_t)'C',
                                     (uint8_t)'K'};
    xx_kolibrikpack_stream *stream = NULL;
    xx_kolibrikpack_member member;
    uint8_t header[XX_KOLIBRIKPACK_HEADER_SIZE];
    char *name = NULL;
    int64_t total;
    int64_t span;
    int64_t minimum;
    uint32_t unpacked_size;
    uint32_t flags;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_KOLIBRIKPACK_MIN_SIZE) return NULL;
    if (span > XX_KOLIBRIKPACK_MAX_COMPRESSED) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_kolibrikpack_read_at(self, self->base_address, header,
                                 sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;

    unpacked_size = xx_kolibrikpack_le32(header + 4);
    flags = xx_kolibrikpack_le32(header + 8);

    /* Four printable magic bytes are weak on their own; the flags dword is
     * what carries the discrimination. kpack's unpacker accepts LZMA plus at
     * most one call-trick filter and nothing else, so the upper 24 bits and
     * bits 1..5 must all be clear. Loosening this to "bit 0 set" would make
     * any file starting with "KPCK" match. */
    if ((flags & ~(XX_KOLIBRIKPACK_FLAG_CALLTRICK1 |
                   XX_KOLIBRIKPACK_FLAG_CALLTRICK2)) !=
        XX_KOLIBRIKPACK_FLAG_LZMA) {
        return NULL;
    }
    /* The two call-trick filters are alternatives, never both. */
    if ((flags & (XX_KOLIBRIKPACK_FLAG_CALLTRICK1 |
                  XX_KOLIBRIKPACK_FLAG_CALLTRICK2)) ==
        (XX_KOLIBRIKPACK_FLAG_CALLTRICK1 | XX_KOLIBRIKPACK_FLAG_CALLTRICK2)) {
        return NULL;
    }
    if (unpacked_size == 0U ||
        (int64_t)unpacked_size > XX_KOLIBRIKPACK_MAX_DECODED) {
        return NULL;
    }

    /* A filtered container must additionally have room for the five-byte
     * trailer the filter reads from its end. */
    minimum = XX_KOLIBRIKPACK_MIN_SIZE;
    if (flags & (XX_KOLIBRIKPACK_FLAG_CALLTRICK1 |
                 XX_KOLIBRIKPACK_FLAG_CALLTRICK2)) {
        minimum += XX_KOLIBRIKPACK_TRAILER_SIZE;
    }
    if (span < minimum) return NULL;

    stream = (xx_kolibrikpack_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* The container stores no name. kpack derives one from the file name by
     * stripping ".kpack"/".kpck"; that is unavailable here, so the reference
     * fallback is used unconditionally. */
    name = xx_str_dup("kpack_data");
    if (!name) goto fail;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_KOLIBRIKPACK_HEADER_SIZE;
    /* Deliberately the WHOLE container, magic included: the decoder reads
     * the call-trick trailer from the end of the buffer it is handed, so the
     * member's data extent has to be everything from the magic to EOF. */
    member.data_offset = self->base_address;
    member.compressed_size = span;
    member.uncompressed_size = (int64_t)unpacked_size;
    /* The flags dword, unchanged. */
    member.method = flags;
    /* No timestamp anywhere in the container. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_kolibrikpack_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    stream->archive_size = span;
    return stream;

fail:
    xx_kolibrikpack_stream_free(stream);
    return NULL;
}


/* The range coder alone needs four payload bytes, so this is the shortest
 * container that could possibly decode. */


/* The container's own flags dword is published unchanged as the method, so a
 * listing shows exactly what the archive says. Bit 0 is always set. */

/* The reference decoder's own ceilings. */

/* One headerless LZMA1 stream per container. The whole container, magic
 * included, is handed to the decoder: the call-trick filter reads its
 * five-byte parameter block from the END of that buffer, so trimming the
 * header off the front would shift nothing but would make the reader's
 * contract differ from the codec's. */
static bool xx_kolibrikpack_decode(Abstractformat *self,
                                   const xx_kolibrikpack_member *member,
                                   uint8_t **out, size_t *out_size,
                                   xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    size_t declared = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* A flags dword this reader does not implement must fail here rather
     * than be treated as plain LZMA. Two separate conditions: no bit outside
     * the known set, and the LZMA bit actually present.
     *
     * These were previously one test, comparing the UNKNOWN bits against
     * FLAG_LZMA. The unknown bits are zero for every valid method word, so
     * that compared 0 against 1 and failed every time -- this decoder could
     * never succeed on any input. */
    if ((member->method & ~XX_KOLIBRIKPACK_METHOD_MASK) != 0U ||
        (member->method & XX_KOLIBRIKPACK_FLAG_LZMA) !=
            XX_KOLIBRIKPACK_FLAG_LZMA ||
        (member->method & (XX_KOLIBRIKPACK_FLAG_CALLTRICK1 |
                           XX_KOLIBRIKPACK_FLAG_CALLTRICK2)) ==
            (XX_KOLIBRIKPACK_FLAG_CALLTRICK1 |
             XX_KOLIBRIKPACK_FLAG_CALLTRICK2)) {
        return false;
    }
    if (member->compressed_size < XX_KOLIBRIKPACK_MIN_SIZE ||
        member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_KOLIBRIKPACK_MAX_COMPRESSED ||
        member->uncompressed_size > XX_KOLIBRIKPACK_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    /* data_offset is the magic, not the payload. */
    if (!xx_kolibrikpack_read_at(self, member->data_offset, input,
                                 (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    /* Re-check the header against the bytes actually read: parse validated a
     * snapshot, and this keeps the decode self-contained if the device
     * changed underneath. */
    if (!xx_kolibrikpack_check_header(input, (size_t)member->compressed_size,
                                      &declared) ||
        declared != (size_t)member->uncompressed_size) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (!xx_kolibrikpack_decode_memory(input,
                                       (size_t)member->compressed_size,
                                       output,
                                       (size_t)member->uncompressed_size,
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

void xx_kolibrikpack_init(xx_kolibrikpack *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_KOLIBRIKPACK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-kolibri-kpack");
    xx_format_set_extension(&archive->format, "kpack");
    archive->format.check_is_valid = xx_kolibrikpack_check_is_valid;
    archive->format.handle_base_info = xx_kolibrikpack_handle_base_info;
    archive->format.get_format_size = xx_kolibrikpack_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_kolibrikpack_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_kolibrikpack_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_kolibrikpack_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_kolibrikpack_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_kolibrikpack_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_kolibrikpack_free_archive_records_reading;
    archive->format.destroy = xx_kolibrikpack_vtable_destroy;
}

xx_kolibrikpack *xx_kolibrikpack_create(xx_io_device *device, int64_t base_address) {
    xx_kolibrikpack *archive = (xx_kolibrikpack *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_kolibrikpack_init(archive, device, base_address);
    return archive;
}

void xx_kolibrikpack_destroy(xx_kolibrikpack *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_kolibrikpack_free(xx_kolibrikpack *archive) {
    if (!archive) return;
    xx_kolibrikpack_destroy(archive);
    xx_mem_free(archive);
}

static void xx_kolibrikpack_vtable_destroy(Abstractformat *self) {
    xx_kolibrikpack_destroy((xx_kolibrikpack *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_kolibrikpack_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_kolibrikpack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_kolibrikpack_parse(self, pd);
    if (!stream) return false;
    xx_kolibrikpack_stream_free(stream);
    return true;
}

bool xx_kolibrikpack_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_kolibrikpack *archive = (xx_kolibrikpack *)self;
    xx_kolibrikpack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_kolibrikpack_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_kolibrikpack_stream_free(stream);
    return true;
}

int64_t xx_kolibrikpack_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_kolibrikpack_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_kolibrikpack *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_kolibrikpack_set_record(xx_archive_record *record,
                                 const xx_kolibrikpack_member *member) {
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

static bool xx_kolibrikpack_copy_options(xx_list_s *target,
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

static const xx_var *xx_kolibrikpack_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_kolibrikpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_kolibrikpack_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_kolibrikpack_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_kolibrikpack_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_kolibrikpack_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_kolibrikpack_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_kolibrikpack_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_kolibrikpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_kolibrikpack_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_kolibrikpack_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_kolibrikpack_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_kolibrikpack_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_kolibrikpack_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_kolibrikpack_stream *stream;
    const xx_kolibrikpack_member *member;
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
    stream = (xx_kolibrikpack_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_kolibrikpack_path_safe(member->name)) return false;

    path_option = xx_kolibrikpack_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_kolibrikpack_decode(self, member, &plain, &plain_size, pd);
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
        !xx_kolibrikpack_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_kolibrikpack_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
