/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * 3M Post-it Software Notes "PSNcompress" packed files - the ".xx_" members of
 * that product's installation media (".ps_", ".dl_", ".ex_", ".wa_", ".in_",
 * ".do_", ".hl_", ".pnl").  Neither XArchive nor Deark carries a module for
 * this format; the header below was derived from the 38 samples in
 * F:\ARC\ARC\PSN, all of which agree on it byte for byte.
 *
 *   header, 65 bytes at offset 0, entirely ASCII:
 *     0x00  53  char[53] "PSNcompress-Copyright\xae 3M Company ALL RIGHTS
 *                        RESERVED"
 *     0x35   4  char[4]  format version, "0001" in every sample
 *     0x39   8  char[8]  plaintext length as uppercase hexadecimal, or
 *                        "FFFFFFFF" when the packer did not record it
 *                        (17 of the 38 samples)
 *     0x41   n  bytes    the compressed stream, running to end-of-file
 *
 * The compressed stream is a PKWARE Data Compression Library (DCL) stream,
 * decoded here with xx_dcl_*.  The "00 06" every stream opens with is not a
 * signature but DCL's own two header bytes: literal mode 0 (uncoded literals)
 * and dictionary size 6 (a 4 KiB window).
 *
 * The identification is anchored, not inferred.  All 38 samples decode to a
 * stream that ends exactly at end-of-file, and all 22 that record a plaintext
 * length decode to precisely that many bytes.  The 16 that record none are
 * corroborated independently: several are the same payload packed twice, once
 * with the length recorded and once without (clock.wa_ 6224, sparkle.wa_ 7226,
 * magic.wa_ 14516, siren.wa_ 22834, rolldown.wa_ 6090), and both copies decode
 * to the same size.  Decoded output carries the expected headers - "MZ" for
 * the .ex_ members, an OLE compound-document signature for readme.do_ - and
 * PSNUnIns.ex_ was compared byte for byte against U3's extraction and is
 * identical.
 *
 * Identification rests on the 53-byte banner, which is long and specific
 * enough to stand alone, plus the version and the eight hexadecimal digits:
 * a file whose length field is not hexadecimal is not this format.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/psn/xx_psn.h"

#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef PSN
#define XX_PSN_FILE_TYPE XX_FILE_TYPE_PSN
#else
#define XX_PSN_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_PSN_MAGIC_SIZE 53
#define XX_PSN_VERSION_SIZE 4
#define XX_PSN_LENGTH_SIZE 8
#define XX_PSN_HEADER_SIZE \
    (XX_PSN_MAGIC_SIZE + XX_PSN_VERSION_SIZE + XX_PSN_LENGTH_SIZE)
/* The packer emits at least one stream byte; a header with nothing behind it
 * carries no payload and is not accepted. */
#define XX_PSN_MIN_SIZE (XX_PSN_HEADER_SIZE + 1)
#define XX_PSN_METHOD_PSNCOMPRESS 1U
/* "FFFFFFFF" means the packer did not record the plaintext length. */
#define XX_PSN_LENGTH_UNKNOWN 0xffffffffU
/* Ceilings for a probe. The largest packed stream in the corpus is 766 KiB
 * and the largest plaintext 1.7 MiB; these leave generous room while keeping
 * a 65-byte header from ever asking for an unbounded allocation. */
#define XX_PSN_MAX_PACKED ((uint64_t)256U * 1024U * 1024U)
#define XX_PSN_MAX_PLAIN ((size_t)512U * 1024U * 1024U)
/* The container stores no file name - the installer renames the packed file
 * by replacing the last extension character with '_' - so the single record
 * gets a fixed placeholder. */
#define XX_PSN_PLACEHOLDER_NAME "psn_data"

typedef struct xx_psn_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t size;             /* packed bytes behind the 65-byte header */
    int64_t uncompressed_size; /* -1 when the header does not record it */
} xx_psn_member;

typedef struct xx_psn_stream_s {
    xx_psn_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_psn_stream;

static void xx_psn_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_psn_read_at(Abstractformat *self, int64_t offset,
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

/* Refuse anything that would escape the extraction directory. */
static bool xx_psn_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* The container carries no name; the single record always gets the same
 * placeholder. */
static char *xx_psn_make_name(void) {
    return xx_str_dup(XX_PSN_PLACEHOLDER_NAME);
}

static void xx_psn_stream_free(void *pointer) {
    xx_psn_stream *stream = (xx_psn_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* --------------------------------------------------------------- parse -- */

/* The banner is 53 bytes with one non-ASCII byte, the registered-trademark
 * sign at index 21; spelling it out keeps the file free of source encoding
 * surprises. */
static const uint8_t xx_psn_magic[XX_PSN_MAGIC_SIZE] = {
    'P', 'S', 'N', 'c', 'o', 'm', 'p', 'r', 'e', 's', 's', '-', 'C',
    'o', 'p', 'y', 'r', 'i', 'g', 'h', 't', 0xaeU, ' ', '3', 'M', ' ',
    'C', 'o', 'm', 'p', 'a', 'n', 'y', ' ', 'A', 'L', 'L', ' ', 'R',
    'I', 'G', 'H', 'T', 'S', ' ', 'R', 'E', 'S', 'E', 'R', 'V', 'E',
    'D'};

/* Parse the eight uppercase hexadecimal digits of the length field.  A digit
 * outside [0-9A-F] makes the file invalid rather than defaulting to anything:
 * the field is the only thing the header says about the payload. */
static bool xx_psn_read_hex32(const uint8_t *data, uint32_t *value) {
    uint32_t result = 0U;
    size_t index;

    for (index = 0U; index < XX_PSN_LENGTH_SIZE; ++index) {
        uint8_t c = data[index];
        uint32_t digit;

        if (c >= '0' && c <= '9') {
            digit = (uint32_t)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            digit = (uint32_t)(c - 'A') + 10U;
        } else {
            return false;
        }
        result = (result << 4) | digit;
    }
    *value = result;
    return true;
}

static xx_psn_stream *xx_psn_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_psn_stream *stream = NULL;
    uint8_t head[XX_PSN_HEADER_SIZE];
    int64_t total;
    int64_t span;
    uint32_t declared;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_PSN_MIN_SIZE) return NULL;
    if (!xx_psn_read_at(self, self->base_address, head, sizeof(head))) {
        return NULL;
    }
    if (xx_rt_memcmp(head, xx_psn_magic, XX_PSN_MAGIC_SIZE) != 0) return NULL;
    if (xx_rt_memcmp(head + XX_PSN_MAGIC_SIZE, "0001", XX_PSN_VERSION_SIZE) !=
        0) {
        return NULL;
    }
    if (!xx_psn_read_hex32(head + XX_PSN_MAGIC_SIZE + XX_PSN_VERSION_SIZE,
                           &declared)) {
        return NULL;
    }

    stream = (xx_psn_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_psn_member *)xx_mem_alloc(sizeof(*stream->items));
    if (!stream->items) goto fail;
    xx_mem_zero(stream->items, sizeof(*stream->items));

    stream->items[0].name = xx_psn_make_name();
    if (!stream->items[0].name) goto fail;
    stream->items[0].header_offset = self->base_address;
    stream->items[0].header_size = XX_PSN_HEADER_SIZE;
    stream->items[0].data_offset = self->base_address + XX_PSN_HEADER_SIZE;
    stream->items[0].size = span - XX_PSN_HEADER_SIZE;
    /* An unrecorded length is published as unknown, not as a guess. */
    stream->items[0].uncompressed_size =
        declared == XX_PSN_LENGTH_UNKNOWN ? -1 : (int64_t)declared;
    stream->count = 1U;

    stream->archive_size = span;
    return stream;

fail:
    xx_psn_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

/* PSNcompress wraps a PKWARE Data Compression Library stream: the two bytes
 * behind the header are DCL's literal mode and dictionary-size fields, 00 06
 * in every sample (uncoded literals, 4 KiB window).
 *
 * The stream is measured before it is decoded, which serves two purposes.  It
 * supplies a plaintext length for the sixteen samples whose header records
 * none, and it yields the exact input extent, which must land precisely on
 * end-of-file - a far stronger check than the banner alone.  When the header
 * does record a length the measured one must equal it, so a stream that
 * decodes "plausibly" but to the wrong size is still refused. */
static bool xx_psn_decode(Abstractformat *self, const xx_psn_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t consumed = 0U;
    size_t produced = 0U;
    size_t written = 0U;

    if (!out || !out_size) return false;
    *out = NULL;
    *out_size = 0U;
    if (!self || !member || member->size <= 0 ||
        (uint64_t)member->size > XX_PSN_MAX_PACKED ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc((size_t)member->size);
    if (!packed) return false;
    if (!xx_psn_read_at(self, member->data_offset, packed,
                        (size_t)member->size)) {
        goto cleanup;
    }

    /* Measure first: refuse anything that does not end exactly at the end of
     * the packed extent, or that would decode past the output ceiling. */
    if (!xx_dcl_scan_memory(packed, (size_t)member->size, XX_PSN_MAX_PLAIN,
                            &consumed, &produced) ||
        consumed != (size_t)member->size || produced == 0U) {
        goto cleanup;
    }
    /* A recorded length is an assertion the decode has to satisfy. */
    if (member->uncompressed_size >= 0 &&
        (uint64_t)member->uncompressed_size != (uint64_t)produced) {
        goto cleanup;
    }
    if (pd && xx_pd_is_stopped(pd)) goto cleanup;

    plain = (uint8_t *)xx_mem_alloc(produced);
    if (!plain) goto cleanup;
    if (!xx_dcl_decode_memory(packed, (size_t)member->size, plain, produced,
                              &written) ||
        written != produced) {
        goto cleanup;
    }

    xx_mem_free(packed);
    *out = plain;
    *out_size = produced;
    return true;

cleanup:
    xx_mem_free(plain);
    xx_mem_free(packed);
    return false;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_psn_init(xx_psn *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_PSN_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-psncompress");
    xx_format_set_extension(&archive->format, "ps_");
    archive->format.check_is_valid = xx_psn_check_is_valid;
    archive->format.handle_base_info = xx_psn_handle_base_info;
    archive->format.get_format_size = xx_psn_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_psn_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_psn_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_psn_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_psn_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_psn_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_psn_free_archive_records_reading;
    archive->format.destroy = xx_psn_vtable_destroy;
}

xx_psn *xx_psn_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_psn *archive = (xx_psn *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_psn_init(archive, device, base_address);
    return archive;
}

void xx_psn_destroy(xx_psn *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_psn_free(xx_psn *archive) {
    if (!archive) return;
    xx_psn_destroy(archive);
    xx_mem_free(archive);
}

static void xx_psn_vtable_destroy(Abstractformat *self) {
    xx_psn_destroy((xx_psn *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_psn_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_psn_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_psn_parse(self, pd);
    if (!stream) return false;
    xx_psn_stream_free(stream);
    return true;
}

bool xx_psn_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_psn *archive = (xx_psn *)self;
    xx_psn_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_psn_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_psn_stream_free(stream);
    return true;
}

int64_t xx_psn_get_format_size(Abstractformat *self,
                                      xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_psn_get_number_of_archive_records(Abstractformat *self,
                                                     xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_psn *)self)->number_of_records : 0U;
}


/* ------------------------------------------------------------- records -- */

static bool xx_psn_set_record(xx_archive_record *record,
                                     const xx_psn_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           (member->uncompressed_size < 0 ||
            xx_archive_record_set_meta_u64(
                record, XX_META_ID_UNCOMPRESSED_SIZE,
                (uint64_t)member->uncompressed_size)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          XX_PSN_METHOD_PSNCOMPRESS) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_psn_copy_options(xx_list_s *target,
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

static const xx_var *xx_psn_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_psn_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_psn_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_psn_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_psn_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_psn_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_psn_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_psn_set_record(&state->current_record,
                                   &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_psn_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_psn_archive_record_move_to_next(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_psn_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_psn_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_psn_set_record(&state->current_record,
                                                 &stream->items[stream->index]);
    return state->has_record;
}

bool xx_psn_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_psn_stream *stream;
    const xx_psn_member *member;
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
    stream = (xx_psn_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_psn_path_safe(member->name)) return false;

    path_option = xx_psn_get_option(&state->options,
                                           XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: read and discard, which verifies the member
         * without writing anything. */
        result = xx_psn_decode(self, member, &plain, &plain_size, pd);
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
        !xx_psn_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_psn_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
