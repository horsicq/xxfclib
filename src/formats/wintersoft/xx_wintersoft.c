/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Wintersoft "**++" archives.
 *
 *   header, 8 bytes at offset 0:
 *     0x00  4 bytes  "**++"
 *     0x04  4 bytes  the codec tag, "LZW " or "HUFF"
 *
 *   then a chain of members, each an 8-byte record followed immediately by
 *   its compressed bytes:
 *     0x00  u32 LE   uncompressed size
 *     0x04  u32 LE   compressed size; the next record starts at
 *                    record_end + compressed_size
 *     0x08  ...      the compressed member
 *
 *   the chain ends when fewer than 8 bytes remain.
 *
 * THE CODEC TAG IS PER FILE, NOT PER MEMBER, so every member of an archive
 * carries the same method. The container stores no names at all - members
 * are numbered - and no timestamps.
 *
 * "HUFF" is the archiver's own adaptive (FGK) Huffman, xx_wintersoft_ahuff.
 * "LZW " is Mark Nelson's LZW15V, the same dialect as the headerless
 * RAW_LZW15V streams, and is deliberately NOT duplicated in the wintersoft
 * module - this reader wires it to xx_lzw15v_decode_memory. See the note in
 * the decode about the two tolerances that entry point does not have.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/wintersoft/xx_wintersoft.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/wintersoft/xx_wintersoft.h"
#include "xxfclib/algo/lzw15v/xx_lzw15v.h"

#include <stdio.h>

#define XX_WINTERSOFT_COPY_CHUNK (64 * 1024)

typedef struct xx_wintersoft_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_wintersoft_member;

typedef struct xx_wintersoft_stream_s {
    xx_wintersoft_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_wintersoft_stream;

static void xx_wintersoft_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_wintersoft_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_wintersoft_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_wintersoft_path_safe(const char *name) {
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

static void xx_wintersoft_stream_free(void *pointer) {
    xx_wintersoft_stream *stream = (xx_wintersoft_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_wintersoft_add(xx_wintersoft_stream *stream,
                          const xx_wintersoft_member *member) {
    xx_wintersoft_member *grown = (xx_wintersoft_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_WINTERSOFT_HEADER_SIZE 8
#define XX_WINTERSOFT_RECORD_SIZE 8
#define XX_WINTERSOFT_MAX_MEMBERS 100000
#define XX_WINTERSOFT_MAX_DECODED ((int64_t)0x10000000)
#define XX_WINTERSOFT_METHOD_LZW15V 0U
#define XX_WINTERSOFT_METHOD_AHUFF 1U
#define XX_WINTERSOFT_NAME_BUFFER 32

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_wintersoft_le32(const uint8_t *data);
static xx_wintersoft_stream *xx_wintersoft_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_wintersoft_decode(Abstractformat *self, const xx_wintersoft_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);



/* The container's method is a four-byte text tag in the FILE header, so a
 * number here is a rendering of it rather than a stored value. There are
 * exactly two tags and they never vary within a file. */

/* No names are stored; members are numbered. 32 bytes holds "99999.bin" and
 * everything shorter with room to spare. */

static uint32_t xx_wintersoft_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_wintersoft_stream *xx_wintersoft_parse(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    static const uint8_t magic[4] = {(uint8_t)'*', (uint8_t)'*', (uint8_t)'+',
                                     (uint8_t)'+'};
    static const uint8_t tag_lzw[4] = {(uint8_t)'L', (uint8_t)'Z',
                                       (uint8_t)'W', (uint8_t)' '};
    static const uint8_t tag_huff[4] = {(uint8_t)'H', (uint8_t)'U',
                                        (uint8_t)'F', (uint8_t)'F'};
    xx_wintersoft_stream *stream = NULL;
    xx_wintersoft_member member;
    uint8_t header[XX_WINTERSOFT_HEADER_SIZE];
    uint8_t record[XX_WINTERSOFT_RECORD_SIZE];
    char name_buffer[XX_WINTERSOFT_NAME_BUFFER];
    int64_t total;
    int64_t span;
    int64_t offset;
    uint32_t method;
    char *name = NULL;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Header plus at least one record: an archive with no member at all is
     * not one. */
    if (span < (int64_t)(XX_WINTERSOFT_HEADER_SIZE +
                         XX_WINTERSOFT_RECORD_SIZE)) {
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    if (!xx_wintersoft_read_at(self, self->base_address, header,
                               sizeof(header))) {
        return NULL;
    }

    /* "**++" alone is four bytes of punctuation and would match far too much.
     * The codec tag is the other half of the signature, and because only two
     * tags exist, requiring one of them is a real constraint rather than a
     * formality - it is the whole reason this eight-byte gate is trustworthy
     * for a container that has no other fixed field anywhere. */
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;
    if (xx_rt_memcmp(header + 4, tag_lzw, sizeof(tag_lzw)) == 0) {
        method = XX_WINTERSOFT_METHOD_LZW15V;
    } else if (xx_rt_memcmp(header + 4, tag_huff, sizeof(tag_huff)) == 0) {
        method = XX_WINTERSOFT_METHOD_AHUFF;
    } else {
        return NULL;
    }

    stream = (xx_wintersoft_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = (int64_t)XX_WINTERSOFT_HEADER_SIZE;

    while ((offset + (int64_t)XX_WINTERSOFT_RECORD_SIZE) <= span) {
        int64_t uncompressed_size;
        int64_t compressed_size;
        int64_t data_offset;
        int length;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_WINTERSOFT_MAX_MEMBERS) goto fail;

        if (!xx_wintersoft_read_at(self, self->base_address + offset, record,
                                   sizeof(record))) {
            goto fail;
        }

        /* Both sizes are written as u32 but read as signed; a negative one is
         * a rejection, not a four-gigabyte member. */
        uncompressed_size = (int64_t)(int32_t)xx_wintersoft_le32(record);
        compressed_size = (int64_t)(int32_t)xx_wintersoft_le32(record + 4);
        if ((uncompressed_size < 0) || (compressed_size < 0)) goto fail;
        if (uncompressed_size > XX_WINTERSOFT_MAX_DECODED) goto fail;

        data_offset = offset + (int64_t)XX_WINTERSOFT_RECORD_SIZE;
        if (!xx_wintersoft_range_within(span, data_offset, compressed_size)) {
            /* A truncated tail. The reference keeps what is really on the
             * device and stops there rather than discarding the whole
             * archive, and this follows it: the member is published with the
             * bytes that exist, so it lists, and its decode fails honestly
             * instead of the listing silently losing every earlier member.
             * The clamp is what keeps the published extent inside the span. */
            compressed_size = span - data_offset;
            if (compressed_size < 0) goto fail;
        }

        length = xx_rt_snprintf(name_buffer, sizeof(name_buffer), "%u.bin",
                                (unsigned int)stream->count);
        if ((length <= 0) || ((size_t)length >= sizeof(name_buffer))) {
            goto fail;
        }
        name = xx_str_dup(name_buffer);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = (int64_t)XX_WINTERSOFT_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        /* Per FILE, not per member - every member repeats the header's tag. */
        member.method = method;
        /* The container records no timestamps. */
        member.timestamp = 0U;
        member.is_folder = false;

        if (!xx_wintersoft_add(stream, &member)) goto fail;
        name = NULL;

        /* The STORED compressed size, not the decoder's stopping point, is
         * what positions the next record. */
        offset = data_offset + compressed_size;
    }

    if (stream->count == 0U) goto fail;

    /* Whatever is left after the last member's bytes is an overlay, so the
     * archive ends where the chain does. */
    stream->archive_size = (offset < span) ? offset : span;
    return stream;

fail:
    if (name) xx_str_free(name);
    xx_wintersoft_stream_free(stream);
    return NULL;
}


/* Decode one member with the archive-wide codec the header named.
 *
 * On the "LZW " path this calls the standalone LZW15V module rather than a
 * copy inside the wintersoft module, exactly as the wintersoft header asks.
 * That header warns that the container relies on two tolerances, and neither
 * of them is present in xx_lzw15v_decode_memory as it stands:
 *
 *  * RUNNING OUT OF INPUT IS A FAILURE THERE, NOT AN END OF STREAM. The bit
 *    reader returns false the moment it needs a bit past the last byte, and
 *    that propagates straight out as a failed decode. A member whose encoder
 *    omitted the explicit 0x100 END and simply stopped will therefore be
 *    rejected here rather than accepted at its natural end.
 *  * THERE IS NO AUTO-BUMP. The width moves only on an explicit 0x101, so
 *    the reference's "widen before reading whenever the bump threshold has
 *    fallen below the next assignable code" never fires. Against a
 *    Nelson-compatible encoder that path is always exactly one code short of
 *    firing, so this one is expected to cost nothing on real members.
 *
 * The first divergence is the one that can actually reject a valid member,
 * and it is left as a rejection on purpose: a false is visible to the caller,
 * whereas quietly treating a truncated stream as complete would hand back a
 * short buffer dressed as a whole member - the one failure mode the decode
 * contract says a caller cannot detect. If a real "LZW " archive turns up
 * that fails here, the fix belongs in xx_lzw15v_decode_memory (accept input
 * exhaustion as an end of stream when the output is exactly full), not in a
 * fallback here. */
static bool xx_wintersoft_decode(Abstractformat *self,
                                 const xx_wintersoft_member *member,
                                 uint8_t **out, size_t *out_size,
                                 xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    bool decoded;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if ((member->method != XX_WINTERSOFT_METHOD_LZW15V) &&
        (member->method != XX_WINTERSOFT_METHOD_AHUFF)) {
        return false;
    }
    if ((member->compressed_size < 0) || (member->uncompressed_size < 0)) {
        return false;
    }
    if (member->compressed_size > XX_WINTERSOFT_MAX_DECODED) return false;
    if (member->uncompressed_size > XX_WINTERSOFT_MAX_DECODED) return false;

    /* A zero-length member is legal - the record simply records no bytes -
     * but both codecs refuse a zero output capacity, so it is answered here
     * with a one-byte allocation and a reported length of zero. */
    if (member->uncompressed_size == 0) {
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }
    if (member->compressed_size == 0) return false;

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_wintersoft_read_at(self, member->data_offset, input,
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

    if (member->method == XX_WINTERSOFT_METHOD_AHUFF) {
        decoded = xx_wintersoft_ahuff_decode_memory(
            input, (size_t)member->compressed_size, output,
            (size_t)member->uncompressed_size, &written);
    } else {
        decoded = xx_lzw15v_decode_memory(input,
                                          (size_t)member->compressed_size,
                                          output,
                                          (size_t)member->uncompressed_size,
                                          &written);
    }

    /* Both codecs already demand an exact fill; the second half of this test
     * is what makes that a property of this reader rather than of whichever
     * codec happens to be wired in. */
    if (!decoded || (written != (size_t)member->uncompressed_size)) {
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

void xx_wintersoft_init(xx_wintersoft *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_WINTERSOFT;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-wintersoft");
    xx_format_set_extension(&archive->format, "wsa");
    archive->format.check_is_valid = xx_wintersoft_check_is_valid;
    archive->format.handle_base_info = xx_wintersoft_handle_base_info;
    archive->format.get_format_size = xx_wintersoft_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_wintersoft_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_wintersoft_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_wintersoft_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_wintersoft_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_wintersoft_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_wintersoft_free_archive_records_reading;
    archive->format.destroy = xx_wintersoft_vtable_destroy;
}

xx_wintersoft *xx_wintersoft_create(xx_io_device *device, int64_t base_address) {
    xx_wintersoft *archive = (xx_wintersoft *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_wintersoft_init(archive, device, base_address);
    return archive;
}

void xx_wintersoft_destroy(xx_wintersoft *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_wintersoft_free(xx_wintersoft *archive) {
    if (!archive) return;
    xx_wintersoft_destroy(archive);
    xx_mem_free(archive);
}

static void xx_wintersoft_vtable_destroy(Abstractformat *self) {
    xx_wintersoft_destroy((xx_wintersoft *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_wintersoft_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_wintersoft_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_wintersoft_parse(self, pd);
    if (!stream) return false;
    xx_wintersoft_stream_free(stream);
    return true;
}

bool xx_wintersoft_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_wintersoft *archive = (xx_wintersoft *)self;
    xx_wintersoft_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_wintersoft_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_wintersoft_stream_free(stream);
    return true;
}

int64_t xx_wintersoft_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_wintersoft_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_wintersoft *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_wintersoft_set_record(xx_archive_record *record,
                                 const xx_wintersoft_member *member) {
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

static bool xx_wintersoft_copy_options(xx_list_s *target,
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

static const xx_var *xx_wintersoft_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_wintersoft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_wintersoft_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_wintersoft_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_wintersoft_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_wintersoft_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_wintersoft_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_wintersoft_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_wintersoft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_wintersoft_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_wintersoft_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_wintersoft_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_wintersoft_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_wintersoft_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_wintersoft_stream *stream;
    const xx_wintersoft_member *member;
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
    stream = (xx_wintersoft_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_wintersoft_path_safe(member->name)) return false;

    path_option = xx_wintersoft_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_wintersoft_decode(self, member, &plain, &plain_size, pd);
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
        !xx_wintersoft_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_wintersoft_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
