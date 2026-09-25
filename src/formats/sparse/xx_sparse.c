/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sparse/xx_sparse.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant resolves to UNKNOWN until the enumerator
 * lands.  Delete this block once XX_FILE_TYPE_SPARSE exists in the enum. */
#ifdef SPARSE
#define XX_SPARSE_FILE_TYPE XX_FILE_TYPE_SPARSE
#else
#define XX_SPARSE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_SPARSE_MAGIC UINT32_C(0xED26FF3A)
#define XX_SPARSE_HEADER_SIZE 28U
#define XX_SPARSE_CHUNK_HEADER_SIZE 12U

#define XX_SPARSE_CHUNK_RAW UINT32_C(0xCAC1)
#define XX_SPARSE_CHUNK_FILL UINT32_C(0xCAC2)
#define XX_SPARSE_CHUNK_DONT_CARE UINT32_C(0xCAC3)
#define XX_SPARSE_CHUNK_CRC32 UINT32_C(0xCAC4)

/* A chunk header is at least 12 bytes, so the input size already caps the
 * chunk count; this is the belt to that braces, keeping one malformed image
 * from pinning a whole 32-bit chunk table in memory. */
#define XX_SPARSE_MAX_CHUNKS 1048576U

/* The header and the chunk headers may both be longer than the sizes this
 * reader understands.  A declared header larger than this is refused rather
 * than skipped over, because at that point the layout is not version 1.0. */
#define XX_SPARSE_MAX_HEADER_SIZE 4096U

/** Staging buffer for expansion.  A multiple of 4 so a FILL pattern tiles it
 * exactly, and of any plausible block size so RAW copies stay aligned. */
#define XX_SPARSE_STAGING_SIZE 65536U

/*
 * Expansion ceiling.
 *
 * A FILL or DONT_CARE chunk turns 12 input bytes into an arbitrary number of
 * output blocks, so nothing about the container bounds what it expands to:
 * a twelve-byte chunk may legally declare 0xFFFFFFFF blocks, which at a
 * 4 KiB block size is roughly 17 TiB of zeros.  That allocates nothing, but
 * an unattended caller would sit there writing it forever, so a declared
 * expansion past this ceiling is refused outright.  64 GiB is comfortably
 * above any real Android partition image.
 *
 * XX_META_ID_OPT_MAX_MEMBER_SIZE overrides it in either direction, following
 * the same convention the zip and rar readers use for extraction limits.
 */
#define XX_SPARSE_DEFAULT_MAX_EXPANDED (UINT64_C(64) << 30)

#define XX_SPARSE_MEMBER_NAME "sparse.img"

typedef struct xx_sparse_chunk_s {
    uint32_t type;         /**< One of the XX_SPARSE_CHUNK_* values. */
    uint32_t blocks;       /**< Output blocks this chunk expands to. */
    uint32_t fill;         /**< FILL: the 4-byte pattern, host order. */
    uint32_t crc;          /**< CRC32: the running checksum carried here. */
    int64_t data_offset;   /**< RAW: device offset of the stored blocks. */
} xx_sparse_chunk;

typedef struct xx_sparse_private_s {
    xx_sparse_chunk *chunks;
    size_t count;
    size_t capacity;
    int64_t input_size;
    int64_t archive_end;
    int64_t expanded_size;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t total_chunks;
    uint32_t image_checksum;
    uint16_t major_version;
    uint16_t minor_version;
} xx_sparse_private;

typedef struct xx_sparse_archive_stream_s {
    xx_sparse_private parsed;
    size_t index;
} xx_sparse_archive_stream;

static void xx_sparse_vtable_destroy(Abstractformat *self);

/* All positioning goes through seek64: an expanded image routinely exceeds
 * 2 GiB and long is 32-bit on Win64. */
static bool xx_sparse_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_sparse_write_all(xx_io_device *device, const void *data,
                                size_t size) {
    const uint8_t *in = (const uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t put = xx_io_write(device, in + done, size - done);
        if (put <= 0 || (size_t)put > size - done) return false;
        done += (size_t)put;
    }
    return true;
}

static bool xx_sparse_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_sparse_range_within(int64_t total_size, int64_t offset,
                                   int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_sparse_private_cleanup(xx_sparse_private *parsed) {
    if (!parsed) return;
    if (parsed->chunks) xx_mem_free(parsed->chunks);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
    parsed->expanded_size = -1;
}

static bool xx_sparse_append_chunk(xx_sparse_private *parsed,
                                   const xx_sparse_chunk *chunk) {
    xx_sparse_chunk *grown;
    size_t capacity;
    if (!parsed || !chunk || parsed->count >= XX_SPARSE_MAX_CHUNKS) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 64U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->chunks)) {
            return false;
        }
        grown = (xx_sparse_chunk *)xx_mem_realloc(
            parsed->chunks, capacity * sizeof(*parsed->chunks));
        if (!grown) return false;
        parsed->chunks = grown;
        parsed->capacity = capacity;
    }
    parsed->chunks[parsed->count++] = *chunk;
    return true;
}

/* Resolve the expansion ceiling.  An operation-specific value wins over a
 * format-wide one, and the built-in default applies when neither is set.  A
 * value that is not a plain non-negative integer is ignored rather than
 * treated as zero, which would reject every image. */
static uint64_t xx_sparse_max_expanded(const Abstractformat *self,
                                       const xx_list_s *options) {
    const xx_var *limit = self ? xx_format_resolve_extra_parameter(
                                     self, options,
                                     XX_META_ID_OPT_MAX_MEMBER_SIZE)
                               : NULL;
    if (!limit) return XX_SPARSE_DEFAULT_MAX_EXPANDED;
    switch (limit->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return xx_var_get_u64(limit);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(limit);
            return value >= 0 ? (uint64_t)value
                              : XX_SPARSE_DEFAULT_MAX_EXPANDED;
        }
        default: return XX_SPARSE_DEFAULT_MAX_EXPANDED;
    }
}

/* Read the file header and validate the fields that make the rest of the
 * walk meaningful.  Reports the offset of the first chunk header and the
 * chunk header size the walk must step by. */
static bool xx_sparse_parse_header(Abstractformat *self,
                                   xx_sparse_private *parsed,
                                   int64_t *out_first_chunk,
                                   uint32_t *out_chunk_header_size) {
    uint8_t header[XX_SPARSE_HEADER_SIZE];
    uint32_t file_header_size;
    uint32_t chunk_header_size;
    if (!xx_sparse_range_within(parsed->input_size, self->base_address,
                                XX_SPARSE_HEADER_SIZE) ||
        !xx_sparse_read_at(self->device, self->base_address, header,
                           sizeof(header)) ||
        xx_data_get_u32(header, sizeof(header), 0U, false) !=
            XX_SPARSE_MAGIC) {
        return false;
    }
    parsed->major_version = xx_data_get_u16(header, sizeof(header), 4U, false);
    parsed->minor_version = xx_data_get_u16(header, sizeof(header), 6U, false);
    file_header_size = xx_data_get_u16(header, sizeof(header), 8U, false);
    chunk_header_size = xx_data_get_u16(header, sizeof(header), 10U, false);
    parsed->block_size = xx_data_get_u32(header, sizeof(header), 12U, false);
    parsed->total_blocks = xx_data_get_u32(header, sizeof(header), 16U, false);
    parsed->total_chunks = xx_data_get_u32(header, sizeof(header), 20U, false);
    parsed->image_checksum =
        xx_data_get_u32(header, sizeof(header), 24U, false);

    /* Only the 1.x layout is described by this reader.  A future major
     * version may move fields, so it is refused rather than guessed at. */
    if (parsed->major_version != 1U) return false;
    if (file_header_size < XX_SPARSE_HEADER_SIZE ||
        file_header_size > XX_SPARSE_MAX_HEADER_SIZE ||
        chunk_header_size < XX_SPARSE_CHUNK_HEADER_SIZE ||
        chunk_header_size > XX_SPARSE_MAX_HEADER_SIZE) {
        return false;
    }
    /* blk_sz is multiplied by a 32-bit block count on every chunk, so a zero
     * or unaligned value is rejected up front. */
    if (parsed->block_size == 0U || (parsed->block_size % 4U) != 0U) {
        return false;
    }
    if (parsed->total_chunks > XX_SPARSE_MAX_CHUNKS) return false;
    /* total_blks * blk_sz is at most 2^64; it is computed in 64 bits and
     * must still land inside int64_t to be a usable file size. */
    {
        uint64_t expanded =
            (uint64_t)parsed->total_blocks * (uint64_t)parsed->block_size;
        if (expanded > (uint64_t)INT64_MAX ||
            expanded > xx_sparse_max_expanded(self, NULL)) {
            return false;
        }
        parsed->expanded_size = (int64_t)expanded;
    }
    *out_chunk_header_size = chunk_header_size;
    return xx_sparse_add(self->base_address, file_header_size,
                         out_first_chunk) &&
           xx_sparse_range_within(parsed->input_size, *out_first_chunk, 0);
}

static bool xx_sparse_parse(Abstractformat *self, xx_sparse_private *parsed,
                            xx_pd_struct *pd) {
    uint32_t chunk_header_size = 0U;
    uint64_t blocks_seen = 0U;
    int64_t offset;
    uint32_t index;
    /* Initialise before the guard clause: callers run the cleanup on their
     * stack copy whatever this returns. */
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
        parsed->expanded_size = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    parsed->input_size = xx_io_total_size(self->device);
    if (!xx_sparse_parse_header(self, parsed, &offset, &chunk_header_size)) {
        goto fail;
    }
    for (index = 0U; index < parsed->total_chunks; ++index) {
        uint8_t chunk_header[XX_SPARSE_CHUNK_HEADER_SIZE];
        xx_sparse_chunk chunk;
        uint32_t total_size;
        uint32_t payload;
        int64_t payload_offset;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_sparse_range_within(parsed->input_size, offset,
                                    (int64_t)chunk_header_size) ||
            !xx_sparse_read_at(self->device, offset, chunk_header,
                               sizeof(chunk_header))) {
            goto fail;
        }
        xx_mem_zero(&chunk, sizeof(chunk));
        chunk.type =
            xx_data_get_u16(chunk_header, sizeof(chunk_header), 0U, false);
        chunk.blocks =
            xx_data_get_u32(chunk_header, sizeof(chunk_header), 4U, false);
        total_size =
            xx_data_get_u32(chunk_header, sizeof(chunk_header), 8U, false);
        chunk.data_offset = -1;

        /* total_sz counts the chunk header too, so anything smaller than the
         * header is nonsense and would let the cursor stand still. */
        if (total_size < chunk_header_size) goto fail;
        payload = total_size - chunk_header_size;
        if (!xx_sparse_add(offset, chunk_header_size, &payload_offset) ||
            !xx_sparse_range_within(parsed->input_size, payload_offset,
                                    (int64_t)payload)) {
            goto fail;
        }
        switch (chunk.type) {
            case XX_SPARSE_CHUNK_RAW:
                /* The payload must be exactly the blocks it claims, so that
                 * expansion never reads past what the chunk owns. */
                if ((uint64_t)payload !=
                    (uint64_t)chunk.blocks * (uint64_t)parsed->block_size) {
                    goto fail;
                }
                chunk.data_offset = payload_offset;
                break;
            case XX_SPARSE_CHUNK_FILL: {
                uint8_t pattern[4];
                if (payload != 4U ||
                    !xx_sparse_read_at(self->device, payload_offset, pattern,
                                       sizeof(pattern))) {
                    goto fail;
                }
                chunk.fill =
                    xx_data_get_u32(pattern, sizeof(pattern), 0U, false);
                break;
            }
            case XX_SPARSE_CHUNK_DONT_CARE:
                if (payload != 0U) goto fail;
                break;
            case XX_SPARSE_CHUNK_CRC32: {
                uint8_t value[4];
                /* A CRC32 chunk produces no output of its own. */
                if (payload != 4U || chunk.blocks != 0U ||
                    !xx_sparse_read_at(self->device, payload_offset, value,
                                       sizeof(value))) {
                    goto fail;
                }
                chunk.crc = xx_data_get_u32(value, sizeof(value), 0U, false);
                break;
            }
            default: goto fail;
        }
        blocks_seen += chunk.blocks;
        if (blocks_seen > (uint64_t)parsed->total_blocks) goto fail;
        if (!xx_sparse_append_chunk(parsed, &chunk) ||
            !xx_sparse_add(offset, total_size, &offset) ||
            offset > parsed->input_size) {
            goto fail;
        }
    }
    /* The chunk list must account for the whole declared image; a shortfall
     * means the file was truncated or the header lies about total_blks. */
    if (blocks_seen != (uint64_t)parsed->total_blocks) goto fail;
    parsed->archive_end = offset;
    return true;
fail:
    xx_sparse_private_cleanup(parsed);
    return false;
}

/* ------------------------------------------------------------------------ */
/* Expansion                                                                 */
/* ------------------------------------------------------------------------ */

/* Write size bytes of a repeating 4-byte pattern, updating the running CRC.
 * A pattern of zero is the DONT_CARE case, which shares this path. */
static bool xx_sparse_emit_pattern(xx_io_device *destination, uint32_t pattern,
                                   int64_t size, uint32_t *crc,
                                   xx_pd_struct *pd) {
    uint8_t staging[XX_SPARSE_STAGING_SIZE];
    size_t index;
    if (size < 0) return false;
    for (index = 0U; index < sizeof(staging); index += 4U) {
        /* The pattern is stored little endian in the output, matching the
         * byte order it was read in. */
        staging[index] = (uint8_t)(pattern & 0xFFU);
        staging[index + 1U] = (uint8_t)((pattern >> 8) & 0xFFU);
        staging[index + 2U] = (uint8_t)((pattern >> 16) & 0xFFU);
        staging[index + 3U] = (uint8_t)((pattern >> 24) & 0xFFU);
    }
    while (size > 0) {
        size_t step = (size < (int64_t)sizeof(staging))
                          ? (size_t)size
                          : sizeof(staging);
        if ((pd && xx_pd_is_stopped(pd)) ||
            !xx_sparse_write_all(destination, staging, step)) {
            return false;
        }
        if (crc) *crc = xx_crc32_calc(*crc, staging, step);
        size -= (int64_t)step;
    }
    return true;
}

/* Copy size bytes straight out of the source device. */
static bool xx_sparse_emit_raw(xx_io_device *source, int64_t offset,
                               xx_io_device *destination, int64_t size,
                               uint32_t *crc, xx_pd_struct *pd) {
    uint8_t staging[XX_SPARSE_STAGING_SIZE];
    if (size < 0 || offset < 0) return false;
    if (size != 0 && xx_io_seek64(source, offset, SEEK_SET) != 0) return false;
    while (size > 0) {
        size_t step = (size < (int64_t)sizeof(staging))
                          ? (size_t)size
                          : sizeof(staging);
        size_t done = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        while (done < step) {
            ssize_t got = xx_io_read(source, staging + done, step - done);
            if (got <= 0 || (size_t)got > step - done) return false;
            done += (size_t)got;
        }
        if (!xx_sparse_write_all(destination, staging, step)) return false;
        if (crc) *crc = xx_crc32_calc(*crc, staging, step);
        size -= (int64_t)step;
    }
    return true;
}

static bool xx_sparse_expand(Abstractformat *self,
                             const xx_sparse_private *parsed,
                             xx_io_device *destination, xx_pd_struct *pd) {
    uint32_t crc = 0U;
    size_t index;
    if (!self || !self->device || !parsed || !destination) return false;
    for (index = 0U; index < parsed->count; ++index) {
        const xx_sparse_chunk *chunk = &parsed->chunks[index];
        int64_t size =
            (int64_t)((uint64_t)chunk->blocks * (uint64_t)parsed->block_size);
        switch (chunk->type) {
            case XX_SPARSE_CHUNK_RAW:
                if (!xx_sparse_emit_raw(self->device, chunk->data_offset,
                                        destination, size, &crc, pd)) {
                    return false;
                }
                break;
            case XX_SPARSE_CHUNK_FILL:
                if (!xx_sparse_emit_pattern(destination, chunk->fill, size,
                                            &crc, pd)) {
                    return false;
                }
                break;
            case XX_SPARSE_CHUNK_DONT_CARE:
                /* "Don't care" means the flasher may leave the blocks alone;
                 * a reassembled file has to put something there, and zero is
                 * what simg2img writes. */
                if (!xx_sparse_emit_pattern(destination, 0U, size, &crc, pd)) {
                    return false;
                }
                break;
            case XX_SPARSE_CHUNK_CRC32:
                /* The checksum covers everything emitted so far.  A mismatch
                 * means the transfer was corrupted, so expansion stops. */
                if (chunk->crc != crc) return false;
                break;
            default: return false;
        }
    }
    /* The header's image_checksum is optional and is written as zero by most
     * producers, so it is only enforced when it is actually present. */
    if (parsed->image_checksum != 0U && parsed->image_checksum != crc) {
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Record plumbing                                                           */
/* ------------------------------------------------------------------------ */

static bool xx_sparse_copy_options(xx_list_s *destination,
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

static const xx_var *xx_sparse_find_option(const xx_list_s *options,
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

static bool xx_sparse_populate_record(xx_archive_record *record,
                                      const xx_sparse_private *parsed,
                                      int64_t base_address) {
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base_address;
    record->header_size = XX_SPARSE_HEADER_SIZE;
    /* The member has no single contiguous payload: it is the whole chunk
     * area taken together, so the record spans the container body. */
    record->data_offset = base_address;
    record->compressed_size = parsed->archive_end - base_address;
    return xx_archive_record_set_original_name(record, XX_SPARSE_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)parsed->expanded_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 1U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          parsed->image_checksum) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static void xx_sparse_archive_stream_free(void *pointer) {
    xx_sparse_archive_stream *stream = (xx_sparse_archive_stream *)pointer;
    if (!stream) return;
    xx_sparse_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

void xx_sparse_init(xx_sparse *sparse, xx_io_device *dev,
                    int64_t base_address) {
    if (!sparse) return;
    xx_mem_zero(sparse, sizeof(*sparse));
    xx_format_init(&sparse->format, dev, base_address);
    sparse->format.endian = XX_ENDIAN_LITTLE;
    sparse->format.file_type = XX_SPARSE_FILE_TYPE;
    sparse->format.format_type = XX_TYPE_ARCHIVE;
    sparse->format.is_archive = true;
    xx_format_set_mime_type(&sparse->format, "application/x-android-sparse");
    xx_format_set_extension(&sparse->format, "img");
    sparse->format.check_is_valid = xx_sparse_check_is_valid;
    sparse->format.handle_base_info = xx_sparse_handle_base_info;
    sparse->format.get_format_size = xx_sparse_get_format_size;
    sparse->format.get_number_of_archive_records =
        xx_sparse_get_number_of_archive_records;
    sparse->format.create_archive_records_reading =
        xx_sparse_create_archive_records_reading;
    sparse->format.get_current_archive_record =
        xx_sparse_get_current_archive_record;
    sparse->format.unpack_current_archive_record =
        xx_sparse_unpack_current_archive_record;
    sparse->format.archive_record_move_to_next =
        xx_sparse_archive_record_move_to_next;
    sparse->format.free_archive_records_reading =
        xx_sparse_free_archive_records_reading;
    sparse->format.destroy = xx_sparse_vtable_destroy;
    sparse->expanded_size = -1;
    sparse->archive_end = -1;
}

xx_sparse *xx_sparse_create(xx_io_device *dev, int64_t base_address) {
    xx_sparse *sparse = (xx_sparse *)xx_mem_alloc(sizeof(*sparse));
    if (sparse) xx_sparse_init(sparse, dev, base_address);
    return sparse;
}

void xx_sparse_destroy(xx_sparse *sparse) {
    if (!sparse) return;
    if (sparse->internal) {
        xx_sparse_private_cleanup((xx_sparse_private *)sparse->internal);
        xx_mem_free(sparse->internal);
        sparse->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&sparse->format);
}

static void xx_sparse_vtable_destroy(Abstractformat *self) {
    xx_sparse_destroy((xx_sparse *)self);
}

void xx_sparse_free(xx_sparse *sparse) {
    if (!sparse) return;
    xx_sparse_destroy(sparse);
    xx_mem_free(sparse);
}

bool xx_sparse_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_sparse_private parsed;
    bool result = xx_sparse_parse(self, &parsed, pd);
    xx_sparse_private_cleanup(&parsed);
    return result;
}

bool xx_sparse_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_sparse_private *parsed;
    xx_sparse *sparse = (xx_sparse *)self;
    int64_t total_size;
    if (!self || !sparse) return false;
    parsed = (xx_sparse_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_sparse_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (sparse->internal) {
        xx_sparse_private_cleanup((xx_sparse_private *)sparse->internal);
        xx_mem_free(sparse->internal);
    }
    sparse->internal = parsed;
    sparse->number_of_records = 1U;
    sparse->number_of_members = 1U;
    sparse->block_size = parsed->block_size;
    sparse->total_blocks = parsed->total_blocks;
    sparse->total_chunks = parsed->total_chunks;
    sparse->image_checksum = parsed->image_checksum;
    sparse->major_version = parsed->major_version;
    sparse->minor_version = parsed->minor_version;
    sparse->expanded_size = parsed->expanded_size;
    sparse->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 1U;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_sparse_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_sparse_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_sparse *)self)->number_of_records;
}

bool xx_sparse_unpack_to_device(xx_sparse *sparse, xx_io_device *destination,
                                xx_pd_struct *pd) {
    xx_sparse_private parsed;
    bool result;
    if (!sparse || !destination) return false;
    result = xx_sparse_parse(&sparse->format, &parsed, pd) &&
             xx_sparse_expand(&sparse->format, &parsed, destination, pd);
    xx_sparse_private_cleanup(&parsed);
    return result;
}

xx_archive_record_state *xx_sparse_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_sparse_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_sparse_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_sparse_copy_options(&state->options, options) ||
        !xx_sparse_parse(self, &stream->parsed, pd)) {
        xx_sparse_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_sparse_archive_stream_free;
    state->total_records = 1;
    if (xx_sparse_populate_record(&state->current_record, &stream->parsed,
                                  self->base_address)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_sparse_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sparse_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_sparse_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_sparse_archive_stream *)state->internal_state;
    ++stream->index;
    /* There is exactly one member, so the first move always ends the walk. */
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

/* The sparse member is reassembled, not copied, so extraction goes through
 * the expander rather than xx_store_unpack_device_to_file(). */
bool xx_sparse_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_sparse_archive_stream *stream;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination_path = NULL;
    xx_io_device *destination = NULL;
    bool result = false;
    bool created = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_sparse_archive_stream *)state->internal_state;
    if (stream->index != 0U) return false;
    /* The parse applied the format-wide ceiling; a stricter one supplied
     * with this read session is honoured here. */
    if (stream->parsed.expanded_size < 0 ||
        (uint64_t)stream->parsed.expanded_size >
            xx_sparse_max_expanded(self, &state->options)) {
        return false;
    }

    option = xx_sparse_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the container is addressable. */
        return stream->parsed.archive_end >= 0 &&
               stream->parsed.archive_end <= stream->parsed.input_size;
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
        destination_path = xx_str_concat3(base, "/", XX_SPARSE_MEMBER_NAME);
    } else {
        destination_path = xx_str_concat(base, XX_SPARSE_MEMBER_NAME);
    }
    if (!destination_path) goto cleanup;
    if (!xx_store_create_dirs_a(destination_path, false)) goto cleanup;
    destination = xx_io_file_open(destination_path, "wb");
    created = destination != NULL;
    if (!destination) goto cleanup;
    result = xx_sparse_expand(self, &stream->parsed, destination, pd);
    xx_io_close(destination);
    destination = NULL;
    if (!result && created) xx_rt_remove(destination_path);

cleanup:
    if (destination) xx_io_close(destination);
    if (owned_base) xx_str_free(owned_base);
    if (destination_path) xx_str_free(destination_path);
    return result;
}

void xx_sparse_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_sparse_get_number_of_records(const xx_sparse *sparse) {
    return sparse ? sparse->number_of_records : 0U;
}
uint64_t xx_sparse_get_number_of_members(const xx_sparse *sparse) {
    return sparse ? sparse->number_of_members : 0U;
}
uint32_t xx_sparse_get_block_size(const xx_sparse *sparse) {
    return sparse ? sparse->block_size : 0U;
}
uint32_t xx_sparse_get_total_blocks(const xx_sparse *sparse) {
    return sparse ? sparse->total_blocks : 0U;
}
uint32_t xx_sparse_get_total_chunks(const xx_sparse *sparse) {
    return sparse ? sparse->total_chunks : 0U;
}
uint32_t xx_sparse_get_image_checksum(const xx_sparse *sparse) {
    return sparse ? sparse->image_checksum : 0U;
}
int64_t xx_sparse_get_expanded_size(const xx_sparse *sparse) {
    return sparse ? sparse->expanded_size : -1;
}
int64_t xx_sparse_get_archive_end(const xx_sparse *sparse) {
    return sparse ? sparse->archive_end : -1;
}
