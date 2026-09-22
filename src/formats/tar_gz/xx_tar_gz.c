/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/tar_gz/xx_tar_gz.h"

#include "../tar_common/xx_tar_common.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/formats/gz/xx_gz.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>

static void xx_tar_gz_vtable_destroy(Abstractformat *self);

static xx_tar_common *xx_tar_gz_common(xx_tar_gz *tar_gz) {
    xx_tar_common *common;
    if (!tar_gz) return NULL;
    common = (xx_tar_common *)tar_gz->internal;
    if (common) return common;
    common = (xx_tar_common *)xx_mem_alloc(sizeof(*common));
    if (!common) return NULL;
    xx_tar_common_init(common);
    tar_gz->internal = common;
    return common;
}

static void xx_tar_gz_clear_public_state(xx_tar_gz *tar_gz) {
    if (!tar_gz) return;
    tar_gz->number_of_records = 0U;
    tar_gz->number_of_members = 0U;
    tar_gz->compressed_size = -1;
    tar_gz->uncompressed_size = -1;
}

static void xx_tar_gz_sync_public_state(xx_tar_gz *tar_gz,
                                        const xx_tar_common *common) {
    if (!tar_gz || !common || !common->valid || !common->tar) {
        xx_tar_gz_clear_public_state(tar_gz);
        return;
    }
    tar_gz->number_of_records = xx_tar_get_number_of_records(common->tar);
    tar_gz->number_of_members = xx_tar_get_number_of_members(common->tar);
    tar_gz->compressed_size = common->compressed_size;
    tar_gz->uncompressed_size = (uint64_t)common->decoded_size <=
                                        (uint64_t)INT64_MAX
                                    ? (int64_t)common->decoded_size
                                    : -1;
}

static bool xx_tar_gz_decode(Abstractformat *outer,
                             xx_io_device *destination,
                             int64_t *compressed_size,
                             xx_pd_struct *pd) {
    xx_gz gzip;
    bool result;
    if (compressed_size) *compressed_size = -1;
    if (!outer || !outer->device || !destination || !compressed_size ||
        outer->base_address < 0) {
        return false;
    }
    xx_gz_init(&gzip, outer->device, outer->base_address);
    result = xx_gz_handle_base_info(&gzip.format, pd) &&
             xx_gz_unpack_to_device(&gzip, destination, pd);
    if (result) *compressed_size = xx_gz_get_format_size(&gzip.format, pd);
    xx_gz_destroy(&gzip);
    return result && *compressed_size > 0;
}

static const xx_var *xx_tar_gz_find_option(const xx_list_s *options,
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

static int xx_tar_gz_compression_level(const xx_list_s *options) {
    const xx_var *value = xx_tar_gz_find_option(
        options, XX_META_ID_OPT_COMPRESSION_LEVEL);
    int64_t requested;
    if (!value) return XX_DEFLATE_LEVEL_DEFAULT;
    requested = xx_var_get_i64(value);
    if (requested < XX_DEFLATE_LEVEL_STORED)
        return XX_DEFLATE_LEVEL_STORED;
    if (requested > XX_DEFLATE_LEVEL_BEST)
        return XX_DEFLATE_LEVEL_BEST;
    return (int)requested;
}

static bool xx_tar_gz_write_all(xx_io_device *device, const void *data,
                                size_t size) {
    const uint8_t *cursor = (const uint8_t *)data;
    while (size != 0U) {
        ssize_t written = xx_io_write(device, cursor, size);
        if (written <= 0 || (size_t)written > size) return false;
        cursor += (size_t)written;
        size -= (size_t)written;
    }
    return true;
}

static void xx_tar_gz_put_u32le(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static bool xx_tar_gz_encode(Abstractformat *outer,
                             const xx_list_s *options,
                             xx_io_device *tar_source, int64_t tar_size,
                             int64_t *compressed_size, xx_pd_struct *pd) {
    uint8_t header[10] = {
        0x1fU, 0x8bU, 8U, 0U, 0U, 0U, 0U, 0U, 0U, 0xffU
    };
    uint8_t trailer[8];
    int64_t measured_tar_size = 0;
    int64_t deflate_size = 0;
    uint32_t crc32 = 0U;
    int level;
    if (compressed_size) *compressed_size = -1;
    if (!outer || !outer->device || !tar_source || !compressed_size ||
        tar_size <= 0 || outer->base_address < 0 ||
        outer->base_address > LONG_MAX || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    level = xx_tar_gz_compression_level(options);
    if (level == XX_DEFLATE_LEVEL_BEST) header[8] = 2U;
    else if (level == XX_DEFLATE_LEVEL_FASTEST) header[8] = 4U;
    if (xx_io_seek(outer->device, (long)outer->base_address, SEEK_SET) != 0 ||
        !xx_tar_gz_write_all(outer->device, header, sizeof(header)) ||
        !xx_deflate_pack_source(tar_source, NULL, &measured_tar_size,
                                &deflate_size, &crc32, outer->device, level,
                                false, pd) ||
        measured_tar_size != tar_size || deflate_size < 0 ||
        deflate_size > INT64_MAX - 18) {
        return false;
    }
    xx_tar_gz_put_u32le(trailer, crc32);
    xx_tar_gz_put_u32le(trailer + 4U, (uint32_t)tar_size);
    if (!xx_tar_gz_write_all(outer->device, trailer, sizeof(trailer))) {
        return false;
    }
    *compressed_size = deflate_size + 18;
    ((xx_tar_gz *)outer)->compressed_size = *compressed_size;
    ((xx_tar_gz *)outer)->uncompressed_size = tar_size;
    return true;
}

void xx_tar_gz_init(xx_tar_gz *tar_gz, xx_io_device *dev,
                    int64_t base_address) {
    if (!tar_gz) return;
    xx_mem_zero(tar_gz, sizeof(*tar_gz));
    xx_format_init(&tar_gz->format, dev, base_address);
    tar_gz->format.endian = XX_ENDIAN_UNKNOWN;
    tar_gz->format.file_type = XX_FILE_TYPE_TAR_GZ;
    tar_gz->format.format_type = XX_TYPE_ARCHIVE;
    tar_gz->format.is_archive = true;
    xx_format_set_mime_type(&tar_gz->format, "application/gzip");
    xx_format_set_extension(&tar_gz->format, "tar.gz");
    tar_gz->format.check_is_valid = xx_tar_gz_check_is_valid;
    tar_gz->format.handle_base_info = xx_tar_gz_handle_base_info;
    tar_gz->format.get_format_size = xx_tar_gz_get_format_size;
    tar_gz->format.get_number_of_archive_records =
        xx_tar_gz_get_number_of_archive_records;
    tar_gz->format.create_archive_records_reading =
        xx_tar_gz_create_archive_records_reading;
    tar_gz->format.get_current_archive_record =
        xx_tar_gz_get_current_archive_record;
    tar_gz->format.unpack_current_archive_record =
        xx_tar_gz_unpack_current_archive_record;
    tar_gz->format.archive_record_move_to_next =
        xx_tar_gz_archive_record_move_to_next;
    tar_gz->format.free_archive_records_reading =
        xx_tar_gz_free_archive_records_reading;
    tar_gz->format.create_archive_records_writing =
        xx_tar_gz_create_archive_records_writing;
    tar_gz->format.pack_archive_record = xx_tar_gz_pack_archive_record;
    tar_gz->format.finalize_archive_records_writing =
        xx_tar_gz_finalize_archive_records_writing;
    tar_gz->format.free_archive_records_writing =
        xx_tar_gz_free_archive_records_writing;
    tar_gz->format.data_struct_id_to_string =
        xx_tar_gz_data_struct_id_to_string;
    tar_gz->format.data_struct_string_to_id =
        xx_tar_gz_data_struct_string_to_id;
    tar_gz->format.create_data_structs_reading =
        xx_tar_gz_create_data_structs_reading;
    tar_gz->format.get_current_data_struct =
        xx_tar_gz_get_current_data_struct;
    tar_gz->format.data_struct_move_to_next =
        xx_tar_gz_data_struct_move_to_next;
    tar_gz->format.free_data_structs_reading =
        xx_tar_gz_free_data_structs_reading;
    tar_gz->format.create_data_struct_records_reading =
        xx_tar_gz_create_data_struct_records_reading;
    tar_gz->format.get_current_data_struct_record =
        xx_tar_gz_get_current_data_struct_record;
    tar_gz->format.data_struct_record_move_to_next =
        xx_tar_gz_data_struct_record_move_to_next;
    tar_gz->format.free_data_struct_records_reading =
        xx_tar_gz_free_data_struct_records_reading;
    tar_gz->format.destroy = xx_tar_gz_vtable_destroy;
    xx_tar_gz_clear_public_state(tar_gz);
}

xx_tar_gz *xx_tar_gz_create(xx_io_device *dev, int64_t base_address) {
    xx_tar_gz *tar_gz = (xx_tar_gz *)xx_mem_alloc(sizeof(*tar_gz));
    if (tar_gz) xx_tar_gz_init(tar_gz, dev, base_address);
    return tar_gz;
}

void xx_tar_gz_destroy(xx_tar_gz *tar_gz) {
    if (!tar_gz) return;
    if (tar_gz->internal) {
        xx_tar_common_cleanup((xx_tar_common *)tar_gz->internal);
        xx_mem_free(tar_gz->internal);
        tar_gz->internal = NULL;
    }
    xx_tar_gz_clear_public_state(tar_gz);
    if (tar_gz->format.close) tar_gz->format.close(&tar_gz->format);
    xx_format_cleanup_extra_parameters(&tar_gz->format);
}

static void xx_tar_gz_vtable_destroy(Abstractformat *self) {
    xx_tar_gz_destroy((xx_tar_gz *)self);
}

void xx_tar_gz_free(xx_tar_gz *tar_gz) {
    if (!tar_gz) return;
    xx_tar_gz_destroy(tar_gz);
    xx_mem_free(tar_gz);
}

bool xx_tar_gz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_gz *tar_gz = (xx_tar_gz *)self;
    xx_tar_common *common = xx_tar_gz_common(tar_gz);
    bool result = common &&
                  xx_tar_common_load(common, self, xx_tar_gz_decode, pd);
    if (result) xx_tar_gz_sync_public_state(tar_gz, common);
    return result;
}

bool xx_tar_gz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_gz *tar_gz = (xx_tar_gz *)self;
    xx_tar_common *common = xx_tar_gz_common(tar_gz);
    bool result = common && xx_tar_common_handle_base_info(
                                common, self, xx_tar_gz_decode, pd);
    if (result) {
        xx_tar_gz_sync_public_state(tar_gz, common);
        self->file_type = XX_FILE_TYPE_TAR_GZ;
        self->format_type = XX_TYPE_ARCHIVE;
        self->is_archive = true;
        xx_format_set_mime_type(self, "application/gzip");
        xx_format_set_extension(self, "tar.gz");
    } else {
        xx_tar_gz_clear_public_state(tar_gz);
    }
    return result;
}

int64_t xx_tar_gz_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_gz *tar_gz = (xx_tar_gz *)self;
    xx_tar_common *common = xx_tar_gz_common(tar_gz);
    int64_t result = common ? xx_tar_common_get_format_size(
                                  common, self, xx_tar_gz_decode, pd)
                            : -1;
    if (result >= 0) xx_tar_gz_sync_public_state(tar_gz, common);
    return result;
}

uint64_t xx_tar_gz_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    xx_tar_gz *tar_gz = (xx_tar_gz *)self;
    xx_tar_common *common = xx_tar_gz_common(tar_gz);
    uint64_t result = common
                          ? xx_tar_common_get_number_of_archive_records(
                                common, self, xx_tar_gz_decode, pd)
                          : 0U;
    if (common && common->valid) xx_tar_gz_sync_public_state(tar_gz, common);
    return result;
}

xx_archive_record_state *xx_tar_gz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tar_gz *tar_gz = (xx_tar_gz *)self;
    xx_tar_common *common = xx_tar_gz_common(tar_gz);
    return common ? xx_tar_common_create_archive_records_reading(
                        common, self, xx_tar_gz_decode, options, pd)
                  : NULL;
}

const xx_archive_record *xx_tar_gz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    return common ? xx_tar_common_get_current_archive_record(common, state)
                  : NULL;
}

bool xx_tar_gz_unpack_current_archive_record(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    return common && xx_tar_common_unpack_current_archive_record(common, state,
                                                                  pd);
}

bool xx_tar_gz_archive_record_move_to_next(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    return common &&
           xx_tar_common_archive_record_move_to_next(common, state, pd);
}

void xx_tar_gz_free_archive_records_reading(Abstractformat *self,
                                             xx_archive_record_state *state) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    xx_tar_common_free_archive_records_reading(common, state);
}

xx_archive_write_state *xx_tar_gz_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tar_gz *tar_gz = (xx_tar_gz *)self;
    xx_archive_write_state *state =
        xx_tar_common_create_archive_records_writing(
            self, options, xx_tar_gz_encode, pd);
    xx_tar_common *common;
    if (!state) return NULL;
    common = tar_gz ? (xx_tar_common *)tar_gz->internal : NULL;
    if (common) {
        xx_tar_common_cleanup(common);
        xx_tar_common_init(common);
    }
    xx_tar_gz_clear_public_state(tar_gz);
    return state;
}

bool xx_tar_gz_pack_archive_record(Abstractformat *self,
                                   xx_archive_write_state *state,
                                   const xx_archive_record *record,
                                   xx_io_device *source_dev,
                                   xx_pd_struct *pd) {
    return xx_tar_common_pack_archive_record(self, state, record, source_dev,
                                              pd);
}

bool xx_tar_gz_finalize_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd) {
    xx_tar_gz *tar_gz = (xx_tar_gz *)self;
    xx_tar_common *common;
    if (!xx_tar_common_finalize_archive_records_writing(self, state, pd)) {
        return false;
    }
    common = tar_gz ? (xx_tar_common *)tar_gz->internal : NULL;
    if (common) {
        xx_tar_common_cleanup(common);
        xx_tar_common_init(common);
    }
    tar_gz->number_of_records = self->number_of_archive_records;
    tar_gz->number_of_members = self->number_of_archive_records;
    tar_gz->compressed_size = self->format_size;
    return true;
}

void xx_tar_gz_free_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state) {
    xx_tar_common_free_archive_records_writing(self, state);
}

const char *xx_tar_gz_data_struct_id_to_string(Abstractformat *self,
                                               uint32_t id) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    return xx_tar_common_data_struct_id_to_string(common, id);
}

uint32_t xx_tar_gz_data_struct_string_to_id(Abstractformat *self,
                                            const char *name) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    return xx_tar_common_data_struct_string_to_id(common, name);
}

xx_data_struct_state *xx_tar_gz_create_data_structs_reading(
    Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_gz *tar_gz = (xx_tar_gz *)self;
    xx_tar_common *common = xx_tar_gz_common(tar_gz);
    return common ? xx_tar_common_create_data_structs_reading(
                        common, self, xx_tar_gz_decode, pd)
                  : NULL;
}

const xx_data_struct *xx_tar_gz_get_current_data_struct(
    Abstractformat *self, xx_data_struct_state *state) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    return common ? xx_tar_common_get_current_data_struct(common, state)
                  : NULL;
}

bool xx_tar_gz_data_struct_move_to_next(Abstractformat *self,
                                         xx_data_struct_state *state,
                                         xx_pd_struct *pd) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    return common && xx_tar_common_data_struct_move_to_next(common, state, pd);
}

void xx_tar_gz_free_data_structs_reading(Abstractformat *self,
                                          xx_data_struct_state *state) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    xx_tar_common_free_data_structs_reading(common, state);
}

xx_data_struct_record_state *xx_tar_gz_create_data_struct_records_reading(
    Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    return common ? xx_tar_common_create_data_struct_records_reading(
                        common, ds, pd)
                  : NULL;
}

const xx_data_struct_record *xx_tar_gz_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    return common
               ? xx_tar_common_get_current_data_struct_record(common, state)
               : NULL;
}

bool xx_tar_gz_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    return common && xx_tar_common_data_struct_record_move_to_next(
                         common, state, pd);
}

void xx_tar_gz_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_gz *)self)->internal
                                : NULL;
    xx_tar_common_free_data_struct_records_reading(common, state);
}

uint64_t xx_tar_gz_get_number_of_records(const xx_tar_gz *tar_gz) {
    return tar_gz ? tar_gz->number_of_records : 0U;
}

uint64_t xx_tar_gz_get_number_of_members(const xx_tar_gz *tar_gz) {
    return tar_gz ? tar_gz->number_of_members : 0U;
}

int64_t xx_tar_gz_get_compressed_size(const xx_tar_gz *tar_gz) {
    return tar_gz ? tar_gz->compressed_size : -1;
}

int64_t xx_tar_gz_get_uncompressed_size(const xx_tar_gz *tar_gz) {
    return tar_gz ? tar_gz->uncompressed_size : -1;
}
