/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/autel/xx_autel.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_AUTEL exists in the enum. */
#ifdef AUTEL
#define XX_AUTEL_FILE_TYPE XX_FILE_TYPE_AUTEL
#else
#define XX_AUTEL_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Name given to the decoded payload record.  Never taken from the file. */
#define XX_AUTEL_PAYLOAD_NAME "autel.decoded"
/** Streaming buffer for the decode pass; a whole number of table blocks. */
#define XX_AUTEL_STAGING_SIZE 65536U

/*
 * The obfuscation table, transcribed from binwalk src/extractors/autel.rs,
 * which credits it to sector7's public analysis.  Entry i applies to payload
 * byte i within each 256 byte block:
 *
 *     plain[i] = ((cipher[i] + add[i]) ^ xor[i]) & 0xFF
 *
 * It is a fixed table, not a key: nothing here is a secret and nothing here
 * makes a trust decision.
 */
static const uint8_t xx_autel_table_add[XX_AUTEL_BLOCK_SIZE] = {
     54U,  96U,  59U, 191U,  45U,  96U,  27U, 152U,  44U, 118U, 115U, 210U,
     13U,  27U,  20U, 139U,  28U,  17U,  19U, 224U,  20U, 145U,  14U,  12U,
     18U,  17U,  29U, 246U, 115U,  28U, 155U,  12U,  31U,  20U,  27U, 142U,
     96U,  18U, 145U,  23U,  13U,  13U,  23U,  19U,  27U,  83U, 146U, 145U,
     18U,  96U,  13U, 159U,  96U,  20U,  20U,  27U,   9U,  96U,  13U, 159U,
     96U, 142U,  31U, 155U,   7U, 224U,  20U,  27U,  28U,  17U,  19U,  96U,
     76U, 208U,  80U,  78U,  96U,  27U,  24U, 140U,  96U,  17U,  12U, 224U,
     14U,  17U, 151U,  14U,  16U,  96U,  13U, 155U,  20U,  29U,  23U,  24U,
     27U,  10U,  96U, 140U,  14U,  17U,  16U, 144U,  11U,  13U,  96U,  17U,
     12U,  96U,  28U,  27U,  27U,  18U,  96U,  31U,  96U,  13U,  23U, 224U,
     27U, 142U,  27U,  24U,  12U,  96U,  84U,  14U,  27U,  10U, 155U,   9U,
     17U,  56U,  96U,  82U,  13U,  27U,  20U, 139U,  28U, 145U,  19U, 118U,
    115U,  20U, 145U,  14U,  12U, 146U,  17U,  29U,  96U,  28U,  27U, 140U,
     31U, 148U,  27U,  14U,  83U,  18U,  17U,  23U,  13U,  13U, 151U, 147U,
     27U,  96U,  19U, 159U,  14U,  25U,  17U, 142U,  16U,  27U,  14U, 224U,
     17U,  12U, 224U,  28U,  27U,  13U,  11U,  96U,  27U,  30U, 224U, 146U,
     31U,  29U,  96U, 140U,  31U,  24U, 140U,  96U,  27U,  29U,  31U, 154U,
     14U,  27U, 140U,  18U,  23U,  96U,  21U,  14U,  17U,   9U,  12U, 155U,
     18U,  96U,  27U, 148U,  29U,  23U,  24U, 155U,  10U,  96U,  28U,  14U,
     31U,  28U,  18U,  31U,  12U,  13U,  96U,  31U,  96U,  13U,  27U,  18U,
     23U,  26U,  27U, 156U,  96U,  79U, 211U,  76U,  77U,  75U, 206U, 182U,
     96U,  59U, 191U, 173U};

static const uint8_t xx_autel_table_xor[XX_AUTEL_BLOCK_SIZE] = {
    147U, 129U, 193U,   0U, 130U, 144U, 129U,   0U, 180U, 141U, 129U,   0U,
    164U, 133U, 192U,   0U, 166U, 133U, 193U,   0U, 161U,   0U, 193U, 132U,
    161U, 140U, 192U,   0U, 178U, 132U,   0U, 132U, 165U, 136U, 193U,   0U,
    164U, 133U,   0U, 132U, 165U, 148U, 193U, 132U, 178U, 137U,   0U,   0U,
    166U, 148U, 193U,   0U, 166U, 129U, 193U, 132U, 160U, 148U, 192U,   0U,
    180U,   0U, 193U,   0U, 166U,   0U, 192U, 132U, 160U, 149U, 193U, 132U,
    164U,   0U, 192U, 132U, 160U, 144U, 193U,   0U, 178U, 141U, 193U,   0U,
    161U, 141U,   0U, 132U, 165U, 137U, 193U,   0U, 161U, 141U, 192U, 132U,
    178U, 133U, 192U,   0U, 180U, 133U, 192U,   0U, 163U, 141U, 192U, 132U,
    178U, 141U, 192U, 132U, 130U, 141U, 193U, 132U, 181U, 140U, 193U,   0U,
    166U,   0U, 192U, 132U, 183U, 133U, 192U, 132U, 178U, 140U,   0U, 132U,
    160U, 133U, 192U, 132U, 160U, 137U, 193U,   0U, 161U,   0U, 192U, 132U,
    165U, 132U,   0U, 132U, 167U,   0U, 193U, 132U, 176U, 144U, 193U,   0U,
    180U,   0U, 192U, 132U, 160U, 137U, 193U, 132U, 165U, 145U,   0U,   0U,
    178U, 137U, 193U,   0U, 160U, 148U, 193U,   0U, 180U, 136U, 193U,   0U,
    178U, 144U,   0U, 132U, 160U, 141U, 193U, 132U, 165U, 140U,   0U,   0U,
    165U, 129U, 192U,   0U, 161U, 145U,   0U, 132U, 165U, 140U, 192U,   0U,
    161U, 145U,   0U, 132U, 167U, 140U, 129U, 132U, 165U, 137U, 193U,   0U,
    161U, 141U, 192U,   0U, 178U, 133U, 192U,   0U, 180U, 133U, 192U, 132U,
    130U, 129U, 193U, 132U, 180U, 144U, 193U, 132U, 160U, 141U, 193U, 132U,
    181U, 140U, 193U,   0U, 166U, 141U,   0U, 132U, 160U, 133U,   0U,   0U,
    129U, 133U,   0U,   0U};

typedef struct xx_autel_private_s {
    int64_t input_size;
    int64_t data_offset;
    int64_t data_size;
    int64_t archive_end;
    uint32_t raw_data_size;
} xx_autel_private;

typedef struct xx_autel_archive_stream_s {
    xx_autel_private parsed;
    size_t index;
} xx_autel_archive_stream;

static void xx_autel_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: long is 32-bit on Win64 and an Autel
 * header can sit at any offset inside a larger carrier. */
static bool xx_autel_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_autel_add64(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_autel_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

void xx_autel_deobfuscate(void *data, size_t size, uint64_t block_offset) {
    uint8_t *bytes = (uint8_t *)data;
    size_t index;
    size_t position = (size_t)(block_offset % XX_AUTEL_BLOCK_SIZE);
    if (!bytes) return;
    for (index = 0U; index < size; ++index) {
        uint32_t value = (uint32_t)bytes[index] + xx_autel_table_add[position];
        bytes[index] = (uint8_t)((value ^ xx_autel_table_xor[position]) & 0xFFU);
        if (++position == XX_AUTEL_BLOCK_SIZE) position = 0U;
    }
}

static void xx_autel_private_cleanup(xx_autel_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static bool xx_autel_parse(Abstractformat *self, xx_autel_private *parsed,
                           xx_pd_struct *pd) {
    static const char magic[XX_AUTEL_MAGIC_SIZE] = {'E', 'C', 'C', '0',
                                                    '1', '0', '1', '\0'};
    static const char copyright[] = "Copyright Autel";
    uint8_t header[XX_AUTEL_HEADER_SIZE];
    uint32_t header_size;
    size_t index;
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
    if (!xx_autel_range_within(parsed->input_size, self->base_address,
                               (int64_t)XX_AUTEL_HEADER_SIZE) ||
        !xx_autel_read_at(self->device, self->base_address, header,
                          XX_AUTEL_HEADER_SIZE) ||
        xx_rt_memcmp(header, magic, XX_AUTEL_MAGIC_SIZE) != 0) {
        goto fail;
    }
    parsed->raw_data_size = xx_data_get_u32(header, XX_AUTEL_HEADER_SIZE, 8U,
                                            false);
    header_size = xx_data_get_u32(header, XX_AUTEL_HEADER_SIZE, 12U, false);
    /* The header declares its own size and every known image writes 0x20.
     * Any other value would mean a layout this reader has no source for. */
    if (header_size != XX_AUTEL_HEADER_SIZE) goto fail;

    /* The copyright string is the second half of the magic in practice, and
     * checking it is what separates a real header from eight lucky bytes. */
    for (index = 0U; index < sizeof(copyright); ++index) {
        if (header[XX_AUTEL_COPYRIGHT_OFFSET + index] !=
            (uint8_t)copyright[index]) {
            goto fail;
        }
    }

    if (parsed->raw_data_size == 0U) goto fail;
    /*
     * The declared payload length is attacker controlled and is bounded
     * against the device HERE, at parse time.  The decode pass below streams
     * through a fixed staging buffer, so a bogus length could not blow up an
     * allocation, but it could otherwise make the reader claim a span that
     * does not exist.
     */
    if (!xx_autel_add64(self->base_address, XX_AUTEL_HEADER_SIZE,
                        &parsed->data_offset)) {
        goto fail;
    }
    parsed->data_size = (int64_t)parsed->raw_data_size;
    if (!xx_autel_range_within(parsed->input_size, parsed->data_offset,
                               parsed->data_size) ||
        !xx_autel_add64(parsed->data_offset, (uint64_t)parsed->data_size,
                        &parsed->archive_end)) {
        goto fail;
    }
    return true;
fail:
    xx_autel_private_cleanup(parsed);
    return false;
}

/*
 * Stream the payload through the table into @p destination.
 *
 * The transform is position dependent, so the staging size is kept a whole
 * multiple of the 256 byte block; the running offset is passed along anyway
 * so a short read can never desynchronise the table index.
 */
static bool xx_autel_write_decoded(xx_io_device *device, int64_t offset,
                                   int64_t size, const char *destination,
                                   xx_pd_struct *pd) {
    uint8_t staging[XX_AUTEL_STAGING_SIZE];
    xx_io_device *output;
    int64_t produced = 0;
    bool ok = true;
    bool created = false;
    if (!device || !destination || offset < 0 || size < 0) return false;
    if (size != 0 && xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    output = xx_io_file_open(destination, "wb");
    created = output != NULL;
    if (!output) return false;
    while (ok && produced < size) {
        int64_t remaining = size - produced;
        size_t step = (remaining < (int64_t)sizeof(staging))
                          ? (size_t)remaining
                          : sizeof(staging);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        while (done < step) {
            ssize_t got = xx_io_read(device, staging + done, step - done);
            if (got <= 0 || (size_t)got > step - done) {
                ok = false;
                break;
            }
            done += (size_t)got;
        }
        if (!ok) break;
        xx_autel_deobfuscate(staging, step, (uint64_t)produced);
        if (xx_io_write(output, staging, step) != (ssize_t)step) ok = false;
        produced += (int64_t)step;
    }
    xx_io_close(output);
    if (!ok && created) xx_rt_remove(destination);
    return ok;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_autel_copy_options(xx_list_s *destination,
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

static const xx_var *xx_autel_find_option(const xx_list_s *options,
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

static bool xx_autel_populate_record(xx_archive_record *record,
                                     const xx_autel_private *parsed) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->data_offset - (int64_t)XX_AUTEL_HEADER_SIZE;
    record->header_size = (int64_t)XX_AUTEL_HEADER_SIZE;
    record->data_offset = parsed->data_offset;
    record->compressed_size = parsed->data_size;
    /*
     * The transform is a byte-for-byte substitution, so the decoded stream is
     * exactly as long as the stored one and the two sizes agree.  The stored
     * bytes are NOT the decoded bytes though - the record names the
     * obfuscated span, and only the unpack path below produces plaintext.
     */
    return xx_archive_record_set_original_name(record, XX_AUTEL_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)parsed->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)parsed->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_autel_archive_stream_free(void *pointer) {
    xx_autel_archive_stream *stream = (xx_autel_archive_stream *)pointer;
    if (!stream) return;
    xx_autel_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_autel_init(xx_autel *autel, xx_io_device *dev, int64_t base_address) {
    if (!autel) return;
    xx_mem_zero(autel, sizeof(*autel));
    xx_format_init(&autel->format, dev, base_address);
    autel->format.endian = XX_ENDIAN_LITTLE;
    autel->format.file_type = XX_AUTEL_FILE_TYPE;
    autel->format.format_type = XX_TYPE_ARCHIVE;
    autel->format.is_archive = true;
    xx_format_set_mime_type(&autel->format, "application/x-autel-firmware");
    xx_format_set_extension(&autel->format, "bin");
    autel->format.check_is_valid = xx_autel_check_is_valid;
    autel->format.handle_base_info = xx_autel_handle_base_info;
    autel->format.get_format_size = xx_autel_get_format_size;
    autel->format.get_number_of_archive_records =
        xx_autel_get_number_of_archive_records;
    autel->format.create_archive_records_reading =
        xx_autel_create_archive_records_reading;
    autel->format.get_current_archive_record =
        xx_autel_get_current_archive_record;
    autel->format.unpack_current_archive_record =
        xx_autel_unpack_current_archive_record;
    autel->format.archive_record_move_to_next =
        xx_autel_archive_record_move_to_next;
    autel->format.free_archive_records_reading =
        xx_autel_free_archive_records_reading;
    autel->format.destroy = xx_autel_vtable_destroy;
    autel->header_size = XX_AUTEL_HEADER_SIZE;
    autel->archive_end = -1;
}

xx_autel *xx_autel_create(xx_io_device *dev, int64_t base_address) {
    xx_autel *autel = (xx_autel *)xx_mem_alloc(sizeof(*autel));
    if (autel) xx_autel_init(autel, dev, base_address);
    return autel;
}

void xx_autel_destroy(xx_autel *autel) {
    if (!autel) return;
    if (autel->internal) {
        xx_autel_private_cleanup((xx_autel_private *)autel->internal);
        xx_mem_free(autel->internal);
        autel->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&autel->format);
}

static void xx_autel_vtable_destroy(Abstractformat *self) {
    xx_autel_destroy((xx_autel *)self);
}

void xx_autel_free(xx_autel *autel) {
    if (!autel) return;
    xx_autel_destroy(autel);
    xx_mem_free(autel);
}

bool xx_autel_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_autel_private parsed;
    bool result = xx_autel_parse(self, &parsed, pd);
    xx_autel_private_cleanup(&parsed);
    return result;
}

bool xx_autel_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_autel_private *parsed;
    xx_autel *autel = (xx_autel *)self;
    int64_t total_size;
    if (!self || !autel) return false;
    parsed = (xx_autel_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_autel_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (autel->internal) {
        xx_autel_private_cleanup((xx_autel_private *)autel->internal);
        xx_mem_free(autel->internal);
    }
    autel->internal = parsed;
    autel->number_of_records = 1U;
    autel->number_of_members = 1U;
    autel->data_size = parsed->raw_data_size;
    autel->header_size = XX_AUTEL_HEADER_SIZE;
    autel->archive_end = parsed->archive_end;
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

int64_t xx_autel_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_autel_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_autel *)self)->number_of_records;
}

xx_archive_record_state *xx_autel_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_autel_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_autel_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_autel_copy_options(&state->options, options) ||
        !xx_autel_parse(self, &stream->parsed, pd)) {
        xx_autel_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_autel_archive_stream_free;
    state->total_records = 1;
    if (xx_autel_populate_record(&state->current_record, &stream->parsed)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_autel_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_autel_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_autel_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    /* The header describes exactly one payload, so the first move ends it. */
    stream = (xx_autel_archive_stream *)state->internal_state;
    ++stream->index;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_autel_unpack_current_archive_record(Abstractformat *self,
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
    option = xx_autel_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
    /* Unlike a stored payload this cannot be a straight copy: the bytes have
     * to go through the table on the way out. */
    result = xx_autel_write_decoded(self->device, record->data_offset,
                                    record->compressed_size, destination, pd);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_autel_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_autel_get_number_of_records(const xx_autel *autel) {
    return autel ? autel->number_of_records : 0U;
}
uint64_t xx_autel_get_number_of_members(const xx_autel *autel) {
    return autel ? autel->number_of_members : 0U;
}
uint32_t xx_autel_get_data_size(const xx_autel *autel) {
    return autel ? autel->data_size : 0U;
}
int64_t xx_autel_get_archive_end(const xx_autel *autel) {
    return autel ? autel->archive_end : -1;
}
