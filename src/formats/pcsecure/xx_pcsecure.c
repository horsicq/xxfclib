/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Central Point PCSECURE (PC Tools 5.x / 6.x / 7.x) protected files. The codec
 * is xx_pcsecure_parse_header() / xx_pcsecure_decode_memory().
 *
 *   header, 68 bytes at offset 0, ENCRYPTED apart from its signature:
 *     0x00  u32 LE   "PCT5" / "PCT6" / "PCT7" / "AfoS"
 *     0x08  u32 LE   flags; bit 0 set: the payload is LZW-compressed
 *     0x0c  u16 LE   DES rounds for the payload, 0..16
 *     0x12  char[4]  stored file extension, dot included
 *     0x18  u32 LE   uncompressed size
 *     0x20  u32 LE   compressed size
 *     0x38  u32 LE   DOS date/time, meaningful for PCT7 only
 *     0x3c  u64      password verifier; non-zero means a user password
 *   then the single member's payload to end of file.
 *
 * This is ENCRYPTION, not compression, but the key is usually not a secret:
 * PCSECURE ships four built-in product keys for files saved without a user
 * password, and a file saved WITH one carries its key in the verifier, wrapped
 * under one more fixed key. The codec does that whole search itself, so this
 * reader only frames the file and asks.
 *
 * When no candidate key opens the header the file is still LISTED -- the
 * signature is in the clear and the payload's extent is known -- but the
 * member is published as encrypted with the payload's own sizes, and
 * extracting it FAILS. It is never handed to a decoder: writing ciphertext out
 * as if it were plaintext is worse than refusing. No key recovery beyond what
 * the codec already does is attempted.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pcsecure/xx_pcsecure.h"

#include "xxfclib/algo/pcsecure/xx_pcsecure.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The alias macro is defined next to the enumerator in xxfc_defs.h, so testing
 * for it picks up the real file type as soon as PCSECURE is registered there.
 * See the port report for the registration this needs. */
#ifdef PCSECURE
#define XX_PCSECURE_FILE_TYPE XX_FILE_TYPE_PCSECURE
#else
#define XX_PCSECURE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_PCSECURE_SIG_PCT5 0x35544350U /* "PCT5" */
#define XX_PCSECURE_SIG_PCT6 0x36544350U /* "PCT6" */
#define XX_PCSECURE_SIG_PCT7 0x37544350U /* "PCT7" */
#define XX_PCSECURE_SIG_AFOS 0x536f6641U /* "AfoS" */

/* xx_pcsecure_parse_header() takes the WHOLE file: it reads only the 68-byte
 * header but uses the total length to bound the header's declared compressed
 * size, so the buffer has to be the real thing. PCSECURE protects individual
 * DOS-era files, so a cap well above anything real keeps that read honest
 * without letting a header field drive an unbounded allocation. */
#define XX_PCSECURE_MAX_INPUT ((int64_t)0x4000000) /* 64 MB */

/* The reference derives the member name from the DEVICE's file name plus the
 * stored extension. xxfclib's I/O devices carry no name, so the fixed stem the
 * reference falls back to when there is no name is used unconditionally. */
#define XX_PCSECURE_NAME_STEM "pcsecure_data"

typedef struct xx_pcsecure_stream_s {
    char *name;
    int64_t input_size;
    int64_t data_offset;
    int64_t data_size;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint64_t timestamp;
    uint32_t signature;
    int32_t rounds;
    bool key_found;
    bool user_password;
    bool compressed;
    bool consumed; /* the single member has been stepped past */
} xx_pcsecure_stream;

static void xx_pcsecure_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint32_t xx_pcsecure_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_pcsecure_known_signature(uint32_t signature) {
    return signature == XX_PCSECURE_SIG_PCT5 ||
           signature == XX_PCSECURE_SIG_PCT6 ||
           signature == XX_PCSECURE_SIG_PCT7 ||
           signature == XX_PCSECURE_SIG_AFOS;
}

static bool xx_pcsecure_read_at(Abstractformat *self, int64_t offset,
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

/* The stored extension already carries its dot. Drop the NUL/space padding and
 * stop at anything that could escape the output directory. */
static char *xx_pcsecure_build_name(const uint8_t *extension) {
    char suffix[5];
    size_t length = 0U;

    if (extension) {
        while (length < 4U) {
            const uint8_t character = extension[length];
            if (character == 0U || character == 0x20U) break;
            if (character < 0x21U || character > 0x7eU) break;
            if (character == '/' || character == '\\' || character == ':') {
                break;
            }
            suffix[length] = (char)character;
            ++length;
        }
    }
    suffix[length] = '\0';
    /* A lone dot is padding, not an extension. */
    if (length == 1U && suffix[0] == '.') suffix[0] = '\0';
    return xx_str_concat(XX_PCSECURE_NAME_STEM, suffix);
}

static void xx_pcsecure_stream_free(void *pointer) {
    xx_pcsecure_stream *stream = (xx_pcsecure_stream *)pointer;

    if (!stream) return;
    xx_str_free(stream->name);
    xx_mem_free(stream);
}

/* --------------------------------------------------------------- parse -- */

static xx_pcsecure_stream *xx_pcsecure_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_pcsecure_stream *stream = NULL;
    xx_pcsecure_info info;
    uint8_t signature_bytes[4];
    uint8_t *data = NULL;
    int64_t total;
    int64_t span;
    uint32_t signature;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Strictly more than the header: a header with no payload is not a file. */
    if (span <= (int64_t)XX_PCSECURE_HEADER_SIZE) return NULL;
    if (span > XX_PCSECURE_MAX_INPUT) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    /* Cheap gate before the whole-file read: the signature is the one field
     * that is not under the encryption. */
    if (!xx_pcsecure_read_at(self, self->base_address, signature_bytes,
                             sizeof(signature_bytes))) {
        return NULL;
    }
    signature = xx_pcsecure_le32(signature_bytes);
    if (!xx_pcsecure_known_signature(signature)) return NULL;

    stream = (xx_pcsecure_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    stream->input_size = span;
    stream->signature = signature;
    stream->data_offset = self->base_address + XX_PCSECURE_HEADER_SIZE;
    stream->data_size = span - (int64_t)XX_PCSECURE_HEADER_SIZE;
    /* Until a key opens the header the only sizes there are belong to the
     * payload itself. */
    stream->compressed_size = stream->data_size;
    stream->uncompressed_size = stream->data_size;

    data = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!data) goto fail;
    if (!xx_pcsecure_read_at(self, self->base_address, data, (size_t)span)) {
        goto fail;
    }
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    xx_mem_zero(&info, sizeof(info));
    if (xx_pcsecure_parse_header(data, (size_t)span, &info)) {
        /* The codec vets the declared sizes against the payload's extent and
         * the round count against 0..16 before it reports success, so these
         * are already bounded. */
        stream->key_found = true;
        stream->rounds = info.rounds;
        stream->user_password = info.user_password;
        stream->compressed = (info.flags & 1U) != 0U;
        stream->compressed_size = (int64_t)info.compressed_size;
        stream->uncompressed_size = (int64_t)info.uncompressed_size;
        /* The DOS date/time field is only meaningful for PCT7. */
        if (signature == XX_PCSECURE_SIG_PCT7) {
            stream->timestamp = (uint64_t)info.dos_time;
        }
        stream->name = xx_pcsecure_build_name(info.name_extension);
    } else {
        /* No key opened the header. Whether that is a real user password or a
         * container variant this reader does not implement, the member is
         * listed and never decoded. user_password cannot be reported here: the
         * codec only publishes it on success, and re-deriving it would mean
         * re-implementing the verifier test. */
        stream->name = xx_pcsecure_build_name(NULL);
    }
    if (!stream->name) goto fail;

    xx_mem_free(data);
    return stream;

fail:
    if (data) xx_mem_free(data);
    xx_pcsecure_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

static bool xx_pcsecure_decode(Abstractformat *self,
                               const xx_pcsecure_stream *stream, uint8_t **out,
                               size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !stream) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* The refusal this format exists to make: no key, no plaintext. */
    if (!stream->key_found) return false;
    if (stream->uncompressed_size <= 0 ||
        stream->uncompressed_size > (int64_t)XX_PCSECURE_MAX_OUTPUT) {
        return false;
    }
    if (stream->input_size <= 0 || stream->input_size > XX_PCSECURE_MAX_INPUT) {
        return false;
    }

    /* xx_pcsecure_decode_memory takes the WHOLE file: it repeats the key
     * search so that the key never has to travel through the container. */
    input = (uint8_t *)xx_mem_alloc((size_t)stream->input_size);
    if (!input) return false;
    if (!xx_pcsecure_read_at(self, self->base_address, input,
                             (size_t)stream->input_size)) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)stream->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (!xx_pcsecure_decode_memory(input, (size_t)stream->input_size, output,
                                   (size_t)stream->uncompressed_size,
                                   &written) ||
        written != (size_t)stream->uncompressed_size) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)stream->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_pcsecure_init(xx_pcsecure *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_PCSECURE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-pcsecure");
    /* PCSECURE keeps the original extension; the protected file has none of
     * its own. */
    xx_format_set_extension(&archive->format, "");
    archive->format.check_is_valid = xx_pcsecure_check_is_valid;
    archive->format.handle_base_info = xx_pcsecure_handle_base_info;
    archive->format.get_format_size = xx_pcsecure_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pcsecure_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pcsecure_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pcsecure_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pcsecure_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pcsecure_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pcsecure_free_archive_records_reading;
    archive->format.destroy = xx_pcsecure_vtable_destroy;
}

xx_pcsecure *xx_pcsecure_create(xx_io_device *device, int64_t base_address) {
    xx_pcsecure *archive = (xx_pcsecure *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_pcsecure_init(archive, device, base_address);
    return archive;
}

void xx_pcsecure_destroy(xx_pcsecure *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_pcsecure_free(xx_pcsecure *archive) {
    if (!archive) return;
    xx_pcsecure_destroy(archive);
    xx_mem_free(archive);
}

static void xx_pcsecure_vtable_destroy(Abstractformat *self) {
    xx_pcsecure_destroy((xx_pcsecure *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_pcsecure_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pcsecure_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    /* A file whose key was not found is still a valid PCSECURE file: the
     * signature and the framing are in the clear. */
    stream = xx_pcsecure_parse(self, pd);
    if (!stream) return false;
    xx_pcsecure_stream_free(stream);
    return true;
}

bool xx_pcsecure_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pcsecure *archive = (xx_pcsecure *)self;
    xx_pcsecure_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_pcsecure_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    /* The payload runs to end of file, so there is never an overlay. */
    self->format_size = stream->input_size;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = 1U;
    archive->number_of_records = 1U;
    archive->signature = stream->signature;
    archive->key_found = stream->key_found;
    archive->user_password = stream->user_password;
    archive->compressed = stream->compressed;
    archive->rounds = stream->rounds;
    xx_pcsecure_stream_free(stream);
    return true;
}

int64_t xx_pcsecure_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_pcsecure_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_pcsecure *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_pcsecure_set_record(xx_archive_record *record,
                                   const xx_pcsecure_stream *stream) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = stream->data_offset - XX_PCSECURE_HEADER_SIZE;
    record->header_size = (int64_t)XX_PCSECURE_HEADER_SIZE;
    record->data_offset = stream->data_offset;
    /* The whole payload is the stream: the cipher runs over every whole 8-byte
     * block and only then is the result cut to the compressed size. */
    record->compressed_size = stream->data_size;
    if (!xx_archive_record_set_original_name(record, stream->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)stream->compressed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)stream->uncompressed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        stream->compressed ? 1U : 0U) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) ||
        /* Encrypted here means "this reader cannot produce the plaintext",
         * which is exactly the case where no key opened the header. */
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         !stream->key_found)) {
        return false;
    }
    if (stream->key_found) {
        if (!xx_archive_record_set_meta_u64(record,
                                            XX_META_ID_ENCRYPTION_METHOD,
                                            (uint64_t)stream->rounds)) {
            return false;
        }
        if (stream->timestamp != 0U &&
            !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                            stream->timestamp)) {
            return false;
        }
    }
    return true;
}

static bool xx_pcsecure_copy_options(xx_list_s *target,
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

static const xx_var *xx_pcsecure_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_pcsecure_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_pcsecure_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_pcsecure_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_pcsecure_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_pcsecure_stream_free;
    state->total_records = 1;
    if (!xx_pcsecure_copy_options(&state->options, options) ||
        !xx_pcsecure_set_record(&state->current_record, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_pcsecure_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pcsecure_archive_record_move_to_next(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_pcsecure_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* There is exactly one member, so the first step is always the last. */
    stream = (xx_pcsecure_stream *)state->internal_state;
    if (stream) stream->consumed = true;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_pcsecure_unpack_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_pcsecure_stream *stream;
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
    stream = (xx_pcsecure_stream *)state->internal_state;
    if (!stream || stream->consumed || !stream->name) return false;
    /* Mirrors the reference: a member whose key was not recovered is listed
     * but never produced. No password option is honoured because the codec
     * accepts none -- it recovers the key from the file or not at all. */
    if (!stream->key_found) return false;

    path_option =
        xx_pcsecure_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = xx_pcsecure_decode(self, stream, &plain, &plain_size, pd);
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
        target_path = xx_str_concat3(base_path, "/", stream->name);
    } else {
        target_path = xx_str_concat(base_path, stream->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_pcsecure_decode(self, stream, &plain, &plain_size, pd)) {
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

void xx_pcsecure_free_archive_records_reading(Abstractformat *self,
                                              xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
