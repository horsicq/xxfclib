/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TPS (Clarion / TopSpeed) archives.
 *
 *   banner, 4 bytes at the base address:  "TPS" 0x1a
 *
 * then a chain of 0x16-byte directory entries, each followed IMMEDIATELY by
 * its own payload -- there is no separate directory region:
 *
 *     +0x00  u8   entry tag; MUST be 0x02
 *     +0x01  u8   name length, 1..12
 *     +0x02  12   the name, Latin-1, NOT NUL terminated, padded
 *     +0x0e  i32  the coded length plus four, SIGNED and sometimes negative
 *     +0x12  u32  the uncompressed length, stored XOR 0x80808080
 *
 * and the chain is closed by a single 0x04 byte when one byte is left over.
 *
 * TWO FIELDS NEED CARE.
 *
 * The dword at +0x0e is signed and may be negative; the reference takes its
 * absolute value and subtracts four.  Reading it as unsigned turns a negative
 * entry into a four-gigabyte member and ends the walk there.
 *
 * The dword at +0x12 is the plaintext length XOR 0x80808080 -- and it is also,
 * un-XORed, the first four bytes fed to the member's first framing checksum,
 * which is why the codec takes it as an input rather than measuring.
 *
 * The ON-DISK length of a member is NOT stored anywhere.  It is derived:
 * the coded bytes, plus one check byte per 0x4000-byte block, plus the single
 * 0x01 end marker, with the block counter starting at four because the size
 * dword above is charged to the first block.  That arithmetic is directory
 * layout, not decompression, so it lives here; the framing it describes is
 * verified by the codec.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/tps/xx_tps.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/tps/xx_tps.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

typedef struct xx_tps_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_tps_member;

typedef struct xx_tps_stream_s {
    xx_tps_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_tps_stream;

static void xx_tps_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_tps_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_tps_range_within(int64_t total, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_tps_path_safe(const char *name) {
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

static void xx_tps_stream_free(void *pointer) {
    xx_tps_stream *stream = (xx_tps_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_tps_add(xx_tps_stream *stream, const xx_tps_member *member) {
    xx_tps_member *grown = (xx_tps_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define XX_TPS_MAGIC_SIZE 4
#define XX_TPS_ENTRY_SIZE 0x16
#define XX_TPS_OFFSET_NAME 0x02
#define XX_TPS_MAX_NAME 12
#define XX_TPS_OFFSET_CODED 0x0E
#define XX_TPS_OFFSET_UNCOMPRESSED 0x12
#define XX_TPS_ENTRY_TAG 0x02U
#define XX_TPS_END_TAG 0x04U
#define XX_TPS_MAX_MEMBERS 100000
/* The framing block size the check bytes are spaced at. */
#define XX_TPS_BLOCK_SIZE ((int64_t)0x4000)
/* The four bytes of the uncompressed-size dword charged to block zero. */
#define XX_TPS_SIZE_DWORD 4
/* The stored plaintext mask. */
#define XX_TPS_SIZE_MASK 0x80808080U
/* Bounds what a corrupt directory entry may ask an extraction to allocate. */
#define XX_TPS_MAX_MEMBER_SIZE ((int64_t)0x10000000)
/* One method, published so a listing has something to show. */
#define XX_TPS_METHOD_LZHUF 1U

static const uint8_t XX_TPS_MAGIC[XX_TPS_MAGIC_SIZE] = {'T', 'P', 'S', 0x1AU};

static uint32_t xx_tps_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The on-disk length of a member whose coded payload is @p coded bytes: the
 * coded bytes themselves, one check byte per block, and the 0x01 end marker.
 * The block count is ceil((coded + 4) / 0x4000) -- the four is the
 * uncompressed-size dword, which the framing charges to block zero. */
static int64_t xx_tps_stored_size(int64_t coded) {
    int64_t blocks;

    if (coded < 0 || coded > XX_TPS_MAX_MEMBER_SIZE) return -1;
    blocks = (coded + XX_TPS_SIZE_DWORD + (XX_TPS_BLOCK_SIZE - 1)) /
             XX_TPS_BLOCK_SIZE;
    return coded + blocks + 1;
}

/* The name field is fixed width and the length byte says how much of it is
 * real. The reference copies those bytes out verbatim; the character test
 * here is the addition, because a directory entry is only reached by walking
 * from the previous member's end and a name full of control bytes means the
 * walk has already drifted off the chain. */
static bool xx_tps_clean_name(const uint8_t *raw, size_t length,
                              char *out /* [XX_TPS_MAX_NAME + 1] */) {
    size_t index;

    if (length == 0U || length > (size_t)XX_TPS_MAX_NAME) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t value = raw[index];

        if (value < 0x20U || value == 0x7FU) return false;
        /* A directory name here is a bare DOS file name; a separator in it
         * would be a path, which this format does not carry. */
        if (value == (uint8_t)'/' || value == (uint8_t)'\\' ||
            value == (uint8_t)':') {
            return false;
        }
        out[index] = (char)value;
    }
    out[length] = '\0';
    if (out[0] == '.' &&
        (length == 1U || (length == 2U && out[1] == '.'))) {
        return false;
    }
    return true;
}

static xx_tps_stream *xx_tps_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_tps_stream *stream;
    uint8_t magic[XX_TPS_MAGIC_SIZE];
    uint8_t entry[XX_TPS_ENTRY_SIZE];
    char name[XX_TPS_MAX_NAME + 1];
    int64_t total;
    int64_t span;
    int64_t offset;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_TPS_MAGIC_SIZE + XX_TPS_ENTRY_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_tps_read_at(self, self->base_address, magic, sizeof(magic)) ||
        xx_rt_memcmp(magic, XX_TPS_MAGIC, XX_TPS_MAGIC_SIZE) != 0) {
        return NULL;
    }

    stream = (xx_tps_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_TPS_MAGIC_SIZE;
    while (offset < span) {
        xx_tps_member member;
        int32_t raw;
        int64_t absolute;
        int64_t coded;
        int64_t stored;
        int64_t data_offset;
        uint32_t uncompressed;
        size_t name_length;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_TPS_MAX_MEMBERS) break;

        if (span - offset == 1) {
            uint8_t tail;

            if (!xx_tps_read_at(self, self->base_address + offset, &tail, 1U)) {
                goto fail;
            }
            /* The terminator is optional: a lone byte that is not it is
             * simply left as trailing data rather than failing the archive,
             * which is what the reference does. */
            if (tail == (uint8_t)XX_TPS_END_TAG) ++offset;
            break;
        }
        if (span - offset < XX_TPS_ENTRY_SIZE) break;

        if (!xx_tps_read_at(self, self->base_address + offset, entry,
                            sizeof(entry))) {
            goto fail;
        }
        /* Anything that is not an entry tag ends the chain. It is not an
         * error: the walk has simply reached whatever follows the archive. */
        if (entry[0] != (uint8_t)XX_TPS_ENTRY_TAG) break;
        name_length = entry[1];
        if (name_length < 1U || name_length > (size_t)XX_TPS_MAX_NAME) break;
        if (!xx_tps_clean_name(entry + XX_TPS_OFFSET_NAME, name_length, name)) {
            break;
        }

        /* Signed on purpose: the field is genuinely negative in the wild and
         * the reference takes its magnitude. The cast through uint32_t keeps
         * the conversion implementation-defined-free. */
        raw = (int32_t)xx_tps_le32(entry + XX_TPS_OFFSET_CODED);
        absolute = raw < 0 ? -(int64_t)raw : (int64_t)raw;
        coded = absolute - XX_TPS_SIZE_DWORD;
        uncompressed =
            xx_tps_le32(entry + XX_TPS_OFFSET_UNCOMPRESSED) ^ XX_TPS_SIZE_MASK;
        /* A plaintext length with the top bit set is a de-masking failure,
         * not a two-gigabyte member. */
        if (coded < 0 || (uncompressed & 0x80000000U) != 0U) break;

        stored = xx_tps_stored_size(coded);
        data_offset = offset + XX_TPS_ENTRY_SIZE;
        if (stored < 0 || !xx_tps_range_within(span, data_offset, stored)) {
            break;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_TPS_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = stored;
        member.uncompressed_size = (int64_t)uncompressed;
        member.method = XX_TPS_METHOD_LZHUF;
        /* The directory carries no timestamp. */
        member.timestamp = 0U;
        member.is_folder = false;
        if (!xx_tps_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        offset = data_offset + stored;
    }

    if (stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;
    stream->archive_size = offset < span ? offset : span;
    return stream;

fail:
    xx_tps_stream_free(stream);
    return NULL;
}

/* The payload is handed to the codec exactly as stored -- XOR mask, per-block
 * check bytes and trailing marker included. The codec owns the framing; the
 * plaintext length from the directory is an input to it, because it seeds the
 * first block's checksum. */
static bool xx_tps_decode(Abstractformat *self, const xx_tps_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;
    bool ok;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 2 ||
        member->compressed_size > XX_TPS_MAX_MEMBER_SIZE) {
        return false;
    }
    if (member->uncompressed_size < 0 ||
        member->uncompressed_size > XX_TPS_MAX_MEMBER_SIZE) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_tps_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!output) {
        xx_mem_free(input);
        return false;
    }

    ok = xx_tps_decode_memory(input, (size_t)member->compressed_size, output,
                              (size_t)member->uncompressed_size, &written);
    xx_mem_free(input);
    if (!ok || written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_tps_init(xx_tps *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_TPS_FILE_TYPE_ID;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-tps");
    xx_format_set_extension(&archive->format, "tps");
    archive->format.check_is_valid = xx_tps_check_is_valid;
    archive->format.handle_base_info = xx_tps_handle_base_info;
    archive->format.get_format_size = xx_tps_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_tps_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_tps_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_tps_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_tps_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_tps_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_tps_free_archive_records_reading;
    archive->format.destroy = xx_tps_vtable_destroy;
}

xx_tps *xx_tps_create(xx_io_device *device, int64_t base_address) {
    xx_tps *archive = (xx_tps *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_tps_init(archive, device, base_address);
    return archive;
}

void xx_tps_destroy(xx_tps *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_tps_free(xx_tps *archive) {
    if (!archive) return;
    xx_tps_destroy(archive);
    xx_mem_free(archive);
}

static void xx_tps_vtable_destroy(Abstractformat *self) {
    xx_tps_destroy((xx_tps *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_tps_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_tps_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_tps_parse(self, pd);
    if (!stream) return false;
    xx_tps_stream_free(stream);
    return true;
}

bool xx_tps_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_tps *archive = (xx_tps *)self;
    xx_tps_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_tps_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_tps_stream_free(stream);
    return true;
}

int64_t xx_tps_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_tps_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_tps *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_tps_set_record(xx_archive_record *record,
                              const xx_tps_member *member) {
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

static bool xx_tps_copy_options(xx_list_s *target, const xx_list_s *options) {
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

static const xx_var *xx_tps_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_tps_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tps_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_tps_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_tps_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_tps_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_tps_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_tps_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_tps_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_tps_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_tps_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_tps_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_tps_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_tps_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_tps_stream *stream;
    const xx_tps_member *member;
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
    stream = (xx_tps_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_tps_path_safe(member->name)) return false;

    path_option = xx_tps_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_tps_decode(self, member, &plain, &plain_size, pd);
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

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_tps_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
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
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_tps_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
