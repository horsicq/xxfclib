/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Single-member ".CMP" containers, the shape used by (among others)
 * Adaptec and Cheyenne install media.
 *
 *   header, 0x3d bytes at offset 0:
 *     0x00  u16 LE   0x007f, the container magic
 *     0x02  u16 LE   0x003d, the header size - must match the real one
 *     0x04  u16 LE   compression method, 1 or 2
 *     0x06  8 bytes  unconstrained
 *     0x0e  u16 LE   container variant; 9 is the old shape, 11 the newer one
 *     0x10  0x18     unconstrained
 *     0x28  15       original name, NUL terminated inside the field
 *     0x37  u16 LE   0x1000, the block size the LZW codec frames on
 *     0x39  u32 LE   uncompressed size
 *
 *   payload: 0x3d to end-of-file, one stream.
 *
 * Method 1 is the framed LZW/stored codec. Method 2 covers TWO different
 * codecs: the original 9-bit LZSS, and - in the newer container variant - a
 * PKWARE Data Compression Library "implode" stream. The payload names itself,
 * because a DCL stream opens with a literal mode of 0 or 1 followed by a
 * dictionary size of 4, 5 or 6, and no old-LZSS member in the reference set
 * begins with such a pair (the closest starts 00 00, and a zero dictionary
 * size is not a legal DCL header). The stream decides, not the variant word:
 * the variant is recorded for reporting only.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cmp/xx_cmp.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/cmp/xx_cmp.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_CMP_COPY_CHUNK (64 * 1024)

typedef struct xx_cmp_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_cmp_member;

typedef struct xx_cmp_stream_s {
    xx_cmp_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_cmp_stream;

static void xx_cmp_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_cmp_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_cmp_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_cmp_path_safe(const char *name) {
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

static void xx_cmp_stream_free(void *pointer) {
    xx_cmp_stream *stream = (xx_cmp_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_cmp_add(xx_cmp_stream *stream,
                          const xx_cmp_member *member) {
    xx_cmp_member *grown = (xx_cmp_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_CMP_MAGIC 0x007fU
#define XX_CMP_VARIANT_OFFSET 0x0e
#define XX_CMP_SIZE_OFFSET 0x39
#define XX_CMP_HEADER_SIZE 0x3d
#define XX_CMP_NAME_OFFSET 0x28
#define XX_CMP_NAME_SIZE 15
#define XX_CMP_BLOCK_SIZE_OFFSET 0x37
#define XX_CMP_BLOCK_SIZE 0x1000U
#define XX_CMP_METHOD_LZW 1U
#define XX_CMP_METHOD_LZSS 2U
#define XX_CMP_DCL_HEADER_SIZE 2
#define XX_CMP_DCL_MAX_LITERALMODE 1U
#define XX_CMP_DCL_MIN_DICTBITS 4U
#define XX_CMP_DCL_MAX_DICTBITS 6U
#define XX_CMP_MAX_MEMBERS 1
#define XX_CMP_MAX_DECODED ((int64_t)0x10000000)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_cmp_le16(const uint8_t *data);
static uint32_t xx_cmp_le32(const uint8_t *data);
static xx_cmp_stream *xx_cmp_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_cmp_looks_like_dcl(const uint8_t *input, size_t input_size);
static bool xx_cmp_decode(Abstractformat *self, const xx_cmp_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);



static uint16_t xx_cmp_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_cmp_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_cmp_stream *xx_cmp_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_cmp_stream *stream;
    xx_cmp_member member;
    uint8_t header[XX_CMP_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t raw;
    uint32_t method;
    uint32_t name_length;
    uint32_t index;
    char *name = NULL;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* The payload runs to EOF, so a file that is exactly the header holds no
     * member. */
    if (span <= (int64_t)XX_CMP_HEADER_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_cmp_read_at(self, self->base_address, header, sizeof(header)))
        return NULL;

    /* The container has no text magic at all: 0x007f is only two bytes, so on
     * its own it would match roughly one file in 65536. What makes the
     * recognition safe is that FOUR fixed values must agree at once - the
     * magic, the header size echoed at 0x02, the method being 1 or 2, and the
     * block size 0x1000 at 0x37 - plus a printable NUL-terminated name in the
     * fixed field. Dropping any one of them, particularly the block-size
     * word, turns this into a format that matches noise. */
    if (xx_cmp_le16(header) != XX_CMP_MAGIC) return NULL;
    /* The header size is stored as well as fixed; a container that disagrees
     * with itself is not this format. */
    if (xx_cmp_le16(header + 2) != (uint16_t)XX_CMP_HEADER_SIZE) return NULL;
    method = (uint32_t)xx_cmp_le16(header + 4);
    if (method != XX_CMP_METHOD_LZW && method != XX_CMP_METHOD_LZSS) {
        return NULL;
    }
    if (xx_cmp_le16(header + XX_CMP_BLOCK_SIZE_OFFSET) !=
        (uint16_t)XX_CMP_BLOCK_SIZE) {
        return NULL;
    }

    /* The reference reads the size as a signed 32-bit value and requires it
     * positive, so the top bit set is a rejection rather than a two-gigabyte
     * member. */
    raw = xx_cmp_le32(header + XX_CMP_SIZE_OFFSET);
    if (raw > 0x7fffffffU) return NULL;
    uncompressed_size = (int64_t)raw;
    if (uncompressed_size <= 0) return NULL;
    if (uncompressed_size > XX_CMP_MAX_DECODED) return NULL;

    /* The name occupies a fixed 15-byte field and is NUL terminated inside
     * it; a field with no NUL simply uses all 15 bytes. */
    name_length = 0U;
    while (name_length < (uint32_t)XX_CMP_NAME_SIZE &&
           header[XX_CMP_NAME_OFFSET + name_length] != 0U) {
        ++name_length;
    }
    /* An empty name field is a rejection: every real container names its one
     * member, and an all-zero field is exactly what a coincidental 0x007f
     * match looks like. */
    if (name_length == 0U) return NULL;
    for (index = 0U; index < name_length; ++index) {
        /* Control bytes are a rejection. Bytes above 0x7e are accepted: the
         * names are DOS OEM text, where accented characters are ordinary. */
        if (header[XX_CMP_NAME_OFFSET + index] < 0x20U) return NULL;
    }

    compressed_size = span - (int64_t)XX_CMP_HEADER_SIZE;

    stream = (xx_cmp_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    if (!xx_cmp_range_within(span, (int64_t)XX_CMP_HEADER_SIZE,
                             compressed_size)) {
        goto fail;
    }
    if (stream->count >= (size_t)XX_CMP_MAX_MEMBERS) goto fail;

    name = (char *)xx_mem_alloc((size_t)name_length + 1U);
    if (!name) goto fail;
    xx_rt_memcpy(name, header + XX_CMP_NAME_OFFSET, (size_t)name_length);
    name[name_length] = '\0';

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = (int64_t)XX_CMP_HEADER_SIZE;
    member.data_offset = self->base_address + (int64_t)XX_CMP_HEADER_SIZE;
    member.compressed_size = compressed_size;
    member.uncompressed_size = uncompressed_size;
    /* The container's own number, not a library enum. Method 2's split into
     * old LZSS and DCL is decided from the stream inside decode, which is the
     * only place that mapping belongs. */
    member.method = method;
    /* The container stores no timestamp. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_cmp_add(stream, &member)) goto fail;
    name = NULL;

    stream->archive_size = span;
    return stream;

fail:
    if (name) xx_str_free(name);
    xx_cmp_stream_free(stream);
    return NULL;
}



/* The container's own numbers, published unchanged in member->method. */

/* A DCL stream names itself in its first two bytes. */

/* Exactly one member per container. */

/* The plaintext length comes from the header and is attacker-controlled. */

/* True when the stream's first two bytes are a legal PKWARE DCL header.
 * Method 2 is overloaded, and this sniff is the only thing that tells the two
 * codecs apart - the container's variant word is not reliable enough to route
 * on, and the reference implementation gets this wrong by always choosing the
 * old LZSS. */
static bool xx_cmp_looks_like_dcl(const uint8_t *input, size_t input_size) {
    if (input_size < (size_t)XX_CMP_DCL_HEADER_SIZE) return false;
    if ((uint32_t)input[0] > XX_CMP_DCL_MAX_LITERALMODE) return false;
    if ((uint32_t)input[1] < XX_CMP_DCL_MIN_DICTBITS) return false;
    if ((uint32_t)input[1] > XX_CMP_DCL_MAX_DICTBITS) return false;
    return true;
}

static bool xx_cmp_decode(Abstractformat *self, const xx_cmp_member *member,
                          uint8_t **out, size_t *out_size,
                          xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    bool ok;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* Only 1 and 2 exist. Anything else must fail rather than fall through to
     * a stored copy, which would hand the caller compressed bytes. */
    if (member->method != XX_CMP_METHOD_LZW &&
        member->method != XX_CMP_METHOD_LZSS) {
        return false;
    }
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_CMP_MAX_DECODED ||
        member->uncompressed_size > XX_CMP_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_cmp_read_at(self, member->data_offset, input,
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

    if (member->method == XX_CMP_METHOD_LZW) {
        ok = xx_cmp_lzw_decode_memory(input, (size_t)member->compressed_size,
                                      output,
                                      (size_t)member->uncompressed_size,
                                      &written);
    } else if (xx_cmp_looks_like_dcl(input,
                                     (size_t)member->compressed_size)) {
        ok = xx_dcl_decode_memory(input, (size_t)member->compressed_size,
                                  output, (size_t)member->uncompressed_size,
                                  &written);
    } else {
        ok = xx_cmp_lzss_decode_memory(input, (size_t)member->compressed_size,
                                       output,
                                       (size_t)member->uncompressed_size,
                                       &written);
    }

    /* Both codecs are output-driven and stop at out_size, so a short result
     * means the stream ran dry. Report that as failure: a partial member
     * returned as success is the one thing the caller cannot detect. */
    if (!ok || written != (size_t)member->uncompressed_size) {
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

void xx_cmp_init(xx_cmp *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_CMP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cmp");
    xx_format_set_extension(&archive->format, "cmp");
    archive->format.check_is_valid = xx_cmp_check_is_valid;
    archive->format.handle_base_info = xx_cmp_handle_base_info;
    archive->format.get_format_size = xx_cmp_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_cmp_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_cmp_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_cmp_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_cmp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_cmp_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_cmp_free_archive_records_reading;
    archive->format.destroy = xx_cmp_vtable_destroy;
}

xx_cmp *xx_cmp_create(xx_io_device *device, int64_t base_address) {
    xx_cmp *archive = (xx_cmp *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_cmp_init(archive, device, base_address);
    return archive;
}

void xx_cmp_destroy(xx_cmp *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_cmp_free(xx_cmp *archive) {
    if (!archive) return;
    xx_cmp_destroy(archive);
    xx_mem_free(archive);
}

static void xx_cmp_vtable_destroy(Abstractformat *self) {
    xx_cmp_destroy((xx_cmp *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_cmp_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_cmp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_cmp_parse(self, pd);
    if (!stream) return false;
    xx_cmp_stream_free(stream);
    return true;
}

bool xx_cmp_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_cmp *archive = (xx_cmp *)self;
    xx_cmp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_cmp_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_cmp_stream_free(stream);
    return true;
}

int64_t xx_cmp_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_cmp_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_cmp *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_cmp_set_record(xx_archive_record *record,
                                 const xx_cmp_member *member) {
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

static bool xx_cmp_copy_options(xx_list_s *target,
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

static const xx_var *xx_cmp_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_cmp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_cmp_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_cmp_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_cmp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_cmp_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_cmp_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_cmp_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_cmp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_cmp_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_cmp_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_cmp_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_cmp_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_cmp_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_cmp_stream *stream;
    const xx_cmp_member *member;
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
    stream = (xx_cmp_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_cmp_path_safe(member->name)) return false;

    path_option = xx_cmp_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_cmp_decode(self, member, &plain, &plain_size, pd);
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
        !xx_cmp_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_cmp_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
