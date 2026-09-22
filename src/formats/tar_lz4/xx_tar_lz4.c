/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/tar_lz4/xx_tar_lz4.h"

#include "../tar_common/xx_tar_common.h"
#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>

#define XX_TAR_LZ4_MAGIC_SIZE 4U
#define XX_TAR_LZ4_HEADER_SIZE 15U
#define XX_TAR_LZ4_BLOCK_SIZE (64U * 1024U)
#define XX_TAR_LZ4_MAX_BUFFER ((size_t)1024U * 1024U * 1024U)

#define XX_TAR_LZ4_XXH_PRIME1 UINT32_C(2654435761)
#define XX_TAR_LZ4_XXH_PRIME2 UINT32_C(2246822519)
#define XX_TAR_LZ4_XXH_PRIME3 UINT32_C(3266489917)
#define XX_TAR_LZ4_XXH_PRIME4 UINT32_C(668265263)
#define XX_TAR_LZ4_XXH_PRIME5 UINT32_C(374761393)

static void xx_tar_lz4_vtable_destroy(Abstractformat *self);

static uint32_t xx_tar_lz4_read32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static uint64_t xx_tar_lz4_read64(const uint8_t *data) {
    return (uint64_t)xx_tar_lz4_read32(data) |
           ((uint64_t)xx_tar_lz4_read32(data + 4U) << 32U);
}

static void xx_tar_lz4_put32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static void xx_tar_lz4_put64(uint8_t *data, uint64_t value) {
    xx_tar_lz4_put32(data, (uint32_t)value);
    xx_tar_lz4_put32(data + 4U, (uint32_t)(value >> 32U));
}

static uint32_t xx_tar_lz4_rotl32(uint32_t value, unsigned count) {
    return (value << count) | (value >> (32U - count));
}

/* A compact local xxHash32 implementation used only for the LZ4 frame
 * descriptor checksum. It is independently written from the public format. */
static uint32_t xx_tar_lz4_xxh32(const uint8_t *data, size_t size) {
    uint32_t hash = XX_TAR_LZ4_XXH_PRIME5 + (uint32_t)size;
    size_t offset = 0U;
    while (size - offset >= 4U) {
        hash += xx_tar_lz4_read32(data + offset) * XX_TAR_LZ4_XXH_PRIME3;
        hash = xx_tar_lz4_rotl32(hash, 17U) * XX_TAR_LZ4_XXH_PRIME4;
        offset += 4U;
    }
    while (offset < size) {
        hash += (uint32_t)data[offset++] * XX_TAR_LZ4_XXH_PRIME5;
        hash = xx_tar_lz4_rotl32(hash, 11U) * XX_TAR_LZ4_XXH_PRIME1;
    }
    hash ^= hash >> 15U;
    hash *= XX_TAR_LZ4_XXH_PRIME2;
    hash ^= hash >> 13U;
    hash *= XX_TAR_LZ4_XXH_PRIME3;
    hash ^= hash >> 16U;
    return hash;
}

static bool xx_tar_lz4_write_all(xx_io_device *device, const void *data,
                                 size_t size, xx_pd_struct *pd) {
    const uint8_t *bytes = (const uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t written;
        if (pd && xx_pd_is_stopped(pd)) return false;
        written = xx_io_write(device, bytes + done, size - done);
        if (written <= 0 || (size_t)written > size - done) return false;
        done += (size_t)written;
    }
    return true;
}

static bool xx_tar_lz4_read_all_at(xx_io_device *device, int64_t offset,
                                   void *data, size_t size) {
    uint8_t *bytes = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 || offset > LONG_MAX ||
        xx_io_seek(device, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t read = xx_io_read(device, bytes + done, size - done);
        if (read <= 0 || (size_t)read > size - done) return false;
        done += (size_t)read;
    }
    return true;
}

static bool xx_tar_lz4_declared_size(const uint8_t *source, size_t size,
                                     size_t *result) {
    uint8_t flags;
    uint8_t block_descriptor;
    uint64_t declared;
    if (!source || !result || size < XX_TAR_LZ4_HEADER_SIZE ||
        xx_tar_lz4_read32(source) != UINT32_C(0x184D2204)) {
        return false;
    }
    flags = source[4];
    block_descriptor = source[5];
    if ((flags >> 6U) != 1U || (flags & UINT8_C(0x02)) != 0U ||
        (flags & UINT8_C(0x08)) == 0U || (flags & UINT8_C(0x01)) != 0U ||
        (block_descriptor & UINT8_C(0x8F)) != 0U) {
        return false;
    }
    declared = xx_tar_lz4_read64(source + 6U);
    if (declared == 0U || declared > (uint64_t)XX_TAR_LZ4_MAX_BUFFER ||
        declared > (uint64_t)SIZE_MAX) {
        return false;
    }
    *result = (size_t)declared;
    return true;
}

static xx_tar_common *xx_tar_lz4_common(xx_tar_lz4 *tar_lz4, bool create) {
    xx_tar_common *common;
    if (!tar_lz4) return NULL;
    common = (xx_tar_common *)tar_lz4->internal;
    if (!common && create) {
        common = (xx_tar_common *)xx_mem_alloc(sizeof(*common));
        if (!common) return NULL;
        xx_tar_common_init(common);
        tar_lz4->internal = common;
    }
    return common;
}

static void xx_tar_lz4_sync(xx_tar_lz4 *tar_lz4,
                            const xx_tar_common *common) {
    if (!tar_lz4 || !common || !common->valid || !common->tar) return;
    tar_lz4->number_of_records = xx_tar_get_number_of_records(common->tar);
    tar_lz4->number_of_members = xx_tar_get_number_of_members(common->tar);
    tar_lz4->compressed_size = common->compressed_size;
    tar_lz4->uncompressed_size = (uint64_t)common->decoded_size <=
                                         (uint64_t)INT64_MAX
                                     ? (int64_t)common->decoded_size
                                     : -1;
}

static bool xx_tar_lz4_decode(Abstractformat *outer,
                              xx_io_device *destination,
                              int64_t *compressed_size,
                              xx_pd_struct *pd) {
    int64_t total;
    int64_t size_i64;
    uint8_t *compressed = NULL;
    uint8_t *decoded = NULL;
    size_t decoded_size;
    size_t written = 0U;
    bool result = false;
    if (compressed_size) *compressed_size = -1;
    if (!outer || !outer->device || !destination || !compressed_size ||
        outer->base_address < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total = xx_io_total_size(outer->device);
    if (total <= outer->base_address ||
        total - outer->base_address > (int64_t)XX_TAR_LZ4_MAX_BUFFER ||
        (uint64_t)(total - outer->base_address) > (uint64_t)SIZE_MAX) {
        return false;
    }
    size_i64 = total - outer->base_address;
    compressed = (uint8_t *)xx_mem_alloc((size_t)size_i64);
    if (!compressed || !xx_tar_lz4_read_all_at(outer->device,
                                                outer->base_address,
                                                compressed, (size_t)size_i64) ||
        !xx_tar_lz4_declared_size(compressed, (size_t)size_i64,
                                  &decoded_size)) {
        goto cleanup;
    }
    decoded = (uint8_t *)xx_mem_alloc(decoded_size);
    if (!decoded || !xx_lz4_decompress_memory(compressed, (size_t)size_i64,
                                               decoded, decoded_size,
                                               &written) ||
        written != decoded_size ||
        !xx_tar_lz4_write_all(destination, decoded, decoded_size, pd)) {
        goto cleanup;
    }
    *compressed_size = size_i64;
    result = true;
cleanup:
    if (decoded) xx_mem_free(decoded);
    if (compressed) xx_mem_free(compressed);
    return result;
}

static bool xx_tar_lz4_encode(Abstractformat *outer,
                              const xx_list_s *options,
                              xx_io_device *tar_source, int64_t tar_size,
                              int64_t *compressed_size,
                              xx_pd_struct *pd) {
    uint8_t header[XX_TAR_LZ4_HEADER_SIZE] = {0};
    uint8_t buffer[XX_TAR_LZ4_BLOCK_SIZE];
    uint8_t block_size[4];
    int64_t remaining;
    int64_t total_size = (int64_t)XX_TAR_LZ4_HEADER_SIZE + 4;
    (void)options;
    if (compressed_size) *compressed_size = -1;
    if (!outer || !outer->device || !outer->device->write ||
        !outer->device->seek || !tar_source || !compressed_size ||
        tar_size <= 0 || outer->base_address < 0 ||
        outer->base_address > LONG_MAX || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    header[0] = 0x04U;
    header[1] = 0x22U;
    header[2] = 0x4dU;
    header[3] = 0x18U;
    header[4] = 0x68U; /* version 1, independent blocks, content size */
    header[5] = 0x40U; /* 64 KiB maximum block size */
    xx_tar_lz4_put64(header + 6U, (uint64_t)tar_size);
    header[14] = (uint8_t)(xx_tar_lz4_xxh32(header + 4U, 10U) >> 8U);
    if (xx_io_seek(outer->device, (long)outer->base_address, SEEK_SET) != 0 ||
        !xx_tar_lz4_write_all(outer->device, header, sizeof(header), pd)) {
        return false;
    }
    remaining = tar_size;
    while (remaining != 0) {
        size_t requested = remaining > (int64_t)sizeof(buffer)
                               ? sizeof(buffer)
                               : (size_t)remaining;
        ssize_t read;
        if (pd && xx_pd_is_stopped(pd)) return false;
        read = xx_io_read(tar_source, buffer, requested);
        if (read <= 0 || (size_t)read > requested ||
            read > INT32_MAX || total_size > INT64_MAX - 4 - read) {
            return false;
        }
        xx_tar_lz4_put32(block_size,
                          UINT32_C(0x80000000) | (uint32_t)read);
        if (!xx_tar_lz4_write_all(outer->device, block_size,
                                  sizeof(block_size), pd) ||
            !xx_tar_lz4_write_all(outer->device, buffer, (size_t)read, pd)) {
            return false;
        }
        total_size += 4 + read;
        remaining -= read;
    }
    xx_mem_zero(block_size, sizeof(block_size));
    if (!xx_tar_lz4_write_all(outer->device, block_size,
                              sizeof(block_size), pd)) {
        return false;
    }
    *compressed_size = total_size;
    ((xx_tar_lz4 *)outer)->compressed_size = total_size;
    ((xx_tar_lz4 *)outer)->uncompressed_size = tar_size;
    return true;
}

void xx_tar_lz4_init(xx_tar_lz4 *tar_lz4, xx_io_device *dev,
                     int64_t base_address) {
    if (!tar_lz4) return;
    xx_mem_zero(tar_lz4, sizeof(*tar_lz4));
    xx_format_init(&tar_lz4->format, dev, base_address);
    tar_lz4->format.endian = XX_ENDIAN_UNKNOWN;
    tar_lz4->format.file_type = XX_FILE_TYPE_TAR_LZ4;
    tar_lz4->format.format_type = XX_TYPE_ARCHIVE;
    tar_lz4->format.is_archive = true;
    xx_format_set_mime_type(&tar_lz4->format, "application/x-lz4");
    xx_format_set_extension(&tar_lz4->format, "tar.lz4");
    tar_lz4->format.check_is_valid = xx_tar_lz4_check_is_valid;
    tar_lz4->format.handle_base_info = xx_tar_lz4_handle_base_info;
    tar_lz4->format.get_format_size = xx_tar_lz4_get_format_size;
    tar_lz4->format.get_number_of_archive_records =
        xx_tar_lz4_get_number_of_archive_records;
    tar_lz4->format.create_archive_records_reading =
        xx_tar_lz4_create_archive_records_reading;
    tar_lz4->format.get_current_archive_record =
        xx_tar_lz4_get_current_archive_record;
    tar_lz4->format.unpack_current_archive_record =
        xx_tar_lz4_unpack_current_archive_record;
    tar_lz4->format.archive_record_move_to_next =
        xx_tar_lz4_archive_record_move_to_next;
    tar_lz4->format.free_archive_records_reading =
        xx_tar_lz4_free_archive_records_reading;
    tar_lz4->format.create_archive_records_writing =
        xx_tar_lz4_create_archive_records_writing;
    tar_lz4->format.pack_archive_record = xx_tar_lz4_pack_archive_record;
    tar_lz4->format.finalize_archive_records_writing =
        xx_tar_lz4_finalize_archive_records_writing;
    tar_lz4->format.free_archive_records_writing =
        xx_tar_lz4_free_archive_records_writing;
    tar_lz4->format.data_struct_id_to_string =
        xx_tar_lz4_data_struct_id_to_string;
    tar_lz4->format.data_struct_string_to_id =
        xx_tar_lz4_data_struct_string_to_id;
    tar_lz4->format.create_data_structs_reading =
        xx_tar_lz4_create_data_structs_reading;
    tar_lz4->format.get_current_data_struct =
        xx_tar_lz4_get_current_data_struct;
    tar_lz4->format.data_struct_move_to_next =
        xx_tar_lz4_data_struct_move_to_next;
    tar_lz4->format.free_data_structs_reading =
        xx_tar_lz4_free_data_structs_reading;
    tar_lz4->format.create_data_struct_records_reading =
        xx_tar_lz4_create_data_struct_records_reading;
    tar_lz4->format.get_current_data_struct_record =
        xx_tar_lz4_get_current_data_struct_record;
    tar_lz4->format.data_struct_record_move_to_next =
        xx_tar_lz4_data_struct_record_move_to_next;
    tar_lz4->format.free_data_struct_records_reading =
        xx_tar_lz4_free_data_struct_records_reading;
    tar_lz4->format.destroy = xx_tar_lz4_vtable_destroy;
    tar_lz4->compressed_size = -1;
    tar_lz4->uncompressed_size = -1;
}

xx_tar_lz4 *xx_tar_lz4_create(xx_io_device *dev, int64_t base_address) {
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)xx_mem_alloc(sizeof(*tar_lz4));
    if (tar_lz4) xx_tar_lz4_init(tar_lz4, dev, base_address);
    return tar_lz4;
}

void xx_tar_lz4_destroy(xx_tar_lz4 *tar_lz4) {
    xx_tar_common *common;
    if (!tar_lz4) return;
    common = xx_tar_lz4_common(tar_lz4, false);
    if (common) {
        xx_tar_common_cleanup(common);
        xx_mem_free(common);
        tar_lz4->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&tar_lz4->format);
}

static void xx_tar_lz4_vtable_destroy(Abstractformat *self) {
    xx_tar_lz4_destroy((xx_tar_lz4 *)self);
}

void xx_tar_lz4_free(xx_tar_lz4 *tar_lz4) {
    if (!tar_lz4) return;
    xx_tar_lz4_destroy(tar_lz4);
    xx_mem_free(tar_lz4);
}

bool xx_tar_lz4_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)self;
    xx_tar_common *common = xx_tar_lz4_common(tar_lz4, true);
    return common && xx_tar_common_load(common, self, xx_tar_lz4_decode, pd);
}

bool xx_tar_lz4_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)self;
    xx_tar_common *common = xx_tar_lz4_common(tar_lz4, true);
    bool result = common && xx_tar_common_handle_base_info(
                                common, self, xx_tar_lz4_decode, pd);
    if (result) xx_tar_lz4_sync(tar_lz4, common);
    return result;
}

int64_t xx_tar_lz4_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)self;
    xx_tar_common *common = xx_tar_lz4_common(tar_lz4, true);
    int64_t result = common ? xx_tar_common_get_format_size(
        common, self, xx_tar_lz4_decode, pd) : -1;
    if (common && common->valid) xx_tar_lz4_sync(tar_lz4, common);
    return result;
}

uint64_t xx_tar_lz4_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)self;
    xx_tar_common *common = xx_tar_lz4_common(tar_lz4, true);
    uint64_t result = common ? xx_tar_common_get_number_of_archive_records(
        common, self, xx_tar_lz4_decode, pd) : 0U;
    if (common && common->valid) xx_tar_lz4_sync(tar_lz4, common);
    return result;
}

xx_archive_record_state *xx_tar_lz4_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_lz4_common((xx_tar_lz4 *)self, true);
    return common ? xx_tar_common_create_archive_records_reading(
        common, self, xx_tar_lz4_decode, options, pd) : NULL;
}

const xx_archive_record *xx_tar_lz4_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return xx_tar_common_get_current_archive_record(
        xx_tar_lz4_common((xx_tar_lz4 *)self, false), state);
}

bool xx_tar_lz4_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    return xx_tar_common_unpack_current_archive_record(
        xx_tar_lz4_common((xx_tar_lz4 *)self, false), state, pd);
}

bool xx_tar_lz4_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    return xx_tar_common_archive_record_move_to_next(
        xx_tar_lz4_common((xx_tar_lz4 *)self, false), state, pd);
}

void xx_tar_lz4_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    xx_tar_common_free_archive_records_reading(
        xx_tar_lz4_common((xx_tar_lz4 *)self, false), state);
}

xx_archive_write_state *xx_tar_lz4_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)self;
    xx_archive_write_state *state = xx_tar_common_create_archive_records_writing(
        self, options, xx_tar_lz4_encode, pd);
    xx_tar_common *common;
    if (!state) return NULL;
    common = xx_tar_lz4_common(tar_lz4, false);
    if (common) xx_tar_common_cleanup(common);
    tar_lz4->number_of_records = 0U;
    tar_lz4->number_of_members = 0U;
    tar_lz4->compressed_size = -1;
    tar_lz4->uncompressed_size = -1;
    return state;
}

bool xx_tar_lz4_pack_archive_record(
    Abstractformat *self, xx_archive_write_state *state,
    const xx_archive_record *record, xx_io_device *source_dev,
    xx_pd_struct *pd) {
    return xx_tar_common_pack_archive_record(self, state, record, source_dev,
                                             pd);
}

bool xx_tar_lz4_finalize_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd) {
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)self;
    xx_tar_common *common;
    if (!xx_tar_common_finalize_archive_records_writing(self, state, pd)) {
        return false;
    }
    common = xx_tar_lz4_common(tar_lz4, false);
    if (common) xx_tar_common_cleanup(common);
    tar_lz4->number_of_records = self->number_of_archive_records;
    tar_lz4->number_of_members = self->number_of_archive_records;
    tar_lz4->compressed_size = self->format_size;
    return true;
}

void xx_tar_lz4_free_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state) {
    xx_tar_common_free_archive_records_writing(self, state);
}

const char *xx_tar_lz4_data_struct_id_to_string(
    Abstractformat *self, uint32_t id) {
    return xx_tar_common_data_struct_id_to_string(
        xx_tar_lz4_common((xx_tar_lz4 *)self, true), id);
}

uint32_t xx_tar_lz4_data_struct_string_to_id(Abstractformat *self,
                                              const char *name) {
    return xx_tar_common_data_struct_string_to_id(
        xx_tar_lz4_common((xx_tar_lz4 *)self, true), name);
}

xx_data_struct_state *xx_tar_lz4_create_data_structs_reading(
    Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_lz4_common((xx_tar_lz4 *)self, true);
    return common ? xx_tar_common_create_data_structs_reading(
        common, self, xx_tar_lz4_decode, pd) : NULL;
}

const xx_data_struct *xx_tar_lz4_get_current_data_struct(
    Abstractformat *self, xx_data_struct_state *state) {
    return xx_tar_common_get_current_data_struct(
        xx_tar_lz4_common((xx_tar_lz4 *)self, false), state);
}

bool xx_tar_lz4_data_struct_move_to_next(
    Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd) {
    return xx_tar_common_data_struct_move_to_next(
        xx_tar_lz4_common((xx_tar_lz4 *)self, false), state, pd);
}

void xx_tar_lz4_free_data_structs_reading(
    Abstractformat *self, xx_data_struct_state *state) {
    xx_tar_common_free_data_structs_reading(
        xx_tar_lz4_common((xx_tar_lz4 *)self, false), state);
}

xx_data_struct_record_state *xx_tar_lz4_create_data_struct_records_reading(
    Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd) {
    return xx_tar_common_create_data_struct_records_reading(
        xx_tar_lz4_common((xx_tar_lz4 *)self, false), ds, pd);
}

const xx_data_struct_record *xx_tar_lz4_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state) {
    return xx_tar_common_get_current_data_struct_record(
        xx_tar_lz4_common((xx_tar_lz4 *)self, false), state);
}

bool xx_tar_lz4_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd) {
    return xx_tar_common_data_struct_record_move_to_next(
        xx_tar_lz4_common((xx_tar_lz4 *)self, false), state, pd);
}

void xx_tar_lz4_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state) {
    xx_tar_common_free_data_struct_records_reading(
        xx_tar_lz4_common((xx_tar_lz4 *)self, false), state);
}

uint64_t xx_tar_lz4_get_number_of_records(const xx_tar_lz4 *tar_lz4) {
    return tar_lz4 ? tar_lz4->number_of_records : 0U;
}

uint64_t xx_tar_lz4_get_number_of_members(const xx_tar_lz4 *tar_lz4) {
    return tar_lz4 ? tar_lz4->number_of_members : 0U;
}

int64_t xx_tar_lz4_get_compressed_size(const xx_tar_lz4 *tar_lz4) {
    return tar_lz4 ? tar_lz4->compressed_size : -1;
}

int64_t xx_tar_lz4_get_uncompressed_size(const xx_tar_lz4 *tar_lz4) {
    return tar_lz4 ? tar_lz4->uncompressed_size : -1;
}
