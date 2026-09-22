/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/tar_xz/xx_tar_xz.h"

#include "../tar_common/xx_tar_common.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/formats/xz/xx_xz.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>

static void xx_tar_xz_vtable_destroy(Abstractformat *self);

#define XX_TAR_XZ_STREAM_HEADER_SIZE 12U
#define XX_TAR_XZ_BLOCK_HEADER_SIZE 12U
#define XX_TAR_XZ_STREAM_FOOTER_SIZE 12U
#define XX_TAR_XZ_CHECK_SIZE 4U

static void xx_tar_xz_put_u32le(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static bool xx_tar_xz_write_exact(xx_io_device *device, const void *data,
                                  size_t size, xx_pd_struct *pd) {
    const uint8_t *bytes = (const uint8_t *)data;
    size_t written = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (written < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, bytes + written, size - written);
        if (amount <= 0 || (size_t)amount > size - written) return false;
        written += (size_t)amount;
    }
    return true;
}

static bool xx_tar_xz_append_vli(uint8_t *buffer, size_t capacity,
                                 size_t *size, uint64_t value) {
    if (!buffer || !size || value > UINT64_C(0x7fffffffffffffff)) {
        return false;
    }
    do {
        uint8_t byte;
        if (*size >= capacity) return false;
        byte = (uint8_t)(value & UINT64_C(0x7f));
        value >>= 7U;
        if (value != 0U) byte |= 0x80U;
        buffer[(*size)++] = byte;
    } while (value != 0U);
    return true;
}

static int xx_tar_xz_compression_level(const xx_list_s *options) {
    int64_t selected = XX_LZMA_LEVEL_DEFAULT;
    size_t index;
    if (options) {
        for (index = 0U; index < options->count; ++index) {
            const xx_meta *option = (const xx_meta *)xx_list_at(
                (const xx_list_t *)options, index);
            if (option && option->meta_id == XX_META_ID_COMPRESSION_LEVEL) {
                selected = xx_var_get_i64(&option->var);
                break;
            }
        }
    }
    if (selected < XX_LZMA_LEVEL_FASTEST) return XX_LZMA_LEVEL_FASTEST;
    if (selected > XX_LZMA_LEVEL_BEST) return XX_LZMA_LEVEL_BEST;
    return (int)selected;
}

static bool xx_tar_xz_encode(Abstractformat *outer,
                             const xx_list_s *options,
                             xx_io_device *tar_source, int64_t tar_size,
                             int64_t *compressed_size,
                             xx_pd_struct *pd) {
    static const uint8_t stream_magic[6] = {
        0xfdU, 0x37U, 0x7aU, 0x58U, 0x5aU, 0x00U
    };
    static const uint8_t zeroes[4] = {0U, 0U, 0U, 0U};
    uint8_t stream_header[XX_TAR_XZ_STREAM_HEADER_SIZE] = {0};
    uint8_t block_header[XX_TAR_XZ_BLOCK_HEADER_SIZE] = {0};
    uint8_t footer[XX_TAR_XZ_STREAM_FOOTER_SIZE] = {0};
    uint8_t index[32] = {0};
    uint8_t lzma2_property = 0U;
    uint32_t tar_crc32 = 0U;
    uint64_t unpadded_size;
    size_t index_size = 0U;
    size_t block_padding;
    int64_t actual_tar_size = 0;
    int64_t lzma2_size = 0;
    int64_t total_size;
    int64_t payload_offset;
    int64_t stream_end;
    int level;

    if (compressed_size) *compressed_size = -1;
    if (!outer || !outer->device || !outer->device->write ||
        !outer->device->seek || !tar_source || !compressed_size ||
        tar_size <= 0 || outer->base_address < 0 ||
        outer->base_address > (int64_t)LONG_MAX -
                                  (int64_t)(XX_TAR_XZ_STREAM_HEADER_SIZE +
                                            XX_TAR_XZ_BLOCK_HEADER_SIZE) ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    payload_offset = outer->base_address +
                     (int64_t)XX_TAR_XZ_STREAM_HEADER_SIZE +
                     (int64_t)XX_TAR_XZ_BLOCK_HEADER_SIZE;
    if (xx_io_seek(outer->device, (long)outer->base_address, SEEK_SET) != 0) {
        return false;
    }

    xx_mem_copy(stream_header, stream_magic, sizeof(stream_magic));
    stream_header[6] = 0U;
    stream_header[7] = 1U; /* CRC32 integrity check. */
    xx_tar_xz_put_u32le(stream_header + 8U,
                        xx_crc32_calc(0U, stream_header + 6U, 2U));
    if (!xx_tar_xz_write_exact(outer->device, stream_header,
                               sizeof(stream_header), pd) ||
        !xx_tar_xz_write_exact(outer->device, block_header,
                               sizeof(block_header), pd)) {
        return false;
    }

    level = xx_tar_xz_compression_level(options);
    if (!xx_lzma2_pack_source(tar_source, NULL, &actual_tar_size,
                              &lzma2_size, &tar_crc32, outer->device,
                              level, &lzma2_property, pd) ||
        actual_tar_size != tar_size || lzma2_size <= 0 ||
        lzma2_size > INT64_MAX -
                         (int64_t)(XX_TAR_XZ_BLOCK_HEADER_SIZE +
                                   XX_TAR_XZ_CHECK_SIZE)) {
        return false;
    }

    unpadded_size = (uint64_t)XX_TAR_XZ_BLOCK_HEADER_SIZE +
                    (uint64_t)lzma2_size +
                    (uint64_t)XX_TAR_XZ_CHECK_SIZE;
    block_padding = (size_t)((4U - (unpadded_size & 3U)) & 3U);
    if (block_padding != 0U &&
        !xx_tar_xz_write_exact(outer->device, zeroes, block_padding, pd)) {
        return false;
    }
    xx_tar_xz_put_u32le(footer, tar_crc32);
    if (!xx_tar_xz_write_exact(outer->device, footer,
                               XX_TAR_XZ_CHECK_SIZE, pd)) {
        return false;
    }

    index[index_size++] = 0U; /* Index Indicator. */
    if (!xx_tar_xz_append_vli(index, sizeof(index), &index_size, 1U) ||
        !xx_tar_xz_append_vli(index, sizeof(index), &index_size,
                              unpadded_size) ||
        !xx_tar_xz_append_vli(index, sizeof(index), &index_size,
                              (uint64_t)tar_size)) {
        return false;
    }
    while ((index_size & 3U) != 0U) index[index_size++] = 0U;
    if (index_size > sizeof(index) - 4U) return false;
    xx_tar_xz_put_u32le(index + index_size,
                        xx_crc32_calc(0U, index, index_size));
    index_size += 4U;
    if (!xx_tar_xz_write_exact(outer->device, index, index_size, pd)) {
        return false;
    }

    xx_tar_xz_put_u32le(footer + 4U,
                        (uint32_t)(index_size / 4U - 1U));
    footer[8] = 0U;
    footer[9] = 1U;
    footer[10] = 0x59U;
    footer[11] = 0x5aU;
    xx_tar_xz_put_u32le(footer,
                        xx_crc32_calc(0U, footer + 4U, 6U));
    if (!xx_tar_xz_write_exact(outer->device, footer, sizeof(footer), pd)) {
        return false;
    }

    if (lzma2_size > INT64_MAX -
                         (int64_t)(XX_TAR_XZ_STREAM_HEADER_SIZE +
                                   XX_TAR_XZ_BLOCK_HEADER_SIZE +
                                   XX_TAR_XZ_CHECK_SIZE +
                                   XX_TAR_XZ_STREAM_FOOTER_SIZE +
                                   sizeof(index) + sizeof(zeroes))) {
        return false;
    }
    total_size = (int64_t)XX_TAR_XZ_STREAM_HEADER_SIZE +
                 (int64_t)XX_TAR_XZ_BLOCK_HEADER_SIZE + lzma2_size +
                 (int64_t)block_padding +
                 (int64_t)XX_TAR_XZ_CHECK_SIZE + (int64_t)index_size +
                 (int64_t)XX_TAR_XZ_STREAM_FOOTER_SIZE;
    if (total_size > INT64_MAX - outer->base_address ||
        outer->base_address + total_size > (int64_t)LONG_MAX) {
        return false;
    }
    stream_end = outer->base_address + total_size;

    block_header[0] = 2U;    /* (2 + 1) * 4 = 12 bytes. */
    block_header[1] = 0U;    /* One filter; sizes are stored in the Index. */
    block_header[2] = 0x21U; /* LZMA2 filter ID. */
    block_header[3] = 1U;    /* One filter-property byte. */
    block_header[4] = lzma2_property;
    xx_tar_xz_put_u32le(block_header + 8U,
                        xx_crc32_calc(0U, block_header, 8U));
    if (xx_io_seek(outer->device,
                   (long)(payload_offset -
                          (int64_t)XX_TAR_XZ_BLOCK_HEADER_SIZE),
                   SEEK_SET) != 0 ||
        !xx_tar_xz_write_exact(outer->device, block_header,
                               sizeof(block_header), pd) ||
        xx_io_seek(outer->device, (long)stream_end, SEEK_SET) != 0) {
        return false;
    }

    *compressed_size = total_size;
    ((xx_tar_xz *)outer)->uncompressed_size = (uint64_t)tar_size;
    ((xx_tar_xz *)outer)->compressed_size = total_size;
    return true;
}

static xx_tar_common *xx_tar_xz_common(xx_tar_xz *tar_xz, bool create) {
    xx_tar_common *common;
    if (!tar_xz) return NULL;
    common = (xx_tar_common *)tar_xz->internal;
    if (!common && create) {
        common = (xx_tar_common *)xx_mem_alloc(sizeof(*common));
        if (!common) return NULL;
        xx_tar_common_init(common);
        tar_xz->internal = common;
    }
    return common;
}

static void xx_tar_xz_sync(xx_tar_xz *tar_xz,
                           const xx_tar_common *common) {
    if (!tar_xz || !common || !common->valid || !common->tar) return;
    tar_xz->number_of_records = xx_tar_get_number_of_records(common->tar);
    tar_xz->number_of_members = xx_tar_get_number_of_members(common->tar);
    tar_xz->uncompressed_size = (uint64_t)common->decoded_size;
    tar_xz->compressed_size = common->compressed_size;
}

static bool xx_tar_xz_decode(Abstractformat *outer,
                             xx_io_device *destination,
                             int64_t *compressed_size,
                             xx_pd_struct *pd) {
    xx_xz xz;
    int64_t extent;
    bool result = false;
    if (!outer || !outer->device || !destination || !compressed_size ||
        outer->base_address < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_xz_init(&xz, outer->device, outer->base_address);
    if (!xx_xz_handle_base_info(&xz.format, pd) ||
        !xx_xz_can_extract(&xz)) {
        goto cleanup;
    }
    extent = xx_xz_get_format_size(&xz.format, pd);
    if (extent <= 0 || xz.uncompressed_size == 0U ||
        xz.uncompressed_size > (uint64_t)SIZE_MAX ||
        !xx_xz_unpack_to_device(&xz, destination, pd)) {
        goto cleanup;
    }
    *compressed_size = extent;
    result = true;

cleanup:
    xx_xz_destroy(&xz);
    return result;
}

void xx_tar_xz_init(xx_tar_xz *tar_xz, xx_io_device *dev,
                    int64_t base_address) {
    if (!tar_xz) return;
    xx_mem_zero(tar_xz, sizeof(*tar_xz));
    xx_format_init(&tar_xz->format, dev, base_address);
    tar_xz->format.endian = XX_ENDIAN_LITTLE;
    tar_xz->format.file_type = XX_FILE_TYPE_TAR_XZ;
    tar_xz->format.format_type = XX_TYPE_ARCHIVE;
    tar_xz->format.is_archive = true;
    xx_format_set_mime_type(&tar_xz->format,
                            "application/x-xz-compressed-tar");
    xx_format_set_extension(&tar_xz->format, "tar.xz");

    tar_xz->format.check_is_valid = xx_tar_xz_check_is_valid;
    tar_xz->format.handle_base_info = xx_tar_xz_handle_base_info;
    tar_xz->format.get_format_size = xx_tar_xz_get_format_size;
    tar_xz->format.get_number_of_archive_records =
        xx_tar_xz_get_number_of_archive_records;
    tar_xz->format.create_archive_records_reading =
        xx_tar_xz_create_archive_records_reading;
    tar_xz->format.get_current_archive_record =
        xx_tar_xz_get_current_archive_record;
    tar_xz->format.unpack_current_archive_record =
        xx_tar_xz_unpack_current_archive_record;
    tar_xz->format.archive_record_move_to_next =
        xx_tar_xz_archive_record_move_to_next;
    tar_xz->format.free_archive_records_reading =
        xx_tar_xz_free_archive_records_reading;
    tar_xz->format.create_archive_records_writing =
        xx_tar_xz_create_archive_records_writing;
    tar_xz->format.pack_archive_record = xx_tar_xz_pack_archive_record;
    tar_xz->format.finalize_archive_records_writing =
        xx_tar_xz_finalize_archive_records_writing;
    tar_xz->format.free_archive_records_writing =
        xx_tar_xz_free_archive_records_writing;
    tar_xz->format.data_struct_id_to_string =
        xx_tar_xz_data_struct_id_to_string;
    tar_xz->format.data_struct_string_to_id =
        xx_tar_xz_data_struct_string_to_id;
    tar_xz->format.create_data_structs_reading =
        xx_tar_xz_create_data_structs_reading;
    tar_xz->format.get_current_data_struct =
        xx_tar_xz_get_current_data_struct;
    tar_xz->format.data_struct_move_to_next =
        xx_tar_xz_data_struct_move_to_next;
    tar_xz->format.free_data_structs_reading =
        xx_tar_xz_free_data_structs_reading;
    tar_xz->format.create_data_struct_records_reading =
        xx_tar_xz_create_data_struct_records_reading;
    tar_xz->format.get_current_data_struct_record =
        xx_tar_xz_get_current_data_struct_record;
    tar_xz->format.data_struct_record_move_to_next =
        xx_tar_xz_data_struct_record_move_to_next;
    tar_xz->format.free_data_struct_records_reading =
        xx_tar_xz_free_data_struct_records_reading;
    tar_xz->format.destroy = xx_tar_xz_vtable_destroy;
    tar_xz->compressed_size = -1;
}

xx_tar_xz *xx_tar_xz_create(xx_io_device *dev, int64_t base_address) {
    xx_tar_xz *tar_xz = (xx_tar_xz *)xx_mem_alloc(sizeof(*tar_xz));
    if (tar_xz) xx_tar_xz_init(tar_xz, dev, base_address);
    return tar_xz;
}

void xx_tar_xz_destroy(xx_tar_xz *tar_xz) {
    xx_tar_common *common;
    if (!tar_xz) return;
    common = xx_tar_xz_common(tar_xz, false);
    if (common) {
        xx_tar_common_cleanup(common);
        xx_mem_free(common);
        tar_xz->internal = NULL;
    }
    if (tar_xz->format.close) tar_xz->format.close(&tar_xz->format);
    xx_format_cleanup_extra_parameters(&tar_xz->format);
}

static void xx_tar_xz_vtable_destroy(Abstractformat *self) {
    xx_tar_xz_destroy((xx_tar_xz *)self);
}

void xx_tar_xz_free(xx_tar_xz *tar_xz) {
    if (!tar_xz) return;
    xx_tar_xz_destroy(tar_xz);
    xx_mem_free(tar_xz);
}

bool xx_tar_xz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_xz *tar_xz = (xx_tar_xz *)self;
    xx_tar_common *common = xx_tar_xz_common(tar_xz, true);
    bool result;
    if (!self || !common) return false;
    result = xx_tar_common_load(common, self, xx_tar_xz_decode, pd);
    if (result) xx_tar_xz_sync(tar_xz, common);
    return result;
}

bool xx_tar_xz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_xz *tar_xz = (xx_tar_xz *)self;
    xx_tar_common *common = xx_tar_xz_common(tar_xz, true);
    if (!self || !common ||
        !xx_tar_common_handle_base_info(common, self, xx_tar_xz_decode, pd)) {
        if (self) self->is_valid = false;
        return false;
    }
    xx_tar_xz_sync(tar_xz, common);
    self->file_type = XX_FILE_TYPE_TAR_XZ;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    return true;
}

int64_t xx_tar_xz_get_format_size(Abstractformat *self,
                                  xx_pd_struct *pd) {
    xx_tar_xz *tar_xz = (xx_tar_xz *)self;
    xx_tar_common *common = xx_tar_xz_common(tar_xz, true);
    if (!self || !common) return -1;
    return xx_tar_common_get_format_size(common, self, xx_tar_xz_decode, pd);
}

uint64_t xx_tar_xz_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    xx_tar_xz *tar_xz = (xx_tar_xz *)self;
    xx_tar_common *common = xx_tar_xz_common(tar_xz, true);
    uint64_t result;
    if (!self || !common) return 0U;
    result = xx_tar_common_get_number_of_archive_records(
        common, self, xx_tar_xz_decode, pd);
    if (common->valid) xx_tar_xz_sync(tar_xz, common);
    return result;
}

xx_archive_record_state *xx_tar_xz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, true);
    if (!self || !common) return NULL;
    return xx_tar_common_create_archive_records_reading(
        common, self, xx_tar_xz_decode, options, pd);
}

const xx_archive_record *xx_tar_xz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, false);
    return common
               ? xx_tar_common_get_current_archive_record(common, state)
               : NULL;
}

bool xx_tar_xz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, false);
    return common && xx_tar_common_unpack_current_archive_record(
                         common, state, pd);
}

bool xx_tar_xz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, false);
    return common && xx_tar_common_archive_record_move_to_next(
                         common, state, pd);
}

void xx_tar_xz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, false);
    if (common) xx_tar_common_free_archive_records_reading(common, state);
}

xx_archive_write_state *xx_tar_xz_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tar_xz *tar_xz = (xx_tar_xz *)self;
    xx_archive_write_state *state;
    xx_tar_common *common;
    if (!self) return NULL;
    state = xx_tar_common_create_archive_records_writing(
        self, options, xx_tar_xz_encode, pd);
    if (!state) return NULL;
    common = xx_tar_xz_common(tar_xz, false);
    if (common) xx_tar_common_cleanup(common);
    tar_xz->number_of_records = 0U;
    tar_xz->number_of_members = 0U;
    tar_xz->uncompressed_size = 0U;
    tar_xz->compressed_size = -1;
    return state;
}

bool xx_tar_xz_pack_archive_record(Abstractformat *self,
                                   xx_archive_write_state *state,
                                   const xx_archive_record *record,
                                   xx_io_device *source_dev,
                                   xx_pd_struct *pd) {
    return xx_tar_common_pack_archive_record(self, state, record,
                                             source_dev, pd);
}

bool xx_tar_xz_finalize_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state,
    xx_pd_struct *pd) {
    xx_tar_xz *tar_xz = (xx_tar_xz *)self;
    xx_tar_common *common;
    if (!self || !xx_tar_common_finalize_archive_records_writing(
                     self, state, pd)) {
        return false;
    }
    common = xx_tar_xz_common(tar_xz, false);
    if (common) xx_tar_common_cleanup(common);
    tar_xz->number_of_records = self->number_of_archive_records;
    tar_xz->number_of_members = self->number_of_archive_records;
    tar_xz->compressed_size = self->format_size;
    return true;
}

void xx_tar_xz_free_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state) {
    xx_tar_common_free_archive_records_writing(self, state);
}

const char *xx_tar_xz_data_struct_id_to_string(Abstractformat *self,
                                                uint32_t id) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, true);
    return common
               ? xx_tar_common_data_struct_id_to_string(common, id)
               : "UNKNOWN";
}

uint32_t xx_tar_xz_data_struct_string_to_id(Abstractformat *self,
                                             const char *name) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, true);
    return common
               ? xx_tar_common_data_struct_string_to_id(common, name)
               : XX_TAR_XZ_DS_UNKNOWN;
}

xx_data_struct_state *xx_tar_xz_create_data_structs_reading(
    Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, true);
    if (!self || !common) return NULL;
    return xx_tar_common_create_data_structs_reading(
        common, self, xx_tar_xz_decode, pd);
}

const xx_data_struct *xx_tar_xz_get_current_data_struct(
    Abstractformat *self, xx_data_struct_state *state) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, false);
    return common
               ? xx_tar_common_get_current_data_struct(common, state)
               : NULL;
}

bool xx_tar_xz_data_struct_move_to_next(Abstractformat *self,
                                         xx_data_struct_state *state,
                                         xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, false);
    return common && xx_tar_common_data_struct_move_to_next(
                         common, state, pd);
}

void xx_tar_xz_free_data_structs_reading(Abstractformat *self,
                                          xx_data_struct_state *state) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, false);
    if (common) xx_tar_common_free_data_structs_reading(common, state);
}

xx_data_struct_record_state *
xx_tar_xz_create_data_struct_records_reading(Abstractformat *self,
                                              const xx_data_struct *ds,
                                              xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, false);
    return common
               ? xx_tar_common_create_data_struct_records_reading(
                     common, ds, pd)
               : NULL;
}

const xx_data_struct_record *xx_tar_xz_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, false);
    return common
               ? xx_tar_common_get_current_data_struct_record(common,
                                                               state)
               : NULL;
}

bool xx_tar_xz_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, false);
    return common && xx_tar_common_data_struct_record_move_to_next(
                         common, state, pd);
}

void xx_tar_xz_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state) {
    xx_tar_common *common = xx_tar_xz_common((xx_tar_xz *)self, false);
    if (common) {
        xx_tar_common_free_data_struct_records_reading(common, state);
    }
}

uint64_t xx_tar_xz_get_number_of_records(const xx_tar_xz *tar_xz) {
    return tar_xz ? tar_xz->number_of_records : 0U;
}

uint64_t xx_tar_xz_get_number_of_members(const xx_tar_xz *tar_xz) {
    return tar_xz ? tar_xz->number_of_members : 0U;
}

uint64_t xx_tar_xz_get_uncompressed_size(const xx_tar_xz *tar_xz) {
    return tar_xz ? tar_xz->uncompressed_size : 0U;
}

int64_t xx_tar_xz_get_compressed_size(const xx_tar_xz *tar_xz) {
    return tar_xz ? tar_xz->compressed_size : -1;
}
