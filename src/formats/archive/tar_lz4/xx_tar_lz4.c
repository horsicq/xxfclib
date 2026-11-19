/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/archive/tar_lz4/xx_tar_lz4.h"
#include "xx_tar_lz4_private.h"

#include "../tar_common/xx_tar_common.h"
#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include "xxfclib/data/xx_data.h"

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

static uint32_t xx_tar_lz4_rotl32(uint32_t value, unsigned count)
{
    return (value << count) | (value >> (32U - count));
}

/* A compact local xxHash32 implementation used only for the LZ4 frame
 * descriptor checksum. It is independently written from the public format. */
static uint32_t xx_tar_lz4_xxh32(const uint8_t *data, size_t size)
{
    uint32_t hash = XX_TAR_LZ4_XXH_PRIME5 + (uint32_t)size;
    size_t offset = 0U;
    while (size - offset >= 4U) {
        hash += xx_data_get_u32(data + offset, 4, 0, false) * XX_TAR_LZ4_XXH_PRIME3;
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

static bool xx_tar_lz4_write_all(xx_io_device *device, const void *data, size_t size, xx_pd_struct *pd)
{
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

static bool xx_tar_lz4_read_all_at(xx_io_device *device, int64_t offset, void *data, size_t size)
{
    uint8_t *bytes = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 || offset > LONG_MAX || xx_io_seek(device, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t read = xx_io_read(device, bytes + done, size - done);
        if (read <= 0 || (size_t)read > size - done) return false;
        done += (size_t)read;
    }
    return true;
}

/* What xx_tar_lz4_declared_size finds of the frames. */
typedef struct xx_tar_lz4_walk_s {
    /* What their content sizes declare; for a frame without one, what its
     * blocks can hold at most. */
    size_t size;
    /* The input ends inside a frame (a cut file). */
    bool cut;
    /* A frame has no content size. */
    bool unsized;
    /* The whole frames, and the last one's offset and size. */
    size_t frames;
    size_t last_offset;
    size_t last_size;
} xx_tar_lz4_walk;

/* Walks the LZ4 frames without decoding their blocks: the TAR's allocation
 * is bounded before the decoder receives the compressed data. A frame
 * without a content size (lz4 by default, tar -I lz4) is bounded by its
 * blocks (a compressed one decodes to at most its frame's block size),
 * within the same maximum. When the input ends inside a frame (a cut file),
 * true with walk->cut set: the size bounds the frames up to and including
 * it. */
static bool xx_tar_lz4_declared_size(const uint8_t *source, size_t size, xx_tar_lz4_walk *walk)
{
    const uint8_t *cursor = source;
    const uint8_t *end;
    uint64_t total = 0U;
    xx_mem_zero(walk, sizeof(*walk));
    if (!source || size < 4U || xx_data_get_u32(source, 4, 0, false) != UINT32_C(0x184D2204)) return false;
    end = source + size;
/* The input ends here, inside a frame. */
#define XX_TAR_LZ4_ENDED() \
    do {                   \
        walk->cut = true;  \
        goto counted;      \
    } while (0)
    while (cursor < end) {
        uint32_t magic;
        uint8_t flags;
        uint8_t block_descriptor;
        uint64_t block_limit;
        uint64_t frame_start = total;
        size_t frame_offset = (size_t)(cursor - source);
        if ((size_t)(end - cursor) < 4U) XX_TAR_LZ4_ENDED();
        magic = xx_data_get_u32(cursor, 4, 0, false);
        cursor += 4U;
        if ((magic & UINT32_C(0xFFFFFFF0)) == UINT32_C(0x184D2A50)) {
            uint32_t skipped;
            if ((size_t)(end - cursor) < 4U) XX_TAR_LZ4_ENDED();
            skipped = xx_data_get_u32(cursor, 4, 0, false);
            cursor += 4U;
            if ((size_t)(end - cursor) < (size_t)skipped) XX_TAR_LZ4_ENDED();
            cursor += skipped;
            continue;
        }
        if (magic != UINT32_C(0x184D2204)) return false;
        if ((size_t)(end - cursor) < 2U) XX_TAR_LZ4_ENDED();
        flags = cursor[0];
        block_descriptor = cursor[1];
        cursor += 2U;
        if ((flags >> 6U) != 1U || (flags & UINT8_C(0x02)) != 0U || (flags & UINT8_C(0x01)) != 0U || (block_descriptor & UINT8_C(0x8F)) != 0U ||
            ((block_descriptor >> 4U) & 7U) < 4U) {
            return false;
        }
        block_limit = (uint64_t)XX_TAR_LZ4_BLOCK_SIZE << (2U * (((block_descriptor >> 4U) & 7U) - 4U));
        if ((flags & UINT8_C(0x08)) != 0U) {
            uint64_t declared;
            if ((size_t)(end - cursor) < 8U) XX_TAR_LZ4_ENDED();
            declared = xx_data_get_u64(cursor, 8, 0, false);
            cursor += 8U;
            if (declared > (uint64_t)XX_TAR_LZ4_MAX_BUFFER - total) return false;
            total += declared;
        } else {
            walk->unsized = true;
        }
        if (cursor == end) XX_TAR_LZ4_ENDED();
        ++cursor; /* The descriptor's checksum, which the decoder checks. */
        for (;;) {
            uint32_t stored;
            uint64_t block_size;
            if ((size_t)(end - cursor) < 4U) XX_TAR_LZ4_ENDED();
            stored = xx_data_get_u32(cursor, 4, 0, false);
            cursor += 4U;
            if (stored == 0U) break;
            block_size = stored & UINT32_C(0x7FFFFFFF);
            if (block_size == 0U || block_size > block_limit) return false;
            if ((flags & UINT8_C(0x08)) == 0U) {
                /* An uncompressed block holds its size, a compressed one at
                 * most the block size. */
                uint64_t most = (stored & UINT32_C(0x80000000)) != 0U ? block_size : block_limit;
                if (most > (uint64_t)XX_TAR_LZ4_MAX_BUFFER - total) return false;
                total += most;
            }
            if ((uint64_t)(end - cursor) < block_size) XX_TAR_LZ4_ENDED();
            cursor += block_size;
            if ((flags & UINT8_C(0x10)) != 0U) {
                if ((size_t)(end - cursor) < 4U) XX_TAR_LZ4_ENDED();
                cursor += 4U;
            }
        }
        if ((flags & UINT8_C(0x04)) != 0U) {
            if ((size_t)(end - cursor) < 4U) XX_TAR_LZ4_ENDED();
            cursor += 4U;
        }
        ++walk->frames;
        walk->last_offset = frame_offset;
        walk->last_size = (size_t)(total - frame_start);
    }
#undef XX_TAR_LZ4_ENDED
    if (walk->frames == 0U) return false;
counted:
    if (total == 0U || total > (uint64_t)SIZE_MAX) return false;
    walk->size = (size_t)total;
    return true;
}

/* Whether the whole frame at frame, of at most size bytes decoded, may end a
 * cut TAR: it does not end as a whole TAR does, in 1024 zero bytes (its end
 * marker, or the zeros after it). */
static bool xx_tar_lz4_may_end_cut(const uint8_t *frame, size_t frame_size, size_t size)
{
    uint8_t *decoded = (uint8_t *)xx_mem_alloc(size ? size : 1U);
    size_t written = 0U;
    bool result = false;
    if (decoded && xx_lz4_decompress_frames(frame, frame_size, decoded, size, &written)) {
        size_t i = written < 1024U ? 0U : written - 1024U;
        result = written < 1024U;
        while (!result && i < written) result = decoded[i++] != 0U;
    }
    xx_mem_free(decoded);
    return result;
}

xx_tar_common *xx_tar_lz4_common(xx_tar_lz4 *tar_lz4, bool create)
{
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

static void xx_tar_lz4_sync(xx_tar_lz4 *tar_lz4, const xx_tar_common *common)
{
    if (!tar_lz4 || !common || !common->valid || !common->tar) return;
    tar_lz4->number_of_records = xx_tar_get_number_of_records(common->tar);
    tar_lz4->number_of_members = xx_tar_get_number_of_members(common->tar);
    tar_lz4->compressed_size = common->compressed_size;
    tar_lz4->uncompressed_size = (uint64_t)common->decoded_size <= (uint64_t)INT64_MAX ? (int64_t)common->decoded_size : -1;
}

bool xx_tar_lz4_decode(Abstractformat *outer, xx_io_device *destination, int64_t *compressed_size, xx_pd_struct *pd)
{
    int64_t total;
    int64_t size_i64;
    uint8_t *compressed = NULL;
    uint8_t *decoded = NULL;
    xx_tar_lz4_walk walk;
    size_t written = 0U;
    size_t checked = 0U;
    bool cut = false;
    bool result = false;
    if (compressed_size) *compressed_size = -1;
    if (!outer || !outer->device || !destination || !compressed_size || outer->base_address < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total = xx_io_total_size(outer->device);
    if (total <= outer->base_address || total - outer->base_address > (int64_t)XX_TAR_LZ4_MAX_BUFFER || (uint64_t)(total - outer->base_address) > (uint64_t)SIZE_MAX) {
        return false;
    }
    size_i64 = total - outer->base_address;
    compressed = (uint8_t *)xx_mem_alloc((size_t)size_i64);
    if (!compressed || !xx_tar_lz4_read_all_at(outer->device, outer->base_address, compressed, (size_t)size_i64) ||
        !xx_tar_lz4_declared_size(compressed, (size_t)size_i64, &walk)) {
        goto cleanup;
    }
    /* A whole stream is one frame with a content size, as our writer and lz4
     * --content-size write it; any other is read as a nested layer, as it
     * always was, unless it is several whole frames whose TAR is cut (a file
     * cut where a frame ends): when their last frame ends as a whole TAR
     * does, or their TAR is whole, they are left to it. */
    if (!walk.cut && (walk.unsized || walk.frames > 1U) &&
        (walk.frames < 2U || !xx_tar_lz4_may_end_cut(compressed + walk.last_offset, (size_t)size_i64 - walk.last_offset, walk.last_size))) {
        goto cleanup;
    }
    decoded = (uint8_t *)xx_mem_alloc(walk.size);
    if (!decoded) goto cleanup;
    /* The size only bounds what a frame without a content size holds. */
    if (!xx_lz4_decompress_memory_prefix(compressed, (size_t)size_i64, decoded, walk.size, &written, &cut, &checked)) {
        /* A cut file: its decoded prefix, when it starts a TAR (anything
         * else is a cut stream of its own type). */
        if (cut && written >= 512U && xx_tar_block_is_header(decoded) && xx_tar_lz4_write_all(destination, decoded, written, pd)) {
            xx_tar_common_stream_checked(destination, checked);
            xx_tar_common_stream_cut(destination);
        }
        goto cleanup;
    }
    if ((!walk.unsized && written != walk.size) || (walk.frames > 1U && xx_tar_common_is_whole_tar(decoded, written, pd)) ||
        !xx_tar_lz4_write_all(destination, decoded, written, pd)) {
        goto cleanup;
    }
    xx_tar_common_stream_checked(destination, checked);
    *compressed_size = size_i64;
    result = true;
cleanup:
    if (decoded) xx_mem_free(decoded);
    if (compressed) xx_mem_free(compressed);
    return result;
}

static bool xx_tar_lz4_encode(Abstractformat *outer, const xx_list_s *options, xx_io_device *tar_source, int64_t tar_size, int64_t *compressed_size, xx_pd_struct *pd)
{
    uint8_t header[XX_TAR_LZ4_HEADER_SIZE] = {0};
    uint8_t buffer[XX_TAR_LZ4_BLOCK_SIZE];
    uint8_t block_size[4];
    int64_t remaining;
    int64_t total_size = (int64_t)XX_TAR_LZ4_HEADER_SIZE + 4;
    (void)options;
    if (compressed_size) *compressed_size = -1;
    if (!outer || !outer->device || !outer->device->write || !outer->device->seek || !tar_source || !compressed_size || tar_size <= 0 || outer->base_address < 0 ||
        outer->base_address > LONG_MAX || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    header[0] = 0x04U;
    header[1] = 0x22U;
    header[2] = 0x4dU;
    header[3] = 0x18U;
    header[4] = 0x68U; /* version 1, independent blocks, content size */
    header[5] = 0x40U; /* 64 KiB maximum block size */
    xx_data_set_u64(header + 6U, 8, 0, (uint64_t)tar_size, false);
    header[14] = (uint8_t)(xx_tar_lz4_xxh32(header + 4U, 10U) >> 8U);
    if (xx_io_seek(outer->device, (long)outer->base_address, SEEK_SET) != 0 || !xx_tar_lz4_write_all(outer->device, header, sizeof(header), pd)) {
        return false;
    }
    remaining = tar_size;
    while (remaining != 0) {
        size_t requested = remaining > (int64_t)sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        ssize_t read;
        if (pd && xx_pd_is_stopped(pd)) return false;
        read = xx_io_read(tar_source, buffer, requested);
        if (read <= 0 || (size_t)read > requested || read > INT32_MAX || total_size > INT64_MAX - 4 - read) {
            return false;
        }
        xx_data_set_u32(block_size, 4, 0, UINT32_C(0x80000000) | (uint32_t)read, false);
        if (!xx_tar_lz4_write_all(outer->device, block_size, sizeof(block_size), pd) || !xx_tar_lz4_write_all(outer->device, buffer, (size_t)read, pd)) {
            return false;
        }
        total_size += 4 + read;
        remaining -= read;
    }
    xx_mem_zero(block_size, sizeof(block_size));
    if (!xx_tar_lz4_write_all(outer->device, block_size, sizeof(block_size), pd)) {
        return false;
    }
    *compressed_size = total_size;
    ((xx_tar_lz4 *)outer)->compressed_size = total_size;
    ((xx_tar_lz4 *)outer)->uncompressed_size = tar_size;
    return true;
}

void xx_tar_lz4_init(xx_tar_lz4 *tar_lz4, xx_io_device *dev, int64_t base_address)
{
    if (!tar_lz4) return;
    xx_mem_zero(tar_lz4, sizeof(*tar_lz4));
    xx_format_init(&tar_lz4->format, dev, base_address);
    tar_lz4->format.endian = XX_ENDIAN_UNKNOWN;
    tar_lz4->format.file_type = XX_FILE_TYPE_TAR_LZ4;
    tar_lz4->format.handle_type = XX_FILE_TYPE_TAR_LZ4;
    tar_lz4->format.format_type = XX_TYPE_ARCHIVE;
    tar_lz4->format.is_archive = true;
    xx_format_set_mime_type(&tar_lz4->format, "application/x-lz4");
    xx_format_set_extension(&tar_lz4->format, "tar.lz4");
    tar_lz4->format.check_is_valid = xx_tar_lz4_check_is_valid;
    tar_lz4->format.handle_base_info = xx_tar_lz4_handle_base_info;
    tar_lz4->format.get_format_size = xx_tar_lz4_get_format_size;
    tar_lz4->format.get_number_of_archive_records = xx_tar_lz4_get_number_of_archive_records;
    tar_lz4->format.create_archive_records_reading = xx_tar_lz4_create_archive_records_reading;
    tar_lz4->format.get_current_archive_record = xx_tar_lz4_get_current_archive_record;
    tar_lz4->format.unpack_current_archive_record = xx_tar_lz4_unpack_current_archive_record;
    tar_lz4->format.archive_record_move_to_next = xx_tar_lz4_archive_record_move_to_next;
    tar_lz4->format.free_archive_records_reading = xx_tar_lz4_free_archive_records_reading;
    tar_lz4->format.create_archive_records_writing = xx_tar_lz4_create_archive_records_writing;
    tar_lz4->format.pack_archive_record = xx_tar_lz4_pack_archive_record;
    tar_lz4->format.finalize_archive_records_writing = xx_tar_lz4_finalize_archive_records_writing;
    tar_lz4->format.free_archive_records_writing = xx_tar_lz4_free_archive_records_writing;
    tar_lz4->format.destroy = xx_tar_lz4_vtable_destroy;
    tar_lz4->compressed_size = -1;
    tar_lz4->uncompressed_size = -1;
}

xx_tar_lz4 *xx_tar_lz4_create(xx_io_device *dev, int64_t base_address)
{
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)xx_mem_alloc(sizeof(*tar_lz4));
    if (tar_lz4) xx_tar_lz4_init(tar_lz4, dev, base_address);
    return tar_lz4;
}

void xx_tar_lz4_destroy(xx_tar_lz4 *tar_lz4)
{
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

static void xx_tar_lz4_vtable_destroy(Abstractformat *self)
{
    xx_tar_lz4_destroy((xx_tar_lz4 *)self);
}

void xx_tar_lz4_free(xx_tar_lz4 *tar_lz4)
{
    if (!tar_lz4) return;
    xx_tar_lz4_destroy(tar_lz4);
    xx_mem_free(tar_lz4);
}

bool xx_tar_lz4_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)self;
    xx_tar_common *common = xx_tar_lz4_common(tar_lz4, true);
    return common && xx_tar_common_load(common, self, xx_tar_lz4_decode, pd);
}

bool xx_tar_lz4_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)self;
    xx_tar_common *common = xx_tar_lz4_common(tar_lz4, true);
    bool result = common && xx_tar_common_handle_base_info(common, self, xx_tar_lz4_decode, pd);
    if (result) xx_tar_lz4_sync(tar_lz4, common);
    return result;
}

int64_t xx_tar_lz4_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)self;
    xx_tar_common *common = xx_tar_lz4_common(tar_lz4, true);
    int64_t result = common ? xx_tar_common_get_format_size(common, self, xx_tar_lz4_decode, pd) : -1;
    if (common && common->valid) xx_tar_lz4_sync(tar_lz4, common);
    return result;
}

uint64_t xx_tar_lz4_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)self;
    xx_tar_common *common = xx_tar_lz4_common(tar_lz4, true);
    uint64_t result = common ? xx_tar_common_get_number_of_archive_records(common, self, xx_tar_lz4_decode, pd) : 0U;
    if (common && common->valid) xx_tar_lz4_sync(tar_lz4, common);
    return result;
}

xx_archive_record_state *xx_tar_lz4_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_tar_common *common = xx_tar_lz4_common((xx_tar_lz4 *)self, true);
    return common ? xx_tar_common_create_archive_records_reading(common, self, xx_tar_lz4_decode, options, pd) : NULL;
}

const xx_archive_record *xx_tar_lz4_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return xx_tar_common_get_current_archive_record(xx_tar_lz4_common((xx_tar_lz4 *)self, false), state);
}

bool xx_tar_lz4_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    return xx_tar_common_unpack_current_archive_record(xx_tar_lz4_common((xx_tar_lz4 *)self, false), state, pd);
}

bool xx_tar_lz4_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    return xx_tar_common_archive_record_move_to_next(xx_tar_lz4_common((xx_tar_lz4 *)self, false), state, pd);
}

void xx_tar_lz4_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    xx_tar_common_free_archive_records_reading(xx_tar_lz4_common((xx_tar_lz4 *)self, false), state);
}

xx_archive_write_state *xx_tar_lz4_create_archive_records_writing(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_tar_lz4 *tar_lz4 = (xx_tar_lz4 *)self;
    xx_archive_write_state *state = xx_tar_common_create_archive_records_writing(self, options, xx_tar_lz4_encode, pd);
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

bool xx_tar_lz4_pack_archive_record(Abstractformat *self, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source_dev, xx_pd_struct *pd)
{
    return xx_tar_common_pack_archive_record(self, state, record, source_dev, pd);
}

bool xx_tar_lz4_finalize_archive_records_writing(Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd)
{
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

void xx_tar_lz4_free_archive_records_writing(Abstractformat *self, xx_archive_write_state *state)
{
    xx_tar_common_free_archive_records_writing(self, state);
}

uint64_t xx_tar_lz4_get_number_of_records(const xx_tar_lz4 *tar_lz4)
{
    return tar_lz4 ? tar_lz4->number_of_records : 0U;
}

uint64_t xx_tar_lz4_get_number_of_members(const xx_tar_lz4 *tar_lz4)
{
    return tar_lz4 ? tar_lz4->number_of_members : 0U;
}

int64_t xx_tar_lz4_get_compressed_size(const xx_tar_lz4 *tar_lz4)
{
    return tar_lz4 ? tar_lz4->compressed_size : -1;
}

int64_t xx_tar_lz4_get_uncompressed_size(const xx_tar_lz4 *tar_lz4)
{
    return tar_lz4 ? tar_lz4->uncompressed_size : -1;
}
