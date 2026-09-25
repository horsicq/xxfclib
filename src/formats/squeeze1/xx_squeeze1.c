/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Classic CP/M-era "Squeeze" (SQ) reader.  The layout, confirmed against the
 * corpus in F:\ARC\ARC\SQUEEZE1 and against the original Greenlaw usq/sq
 * sources, is:
 *
 *   u16 LE magic       0xFF76 (76 FF on disk)
 *   u16 LE checksum    sum of every uncompressed byte, modulo 65536
 *   char[]             NUL-terminated original CP/M file name
 *   u16 LE numnodes    Huffman node count, 0 for an empty file
 *   s16 LE[numnodes*2] decode tree, two children per node.  A non-negative
 *                      value indexes a child node; a negative value v is a
 *                      leaf carrying symbol -(v + 1), where 256 is SPEOF.
 *   bitstream          LSB first within each byte, walked from node 0.
 *
 * Decoded symbols pass through the traditional 0x90 repeat escape: 0x90
 * followed by 0 emits a literal 0x90, 0x90 followed by n emits n - 1 further
 * copies of the preceding byte.  No plaintext length is stored, so decoding
 * stops at SPEOF and the header checksum is the only integrity anchor -- it
 * is therefore always verified before a file is accepted.
 *
 * This is NOT the unrelated Oracle/R:BASE "squeeze" (which shares the two
 * magic bytes but prefixes a 32-bit decoded length) and NOT the SQ container
 * with the 53 51 AC AE signature.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/squeeze1/xx_squeeze1.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include <string.h>

#ifdef SQUEEZE1
#define XX_SQUEEZE1_FILE_TYPE XX_FILE_TYPE_SQUEEZE1
#else
#define XX_SQUEEZE1_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_SQUEEZE1_HEADER_SIZE 4U
#define XX_SQUEEZE1_MAX_NAME 255U
/* NUMVALS in the original sources is 257 (0..255 plus SPEOF), so a complete
 * decode tree never needs more than 256 internal nodes. */
#define XX_SQUEEZE1_MAX_NODES 256U
#define XX_SQUEEZE1_SPEOF 256U
#define XX_SQUEEZE1_RLE_ESCAPE 0x90U
#define XX_SQUEEZE1_MAX_INPUT ((uint64_t)256U * 1024U * 1024U)
#define XX_SQUEEZE1_MAX_OUTPUT ((uint64_t)512U * 1024U * 1024U)

typedef struct squeeze1_context_s {
    uint64_t uncompressed_size;
    uint32_t tree_offset;
    uint32_t data_offset;
    uint16_t checksum;
    uint16_t node_count;
    int64_t stream_size;
    char file_name[256];
} squeeze1_context;

/* Growable decode sink.  In measure mode nothing is materialized, which lets
 * check_is_valid run the exact same decoder without paying for the buffer. */
typedef struct squeeze1_sink_s {
    uint8_t *data;
    size_t size;
    size_t capacity;
    bool materialize;
    uint16_t checksum;
} squeeze1_sink;

typedef struct squeeze1_bit_reader_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint8_t current;
    unsigned bits_left;
} squeeze1_bit_reader;

static void squeeze1_vtable_destroy(Abstractformat *self);

static uint16_t squeeze1_read16le(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static int32_t squeeze1_read_signed16le(const uint8_t *data) {
    uint16_t value = squeeze1_read16le(data);
    return (value & UINT16_C(0x8000)) != 0U
               ? (int32_t)value - INT32_C(65536)
               : (int32_t)value;
}

static bool squeeze1_read_exact_at(xx_io_device *device, int64_t offset,
                                   void *data, size_t size) {
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)data + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool squeeze1_write_all(xx_io_device *device, const void *data,
                               size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (!device || (!data && size != 0U)) return false;
    while (done < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(device, (const uint8_t *)data + done,
                             size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* The stored name is a raw CP/M 8.3 name.  Keep it recognisable but render it
 * harmless for the filesystem; anything outside the safe set becomes '_'. */
static bool squeeze1_read_name(const uint8_t *input, size_t input_size,
                               size_t offset, char *name,
                               size_t *next_offset) {
    size_t length = 0U;
    size_t index;
    if (!input || !name || !next_offset || offset >= input_size) return false;
    while (offset + length < input_size && length < XX_SQUEEZE1_MAX_NAME &&
           input[offset + length] != 0U) {
        uint8_t ch = input[offset + length];
        if (ch < 0x20U || ch >= 0x7fU) return false;
        ++length;
    }
    if (length == 0U || offset + length >= input_size ||
        input[offset + length] != 0U)
        return false;
    for (index = 0U; index < length; ++index) {
        uint8_t ch = input[offset + index];
        bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
                    ch == '-';
        name[index] = safe ? (char)ch : '_';
    }
    name[length] = '\0';
    if (xx_rt_strcmp(name, ".") == 0 || xx_rt_strcmp(name, "..") == 0)
        xx_rt_memcpy(name, "payload", sizeof("payload"));
    *next_offset = offset + length + 1U;
    return true;
}

/* Bound the declared node count against the bytes that actually exist before
 * anything is indexed, and reject a tree whose children point outside it. */
static bool squeeze1_parse_header(const uint8_t *input, size_t input_size,
                                  squeeze1_context *context) {
    size_t offset;
    size_t table_size;
    size_t index;
    uint16_t node_count;
    if (!input || !context || input_size < 7U || input[0] != 0x76U ||
        input[1] != 0xffU)
        return false;
    if (!squeeze1_read_name(input, input_size, XX_SQUEEZE1_HEADER_SIZE,
                            context->file_name, &offset))
        return false;
    if (input_size - offset < 2U) return false;
    node_count = squeeze1_read16le(input + offset);
    offset += 2U;
    if (node_count > XX_SQUEEZE1_MAX_NODES) return false;
    table_size = (size_t)node_count * 4U;
    if (table_size > input_size - offset) return false;
    for (index = 0U; index < (size_t)node_count * 2U; ++index) {
        int32_t child = squeeze1_read_signed16le(input + offset + index * 2U);
        if (child >= (int32_t)node_count ||
            (child < 0 && (uint32_t)(-child - 1) > XX_SQUEEZE1_SPEOF))
            return false;
    }
    /* An empty member is the only case with no tree at all; anything else
     * must still have room for at least one payload byte. */
    if (node_count == 0U) {
        if (squeeze1_read16le(input + 2U) != 0U) return false;
    } else if (input_size - offset - table_size == 0U) {
        return false;
    }
    context->checksum = squeeze1_read16le(input + 2U);
    context->node_count = node_count;
    context->tree_offset = (uint32_t)offset;
    context->data_offset = (uint32_t)(offset + table_size);
    context->uncompressed_size = 0U;
    context->stream_size = (int64_t)input_size;
    return true;
}

static bool squeeze1_sink_put(squeeze1_sink *sink, uint8_t value) {
    if (!sink || (uint64_t)sink->size >= XX_SQUEEZE1_MAX_OUTPUT) return false;
    if (sink->materialize) {
        if (sink->size == sink->capacity) {
            size_t wanted = sink->capacity ? sink->capacity * 2U : 65536U;
            uint8_t *grown;
            if ((uint64_t)wanted > XX_SQUEEZE1_MAX_OUTPUT)
                wanted = (size_t)XX_SQUEEZE1_MAX_OUTPUT;
            if (wanted <= sink->size) return false;
            grown = (uint8_t *)xx_mem_realloc(sink->data, wanted);
            if (!grown) return false;
            sink->data = grown;
            sink->capacity = wanted;
        }
        sink->data[sink->size] = value;
    }
    ++sink->size;
    sink->checksum = (uint16_t)(sink->checksum + value);
    return true;
}

static bool squeeze1_read_bit(squeeze1_bit_reader *reader, uint32_t *bit) {
    if (!reader || !bit) return false;
    if (reader->bits_left == 0U) {
        if (reader->position >= reader->size) return false;
        reader->current = reader->data[reader->position++];
        reader->bits_left = 8U;
    }
    *bit = (uint32_t)(reader->current & 1U);
    reader->current = (uint8_t)(reader->current >> 1U);
    --reader->bits_left;
    return true;
}

static bool squeeze1_decode_symbol(squeeze1_bit_reader *reader,
                                   const uint8_t *tree, uint16_t node_count,
                                   uint32_t *symbol) {
    uint16_t node = 0U;
    unsigned guard = 0U;
    if (!reader || !tree || node_count == 0U || !symbol) return false;
    for (;;) {
        uint32_t bit;
        int32_t child;
        if (++guard > node_count || !squeeze1_read_bit(reader, &bit))
            return false;
        child = squeeze1_read_signed16le(tree + (size_t)node * 4U +
                                         (size_t)bit * 2U);
        if (child < 0) {
            *symbol = (uint32_t)(-child - 1);
            return true;
        }
        if ((uint32_t)child >= node_count) return false;
        node = (uint16_t)child;
    }
}

/* Runs the Huffman walk and the 0x90 repeat stage to SPEOF.  With
 * @p materialize false nothing is allocated and only the length and checksum
 * come back, which is what validation needs. */
static bool squeeze1_decode(const uint8_t *input, size_t input_size,
                            const squeeze1_context *context, bool materialize,
                            uint8_t **output, uint64_t *output_size,
                            uint16_t *checksum, size_t *consumed,
                            xx_pd_struct *pd) {
    squeeze1_sink sink;
    squeeze1_bit_reader reader;
    bool repeat_pending = false;
    bool has_last = false;
    bool finished = false;
    uint8_t last = 0U;
    unsigned tick = 0U;
    if (!input || !context || !output_size || !checksum) return false;
    if (output) *output = NULL;
    *output_size = 0U;
    *checksum = 0U;
    if (consumed) *consumed = context->data_offset;
    if (context->data_offset > input_size) return false;
    xx_rt_memset(&sink, 0, sizeof(sink));
    sink.materialize = materialize;
    xx_rt_memset(&reader, 0, sizeof(reader));
    reader.data = input + context->data_offset;
    reader.size = input_size - context->data_offset;
    if (context->node_count == 0U) {
        /* Empty member: no tree, no bitstream, nothing to emit. */
        finished = true;
    }
    while (!finished) {
        uint32_t symbol;
        if ((++tick & 0xffffU) == 0U && pd && xx_pd_is_stopped(pd)) goto fail;
        if (!squeeze1_decode_symbol(&reader, input + context->tree_offset,
                                    context->node_count, &symbol))
            goto fail;
        if (symbol == XX_SQUEEZE1_SPEOF) {
            if (repeat_pending) goto fail;
            finished = true;
            break;
        }
        if (repeat_pending) {
            uint32_t count;
            repeat_pending = false;
            if (symbol == 0U) {
                if (!squeeze1_sink_put(&sink, XX_SQUEEZE1_RLE_ESCAPE))
                    goto fail;
                last = XX_SQUEEZE1_RLE_ESCAPE;
                has_last = true;
                continue;
            }
            if (!has_last) goto fail;
            for (count = 1U; count < symbol; ++count)
                if (!squeeze1_sink_put(&sink, last)) goto fail;
            continue;
        }
        if (symbol == XX_SQUEEZE1_RLE_ESCAPE) {
            repeat_pending = true;
            continue;
        }
        if (!squeeze1_sink_put(&sink, (uint8_t)symbol)) goto fail;
        last = (uint8_t)symbol;
        has_last = true;
    }
    if (sink.checksum != context->checksum) goto fail;
    if (consumed) *consumed = context->data_offset + reader.position;
    *output_size = (uint64_t)sink.size;
    *checksum = sink.checksum;
    if (materialize && output) {
        *output = sink.data;
        sink.data = NULL;
    } else if (sink.data) {
        xx_mem_free(sink.data);
    }
    return true;
fail:
    if (sink.data) xx_mem_free(sink.data);
    return false;
}

/* Reads the whole stream once and either validates it or hands back the
 * plaintext.  The input size is bounded before a single byte is allocated. */
static bool squeeze1_process(Abstractformat *self, squeeze1_context *context,
                             uint8_t **plain, uint64_t *plain_size,
                             xx_pd_struct *pd) {
    int64_t total_size;
    int64_t input_size;
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    uint64_t produced = 0U;
    uint16_t checksum = 0U;
    squeeze1_context parsed;
    bool result = false;
    if (plain) *plain = NULL;
    if (plain_size) *plain_size = 0U;
    if (!self || !self->device || self->base_address < 0 || !context ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address) return false;
    input_size = total_size - self->base_address;
    if (input_size < 7 || (uint64_t)input_size > XX_SQUEEZE1_MAX_INPUT ||
        (uint64_t)input_size > (uint64_t)SIZE_MAX)
        return false;
    input = (uint8_t *)xx_mem_alloc((size_t)input_size);
    if (!input ||
        !squeeze1_read_exact_at(self->device, self->base_address, input,
                                (size_t)input_size) ||
        !squeeze1_parse_header(input, (size_t)input_size, &parsed) ||
        !squeeze1_decode(input, (size_t)input_size, &parsed, plain != NULL,
                         &output, &produced, &checksum, NULL, pd))
        goto cleanup;
    parsed.uncompressed_size = produced;
    *context = parsed;
    if (plain) {
        *plain = output;
        output = NULL;
        if (plain_size) *plain_size = produced;
    }
    result = true;
cleanup:
    if (output) xx_mem_free(output);
    if (input) xx_mem_free(input);
    return result;
}

static bool squeeze1_copy_options(xx_list_s *destination,
                                  const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    if (!destination) return false;
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

static const xx_var *squeeze1_find_option(const xx_list_s *options,
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

static bool squeeze1_populate_record(Abstractformat *self,
                                     xx_archive_record *record) {
    const xx_squeeze1 *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size < 7)
        return false;
    archive = (const xx_squeeze1 *)self;
    if ((int64_t)archive->data_offset > self->format_size) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = (int64_t)archive->data_offset;
    record->data_offset = self->base_address + (int64_t)archive->data_offset;
    record->compressed_size = self->format_size - (int64_t)archive->data_offset;
    return xx_archive_record_set_original_name(record, archive->file_name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static void squeeze1_reset(xx_squeeze1 *archive) {
    if (!archive) return;
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
    archive->tree_offset = 0U;
    archive->data_offset = 0U;
    archive->checksum = 0U;
    archive->node_count = 0U;
    archive->file_name[0] = '\0';
}

void xx_squeeze1_init(xx_squeeze1 *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SQUEEZE1_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-squeezed");
    xx_format_set_extension(&archive->format, "sq");
    archive->format.check_is_valid = xx_squeeze1_check_is_valid;
    archive->format.handle_base_info = xx_squeeze1_handle_base_info;
    archive->format.get_format_size = xx_squeeze1_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_squeeze1_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_squeeze1_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_squeeze1_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_squeeze1_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_squeeze1_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_squeeze1_free_archive_records_reading;
    archive->format.destroy = squeeze1_vtable_destroy;
    archive->stream_end = -1;
}

xx_squeeze1 *xx_squeeze1_create(xx_io_device *device, int64_t base_address) {
    xx_squeeze1 *archive = (xx_squeeze1 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_squeeze1_init(archive, device, base_address);
    return archive;
}

void xx_squeeze1_destroy(xx_squeeze1 *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    squeeze1_reset(archive);
}

static void squeeze1_vtable_destroy(Abstractformat *self) {
    xx_squeeze1_destroy((xx_squeeze1 *)self);
}

void xx_squeeze1_free(xx_squeeze1 *archive) {
    if (!archive) return;
    xx_squeeze1_destroy(archive);
    xx_mem_free(archive);
}

bool xx_squeeze1_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    squeeze1_context context;
    return squeeze1_process(self, &context, NULL, NULL, pd);
}

bool xx_squeeze1_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    squeeze1_context context;
    xx_squeeze1 *archive;
    if (!self || !squeeze1_process(self, &context, NULL, NULL, pd)) {
        if (self) {
            squeeze1_reset((xx_squeeze1 *)self);
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_squeeze1 *)self;
    archive->uncompressed_size = context.uncompressed_size;
    archive->stream_end = self->base_address + context.stream_size;
    archive->tree_offset = context.tree_offset;
    archive->data_offset = context.data_offset;
    archive->checksum = context.checksum;
    archive->node_count = context.node_count;
    xx_rt_memcpy(archive->file_name, context.file_name,
                 sizeof(archive->file_name));
    self->format_size = context.stream_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_SQUEEZE1_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_squeeze1_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return -1;
    return self->format_size;
}

uint64_t xx_squeeze1_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return 1U;
}

bool xx_squeeze1_unpack_to_device(xx_squeeze1 *archive,
                                  xx_io_device *destination,
                                  xx_pd_struct *pd) {
    squeeze1_context context;
    uint8_t *plain = NULL;
    uint64_t plain_size = 0U;
    bool result;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_format_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !squeeze1_process(&archive->format, &context, &plain, &plain_size,
                          pd))
        return false;
    result = context.checksum == archive->checksum &&
             context.uncompressed_size == archive->uncompressed_size &&
             plain_size <= (uint64_t)SIZE_MAX &&
             (plain_size == 0U ||
              squeeze1_write_all(destination, plain, (size_t)plain_size, pd));
    if (plain) xx_mem_free(plain);
    return result;
}

xx_archive_record_state *xx_squeeze1_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid)
        return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!squeeze1_copy_options(&state->options, options) ||
        !squeeze1_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_squeeze1_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_squeeze1_archive_record_move_to_next(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    (void)pd;
    if (!self || !state || state->format != self || !state->has_record)
        return false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_squeeze1_unpack_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path;
    bool result;
    bool created = false;
    xx_squeeze1 *archive = (xx_squeeze1 *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    path_value = squeeze1_find_option(&state->options,
                                      XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        squeeze1_context context;
        return squeeze1_process(self, &context, NULL, NULL, pd) &&
               context.checksum == archive->checksum &&
               context.uncompressed_size == archive->uncompressed_size;
    }
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING ||
               path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (!base_path || !archive->file_name[0]) {
        if (owned_path) xx_str_free(owned_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\')
        destination_path = xx_str_concat3(base_path, "/", archive->file_name);
    else
        destination_path = xx_str_concat(base_path, archive->file_name);
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path || !xx_store_create_dirs_a(destination_path, false)) {
        if (destination_path) xx_str_free(destination_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        result = output && xx_squeeze1_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_squeeze1_free_archive_records_reading(Abstractformat *self,
                                              xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_squeeze1_get_uncompressed_size(const xx_squeeze1 *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_squeeze1_get_stream_end(const xx_squeeze1 *archive) {
    return archive ? archive->stream_end : -1;
}

uint16_t xx_squeeze1_get_checksum(const xx_squeeze1 *archive) {
    return archive ? archive->checksum : 0U;
}
