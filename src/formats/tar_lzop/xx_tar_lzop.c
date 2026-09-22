/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/tar_lzop/xx_tar_lzop.h"

#include "../tar_common/xx_tar_common.h"
#include "xxfclib/algo/lzop/xx_lzop.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>

static void xx_tar_lzop_vtable_destroy(Abstractformat *self);

static void xx_tar_lzop_clear_public_state(xx_tar_lzop *archive) {
    if (!archive) return;
    archive->number_of_records = 0U;
    archive->number_of_members = 0U;
    archive->lzop_stream_count = 0U;
    archive->compressed_size = -1;
    archive->uncompressed_size = -1;
}

static xx_tar_common *xx_tar_lzop_common(xx_tar_lzop *archive,
                                          bool create) {
    xx_tar_common *common;
    if (!archive) return NULL;
    common = (xx_tar_common *)archive->internal;
    if (!common && create) {
        common = (xx_tar_common *)xx_mem_alloc(sizeof(*common));
        if (!common) return NULL;
        xx_tar_common_init(common);
        archive->internal = common;
    }
    return common;
}

static void xx_tar_lzop_sync_public_state(xx_tar_lzop *archive,
                                          const xx_tar_common *common) {
    if (!archive || !common || !common->valid || !common->tar) return;
    archive->number_of_records = xx_tar_get_number_of_records(common->tar);
    archive->number_of_members = xx_tar_get_number_of_members(common->tar);
    archive->compressed_size = common->compressed_size;
    archive->uncompressed_size = common->decoded_size <= (size_t)INT64_MAX
                                     ? (int64_t)common->decoded_size
                                     : -1;
}

static bool xx_tar_lzop_decode(Abstractformat *outer,
                               xx_io_device *destination,
                               int64_t *compressed_size,
                               xx_pd_struct *pd) {
    int64_t total_size;
    int64_t transport_size;
    int64_t decoded_size;
    size_t stream_count;
    if (compressed_size) *compressed_size = -1;
    if (!outer || !outer->device || !destination || !compressed_size ||
        outer->base_address < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(outer->device);
    if (total_size <= outer->base_address) return false;
    transport_size = total_size - outer->base_address;
    if (!xx_lzop_decode_device(outer->device, outer->base_address,
                               transport_size, destination, &decoded_size,
                               &stream_count, pd) || decoded_size < 0 ||
        stream_count == 0U) {
        return false;
    }
    ((xx_tar_lzop *)outer)->lzop_stream_count = (uint64_t)stream_count;
    *compressed_size = transport_size;
    return true;
}

void xx_tar_lzop_init(xx_tar_lzop *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_TAR_LZOP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lzop");
    xx_format_set_extension(&archive->format, "tar.lzo");
    archive->format.check_is_valid = xx_tar_lzop_check_is_valid;
    archive->format.handle_base_info = xx_tar_lzop_handle_base_info;
    archive->format.get_format_size = xx_tar_lzop_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_tar_lzop_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_tar_lzop_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_tar_lzop_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_tar_lzop_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_tar_lzop_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_tar_lzop_free_archive_records_reading;
    archive->format.destroy = xx_tar_lzop_vtable_destroy;
    xx_tar_lzop_clear_public_state(archive);
}

xx_tar_lzop *xx_tar_lzop_create(xx_io_device *device, int64_t base_address) {
    xx_tar_lzop *archive = (xx_tar_lzop *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_tar_lzop_init(archive, device, base_address);
    return archive;
}

void xx_tar_lzop_destroy(xx_tar_lzop *archive) {
    if (!archive) return;
    if (archive->internal) {
        xx_tar_common_cleanup((xx_tar_common *)archive->internal);
        xx_mem_free(archive->internal);
        archive->internal = NULL;
    }
    xx_tar_lzop_clear_public_state(archive);
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
}

static void xx_tar_lzop_vtable_destroy(Abstractformat *self) {
    xx_tar_lzop_destroy((xx_tar_lzop *)self);
}

void xx_tar_lzop_free(xx_tar_lzop *archive) {
    if (!archive) return;
    xx_tar_lzop_destroy(archive);
    xx_mem_free(archive);
}

bool xx_tar_lzop_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_lzop *archive = (xx_tar_lzop *)self;
    xx_tar_common *common = xx_tar_lzop_common(archive, true);
    bool result = common &&
                  xx_tar_common_load(common, self, xx_tar_lzop_decode, pd);
    if (result) xx_tar_lzop_sync_public_state(archive, common);
    return result;
}

bool xx_tar_lzop_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_lzop *archive = (xx_tar_lzop *)self;
    xx_tar_common *common = xx_tar_lzop_common(archive, true);
    bool result = common && xx_tar_common_handle_base_info(
                                common, self, xx_tar_lzop_decode, pd);
    if (result) {
        xx_tar_lzop_sync_public_state(archive, common);
        self->file_type = XX_FILE_TYPE_TAR_LZOP;
        self->format_type = XX_TYPE_ARCHIVE;
        self->is_archive = true;
        self->is_executable = false;
        self->is_crypted = false;
    } else {
        xx_tar_lzop_clear_public_state(archive);
    }
    return result;
}

int64_t xx_tar_lzop_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_lzop *archive = (xx_tar_lzop *)self;
    xx_tar_common *common = xx_tar_lzop_common(archive, true);
    int64_t result = common ? xx_tar_common_get_format_size(
                                  common, self, xx_tar_lzop_decode, pd)
                            : -1;
    if (result >= 0) xx_tar_lzop_sync_public_state(archive, common);
    return result;
}

uint64_t xx_tar_lzop_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_lzop *archive = (xx_tar_lzop *)self;
    xx_tar_common *common = xx_tar_lzop_common(archive, true);
    uint64_t result = common ? xx_tar_common_get_number_of_archive_records(
                                 common, self, xx_tar_lzop_decode, pd)
                             : 0U;
    if (common && common->valid) xx_tar_lzop_sync_public_state(archive, common);
    return result;
}

xx_archive_record_state *xx_tar_lzop_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_lzop_common((xx_tar_lzop *)self, true);
    return common ? xx_tar_common_create_archive_records_reading(
                        common, self, xx_tar_lzop_decode, options, pd)
                  : NULL;
}

const xx_archive_record *xx_tar_lzop_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_lzop *)self)->internal
                                : NULL;
    return common ? xx_tar_common_get_current_archive_record(common, state)
                  : NULL;
}

bool xx_tar_lzop_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_lzop *)self)->internal
                                : NULL;
    return common && xx_tar_common_unpack_current_archive_record(common, state,
                                                                  pd);
}

bool xx_tar_lzop_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_lzop *)self)->internal
                                : NULL;
    return common && xx_tar_common_archive_record_move_to_next(common, state,
                                                                pd);
}

void xx_tar_lzop_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    xx_tar_common *common = self
                                ? (xx_tar_common *)((xx_tar_lzop *)self)->internal
                                : NULL;
    if (common) xx_tar_common_free_archive_records_reading(common, state);
}

uint64_t xx_tar_lzop_get_number_of_records(const xx_tar_lzop *archive) {
    return archive ? archive->number_of_records : 0U;
}

uint64_t xx_tar_lzop_get_number_of_members(const xx_tar_lzop *archive) {
    return archive ? archive->number_of_members : 0U;
}

uint64_t xx_tar_lzop_get_lzop_stream_count(const xx_tar_lzop *archive) {
    return archive ? archive->lzop_stream_count : 0U;
}

int64_t xx_tar_lzop_get_compressed_size(const xx_tar_lzop *archive) {
    return archive ? archive->compressed_size : -1;
}

int64_t xx_tar_lzop_get_uncompressed_size(const xx_tar_lzop *archive) {
    return archive ? archive->uncompressed_size : -1;
}
