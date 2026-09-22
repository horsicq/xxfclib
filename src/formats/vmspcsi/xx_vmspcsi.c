/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OpenVMS PCSI$COMPRESSED kits: "OpenVMS DCX PCSI Compressed File".
 *
 * The WHOLE FILE is the unit of work.  The coding tables live in a blob near
 * the front and every record after them is decoded against those tables, so a
 * member cannot be described by one contiguous range.
 *
 * File header (0x50 bytes; the ASCII banner is the magic)
 *   +0x00  "OpenVMS DCX PCSI Compressed File"
 *   +0x38  u32le  size of the table blob
 *   +0x40  u32le  record count
 *
 * The table blob sits at file offset 0x200:
 *   +0x00  u32le  size, equal to the header's copy, at least 0x14
 *   +0x04  u32le  0
 *   +0x08  u32le  0x5BF5A3A7   DCX magic
 *   +0x0c  u32le  0
 *   +0x10  u16le  context count
 *   +0x12  u16le  0x14         header length
 * followed by that many variable-length context blocks.  Each block is
 * expanded into a fixed 0x440-byte slot, zero filled first, because the codec
 * indexes its three tables by node number and by symbol with no bounds of its
 * own; the fixed slot is what makes those indices safe:
 *   +0x00  u16le  block length on disk
 *   +0x02  u8     first symbol
 *   +0x03  u8     last symbol (>= first)
 *   +0x06  u16le  0x000c  header length
 *   +0x08  u16le  node table offset (> 0x0c, at most 0x40 leaf-bitmap bytes)
 *   +0x0a  u16le  context map offset (0 = every symbol maps to context 0)
 * The leaf bitmap goes to slot[0x000..0x040), the node table to
 * slot[0x040..0x240) and the map to slot[0x240 + first*2 ...].
 *
 * Two silent traps, both reproduced here:
 *   * the reader position after the last context block is rounded UP to the
 *     next 0x200 boundary before the records start;
 *   * every record begins with a 2-byte header that is read and discarded.
 *
 * The coding is a per-context binary tree walked LSB-first: a 1 bit moves to
 * the odd sibling, a leaf emits the node table's byte and switches to the
 * context the map names for that symbol, an interior node jumps to child*2,
 * and an interior child of 0 ends the record.
 *
 * Ported from XArchive Algos/xvmspcsidecoder.cpp and
 * packages/xvmspcsiarchive.cpp.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/vmspcsi/xx_vmspcsi.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef VMSPCSI
#define XX_VMSPCSI_FILE_TYPE XX_FILE_TYPE_VMSPCSI
#else
#define XX_VMSPCSI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PCSI_BANNER "OpenVMS DCX PCSI Compressed File"
#define PCSI_BANNER_SIZE 32U
#define PCSI_HEADER_SIZE 0x50
#define PCSI_TABLE_OFFSET 0x200
#define PCSI_TABLE_HEADER_SIZE 0x14
#define PCSI_BLOCK_HEADER_SIZE 0x0c
#define PCSI_SLOT_SIZE 0x440
#define PCSI_SLOT_NODE 0x40
#define PCSI_SLOT_MAP 0x240
#define PCSI_MAX_NODE 0x200
#define PCSI_DCX_MAGIC UINT32_C(0x5bf5a3a7)
#define PCSI_RECORD_HEADER_SIZE 2
#define PCSI_MAX_RECORDS 0x1000000
#define PCSI_MAX_INPUT_SIZE ((int64_t)0x20000000)
#define PCSI_MAX_OUTPUT ((int64_t)0x20000000)

typedef struct pcsi_tables_s {
    uint8_t *slots;
    int32_t context_count;
    int32_t record_count;
    int64_t records_offset;
} pcsi_tables;

typedef struct pcsi_stream_s {
    uint8_t *image;
    int64_t image_size;
    char *name;
    int64_t uncompressed_size;
    int32_t record_count;
    size_t index;
    size_t count;
} pcsi_stream;

static uint32_t pcsi_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint16_t pcsi_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static int64_t pcsi_round_up(int64_t value) {
    return (value + (PCSI_TABLE_OFFSET - 1)) & ~(int64_t)(PCSI_TABLE_OFFSET - 1);
}

static bool pcsi_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool pcsi_parse_header(const uint8_t *data, int64_t size,
                              uint32_t *table_size, int32_t *record_count) {
    uint32_t count;
    if (size < PCSI_HEADER_SIZE ||
        size < (PCSI_TABLE_OFFSET + PCSI_TABLE_HEADER_SIZE))
        return false;
    if (xx_rt_memcmp(data, PCSI_BANNER, (size_t)PCSI_BANNER_SIZE) != 0)
        return false;
    *table_size = pcsi_le32(data + 0x38);
    count = pcsi_le32(data + 0x40);
    /* A count of zero produces no output at all: a failure, not an empty
     * file. */
    if (count < 1U || count > (uint32_t)PCSI_MAX_RECORDS) return false;
    if (*table_size < (uint32_t)PCSI_TABLE_HEADER_SIZE) return false;
    *record_count = (int32_t)count;
    return true;
}

static void pcsi_tables_cleanup(pcsi_tables *tables) {
    if (!tables) return;
    if (tables->slots) xx_mem_free(tables->slots);
    xx_mem_zero(tables, sizeof(*tables));
}

static bool pcsi_parse_tables(const uint8_t *data, int64_t size,
                              uint32_t table_size, pcsi_tables *tables,
                              xx_pd_struct *pd) {
    int64_t position = PCSI_TABLE_OFFSET;
    int64_t slot_bytes;
    int32_t context_count;
    int32_t index;
    if (!tables) return false;
    if ((size - position) < PCSI_TABLE_HEADER_SIZE) return false;
    if (pcsi_le32(data + position) != table_size ||
        pcsi_le32(data + position + 0x04) != 0U ||
        pcsi_le32(data + position + 0x08) != PCSI_DCX_MAGIC ||
        pcsi_le32(data + position + 0x0c) != 0U ||
        pcsi_le16(data + position + 0x12) != (uint16_t)PCSI_TABLE_HEADER_SIZE)
        return false;
    context_count = (int32_t)pcsi_le16(data + position + 0x10);
    if (context_count < 1) return false;
    /* Every block costs at least its own 0x0c-byte header on disk, so the
     * blob's size bounds the slot array and a crafted count cannot amplify
     * the allocation. */
    if ((int64_t)context_count >
        ((int64_t)table_size - PCSI_TABLE_HEADER_SIZE) / PCSI_BLOCK_HEADER_SIZE)
        return false;
    slot_bytes = (int64_t)context_count * PCSI_SLOT_SIZE;
    if (slot_bytes > (int64_t)INT32_MAX) return false;
    tables->slots = (uint8_t *)xx_mem_calloc((size_t)slot_bytes, 1U);
    if (!tables->slots) return false;
    tables->context_count = context_count;

    position += PCSI_TABLE_HEADER_SIZE;
    for (index = 0; index < context_count; ++index) {
        int64_t block_length, block_header, node_offset, map_offset;
        int64_t bitmap_size, symbol_count, node_size;
        int32_t first, last;
        uint8_t *slot;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if ((size - position) < PCSI_BLOCK_HEADER_SIZE) goto fail;
        block_length = (int64_t)pcsi_le16(data + position);
        first = data[position + 0x02];
        last = data[position + 0x03];
        block_header = (int64_t)pcsi_le16(data + position + 0x06);
        node_offset = (int64_t)pcsi_le16(data + position + 0x08);
        map_offset = (int64_t)pcsi_le16(data + position + 0x0a);
        if (first > last || data[position + 0x04] != 0U ||
            data[position + 0x05] != 0U ||
            block_header != PCSI_BLOCK_HEADER_SIZE)
            goto fail;
        if (node_offset <= PCSI_BLOCK_HEADER_SIZE ||
            (node_offset - PCSI_BLOCK_HEADER_SIZE) > PCSI_SLOT_NODE)
            goto fail;
        if (block_length < node_offset || block_length > (size - position))
            goto fail;
        slot = tables->slots + (int64_t)index * PCSI_SLOT_SIZE;
        bitmap_size = node_offset - PCSI_BLOCK_HEADER_SIZE;
        xx_rt_memcpy(slot, data + position + PCSI_BLOCK_HEADER_SIZE,
                     (size_t)bitmap_size);
        symbol_count = (int64_t)last - first + 1;
        if (map_offset == 0) {
            node_size = block_length - node_offset;
            if (node_size <= 0 || node_size > (PCSI_SLOT_MAP - PCSI_SLOT_NODE))
                goto fail;
            xx_rt_memcpy(slot + PCSI_SLOT_NODE, data + position + node_offset,
                         (size_t)node_size);
        } else {
            int64_t map_start;
            node_size = map_offset - node_offset;
            if (node_size <= 0 || node_size > (PCSI_SLOT_MAP - PCSI_SLOT_NODE))
                goto fail;
            if ((block_length - map_offset) != (symbol_count * 2)) goto fail;
            xx_rt_memcpy(slot + PCSI_SLOT_NODE, data + position + node_offset,
                         (size_t)node_size);
            map_start = PCSI_SLOT_MAP + (int64_t)first * 2;
            if (map_start < PCSI_SLOT_MAP ||
                (map_start + symbol_count * 2) > PCSI_SLOT_SIZE)
                goto fail;
            xx_rt_memcpy(slot + map_start, data + position + map_offset,
                         (size_t)(symbol_count * 2));
        }
        position += block_length;
    }

    /* Every symbol of every context must name a context that exists.  Doing it
     * once here is what keeps the inner decode loop free of a range test. */
    for (index = 0; index < context_count; ++index) {
        const uint8_t *slot = tables->slots + (int64_t)index * PCSI_SLOT_SIZE;
        int32_t symbol;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        for (symbol = 0; symbol < 256; ++symbol)
            if ((int32_t)pcsi_le16(slot + PCSI_SLOT_MAP + symbol * 2) >=
                context_count)
                goto fail;
    }
    tables->records_offset = pcsi_round_up(position);
    if (tables->records_offset < 0 || tables->records_offset > size) goto fail;
    return true;
fail:
    pcsi_tables_cleanup(tables);
    return false;
}

/* One pass over every record.  output may be NULL, in which case the walk only
 * counts; that is how the output size is learned without an allocation. */
static bool pcsi_run_records(const uint8_t *data, int64_t size,
                             int64_t position, const uint8_t *slots,
                             int32_t record_count, uint8_t *output,
                             int64_t capacity, int64_t *produced_out,
                             xx_pd_struct *pd) {
    int64_t produced = 0;
    int32_t record;
    if (!produced_out) return false;
    for (record = 0; record < record_count; ++record) {
        const uint8_t *context = slots;
        int32_t node = 0;
        uint32_t accumulator = 0U;
        int32_t bits_left = 0;
        if (pd && xx_pd_is_stopped(pd)) return false;
        /* The 2-byte record header the reference reads and discards. */
        if ((size - position) < PCSI_RECORD_HEADER_SIZE) return false;
        position += PCSI_RECORD_HEADER_SIZE;
        for (;;) {
            uint32_t bit;
            if (bits_left == 0) {
                if (position >= size) return false;
                accumulator = data[position];
                ++position;
                bits_left = 8;
            }
            bit = accumulator & 1U;
            accumulator >>= 1U;
            --bits_left;
            if (bit) ++node;
            if (node >= PCSI_MAX_NODE) return false;
            if (context[node >> 3] & (1U << (node & 7))) {
                int32_t symbol = context[PCSI_SLOT_NODE + node];
                int32_t next;
                if (produced >= capacity) return false;
                if (output) output[produced] = (uint8_t)symbol;
                ++produced;
                next = (int32_t)pcsi_le16(context + PCSI_SLOT_MAP + symbol * 2);
                context = slots + (int64_t)next * PCSI_SLOT_SIZE;
                node = 0;
            } else {
                int32_t child = context[PCSI_SLOT_NODE + node];
                if (child == 0) break;
                node = child * 2;
            }
        }
    }
    *produced_out = produced;
    return true;
}

static bool pcsi_prepare(const uint8_t *image, int64_t size,
                         pcsi_tables *tables, xx_pd_struct *pd) {
    uint32_t table_size = 0U;
    int32_t record_count = 0;
    xx_mem_zero(tables, sizeof(*tables));
    if (size < (PCSI_TABLE_OFFSET + PCSI_TABLE_HEADER_SIZE)) return false;
    if (!pcsi_parse_header(image, size, &table_size, &record_count))
        return false;
    if ((int64_t)table_size > (size - PCSI_TABLE_OFFSET)) return false;
    if (!pcsi_parse_tables(image, size, table_size, tables, pd)) return false;
    tables->record_count = record_count;
    return true;
}

static void pcsi_stream_free(void *opaque) {
    pcsi_stream *stream = (pcsi_stream *)opaque;
    if (!stream) return;
    if (stream->name) xx_str_free(stream->name);
    if (stream->image) xx_mem_free(stream->image);
    xx_mem_free(stream);
}

/* Cheap gate: the banner and the table blob, without the record walk. */
static bool pcsi_check_header(Abstractformat *format, int64_t *input_size) {
    uint8_t header[PCSI_HEADER_SIZE];
    uint32_t table_size = 0U;
    int32_t record_count = 0;
    int64_t total, size;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (PCSI_TABLE_OFFSET + PCSI_TABLE_HEADER_SIZE) ||
        size > PCSI_MAX_INPUT_SIZE)
        return false;
    if (!pcsi_read_at(format->device, format->base_address, header,
                      sizeof(header)))
        return false;
    if (!pcsi_parse_header(header, size, &table_size, &record_count))
        return false;
    if ((int64_t)table_size > (size - PCSI_TABLE_OFFSET)) return false;
    if (input_size) *input_size = size;
    return true;
}

/* Full parse: loads the container and measures the single member. */
static bool pcsi_parse(Abstractformat *format, bool measure,
                       pcsi_stream **result, xx_pd_struct *pd) {
    pcsi_stream *stream = NULL;
    pcsi_tables tables;
    int64_t size = 0;
    xx_mem_zero(&tables, sizeof(tables));
    if (!result || !pcsi_check_header(format, &size)) return false;
    stream = (pcsi_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->image = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!stream->image ||
        !pcsi_read_at(format->device, format->base_address, stream->image,
                      (size_t)size))
        goto fail;
    stream->image_size = size;
    /* The container stores no name; the kit always comes back out as this. */
    stream->name = xx_str_dup("FILE.PCSI");
    if (!stream->name) goto fail;
    stream->count = 1U;
    stream->uncompressed_size = -1;
    if (!pcsi_prepare(stream->image, size, &tables, pd)) goto fail;
    stream->record_count = tables.record_count;
    if (measure) {
        int64_t produced = 0;
        if (!pcsi_run_records(stream->image, size, tables.records_offset,
                              tables.slots, tables.record_count, NULL,
                              PCSI_MAX_OUTPUT, &produced, pd) ||
            produced <= 0 || produced > (int64_t)INT32_MAX)
            goto fail;
        stream->uncompressed_size = produced;
    }
    pcsi_tables_cleanup(&tables);
    *result = stream;
    return true;
fail:
    pcsi_tables_cleanup(&tables);
    pcsi_stream_free(stream);
    return false;
}

static bool pcsi_decode(const pcsi_stream *stream, uint8_t **plain,
                        int64_t *plain_size, xx_pd_struct *pd) {
    pcsi_tables tables;
    uint8_t *output = NULL;
    int64_t produced = 0;
    xx_mem_zero(&tables, sizeof(tables));
    if (!stream || !plain || !plain_size || stream->uncompressed_size <= 0)
        return false;
    if (!pcsi_prepare(stream->image, stream->image_size, &tables, pd))
        return false;
    output = (uint8_t *)xx_mem_alloc((size_t)stream->uncompressed_size);
    if (!output) goto fail;
    if (!pcsi_run_records(stream->image, stream->image_size,
                          tables.records_offset, tables.slots,
                          tables.record_count, output,
                          stream->uncompressed_size, &produced, pd) ||
        produced != stream->uncompressed_size)
        goto fail;
    pcsi_tables_cleanup(&tables);
    *plain = output;
    *plain_size = produced;
    return true;
fail:
    pcsi_tables_cleanup(&tables);
    if (output) xx_mem_free(output);
    return false;
}

static bool pcsi_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *pcsi_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool pcsi_set_record(xx_archive_record *record,
                            const pcsi_stream *stream, int64_t base_address) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base_address;
    record->header_size = PCSI_HEADER_SIZE;
    record->data_offset = base_address + PCSI_TABLE_OFFSET;
    record->compressed_size = stream->image_size - PCSI_TABLE_OFFSET;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)(stream->image_size - PCSI_TABLE_OFFSET)) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               stream->uncompressed_size > 0
                   ? (uint64_t)stream->uncompressed_size
                   : 0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          "OpenVMS DCX") &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_vmspcsi_init(xx_vmspcsi *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_VMSPCSI_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-vms-pcsi");
    xx_format_set_extension(&archive->format, "pcsi$compressed");
    archive->format.check_is_valid = xx_vmspcsi_check_is_valid;
    archive->format.handle_base_info = xx_vmspcsi_handle_base_info;
    archive->format.get_format_size = xx_vmspcsi_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_vmspcsi_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_vmspcsi_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_vmspcsi_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_vmspcsi_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_vmspcsi_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_vmspcsi_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_vmspcsi *xx_vmspcsi_create(xx_io_device *device, int64_t base_address) {
    xx_vmspcsi *archive = (xx_vmspcsi *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_vmspcsi_init(archive, device, base_address);
    return archive;
}

void xx_vmspcsi_destroy(xx_vmspcsi *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_vmspcsi_free(xx_vmspcsi *archive) {
    if (!archive) return;
    xx_vmspcsi_destroy(archive);
    xx_mem_free(archive);
}

bool xx_vmspcsi_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    pcsi_stream *stream;
    if (!pcsi_parse(format, false, &stream, pd)) return false;
    pcsi_stream_free(stream);
    return true;
}

bool xx_vmspcsi_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    pcsi_stream *stream;
    xx_vmspcsi *archive;
    if (!format || !pcsi_parse(format, false, &stream, pd)) return false;
    archive = (xx_vmspcsi *)format;
    archive->number_of_records = 1U;
    archive->archive_end = format->base_address + stream->image_size;
    format->number_of_archive_records = 1U;
    format->format_size = stream->image_size;
    format->is_valid = true;
    format->base_info_handled = true;
    pcsi_stream_free(stream);
    return true;
}

int64_t xx_vmspcsi_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vmspcsi_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_vmspcsi_get_number_of_archive_records(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_vmspcsi_handle_base_info(format, pd))
               ? ((xx_vmspcsi *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_vmspcsi_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    pcsi_stream *stream;
    xx_archive_record_state *state;
    if (!pcsi_parse(format, true, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pcsi_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pcsi_stream_free;
    state->total_records = 1;
    if (!pcsi_copy_options(&state->options, options) ||
        !pcsi_set_record(&state->current_record, stream,
                         format->base_address)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_vmspcsi_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_vmspcsi_archive_record_move_to_next(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    pcsi_stream *stream;
    (void)pd;
    (void)format;
    if (state && (stream = (pcsi_stream *)state->internal_state) != NULL)
        ++stream->index;
    if (state) state->has_record = false;
    return false;
}

bool xx_vmspcsi_unpack_current_archive_record(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    pcsi_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    int64_t plain_size = 0;
    size_t written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (pcsi_stream *)state->internal_state) ||
        stream->index != 0U || (pd && xx_pd_is_stopped(pd)))
        return false;
    if (!pcsi_decode(stream, &plain, &plain_size, pd)) goto done;
    path_option = pcsi_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
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
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < (size_t)plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         (size_t)plain_size - written);
            if (amount <= 0 || (size_t)amount > (size_t)plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_vmspcsi_free_archive_records_reading(Abstractformat *format,
                                             xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
