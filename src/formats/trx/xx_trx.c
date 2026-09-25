/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/trx/xx_trx.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_TRX exists in the enum. */
#ifdef TRX
#define XX_TRX_FILE_TYPE XX_FILE_TYPE_TRX
#else
#define XX_TRX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/** Streaming buffer for the CRC pass. */
#define XX_TRX_STAGING_SIZE 65536U

typedef struct xx_trx_partition_s {
    char *name;
    int64_t data_offset;
    int64_t data_size;
    uint32_t raw_offset; /**< The header field, for reporting. */
} xx_trx_partition;

typedef struct xx_trx_private_s {
    xx_trx_partition partitions[XX_TRX_MAX_PARTITIONS];
    size_t count;
    int64_t input_size;
    int64_t archive_end;
    uint32_t image_size;
    uint32_t crc32;
    uint32_t flags;
    uint32_t version;
    uint32_t header_size;
} xx_trx_private;

typedef struct xx_trx_archive_stream_s {
    xx_trx_private parsed;
    size_t index;
} xx_trx_archive_stream;

static void xx_trx_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: a TRX image is bounded by a 32-bit
 * length field but its base address inside a larger carrier is not, and long
 * is 32-bit on Win64. */
static bool xx_trx_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_trx_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_trx_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_trx_private_cleanup(xx_trx_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < XX_TRX_MAX_PARTITIONS; ++index) {
        if (parsed->partitions[index].name) {
            xx_str_free(parsed->partitions[index].name);
        }
    }
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

/* Build "partition<index>" without pulling in snprintf. */
static char *xx_trx_partition_name(size_t index) {
    char buffer[16];
    size_t used = 0U;
    const char prefix[] = "partition";
    size_t position;
    for (position = 0U; position + 1U < sizeof(prefix); ++position) {
        buffer[used++] = prefix[position];
    }
    if (index == 0U) {
        buffer[used++] = '0';
    } else {
        char reversed[8];
        size_t length = 0U;
        while (index != 0U && length < sizeof(reversed)) {
            reversed[length++] = (char)('0' + (index % 10U));
            index /= 10U;
        }
        while (length != 0U) buffer[used++] = reversed[--length];
    }
    buffer[used] = '\0';
    return xx_str_create(buffer);
}

/*
 * The TRX CRC32 is JAMCRC, not the finished ISO-HDLC CRC32: OpenWrt's
 * crc32buf() seeds 0xFFFFFFFF and returns the register with no final XOR.
 * xx_crc32_calc() applies that XOR, so the result is complemented back here.
 * The range starts at flag_version (offset 12), NOT at the start of the
 * header, and runs to base_address + len.
 */
static bool xx_trx_crc_range(xx_io_device *device, int64_t offset, int64_t size,
                             uint32_t *out_crc, xx_pd_struct *pd) {
    uint8_t staging[XX_TRX_STAGING_SIZE];
    uint32_t crc = 0U;
    if (!device || !out_crc || offset < 0 || size < 0) return false;
    if (size != 0 && xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (size > 0) {
        size_t step =
            (size < (int64_t)sizeof(staging)) ? (size_t)size : sizeof(staging);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < step) {
            ssize_t got = xx_io_read(device, staging + done, step - done);
            if (got <= 0 || (size_t)got > step - done) return false;
            done += (size_t)got;
        }
        crc = xx_crc32_calc(crc, staging, step);
        size -= (int64_t)step;
    }
    *out_crc = ~crc;
    return true;
}

static bool xx_trx_parse(Abstractformat *self, xx_trx_private *parsed,
                         xx_pd_struct *pd) {
    uint8_t header[XX_TRX_HEADER_SIZE_V2];
    uint32_t offsets[XX_TRX_MAX_PARTITIONS];
    uint32_t partition_count;
    uint32_t flag_version;
    uint32_t computed = 0U;
    uint32_t previous = 0U;
    size_t index;
    size_t published = 0U;
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
    if (!xx_trx_range_within(parsed->input_size, self->base_address,
                             XX_TRX_HEADER_SIZE_V1) ||
        !xx_trx_read_at(self->device, self->base_address, header,
                        XX_TRX_HEADER_SIZE_V1) ||
        xx_data_get_u32(header, XX_TRX_HEADER_SIZE_V1, 0U, false) !=
            XX_TRX_MAGIC) {
        goto fail;
    }
    parsed->image_size = xx_data_get_u32(header, XX_TRX_HEADER_SIZE_V1, 4U,
                                         false);
    parsed->crc32 = xx_data_get_u32(header, XX_TRX_HEADER_SIZE_V1, 8U, false);
    flag_version = xx_data_get_u32(header, XX_TRX_HEADER_SIZE_V1, 12U, false);
    parsed->flags = flag_version & UINT32_C(0xFFFF);
    parsed->version = flag_version >> 16U;

    /* Only the two published header layouts are accepted.  A v2 header is
     * four bytes longer and carries a fourth partition offset; anything else
     * would leave the offset table at an unknown size. */
    if (parsed->version == 1U) {
        parsed->header_size = XX_TRX_HEADER_SIZE_V1;
        partition_count = 3U;
    } else if (parsed->version == 2U) {
        parsed->header_size = XX_TRX_HEADER_SIZE_V2;
        partition_count = 4U;
        if (!xx_trx_range_within(parsed->input_size, self->base_address,
                                 XX_TRX_HEADER_SIZE_V2) ||
            !xx_trx_read_at(self->device, self->base_address, header,
                            XX_TRX_HEADER_SIZE_V2)) {
            goto fail;
        }
    } else {
        goto fail;
    }

    /* len counts the header too, so an image cannot be shorter than one, and
     * it has to be physically present on the device before the CRC pass is
     * allowed to stream it. */
    if (parsed->image_size < parsed->header_size ||
        !xx_trx_add(self->base_address, parsed->image_size,
                    &parsed->archive_end) ||
        parsed->archive_end > parsed->input_size) {
        goto fail;
    }

    /* The CRC is not advisory - a router bootloader refuses an image whose
     * CRC does not match - so a mismatch is a parse failure here too. */
    if (!xx_trx_crc_range(self->device, self->base_address + 12,
                          (int64_t)parsed->image_size - 12, &computed, pd) ||
        computed != parsed->crc32) {
        goto fail;
    }

    for (index = 0U; index < partition_count; ++index) {
        offsets[index] = xx_data_get_u32(header, parsed->header_size,
                                         16U + 4U * index, false);
    }
    for (index = partition_count; index < XX_TRX_MAX_PARTITIONS; ++index) {
        offsets[index] = 0U;
    }

    /*
     * A zero offset means the partition is absent.  Present offsets must sit
     * inside the image, at or after the header, and must not run backwards:
     * a partition ends where the next present one begins, so an unordered
     * table would describe a negative length.
     */
    for (index = 0U; index < partition_count; ++index) {
        if (offsets[index] == 0U) continue;
        if (offsets[index] < parsed->header_size ||
            offsets[index] > parsed->image_size || offsets[index] < previous) {
            goto fail;
        }
        previous = offsets[index];
    }

    for (index = 0U; index < partition_count; ++index) {
        uint32_t end = parsed->image_size;
        size_t probe;
        xx_trx_partition *partition;
        if (offsets[index] == 0U) continue;
        for (probe = index + 1U; probe < partition_count; ++probe) {
            if (offsets[probe] != 0U) {
                end = offsets[probe];
                break;
            }
        }
        partition = &parsed->partitions[published];
        partition->raw_offset = offsets[index];
        if (!xx_trx_add(self->base_address, offsets[index],
                        &partition->data_offset)) {
            goto fail;
        }
        partition->data_size = (int64_t)end - (int64_t)offsets[index];
        if (!xx_trx_range_within(parsed->input_size, partition->data_offset,
                                 partition->data_size)) {
            goto fail;
        }
        /* The name is generated here, never taken from the file, so it needs
         * no sanitising before it is used as a destination path component. */
        partition->name = xx_trx_partition_name(published);
        if (!partition->name) goto fail;
        ++published;
    }
    parsed->count = published;
    /* A header whose whole offset table is zero describes no payload at all
     * and would publish nothing; no producer emits one. */
    if (published == 0U) goto fail;
    return true;
fail:
    xx_trx_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_trx_copy_options(xx_list_s *destination,
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

static const xx_var *xx_trx_find_option(const xx_list_s *options,
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

static bool xx_trx_populate_record(xx_archive_record *record,
                                   const xx_trx_private *parsed,
                                   const xx_trx_partition *partition) {
    if (!record || !parsed || !partition || !partition->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->archive_end - parsed->image_size;
    record->header_size = (int64_t)parsed->header_size;
    record->data_offset = partition->data_offset;
    record->compressed_size = partition->data_size;
    /* Partitions are stored, so compressed and uncompressed sizes agree and
     * the compression method is "none". */
    return xx_archive_record_set_original_name(record, partition->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)partition->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)partition->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void xx_trx_archive_stream_free(void *pointer) {
    xx_trx_archive_stream *stream = (xx_trx_archive_stream *)pointer;
    if (!stream) return;
    xx_trx_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_trx_init(xx_trx *trx, xx_io_device *dev, int64_t base_address) {
    if (!trx) return;
    xx_mem_zero(trx, sizeof(*trx));
    xx_format_init(&trx->format, dev, base_address);
    trx->format.endian = XX_ENDIAN_LITTLE;
    trx->format.file_type = XX_TRX_FILE_TYPE;
    trx->format.format_type = XX_TYPE_ARCHIVE;
    trx->format.is_archive = true;
    xx_format_set_mime_type(&trx->format, "application/x-trx-firmware");
    xx_format_set_extension(&trx->format, "trx");
    trx->format.check_is_valid = xx_trx_check_is_valid;
    trx->format.handle_base_info = xx_trx_handle_base_info;
    trx->format.get_format_size = xx_trx_get_format_size;
    trx->format.get_number_of_archive_records =
        xx_trx_get_number_of_archive_records;
    trx->format.create_archive_records_reading =
        xx_trx_create_archive_records_reading;
    trx->format.get_current_archive_record = xx_trx_get_current_archive_record;
    trx->format.unpack_current_archive_record =
        xx_trx_unpack_current_archive_record;
    trx->format.archive_record_move_to_next = xx_trx_archive_record_move_to_next;
    trx->format.free_archive_records_reading =
        xx_trx_free_archive_records_reading;
    trx->format.destroy = xx_trx_vtable_destroy;
    trx->archive_end = -1;
}

xx_trx *xx_trx_create(xx_io_device *dev, int64_t base_address) {
    xx_trx *trx = (xx_trx *)xx_mem_alloc(sizeof(*trx));
    if (trx) xx_trx_init(trx, dev, base_address);
    return trx;
}

void xx_trx_destroy(xx_trx *trx) {
    if (!trx) return;
    if (trx->internal) {
        xx_trx_private_cleanup((xx_trx_private *)trx->internal);
        xx_mem_free(trx->internal);
        trx->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&trx->format);
}

static void xx_trx_vtable_destroy(Abstractformat *self) {
    xx_trx_destroy((xx_trx *)self);
}

void xx_trx_free(xx_trx *trx) {
    if (!trx) return;
    xx_trx_destroy(trx);
    xx_mem_free(trx);
}

bool xx_trx_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_trx_private parsed;
    bool result = xx_trx_parse(self, &parsed, pd);
    xx_trx_private_cleanup(&parsed);
    return result;
}

bool xx_trx_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_trx_private *parsed;
    xx_trx *trx = (xx_trx *)self;
    int64_t total_size;
    if (!self || !trx) return false;
    parsed = (xx_trx_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_trx_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (trx->internal) {
        xx_trx_private_cleanup((xx_trx_private *)trx->internal);
        xx_mem_free(trx->internal);
    }
    trx->internal = parsed;
    trx->number_of_records = parsed->count;
    trx->number_of_members = parsed->count;
    trx->image_size = parsed->image_size;
    trx->crc32 = parsed->crc32;
    trx->flags = parsed->flags;
    trx->version = parsed->version;
    trx->header_size = parsed->header_size;
    trx->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_trx_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_trx_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_trx *)self)->number_of_records;
}

xx_archive_record_state *xx_trx_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_trx_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_trx_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_trx_copy_options(&state->options, options) ||
        !xx_trx_parse(self, &stream->parsed, pd)) {
        xx_trx_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_trx_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_trx_populate_record(&state->current_record, &stream->parsed,
                               &stream->parsed.partitions[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_trx_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_trx_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_trx_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_trx_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_trx_populate_record(&state->current_record, &stream->parsed,
                                &stream->parsed.partitions[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_trx_unpack_current_archive_record(Abstractformat *self,
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
    option = xx_trx_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the partition's span is addressable. */
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

void xx_trx_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_trx_get_number_of_records(const xx_trx *trx) {
    return trx ? trx->number_of_records : 0U;
}
uint64_t xx_trx_get_number_of_members(const xx_trx *trx) {
    return trx ? trx->number_of_members : 0U;
}
uint32_t xx_trx_get_image_size(const xx_trx *trx) {
    return trx ? trx->image_size : 0U;
}
uint32_t xx_trx_get_crc32(const xx_trx *trx) { return trx ? trx->crc32 : 0U; }
uint32_t xx_trx_get_version(const xx_trx *trx) {
    return trx ? trx->version : 0U;
}
uint32_t xx_trx_get_flags(const xx_trx *trx) { return trx ? trx->flags : 0U; }
int64_t xx_trx_get_archive_end(const xx_trx *trx) {
    return trx ? trx->archive_end : -1;
}
