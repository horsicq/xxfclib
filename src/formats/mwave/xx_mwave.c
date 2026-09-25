/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the IBM Mwave DSP distribution file (the ".Z" members of
 * the Mwave soundcard/modem subsystem: DSP task images, drivers and scripts).
 *
 * The format is undocumented; the layout below was recovered from the sample
 * corpus and holds for all 183 samples:
 *
 *   0x00  u8   0x1f 0x9d          Unix compress magic, repeated at the front
 *   0x02  u16  DOS time
 *   0x04  u16  DOS date           (1993..1995 across the corpus)
 *   0x06  u16  0x0020             constant
 *   0x08  char name[12]           original 8.3 name, NUL padded
 *   0x14  u8   0x00 0x00 0x00     reserved
 *   0x17  u8   compress flags     0x8c = block mode, 12-bit codes
 *   0x18  ...  LZW payload
 *
 * So the file is a Unix compress stream whose three-byte transport header has
 * been split: the magic sits at the very front, the flags byte immediately
 * ahead of the codes.  Rather than re-implementing LZW this reader splices the
 * two halves back together through a small read-only adapter device and hands
 * the result to the library's existing xx_compress decoder, which decoded all
 * 183 samples; the decoded RIFF/MZ images self-describe their own length,
 * which is what confirms the split is right.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mwave/xx_mwave.h"

#include "xxfclib/algo/compress/xx_compress.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef MWAVE
#define XX_MWAVE_FILE_TYPE XX_FILE_TYPE_MWAVE
#else
#define XX_MWAVE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define MWAVE_HEADER_SIZE 24U
#define MWAVE_NAME_SIZE 12U
#define MWAVE_CONSTANT 0x0020U
#define MWAVE_FALLBACK_NAME "payload"
#define MWAVE_MAX_INPUT ((uint64_t)256U * 1024U * 1024U)
#define MWAVE_MAX_OUTPUT ((uint64_t)1024U * 1024U * 1024U)

static void xx_mwave_vtable_destroy(Abstractformat *self);

/* --- Source adapter: {0x1f, 0x9d, flags} followed by the packed extent. --- */

typedef struct mwave_source_s {
    xx_io_device *base;
    int64_t data_offset;
    int64_t data_size;
    int64_t position;
    uint8_t head[3];
} mwave_source;

static ssize_t mwave_source_read(xx_io_device *self, void *buffer, size_t size) {
    mwave_source *source = self ? (mwave_source *)self->priv : NULL;
    uint8_t *out = (uint8_t *)buffer;
    size_t done = 0U;
    int64_t total;
    if (!source || (!buffer && size != 0U) || source->position < 0) return -1;
    total = 3 + source->data_size;
    if (source->position > total) return -1;
    while (done < size && source->position < 3) {
        out[done++] = source->head[source->position++];
    }
    if (done < size && source->position < total) {
        size_t wanted = size - done;
        ssize_t amount;
        if ((int64_t)wanted > total - source->position)
            wanted = (size_t)(total - source->position);
        if (xx_io_seek64(source->base,
                         source->data_offset + source->position - 3,
                         SEEK_SET) != 0)
            return -1;
        amount = xx_io_read(source->base, out + done, wanted);
        if (amount < 0 || (size_t)amount > wanted) return -1;
        source->position += amount;
        done += (size_t)amount;
    }
    return (ssize_t)done;
}

static int mwave_source_seek64(xx_io_device *self, int64_t offset, int whence) {
    mwave_source *source = self ? (mwave_source *)self->priv : NULL;
    int64_t total, target;
    if (!source) return -1;
    total = 3 + source->data_size;
    if (whence == SEEK_SET) target = offset;
    else if (whence == SEEK_CUR) target = source->position + offset;
    else if (whence == SEEK_END) target = total + offset;
    else return -1;
    if (target < 0 || target > total) return -1;
    source->position = target;
    return 0;
}

static int mwave_source_seek(xx_io_device *self, long offset, int whence) {
    return mwave_source_seek64(self, (int64_t)offset, whence);
}

static int64_t mwave_source_tell(xx_io_device *self) {
    mwave_source *source = self ? (mwave_source *)self->priv : NULL;
    return source ? source->position : -1;
}

static int64_t mwave_source_total_size(xx_io_device *self) {
    mwave_source *source = self ? (mwave_source *)self->priv : NULL;
    return source ? 3 + source->data_size : -1;
}

/* --- Output sink: counts plaintext and optionally forwards it. --- */

typedef struct mwave_sink_s {
    xx_io_device *target;
    xx_pd_struct *pd;
    uint64_t total;
    uint64_t limit;
    bool failed;
} mwave_sink;

static ssize_t mwave_sink_write(xx_io_device *device, const void *data,
                                size_t size) {
    mwave_sink *sink = device ? (mwave_sink *)device->priv : NULL;
    size_t done = 0U;
    if (!sink || (!data && size != 0U) ||
        (sink->pd && xx_pd_is_stopped(sink->pd)) ||
        (uint64_t)size > sink->limit - sink->total) {
        if (sink) sink->failed = true;
        return -1;
    }
    while (sink->target && done < size) {
        ssize_t amount = xx_io_write(sink->target, (const uint8_t *)data + done,
                                     size - done);
        if (amount <= 0 || (size_t)amount > size - done) {
            sink->failed = true;
            return -1;
        }
        done += (size_t)amount;
    }
    sink->total += (uint64_t)size;
    return (ssize_t)size;
}

/* --- Header --- */

typedef struct mwave_header_s {
    char name[MWAVE_NAME_SIZE + 1U];
    uint16_t dos_time;
    uint16_t dos_date;
    uint8_t flags;
    int64_t packed_size;
} mwave_header;

static uint16_t mwave_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static bool mwave_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Names are flat 8.3 strings; some samples leave stale bytes after the
 * terminator, so only the leading run is taken. */
static void mwave_extract_name(const uint8_t *bytes, char *out) {
    size_t index, used = 0U;
    for (index = 0U; index < MWAVE_NAME_SIZE; ++index) {
        uint8_t c = bytes[index];
        if (c == 0U) break;
        if (c < 0x20U || c == '/' || c == '\\' || c == '"' || c == '*' ||
            c == ':' || c == '<' || c == '>' || c == '?' || c == '|')
            out[used++] = '_';
        else
            out[used++] = (char)c;
    }
    while (used != 0U && (out[used - 1U] == ' ' || out[used - 1U] == '.'))
        --used;
    out[used] = 0;
}

static bool mwave_parse_header(Abstractformat *format, mwave_header *result) {
    uint8_t header[MWAVE_HEADER_SIZE];
    int64_t total, size;
    unsigned maximum_bits;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size <= (int64_t)MWAVE_HEADER_SIZE ||
        (uint64_t)size > MWAVE_MAX_INPUT ||
        !mwave_read_at(format->device, format->base_address, header,
                       sizeof(header)))
        return false;
    if (header[0] != XX_COMPRESS_MAGIC0 || header[1] != XX_COMPRESS_MAGIC1 ||
        mwave_le16(header + 6U) != MWAVE_CONSTANT ||
        header[20] != 0U || header[21] != 0U || header[22] != 0U)
        return false;
    /* The flags byte is the standard compress one; refuse anything the
     * decoder would not accept rather than guessing a width. */
    if ((header[23] & 0x60U) != 0U) return false;
    maximum_bits = header[23] & 0x1fU;
    if (maximum_bits < 9U || maximum_bits > 16U) return false;
    xx_mem_zero(result, sizeof(*result));
    mwave_extract_name(header + 8U, result->name);
    if (result->name[0] == 0)
        xx_rt_memcpy(result->name, MWAVE_FALLBACK_NAME,
                     xx_rt_strlen(MWAVE_FALLBACK_NAME) + 1U);
    result->dos_time = mwave_le16(header + 2U);
    result->dos_date = mwave_le16(header + 4U);
    result->flags = header[23];
    result->packed_size = size - (int64_t)MWAVE_HEADER_SIZE;
    return true;
}

/* Splice the compress transport header back together and decode the whole
 * remaining extent.  The stream carries neither a length nor a checksum, so a
 * member occupies the rest of the device by definition. */
static bool mwave_decode_stream(Abstractformat *self, xx_io_device *destination,
                                const mwave_header *header,
                                uint64_t *uncompressed_size,
                                xx_pd_struct *pd) {
    mwave_source source;
    xx_io_device source_device;
    mwave_sink output;
    xx_io_device sink;
    int64_t decoded_size = -1;
    if (!self || !self->device || !header || !uncompressed_size ||
        header->packed_size <= 0 || (pd && xx_pd_is_stopped(pd)))
        return false;
    xx_mem_zero(&source, sizeof(source));
    xx_mem_zero(&source_device, sizeof(source_device));
    xx_mem_zero(&output, sizeof(output));
    xx_mem_zero(&sink, sizeof(sink));
    source.base = self->device;
    source.data_offset = self->base_address + (int64_t)MWAVE_HEADER_SIZE;
    source.data_size = header->packed_size;
    source.head[0] = XX_COMPRESS_MAGIC0;
    source.head[1] = XX_COMPRESS_MAGIC1;
    source.head[2] = header->flags;
    source_device.read = mwave_source_read;
    source_device.seek = mwave_source_seek;
    source_device.seek64 = mwave_source_seek64;
    source_device.tell = mwave_source_tell;
    source_device.total_size = mwave_source_total_size;
    source_device.priv = &source;
    output.target = destination;
    output.pd = pd;
    output.limit = MWAVE_MAX_OUTPUT;
    sink.write = mwave_sink_write;
    sink.priv = &output;
    if (!xx_compress_decode_device(&source_device, 0, 3 + header->packed_size,
                                   &sink, &decoded_size, pd) ||
        output.failed || decoded_size < 0 ||
        (uint64_t)decoded_size != output.total)
        return false;
    *uncompressed_size = output.total;
    return true;
}

static bool mwave_copy_options(xx_list_s *destination,
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

static const xx_var *mwave_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool mwave_populate_record(Abstractformat *self,
                                  xx_archive_record *record) {
    const xx_mwave *archive;
    if (!self || !record || !self->base_info_handled || !self->is_valid ||
        self->format_size <= (int64_t)MWAVE_HEADER_SIZE)
        return false;
    archive = (const xx_mwave *)self;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address;
    record->header_size = (int64_t)MWAVE_HEADER_SIZE;
    record->data_offset = self->base_address + (int64_t)MWAVE_HEADER_SIZE;
    record->compressed_size = self->format_size - (int64_t)MWAVE_HEADER_SIZE;
    return xx_archive_record_set_original_name(record, archive->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          archive->uncompressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)record->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          archive->compress_flags) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          archive->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          archive->dos_date) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_mwave_init(xx_mwave *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MWAVE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mwave");
    xx_format_set_extension(&archive->format, "z");
    archive->format.check_is_valid = xx_mwave_check_is_valid;
    archive->format.handle_base_info = xx_mwave_handle_base_info;
    archive->format.get_format_size = xx_mwave_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_mwave_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_mwave_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_mwave_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_mwave_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_mwave_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_mwave_free_archive_records_reading;
    archive->format.destroy = xx_mwave_vtable_destroy;
    archive->stream_end = -1;
}

xx_mwave *xx_mwave_create(xx_io_device *device, int64_t base_address) {
    xx_mwave *archive = (xx_mwave *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_mwave_init(archive, device, base_address);
    return archive;
}

void xx_mwave_destroy(xx_mwave *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->uncompressed_size = 0U;
    archive->stream_end = -1;
}

static void xx_mwave_vtable_destroy(Abstractformat *self) {
    xx_mwave_destroy((xx_mwave *)self);
}

void xx_mwave_free(xx_mwave *archive) {
    if (!archive) return;
    xx_mwave_destroy(archive);
    xx_mem_free(archive);
}

bool xx_mwave_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    mwave_header header;
    uint64_t size = 0U;
    return mwave_parse_header(self, &header) &&
           mwave_decode_stream(self, NULL, &header, &size, pd);
}

bool xx_mwave_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    mwave_header header;
    uint64_t size = 0U;
    xx_mwave *archive;
    if (!self) return false;
    if (!mwave_parse_header(self, &header) ||
        !mwave_decode_stream(self, NULL, &header, &size, pd)) {
        archive = (xx_mwave *)self;
        archive->uncompressed_size = 0U;
        archive->stream_end = -1;
        archive->name[0] = 0;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    archive = (xx_mwave *)self;
    archive->uncompressed_size = size;
    archive->dos_time = header.dos_time;
    archive->dos_date = header.dos_date;
    archive->compress_flags = header.flags;
    xx_rt_memcpy(archive->name, header.name,
                 xx_rt_strlen(header.name) + 1U);
    self->format_size = (int64_t)MWAVE_HEADER_SIZE + header.packed_size;
    archive->stream_end = self->base_address + self->format_size;
    self->number_of_archive_records = 1U;
    self->overlay_offset = -1;
    self->overlay_size = 0;
    self->file_type = XX_MWAVE_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_mwave_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_mwave_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_mwave_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_mwave_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_mwave_unpack_to_device(xx_mwave *archive, xx_io_device *destination,
                               xx_pd_struct *pd) {
    mwave_header header;
    uint64_t size = 0U;
    if (!archive || !destination ||
        (!archive->format.base_info_handled &&
         !xx_mwave_handle_base_info(&archive->format, pd)) ||
        !archive->format.is_valid ||
        !mwave_parse_header(&archive->format, &header) ||
        !mwave_decode_stream(&archive->format, destination, &header, &size, pd))
        return false;
    return size == archive->uncompressed_size;
}

xx_archive_record_state *xx_mwave_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_mwave_handle_base_info(self, pd)) ||
        !self->is_valid) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!mwave_copy_options(&state->options, options) ||
        !mwave_populate_record(self, &state->current_record)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_mwave_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_mwave_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    (void)pd;
    if (!self || !state || state->format != self) return false;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_mwave_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base_path = NULL;
    char *owned_path = NULL;
    char *destination_path = NULL;
    bool result = false;
    bool created = false;
    xx_mwave *archive = (xx_mwave *)self;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    path_value = mwave_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) return xx_mwave_check_is_valid(self, pd);
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW)
        base_path = xx_var_get_str(path_value);
    else if (path_value->type == XX_VAR_TYPE_WSTRING ||
             path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base_path = owned_path;
    }
    if (base_path && archive->name[0]) {
        destination_path =
            (base_path[0] && base_path[xx_str_len(base_path) - 1U] != '/' &&
             base_path[xx_str_len(base_path) - 1U] != '\\')
                ? xx_str_concat3(base_path, "/", archive->name)
                : xx_str_concat(base_path, archive->name);
    }
    if (owned_path) xx_str_free(owned_path);
    if (!destination_path) return false;
    if (xx_store_create_dirs_a(destination_path, false)) {
        xx_io_device *output = xx_io_file_open(destination_path, "wb");
        created = output != NULL;
        result = output && xx_mwave_unpack_to_device(archive, output, pd);
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(destination_path);
    xx_str_free(destination_path);
    return result;
}

void xx_mwave_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_mwave_get_uncompressed_size(const xx_mwave *archive) {
    return archive ? archive->uncompressed_size : 0U;
}

int64_t xx_mwave_get_stream_end(const xx_mwave *archive) {
    return archive ? archive->stream_end : -1;
}
