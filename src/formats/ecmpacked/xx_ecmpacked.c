/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * EmmaSetup packed members.
 *
 *   header, 38 bytes at offset 0:
 *     0x00  4 bytes   signature, 'E' 'C' 'M' 0x00
 *     0x04  u16 LE    version
 *     0x06  u32 LE    unknown, varies across the corpus
 *     0x0a  u16 LE    unknown
 *     0x0c  u32 LE    unknown
 *     0x10  20 bytes  reserved, zero in every known member
 *     0x24  u16 LE    unknown
 *
 *   payload: offset 38 to end-of-file, one Okumura LZSS stream
 *     (4 KiB ring, F = 18) holding exactly one member.
 *
 * The container is a single-member wrapper: it stores no member count, no
 * name, and - crucially - no uncompressed size. The only way to learn the
 * decoded length is to walk the token stream and count the bytes it would
 * emit, which is what parse does; the walk doubles as the format's real
 * validation, because a stream that does not walk cleanly to end-of-file is
 * not this format.
 *
 * "ECM\0" also opens Neill Corlett's unrelated ECM (CD-image) files. Those
 * cannot carry the run of twenty zero bytes at 0x10, because their token
 * stream treats 0x00 as the end-of-stream marker, so the reserved-run check
 * is what separates the two.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ecmpacked/xx_ecmpacked.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/ampk/xx_ampk.h"

#include <stdio.h>

#define XX_ECMPACKED_COPY_CHUNK (64 * 1024)

typedef struct xx_ecmpacked_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_ecmpacked_member;

typedef struct xx_ecmpacked_stream_s {
    xx_ecmpacked_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_ecmpacked_stream;

static void xx_ecmpacked_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ecmpacked_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_ecmpacked_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_ecmpacked_path_safe(const char *name) {
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

static void xx_ecmpacked_stream_free(void *pointer) {
    xx_ecmpacked_stream *stream = (xx_ecmpacked_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_ecmpacked_add(xx_ecmpacked_stream *stream,
                          const xx_ecmpacked_member *member) {
    xx_ecmpacked_member *grown = (xx_ecmpacked_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_ECMPACKED_SCAN_CHUNK 0x10000
#define XX_ECMPACKED_MEMBER_NAME "ecm_data"
#define XX_ECMPACKED_HEADER_SIZE 38
#define XX_ECMPACKED_RESERVED_OFFSET 0x10
#define XX_ECMPACKED_RESERVED_END 0x24
#define XX_ECMPACKED_MAX_MEMBERS 1
#define XX_ECMPACKED_METHOD_LZSS 0U
#define XX_ECMPACKED_MAX_DECODED ((int64_t)0x10000000)

typedef struct xx_ecmpacked_scan_s {
    Abstractformat *self;
    int64_t base;      /* absolute offset of payload byte 0 */
    int64_t size;      /* payload length */
    uint8_t *buffer;
    int64_t chunk_offset;
    int64_t chunk_size;
} xx_ecmpacked_scan;

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static bool xx_ecmpacked_scan_byte(xx_ecmpacked_scan *scan, int64_t position, uint8_t *out);
static bool xx_ecmpacked_measure(Abstractformat *self, int64_t base, int64_t size, int64_t *out_size, xx_pd_struct *pd);
static xx_ecmpacked_stream *xx_ecmpacked_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_ecmpacked_decode(Abstractformat *self, const xx_ecmpacked_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The payload is walked, not buffered: it can be the whole file, and the walk
 * needs at most two bytes at a time. */

/* No member name is stored anywhere in the container - the installer supplies
 * it out of band - so every archive lists its single member under this
 * name. */



/* Fetch one payload byte, refilling the window when the cursor leaves it. */
static bool xx_ecmpacked_scan_byte(xx_ecmpacked_scan *scan, int64_t position,
                                   uint8_t *out) {
    int64_t wanted;

    if (position < 0 || position >= scan->size) return false;
    if (position < scan->chunk_offset ||
        position >= scan->chunk_offset + scan->chunk_size) {
        wanted = scan->size - position;
        if (wanted > XX_ECMPACKED_SCAN_CHUNK) {
            wanted = XX_ECMPACKED_SCAN_CHUNK;
        }
        if (!xx_ecmpacked_read_at(scan->self, scan->base + position,
                                  scan->buffer, (size_t)wanted)) {
            return false;
        }
        scan->chunk_offset = position;
        scan->chunk_size = wanted;
    }
    *out = scan->buffer[position - scan->chunk_offset];
    return true;
}

/* Walk the LZSS token stream and count the bytes it would emit. The container
 * never stores the decoded length, so this is the only way to learn it - and
 * because the walk must consume the payload exactly, it is simultaneously the
 * strongest structural check the format offers. */
static bool xx_ecmpacked_measure(Abstractformat *self, int64_t base,
                                 int64_t size, int64_t *out_size,
                                 xx_pd_struct *pd) {
    xx_ecmpacked_scan scan;
    int64_t position = 0;
    int64_t unpacked = 0;
    uint32_t flags = 0U;
    int32_t flag_bits = 0;
    uint8_t second = 0U;
    bool result = false;

    *out_size = 0;
    if (size <= 0) return false;
    scan.self = self;
    scan.base = base;
    scan.size = size;
    scan.chunk_offset = 0;
    scan.chunk_size = 0;
    scan.buffer = (uint8_t *)xx_mem_alloc((size_t)XX_ECMPACKED_SCAN_CHUNK);
    if (!scan.buffer) return false;

    while (position < size) {
        if (pd && xx_pd_is_stopped(pd)) goto done;

        if (flag_bits == 0) {
            if (!xx_ecmpacked_scan_byte(&scan, position, &second)) goto done;
            flags = (uint32_t)second;
            flag_bits = 8;
            ++position;
            continue;
        }

        if (flags & 1U) {
            /* A literal: one input byte, one output byte. Its value does not
             * affect the count, so it is not read here. */
            ++position;
            ++unpacked;
        } else {
            /* A genuine stream never truncates a two-byte match token: it
             * ends either on a flag-group boundary or with the input spent
             * between tokens. Anything else is not this format, and this is
             * the check that keeps a stray "ECM\0" from measuring. */
            if (size - position < 2) goto done;
            if (!xx_ecmpacked_scan_byte(&scan, position + 1, &second)) {
                goto done;
            }
            /* Length is stored three less than the true run length. */
            unpacked += (int64_t)(second & 0x0fU) + 3;
            position += 2;
        }

        flags >>= 1;
        --flag_bits;
        if (unpacked > XX_ECMPACKED_MAX_DECODED) goto done;
    }

    if (unpacked <= 0) goto done;
    *out_size = unpacked;
    result = true;

done:
    xx_mem_free(scan.buffer);
    return result;
}

static xx_ecmpacked_stream *xx_ecmpacked_parse(Abstractformat *self,
                                               xx_pd_struct *pd) {
    static const uint8_t signature[4] = {(uint8_t)'E', (uint8_t)'C',
                                         (uint8_t)'M', 0U};
    xx_ecmpacked_stream *stream;
    xx_ecmpacked_member member;
    uint8_t header[XX_ECMPACKED_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    int64_t uncompressed_size = 0;
    size_t index;
    char *name;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Header plus at least a flag byte and one token byte; anything shorter
     * cannot hold a member. */
    if (span < XX_ECMPACKED_HEADER_SIZE + 2) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_ecmpacked_read_at(self, self->base_address, header,
                              sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, signature, sizeof(signature)) != 0) return NULL;

    /* Twenty zero bytes at 0x10. Four bytes of signature are far too weak on
     * their own, and this run is what separates the container from Neill
     * Corlett's unrelated "ECM\0" files, whose token stream cannot produce
     * such a run. Loosening this check makes the format match those. */
    for (index = (size_t)XX_ECMPACKED_RESERVED_OFFSET;
         index < (size_t)XX_ECMPACKED_RESERVED_END; ++index) {
        if (header[index] != 0U) return NULL;
    }

    /* The version word at 0x04 is recorded but not constrained: the corpus
     * carries several values and no rule relating them to the layout. */

    compressed_size = span - XX_ECMPACKED_HEADER_SIZE;
    if (compressed_size > XX_ECMPACKED_MAX_DECODED) return NULL;
    if (!xx_ecmpacked_measure(self,
                              self->base_address + XX_ECMPACKED_HEADER_SIZE,
                              compressed_size, &uncompressed_size, pd)) {
        return NULL;
    }

    stream = (xx_ecmpacked_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* The payload is the file minus the header, so containment is arithmetic
     * rather than a claim from the container - but check it anyway, because a
     * later edit to the header size must not silently publish an overrun. */
    if (!xx_ecmpacked_range_within(span, XX_ECMPACKED_HEADER_SIZE,
                                   compressed_size)) {
        goto fail;
    }
    if (stream->count >= (size_t)XX_ECMPACKED_MAX_MEMBERS) goto fail;

    name = xx_str_dup(XX_ECMPACKED_MEMBER_NAME);
    if (!name) goto fail;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_ECMPACKED_HEADER_SIZE;
    member.data_offset = self->base_address + XX_ECMPACKED_HEADER_SIZE;
    member.compressed_size = compressed_size;
    member.uncompressed_size = uncompressed_size;
    member.method = XX_ECMPACKED_METHOD_LZSS;
    /* No timestamp is stored anywhere in the container. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_ecmpacked_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }

    stream->archive_size = span;
    return stream;

fail:
    xx_ecmpacked_stream_free(stream);
    return NULL;
}



/* The container holds exactly one member, so the cap the briefing asks for is
 * a constant rather than a runaway guard. */

/* The container carries no method field: every member is Okumura LZSS. Zero
 * is the only value parse ever publishes, and decode refuses anything else so
 * that a future method cannot be silently mistaken for this one. */

/* The shared AMPK LZSS decoder refuses an output larger than this, and the
 * measured length is derived from attacker-controlled tokens, so the walk and
 * the allocation must both stop at the same ceiling. */

/* One Okumura LZSS stream, whose decoded length parse measured by walking the
 * tokens. The decoder is output-driven: it stops exactly at out_size and
 * fails if the input runs out first, so "wrote fewer bytes than claimed" is
 * reported as failure rather than as a short success. */
static bool xx_ecmpacked_decode(Abstractformat *self,
                                const xx_ecmpacked_member *member,
                                uint8_t **out, size_t *out_size,
                                xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* A method this reader does not implement must fail here: treating it as
     * stored would emit compressed bytes that look like data. */
    if (member->method != XX_ECMPACKED_METHOD_LZSS) return false;
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_ECMPACKED_MAX_DECODED ||
        member->uncompressed_size > XX_ECMPACKED_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_ecmpacked_read_at(self, member->data_offset, input,
                              (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (!xx_ampk_lzss_decode_memory(input, (size_t)member->compressed_size,
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

void xx_ecmpacked_init(xx_ecmpacked *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ECMPACKED;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-emmasetup-ecm");
    xx_format_set_extension(&archive->format, "ecm");
    archive->format.check_is_valid = xx_ecmpacked_check_is_valid;
    archive->format.handle_base_info = xx_ecmpacked_handle_base_info;
    archive->format.get_format_size = xx_ecmpacked_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ecmpacked_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ecmpacked_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ecmpacked_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ecmpacked_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ecmpacked_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ecmpacked_free_archive_records_reading;
    archive->format.destroy = xx_ecmpacked_vtable_destroy;
}

xx_ecmpacked *xx_ecmpacked_create(xx_io_device *device, int64_t base_address) {
    xx_ecmpacked *archive = (xx_ecmpacked *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ecmpacked_init(archive, device, base_address);
    return archive;
}

void xx_ecmpacked_destroy(xx_ecmpacked *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ecmpacked_free(xx_ecmpacked *archive) {
    if (!archive) return;
    xx_ecmpacked_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ecmpacked_vtable_destroy(Abstractformat *self) {
    xx_ecmpacked_destroy((xx_ecmpacked *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ecmpacked_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ecmpacked_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ecmpacked_parse(self, pd);
    if (!stream) return false;
    xx_ecmpacked_stream_free(stream);
    return true;
}

bool xx_ecmpacked_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ecmpacked *archive = (xx_ecmpacked *)self;
    xx_ecmpacked_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ecmpacked_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_ecmpacked_stream_free(stream);
    return true;
}

int64_t xx_ecmpacked_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ecmpacked_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ecmpacked *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ecmpacked_set_record(xx_archive_record *record,
                                 const xx_ecmpacked_member *member) {
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

static bool xx_ecmpacked_copy_options(xx_list_s *target,
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

static const xx_var *xx_ecmpacked_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ecmpacked_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ecmpacked_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ecmpacked_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ecmpacked_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ecmpacked_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ecmpacked_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ecmpacked_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ecmpacked_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ecmpacked_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ecmpacked_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ecmpacked_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ecmpacked_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ecmpacked_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ecmpacked_stream *stream;
    const xx_ecmpacked_member *member;
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
    stream = (xx_ecmpacked_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ecmpacked_path_safe(member->name)) return false;

    path_option = xx_ecmpacked_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ecmpacked_decode(self, member, &plain, &plain_size, pd);
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
        !xx_ecmpacked_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_ecmpacked_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
