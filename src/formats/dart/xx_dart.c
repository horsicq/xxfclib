/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DART (Apple's Disk Archival/Retrieval Tool) disk images.  xx_dart.h carries
 * the layout.  Written from the format structure; the acceptance rules follow
 * the behaviour of CiderPress II (DiskArc.Disk.DART, Apache-2.0), which was
 * used as the oracle: same header checks, same 0xFFFF "stored raw" escape,
 * same zero-preset LZHUF ring, and input bytes past a block's stored length
 * read as zero.  This reader is stricter in one place: a block that does not
 * expand to exactly 20960 bytes fails the extraction instead of leaving zeros.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dart/xx_dart.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef DART
#define XX_DART_FILE_TYPE XX_FILE_TYPE_DART
#else
#define XX_DART_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define DART_SD_COUNT 40U
#define DART_HD_COUNT 72U
#define DART_MAX_HEADER (4U + 2U * DART_HD_COUNT)
#define DART_RAW 0xFFFFU
/* CiderPress II reads a compressed block into a 32 KiB buffer; nothing longer
 * is a DART block (the encoder stores a block raw instead). */
#define DART_MAX_PACKED 0x8000U

#define DART_NAME_IMAGE "image.img"
#define DART_NAME_TAGS "tags.bin"

typedef struct dart_info_s {
    uint32_t compression;
    uint32_t disk_type;
    uint32_t disk_kib;
    uint32_t blocks;
    uint32_t header_size;
    uint16_t lengths[DART_HD_COUNT];
    int64_t stored_size;
} dart_info;

typedef struct dart_stream_s {
    dart_info info;
    size_t index;
    size_t count;
} dart_stream;

/* ---------------------------------------------------------------------- */
/* I/O helpers                                                             */

static bool dart_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool dart_write_all(xx_io_device *device, const uint8_t *data,
                           size_t size) {
    size_t done = 0U;
    if (!device) return true;
    while (done < size) {
        ssize_t wrote = xx_io_write(device, data + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return false;
        done += (size_t)wrote;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* LZHUF: LZSS over an adaptive Huffman tree (Okumura/Yoshizaki, 1988),    */
/* the parameter set DART uses.                                            */

#define LZ_N 4096U
#define LZ_F 60U
#define LZ_THRESHOLD 2U
#define LZ_NCHAR (256U - LZ_THRESHOLD + LZ_F) /* 314 symbols */
#define LZ_T (LZ_NCHAR * 2U - 1U)              /* 627 tree nodes */
#define LZ_ROOT (LZ_T - 1U)
#define LZ_MAX_FREQ 0x8000U

typedef struct dart_lzh_s {
    uint16_t freq[LZ_T + 1U];          /* + a sentinel above the root */
    uint16_t parent[LZ_T + LZ_NCHAR];  /* nodes, then the leaf slots */
    uint16_t child[LZ_T + 1U];         /* left child; a leaf holds sym + T */
    uint8_t ring[LZ_N];
    const uint8_t *input;
    size_t input_size;
    size_t input_pos;
    uint32_t acc;
    uint32_t acc_bits;
} dart_lzh;

static void lz_tree_start(dart_lzh *z) {
    uint32_t leaf, node, pair;
    for (leaf = 0U; leaf < LZ_NCHAR; ++leaf) {
        z->freq[leaf] = 1U;
        z->child[leaf] = (uint16_t)(leaf + LZ_T);
        z->parent[leaf + LZ_T] = (uint16_t)leaf;
    }
    pair = 0U;
    for (node = LZ_NCHAR; node <= LZ_ROOT; ++node) {
        z->freq[node] = (uint16_t)(z->freq[pair] + z->freq[pair + 1U]);
        z->child[node] = (uint16_t)pair;
        z->parent[pair] = (uint16_t)node;
        z->parent[pair + 1U] = (uint16_t)node;
        pair += 2U;
    }
    z->freq[LZ_T] = 0xFFFFU;
    z->parent[LZ_ROOT] = 0U;
    z->child[LZ_T] = 0U;
}

/* Halve every leaf weight and rebuild the internal nodes in sorted order. */
static void lz_tree_rebuild(dart_lzh *z) {
    uint32_t from, to = 0U, pair, node, slot, move;
    for (from = 0U; from < LZ_T; ++from) {
        if (z->child[from] >= LZ_T) {
            z->freq[to] = (uint16_t)((z->freq[from] + 1U) / 2U);
            z->child[to] = z->child[from];
            ++to;
        }
    }
    pair = 0U;
    for (node = LZ_NCHAR; node < LZ_T; ++node) {
        uint16_t weight = (uint16_t)(z->freq[pair] + z->freq[pair + 1U]);
        slot = node;
        while (slot > 0U && weight < z->freq[slot - 1U]) --slot;
        for (move = node; move > slot; --move) {
            z->freq[move] = z->freq[move - 1U];
            z->child[move] = z->child[move - 1U];
        }
        z->freq[slot] = weight;
        z->child[slot] = (uint16_t)pair;
        pair += 2U;
    }
    for (node = 0U; node < LZ_T; ++node) {
        uint32_t c = z->child[node];
        if (c >= LZ_T) {
            z->parent[c] = (uint16_t)node;
        } else {
            z->parent[c] = (uint16_t)node;
            z->parent[c + 1U] = (uint16_t)node;
        }
    }
}

/* Count one occurrence of @p symbol, keeping the weights sorted. */
static void lz_tree_count(dart_lzh *z, uint32_t symbol) {
    uint32_t node, guard = 0U;
    if (z->freq[LZ_ROOT] == LZ_MAX_FREQ) lz_tree_rebuild(z);
    node = z->parent[symbol + LZ_T];
    do {
        uint32_t weight = (uint32_t)z->freq[node] + 1U, swap;
        z->freq[node] = (uint16_t)weight;
        swap = node + 1U;
        if (weight > z->freq[swap]) {
            uint32_t a, b;
            while (swap < LZ_T && weight > z->freq[swap + 1U]) ++swap;
            z->freq[node] = z->freq[swap];
            z->freq[swap] = (uint16_t)weight;
            a = z->child[node];
            z->parent[a] = (uint16_t)swap;
            if (a < LZ_T) z->parent[a + 1U] = (uint16_t)swap;
            b = z->child[swap];
            z->child[swap] = (uint16_t)a;
            z->parent[b] = (uint16_t)node;
            if (b < LZ_T) z->parent[b + 1U] = (uint16_t)node;
            z->child[node] = (uint16_t)b;
            node = swap;
        }
        node = z->parent[node];
    } while (node != 0U && ++guard < LZ_T);
}

static void lz_need(dart_lzh *z, uint32_t bits) {
    while (z->acc_bits < bits) {
        uint32_t byte = 0U;
        if (z->input_pos < z->input_size) byte = z->input[z->input_pos];
        ++z->input_pos;
        z->acc = (z->acc << 8U) | byte;
        z->acc_bits += 8U;
    }
}

static uint32_t lz_bit(dart_lzh *z) {
    lz_need(z, 1U);
    --z->acc_bits;
    return (z->acc >> z->acc_bits) & 1U;
}

static uint32_t lz_byte(dart_lzh *z) {
    lz_need(z, 8U);
    z->acc_bits -= 8U;
    return (z->acc >> z->acc_bits) & 0xFFU;
}

static bool lz_symbol(dart_lzh *z, uint32_t *symbol) {
    uint32_t node = z->child[LZ_ROOT], depth = 0U;
    while (node < LZ_T) {
        if (++depth > LZ_T) return false;
        node = z->child[node + lz_bit(z)];
    }
    node -= LZ_T;
    if (node >= LZ_NCHAR) return false;
    lz_tree_count(z, node);
    *symbol = node;
    return true;
}

/* A distance is one byte whose top bits select a 6-bit high part and how
 * many more bits follow; the low six bits of the widened value are kept. */
static uint32_t lz_distance(dart_lzh *z) {
    uint32_t value = lz_byte(z), high, extra;
    if (value < 0x20U) {
        high = 0U; extra = 1U;
    } else if (value < 0x50U) {
        high = 1U + ((value - 0x20U) >> 4U); extra = 2U;
    } else if (value < 0x90U) {
        high = 4U + ((value - 0x50U) >> 3U); extra = 3U;
    } else if (value < 0xC0U) {
        high = 12U + ((value - 0x90U) >> 2U); extra = 4U;
    } else if (value < 0xF0U) {
        high = 24U + ((value - 0xC0U) >> 1U); extra = 5U;
    } else {
        high = 48U + (value - 0xF0U); extra = 6U;
    }
    while (extra--) value = (value << 1U) | lz_bit(z);
    return (high << 6U) | (value & 0x3FU);
}

static bool dart_lzh_decode(dart_lzh *z, const uint8_t *input,
                            size_t input_size, uint8_t *output,
                            size_t output_size) {
    size_t produced = 0U;
    uint32_t cursor = LZ_N - LZ_F;
    xx_mem_zero(z, sizeof(*z));
    lz_tree_start(z);
    z->input = input;
    z->input_size = input_size;
    while (produced < output_size) {
        uint32_t symbol;
        /* Every symbol yields at least one byte, so this loop is bounded by
         * the output size.  Bytes past the input read as zero, exactly as in
         * the reference: the format has no check value, so a damaged block
         * still expands (to damaged data) there too. */
        if (!lz_symbol(z, &symbol)) return false;
        if (symbol < 256U) {
            output[produced++] = (uint8_t)symbol;
            z->ring[cursor] = (uint8_t)symbol;
            cursor = (cursor + 1U) & (LZ_N - 1U);
        } else {
            uint32_t source = (cursor - lz_distance(z) - 1U) & (LZ_N - 1U);
            uint32_t length = symbol - 256U + LZ_THRESHOLD + 1U, k;
            for (k = 0U; k < length && produced < output_size; ++k) {
                uint8_t value = z->ring[(source + k) & (LZ_N - 1U)];
                output[produced++] = value;
                z->ring[cursor] = value;
                cursor = (cursor + 1U) & (LZ_N - 1U);
            }
        }
    }
    return true;
}

bool xx_dart_lzhuf_decode_memory(const uint8_t *input, size_t input_size,
                                 uint8_t *output, size_t output_size) {
    dart_lzh *z;
    bool result;
    if ((!input && input_size != 0U) || (!output && output_size != 0U))
        return false;
    z = (dart_lzh *)xx_mem_alloc(sizeof(*z));
    if (!z) return false;
    result = dart_lzh_decode(z, input, input_size, output, output_size);
    xx_mem_free(z);
    return result;
}

/* ---------------------------------------------------------------------- */
/* RLE on big-endian 16-bit words                                          */

static bool dart_rle_decode(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t output_size) {
    size_t in = 0U, out = 0U;
    while (in < input_size) {
        int32_t count;
        if (input_size - in < 2U) return false;
        count = (int16_t)(uint16_t)(((uint32_t)input[in] << 8U) | input[in + 1U]);
        in += 2U;
        if (count > 0) {
            size_t bytes = (size_t)count * 2U;
            if (bytes > input_size - in || bytes > output_size - out)
                return false;
            xx_rt_memcpy(output + out, input + in, bytes);
            in += bytes;
            out += bytes;
        } else if (count < 0) {
            size_t repeat = (size_t)(-count), k;
            uint8_t hi, lo;
            if (input_size - in < 2U || repeat * 2U > output_size - out)
                return false;
            hi = input[in];
            lo = input[in + 1U];
            in += 2U;
            for (k = 0U; k < repeat; ++k) {
                output[out++] = hi;
                output[out++] = lo;
            }
        } else {
            return false;
        }
    }
    return out == output_size;
}

/* ---------------------------------------------------------------------- */
/* Header                                                                  */

static bool dart_kind_ok(uint32_t type, uint32_t kib) {
    if (type >= 1U && type <= 3U) return kib == 400U || kib == 800U;
    if (type >= 0x10U && type <= 0x12U) return kib == 720U || kib == 1440U;
    return false;
}

static uint64_t dart_stored_length(const dart_info *info, uint32_t index) {
    uint32_t value = info->lengths[index];
    if (value == DART_RAW || info->compression == 2U)
        return XX_DART_BLOCK_SIZE;
    return info->compression == 0U ? (uint64_t)value * 2U : (uint64_t)value;
}

static bool dart_parse(Abstractformat *format, dart_info *out) {
    uint8_t header[DART_MAX_HEADER];
    dart_info info;
    int64_t total, size;
    uint32_t count, index;
    uint64_t sum;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < 4 + 2 * (int64_t)DART_SD_COUNT ||
        !dart_read_at(format->device, format->base_address, header, 4U))
        return false;
    xx_mem_zero(&info, sizeof(info));
    info.compression = header[0];
    info.disk_type = header[1];
    info.disk_kib = ((uint32_t)header[2] << 8U) | header[3];
    if (info.compression > 2U || !dart_kind_ok(info.disk_type, info.disk_kib))
        return false;
    count = info.disk_kib > 800U ? DART_HD_COUNT : DART_SD_COUNT;
    info.blocks = info.disk_kib / 20U;
    info.header_size = 4U + 2U * count;
    if (size < (int64_t)info.header_size ||
        !dart_read_at(format->device, format->base_address + 4, header + 4U,
                      2U * count))
        return false;
    sum = info.header_size;
    for (index = 0U; index < count; ++index) {
        uint32_t value = ((uint32_t)header[4U + 2U * index] << 8U) |
                         header[5U + 2U * index];
        info.lengths[index] = (uint16_t)value;
        if (index >= info.blocks) {
            if (value != 0U) return false;
            continue;
        }
        if (value == 0U) return false;
        if (info.compression == 2U) {
            if (value != XX_DART_BLOCK_SIZE && value != DART_RAW) return false;
        } else if (value != DART_RAW) {
            uint64_t bytes = info.compression == 0U ? (uint64_t)value * 2U
                                                    : (uint64_t)value;
            if (bytes > DART_MAX_PACKED) return false;
            /* RLE needs a count word and at least one data word. */
            if (info.compression == 0U && value < 2U) return false;
        }
        sum += dart_stored_length(&info, index);
    }
    if (sum > (uint64_t)size) return false;
    info.stored_size = (int64_t)sum;
    *out = info;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Expansion                                                               */

static bool dart_expand(Abstractformat *format, const dart_info *info,
                        xx_io_device *data_out, xx_io_device *tag_out,
                        xx_pd_struct *pd) {
    uint8_t *packed = NULL, *block = NULL;
    dart_lzh *z = NULL;
    int64_t offset;
    uint32_t index;
    bool result = false;
    packed = (uint8_t *)xx_mem_alloc(DART_MAX_PACKED > XX_DART_BLOCK_SIZE
                                         ? DART_MAX_PACKED
                                         : XX_DART_BLOCK_SIZE);
    block = (uint8_t *)xx_mem_alloc(XX_DART_BLOCK_SIZE);
    if (info->compression == 1U) z = (dart_lzh *)xx_mem_alloc(sizeof(*z));
    if (!packed || !block || (info->compression == 1U && !z)) goto done;
    offset = format->base_address + (int64_t)info->header_size;
    for (index = 0U; index < info->blocks; ++index) {
        uint64_t stored = dart_stored_length(info, index);
        bool raw = info->compression == 2U || info->lengths[index] == DART_RAW;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (raw) {
            if (!dart_read_at(format->device, offset, block,
                              XX_DART_BLOCK_SIZE))
                goto done;
        } else {
            if (!dart_read_at(format->device, offset, packed, (size_t)stored))
                goto done;
            if (info->compression == 0U) {
                if (!dart_rle_decode(packed, (size_t)stored, block,
                                     XX_DART_BLOCK_SIZE))
                    goto done;
            } else if (!dart_lzh_decode(z, packed, (size_t)stored, block,
                                        XX_DART_BLOCK_SIZE)) {
                goto done;
            }
        }
        offset += (int64_t)stored;
        if (!dart_write_all(data_out, block, XX_DART_BLOCK_DATA) ||
            !dart_write_all(tag_out, block + XX_DART_BLOCK_DATA,
                            XX_DART_BLOCK_TAGS))
            goto done;
    }
    result = true;
done:
    if (z) xx_mem_free(z);
    if (block) xx_mem_free(block);
    if (packed) xx_mem_free(packed);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool dart_copy_options(xx_list_s *destination,
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

static const xx_var *dart_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool dart_set_record(xx_archive_record *record, const dart_info *info,
                            int64_t base, size_t index) {
    uint64_t size = index == 0U ? (uint64_t)info->disk_kib * 1024U
                                : (uint64_t)info->disk_kib * 24U;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base;
    record->header_size = info->header_size;
    record->data_offset = base + (int64_t)info->header_size;
    record->compressed_size =
        info->stored_size - (int64_t)info->header_size;
    return xx_archive_record_set_original_name(
               record, index == 0U ? DART_NAME_IMAGE : DART_NAME_TAGS) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)(info->stored_size - (int64_t)info->header_size)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          info->compression) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void dart_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

void xx_dart_init(xx_dart *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_DART_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-dart");
    xx_format_set_extension(&archive->format, "dart");
    archive->format.check_is_valid = xx_dart_check_is_valid;
    archive->format.handle_base_info = xx_dart_handle_base_info;
    archive->format.get_format_size = xx_dart_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_dart_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_dart_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_dart_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_dart_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_dart_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_dart_free_archive_records_reading;
}

xx_dart *xx_dart_create(xx_io_device *device, int64_t base_address) {
    xx_dart *archive = (xx_dart *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_dart_init(archive, device, base_address);
    return archive;
}

void xx_dart_destroy(xx_dart *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_dart_free(xx_dart *archive) {
    if (!archive) return;
    xx_dart_destroy(archive);
    xx_mem_free(archive);
}

bool xx_dart_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    dart_info info;
    (void)pd;
    return dart_parse(format, &info);
}

bool xx_dart_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    dart_info info;
    xx_dart *archive;
    (void)pd;
    if (!format || !dart_parse(format, &info)) return false;
    archive = (xx_dart *)format;
    archive->number_of_records = 2U;
    archive->compression = info.compression;
    archive->disk_type = info.disk_type;
    archive->disk_kib = info.disk_kib;
    archive->block_count = info.blocks;
    archive->header_size = info.header_size;
    archive->stored_size = info.stored_size;
    format->number_of_archive_records = 2U;
    format->format_size = info.stored_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_dart_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_dart_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_dart_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_dart_handle_base_info(format, pd))
               ? ((xx_dart *)format)->number_of_records : 0U;
}

bool xx_dart_unpack_to_device(xx_dart *archive, xx_io_device *data_destination,
                              xx_io_device *tag_destination,
                              xx_pd_struct *pd) {
    dart_info info;
    if (!archive || !dart_parse(&archive->format, &info)) return false;
    return dart_expand(&archive->format, &info, data_destination,
                       tag_destination, pd);
}

xx_archive_record_state *xx_dart_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    dart_stream *stream;
    xx_archive_record_state *state;
    dart_info info;
    (void)pd;
    if (!dart_parse(format, &info)) return NULL;
    stream = (dart_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->info = info;
    stream->count = 2U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = dart_stream_free;
    state->total_records = 2U;
    if (!dart_copy_options(&state->options, options) ||
        !dart_set_record(&state->current_record, &stream->info,
                         format->base_address, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_dart_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_dart_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    dart_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (dart_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!dart_set_record(&state->current_record, &stream->info,
                         format->base_address, stream->index)) {
        state->has_record = false;
        return false;
    }
    return true;
}

bool xx_dart_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    dart_stream *stream;
    const xx_var *path_option;
    const char *base = NULL, *name;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    dart_info check;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (dart_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    /* The header is re-read so a device that changed underneath is caught. */
    if (!dart_parse(format, &check) ||
        check.stored_size != stream->info.stored_size ||
        check.compression != stream->info.compression ||
        check.disk_kib != stream->info.disk_kib)
        return false;
    name = stream->index == 0U ? DART_NAME_IMAGE : DART_NAME_TAGS;
    path_option = dart_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return dart_expand(format, &check, NULL, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", name)
               : xx_str_concat(base, name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = stream->index == 0U
                     ? dart_expand(format, &check, destination, NULL, pd)
                     : dart_expand(format, &check, NULL, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_dart_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
