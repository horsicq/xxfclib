/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM TERSE containers, as written by TRSMAIN and AMATERSE on MVS/z-OS.
 *
 * The container holds exactly ONE member. There is no directory, no member
 * name, no record framing, no checksum -- and, crucially, NO STORED
 * UNCOMPRESSED LENGTH. The only way to learn a member's size is to run the
 * decoder, which is what this reader does at parse time.
 *
 * Two header shapes exist, and which one is present is a detection result
 * rather than a fixed skip -- xx_terse_detect() decides:
 *
 *   4 bytes:  01 89 69 A5            (the 0xA5698901 magic read LE)
 *  12 bytes:  one of three profiles, all of them weak on their own --
 *               05 vv nn nn 00 00 00 00 00 00 00 00
 *               09 vv nn nn xx .. .. .. 00 00 00 00
 *               02 vv nn nn xx .. .. .. 00 00 00 00
 *             with vv < 2 and the nn word nonzero -- so the detector follows
 *             them with a sanity probe over the first five 12-bit codes.
 *
 * After the header comes the code stream: 12-bit codes, MSB first, driving a
 * binary pair tree with LRU node recycling. Code 0 ends the stream. There is
 * no Huffman table anywhere and no character-set translation; real data sets
 * are a mix of EBCDIC and ASCII and the bytes are emitted exactly as
 * produced.
 *
 * Because the header size is a detection result, the WHOLE FILE -- header
 * included -- is what goes to the decoder, so the member's data offset is 0
 * and its compressed size is the number of input bytes the stream actually
 * occupies, as reported by xx_terse_scan_memory().
 *
 * Only the SPACK-style stream is implemented; there is no decoder for the
 * static-Huffman PACK variant, and a PACK stream fails the code probe in
 * xx_terse_detect() rather than being decoded as nonsense.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/terse/xx_terse.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/terse/xx_terse.h"

#include <stdio.h>

#define XX_TERSE_COPY_CHUNK (64 * 1024)

typedef struct xx_terse_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_terse_member;

typedef struct xx_terse_stream_s {
    xx_terse_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_terse_stream;

static void xx_terse_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_terse_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_terse_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_terse_path_safe(const char *name) {
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

static void xx_terse_stream_free(void *pointer) {
    xx_terse_stream *stream = (xx_terse_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_terse_add(xx_terse_stream *stream,
                          const xx_terse_member *member) {
    xx_terse_member *grown = (xx_terse_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_TERSE_PROBE_SIZE 20
#define XX_TERSE_MIN_SIZE 5
#define XX_TERSE_MAX_MEMBERS 1
#define XX_TERSE_MAX_INPUT 0x20000000
#define XX_TERSE_MAX_DECODED 0x20000000
#define XX_TERSE_METHOD_SPACK 1U
#define XX_TERSE_MEMBER_NAME "terse"

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static xx_terse_stream *xx_terse_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_terse_decode(Abstractformat *self, const xx_terse_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Enough for a 12-byte header plus the eight bytes the detector's code probe
 * reads; the same probe window the reference detector uses. */
/* The 4-byte magic header plus one byte of stream. */
/* One member, always: the cap exists only so the generator-wide rule is
 * satisfied by a named constant. */
/* Mirrors the reference ceiling on both the input and the decoded size. */
/* The container carries no method field -- every stream is the same codec.
 * The value is synthesised so that 0 keeps its generator-wide meaning of
 * "stored" and a listing never claims this member is uncompressed. */
/* No name is stored anywhere in the container; the Qt reference falls back
 * to the device's file name, which is not reachable from here. */

static xx_terse_stream *xx_terse_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    xx_terse_stream *stream = NULL;
    uint8_t probe[XX_TERSE_PROBE_SIZE];
    uint8_t *data = NULL;
    xx_terse_member member;
    int64_t total;
    int64_t span;
    size_t probe_size;
    size_t header_size = 0U;
    size_t consumed = 0U;
    size_t produced = 0U;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_TERSE_MIN_SIZE || span > XX_TERSE_MAX_INPUT) return NULL;

    probe_size = span < (int64_t)XX_TERSE_PROBE_SIZE ? (size_t)span
                                                     : (size_t)XX_TERSE_PROBE_SIZE;
    if (!xx_terse_read_at(self, self->base_address, probe, probe_size)) {
        return NULL;
    }
    /* Cheap gate first: the detector recognises the 4-byte magic outright,
     * and for the three 12-byte header profiles -- which are little more
     * than a leading byte, a version below 2 and eight zero bytes -- it
     * follows up with a probe over the first five 12-bit codes. Those
     * profiles are far too weak to stand alone, which is why the full
     * measuring decode below is part of validity and not an extra. */
    if (!xx_terse_detect(probe, probe_size, &header_size)) return NULL;
    if (span <= (int64_t)header_size) return NULL;
    if ((uint64_t)span > (uint64_t)SIZE_MAX) return NULL;

    data = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!data) return NULL;
    if (!xx_terse_read_at(self, self->base_address, data, (size_t)span)) {
        xx_mem_free(data);
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(data);
        return NULL;
    }

    /* The container stores no decoded length, so the size has to be measured
     * by running the stream. Reaching the end code within the ceiling is
     * also the real proof that this is a TERSE container at all: a file that
     * merely matches one of the 12-byte header profiles will run the node
     * ring into an unreadable code and fail here. */
    if (!xx_terse_scan_memory(data, (size_t)span, (size_t)XX_TERSE_MAX_DECODED,
                              &consumed, &produced)) {
        xx_mem_free(data);
        return NULL;
    }
    xx_mem_free(data);
    data = NULL;

    /* A stream that consumes no more than its own header, or that decodes to
     * nothing, is not a data set. */
    if (consumed <= header_size || produced == 0U) return NULL;
    if ((int64_t)consumed > span) return NULL;

    stream = (xx_terse_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    xx_mem_zero(&member, sizeof(member));
    member.name = xx_str_dup(XX_TERSE_MEMBER_NAME);
    if (!member.name) goto fail;
    member.header_offset = self->base_address;
    member.header_size = (int64_t)header_size;
    /* Deliberately the start of the file and not the end of the header: the
     * header size is something xx_terse_detect() worked out, so the decoder
     * has to be handed the header back along with the codes. The member
     * therefore overlaps its own header, which is correct here. */
    member.data_offset = self->base_address;
    member.compressed_size = (int64_t)consumed;
    member.uncompressed_size = (int64_t)produced;
    member.method = XX_TERSE_METHOD_SPACK;
    member.timestamp = 0U;
    member.is_folder = false;
    if (!xx_terse_add(stream, &member)) {
        xx_str_free(member.name);
        goto fail;
    }

    /* Whole bytes still sitting in the decoder's bit cache were never part of
     * the stream, so `consumed` is the exact end of the container and
     * anything past it is overlay. */
    stream->archive_size = (int64_t)consumed;
    return stream;

fail:
    xx_terse_stream_free(stream);
    return NULL;
}


/* The measured size is re-derived by the decoder itself, but it still passes
 * through the member struct, so it is capped before it becomes an
 * allocation. */

static bool xx_terse_decode(Abstractformat *self,
                            const xx_terse_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    size_t plain_size;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size <= 0) {
        return false;
    }
    if (member->uncompressed_size > XX_TERSE_MAX_DECODED) return false;
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) return false;

    /* The container defines no method field, so there is exactly one value
     * the parse ever writes. Anything else means the member did not come
     * from this parse, and guessing at it would produce garbage. */
    if (member->method != XX_TERSE_METHOD_SPACK) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    /* data_offset is the start of the container, header included: the
     * decoder re-runs xx_terse_detect() on what it is given. */
    if (!xx_terse_read_at(self, member->data_offset, packed,
                          (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain_size = (size_t)member->uncompressed_size;
    plain = (uint8_t *)xx_mem_alloc(plain_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (!xx_terse_decode_memory(packed, (size_t)member->compressed_size, plain,
                                plain_size, &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* The measure and the decode run the same core over the same bytes, so
     * they cannot disagree -- but returning true with fewer bytes than the
     * member claims is the one failure a caller cannot detect, so the
     * equality is asserted rather than assumed. */
    if (written != plain_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_terse_init(xx_terse *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_TERSE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-terse");
    xx_format_set_extension(&archive->format, "trs");
    archive->format.check_is_valid = xx_terse_check_is_valid;
    archive->format.handle_base_info = xx_terse_handle_base_info;
    archive->format.get_format_size = xx_terse_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_terse_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_terse_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_terse_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_terse_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_terse_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_terse_free_archive_records_reading;
    archive->format.destroy = xx_terse_vtable_destroy;
}

xx_terse *xx_terse_create(xx_io_device *device, int64_t base_address) {
    xx_terse *archive = (xx_terse *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_terse_init(archive, device, base_address);
    return archive;
}

void xx_terse_destroy(xx_terse *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_terse_free(xx_terse *archive) {
    if (!archive) return;
    xx_terse_destroy(archive);
    xx_mem_free(archive);
}

static void xx_terse_vtable_destroy(Abstractformat *self) {
    xx_terse_destroy((xx_terse *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_terse_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_terse_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_terse_parse(self, pd);
    if (!stream) return false;
    xx_terse_stream_free(stream);
    return true;
}

bool xx_terse_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_terse *archive = (xx_terse *)self;
    xx_terse_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_terse_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_terse_stream_free(stream);
    return true;
}

int64_t xx_terse_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_terse_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_terse *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_terse_set_record(xx_archive_record *record,
                                 const xx_terse_member *member) {
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

static bool xx_terse_copy_options(xx_list_s *target,
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

static const xx_var *xx_terse_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_terse_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_terse_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_terse_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_terse_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_terse_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_terse_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_terse_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_terse_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_terse_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_terse_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_terse_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_terse_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_terse_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_terse_stream *stream;
    const xx_terse_member *member;
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
    stream = (xx_terse_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_terse_path_safe(member->name)) return false;

    path_option = xx_terse_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_terse_decode(self, member, &plain, &plain_size, pd);
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
        !xx_terse_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_terse_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
