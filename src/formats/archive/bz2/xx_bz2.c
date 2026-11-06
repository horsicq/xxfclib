/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xx_bz2_datastructs_internal.h"
#include "xxfclib/formats/archive/bz2/xx_bz2.h"
#include "xx_bz2_defs.h"
#include "xx_bzip2_internal.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct xx_bz2_counter_s {
    uint64_t count;
    bool overflow;
    bool failed; /* The target refused a write. */
    xx_io_device *target;
} xx_bz2_counter;

#include "../../xx_single_stream_writer.h"

static void xx_bz2_vtable_destroy(Abstractformat *self);

bool xx_bz2_read_exact_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    uint8_t *bytes = (uint8_t *)buffer;
    if (!device || (!buffer && size != 0U) || offset < 0 || offset > LONG_MAX) {
        return false;
    }
    if (xx_io_seek(device, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, bytes + done, size - done);
        if (got <= 0) {
            return false;
        }
        done += (size_t)got;
    }
    return true;
}

static ssize_t xx_bz2_count_write(xx_io_device *device, const void *buffer, size_t size)
{
    xx_bz2_counter *counter;
    if (!device || !device->priv || size > (size_t)INT_MAX) {
        return -1;
    }
    counter = (xx_bz2_counter *)device->priv;
    if (counter->count > UINT64_MAX - (uint64_t)size) {
        counter->overflow = true;
        return -1;
    }
    if (counter->target) {
        ssize_t written = xx_io_write(counter->target, buffer, size);
        if (written < 0 || (size_t)written != size) {
            counter->failed = true;
            return -1;
        }
    }
    counter->count += (uint64_t)size;
    return (ssize_t)size;
}

static bool xx_bz2_header_is_valid(const uint8_t header[XX_BZ2_HEADER_SIZE])
{
    return header[0] == (uint8_t)'B' && header[1] == (uint8_t)'Z' && header[2] == (uint8_t)'h' && header[3] >= (uint8_t)'1' && header[3] <= (uint8_t)'9';
}

/* Decode all directly concatenated streams.  The decoder may buffer bytes
 * beyond an EOS marker, so unread input-buffer bytes are subtracted when the
 * exact compressed extent of each stream is calculated. ran_out (optional):
 * a failure because the input ended inside a stream; the destination then
 * holds the blocks before the cut one, each checked by its CRC. */
static bool xx_bz2_decode_streams(Abstractformat *self, xx_io_device *destination, uint64_t *uncompressed_size, int64_t *stream_size, uint8_t *first_block_size,
                                  bool *ran_out, xx_pd_struct *pd)
{
    int64_t total_size;
    int64_t offset;
    xx_bz2_counter counter;
    xx_io_device sink;
    bool first = true;

    if (ran_out) *ran_out = false;
    if (!self || !self->device || self->base_address < 0 || self->base_address > LONG_MAX || !uncompressed_size || !stream_size || !first_block_size) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address || total_size - self->base_address < XX_BZ2_MIN_STREAM_SIZE) {
        return false;
    }

    xx_mem_zero(&counter, sizeof(counter));
    xx_mem_zero(&sink, sizeof(sink));
    counter.target = destination;
    sink.write = xx_bz2_count_write;
    sink.priv = &counter;
    offset = self->base_address;

    for (;;) {
        uint8_t header[XX_BZ2_HEADER_SIZE];
        int64_t available = total_size - offset;
        int64_t loaded;
        int64_t unread;
        int64_t consumed;
        bz2_bit_reader reader;
        bool result;

        if (available < XX_BZ2_MIN_STREAM_SIZE || offset > LONG_MAX || !xx_bz2_read_exact_at(self->device, offset, header, sizeof(header)) ||
            !xx_bz2_header_is_valid(header) || xx_io_seek(self->device, (long)offset, SEEK_SET) != 0) {
            /* A later stream, whose header the end of this loop found,
             * that the end of the file cuts before its first Block's or its
             * end's: a cut file. */
            if (ran_out && !first && available < XX_BZ2_MIN_STREAM_SIZE && !(pd && xx_pd_is_stopped(pd))) *ran_out = true;
            return false;
        }
        if (first) {
            *first_block_size = (uint8_t)(header[3] - (uint8_t)'0');
        }
        bz2_br_init(&reader, self->device, NULL, 0U, available);
        result = xx_bzip2_decompress_stream(&reader, &sink, NULL, 0U, NULL, pd);
        if (!result || reader.error || counter.overflow || reader.remaining < 0 || reader.ibuf_pos > reader.ibuf_len || reader.n_bits < 0) {
            /* The input ended inside the stream: a cut file. */
            if (ran_out && !result && reader.eof && !counter.failed && !counter.overflow && !(pd && xx_pd_is_stopped(pd))) *ran_out = true;
            bz2_br_free(&reader);
            return false;
        }
        loaded = available - reader.remaining;
        unread = (int64_t)(reader.ibuf_len - reader.ibuf_pos) + (int64_t)(reader.n_bits / 8);
        if (loaded < unread) {
            bz2_br_free(&reader);
            return false;
        }
        consumed = loaded - unread;
        if (consumed < XX_BZ2_MIN_STREAM_SIZE || consumed > available) {
            bz2_br_free(&reader);
            return false;
        }
        int pad = reader.n_bits % 8;
        if (pad != 0) {
            uint32_t pad_bits = (uint32_t)((reader.bits >> (reader.n_bits - pad)) & ((1u << pad) - 1u));
            if (pad_bits != 0U) {
                bz2_br_free(&reader);
                return false;
            }
        }
        bz2_br_free(&reader);
        offset += consumed;
        first = false;

        if (total_size - offset < XX_BZ2_HEADER_SIZE || !xx_bz2_read_exact_at(self->device, offset, header, sizeof(header)) || !xx_bz2_header_is_valid(header)) {
            break;
        }
    }
    *uncompressed_size = counter.count;
    *stream_size = offset - self->base_address;
    return true;
}

static bool xx_bz2_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    if (!destination || !source) {
        return source == NULL;
    }
    for (size_t i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
        xx_meta copy;
        if (!item) {
            continue;
        }
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_bz2_find_option(const xx_list_s *options, uint32_t meta_id)
{
    if (!options) {
        return NULL;
    }
    for (size_t i = 0U; i < options->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (item && item->meta_id == meta_id) {
            return &item->var;
        }
    }
    return NULL;
}

static bool xx_bz2_populate_archive_record(Abstractformat *self, xx_archive_record *record)
{
    const xx_bz2 *bz2;
    if (!self || !record || !self->base_info_handled || !self->is_valid) {
        return false;
    }
    bz2 = (const xx_bz2 *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = XX_BZ2_HEADER_SIZE;
    record->data_offset = self->base_address;
    record->compressed_size = self->format_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME, XX_BZ2_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, bz2->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)self->format_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, XX_BZ2_COMPRESSION_METHOD) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

void xx_bz2_init(xx_bz2 *bz2, xx_io_device *dev, int64_t base_address)
{
    if (!bz2) {
        return;
    }
    xx_mem_zero(bz2, sizeof(*bz2));
    xx_format_init(&bz2->format, dev, base_address);
    bz2->format.endian = XX_ENDIAN_UNKNOWN;
    bz2->format.file_type = XX_FILE_TYPE_BZ2;
    bz2->format.handle_type = XX_HANDLE_TYPE_BZ2;
    bz2->format.format_type = XX_TYPE_ARCHIVE;
    bz2->format.is_archive = true;
    xx_format_set_mime_type(&bz2->format, "application/x-bzip2");
    xx_format_set_extension(&bz2->format, "bz2");

    bz2->format.check_is_valid = xx_bz2_check_is_valid;
    bz2->format.handle_base_info = xx_bz2_handle_base_info;
    bz2->format.get_format_size = xx_bz2_get_format_size;
    bz2->format.get_number_of_archive_records = xx_bz2_get_number_of_archive_records;
    bz2->format.create_archive_records_reading = xx_bz2_create_archive_records_reading;
    bz2->format.get_current_archive_record = xx_bz2_get_current_archive_record;
    bz2->format.unpack_current_archive_record = xx_bz2_unpack_current_archive_record;
    bz2->format.archive_record_move_to_next = xx_bz2_archive_record_move_to_next;
    bz2->format.free_archive_records_reading = xx_bz2_free_archive_records_reading;
#if !defined(XXFC_ARCHIVE_WRITE_TAR_ONLY) || !XXFC_ARCHIVE_WRITE_TAR_ONLY
    bz2->format.create_archive_records_writing = xx_bz2_create_archive_records_writing;
    bz2->format.pack_archive_record = xx_bz2_pack_archive_record;
    bz2->format.finalize_archive_records_writing = xx_bz2_finalize_archive_records_writing;
    bz2->format.free_archive_records_writing = xx_bz2_free_archive_records_writing;
#endif
    bz2->format.destroy = xx_bz2_vtable_destroy;
    bz2->stream_end = -1;
}

xx_bz2 *xx_bz2_create(xx_io_device *dev, int64_t base_address)
{
    xx_bz2 *bz2 = (xx_bz2 *)xx_mem_alloc(sizeof(*bz2));
    if (bz2) {
        xx_bz2_init(bz2, dev, base_address);
    }
    return bz2;
}

void xx_bz2_destroy(xx_bz2 *bz2)
{
    if (bz2 && bz2->format.close) {
        bz2->format.close(&bz2->format);
    }
    if (bz2) xx_format_cleanup_extra_parameters(&bz2->format);
}

static void xx_bz2_vtable_destroy(Abstractformat *self)
{
    xx_bz2_destroy((xx_bz2 *)self);
}

void xx_bz2_free(xx_bz2 *bz2)
{
    if (bz2) {
        xx_bz2_destroy(bz2);
        xx_mem_free(bz2);
    }
}

bool xx_bz2_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    uint8_t header[XX_BZ2_HEADER_SIZE];
    int64_t total_size;
    (void)pd;
    if (!self || !self->device || self->base_address < 0) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address || total_size - self->base_address < XX_BZ2_HEADER_SIZE ||
        !xx_bz2_read_exact_at(self->device, self->base_address, header, sizeof(header))) {
        return false;
    }
    return xx_bz2_header_is_valid(header);
}

bool xx_bz2_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    uint8_t header[XX_BZ2_HEADER_SIZE];
    uint64_t uncompressed_size;
    int64_t stream_size;
    int64_t total_size;
    uint8_t first_block_size;
    xx_bz2 *bz2;

    if (!self || !self->device || !xx_bz2_check_is_valid(self, pd) || !xx_bz2_read_exact_at(self->device, self->base_address, header, sizeof(header)) ||
        !xx_bz2_decode_streams(self, NULL, &uncompressed_size, &stream_size, &first_block_size, NULL, pd)) {
        if (self) {
            bz2 = (xx_bz2 *)self;
            bz2->block_size_100k = 0U;
            bz2->uncompressed_size = 0U;
            bz2->stream_end = -1;
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    total_size = xx_io_total_size(self->device);
    bz2 = (xx_bz2 *)self;
    bz2->block_size_100k = first_block_size;
    bz2->uncompressed_size = uncompressed_size;
    bz2->stream_end = self->base_address + stream_size;
    self->format_size = stream_size;
    self->number_of_archive_records = 1U;
    if (bz2->stream_end < total_size) {
        self->overlay_offset = bz2->stream_end;
        self->overlay_size = total_size - bz2->stream_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->file_type = XX_FILE_TYPE_BZ2;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_bz2_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_bz2_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return 1U;
}

bool xx_bz2_unpack_to_device(xx_bz2 *bz2, xx_io_device *destination, xx_pd_struct *pd)
{
    uint64_t uncompressed_size;
    int64_t stream_size;
    uint8_t first_block_size;
    if (!bz2 || !destination || (!bz2->format.base_info_handled && !xx_format_handle_base_info(&bz2->format, pd)) || !bz2->format.is_valid ||
        !xx_bz2_decode_streams(&bz2->format, destination, &uncompressed_size, &stream_size, &first_block_size, NULL, pd)) {
        return false;
    }
    return stream_size == bz2->format.format_size && uncompressed_size == bz2->uncompressed_size;
}

bool xx_bz2_decode_stream(xx_io_device *device, int64_t base_address, xx_io_device *destination, int64_t *stream_size, bool *cut, xx_pd_struct *pd)
{
    xx_bz2 bz2;
    uint64_t uncompressed_size;
    uint8_t first_block_size;
    bool ran_out = false;
    bool result = false;
    if (stream_size) *stream_size = -1;
    if (cut) *cut = false;
    if (!device || !destination || !stream_size || base_address < 0) return false;
    xx_bz2_init(&bz2, device, base_address);
    if (xx_bz2_check_is_valid(&bz2.format, pd)) {
        result = xx_bz2_decode_streams(&bz2.format, destination, &uncompressed_size, stream_size, &first_block_size, &ran_out, pd) && *stream_size > 0;
        if (!result && cut) *cut = ran_out;
    }
    xx_bz2_destroy(&bz2);
    return result;
}

typedef struct xx_bz2_prefix_sink_s {
    uint8_t *data;
    size_t capacity;
    size_t size;
    bool full;
} xx_bz2_prefix_sink;

/* Keeps what fits and refuses the rest, which stops the decoder. */
static ssize_t xx_bz2_prefix_write(xx_io_device *device, const void *data, size_t size)
{
    xx_bz2_prefix_sink *sink = device ? (xx_bz2_prefix_sink *)device->priv : NULL;
    size_t amount;
    if (!sink || (!data && size != 0U)) return -1;
    amount = size < sink->capacity - sink->size ? size : sink->capacity - sink->size;
    if (amount != 0U) {
        xx_mem_copy(sink->data + sink->size, data, amount);
        sink->size += amount;
    }
    if (amount == size) return (ssize_t)size;
    sink->full = true;
    return -1;
}

bool xx_bz2_decode_prefix(xx_io_device *device, int64_t base_address, void *buffer, size_t capacity, size_t *size, bool *complete, xx_pd_struct *pd)
{
    xx_bz2 bz2;
    xx_bz2_prefix_sink prefix;
    xx_io_device sink;
    uint64_t uncompressed_size;
    int64_t stream_size;
    uint8_t first_block_size;
    bool result = false;
    if (size) *size = 0U;
    if (complete) *complete = false;
    if (!device || !buffer || capacity == 0U || !size || base_address < 0) return false;
    xx_mem_zero(&prefix, sizeof(prefix));
    xx_mem_zero(&sink, sizeof(sink));
    prefix.data = (uint8_t *)buffer;
    prefix.capacity = capacity;
    sink.write = xx_bz2_prefix_write;
    sink.priv = &prefix;
    xx_bz2_init(&bz2, device, base_address);
    if (xx_bz2_check_is_valid(&bz2.format, pd)) {
        if (xx_bz2_decode_streams(&bz2.format, &sink, &uncompressed_size, &stream_size, &first_block_size, NULL, pd)) {
            if (complete) *complete = true;
            result = true;
        } else {
            result = prefix.full && !(pd && xx_pd_is_stopped(pd));
        }
    }
    xx_bz2_destroy(&bz2);
    *size = prefix.size;
    return result;
}

xx_archive_record_state *xx_bz2_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) || !self->is_valid) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_bz2_copy_options(&state->options, options) || !xx_bz2_populate_archive_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_bz2_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_bz2_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_bz2_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    const xx_var *path_value;
    const char *base_utf8 = NULL;
    char *owned_base = NULL;
    char *destination;
    bool result;
    bool created = false;
    uint64_t ignored_size;
    int64_t ignored_stream_size;
    uint8_t ignored_block_size;

    if (!self || !self->device || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    path_value = xx_bz2_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) {
        return xx_bz2_decode_streams(self, NULL, &ignored_size, &ignored_stream_size, &ignored_block_size, NULL, pd) && ignored_stream_size == self->format_size;
    }
    if (path_value->type == XX_VAR_TYPE_STRING || path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base_utf8 = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING || path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_utf8 = owned_base;
    }
    if (!base_utf8) {
        if (owned_base) {
            xx_str_free(owned_base);
        }
        return false;
    }
    if (base_utf8[0] != '\0' && base_utf8[xx_str_len(base_utf8) - 1U] != '/' && base_utf8[xx_str_len(base_utf8) - 1U] != '\\') {
        destination = xx_str_concat3(base_utf8, "/", XX_BZ2_PAYLOAD_NAME);
    } else {
        destination = xx_str_concat(base_utf8, XX_BZ2_PAYLOAD_NAME);
    }
    if (owned_base) {
        xx_str_free(owned_base);
    }
    if (!destination || !xx_store_create_dirs_a(destination, false)) {
        if (destination) {
            xx_str_free(destination);
        }
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(destination, "wb");
        created = output != NULL;
        result = output != NULL && xx_bz2_decode_streams(self, output, &ignored_size, &ignored_stream_size, &ignored_block_size, NULL, pd) &&
                 ignored_stream_size == self->format_size;
        if (output) {
            if (xx_io_close(output) != 0) {
                result = false;
            }
        }
    }
    if (!result && created) {
        xx_rt_remove(destination);
    }
    xx_str_free(destination);
    return result;
}

void xx_bz2_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

uint8_t xx_bz2_get_block_size_100k(const xx_bz2 *bz2)
{
    return bz2 ? bz2->block_size_100k : 0U;
}

uint64_t xx_bz2_get_uncompressed_size(const xx_bz2 *bz2)
{
    return bz2 ? bz2->uncompressed_size : 0U;
}

int64_t xx_bz2_get_stream_end(const xx_bz2 *bz2)
{
    return bz2 ? bz2->stream_end : -1;
}

xx_archive_write_state *xx_bz2_create_archive_records_writing(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    return ss_create(self, options, pd);
}
bool xx_bz2_pack_archive_record(Abstractformat *self, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source, xx_pd_struct *pd)
{
    return ss_pack(self, state, record, source, pd);
}
bool xx_bz2_finalize_archive_records_writing(Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd)
{
    return ss_finalize(self, state, pd);
}
void xx_bz2_free_archive_records_writing(Abstractformat *self, xx_archive_write_state *state)
{
    ss_free(self, state);
}
