/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MRNZ files, the wrapper the PC DOS installation disks put around their
 * compressed files. This wraps ONE payload, not a multi-member archive, so the
 * record list this reader publishes always holds exactly one entry.
 *
 *   header, 12 bytes at offset 0:
 *     0x00   4  char     magic "MRNZ"
 *     0x04   4  bytes    88 f0 27 33, the same check word SZDD carries
 *     0x08   4  u32 LE   obfuscated byte count, 0 .. 255
 *     0x0c   n  payload  running to end-of-file
 *
 * The payload is NOT compressed by this layer. The first "obfuscated byte
 * count" bytes of it are masked with XOR 0xae and everything behind them is
 * stored verbatim, so the plaintext is exactly as long as the payload. The
 * mask hides the payload's own signature, which is the whole point of the
 * wrapper: across the reference corpus the recovered payload is a KWAJ stream
 * ("KWAJ" 88 f0 27 d1), an MZ executable, or a raw .COM image. Decoding that
 * inner format is that format's business, not this reader's.
 *
 * Layout and the 0xae mask are DOCUMENTED: this is the same handling as
 * deark's "mrnz" module, and the corpus agrees with it - every sample carries
 * a count of 38 and recovers a recognisable payload signature. The size
 * ceilings and the plausibility gate below are this reader's own policy.
 *
 * There is no name, no timestamp, no length field and no checksum anywhere in
 * the container, so the eight magic bytes plus the bounded count are all the
 * validation the format makes possible.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mrnz/xx_mrnz.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The alias macro is defined next to the enumerator in xxfc_defs.h, so testing
 * for it picks up the real file type as soon as MRNZ is registered there.
 * Until then the reader identifies itself as unknown rather than borrowing
 * another format's id. See the port report for the registration this needs. */
#ifdef MRNZ
#define XX_MRNZ_FILE_TYPE XX_FILE_TYPE_MRNZ
#else
#define XX_MRNZ_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_MRNZ_HEADER_SIZE 12
#define XX_MRNZ_MIN_FILE_SIZE 13
#define XX_MRNZ_MAX_MEMBERS 1
#define XX_MRNZ_METHOD_XOR 1U
#define XX_MRNZ_MASK 0xaeU
/* The count is a 32-bit field holding a value the format never lets exceed a
 * byte; anything larger is a different file that happens to start with the
 * magic. */
#define XX_MRNZ_MAX_OBFUSCATED 255
#define XX_MRNZ_MAX_DECODED ((int64_t)256 * 1024 * 1024)
/* The original file name is stored nowhere - the installer keeps it in its own
 * script - so the single record gets a fixed, deliberately extension-less
 * placeholder. */
#define XX_MRNZ_PLACEHOLDER_NAME "mrnz_data"

typedef struct xx_mrnz_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    int64_t obfuscated_size;
    uint32_t method;
    bool is_folder;
} xx_mrnz_member;

typedef struct xx_mrnz_stream_s {
    xx_mrnz_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_mrnz_stream;

static void xx_mrnz_vtable_destroy(Abstractformat *self);

static const uint8_t xx_mrnz_magic[8] = {'M',  'R',  'N',  'Z',
                                         0x88U, 0xf0U, 0x27U, 0x33U};

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_mrnz_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_mrnz_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_mrnz_range_within(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_mrnz_path_safe(const char *name) {
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

static void xx_mrnz_stream_free(void *pointer) {
    xx_mrnz_stream *stream = (xx_mrnz_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of member->name. */
static bool xx_mrnz_add(xx_mrnz_stream *stream, const xx_mrnz_member *member) {
    xx_mrnz_member *grown = (xx_mrnz_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* --------------------------------------------------------------- parse -- */

static xx_mrnz_stream *xx_mrnz_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_mrnz_stream *stream;
    xx_mrnz_member member;
    uint8_t header[XX_MRNZ_HEADER_SIZE];
    char *name;
    int64_t total;
    int64_t span;
    int64_t payload_size;
    int64_t obfuscated_size;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_MRNZ_MIN_FILE_SIZE) return NULL;
    if (!xx_mrnz_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, xx_mrnz_magic, sizeof(xx_mrnz_magic)) != 0) {
        return NULL;
    }

    /* The container stores no payload length, so end-of-file is the only
     * boundary there is, and the payload is also the plaintext: this layer
     * masks bytes, it never compresses them. */
    payload_size = span - XX_MRNZ_HEADER_SIZE;
    if (payload_size < 1) return NULL;
    if (payload_size > XX_MRNZ_MAX_DECODED) return NULL;

    {
        uint32_t declared = xx_mrnz_le32(header + 8);
        /* Bounded before it is used for anything: the mask loop runs over
         * exactly this many bytes of a buffer sized from the file. */
        if (declared > (uint32_t)XX_MRNZ_MAX_OBFUSCATED) return NULL;
        obfuscated_size = (int64_t)declared;
    }
    /* A count reaching past the end of the payload describes a file that is
     * not what the header says it is. */
    if (obfuscated_size > payload_size) return NULL;
    if (!xx_mrnz_range_within(span, (int64_t)XX_MRNZ_HEADER_SIZE,
                              payload_size)) {
        return NULL;
    }

    stream = (xx_mrnz_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = xx_str_dup(XX_MRNZ_PLACEHOLDER_NAME);
    if (!name) goto fail;
    if (!xx_mrnz_path_safe(name)) {
        xx_str_free(name);
        goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_MRNZ_HEADER_SIZE;
    member.data_offset = self->base_address + XX_MRNZ_HEADER_SIZE;
    member.compressed_size = payload_size;
    /* Masking does not change the length: plaintext and payload are the same
     * size, always. */
    member.uncompressed_size = payload_size;
    member.obfuscated_size = obfuscated_size;
    member.method = XX_MRNZ_METHOD_XOR;
    /* The wrapper has no directory entries and never will: it holds one
     * file. */
    member.is_folder = false;

    if (!xx_mrnz_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    if (stream->count != (size_t)XX_MRNZ_MAX_MEMBERS) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_mrnz_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

/* Lift the mask off the front of the payload and pass the rest through. */
static bool xx_mrnz_decode(Abstractformat *self, const xx_mrnz_member *member,
                           uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *output;
    size_t index;
    size_t masked;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_MRNZ_METHOD_XOR) return false;
    if (member->compressed_size < 1 ||
        member->uncompressed_size != member->compressed_size) {
        return false;
    }
    if (member->compressed_size > XX_MRNZ_MAX_DECODED) return false;
    if (member->obfuscated_size < 0 ||
        member->obfuscated_size > XX_MRNZ_MAX_OBFUSCATED ||
        member->obfuscated_size > member->compressed_size) {
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) return false;
    if (!xx_mrnz_read_at(self, member->data_offset, output,
                         (size_t)member->uncompressed_size)) {
        xx_mem_free(output);
        return false;
    }
    masked = (size_t)member->obfuscated_size;
    for (index = 0U; index < masked; ++index) {
        output[index] = (uint8_t)(output[index] ^ (uint8_t)XX_MRNZ_MASK);
    }
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_mrnz_init(xx_mrnz *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MRNZ_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mrnz");
    xx_format_set_extension(&archive->format, "mrnz");
    archive->format.check_is_valid = xx_mrnz_check_is_valid;
    archive->format.handle_base_info = xx_mrnz_handle_base_info;
    archive->format.get_format_size = xx_mrnz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_mrnz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_mrnz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_mrnz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_mrnz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_mrnz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_mrnz_free_archive_records_reading;
    archive->format.destroy = xx_mrnz_vtable_destroy;
}

xx_mrnz *xx_mrnz_create(xx_io_device *device, int64_t base_address) {
    xx_mrnz *archive = (xx_mrnz *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_mrnz_init(archive, device, base_address);
    return archive;
}

void xx_mrnz_destroy(xx_mrnz *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_mrnz_free(xx_mrnz *archive) {
    if (!archive) return;
    xx_mrnz_destroy(archive);
    xx_mem_free(archive);
}

static void xx_mrnz_vtable_destroy(Abstractformat *self) {
    xx_mrnz_destroy((xx_mrnz *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_mrnz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_mrnz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_mrnz_parse(self, pd);
    if (!stream) return false;
    xx_mrnz_stream_free(stream);
    return true;
}

bool xx_mrnz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_mrnz *archive = (xx_mrnz *)self;
    xx_mrnz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_mrnz_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_mrnz_stream_free(stream);
    return true;
}

int64_t xx_mrnz_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_mrnz_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_mrnz *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_mrnz_set_record(xx_archive_record *record,
                               const xx_mrnz_member *member) {
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
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_mrnz_copy_options(xx_list_s *target, const xx_list_s *options) {
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

static const xx_var *xx_mrnz_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_mrnz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_mrnz_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_mrnz_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mrnz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_mrnz_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_mrnz_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_mrnz_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_mrnz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_mrnz_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_mrnz_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_mrnz_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_mrnz_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_mrnz_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_mrnz_stream *stream;
    const xx_mrnz_member *member;
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
    stream = (xx_mrnz_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_mrnz_path_safe(member->name)) return false;

    path_option = xx_mrnz_get_option(&state->options,
                                     XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_mrnz_decode(self, member, &plain, &plain_size, pd);
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

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_mrnz_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
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

void xx_mrnz_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
