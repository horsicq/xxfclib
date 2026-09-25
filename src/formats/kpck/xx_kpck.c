/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * KPCK packed streams - the container written by KolibriOS kpack.
 *
 *   header, 12 bytes at offset 0:
 *     0x00   4  char[4] magic "KPCK"
 *     0x04   4  u32 LE  plaintext length
 *     0x08   4  u32 LE  method / flag word.  Only three values occur in the
 *                       corpus - 0x01 (189x), 0x81 (180x) and 0x41 (37x):
 *                         0x01  LZMA, always set
 *                         0x40  x86 call-trick filter 1
 *                         0x80  x86 call-trick filter 2
 *                       The two call-trick bits are mutually exclusive and no
 *                       other bit may be set; kpack's own unpacker accepts
 *                       nothing else and neither does this reader.
 *     0x0c   n  payload packed stream, running to end-of-file
 *
 * THIS IS NOT A CONTAINER.  Every sample holds exactly one payload: there is
 * no member count, no name, no directory and no terminator anywhere in the
 * file.  So a valid file yields exactly one record.
 *
 * THE CODEC IS LZMA1, hard-wired lc=3 lp=0 pb=2, with NO 13-byte LZMA header
 * and a range coder initialised from the FIRST FOUR payload bytes read
 * little-endian (kpack byte-swaps that dword before handing it to a stock
 * five-byte initialiser, so no dummy byte is skipped).  When a call-trick bit
 * is set the last five bytes of the CONTAINER are the filter's parameters,
 * which is why the decode below hands the shared decoder the whole file from
 * the magic rather than the payload alone.  That decoder already exists as
 * xx_kolibrikpack_decode_memory(); this reader does not duplicate it.
 *
 * Identification of the codec came from the corpus itself: every stream
 * decodes through that decoder to exactly the declared plaintext length, and
 * all 406 plaintexts start with the "MENUET01" executable signature, which is
 * what KolibriOS binaries are.  The stored plaintext length is the anchor - a
 * wrong decode cannot land on it 406 times out of 406.
 *
 * The original file name is stored nowhere, so the single record carries a
 * fixed placeholder name; kpack itself relies on the container's own file
 * name, which this reader cannot see.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/kpck/xx_kpck.h"

#include "xxfclib/algo/kolibrikpack/xx_kolibrikpack.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef KPCK
#define XX_KPCK_FILE_TYPE XX_FILE_TYPE_KPCK
#else
#define XX_KPCK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_KPCK_HEADER_SIZE 12
#define XX_KPCK_MIN_PACKED_SIZE 2
#define XX_KPCK_MAX_MEMBERS 1
#define XX_KPCK_PLACEHOLDER_NAME "kpck_data"
/* The plaintext length is attacker-controlled; this caps what a future decode
 * could ever be asked to allocate. */
#define XX_KPCK_MAX_DECODED ((int64_t)256 * 1024 * 1024)
/* The corpus tops out near 30:1.  1024:1 leaves generous headroom while still
 * refusing a twelve-byte header that claims a gigabyte. */
#define XX_KPCK_MAX_RATIO 1024
#define XX_KPCK_RATIO_SLACK 8192
/* The method word's bits, as kpack defines them. */
#define XX_KPCK_FLAG_LZMA 0x01U
#define XX_KPCK_FLAG_CALLTRICK1 0x40U
#define XX_KPCK_FLAG_CALLTRICK2 0x80U
#define XX_KPCK_METHOD_MASK                                                  \
    (XX_KPCK_FLAG_LZMA | XX_KPCK_FLAG_CALLTRICK1 | XX_KPCK_FLAG_CALLTRICK2)

typedef struct xx_kpck_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
} xx_kpck_member;

typedef struct xx_kpck_stream_s {
    xx_kpck_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_kpck_stream;

static void xx_kpck_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_kpck_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_kpck_read_at(Abstractformat *self, int64_t offset,
                            uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

static const xx_var *xx_kpck_get_option(const xx_list_s *options,
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

static bool xx_kpck_path_safe(const char *name) {
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

static void xx_kpck_stream_free(void *pointer) {
    xx_kpck_stream *stream = (xx_kpck_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_kpck_add(xx_kpck_stream *stream, const xx_kpck_member *member) {
    xx_kpck_member *grown = (xx_kpck_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* --------------------------------------------------------------- parse -- */

static xx_kpck_stream *xx_kpck_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_kpck_stream *stream;
    xx_kpck_member member;
    uint8_t header[XX_KPCK_HEADER_SIZE];
    char *name;
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t raw_size;
    uint32_t method;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A header with no payload behind it is not a packed stream. */
    if (span <= XX_KPCK_HEADER_SIZE) return NULL;
    if (!xx_kpck_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, "KPCK", 4U) != 0) return NULL;

    raw_size = xx_kpck_le32(header + 4);
    method = xx_kpck_le32(header + 8);
    /* The method word is the kpack flag bitfield: LZMA always set, at most
     * one of the two call-trick filters, nothing else.  A word outside that
     * is not something this reader can describe or decode, so it is rejected
     * rather than carried along as an opaque number. */
    if ((method & ~XX_KPCK_METHOD_MASK) != 0U ||
        (method & XX_KPCK_FLAG_LZMA) == 0U ||
        (method & (XX_KPCK_FLAG_CALLTRICK1 | XX_KPCK_FLAG_CALLTRICK2)) ==
            (XX_KPCK_FLAG_CALLTRICK1 | XX_KPCK_FLAG_CALLTRICK2)) {
        return NULL;
    }

    /* Bounded before it is used for anything: the length drives the eventual
     * output allocation, so it is capped against the ratio the payload can
     * physically justify as well as against an absolute ceiling. */
    if ((raw_size & 0x80000000U) != 0U) return NULL;
    uncompressed_size = (int64_t)raw_size;
    if (uncompressed_size < 1 || uncompressed_size > XX_KPCK_MAX_DECODED) {
        return NULL;
    }

    /* The header stores no packed size: end-of-file is the only boundary. */
    compressed_size = span - XX_KPCK_HEADER_SIZE;
    if (compressed_size < XX_KPCK_MIN_PACKED_SIZE) return NULL;
    if (compressed_size > XX_KPCK_MAX_DECODED) return NULL;
    if (uncompressed_size >
        (compressed_size * XX_KPCK_MAX_RATIO) + XX_KPCK_RATIO_SLACK) {
        return NULL;
    }

    stream = (xx_kpck_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* The original name is stored nowhere and this reader cannot see the
     * container's own file name, so the single record gets a fixed, extension
     * -less placeholder: inventing an extension would be a claim about
     * content the format never makes. */
    name = xx_str_dup(XX_KPCK_PLACEHOLDER_NAME);
    if (!name) goto fail;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_KPCK_HEADER_SIZE;
    member.data_offset = self->base_address + XX_KPCK_HEADER_SIZE;
    member.compressed_size = compressed_size;
    member.uncompressed_size = uncompressed_size;
    member.method = method;
    if (!xx_kpck_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    if (stream->count != (size_t)XX_KPCK_MAX_MEMBERS) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_kpck_stream_free(stream);
    return NULL;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_kpck_init(xx_kpck *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_KPCK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-kpck");
    xx_format_set_extension(&archive->format, "kpck");
    archive->format.check_is_valid = xx_kpck_check_is_valid;
    archive->format.handle_base_info = xx_kpck_handle_base_info;
    archive->format.get_format_size = xx_kpck_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_kpck_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_kpck_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_kpck_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_kpck_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_kpck_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_kpck_free_archive_records_reading;
    archive->format.destroy = xx_kpck_vtable_destroy;
}

xx_kpck *xx_kpck_create(xx_io_device *device, int64_t base_address) {
    xx_kpck *archive = (xx_kpck *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_kpck_init(archive, device, base_address);
    return archive;
}

void xx_kpck_destroy(xx_kpck *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_kpck_free(xx_kpck *archive) {
    if (!archive) return;
    xx_kpck_destroy(archive);
    xx_mem_free(archive);
}

static void xx_kpck_vtable_destroy(Abstractformat *self) {
    xx_kpck_destroy((xx_kpck *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_kpck_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_kpck_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_kpck_parse(self, pd);
    if (!stream) return false;
    xx_kpck_stream_free(stream);
    return true;
}

bool xx_kpck_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_kpck *archive = (xx_kpck *)self;
    xx_kpck_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_kpck_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_kpck_stream_free(stream);
    return true;
}

int64_t xx_kpck_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_kpck_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_kpck *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_kpck_set_record(xx_archive_record *record,
                               const xx_kpck_member *member) {
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
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_kpck_copy_options(xx_list_s *target, const xx_list_s *options) {
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

xx_archive_record_state *xx_kpck_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_kpck_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_kpck_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_kpck_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_kpck_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_kpck_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_kpck_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_kpck_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_kpck_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_kpck_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_kpck_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_kpck_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

/* The decoder wants the WHOLE container from the magic, not the payload: a
 * call-tricked stream keeps its five-byte filter trailer at end-of-file, so
 * handing over only the bytes behind the header would silently drop the
 * filter and produce a plausible-looking wrong answer. */
static bool xx_kpck_decode(Abstractformat *self, const xx_kpck_member *member,
                           uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    int64_t container_size;
    size_t written = 0U;
    size_t declared = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_KPCK_MAX_DECODED) {
        return false;
    }
    if (member->compressed_size < XX_KPCK_MIN_PACKED_SIZE ||
        member->compressed_size > XX_KPCK_MAX_DECODED) {
        return false;
    }
    container_size = member->header_size + member->compressed_size;

    input = (uint8_t *)xx_mem_alloc((size_t)container_size);
    if (!input) return false;
    if (!xx_kpck_read_at(self, member->header_offset, input,
                         (size_t)container_size)) {
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
    if (!xx_kolibrikpack_check_header(input, (size_t)container_size,
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
    /* The declared plaintext length is the only anchor the format offers, so
     * a short decode is a failed decode. */
    if (!xx_kolibrikpack_decode_memory(input, (size_t)container_size, output,
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

bool xx_kpck_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_kpck_stream *stream;
    const xx_kpck_member *member;
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
    stream = (xx_kpck_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_kpck_path_safe(member->name)) return false;

    path_option =
        xx_kpck_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_kpck_decode(self, member, &plain, &plain_size, pd);
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
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_kpck_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_kpck_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
