/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the undocumented "FINEAR" installer transport.  A FINEAR
 * file is one compressed stream and nothing else -- no member table, no stored
 * name; the original name lives in the installer script beside it, which is
 * why the corpus filenames are the truncated "NAME.EX_" style.
 *
 * The layout was recovered from the sample corpus and confirmed against all
 * 179 samples:
 *
 *   0x00  char magic[6]  "FINEAR"
 *   0x06  u8   0xdd 0x88 0xdd  (constant; version/method marker)
 *   0x09  u32  CRC-16/ARC of the decoded stream, zero extended to 32 bits
 *   0x0D  u32  uncompressed size
 *   0x11  ...  payload
 *
 * The payload is not bespoke: it is the classic LZHUF scheme (LZSS with an
 * adaptive Huffman coder), byte for byte compatible with LHA's -lh1-, so the
 * library's existing xx_lzh1_decode_memory decodes it.  Every one of the 179
 * samples decodes to exactly the declared size with the declared CRC-16/ARC,
 * which is the anchor that distinguishes a correct decode from a plausible
 * one.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/finear/xx_finear.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef FINEAR
#define XX_FINEAR_FILE_TYPE XX_FILE_TYPE_FINEAR
#else
#define XX_FINEAR_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define FINEAR_HEADER_SIZE 17U
#define FINEAR_PAYLOAD_NAME "payload"
/* A 17-byte header must not be able to ask for an unbounded allocation. */
#define FINEAR_MAX_UNPACKED ((uint64_t)256U * 1024U * 1024U)
#define FINEAR_MAX_PACKED ((uint64_t)256U * 1024U * 1024U)

static void xx_finear_vtable_destroy(Abstractformat *self);

static uint16_t finear_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t finear_le32(const uint8_t *bytes) {
    return (uint32_t)finear_le16(bytes) |
           ((uint32_t)finear_le16(bytes + 2U) << 16U);
}

static bool finear_read_at(xx_io_device *device, int64_t offset, void *buffer,
                           size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Read and validate the fixed header without touching the payload. */
static bool finear_parse_header(Abstractformat *format, uint32_t *checksum,
                                uint64_t *unpacked, int64_t *packed_size) {
    uint8_t header[FINEAR_HEADER_SIZE];
    int64_t total, size;
    uint32_t stored_checksum, stored_unpacked;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size <= (int64_t)FINEAR_HEADER_SIZE ||
        !finear_read_at(format->device, format->base_address, header,
                        sizeof(header)))
        return false;
    if (xx_rt_memcmp(header, "FINEAR", 6U) != 0 || header[6] != 0xddU ||
        header[7] != 0x88U || header[8] != 0xddU)
        return false;
    stored_checksum = finear_le32(header + 9U);
    stored_unpacked = finear_le32(header + 13U);
    /* The checksum field is a 16-bit CRC written into a 32-bit slot; anything
     * in the high half means this is not the layout we understand. */
    if (stored_checksum > 0xffffU) return false;
    if ((uint64_t)stored_unpacked > FINEAR_MAX_UNPACKED) return false;
    if ((uint64_t)(size - (int64_t)FINEAR_HEADER_SIZE) > FINEAR_MAX_PACKED)
        return false;
    /* LZHUF never expands, so a declared plaintext smaller than the payload
     * would be a contradiction. */
    if ((uint64_t)stored_unpacked <
        (uint64_t)(size - (int64_t)FINEAR_HEADER_SIZE))
        return false;
    if (checksum) *checksum = stored_checksum;
    if (unpacked) *unpacked = stored_unpacked;
    if (packed_size) *packed_size = size - (int64_t)FINEAR_HEADER_SIZE;
    return true;
}

/* Decode the single stream into a caller-owned buffer, verifying the stored
 * CRC-16/ARC.  Fails closed: a stream that does not reproduce the declared
 * size and checksum is not returned at all. */
static bool finear_decode(Abstractformat *format, uint8_t **plain,
                          size_t *plain_size, xx_pd_struct *pd) {
    uint32_t checksum = 0U;
    uint64_t unpacked = 0U;
    int64_t packed_size = 0;
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    if (!plain || !plain_size || (pd && xx_pd_is_stopped(pd))) return false;
    if (!finear_parse_header(format, &checksum, &unpacked, &packed_size))
        return false;
    if (unpacked > SIZE_MAX || (uint64_t)packed_size > SIZE_MAX) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)packed_size);
    output = (uint8_t *)xx_mem_alloc(unpacked != 0U ? (size_t)unpacked : 1U);
    if (!packed || !output ||
        !finear_read_at(format->device,
                        format->base_address + (int64_t)FINEAR_HEADER_SIZE,
                        packed, (size_t)packed_size))
        goto fail;
    if (!xx_lzh1_decode_memory(packed, (size_t)packed_size, output,
                               (size_t)unpacked, &written) ||
        written != (size_t)unpacked ||
        (uint32_t)xx_crc16(XX_CRC_TYPE_CRC16_ARC, output, written) != checksum)
        goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

static bool finear_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *finear_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool finear_populate_record(Abstractformat *self,
                                   xx_archive_record *record) {
    const xx_finear *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size <= (int64_t)FINEAR_HEADER_SIZE)
        return false;
    archive = (const xx_finear *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = (int64_t)FINEAR_HEADER_SIZE;
    record->data_offset = self->base_address + (int64_t)FINEAR_HEADER_SIZE;
    record->compressed_size =
        self->format_size - (int64_t)FINEAR_HEADER_SIZE;
    return xx_archive_record_set_original_name(record, FINEAR_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_finear_init(xx_finear *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FINEAR_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-finear");
    xx_format_set_extension(&archive->format, "finear");
    archive->format.check_is_valid = xx_finear_check_is_valid;
    archive->format.handle_base_info = xx_finear_handle_base_info;
    archive->format.get_format_size = xx_finear_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_finear_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_finear_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_finear_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_finear_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_finear_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_finear_free_archive_records_reading;
    archive->format.destroy = xx_finear_vtable_destroy;
    archive->stream_end = -1;
}

xx_finear *xx_finear_create(xx_io_device *device, int64_t base_address) {
    xx_finear *archive = (xx_finear *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_finear_init(archive, device, base_address);
    return archive;
}

void xx_finear_destroy(xx_finear *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
    archive->checksum = 0U;
    archive->stream_end = -1;
}

static void xx_finear_vtable_destroy(Abstractformat *self) {
    xx_finear_destroy((xx_finear *)self);
}

void xx_finear_free(xx_finear *archive) {
    if (!archive) return;
    xx_finear_destroy(archive);
    xx_mem_free(archive);
}

bool xx_finear_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    if (!finear_decode(self, &plain, &plain_size, pd)) return false;
    xx_mem_free(plain);
    return true;
}

bool xx_finear_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    uint32_t checksum = 0U;
    uint64_t unpacked = 0U;
    int64_t packed_size = 0;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    xx_finear *archive;
    if (!self) return false;
    if (!finear_parse_header(self, &checksum, &unpacked, &packed_size) ||
        !finear_decode(self, &plain, &plain_size, pd)) {
        archive = (xx_finear *)self;
        archive->uncompressed_size = 0U;
        archive->checksum = 0U;
        archive->stream_end = -1;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    xx_mem_free(plain);
    archive = (xx_finear *)self;
    archive->uncompressed_size = unpacked;
    archive->checksum = checksum;
    self->format_size = (int64_t)FINEAR_HEADER_SIZE + packed_size;
    archive->stream_end = self->base_address + self->format_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_FINEAR_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_finear_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_finear_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_finear_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_finear_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_finear_unpack_to_device(xx_finear *archive, xx_io_device *destination,
                                xx_pd_struct *pd) {
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = true;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_finear_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !finear_decode(&archive->format, &plain, &plain_size, pd))
        return false;
    while (written < plain_size) {
        ssize_t amount = xx_io_write(destination, plain + written,
                                     plain_size - written);
        if (amount <= 0 || (size_t)amount > plain_size - written) {
            result = false;
            break;
        }
        written += (size_t)amount;
    }
    xx_mem_free(plain);
    return result;
}

xx_archive_record_state *xx_finear_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_finear_handle_base_info(self, pd)) ||
        !self->is_valid) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!finear_copy_options(&state->options, options) ||
        !finear_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_finear_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_finear_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    (void)pd;
    if (!self || !state || state->format != self) return false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_finear_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path = NULL;
    bool result = false;
    xx_finear *archive = (xx_finear *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    path_value = finear_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) return xx_finear_check_is_valid(self, pd);
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW)
        base_path = xx_var_get_str(path_value);
    else if (path_value->type == XX_VAR_TYPE_WSTRING ||
             path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (base_path) {
        destination_path =
            (base_path[0] && base_path[xx_str_len(base_path) - 1U] != '/' &&
             base_path[xx_str_len(base_path) - 1U] != '\\')
                ? xx_str_concat3(base_path, "/", FINEAR_PAYLOAD_NAME)
                : xx_str_concat(base_path, FINEAR_PAYLOAD_NAME);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path) return false;
    if (xx_store_create_dirs_a(destination_path, false)) {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        result = output && xx_finear_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_finear_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_finear_get_uncompressed_size(const xx_finear *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_finear_get_stream_end(const xx_finear *archive) {
    return archive ? archive->stream_end : -1;
}
