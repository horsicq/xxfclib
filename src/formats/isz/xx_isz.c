/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * UltraISO ISZ ("ISO zipped"): a disc image cut into fixed-size chunks,
 * each stored, zlib, bzip2 or all-zero, behind a table of obfuscated chunk
 * pointers.  xx_isz.h carries the field table.  The image is presented as
 * one member, image.iso.
 *
 * Written from the format's published layout.  libmirage's filter-isz
 * (GPL) and the unisz tool were used only to confirm behaviour, never as
 * source: bzip2 chunks do not carry a usable "BZh" signature (it is
 * restored before decoding), AES modes are ECB keyed by the zero-padded
 * password with a chunk's trailing partial block left plain, and a chunk
 * pointer's stored length advances the data position whatever its type.
 *
 * The container stores no per-chunk unpacked sizes, so the grammar is the
 * anchor: the chunk count must be exactly ceil(sectors * sector size /
 * chunk size), every pointer must carry a length its type allows (a stored
 * chunk is exactly its share of the image), and the chunks must tile the
 * data area inside the file.  Detection is that structural walk alone; the
 * chunks are decoded only on extraction, one chunk at a time, so memory is
 * bounded by the 8 MiB chunk ceiling whatever the image size.
 *
 * A volume set (.isz + .i01 + ...) is recognised from its first file and
 * listed, but its image cannot be rebuilt from that file alone, so the
 * member does not extract.  Continuation volumes (volume number != 0) are
 * refused: they hold no chunk table.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/isz/xx_isz.h"

#include "xxfclib/algo/aes/xx_aes.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as ISZ is registered there. */
#ifdef ISZ
#define XX_ISZ_FILE_TYPE XX_FILE_TYPE_ISZ
#else
#define XX_ISZ_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ISZ_HEADER_MIN 48U
#define ISZ_HEADER_FULL 64U
/* UltraISO writes 3-byte pointers, whose 22-bit length field caps a stored
 * chunk below 4 MiB; 8 MiB leaves room for 4-byte pointers while bounding
 * the extraction buffers. */
#define ISZ_MAX_CHUNK_SIZE (8U * 1024U * 1024U)
/* 16 Mi chunks: a pointer table of at most 64 MiB, walked sequentially. */
#define ISZ_MAX_CHUNKS (16U * 1024U * 1024U)
/* Volume numbers run 0..99. */
#define ISZ_MAX_SEGMENTS 100U
#define ISZ_SEGMENT_RECORD 24U
/* Pointers fetched per table read; a multiple of 4 keeps the XOR key phase
 * of every window at zero. */
#define ISZ_TABLE_WINDOW 4096U
#define ISZ_ECB_SLICE 4096U

#define ISZ_TYPE_ZERO 0U
#define ISZ_TYPE_DATA 1U
#define ISZ_TYPE_ZLIB 2U
#define ISZ_TYPE_BZIP2 3U

#define ISZ_ENC_NONE 0U
#define ISZ_ENC_PASSWORD 1U
#define ISZ_ENC_AES256 4U

#define ISZ_IMAGE_NAME "image.iso"

/* The pointer and segment tables are XORed with ~"IsZ!". */
static const uint8_t isz_key[4] = {0xB6U, 0x8CU, 0xA5U, 0xDEU};

typedef struct isz_context_s {
    int64_t base;
    int64_t input_size;          /**< Bytes available from base. */
    uint8_t header_size;
    uint8_t version;
    uint8_t encryption;
    uint8_t pointer_size;
    uint16_t sector_size;
    uint32_t volume_serial;
    uint32_t total_sectors;
    uint32_t chunk_count;
    uint32_t chunk_size;
    uint32_t pointer_offset;
    uint32_t segment_offset;
    uint32_t data_offset;
    uint64_t segment_size;
    uint64_t image_size;
    int64_t chunk_data;          /**< First chunk's data, relative to base. */
    uint32_t local_chunks;       /**< Chunks that start in this file. */
    uint32_t local_tail;         /**< Bytes of the last of them that continue
                                      in the next volume. */
    uint32_t segment_count;
    uint64_t local_packed;       /**< Chunk bytes present in this file. */
    uint64_t packed_total;       /**< Chunk bytes of the whole image. */
    uint32_t max_packed;
    uint32_t type_mask;          /**< 1 << type for every chunk type seen. */
    int64_t format_size;
    bool multi_volume;
} isz_context;

typedef struct isz_stream_s {
    isz_context context;
    size_t index;
    size_t count;
} isz_stream;

/* Sequential reader over the chunk pointer table. */
typedef struct isz_table_s {
    xx_io_device *device;
    const isz_context *context;
    uint32_t first;              /**< Index of the pointer in window[0]. */
    uint32_t filled;             /**< Pointers held in the window. */
    uint8_t *window;
} isz_table;

static uint16_t isz_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t isz_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U) | ((uint32_t)b[2] << 16U) |
           ((uint32_t)b[3] << 24U);
}

static uint64_t isz_le64(const uint8_t *b) {
    return (uint64_t)isz_le32(b) | ((uint64_t)isz_le32(b + 4U) << 32U);
}

static bool isz_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static void isz_deobfuscate(uint8_t *data, size_t size, uint64_t phase) {
    size_t index;
    for (index = 0U; index < size; ++index)
        data[index] ^= isz_key[(size_t)((phase + index) & 3U)];
}

/* Unpacked size of chunk `index`: the chunk size, or what is left of the
 * image for the last one. */
static uint32_t isz_expected(const isz_context *context, uint32_t index) {
    uint64_t start = (uint64_t)index * context->chunk_size;
    uint64_t left = context->image_size - start;
    return left < context->chunk_size ? (uint32_t)left : context->chunk_size;
}

/* The largest length a compressed chunk may carry.  zlib and bzip2 expand
 * incompressible input by well under an eighth plus a small constant. */
static uint64_t isz_packed_bound(const isz_context *context) {
    return (uint64_t)context->chunk_size + context->chunk_size / 8U + 1024U;
}

static bool isz_table_open(isz_table *table, xx_io_device *device,
                           const isz_context *context) {
    xx_mem_zero(table, sizeof(*table));
    table->device = device;
    table->context = context;
    if (context->pointer_offset == 0U) return true;
    table->window = (uint8_t *)xx_mem_alloc((size_t)ISZ_TABLE_WINDOW *
                                            context->pointer_size);
    return table->window != NULL;
}

static void isz_table_close(isz_table *table) {
    if (table->window) xx_mem_free(table->window);
    table->window = NULL;
}

/* Pointer `index` as (type, length).  Indices are requested in ascending
 * order; the table is read one window at a time. */
static bool isz_table_get(isz_table *table, uint32_t index, uint32_t *type,
                          uint32_t *length) {
    const isz_context *context = table->context;
    uint32_t value = 0U, bits;
    const uint8_t *entry;
    uint8_t byte;
    if (index >= context->chunk_count) return false;
    if (context->pointer_offset == 0U) {
        /* No table: every chunk is stored at its full share of the image. */
        *type = ISZ_TYPE_DATA;
        *length = isz_expected(context, index);
        return true;
    }
    if (!table->window || index < table->first ||
        index - table->first >= table->filled) {
        uint32_t first = index - index % ISZ_TABLE_WINDOW;
        uint32_t count = context->chunk_count - first;
        uint64_t position = (uint64_t)first * context->pointer_size;
        if (count > ISZ_TABLE_WINDOW) count = ISZ_TABLE_WINDOW;
        table->filled = 0U;
        if (!table->window ||
            !isz_read_at(table->device,
                         context->base + (int64_t)context->pointer_offset +
                             (int64_t)position,
                         table->window,
                         (size_t)count * context->pointer_size))
            return false;
        isz_deobfuscate(table->window, (size_t)count * context->pointer_size,
                        position);
        table->first = first;
        table->filled = count;
    }
    entry = table->window +
            (size_t)(index - table->first) * context->pointer_size;
    for (byte = 0U; byte < context->pointer_size; ++byte)
        value |= (uint32_t)entry[byte] << (8U * byte);
    bits = 8U * context->pointer_size - 2U;
    *type = (value >> bits) & 3U;
    *length = value & ((UINT32_C(1) << bits) - 1U);
    return true;
}

/* A pointer's length has to suit its type. */
static bool isz_chunk_ok(const isz_context *context, uint32_t index,
                         uint32_t type, uint32_t length) {
    switch (type) {
    case ISZ_TYPE_ZERO:
        return (uint64_t)length <= isz_packed_bound(context);
    case ISZ_TYPE_DATA:
        return length == isz_expected(context, index);
    case ISZ_TYPE_ZLIB:
        return length >= 3U && (uint64_t)length <= isz_packed_bound(context);
    default:
        return length >= 4U && (uint64_t)length <= isz_packed_bound(context);
    }
}

static bool isz_ranges_overlap(uint64_t a, uint64_t a_size, uint64_t b,
                               uint64_t b_size) {
    return a_size != 0U && b_size != 0U && a < b + b_size && b < a + a_size;
}

/* Segment table: at least one record before the zero-size terminator.  One
 * record describes a single-file image; more make a volume set whose first
 * volume is this file. */
static bool isz_parse_segments(isz_context *context, xx_io_device *device) {
    uint8_t record[ISZ_SEGMENT_RECORD];
    uint32_t count = 0U, next_first = 0U, total_chunks = 0U;
    uint64_t position = context->segment_offset;
    for (;;) {
        uint64_t size;
        uint32_t chunks, first, offset, left;
        if (count > ISZ_MAX_SEGMENTS ||
            position + ISZ_SEGMENT_RECORD > (uint64_t)context->input_size ||
            !isz_read_at(device, context->base + (int64_t)position, record,
                         sizeof(record)))
            return false;
        isz_deobfuscate(record, sizeof(record), 0U);
        size = isz_le64(record);
        chunks = isz_le32(record + 8U);
        first = isz_le32(record + 12U);
        offset = isz_le32(record + 16U);
        left = isz_le32(record + 20U);
        position += ISZ_SEGMENT_RECORD;
        if (size == 0U) break;
        if (count == ISZ_MAX_SEGMENTS) return false;
        if (first != next_first || chunks == 0U ||
            chunks > context->chunk_count - total_chunks ||
            offset < context->header_size)
            return false;
        if (count == 0U) {
            context->chunk_data = (int64_t)offset;
            context->local_chunks = chunks;
            context->local_tail = left;
        }
        total_chunks += chunks;
        next_first = first + chunks;
        ++count;
        if (total_chunks == context->chunk_count && left != 0U) return false;
    }
    if (count == 0U || total_chunks != context->chunk_count) return false;
    context->segment_count = count;
    context->multi_volume = count > 1U;
    return true;
}

/* Walk every chunk pointer: lengths must suit their types, and the sums
 * locate the end of the chunk data. */
static bool isz_walk(isz_context *context, xx_io_device *device,
                     xx_pd_struct *pd) {
    isz_table table;
    uint32_t index;
    bool result = false;
    if (!isz_table_open(&table, device, context)) return false;
    context->local_packed = 0U;
    context->packed_total = 0U;
    context->max_packed = 0U;
    context->type_mask = 0U;
    for (index = 0U; index < context->chunk_count; ++index) {
        uint32_t type = 0U, length = 0U;
        if ((index & 0xFFFU) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        if (!isz_table_get(&table, index, &type, &length) ||
            !isz_chunk_ok(context, index, type, length))
            goto done;
        context->packed_total += length;
        if (index < context->local_chunks) {
            context->local_packed += length;
            /* The part that continues in the next volume has to be a
             * proper tail of the chunk that starts here. */
            if (index + 1U == context->local_chunks &&
                context->local_tail != 0U && context->local_tail >= length)
                goto done;
        }
        if (length > context->max_packed) context->max_packed = length;
        context->type_mask |= 1U << type;
    }
    result = true;
done:
    isz_table_close(&table);
    return result;
}

/* A trailing copy of the header followed by its u32 size, which some
 * writers append; counted into the format size when present. */
static int64_t isz_footer_size(const isz_context *context,
                               xx_io_device *device, int64_t data_end) {
    uint8_t header[256], footer[260];
    size_t size = context->header_size;
    if (context->input_size - data_end < (int64_t)size + 4 ||
        !isz_read_at(device, context->base, header, size) ||
        !isz_read_at(device, context->base + data_end, footer, size + 4U) ||
        xx_rt_memcmp(header, footer, size) != 0 ||
        isz_le32(footer + size) != (uint32_t)size)
        return 0;
    return (int64_t)size + 4;
}

static bool isz_parse(Abstractformat *format, isz_context *out,
                      xx_pd_struct *pd) {
    uint8_t header[ISZ_HEADER_FULL];
    isz_context context;
    int64_t total, local_end;
    uint64_t table_bytes = 0U, segment_bytes = 0U;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(&context, sizeof(context));
    context.base = format->base_address;
    context.input_size = total - format->base_address;
    if (context.input_size < (int64_t)ISZ_HEADER_MIN ||
        !isz_read_at(format->device, context.base, header, ISZ_HEADER_MIN) ||
        xx_rt_memcmp(header, "IsZ!", 4U) != 0)
        return false;
    context.header_size = header[4];
    context.version = header[5];
    context.volume_serial = isz_le32(header + 6U);
    context.sector_size = isz_le16(header + 10U);
    context.total_sectors = isz_le32(header + 12U);
    context.encryption = header[16];
    context.segment_size = isz_le64(header + 17U);
    context.chunk_count = isz_le32(header + 25U);
    context.chunk_size = isz_le32(header + 29U);
    context.pointer_size = header[33];
    context.pointer_offset = isz_le32(header + 35U);
    context.segment_offset = isz_le32(header + 39U);
    context.data_offset = isz_le32(header + 43U);
    if (context.header_size < ISZ_HEADER_MIN || context.version > 1U ||
        (int64_t)context.header_size > context.input_size ||
        context.encryption > ISZ_ENC_AES256 || header[34] != 0U ||
        context.sector_size == 0U || context.chunk_size == 0U ||
        context.chunk_size > ISZ_MAX_CHUNK_SIZE ||
        context.chunk_size % context.sector_size != 0U ||
        context.total_sectors == 0U || context.chunk_count == 0U ||
        context.chunk_count > ISZ_MAX_CHUNKS ||
        context.data_offset < context.header_size)
        return false;
    context.image_size =
        (uint64_t)context.total_sectors * context.sector_size;
    if ((context.image_size + context.chunk_size - 1U) / context.chunk_size !=
        (uint64_t)context.chunk_count)
        return false;
    if (context.pointer_offset != 0U) {
        if (context.pointer_size < 1U || context.pointer_size > 4U ||
            context.pointer_offset < context.header_size)
            return false;
        table_bytes = (uint64_t)context.chunk_count * context.pointer_size;
        if ((uint64_t)context.pointer_offset + table_bytes >
            (uint64_t)context.input_size)
            return false;
    }
    context.chunk_data = (int64_t)context.data_offset;
    context.local_chunks = context.chunk_count;
    context.segment_count = 1U;
    if (context.segment_offset != 0U) {
        if (context.segment_offset < context.header_size ||
            !isz_parse_segments(&context, format->device))
            return false;
        segment_bytes =
            (uint64_t)(context.segment_count + 1U) * ISZ_SEGMENT_RECORD;
    }
    if (!isz_walk(&context, format->device, pd)) return false;
    if (context.multi_volume) {
        /* Only the first volume's share is here. */
        local_end = context.chunk_data + (int64_t)context.local_packed -
                    (int64_t)context.local_tail;
        if (local_end > context.input_size) return false;
    } else {
        local_end = context.chunk_data + (int64_t)context.packed_total;
        if (context.segment_offset == 0U && context.segment_size != 0U &&
            (uint64_t)local_end > context.segment_size) {
            /* No segment table, but the data outgrows the volume size: the
             * rest lives in the following volumes. */
            if (context.segment_size > (uint64_t)context.input_size ||
                context.segment_size <= (uint64_t)context.chunk_data)
                return false;
            context.multi_volume = true;
            local_end = (int64_t)context.segment_size;
        } else if (local_end > context.input_size) {
            return false;
        }
    }
    /* The tables must stay clear of the chunk data. */
    if (isz_ranges_overlap(context.pointer_offset, table_bytes,
                           (uint64_t)context.chunk_data,
                           (uint64_t)(local_end - context.chunk_data)) ||
        isz_ranges_overlap(context.segment_offset, segment_bytes,
                           (uint64_t)context.chunk_data,
                           (uint64_t)(local_end - context.chunk_data)) ||
        (uint64_t)context.chunk_data < context.header_size)
        return false;
    context.format_size = local_end;
    if (!context.multi_volume)
        context.format_size +=
            isz_footer_size(&context, format->device, local_end);
    if (context.pointer_offset != 0U &&
        (int64_t)(context.pointer_offset + table_bytes) > context.format_size)
        context.format_size = (int64_t)(context.pointer_offset + table_bytes);
    if (segment_bytes != 0U &&
        (int64_t)(context.segment_offset + segment_bytes) >
            context.format_size)
        context.format_size =
            (int64_t)(context.segment_offset + segment_bytes);
    if (context.format_size > context.input_size) return false;
    *out = context;
    return true;
}

static bool isz_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *isz_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static uint32_t isz_method(const isz_context *context) {
    if (context->type_mask & (1U << ISZ_TYPE_BZIP2)) return 12U;
    if (context->type_mask & (1U << ISZ_TYPE_ZLIB)) return 8U;
    return 0U;
}

static bool isz_set_record(xx_archive_record *record,
                           const isz_context *context) {
    uint64_t packed = context->multi_volume ? context->local_packed
                                            : context->packed_total;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = context->base;
    record->header_size = context->header_size;
    record->data_offset = context->base + context->chunk_data;
    record->compressed_size = (int64_t)packed;
    return xx_archive_record_set_original_name(record, ISZ_IMAGE_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          packed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          context->image_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          isz_method(context)) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           context->encryption !=
                                               ISZ_ENC_NONE) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* AES-ECB over the whole 16-byte blocks of `data`, in place, built on the
 * library's CBC primitive: CBC with a zero IV yields D(C[i]) ^ C[i-1], so
 * XOR-ing the previous ciphertext block back in leaves D(C[i]). */
static bool isz_ecb_decrypt(uint8_t *data, size_t size, const uint8_t *key,
                            size_t key_size) {
    static const uint8_t zero_iv[16] = {0};
    uint8_t saved[ISZ_ECB_SLICE];
    size_t whole = size & ~(size_t)15U, at = 0U;
    while (at < whole) {
        size_t amount = whole - at < sizeof(saved) ? whole - at : sizeof(saved);
        size_t index;
        xx_rt_memcpy(saved, data + at, amount);
        if (!xx_aes_cbc_decrypt(saved, amount, key, key_size, zero_iv,
                                data + at)) {
            xx_rt_memset(saved, 0, sizeof(saved));
            return false;
        }
        for (index = 16U; index < amount; ++index)
            data[at + index] ^= saved[index - 16U];
        at += amount;
    }
    xx_rt_memset(saved, 0, sizeof(saved));
    return true;
}

static bool isz_write_all(xx_io_device *device, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    if (!device) return true; /* verify-only pass */
    while (done < size) {
        ssize_t amount = xx_io_write(device, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* The AES key: the password's bytes, zero padded or cut to the key size.
 * Returns the key size, 0 when the image cannot be decrypted. */
static size_t isz_make_key(const isz_context *context, const xx_var *password,
                           uint8_t key[32]) {
    const uint8_t *bytes = NULL;
    size_t size = 0U, key_size;
    char *owned = NULL;
    if (context->encryption < 2U || context->encryption > ISZ_ENC_AES256 ||
        !password)
        return 0U;
    key_size = 16U + 8U * (size_t)(context->encryption - 2U);
    if (password->type == XX_VAR_TYPE_BYTES ||
        password->type == XX_VAR_TYPE_BYTES_VIEW) {
        bytes = (const uint8_t *)xx_var_get_bytes(password, &size);
    } else if (password->type == XX_VAR_TYPE_STRING ||
               password->type == XX_VAR_TYPE_STRING_VIEW) {
        bytes = (const uint8_t *)xx_var_get_str(password);
        size = bytes ? xx_str_len((const char *)bytes) : 0U;
    } else if (password->type == XX_VAR_TYPE_WSTRING ||
               password->type == XX_VAR_TYPE_WSTRING_VIEW) {
        const wchar_t *wide = xx_var_get_wstr(password);
        owned = wide ? xx_str_unicode_to_utf8(wide) : NULL;
        bytes = (const uint8_t *)owned;
        size = owned ? xx_str_len(owned) : 0U;
    }
    if (!bytes && size != 0U) return 0U;
    xx_rt_memset(key, 0, 32U);
    if (size > key_size) size = key_size;
    if (size) xx_rt_memcpy(key, bytes, size);
    if (owned) {
        xx_rt_memset(owned, 0, xx_str_len(owned));
        xx_str_free(owned);
    }
    return key_size;
}

/* Rebuild the image chunk by chunk into `destination` (NULL: decode and
 * discard, to prove the image decodes). */
static bool isz_rebuild(Abstractformat *format, const isz_context *context,
                        const xx_var *password, xx_io_device *destination,
                        xx_pd_struct *pd) {
    isz_table table;
    uint8_t key[32];
    size_t key_size = 0U;
    uint8_t *plain = NULL, *packed = NULL;
    int64_t position = context->base + context->chunk_data;
    uint32_t index;
    bool result = false, table_open = false;
    if (context->multi_volume || context->encryption == ISZ_ENC_PASSWORD)
        return false;
    if (context->encryption != ISZ_ENC_NONE) {
        key_size = isz_make_key(context, password, key);
        if (key_size == 0U) return false;
    }
    plain = (uint8_t *)xx_mem_alloc(context->chunk_size);
    packed = (uint8_t *)xx_mem_alloc(context->max_packed ? context->max_packed
                                                         : 1U);
    if (!plain || !packed || !isz_table_open(&table, format->device, context))
        goto done;
    table_open = true;
    for (index = 0U; index < context->chunk_count; ++index) {
        uint32_t type = 0U, length = 0U, expected = isz_expected(context, index);
        size_t written = 0U;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!isz_table_get(&table, index, &type, &length) ||
            !isz_chunk_ok(context, index, type, length))
            goto done;
        switch (type) {
        case ISZ_TYPE_ZERO:
            xx_rt_memset(plain, 0, expected);
            break;
        case ISZ_TYPE_DATA:
            if (!isz_read_at(format->device, position, plain, length) ||
                (key_size && !isz_ecb_decrypt(plain, length, key, key_size)))
                goto done;
            break;
        case ISZ_TYPE_ZLIB:
            if (!isz_read_at(format->device, position, packed, length) ||
                (key_size && !isz_ecb_decrypt(packed, length, key, key_size)) ||
                !xx_zlib_stream_decode_memory(packed, length, plain,
                                              context->chunk_size, &written) ||
                written < expected)
                goto done;
            break;
        default:
            if (!isz_read_at(format->device, position, packed, length) ||
                (key_size && !isz_ecb_decrypt(packed, length, key, key_size)))
                goto done;
            /* The stored chunk's signature is not usable; restore it. */
            packed[0] = 'B';
            packed[1] = 'Z';
            packed[2] = 'h';
            if (!xx_bzip2_decompress_memory(packed, length, plain,
                                            context->chunk_size, &written) ||
                written < expected)
                goto done;
            break;
        }
        if (!isz_write_all(destination, plain, expected)) goto done;
        position += (int64_t)length;
    }
    result = true;
done:
    if (table_open) isz_table_close(&table);
    if (plain) xx_mem_free(plain);
    if (packed) xx_mem_free(packed);
    xx_rt_memset(key, 0, sizeof(key));
    return result;
}

static void isz_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

void xx_isz_init(xx_isz *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_ISZ_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-isz");
    xx_format_set_extension(&archive->format, "isz");
    archive->format.check_is_valid = xx_isz_check_is_valid;
    archive->format.handle_base_info = xx_isz_handle_base_info;
    archive->format.get_format_size = xx_isz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_isz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_isz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_isz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_isz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_isz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_isz_free_archive_records_reading;
}

xx_isz *xx_isz_create(xx_io_device *device, int64_t base_address) {
    xx_isz *archive = (xx_isz *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_isz_init(archive, device, base_address);
    return archive;
}

void xx_isz_destroy(xx_isz *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_isz_free(xx_isz *archive) {
    if (!archive) return;
    xx_isz_destroy(archive);
    xx_mem_free(archive);
}

bool xx_isz_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    isz_context context;
    return isz_parse(format, &context, pd);
}

bool xx_isz_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    isz_context context;
    xx_isz *archive;
    if (!format || !isz_parse(format, &context, pd)) return false;
    archive = (xx_isz *)format;
    archive->number_of_records = 1U;
    archive->image_size = context.image_size;
    archive->chunk_count = context.chunk_count;
    archive->chunk_size = context.chunk_size;
    archive->encryption = context.encryption;
    archive->multi_volume = context.multi_volume;
    format->number_of_archive_records = 1U;
    format->format_size = context.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_isz_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_isz_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_isz_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_isz_handle_base_info(format, pd))
               ? ((xx_isz *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_isz_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    isz_stream *stream;
    xx_archive_record_state *state;
    isz_context context;
    if (!isz_parse(format, &context, pd)) return NULL;
    stream = (isz_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->context = context;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = isz_stream_free;
    state->total_records = 1U;
    if (!isz_copy_options(&state->options, options) ||
        !isz_set_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_isz_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_isz_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    isz_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (isz_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_isz_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    isz_stream *stream;
    const xx_var *path_option;
    const xx_var *password;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (isz_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    password = xx_format_resolve_extra_parameter(format, &state->options,
                                                 XX_META_ID_OPT_PASSWORD);
    path_option = isz_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: prove the image decodes and report that. */
        return isz_rebuild(format, &stream->context, password, NULL, pd);
    }
    if (stream->context.multi_volume ||
        stream->context.encryption == ISZ_ENC_PASSWORD ||
        (stream->context.encryption != ISZ_ENC_NONE && !password))
        return false; /* nothing to write: do not create an empty file */
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", ISZ_IMAGE_NAME)
               : xx_str_concat(base, ISZ_IMAGE_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = isz_rebuild(format, &stream->context, password, destination,
                             pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_isz_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
