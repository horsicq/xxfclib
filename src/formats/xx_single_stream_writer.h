/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Shared private writer for the single-file gzip, bzip2 and XZ containers.
 * Payloads stream through borrowed devices. No input or output is spooled.
 */
#ifndef XX_SINGLE_STREAM_WRITER_H
#define XX_SINGLE_STREAM_WRITER_H
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/formats/xz/xx_xz.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include <limits.h>
#include <string.h>

typedef struct ss_writer {
    bool written, finalized, failed;
    uint64_t output_size;
} ss_writer;
typedef struct ss_source {
    xx_io_device device;
    xx_io_device *target;
    xx_pd_struct *pd;
    int64_t size, position;
    uint32_t crc;
    size_t capacity;
} ss_source;
typedef struct ss_output {
    xx_io_device device;
    xx_io_device *target;
    xx_pd_struct *pd;
    uint64_t size, limit;
    size_t capacity;
    bool failed;
} ss_output;

static bool ss_error(xx_pd_struct *pd, const char *message) {
    xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, message);
    return false;
}
static bool ss_uint(const xx_var *value, uint64_t *number) {
    if (!value || value->type < XX_VAR_TYPE_INT8 ||
        value->type > XX_VAR_TYPE_UINT64 ||
        (value->type <= XX_VAR_TYPE_INT64 && xx_var_get_i64(value) < 0))
        return false;
    *number = xx_var_get_u64(value);
    return true;
}
static const xx_var *ss_option(Abstractformat *format,
                              const xx_archive_write_state *state,
                              const xx_archive_record *record, uint32_t id) {
    const xx_var *value = record ? xx_archive_record_find_meta(record, id) : NULL;
    return value ? value : xx_format_resolve_extra_parameter(format,
                                                       &state->options, id);
}
static bool ss_settings(Abstractformat *format,
                        const xx_archive_write_state *state,
                        const xx_archive_record *record, int *level,
                        uint64_t *memory, uint64_t *max_size,
                        xx_pd_struct *pd) {
    const xx_var *value;
    uint64_t number;
    uint64_t method = format->file_type == XX_FILE_TYPE_GZ ? 8U :
                      format->file_type == XX_FILE_TYPE_BZ2 ? 12U : 0x21U;
    *level = format->file_type == XX_FILE_TYPE_BZ2 ? XX_BZIP2_LEVEL_DEFAULT : 6;
    *memory = UINT64_C(256) * 1024U * 1024U;
    *max_size = UINT64_MAX;
    value = ss_option(format, state, record, XX_META_ID_COMPRESSION_LEVEL);
    if (value) {
        if (!ss_uint(value, &number) || number > 9U ||
            (format->file_type == XX_FILE_TYPE_BZ2 && number == 0U))
            return ss_error(pd, "Invalid single-stream compression level");
        *level = (int)number;
    }
    value = ss_option(format, state, record, XX_META_ID_COMPRESSION_METHOD);
    if (value && (!ss_uint(value, &number) || number != method))
        return ss_error(pd, "Unsupported single-stream compression method");
    value = ss_option(format, state, record, XX_META_ID_OPT_MEMORY_LIMIT);
    if (value && !ss_uint(value, memory))
        return ss_error(pd, "Invalid writer memory limit");
    value = ss_option(format, state, record, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (value && !ss_uint(value, max_size))
        return ss_error(pd, "Invalid writer member size limit");
    value = ss_option(format, state, record, XX_META_ID_ENCRYPTION_METHOD);
    if ((value && (!ss_uint(value, &number) || number != 0U)) ||
        ss_option(format, state, record, XX_META_ID_OPT_PASSWORD) ||
        (record && xx_archive_record_get_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false)))
        return ss_error(pd, "This single-stream container does not support passwords");
    return true;
}
static uint64_t ss_retained(const xx_archive_write_state *state) {
    return sizeof(*state) + sizeof(ss_writer) +
           (uint64_t)state->options.count * sizeof(xx_meta) * 2U + 4096U;
}
/* Conservative upper bounds for the current native codec workspaces:
 * Deflate has its token table, two hash tables and window; BWT uses four
 * uint32 arrays in addition to the raw/RLE/BWT blocks; LZMA2 reuses a 64KiB
 * dictionary workspace with a 4MiB hash table. Include transfer staging.
 */
static bool ss_budget(Abstractformat *format, const xx_archive_write_state *state,
                      int level, uint64_t memory, bool codec, xx_pd_struct *pd) {
    uint64_t needed = ss_retained(state);
    size_t capacity = xx_get_file_buffer_size();
    if (!capacity || capacity > (size_t)PTRDIFF_MAX ||
        (uint64_t)capacity > (UINT64_MAX - needed) / 2U)
        return ss_error(pd, "Invalid writer transfer buffer size");
    if (codec) {
        needed += (uint64_t)capacity * 2U;
        if (format->file_type == XX_FILE_TYPE_GZ) needed += 2U * 1024U * 1024U;
        else if (format->file_type == XX_FILE_TYPE_BZ2)
            needed += (uint64_t)level * 100000U * 21U + 256U * 1024U;
        else needed += 8U * 1024U * 1024U;
    }
    return needed <= memory || ss_error(pd, "Compression workspace exceeds writer memory limit");
}
static ssize_t ss_source_read(xx_io_device *device, void *data, size_t size) {
    ss_source *source = (ss_source *)device->priv;
    ssize_t amount;
    if (xx_pd_is_stopped(source->pd) || (!data && size) ||
        size > (size_t)PTRDIFF_MAX) return -1;
    if ((uint64_t)size > (uint64_t)(source->size - source->position))
        size = (size_t)(source->size - source->position);
    if (size > source->capacity) size = source->capacity;
    if (!size) return 0;
    amount = xx_io_read(source->target, data, size);
    if (amount <= 0 || (size_t)amount > size) return -1;
    source->crc = xx_crc32_calc(source->crc, data, (size_t)amount);
    source->position += amount;
    return amount;
}
static int64_t ss_source_size(xx_io_device *device) {
    return ((ss_source *)device->priv)->size;
}
static int64_t ss_source_tell(xx_io_device *device) {
    return ((ss_source *)device->priv)->position;
}
static int ss_source_seek64(xx_io_device *device, int64_t offset, int whence) {
    ss_source *source = (ss_source *)device->priv;
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? source->position :
                   whence == SEEK_END ? source->size : -1;
    /* The codecs may seek to zero before reading; disallow CRC-breaking rereads. */
    return base >= 0 && offset >= -base && offset <= source->size - base &&
           base + offset == source->position ? 0 : -1;
}
static int ss_source_seek(xx_io_device *device, long offset, int whence) {
    return ss_source_seek64(device, offset, whence);
}
static ssize_t ss_output_write(xx_io_device *device, const void *data, size_t size) {
    ss_output *output = (ss_output *)device->priv;
    size_t done = 0;
    if (output->failed || (!data && size) || size > (size_t)PTRDIFF_MAX ||
        (uint64_t)size > output->limit - output->size) return -1;
    while (done < size) {
        size_t want = size - done;
        ssize_t amount;
        if (xx_pd_is_stopped(output->pd)) { output->failed = true; return -1; }
        if (want > output->capacity) want = output->capacity;
        amount = xx_io_write(output->target, (const uint8_t *)data + done, want);
        if (amount <= 0 || (size_t)amount > want) { output->failed = true; return -1; }
        done += (size_t)amount;
        output->size += (uint64_t)amount;
    }
    return (ssize_t)size;
}
static int64_t ss_output_size(xx_io_device *device) {
    return (int64_t)((ss_output *)device->priv)->size;
}
static void ss_put32(uint8_t *bytes, uint32_t value) {
    unsigned i;
    for (i = 0; i < 4U; ++i) bytes[i] = (uint8_t)(value >> (i * 8U));
}
static bool ss_gzip(ss_source *source, ss_output *output, int level,
                    xx_pd_struct *pd) {
    uint8_t header[10] = {0x1f,0x8b,8,0,0,0,0,0,0,255};
    uint8_t trailer[8];
    if (level == 9) header[8] = 2;
    else if (level == 1) header[8] = 4;
    if (xx_io_write(&output->device, header, sizeof(header)) != sizeof(header)) return false;
    if (source->size == 0) {
        /* The codec's zero-length API emits no bytes. RFC1951 still requires
         * a final block: fixed Huffman BFINAL=1/BTYPE=1 followed by EOB256. */
        static const uint8_t empty_deflate[2] = {3, 0};
        if (xx_io_write(&output->device, empty_deflate, sizeof(empty_deflate)) != sizeof(empty_deflate)) return false;
    } else if (!xx_deflate_pack_device(&source->device, 0, source->size,
                                      &output->device, level, false, pd)) return false;
    ss_put32(trailer, source->crc);
    ss_put32(trailer + 4, (uint32_t)source->size);
    return xx_io_write(&output->device, trailer, sizeof(trailer)) == sizeof(trailer);
}
static xx_archive_write_state *ss_create(Abstractformat *self,
                                         const xx_list_s *options,
                                         xx_pd_struct *pd) {
    xx_archive_write_state *state;
    ss_writer *writer;
    size_t i;
    int level;
    uint64_t memory, max_size;
    if (!self || !self->device || !self->device->write ||
        self->base_address < 0 || xx_pd_is_stopped(pd)) return NULL;
    state = (xx_archive_write_state *)xx_mem_alloc(sizeof(*state));
    writer = (ss_writer *)xx_mem_calloc(1, sizeof(*writer));
    if (!state || !writer) { xx_mem_free(state); xx_mem_free(writer); return NULL; }
    xx_archive_write_state_init(state, self);
    state->internal_state = writer;
    state->free_internal = xx_mem_free;
    /* Only numeric options are copied. Reject unsupported options before I/O. */
    if (options && options->count > 5U) goto bad;
    for (i = 0; options && i < options->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(options, i);
        xx_meta copy;
        uint64_t number;
        if (!item || !ss_uint(&item->var, &number)) goto bad;
        switch (item->meta_id) {
            case XX_META_ID_COMPRESSION_LEVEL: case XX_META_ID_COMPRESSION_METHOD:
            case XX_META_ID_OPT_MEMORY_LIMIT: case XX_META_ID_OPT_MAX_MEMBER_SIZE:
            case XX_META_ID_ENCRYPTION_METHOD: break;
            default: goto bad;
        }
        if (xx_format_resolve_extra_parameter(NULL, &state->options, item->meta_id)) goto bad;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(&state->options, &copy)) {
            xx_meta_cleanup(&copy); goto bad;
        }
    }
    if (!ss_settings(self, state, NULL, &level, &memory, &max_size, pd) ||
        !ss_budget(self, state, level, memory, false, pd)) goto bad;
    state->total_records = 0;
    return state;
bad:
    ss_error(pd, "Unsupported or over-budget single-stream writer options");
    xx_archive_write_state_free(state);
    return NULL;
}
static bool ss_pack(Abstractformat *self, xx_archive_write_state *state,
                    const xx_archive_record *record, xx_io_device *source,
                    xx_pd_struct *pd) {
    ss_writer *writer;
    ss_source checked;
    ss_output output;
    const char *name;
    uint64_t memory, max_size;
    int level;
    int64_t size, saved;
    bool result;
    if (!self || !state || state->format != self ||
        !(writer = (ss_writer *)state->internal_state)) return false;
    if (writer->failed || writer->finalized || writer->written || !record ||
        !source || !source->read || source == self->device || xx_pd_is_stopped(pd)) goto invalid;
    name = xx_archive_record_get_original_name(record);
    if (xx_archive_record_get_meta_bool(record, XX_META_ID_IS_FOLDER, false) ||
        xx_archive_record_find_meta(record, XX_META_ID_LINK_TARGET) ||
        (name && name[0] && (name[strlen(name)-1] == '/' || name[strlen(name)-1] == '\\')))
        goto invalid;
    size = xx_io_total_size(source);
    saved = xx_io_tell(source);
    if (size < 0 || saved < 0 || !ss_settings(self, state, record, &level,
        &memory, &max_size, pd) || (uint64_t)size > max_size ||
        !ss_budget(self, state, level, memory, true, pd)) goto invalid;
    if (xx_io_seek64(source, 0, SEEK_SET) != 0) goto invalid;
    if (xx_io_seek64(self->device, self->base_address, SEEK_SET) != 0) {
        (void)xx_io_seek64(source, saved, SEEK_SET); goto invalid;
    }
    memset(&checked, 0, sizeof(checked));
    checked.target = source; checked.pd = pd; checked.size = size;
    checked.capacity = xx_get_file_buffer_size();
    checked.device.priv = &checked; checked.device.read = ss_source_read;
    checked.device.seek = ss_source_seek; checked.device.seek64 = ss_source_seek64;
    checked.device.tell = ss_source_tell;
    checked.device.total_size = checked.device.get_total_size = checked.device.size = ss_source_size;
    memset(&output, 0, sizeof(output));
    output.target = self->device; output.pd = pd;
    output.limit = (uint64_t)(INT64_MAX - self->base_address);
    output.capacity = checked.capacity;
    output.device.priv = &output; output.device.write = ss_output_write;
    output.device.tell = output.device.total_size = output.device.get_total_size = output.device.size = ss_output_size;
    self->is_valid = false; self->base_info_handled = false; self->format_size = -1;
    if (self->file_type == XX_FILE_TYPE_GZ) result = ss_gzip(&checked, &output, level, pd);
    else if (self->file_type == XX_FILE_TYPE_BZ2)
        result = xx_bzip2_pack_device(&checked.device, 0, size, &output.device, level, pd);
    else result = xx_xz_pack_to_device(&checked.device, 0, size, &output.device, level, pd);
    if (xx_io_seek64(source, saved, SEEK_SET) != 0) result = false;
    if (!result || output.failed || checked.position != size || xx_pd_is_stopped(pd)) goto invalid;
    writer->written = true; writer->output_size = output.size;
    state->current_index = 0; state->total_records = 1;
    return true;
invalid:
    writer->failed = true;
    return ss_error(pd, "Single-stream writer requires exactly one regular file within its limits");
}
static bool ss_finalize(Abstractformat *self, xx_archive_write_state *state,
                        xx_pd_struct *pd) {
    ss_writer *writer;
    int level;
    uint64_t memory, max_size;
    if (!self || !state || state->format != self ||
        !(writer = (ss_writer *)state->internal_state) || writer->failed ||
        !writer->written || xx_pd_is_stopped(pd) ||
        !ss_settings(self, state, NULL, &level, &memory, &max_size, pd) ||
        !ss_budget(self, state, level, memory, false, pd)) return false;
    writer->finalized = true;
    self->format_size = (int64_t)writer->output_size;
    self->number_of_archive_records = 1;
    return true;
}
static void ss_free(Abstractformat *self, xx_archive_write_state *state) {
    (void)self;
    xx_archive_write_state_free(state);
}
#endif
