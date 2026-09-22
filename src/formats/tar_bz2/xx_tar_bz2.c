/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/tar_bz2/xx_tar_bz2.h"

#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/formats/bz2/xx_bz2.h"
#include "xxfclib/memory/xx_memory.h"
#include "xx_tar_common.h"

#include <limits.h>

static void xx_tar_bz2_vtable_destroy(Abstractformat *self);

static xx_tar_common *xx_tar_bz2_common(Abstractformat *self) {
    return self ? (xx_tar_common *)((xx_tar_bz2 *)self)->internal : NULL;
}

static const xx_tar_common *xx_tar_bz2_common_const(
    const xx_tar_bz2 *tar_bz2) {
    return tar_bz2 ? (const xx_tar_common *)tar_bz2->internal : NULL;
}

static bool xx_tar_bz2_decode(Abstractformat *outer,
                              xx_io_device *destination,
                              int64_t *compressed_size,
                              xx_pd_struct *pd) {
    xx_bz2 transport;
    bool result;

    if (!outer || !outer->device || !destination || !compressed_size ||
        outer->base_address < 0) {
        return false;
    }
    *compressed_size = -1;
    xx_bz2_init(&transport, outer->device, outer->base_address);
    if (!xx_bz2_handle_base_info(&transport.format, pd)) {
        xx_bz2_destroy(&transport);
        return false;
    }
    result = xx_bz2_unpack_to_device(&transport, destination, pd);
    if (result) {
        *compressed_size = transport.format.format_size;
    }
    xx_bz2_destroy(&transport);
    return result && *compressed_size > 0;
}

static const xx_var *xx_tar_bz2_find_option(const xx_list_s *options,
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

static int xx_tar_bz2_compression_level(const xx_list_s *options) {
    const xx_var *value = xx_tar_bz2_find_option(
        options, XX_META_ID_OPT_COMPRESSION_LEVEL);
    int64_t requested;
    if (!value) return XX_BZIP2_LEVEL_DEFAULT;
    requested = xx_var_get_i64(value);
    if (requested < XX_BZIP2_LEVEL_FASTEST)
        return XX_BZIP2_LEVEL_FASTEST;
    if (requested > XX_BZIP2_LEVEL_BEST)
        return XX_BZIP2_LEVEL_BEST;
    return (int)requested;
}

static bool xx_tar_bz2_encode(Abstractformat *outer,
                              const xx_list_s *options,
                              xx_io_device *tar_source, int64_t tar_size,
                              int64_t *compressed_size,
                              xx_pd_struct *pd) {
    int64_t measured_tar_size = 0;
    int64_t bzip2_size = 0;
    uint32_t crc32 = 0U;
    int level;
    if (compressed_size) *compressed_size = -1;
    if (!outer || !outer->device || !tar_source || !compressed_size ||
        tar_size <= 0 || outer->base_address < 0 ||
        outer->base_address > LONG_MAX || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    level = xx_tar_bz2_compression_level(options);
    if (xx_io_seek(outer->device, (long)outer->base_address, SEEK_SET) != 0 ||
        !xx_bzip2_pack_source(tar_source, NULL, &measured_tar_size,
                              &bzip2_size, &crc32, outer->device, level, pd) ||
        measured_tar_size != tar_size || bzip2_size <= 0) {
        return false;
    }
    *compressed_size = bzip2_size;
    ((xx_tar_bz2 *)outer)->compressed_size = bzip2_size;
    ((xx_tar_bz2 *)outer)->uncompressed_size = (uint64_t)tar_size;
    return true;
}

static void xx_tar_bz2_sync_public_state(xx_tar_bz2 *tar_bz2) {
    const xx_tar_common *common;
    if (!tar_bz2) return;
    common = xx_tar_bz2_common_const(tar_bz2);
    if (!common || !common->valid || !common->tar) {
        tar_bz2->number_of_records = 0U;
        tar_bz2->number_of_members = 0U;
        tar_bz2->uncompressed_size = 0U;
        tar_bz2->compressed_size = -1;
        return;
    }
    tar_bz2->number_of_records = xx_tar_get_number_of_records(common->tar);
    tar_bz2->number_of_members = xx_tar_get_number_of_members(common->tar);
    tar_bz2->uncompressed_size = (uint64_t)common->decoded_size;
    tar_bz2->compressed_size = common->compressed_size;
}

void xx_tar_bz2_init(xx_tar_bz2 *tar_bz2, xx_io_device *dev,
                     int64_t base_address) {
    xx_tar_common *common;
    if (!tar_bz2) return;
    xx_mem_zero(tar_bz2, sizeof(*tar_bz2));
    xx_format_init(&tar_bz2->format, dev, base_address);
    tar_bz2->format.endian = XX_ENDIAN_UNKNOWN;
    tar_bz2->format.file_type = XX_FILE_TYPE_TAR_BZ2;
    tar_bz2->format.format_type = XX_TYPE_ARCHIVE;
    tar_bz2->format.is_archive = true;
    xx_format_set_mime_type(&tar_bz2->format, "application/x-bzip2");
    xx_format_set_extension(&tar_bz2->format, "tar.bz2");

    tar_bz2->format.check_is_valid = xx_tar_bz2_check_is_valid;
    tar_bz2->format.handle_base_info = xx_tar_bz2_handle_base_info;
    tar_bz2->format.get_format_size = xx_tar_bz2_get_format_size;
    tar_bz2->format.get_number_of_archive_records =
        xx_tar_bz2_get_number_of_archive_records;
    tar_bz2->format.create_archive_records_reading =
        xx_tar_bz2_create_archive_records_reading;
    tar_bz2->format.get_current_archive_record =
        xx_tar_bz2_get_current_archive_record;
    tar_bz2->format.unpack_current_archive_record =
        xx_tar_bz2_unpack_current_archive_record;
    tar_bz2->format.archive_record_move_to_next =
        xx_tar_bz2_archive_record_move_to_next;
    tar_bz2->format.free_archive_records_reading =
        xx_tar_bz2_free_archive_records_reading;
    tar_bz2->format.create_archive_records_writing =
        xx_tar_bz2_create_archive_records_writing;
    tar_bz2->format.pack_archive_record = xx_tar_bz2_pack_archive_record;
    tar_bz2->format.finalize_archive_records_writing =
        xx_tar_bz2_finalize_archive_records_writing;
    tar_bz2->format.free_archive_records_writing =
        xx_tar_bz2_free_archive_records_writing;
    tar_bz2->format.data_struct_id_to_string =
        xx_tar_bz2_data_struct_id_to_string;
    tar_bz2->format.data_struct_string_to_id =
        xx_tar_bz2_data_struct_string_to_id;
    tar_bz2->format.create_data_structs_reading =
        xx_tar_bz2_create_data_structs_reading;
    tar_bz2->format.get_current_data_struct =
        xx_tar_bz2_get_current_data_struct;
    tar_bz2->format.data_struct_move_to_next =
        xx_tar_bz2_data_struct_move_to_next;
    tar_bz2->format.free_data_structs_reading =
        xx_tar_bz2_free_data_structs_reading;
    tar_bz2->format.create_data_struct_records_reading =
        xx_tar_bz2_create_data_struct_records_reading;
    tar_bz2->format.get_current_data_struct_record =
        xx_tar_bz2_get_current_data_struct_record;
    tar_bz2->format.data_struct_record_move_to_next =
        xx_tar_bz2_data_struct_record_move_to_next;
    tar_bz2->format.free_data_struct_records_reading =
        xx_tar_bz2_free_data_struct_records_reading;
    tar_bz2->format.destroy = xx_tar_bz2_vtable_destroy;
    tar_bz2->compressed_size = -1;

    common = (xx_tar_common *)xx_mem_alloc(sizeof(*common));
    if (common) {
        xx_tar_common_init(common);
        tar_bz2->internal = common;
    }
}

xx_tar_bz2 *xx_tar_bz2_create(xx_io_device *dev, int64_t base_address) {
    xx_tar_bz2 *tar_bz2 = (xx_tar_bz2 *)xx_mem_alloc(sizeof(*tar_bz2));
    if (tar_bz2) xx_tar_bz2_init(tar_bz2, dev, base_address);
    return tar_bz2;
}

void xx_tar_bz2_destroy(xx_tar_bz2 *tar_bz2) {
    xx_tar_common *common;
    if (!tar_bz2) return;
    common = (xx_tar_common *)tar_bz2->internal;
    if (common) {
        xx_tar_common_cleanup(common);
        xx_mem_free(common);
        tar_bz2->internal = NULL;
    }
    if (tar_bz2->format.close) tar_bz2->format.close(&tar_bz2->format);
    xx_format_cleanup_extra_parameters(&tar_bz2->format);
}

static void xx_tar_bz2_vtable_destroy(Abstractformat *self) {
    xx_tar_bz2_destroy((xx_tar_bz2 *)self);
}

void xx_tar_bz2_free(xx_tar_bz2 *tar_bz2) {
    if (!tar_bz2) return;
    xx_tar_bz2_destroy(tar_bz2);
    xx_mem_free(tar_bz2);
}

bool xx_tar_bz2_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    return common && xx_tar_common_load(common, self, xx_tar_bz2_decode, pd);
}

bool xx_tar_bz2_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    bool result;
    if (!common) return false;
    result = xx_tar_common_handle_base_info(common, self, xx_tar_bz2_decode,
                                            pd);
    xx_tar_bz2_sync_public_state((xx_tar_bz2 *)self);
    if (result) {
        self->file_type = XX_FILE_TYPE_TAR_BZ2;
        self->format_type = XX_TYPE_ARCHIVE;
        self->is_archive = true;
    }
    return result;
}

int64_t xx_tar_bz2_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    int64_t result;
    if (!common) return -1;
    result = xx_tar_common_get_format_size(common, self, xx_tar_bz2_decode,
                                           pd);
    xx_tar_bz2_sync_public_state((xx_tar_bz2 *)self);
    return result;
}

uint64_t xx_tar_bz2_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    uint64_t result;
    if (!common) return 0U;
    result = xx_tar_common_get_number_of_archive_records(
        common, self, xx_tar_bz2_decode, pd);
    xx_tar_bz2_sync_public_state((xx_tar_bz2 *)self);
    return result;
}

xx_archive_record_state *xx_tar_bz2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    if (!common) return NULL;
    return xx_tar_common_create_archive_records_reading(
        common, self, xx_tar_bz2_decode, options, pd);
}

const xx_archive_record *xx_tar_bz2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    return common
               ? xx_tar_common_get_current_archive_record(common, state)
               : NULL;
}

bool xx_tar_bz2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    return common && xx_tar_common_unpack_current_archive_record(
                         common, state, pd);
}

bool xx_tar_bz2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    return common &&
           xx_tar_common_archive_record_move_to_next(common, state, pd);
}

void xx_tar_bz2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    if (common) xx_tar_common_free_archive_records_reading(common, state);
}

xx_archive_write_state *xx_tar_bz2_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tar_bz2 *tar_bz2 = (xx_tar_bz2 *)self;
    xx_archive_write_state *state =
        xx_tar_common_create_archive_records_writing(
            self, options, xx_tar_bz2_encode, pd);
    xx_tar_common *common;
    if (!state) return NULL;
    common = tar_bz2 ? (xx_tar_common *)tar_bz2->internal : NULL;
    if (common) {
        xx_tar_common_cleanup(common);
        xx_tar_common_init(common);
    }
    tar_bz2->number_of_records = 0U;
    tar_bz2->number_of_members = 0U;
    tar_bz2->uncompressed_size = 0U;
    tar_bz2->compressed_size = -1;
    return state;
}

bool xx_tar_bz2_pack_archive_record(Abstractformat *self,
                                    xx_archive_write_state *state,
                                    const xx_archive_record *record,
                                    xx_io_device *source_dev,
                                    xx_pd_struct *pd) {
    return xx_tar_common_pack_archive_record(self, state, record, source_dev,
                                              pd);
}

bool xx_tar_bz2_finalize_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd) {
    xx_tar_bz2 *tar_bz2 = (xx_tar_bz2 *)self;
    xx_tar_common *common;
    if (!xx_tar_common_finalize_archive_records_writing(self, state, pd)) {
        return false;
    }
    common = tar_bz2 ? (xx_tar_common *)tar_bz2->internal : NULL;
    if (common) {
        xx_tar_common_cleanup(common);
        xx_tar_common_init(common);
    }
    tar_bz2->number_of_records = self->number_of_archive_records;
    tar_bz2->number_of_members = self->number_of_archive_records;
    tar_bz2->compressed_size = self->format_size;
    return true;
}

void xx_tar_bz2_free_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state) {
    xx_tar_common_free_archive_records_writing(self, state);
}

const char *xx_tar_bz2_data_struct_id_to_string(Abstractformat *self,
                                                uint32_t id) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    return common ? xx_tar_common_data_struct_id_to_string(common, id)
                  : "UNKNOWN";
}

uint32_t xx_tar_bz2_data_struct_string_to_id(Abstractformat *self,
                                              const char *name) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    return common ? xx_tar_common_data_struct_string_to_id(common, name)
                  : (uint32_t)XX_TAR_DS_UNKNOWN;
}

xx_data_struct_state *xx_tar_bz2_create_data_structs_reading(
    Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    if (!common) return NULL;
    return xx_tar_common_create_data_structs_reading(
        common, self, xx_tar_bz2_decode, pd);
}

const xx_data_struct *xx_tar_bz2_get_current_data_struct(
    Abstractformat *self, xx_data_struct_state *state) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    return common ? xx_tar_common_get_current_data_struct(common, state)
                  : NULL;
}

bool xx_tar_bz2_data_struct_move_to_next(
    Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    return common &&
           xx_tar_common_data_struct_move_to_next(common, state, pd);
}

void xx_tar_bz2_free_data_structs_reading(Abstractformat *self,
                                          xx_data_struct_state *state) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    if (common) xx_tar_common_free_data_structs_reading(common, state);
}

xx_data_struct_record_state *
xx_tar_bz2_create_data_struct_records_reading(Abstractformat *self,
                                               const xx_data_struct *ds,
                                               xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    return common ? xx_tar_common_create_data_struct_records_reading(
                        common, ds, pd)
                  : NULL;
}

const xx_data_struct_record *xx_tar_bz2_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    return common
               ? xx_tar_common_get_current_data_struct_record(common, state)
               : NULL;
}

bool xx_tar_bz2_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    return common && xx_tar_common_data_struct_record_move_to_next(
                         common, state, pd);
}

void xx_tar_bz2_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state) {
    xx_tar_common *common = xx_tar_bz2_common(self);
    if (common) {
        xx_tar_common_free_data_struct_records_reading(common, state);
    }
}

uint64_t xx_tar_bz2_get_number_of_records(const xx_tar_bz2 *tar_bz2) {
    return tar_bz2 ? tar_bz2->number_of_records : 0U;
}

uint64_t xx_tar_bz2_get_number_of_members(const xx_tar_bz2 *tar_bz2) {
    return tar_bz2 ? tar_bz2->number_of_members : 0U;
}

uint64_t xx_tar_bz2_get_uncompressed_size(const xx_tar_bz2 *tar_bz2) {
    return tar_bz2 ? tar_bz2->uncompressed_size : 0U;
}

int64_t xx_tar_bz2_get_compressed_size(const xx_tar_bz2 *tar_bz2) {
    return tar_bz2 ? tar_bz2->compressed_size : -1;
}
