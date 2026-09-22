/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/binhdr/xx_binhdr.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is supplied locally until the enumerator
 * lands. Delete this block once XX_FILE_TYPE_BINHDR exists in the enum. */
#ifdef BINHDR
#define XX_BINHDR_FILE_TYPE XX_FILE_TYPE_BINHDR
#else
#define XX_BINHDR_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_BINHDR_HEADER_SIZE 30
#define XX_BINHDR_MAGIC_OFFSET 14
#define XX_BINHDR_PAYLOAD_NAME "binhdr_payload.bin"

typedef struct xx_binhdr_private_s {
    int64_t input_size;
    int64_t payload_offset;
    int64_t payload_size;
    uint32_t build_date;
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t hardware_id;
    char board_id[8];
} xx_binhdr_private;

typedef struct xx_binhdr_archive_stream_s {
    xx_binhdr_private parsed;
    size_t index;
} xx_binhdr_archive_stream;

static void xx_binhdr_vtable_destroy(Abstractformat *self);

/* seek64 rather than seek: a firmware image can sit past the 2 GiB mark that
 * a 32-bit `long` would silently clamp to on Win64. */
static bool xx_binhdr_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_binhdr_range_within(int64_t total_size, int64_t offset,
                                   int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_binhdr_private_cleanup(xx_binhdr_private *parsed) {
    if (!parsed) return;
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->payload_offset = -1;
}

/* The four board-id bytes are a display string, so only printable ASCII is
 * accepted; anything else means the "U2ND" bytes were a coincidence. */
static bool xx_binhdr_board_id_is_plausible(const uint8_t *raw) {
    size_t index;
    for (index = 0U; index < 4U; ++index) {
        if (raw[index] < 0x20U || raw[index] > 0x7EU) return false;
    }
    return true;
}

static bool xx_binhdr_parse(Abstractformat *self, xx_binhdr_private *parsed,
                            xx_pd_struct *pd) {
    uint8_t header[XX_BINHDR_HEADER_SIZE];
    int64_t total_size;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3_low;
    uint32_t reserved3_high;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->payload_offset = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_binhdr_range_within(total_size, self->base_address,
                                XX_BINHDR_HEADER_SIZE) ||
        !xx_binhdr_read_at(self->device, self->base_address, header,
                           sizeof(header))) {
        return false;
    }
    if (xx_rt_memcmp(header + XX_BINHDR_MAGIC_OFFSET, "U2ND", 4U) != 0) {
        return false;
    }
    /* The three reserved fields carry the weight of the validation: a 4-byte
     * magic on its own matches far too readily in firmware blobs. */
    reserved1 = xx_data_get_u32(header, sizeof(header), 4U, false);
    reserved2 = (uint32_t)header[19] | ((uint32_t)header[20] << 8U) |
                ((uint32_t)header[21] << 16U);
    reserved3_low = xx_data_get_u32(header, sizeof(header), 22U, false);
    reserved3_high = xx_data_get_u32(header, sizeof(header), 26U, false);
    if (reserved1 != 0U || reserved2 != 0U || reserved3_low != 0U ||
        reserved3_high != 0U || header[18] > 3U ||
        !xx_binhdr_board_id_is_plausible(header)) {
        return false;
    }
    parsed->input_size = total_size;
    xx_mem_copy(parsed->board_id, header, 4U);
    parsed->board_id[4] = '\0';
    parsed->build_date = xx_data_get_u32(header, sizeof(header), 8U, false);
    parsed->version_major = header[12];
    parsed->version_minor = header[13];
    parsed->hardware_id = header[18];
    parsed->payload_offset = self->base_address + XX_BINHDR_HEADER_SIZE;
    /* No length field exists, so the payload is whatever follows. */
    parsed->payload_size = total_size - parsed->payload_offset;
    return true;
}

static bool xx_binhdr_copy_options(xx_list_s *destination,
                                   const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
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

static const xx_var *xx_binhdr_find_option(const xx_list_s *options,
                                           uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_binhdr_populate_record(xx_archive_record *record,
                                      const xx_binhdr_private *parsed) {
    if (!record || !parsed || parsed->payload_offset < 0) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = parsed->payload_offset - XX_BINHDR_HEADER_SIZE;
    record->header_size = XX_BINHDR_HEADER_SIZE;
    record->data_offset = parsed->payload_offset;
    record->compressed_size = parsed->payload_size;
    return xx_archive_record_set_original_name(record,
                                               XX_BINHDR_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)parsed->payload_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static void xx_binhdr_archive_stream_free(void *pointer) {
    xx_binhdr_archive_stream *stream = (xx_binhdr_archive_stream *)pointer;
    if (!stream) return;
    xx_binhdr_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

void xx_binhdr_init(xx_binhdr *binhdr, xx_io_device *dev,
                    int64_t base_address) {
    if (!binhdr) return;
    xx_mem_zero(binhdr, sizeof(*binhdr));
    xx_format_init(&binhdr->format, dev, base_address);
    binhdr->format.endian = XX_ENDIAN_LITTLE;
    binhdr->format.file_type = XX_BINHDR_FILE_TYPE;
    binhdr->format.format_type = XX_TYPE_ARCHIVE;
    binhdr->format.is_archive = true;
    xx_format_set_mime_type(&binhdr->format, "application/x-binhdr-firmware");
    xx_format_set_extension(&binhdr->format, "bin");
    binhdr->format.check_is_valid = xx_binhdr_check_is_valid;
    binhdr->format.handle_base_info = xx_binhdr_handle_base_info;
    binhdr->format.get_format_size = xx_binhdr_get_format_size;
    binhdr->format.get_number_of_archive_records =
        xx_binhdr_get_number_of_archive_records;
    binhdr->format.create_archive_records_reading =
        xx_binhdr_create_archive_records_reading;
    binhdr->format.get_current_archive_record =
        xx_binhdr_get_current_archive_record;
    binhdr->format.unpack_current_archive_record =
        xx_binhdr_unpack_current_archive_record;
    binhdr->format.archive_record_move_to_next =
        xx_binhdr_archive_record_move_to_next;
    binhdr->format.free_archive_records_reading =
        xx_binhdr_free_archive_records_reading;
    binhdr->format.destroy = xx_binhdr_vtable_destroy;
    binhdr->payload_offset = -1;
}

xx_binhdr *xx_binhdr_create(xx_io_device *dev, int64_t base_address) {
    xx_binhdr *binhdr = (xx_binhdr *)xx_mem_alloc(sizeof(*binhdr));
    if (binhdr) xx_binhdr_init(binhdr, dev, base_address);
    return binhdr;
}

void xx_binhdr_destroy(xx_binhdr *binhdr) {
    if (!binhdr) return;
    if (binhdr->internal) {
        xx_binhdr_private_cleanup((xx_binhdr_private *)binhdr->internal);
        xx_mem_free(binhdr->internal);
        binhdr->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&binhdr->format);
}

static void xx_binhdr_vtable_destroy(Abstractformat *self) {
    xx_binhdr_destroy((xx_binhdr *)self);
}

void xx_binhdr_free(xx_binhdr *binhdr) {
    if (!binhdr) return;
    xx_binhdr_destroy(binhdr);
    xx_mem_free(binhdr);
}

bool xx_binhdr_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_binhdr_private parsed;
    bool result = xx_binhdr_parse(self, &parsed, pd);
    xx_binhdr_private_cleanup(&parsed);
    return result;
}

bool xx_binhdr_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_binhdr_private *parsed;
    xx_binhdr *binhdr = (xx_binhdr *)self;
    if (!self || !binhdr) return false;
    parsed = (xx_binhdr_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_binhdr_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (binhdr->internal) {
        xx_binhdr_private_cleanup((xx_binhdr_private *)binhdr->internal);
        xx_mem_free(binhdr->internal);
    }
    binhdr->internal = parsed;
    binhdr->number_of_records = parsed->payload_size > 0 ? 1U : 0U;
    binhdr->number_of_members = binhdr->number_of_records;
    xx_mem_copy(binhdr->board_id, parsed->board_id, sizeof(binhdr->board_id));
    binhdr->build_date = parsed->build_date;
    binhdr->version_major = parsed->version_major;
    binhdr->version_minor = parsed->version_minor;
    binhdr->hardware_id = parsed->hardware_id;
    binhdr->payload_offset = parsed->payload_offset;
    binhdr->payload_size = parsed->payload_size;
    /* The header declares no payload length, so everything to the end of the
     * device belongs to the format and there is no overlay to report. */
    self->format_size = parsed->input_size - self->base_address;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->number_of_archive_records = binhdr->number_of_records;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_binhdr_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_binhdr_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_binhdr *)self)->number_of_records;
}

xx_archive_record_state *xx_binhdr_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_binhdr_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_binhdr_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_binhdr_copy_options(&state->options, options) ||
        !xx_binhdr_parse(self, &stream->parsed, pd)) {
        xx_binhdr_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_binhdr_archive_stream_free;
    state->total_records = stream->parsed.payload_size > 0 ? 1 : 0;
    if (stream->parsed.payload_size > 0 &&
        xx_binhdr_populate_record(&state->current_record, &stream->parsed)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_binhdr_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_binhdr_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_binhdr_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_binhdr_archive_stream *)state->internal_state;
    /* The format holds exactly one member, so the first advance ends it. */
    ++stream->index;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_binhdr_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    if (!name || !name[0]) return false;
    option = xx_binhdr_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
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
    if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device,
                                                record->data_offset,
                                                record->compressed_size,
                                                destination, pd);
        if (!result) xx_rt_remove(destination);
    } else {
        result = false;
    }
    if (owned_base) xx_str_free(owned_base);
    xx_str_free(destination);
    return result;
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return false;
}

void xx_binhdr_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_binhdr_get_number_of_records(const xx_binhdr *binhdr) {
    return binhdr ? binhdr->number_of_records : 0U;
}
const char *xx_binhdr_get_board_id(const xx_binhdr *binhdr) {
    return binhdr ? binhdr->board_id : NULL;
}
uint32_t xx_binhdr_get_build_date(const xx_binhdr *binhdr) {
    return binhdr ? binhdr->build_date : 0U;
}
uint8_t xx_binhdr_get_version_major(const xx_binhdr *binhdr) {
    return binhdr ? binhdr->version_major : 0U;
}
uint8_t xx_binhdr_get_version_minor(const xx_binhdr *binhdr) {
    return binhdr ? binhdr->version_minor : 0U;
}
uint8_t xx_binhdr_get_hardware_id(const xx_binhdr *binhdr) {
    return binhdr ? binhdr->hardware_id : 0U;
}
const char *xx_binhdr_get_hardware_name(const xx_binhdr *binhdr) {
    if (!binhdr) return NULL;
    switch (binhdr->hardware_id) {
        case 0U: return "4702";
        case 1U: return "4712";
        case 2U: return "4712L";
        case 3U: return "4704";
        default: return NULL;
    }
}
int64_t xx_binhdr_get_payload_offset(const xx_binhdr *binhdr) {
    return binhdr ? binhdr->payload_offset : -1;
}
int64_t xx_binhdr_get_payload_size(const xx_binhdr *binhdr) {
    return binhdr ? binhdr->payload_size : 0;
}
