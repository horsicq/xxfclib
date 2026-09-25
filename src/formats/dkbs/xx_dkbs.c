/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dkbs/xx_dkbs.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_DKBS exists in the enum. */
#ifdef DKBS
#define XX_DKBS_FILE_TYPE XX_FILE_TYPE_DKBS
#else
#define XX_DKBS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** The payload record's generated name.  Never taken from the file. */
#define XX_DKBS_PAYLOAD_NAME "payload"

typedef struct xx_dkbs_private_s {
    char board_id[XX_DKBS_STRING_SIZE + 1U];
    char version[XX_DKBS_STRING_SIZE + 1U];
    char boot_device[XX_DKBS_STRING_SIZE + 1U];
    int64_t input_size;
    int64_t data_offset;
    int64_t data_size;
    int64_t archive_end;
    uint32_t raw_data_size;
    bool size_is_big_endian;
} xx_dkbs_private;

typedef struct xx_dkbs_archive_stream_s {
    xx_dkbs_private parsed;
    size_t index;
} xx_dkbs_archive_stream;

static void xx_dkbs_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: a DKBS header can sit at any offset
 * inside a larger flash dump and long is 32-bit on Win64. */
static bool xx_dkbs_read_at(xx_io_device *device, int64_t offset, void *data,
                            size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_dkbs_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_dkbs_range_within(int64_t total_size, int64_t offset,
                                 int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/*
 * Copy a fixed-width, NUL padded header string out of the raw header.
 *
 * binwalk only requires these to be non-empty.  That is thin: three fields of
 * arbitrary bytes will "parse" on any random 0xA0 block that happens to carry
 * the six magic bytes.  The extra requirement here is that every byte up to
 * the terminator be printable ASCII, which is what a board ID, a version and
 * a device node all are in practice and which costs a genuine image nothing.
 */
static bool xx_dkbs_copy_string(const uint8_t *header, size_t header_size,
                                size_t offset, char *out) {
    size_t index;
    if (!header || !out || offset + XX_DKBS_STRING_SIZE > header_size) {
        return false;
    }
    for (index = 0U; index < XX_DKBS_STRING_SIZE; ++index) {
        uint8_t value = header[offset + index];
        if (value == 0U) break;
        if (value < 0x20U || value > 0x7EU) return false;
        out[index] = (char)value;
    }
    out[index] = '\0';
    /* An empty field means the header is not a DKBS header at all. */
    return index != 0U;
}

static void xx_dkbs_private_cleanup(xx_dkbs_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static bool xx_dkbs_parse(Abstractformat *self, xx_dkbs_private *parsed,
                          xx_pd_struct *pd) {
    static const char magic[XX_DKBS_MAGIC_SIZE] = {'_', 'd', 'k', 'b', 's', '_'};
    uint8_t header[XX_DKBS_HEADER_SIZE];
    uint32_t big_value;
    uint32_t little_value;
    int64_t available;
    /* Initialise before the guard clause: callers run the cleanup on their
     * stack copy whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (!xx_dkbs_range_within(parsed->input_size, self->base_address,
                              (int64_t)XX_DKBS_HEADER_SIZE) ||
        !xx_dkbs_read_at(self->device, self->base_address, header,
                         XX_DKBS_HEADER_SIZE)) {
        goto fail;
    }
    /* The literal sits seven bytes in, after a short vendor prefix. */
    if (xx_rt_memcmp(header + XX_DKBS_MAGIC_OFFSET, magic,
                     XX_DKBS_MAGIC_SIZE) != 0) {
        goto fail;
    }
    if (!xx_dkbs_copy_string(header, XX_DKBS_HEADER_SIZE,
                             XX_DKBS_BOARD_ID_OFFSET, parsed->board_id) ||
        !xx_dkbs_copy_string(header, XX_DKBS_HEADER_SIZE,
                             XX_DKBS_VERSION_OFFSET, parsed->version) ||
        !xx_dkbs_copy_string(header, XX_DKBS_HEADER_SIZE,
                             XX_DKBS_BOOT_DEVICE_OFFSET,
                             parsed->boot_device)) {
        goto fail;
    }
    /*
     * The literal is part of the board ID string ("<prefix>_dkbs_<model>"),
     * so the string must run through it.  binwalk does not check this; a
     * NUL inside the seven prefix bytes would leave "_dkbs_" floating in the
     * padding of a short string, which no producer does and which only
     * widens what random data can pass for a header.
     */
    if (xx_str_len(parsed->board_id) <
        (size_t)(XX_DKBS_MAGIC_OFFSET + XX_DKBS_MAGIC_SIZE)) {
        goto fail;
    }

    /*
     * The declared length is attacker controlled, so it is bounded against
     * the device HERE, at parse time, not later at extraction.  A header
     * claiming a gigabyte of payload on a four kilobyte file is rejected
     * before any buffer is sized from it.
     */
    if (!xx_dkbs_add(self->base_address, XX_DKBS_HEADER_SIZE,
                     &parsed->data_offset) ||
        parsed->data_offset > parsed->input_size) {
        goto fail;
    }
    available = parsed->input_size - parsed->data_offset;

    /*
     * The size field has no endianness marker.  binwalk reads it big endian
     * and keeps that reading whenever its top byte is clear (under 16 MiB),
     * falling back to little endian otherwise.  On its own that misreads a
     * little endian payload whose low byte is zero: 1 MiB stored LE is
     * 00 00 10 00, which big endian reads as 4 KiB, fits, and silently
     * splits the image in the wrong place.
     *
     * So the reading that ends the payload exactly at the end of the input
     * wins first -- the shape of every standalone image, and decisive
     * evidence of the field's byte order.  Only when neither reading does is
     * binwalk's rule applied, and a reading that does not fit is never
     * taken.  Where binwalk accepts an image this picks the same reading
     * unless the other one explains the file exactly; it additionally
     * accepts a little endian image whose big endian reading has a clear
     * top byte but overruns the input, which binwalk rejects outright.
     */
    big_value = xx_data_get_u32(header, XX_DKBS_HEADER_SIZE,
                                XX_DKBS_DATA_SIZE_OFFSET, true);
    little_value = xx_data_get_u32(header, XX_DKBS_HEADER_SIZE,
                                   XX_DKBS_DATA_SIZE_OFFSET, false);
    /* A zero length describes no payload; no producer emits one.  Zero
     * reads the same in both byte orders. */
    if (big_value == 0U) goto fail;
    if ((int64_t)big_value == available) {
        parsed->size_is_big_endian = true;
    } else if ((int64_t)little_value == available) {
        parsed->size_is_big_endian = false;
    } else if ((big_value & UINT32_C(0xFF000000)) == 0U &&
               (int64_t)big_value <= available) {
        parsed->size_is_big_endian = true;
    } else if ((int64_t)little_value <= available) {
        parsed->size_is_big_endian = false;
    } else {
        goto fail;
    }
    parsed->raw_data_size =
        parsed->size_is_big_endian ? big_value : little_value;
    parsed->data_size = (int64_t)parsed->raw_data_size;
    if (!xx_dkbs_range_within(parsed->input_size, parsed->data_offset,
                              parsed->data_size) ||
        !xx_dkbs_add(parsed->data_offset, (uint64_t)parsed->data_size,
                     &parsed->archive_end)) {
        goto fail;
    }
    return true;
fail:
    xx_dkbs_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_dkbs_copy_options(xx_list_s *destination,
                                 const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_dkbs_find_option(const xx_list_s *options,
                                         uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_dkbs_populate_record(xx_archive_record *record,
                                    const xx_dkbs_private *parsed) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->data_offset - (int64_t)XX_DKBS_HEADER_SIZE;
    record->header_size = (int64_t)XX_DKBS_HEADER_SIZE;
    record->data_offset = parsed->data_offset;
    record->compressed_size = parsed->data_size;
    /* The payload is stored verbatim, so compressed and uncompressed sizes
     * agree and the compression method is "none". */
    return xx_archive_record_set_original_name(record, XX_DKBS_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)parsed->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)parsed->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_dkbs_archive_stream_free(void *pointer) {
    xx_dkbs_archive_stream *stream = (xx_dkbs_archive_stream *)pointer;
    if (!stream) return;
    xx_dkbs_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_dkbs_init(xx_dkbs *dkbs, xx_io_device *dev, int64_t base_address) {
    if (!dkbs) return;
    xx_mem_zero(dkbs, sizeof(*dkbs));
    xx_format_init(&dkbs->format, dev, base_address);
    /* The identification strings are bytes and the one numeric field is
     * endianness-detected per image.  Big is only the default until
     * handle_base_info replaces it with the byte order the size field was
     * actually read in. */
    dkbs->format.endian = XX_ENDIAN_BIG;
    dkbs->format.file_type = XX_DKBS_FILE_TYPE;
    dkbs->format.format_type = XX_TYPE_ARCHIVE;
    dkbs->format.is_archive = true;
    xx_format_set_mime_type(&dkbs->format, "application/x-dkbs-firmware");
    xx_format_set_extension(&dkbs->format, "bin");
    dkbs->format.check_is_valid = xx_dkbs_check_is_valid;
    dkbs->format.handle_base_info = xx_dkbs_handle_base_info;
    dkbs->format.get_format_size = xx_dkbs_get_format_size;
    dkbs->format.get_number_of_archive_records =
        xx_dkbs_get_number_of_archive_records;
    dkbs->format.create_archive_records_reading =
        xx_dkbs_create_archive_records_reading;
    dkbs->format.get_current_archive_record = xx_dkbs_get_current_archive_record;
    dkbs->format.unpack_current_archive_record =
        xx_dkbs_unpack_current_archive_record;
    dkbs->format.archive_record_move_to_next =
        xx_dkbs_archive_record_move_to_next;
    dkbs->format.free_archive_records_reading =
        xx_dkbs_free_archive_records_reading;
    dkbs->format.destroy = xx_dkbs_vtable_destroy;
    dkbs->header_size = XX_DKBS_HEADER_SIZE;
    dkbs->archive_end = -1;
}

xx_dkbs *xx_dkbs_create(xx_io_device *dev, int64_t base_address) {
    xx_dkbs *dkbs = (xx_dkbs *)xx_mem_alloc(sizeof(*dkbs));
    if (dkbs) xx_dkbs_init(dkbs, dev, base_address);
    return dkbs;
}

void xx_dkbs_destroy(xx_dkbs *dkbs) {
    if (!dkbs) return;
    if (dkbs->internal) {
        xx_dkbs_private_cleanup((xx_dkbs_private *)dkbs->internal);
        xx_mem_free(dkbs->internal);
        dkbs->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&dkbs->format);
}

static void xx_dkbs_vtable_destroy(Abstractformat *self) {
    xx_dkbs_destroy((xx_dkbs *)self);
}

void xx_dkbs_free(xx_dkbs *dkbs) {
    if (!dkbs) return;
    xx_dkbs_destroy(dkbs);
    xx_mem_free(dkbs);
}

bool xx_dkbs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dkbs_private parsed;
    bool result = xx_dkbs_parse(self, &parsed, pd);
    xx_dkbs_private_cleanup(&parsed);
    return result;
}

bool xx_dkbs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dkbs_private *parsed;
    xx_dkbs *dkbs = (xx_dkbs *)self;
    int64_t total_size;
    if (!self || !dkbs) return false;
    parsed = (xx_dkbs_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_dkbs_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (dkbs->internal) {
        xx_dkbs_private_cleanup((xx_dkbs_private *)dkbs->internal);
        xx_mem_free(dkbs->internal);
    }
    dkbs->internal = parsed;
    dkbs->number_of_records = 1U;
    dkbs->number_of_members = 1U;
    dkbs->data_size = parsed->raw_data_size;
    dkbs->header_size = XX_DKBS_HEADER_SIZE;
    dkbs->size_is_big_endian = parsed->size_is_big_endian;
    /* The size field is the header's only numeric field, so the byte order
     * it was read in is the image's endianness. */
    self->endian =
        parsed->size_is_big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    dkbs->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_dkbs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_dkbs_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_dkbs *)self)->number_of_records;
}

xx_archive_record_state *xx_dkbs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_dkbs_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_dkbs_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_dkbs_copy_options(&state->options, options) ||
        !xx_dkbs_parse(self, &stream->parsed, pd)) {
        xx_dkbs_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_dkbs_archive_stream_free;
    state->total_records = 1;
    if (xx_dkbs_populate_record(&state->current_record, &stream->parsed)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_dkbs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dkbs_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_dkbs_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* There is exactly one payload, so the first move always ends the walk. */
    stream = (xx_dkbs_archive_stream *)state->internal_state;
    ++stream->index;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_dkbs_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!name || !name[0]) return false;
    option = xx_dkbs_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the payload's span is addressable. */
        int64_t total = xx_io_total_size(self->device);
        return record->data_offset >= 0 && record->compressed_size >= 0 &&
               record->data_offset <= total &&
               record->compressed_size <= total - record->data_offset;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", name);
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    if (!xx_store_create_dirs_a(destination, false)) goto cleanup;
    result = xx_store_unpack_device_to_file(self->device, record->data_offset,
                                            record->compressed_size,
                                            destination, pd);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_dkbs_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_dkbs_get_number_of_records(const xx_dkbs *dkbs) {
    return dkbs ? dkbs->number_of_records : 0U;
}
uint64_t xx_dkbs_get_number_of_members(const xx_dkbs *dkbs) {
    return dkbs ? dkbs->number_of_members : 0U;
}
uint32_t xx_dkbs_get_data_size(const xx_dkbs *dkbs) {
    return dkbs ? dkbs->data_size : 0U;
}
int64_t xx_dkbs_get_archive_end(const xx_dkbs *dkbs) {
    return dkbs ? dkbs->archive_end : -1;
}
const char *xx_dkbs_get_board_id(const xx_dkbs *dkbs) {
    return (dkbs && dkbs->internal)
               ? ((const xx_dkbs_private *)dkbs->internal)->board_id
               : NULL;
}
const char *xx_dkbs_get_version(const xx_dkbs *dkbs) {
    return (dkbs && dkbs->internal)
               ? ((const xx_dkbs_private *)dkbs->internal)->version
               : NULL;
}
const char *xx_dkbs_get_boot_device(const xx_dkbs *dkbs) {
    return (dkbs && dkbs->internal)
               ? ((const xx_dkbs_private *)dkbs->internal)->boot_device
               : NULL;
}
