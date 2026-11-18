/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/archive/tar_zstd/xx_tar_zstd.h"
#include "xx_tar_zstd_datastructs_internal.h"

#include "../tar_common/xx_tar_common.h"
#include "../../../algo/zstd/xx_zstd_dec.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include "xxfclib/data/xx_data.h"

#define XX_TAR_ZSTD_MAGIC UINT32_C(0xFD2FB528)
#define XX_TAR_ZSTD_SKIP_MAGIC UINT32_C(0x184D2A50)
#define XX_TAR_ZSTD_BLOCK_MAX (128U * 1024U)
#define XX_TAR_ZSTD_MAX_BUFFER ((size_t)1024U * 1024U * 1024U)

static void xx_tar_zstd_vtable_destroy(Abstractformat *self);

static bool xx_tar_zstd_take(const uint8_t **cursor, const uint8_t *end,
                             size_t count) {
    if (!cursor || !*cursor || !end || (size_t)(end - *cursor) < count) {
        return false;
    }
    *cursor += count;
    return true;
}

static bool xx_tar_zstd_read_variable(const uint8_t **cursor,
                                      const uint8_t *end, unsigned count,
                                      uint64_t *value) {
    uint64_t result = 0U;
    unsigned index;
    if (!cursor || !value || count > 8U ||
        (size_t)(end - *cursor) < count) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        result |= (uint64_t)(*cursor)[index] << (8U * index);
    }
    *cursor += count;
    *value = result;
    return true;
}

/* What xx_tar_zstd_declared_size finds of the frames. */
typedef struct xx_tar_zstd_walk_s {
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
} xx_tar_zstd_walk;

/* Walks complete standard/skippable Zstandard frames without decoding blocks:
 * the logical TAR allocation is bounded before the decoder receives
 * attacker-controlled compressed data. A frame without a content size (tar
 * --zstd, zstd in a pipe) is bounded by its blocks (a compressed one decodes
 * to at most 128 KiB), within the same maximum. When the input ends inside a
 * frame (a cut file), true with walk->cut set: the size bounds the frames up
 * to and including it. */
static bool xx_tar_zstd_declared_size(const uint8_t *source, size_t size, xx_tar_zstd_walk *walk)
{
    const uint8_t *cursor = source;
    const uint8_t *end;
    uint64_t total = 0U;

    xx_mem_zero(walk, sizeof(*walk));
    if (!source || size < 4U) return false;
    end = source + size;
/* Whether the input ends here; else not a frame this walk takes. */
#define XX_TAR_ZSTD_ENDED() \
    do {                    \
        walk->cut = true;   \
        goto counted;       \
    } while (0)
    while (cursor < end) {
        uint32_t magic;
        uint8_t descriptor;
        unsigned content_size_flag;
        unsigned dictionary_flag;
        unsigned dictionary_size;
        unsigned content_size_bytes;
        bool single_segment;
        bool checksum;
        uint64_t ignored = 0U;
        uint64_t frame_size = 0U;
        uint64_t frame_start = total;
        size_t frame_offset = (size_t)(cursor - source);
        bool last_block = false;

        if ((size_t)(end - cursor) < 4U) XX_TAR_ZSTD_ENDED();
        magic = xx_data_get_u32(cursor, 4, 0, false);
        cursor += 4U;
        if ((magic & UINT32_C(0xFFFFFFF0)) == XX_TAR_ZSTD_SKIP_MAGIC) {
            uint32_t skipped_size;
            if ((size_t)(end - cursor) < 4U) XX_TAR_ZSTD_ENDED();
            skipped_size = xx_data_get_u32(cursor, 4, 0, false);
            cursor += 4U;
            if (!xx_tar_zstd_take(&cursor, end, (size_t)skipped_size)) {
                XX_TAR_ZSTD_ENDED();
            }
            continue;
        }
        if (magic != XX_TAR_ZSTD_MAGIC) return false;
        if (cursor == end) XX_TAR_ZSTD_ENDED();

        descriptor = *cursor++;
        if ((descriptor & UINT8_C(0x18)) != 0U) return false;
        content_size_flag = descriptor >> 6U;
        single_segment = (descriptor & UINT8_C(0x20)) != 0U;
        checksum = (descriptor & UINT8_C(0x04)) != 0U;
        dictionary_flag = descriptor & UINT8_C(0x03);
        if (!single_segment && !xx_tar_zstd_take(&cursor, end, 1U)) {
            XX_TAR_ZSTD_ENDED();
        }
        dictionary_size = dictionary_flag == 0U ? 0U :
                          dictionary_flag == 1U ? 1U :
                          dictionary_flag == 2U ? 2U : 4U;
        if (!xx_tar_zstd_read_variable(&cursor, end, dictionary_size,
                                       &ignored)) {
            XX_TAR_ZSTD_ENDED();
        }
        content_size_bytes = content_size_flag == 0U ?
                                 (single_segment ? 1U : 0U) :
                             content_size_flag == 1U ? 2U :
                             content_size_flag == 2U ? 4U : 8U;
        if (content_size_bytes == 0U) {
            walk->unsized = true;
        } else {
            if (!xx_tar_zstd_read_variable(&cursor, end, content_size_bytes, &frame_size)) {
                XX_TAR_ZSTD_ENDED();
            }
            if (content_size_bytes == 2U) frame_size += 256U;
            if (frame_size > (uint64_t)XX_TAR_ZSTD_MAX_BUFFER || total > (uint64_t)XX_TAR_ZSTD_MAX_BUFFER - frame_size) {
                return false;
            }
            total += frame_size;
        }

        while (!last_block) {
            uint32_t header;
            unsigned block_type;
            size_t block_size;
            size_t encoded_size;
            if ((size_t)(end - cursor) < 3U) XX_TAR_ZSTD_ENDED();
            header = xx_data_get_u24(cursor, 3, 0, false);
            cursor += 3U;
            last_block = (header & 1U) != 0U;
            block_type = (header >> 1U) & 3U;
            block_size = (size_t)(header >> 3U);
            if (block_type == 3U || block_size > XX_TAR_ZSTD_BLOCK_MAX) {
                return false;
            }
            if (content_size_bytes == 0U) {
                /* A raw or RLE block holds its size, a compressed one at
                 * most the largest block. */
                uint64_t most = block_type == 2U ? (uint64_t)XX_TAR_ZSTD_BLOCK_MAX : (uint64_t)block_size;
                if (total > (uint64_t)XX_TAR_ZSTD_MAX_BUFFER - most) return false;
                total += most;
            }
            encoded_size = block_type == 1U ? 1U : block_size;
            if (!xx_tar_zstd_take(&cursor, end, encoded_size)) XX_TAR_ZSTD_ENDED();
        }
        if (checksum && !xx_tar_zstd_take(&cursor, end, 4U)) XX_TAR_ZSTD_ENDED();
        ++walk->frames;
        walk->last_offset = frame_offset;
        walk->last_size = (size_t)(total - frame_start);
    }
#undef XX_TAR_ZSTD_ENDED
    if (walk->frames == 0U) return false;
counted:
    if (total == 0U || total > (uint64_t)SIZE_MAX) {
        return false;
    }
    walk->size = (size_t)total;
    return true;
}

/* Whether the whole frame at frame, of at most size bytes decoded, may end a
 * cut TAR: it does not end as a whole TAR does, in 1024 zero bytes (its end
 * marker, or the zeros after it). */
static bool xx_tar_zstd_may_end_cut(const uint8_t *frame, size_t frame_size, size_t size)
{
    uint8_t *decoded = (uint8_t *)xx_mem_alloc(size ? size : 1U);
    size_t written = 0U;
    bool result = false;
    if (decoded && xx_zstd_decode_frames_bounded(frame, frame_size, decoded, size, &written)) {
        size_t i = written < 1024U ? 0U : written - 1024U;
        result = written < 1024U;
        while (!result && i < written) result = decoded[i++] != 0U;
    }
    xx_mem_free(decoded);
    return result;
}

static bool xx_tar_zstd_read_all_at(xx_io_device *device, int64_t offset,
                                    void *data, size_t size) {
    uint8_t *bytes = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t read = xx_io_read(device, bytes + done, size - done);
        if (read <= 0 || (size_t)read > size - done) return false;
        done += (size_t)read;
    }
    return true;
}

static bool xx_tar_zstd_write_all(xx_io_device *device, const uint8_t *data, size_t size, xx_pd_struct *pd)
{
    while (size != 0U) {
        size_t chunk = size < (size_t)65536U ? size : (size_t)65536U;
        ssize_t written;
        if (pd && xx_pd_is_stopped(pd)) return false;
        written = xx_io_write(device, data, chunk);
        if (written <= 0 || (size_t)written > chunk) return false;
        data += (size_t)written;
        size -= (size_t)written;
    }
    return true;
}

xx_tar_common *xx_tar_zstd_common(xx_tar_zstd *tar_zstd,
                                         bool create) {
    xx_tar_common *common;
    if (!tar_zstd) return NULL;
    common = (xx_tar_common *)tar_zstd->internal;
    if (!common && create) {
        common = (xx_tar_common *)xx_mem_alloc(sizeof(*common));
        if (!common) return NULL;
        xx_tar_common_init(common);
        tar_zstd->internal = common;
    }
    return common;
}

static void xx_tar_zstd_sync(xx_tar_zstd *tar_zstd,
                             const xx_tar_common *common) {
    if (!tar_zstd || !common || !common->valid || !common->tar) return;
    tar_zstd->number_of_records = xx_tar_get_number_of_records(common->tar);
    tar_zstd->number_of_members = xx_tar_get_number_of_members(common->tar);
    tar_zstd->uncompressed_size = (uint64_t)common->decoded_size;
    tar_zstd->compressed_size = common->compressed_size;
}

bool xx_tar_zstd_decode(Abstractformat *outer,
                               xx_io_device *destination,
                               int64_t *compressed_size,
                               xx_pd_struct *pd) {
    int64_t total_size;
    int64_t transport_size;
    uint8_t *transport = NULL;
    uint8_t *decoded = NULL;
    xx_tar_zstd_walk walk;
    size_t written = 0U;
    size_t checked = 0U;
    bool cut = false;
    bool result = false;

    if (compressed_size) *compressed_size = -1;
    if (!outer || !outer->device || !destination || !compressed_size ||
        outer->base_address < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(outer->device);
    if (total_size <= outer->base_address) return false;
    transport_size = total_size - outer->base_address;
    if ((uint64_t)transport_size > (uint64_t)XX_TAR_ZSTD_MAX_BUFFER ||
        (uint64_t)transport_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    transport = (uint8_t *)xx_mem_alloc((size_t)transport_size);
    if (!transport || !xx_tar_zstd_read_all_at(outer->device, outer->base_address, transport, (size_t)transport_size) ||
        !xx_tar_zstd_declared_size(transport, (size_t)transport_size, &walk)) {
        goto cleanup;
    }
    /* A whole stream with a frame without a content size is read as a
     * nested layer, as it always was, unless it is several whole frames
     * whose TAR is cut (a file cut where a frame ends, as pzstd writes
     * them): when their last frame ends as a whole TAR does, or their TAR is
     * whole, they are left to it. */
    if (!walk.cut && walk.unsized &&
        (walk.frames < 2U || !xx_tar_zstd_may_end_cut(transport + walk.last_offset, (size_t)transport_size - walk.last_offset, walk.last_size))) {
        goto cleanup;
    }
    /* Decoded from the input read above: one read, one pass. */
    decoded = (uint8_t *)xx_mem_alloc(walk.size);
    if (!decoded || (pd && xx_pd_is_stopped(pd))) goto cleanup;
    /* The size only bounds what a frame without a content size holds. */
    if (xx_zstd_decode_frames_prefix(transport, (size_t)transport_size, decoded, walk.size, &written, &cut, &checked)) {
        if ((walk.unsized ? xx_tar_common_is_whole_tar(decoded, written, pd) : written != walk.size) || !xx_tar_zstd_write_all(destination, decoded, written, pd)) {
            goto cleanup;
        }
        xx_tar_common_stream_checked(destination, checked);
        *compressed_size = transport_size;
        result = true;
    } else if (cut && written >= 512U && xx_tar_block_is_header(decoded) && xx_tar_zstd_write_all(destination, decoded, written, pd)) {
        /* Only a TAR's start is cached: anything else is a cut stream of
         * its own type. */
        xx_tar_common_stream_checked(destination, checked);
        xx_tar_common_stream_cut(destination);
    }

cleanup:
    xx_mem_free(decoded);
    xx_mem_free(transport);
    return result;
}

static const xx_var *xx_tar_zstd_find_option(const xx_list_s *options,
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

static int xx_tar_zstd_compression_level(const xx_list_s *options) {
    const xx_var *value = xx_tar_zstd_find_option(
        options, XX_META_ID_OPT_COMPRESSION_LEVEL);
    int64_t requested;
    if (!value) return XX_ZSTD_LEVEL_DEFAULT;
    requested = xx_var_get_i64(value);
    if (requested < XX_ZSTD_LEVEL_FASTEST) return XX_ZSTD_LEVEL_FASTEST;
    if (requested > XX_ZSTD_LEVEL_BEST) return XX_ZSTD_LEVEL_BEST;
    return (int)requested;
}

static bool xx_tar_zstd_encode(Abstractformat *outer,
                               const xx_list_s *options,
                               xx_io_device *tar_source, int64_t tar_size,
                               int64_t *compressed_size,
                               xx_pd_struct *pd) {
    int64_t end_position;
    int level;
    if (compressed_size) *compressed_size = -1;
    if (!outer || !outer->device || !outer->device->write ||
        !outer->device->seek || !tar_source || !compressed_size ||
        tar_size <= 0 || outer->base_address < 0 ||
        (uint64_t)tar_size > (uint64_t)XX_TAR_ZSTD_MAX_BUFFER ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    if (xx_io_seek64(outer->device, outer->base_address, SEEK_SET) != 0) {
        return false;
    }
    level = xx_tar_zstd_compression_level(options);
    if (!xx_zstd_pack_device(tar_source, 0, tar_size, outer->device, level,
                             pd)) {
        return false;
    }
    end_position = xx_io_tell(outer->device);
    if (end_position <= outer->base_address) return false;
    *compressed_size = end_position - outer->base_address;
    ((xx_tar_zstd *)outer)->compressed_size = *compressed_size;
    ((xx_tar_zstd *)outer)->uncompressed_size = (uint64_t)tar_size;
    return true;
}

void xx_tar_zstd_init(xx_tar_zstd *tar_zstd, xx_io_device *dev,
                      int64_t base_address) {
    if (!tar_zstd) return;
    xx_mem_zero(tar_zstd, sizeof(*tar_zstd));
    xx_format_init(&tar_zstd->format, dev, base_address);
    tar_zstd->format.endian = XX_ENDIAN_LITTLE;
    tar_zstd->format.file_type = XX_FILE_TYPE_TAR_ZSTD;
    tar_zstd->format.handle_type = XX_FILE_TYPE_TAR_ZSTD;
    tar_zstd->format.format_type = XX_TYPE_ARCHIVE;
    tar_zstd->format.is_archive = true;
    xx_format_set_mime_type(&tar_zstd->format,
                            "application/zstd-compressed-tar");
    xx_format_set_extension(&tar_zstd->format, "tar.zst");
    tar_zstd->format.check_is_valid = xx_tar_zstd_check_is_valid;
    tar_zstd->format.handle_base_info = xx_tar_zstd_handle_base_info;
    tar_zstd->format.get_format_size = xx_tar_zstd_get_format_size;
    tar_zstd->format.get_number_of_archive_records =
        xx_tar_zstd_get_number_of_archive_records;
    tar_zstd->format.create_archive_records_reading =
        xx_tar_zstd_create_archive_records_reading;
    tar_zstd->format.get_current_archive_record =
        xx_tar_zstd_get_current_archive_record;
    tar_zstd->format.unpack_current_archive_record =
        xx_tar_zstd_unpack_current_archive_record;
    tar_zstd->format.archive_record_move_to_next =
        xx_tar_zstd_archive_record_move_to_next;
    tar_zstd->format.free_archive_records_reading =
        xx_tar_zstd_free_archive_records_reading;
    tar_zstd->format.create_archive_records_writing =
        xx_tar_zstd_create_archive_records_writing;
    tar_zstd->format.pack_archive_record = xx_tar_zstd_pack_archive_record;
    tar_zstd->format.finalize_archive_records_writing =
        xx_tar_zstd_finalize_archive_records_writing;
    tar_zstd->format.free_archive_records_writing =
        xx_tar_zstd_free_archive_records_writing;
    tar_zstd->format.destroy = xx_tar_zstd_vtable_destroy;
    tar_zstd->compressed_size = -1;
}

xx_tar_zstd *xx_tar_zstd_create(xx_io_device *dev, int64_t base_address) {
    xx_tar_zstd *tar_zstd = (xx_tar_zstd *)xx_mem_alloc(sizeof(*tar_zstd));
    if (tar_zstd) xx_tar_zstd_init(tar_zstd, dev, base_address);
    return tar_zstd;
}

void xx_tar_zstd_destroy(xx_tar_zstd *tar_zstd) {
    xx_tar_common *common;
    if (!tar_zstd) return;
    common = xx_tar_zstd_common(tar_zstd, false);
    if (common) {
        xx_tar_common_cleanup(common);
        xx_mem_free(common);
        tar_zstd->internal = NULL;
    }
    if (tar_zstd->format.close) tar_zstd->format.close(&tar_zstd->format);
    xx_format_cleanup_extra_parameters(&tar_zstd->format);
}

static void xx_tar_zstd_vtable_destroy(Abstractformat *self) {
    xx_tar_zstd_destroy((xx_tar_zstd *)self);
}

void xx_tar_zstd_free(xx_tar_zstd *tar_zstd) {
    if (!tar_zstd) return;
    xx_tar_zstd_destroy(tar_zstd);
    xx_mem_free(tar_zstd);
}

bool xx_tar_zstd_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_zstd *tar_zstd = (xx_tar_zstd *)self;
    xx_tar_common *common = xx_tar_zstd_common(tar_zstd, true);
    bool result;
    if (!self || !common) return false;
    result = xx_tar_common_load(common, self, xx_tar_zstd_decode, pd);
    if (result) xx_tar_zstd_sync(tar_zstd, common);
    return result;
}

bool xx_tar_zstd_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_zstd *tar_zstd = (xx_tar_zstd *)self;
    xx_tar_common *common = xx_tar_zstd_common(tar_zstd, true);
    if (!self || !common ||
        !xx_tar_common_handle_base_info(common, self, xx_tar_zstd_decode,
                                        pd)) {
        if (self) self->is_valid = false;
        return false;
    }
    xx_tar_zstd_sync(tar_zstd, common);
    self->file_type = XX_FILE_TYPE_TAR_ZSTD;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    return true;
}

int64_t xx_tar_zstd_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    xx_tar_zstd *tar_zstd = (xx_tar_zstd *)self;
    xx_tar_common *common = xx_tar_zstd_common(tar_zstd, true);
    int64_t result;
    if (!self || !common) return -1;
    result = xx_tar_common_get_format_size(common, self, xx_tar_zstd_decode,
                                           pd);
    if (common->valid) xx_tar_zstd_sync(tar_zstd, common);
    return result;
}

uint64_t xx_tar_zstd_get_number_of_archive_records(Abstractformat *self,
                                                    xx_pd_struct *pd) {
    xx_tar_zstd *tar_zstd = (xx_tar_zstd *)self;
    xx_tar_common *common = xx_tar_zstd_common(tar_zstd, true);
    uint64_t result;
    if (!self || !common) return 0U;
    result = xx_tar_common_get_number_of_archive_records(
        common, self, xx_tar_zstd_decode, pd);
    if (common->valid) xx_tar_zstd_sync(tar_zstd, common);
    return result;
}

xx_archive_record_state *xx_tar_zstd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_zstd_common((xx_tar_zstd *)self, true);
    if (!self || !common) return NULL;
    return xx_tar_common_create_archive_records_reading(
        common, self, xx_tar_zstd_decode, options, pd);
}

const xx_archive_record *xx_tar_zstd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    xx_tar_common *common = xx_tar_zstd_common((xx_tar_zstd *)self, false);
    return common
               ? xx_tar_common_get_current_archive_record(common, state)
               : NULL;
}

bool xx_tar_zstd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_zstd_common((xx_tar_zstd *)self, false);
    return common && xx_tar_common_unpack_current_archive_record(
                         common, state, pd);
}

bool xx_tar_zstd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_tar_common *common = xx_tar_zstd_common((xx_tar_zstd *)self, false);
    return common && xx_tar_common_archive_record_move_to_next(common, state,
                                                                 pd);
}

void xx_tar_zstd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    xx_tar_common *common = xx_tar_zstd_common((xx_tar_zstd *)self, false);
    if (common) xx_tar_common_free_archive_records_reading(common, state);
}

xx_archive_write_state *xx_tar_zstd_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_tar_zstd *tar_zstd = (xx_tar_zstd *)self;
    xx_archive_write_state *state;
    xx_tar_common *common;
    if (!self) return NULL;
    state = xx_tar_common_create_archive_records_writing(
        self, options, xx_tar_zstd_encode, pd);
    if (!state) return NULL;
    common = xx_tar_zstd_common(tar_zstd, false);
    if (common) xx_tar_common_cleanup(common);
    tar_zstd->number_of_records = 0U;
    tar_zstd->number_of_members = 0U;
    tar_zstd->uncompressed_size = 0U;
    tar_zstd->compressed_size = -1;
    return state;
}

bool xx_tar_zstd_pack_archive_record(Abstractformat *self,
                                     xx_archive_write_state *state,
                                     const xx_archive_record *record,
                                     xx_io_device *source_dev,
                                     xx_pd_struct *pd) {
    return xx_tar_common_pack_archive_record(self, state, record, source_dev,
                                              pd);
}

bool xx_tar_zstd_finalize_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd) {
    xx_tar_zstd *tar_zstd = (xx_tar_zstd *)self;
    xx_tar_common *common;
    if (!self || !xx_tar_common_finalize_archive_records_writing(self, state,
                                                                   pd)) {
        return false;
    }
    common = xx_tar_zstd_common(tar_zstd, false);
    if (common) xx_tar_common_cleanup(common);
    tar_zstd->number_of_records = self->number_of_archive_records;
    tar_zstd->number_of_members = self->number_of_archive_records;
    tar_zstd->compressed_size = self->format_size;
    return true;
}

void xx_tar_zstd_free_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state) {
    xx_tar_common_free_archive_records_writing(self, state);
}

uint64_t xx_tar_zstd_get_number_of_records(const xx_tar_zstd *tar_zstd) {
    return tar_zstd ? tar_zstd->number_of_records : 0U;
}

uint64_t xx_tar_zstd_get_number_of_members(const xx_tar_zstd *tar_zstd) {
    return tar_zstd ? tar_zstd->number_of_members : 0U;
}

uint64_t xx_tar_zstd_get_uncompressed_size(const xx_tar_zstd *tar_zstd) {
    return tar_zstd ? tar_zstd->uncompressed_size : 0U;
}

int64_t xx_tar_zstd_get_compressed_size(const xx_tar_zstd *tar_zstd) {
    return tar_zstd ? tar_zstd->compressed_size : -1;
}
