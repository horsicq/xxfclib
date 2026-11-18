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
#include "xx_gz_datastructs_internal.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/formats/archive/gz/xx_gz.h"
#include "xx_gz_defs.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "../../../algo/deflate/xx_deflate_internal.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct xx_gz_archive_stream_s {
    size_t index;
} xx_gz_archive_stream;

typedef struct xx_gz_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t written;
    uint32_t crc32;
    bool failed;
} xx_gz_sink;

#include "../../xx_single_stream_writer.h"
#include "../../xx_archive_salvage.h"

static void xx_gz_vtable_destroy(Abstractformat *self);

static bool xx_gz_read_exact_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t transfer_capacity = xx_get_file_buffer_size();
    uint8_t *bytes = (uint8_t *)buffer;
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || offset > LONG_MAX || xx_io_seek(device, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        size_t request = size - done;
        ssize_t got;
        if (request > transfer_capacity) request = transfer_capacity;
        got = xx_io_read(device, bytes + done, request);
        if (got <= 0) {
            return false;
        }
        done += (size_t)got;
    }
    return true;
}

static void xx_gz_member_cleanup(xx_gz_member *member)
{
    if (!member) return;
    if (member->name) xx_mem_free(member->name);
    if (member->comment) xx_mem_free(member->comment);
    xx_mem_zero(member, sizeof(*member));
}

static void xx_gz_private_free(xx_gz_private *priv)
{
    if (!priv) return;
    if (priv->members) {
        for (size_t i = 0; i < priv->count; ++i) {
            xx_gz_member_cleanup(&priv->members[i]);
        }
        xx_mem_free(priv->members);
    }
    xx_mem_free(priv);
}

static bool xx_gz_private_append(xx_gz_private *priv, xx_gz_member *member)
{
    xx_gz_member *items;
    size_t count;
    if (!priv || !member || priv->count == SIZE_MAX || priv->count + 1U > SIZE_MAX / sizeof(*items)) {
        return false;
    }
    count = priv->count + 1U;
    items = (xx_gz_member *)xx_mem_realloc(priv->members, count * sizeof(*items));
    if (!items) return false;
    priv->members = items;
    priv->members[priv->count] = *member;
    priv->count = count;
    xx_mem_zero(member, sizeof(*member));
    return true;
}

static ssize_t xx_gz_sink_write(xx_io_device *device, const void *data, size_t size)
{
    xx_gz_sink *sink = device ? (xx_gz_sink *)device->priv : NULL;
    if (!sink || (!data && size != 0U) || size > UINT64_MAX - sink->written) {
        if (sink) sink->failed = true;
        return -1;
    }
    if (sink->target && size != 0U && xx_io_write(sink->target, data, size) != (ssize_t)size) {
        sink->failed = true;
        return -1;
    }
    sink->crc32 = xx_crc32_calc(sink->crc32, data, size);
    sink->written += (uint64_t)size;
    return (ssize_t)size;
}

static int64_t xx_gz_sink_size(xx_io_device *device)
{
    const xx_gz_sink *sink = device ? (const xx_gz_sink *)device->priv : NULL;
    return sink && sink->written <= (uint64_t)INT64_MAX ? (int64_t)sink->written : -1;
}

static void xx_gz_sink_init(xx_gz_sink *sink, xx_io_device *target)
{
    xx_mem_zero(sink, sizeof(*sink));
    sink->target = target;
    sink->device.write = xx_gz_sink_write;
    sink->device.total_size = xx_gz_sink_size;
    sink->device.get_total_size = xx_gz_sink_size;
    sink->device.size = xx_gz_sink_size;
    sink->device.priv = sink;
}

/* Inflate through the library's RFC 1951 engine while retaining the precise
 * number of consumed bytes. The public deflate API intentionally hides its
 * buffered reader, but gzip needs that boundary to locate each trailer.
 * ran_out (optional): a failure because the input ended inside the data,
 * after the target was given all it decoded; a damaged stream leaves it
 * clear. */
static bool xx_gz_inflate_member(xx_io_device *source, int64_t offset, int64_t available, xx_io_device *target, int64_t *consumed, uint64_t *written, uint32_t *crc32,
                                 bool *ran_out, xx_pd_struct *pd)
{
    xx_bit_reader reader;
    xx_gz_sink sink;
    uint64_t loaded;
    uint64_t unread;
    bool result;
    if (ran_out) *ran_out = false;
    if (!source || offset < 0 || offset > LONG_MAX || available <= 0 || !consumed || !written || !crc32 || xx_io_seek(source, (long)offset, SEEK_SET) != 0 ||
        !xx_br_init(&reader, source, NULL, 0U, available)) {
        return false;
    }
    xx_gz_sink_init(&sink, target);
    result = xx_deflate_decompress_stream(&reader, &sink.device, NULL, 0U, NULL, false, pd);
    if (!result && ran_out) *ran_out = reader.eof && !sink.failed && !(pd && xx_pd_is_stopped(pd));
    if (reader.remaining_input < 0 || reader.remaining_input > available || reader.buffer_pos > reader.buffer_len || reader.bit_count < 0) {
        result = false;
        loaded = 0U;
        unread = 0U;
    } else {
        loaded = (uint64_t)(available - reader.remaining_input);
        unread = (uint64_t)(reader.buffer_len - reader.buffer_pos) + (uint64_t)(reader.bit_count / 8);
    }
    if (!result || sink.failed || unread > loaded || loaded - unread == 0U || loaded - unread > (uint64_t)INT64_MAX) {
        result = false;
    } else {
        *consumed = (int64_t)(loaded - unread);
        *written = sink.written;
        *crc32 = sink.crc32;
    }
    xx_br_free(&reader);
    return result;
}

static bool xx_gz_crc_region_buffered(xx_io_device *device, int64_t offset, size_t size, uint32_t *crc, uint8_t *buffer, size_t buffer_capacity)
{
    size_t done = 0U;
    if (!device || !crc) return false;
    while (done < size) {
        size_t chunk = size - done;
        if (chunk > buffer_capacity) chunk = buffer_capacity;
        if (offset > INT64_MAX - (int64_t)done || !xx_gz_read_exact_at(device, offset + (int64_t)done, buffer, chunk)) {
            return false;
        }
        *crc = xx_crc32_calc(*crc, buffer, chunk);
        done += chunk;
    }
    return true;
}

static bool xx_gz_crc_region(xx_io_device *device, int64_t offset, size_t size, uint32_t *crc)
{
    size_t buffer_capacity = xx_get_file_buffer_size();
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(buffer_capacity);
    bool buffer_result;
    if (!buffer) return false;
    buffer_result = xx_gz_crc_region_buffered(device, offset, size, crc, buffer, buffer_capacity);
    xx_mem_free(buffer);
    return buffer_result;
}

static bool xx_gz_read_optional_string(xx_io_device *device, int64_t total_size, int64_t *cursor, uint32_t *header_crc, char **text, int64_t *field_size)
{
    char *buffer;
    size_t length = 0U;
    size_t capacity = 64U;
    if (!device || !cursor || !header_crc || !text || !field_size || *cursor < 0 || *cursor >= total_size) {
        return false;
    }
    buffer = (char *)xx_mem_alloc(capacity);
    if (!buffer) return false;
    for (;;) {
        uint8_t byte;
        if (*cursor >= total_size || length >= XX_GZ_MAX_OPTIONAL_STRING || !xx_gz_read_exact_at(device, *cursor, &byte, 1U)) {
            xx_mem_free(buffer);
            return false;
        }
        ++(*cursor);
        *header_crc = xx_crc32_calc(*header_crc, &byte, 1U);
        if (byte == 0U) break;
        if (length + 1U >= capacity) {
            char *grown;
            size_t next = capacity * 2U;
            if (next > XX_GZ_MAX_OPTIONAL_STRING + 1U) {
                next = XX_GZ_MAX_OPTIONAL_STRING + 1U;
            }
            grown = (char *)xx_mem_realloc(buffer, next);
            if (!grown) {
                xx_mem_free(buffer);
                return false;
            }
            buffer = grown;
            capacity = next;
        }
        buffer[length++] = (char)byte;
    }
    buffer[length] = '\0';
    *text = buffer;
    *field_size = (int64_t)length + 1;
    return true;
}

static bool xx_gz_make_default_name(size_t index, char **name)
{
    char buffer[64];
    int length;
    if (!name) return false;
    length = xx_rt_snprintf(buffer, sizeof(buffer), "member-%llu", (unsigned long long)index + 1ULL);
    if (length <= 0 || (size_t)length >= sizeof(buffer)) return false;
    *name = xx_str_dup(buffer);
    return *name != NULL;
}

static bool xx_gz_parse_one(Abstractformat *self, int64_t total_size, int64_t offset, size_t index, xx_gz_member *member, bool header_only, xx_pd_struct *pd)
{
    uint8_t header[XX_GZ_HEADER_SIZE];
    uint8_t trailer[XX_GZ_TRAILER_SIZE];
    int64_t cursor;
    uint32_t header_crc;
    uint64_t actual_size;
    uint32_t actual_crc;
    int64_t compressed_size;
    if (!member) {
        return false;
    }
    /* The caller always cleans this object on failure.  Initialise it before
     * validating the fixed header so malformed input can never make cleanup
     * inspect indeterminate pointers. */
    xx_mem_zero(member, sizeof(*member));
    if (!self || offset < 0 || total_size < offset || total_size - offset < (int64_t)(XX_GZ_HEADER_SIZE + XX_GZ_TRAILER_SIZE) ||
        !xx_gz_read_exact_at(self->device, offset, header, sizeof(header)) || header[0] != XX_GZ_ID1 || header[1] != XX_GZ_ID2 || header[2] != XX_GZ_CM_DEFLATE ||
        (header[3] & XX_GZ_FLAG_RESERVED) != 0U) {
        return false;
    }
    member->extra_offset = -1;
    member->name_offset = -1;
    member->comment_offset = -1;
    member->header_crc_offset = -1;
    member->header_offset = offset;
    member->flags = header[3];
    member->modification_time = xx_data_get_u32(header, sizeof(header), 4U, XX_LITTLE_ENDIAN);
    member->extra_flags = header[8];
    member->operating_system = header[9];
    header_crc = xx_crc32_calc(0U, header, sizeof(header));
    cursor = offset + XX_GZ_HEADER_SIZE;

    if ((member->flags & XX_GZ_FLAG_FEXTRA) != 0U) {
        uint8_t size_bytes[2];
        uint16_t extra_length;
        if (total_size - cursor < 2 || !xx_gz_read_exact_at(self->device, cursor, size_bytes, 2U)) {
            goto fail;
        }
        extra_length = xx_data_get_u16(size_bytes, 2U, 0U, XX_LITTLE_ENDIAN);
        member->extra_offset = cursor;
        member->extra_size = (int64_t)extra_length + 2;
        if (total_size - cursor < member->extra_size) goto fail;
        header_crc = xx_crc32_calc(header_crc, size_bytes, 2U);
        cursor += 2;
        if (!xx_gz_crc_region(self->device, cursor, extra_length, &header_crc)) {
            goto fail;
        }
        cursor += extra_length;
    }
    if ((member->flags & XX_GZ_FLAG_FNAME) != 0U) {
        member->name_offset = cursor;
        if (!xx_gz_read_optional_string(self->device, total_size, &cursor, &header_crc, &member->name, &member->name_size)) {
            goto fail;
        }
    } else if (!xx_gz_make_default_name(index, &member->name)) {
        goto fail;
    }
    if ((member->flags & XX_GZ_FLAG_FCOMMENT) != 0U) {
        member->comment_offset = cursor;
        if (!xx_gz_read_optional_string(self->device, total_size, &cursor, &header_crc, &member->comment, &member->comment_size)) {
            goto fail;
        }
    }
    if ((member->flags & XX_GZ_FLAG_FHCRC) != 0U) {
        uint8_t value[2];
        member->header_crc_offset = cursor;
        if (total_size - cursor < 2 || !xx_gz_read_exact_at(self->device, cursor, value, sizeof(value))) {
            goto fail;
        }
        member->header_crc16 = xx_data_get_u16(value, sizeof(value), 0U, XX_LITTLE_ENDIAN);
        if (member->header_crc16 != (uint16_t)header_crc) goto fail;
        cursor += 2;
    }
    if (!member->name || member->name[0] == '\0') {
        if (member->name) xx_mem_free(member->name);
        member->name = NULL;
        if (!xx_gz_make_default_name(index, &member->name)) goto fail;
    }
    member->data_offset = cursor;
    member->header_size = cursor - offset;
    if (header_only) return true;
    if (total_size - cursor <= (int64_t)XX_GZ_TRAILER_SIZE ||
        !xx_gz_inflate_member(self->device, cursor, total_size - cursor, NULL, &compressed_size, &actual_size, &actual_crc, NULL, pd) ||
        compressed_size > total_size - cursor - (int64_t)XX_GZ_TRAILER_SIZE) {
        goto fail;
    }
    member->compressed_size = compressed_size;
    member->trailer_offset = cursor + compressed_size;
    if (!xx_gz_read_exact_at(self->device, member->trailer_offset, trailer, sizeof(trailer))) {
        goto fail;
    }
    member->crc32 = xx_data_get_u32(trailer, sizeof(trailer), 0U, XX_LITTLE_ENDIAN);
    {
        uint32_t stored_size = xx_data_get_u32(trailer, sizeof(trailer), 4U, XX_LITTLE_ENDIAN);
        if (member->crc32 != actual_crc || stored_size != (uint32_t)actual_size) {
            goto fail;
        }
    }
    /* ISIZE is modulo 2^32.  Parsing already counted the actual output, so
     * retain that full width for record metadata and later verification. */
    member->uncompressed_size = actual_size;
    member->member_end = member->trailer_offset + XX_GZ_TRAILER_SIZE;
    return true;

fail:
    xx_gz_member_cleanup(member);
    return false;
}

static bool xx_gz_has_magic_at(xx_io_device *device, int64_t total_size, int64_t offset)
{
    uint8_t magic[2];
    return device && offset >= 0 && total_size - offset >= 2 && xx_gz_read_exact_at(device, offset, magic, sizeof(magic)) && magic[0] == XX_GZ_ID1 &&
           magic[1] == XX_GZ_ID2;
}

/* Python's gzip reader accepts zero-filled gaps between members.  Read them in
 * chunks rather than issuing one seek/read per byte: real streams may align
 * members to sector or tape-record boundaries.  The caller accepts padding
 * only when another member follows or it runs through the end of the file. */
static bool xx_gz_skip_zero_padding(xx_io_device *device, int64_t total_size, int64_t offset, int64_t *after)
{
    uint8_t buffer[4096];
    int64_t cursor = offset;
    if (!device || !after || offset < 0 || offset > total_size) return false;
    while (cursor < total_size) {
        int64_t remaining = total_size - cursor;
        size_t count = remaining < (int64_t)sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
        size_t index = 0U;
        if (!xx_gz_read_exact_at(device, cursor, buffer, count)) return false;
        while (index < count && buffer[index] == 0U) ++index;
        cursor += (int64_t)index;
        if (index < count) break;
    }
    *after = cursor;
    return true;
}

static xx_gz_private *xx_gz_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_gz_private *priv;
    int64_t total_size;
    int64_t offset;
    if (!self || !self->device || self->base_address < 0) return NULL;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address || total_size - self->base_address < (int64_t)(XX_GZ_HEADER_SIZE + XX_GZ_TRAILER_SIZE)) {
        return NULL;
    }
    priv = (xx_gz_private *)xx_mem_calloc(1U, sizeof(*priv));
    if (!priv) return NULL;
    offset = self->base_address;
    for (;;) {
        xx_gz_member member;
        int64_t after_padding;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_gz_parse_one(self, total_size, offset, priv->count, &member, false, pd) || !xx_gz_private_append(priv, &member)) {
            xx_gz_member_cleanup(&member);
            goto fail;
        }
        offset = priv->members[priv->count - 1U].member_end;
        if (!xx_gz_skip_zero_padding(self->device, total_size, offset, &after_padding)) goto fail;
        if (xx_gz_has_magic_at(self->device, total_size, after_padding)) {
            offset = after_padding;
            continue;
        }
        if (after_padding == total_size) offset = after_padding;
        break;
    }
    priv->stream_end = offset;
    return priv;

fail:
    xx_gz_private_free(priv);
    return NULL;
}

static bool xx_gz_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    if (!destination || !source) return source == NULL;
    for (size_t i = 0; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_gz_find_option(const xx_list_s *options, uint32_t meta_id)
{
    if (!options) return NULL;
    for (size_t i = 0; i < options->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

/* ':' names a drive or a file's stream on Windows; elsewhere it is an
 * ordinary character of a member name ("9:00-notes.txt", "a:b"). */
#ifdef _WIN32
#define XX_GZ_COLON_RESERVED true
#else
#define XX_GZ_COLON_RESERVED false
#endif

static bool xx_gz_safe_name(const char *name)
{
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') {
        return false;
    }
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if ((ch == ':' && XX_GZ_COLON_RESERVED) || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') || (length == 2U && component[0] == '.' && component[1] == '.')) {
                return false;
            }
            /* Windows normalises trailing dots/spaces and treats these base
             * names as devices even when an extension is present.  Rejecting
             * them on every platform keeps archive extraction deterministic
             * and prevents writes to CON, NUL, COM1, or LPT1 device paths. */
            if (component[length - 1U] == '.' || component[length - 1U] == ' ') {
                return false;
            }
            {
                size_t base_length = 0U;
                char folded[7];
                while (base_length < length && component[base_length] != '.') {
                    ++base_length;
                }
                if (base_length < sizeof(folded)) {
                    size_t i;
                    for (i = 0U; i < base_length; ++i) {
                        char value = component[i];
                        folded[i] = value >= 'a' && value <= 'z' ? (char)(value - ('a' - 'A')) : value;
                    }
                    folded[base_length] = '\0';
                    if ((base_length == 3U && (xx_rt_memcmp(folded, "CON", 3U) == 0 || xx_rt_memcmp(folded, "PRN", 3U) == 0 || xx_rt_memcmp(folded, "AUX", 3U) == 0 ||
                                               xx_rt_memcmp(folded, "NUL", 3U) == 0)) ||
                        (base_length == 4U && ((xx_rt_memcmp(folded, "COM", 3U) == 0 || xx_rt_memcmp(folded, "LPT", 3U) == 0) && folded[3] >= '1' && folded[3] <= '9')) ||
                        (base_length == 6U && xx_rt_memcmp(folded, "CLOCK$", 6U) == 0)) {
                        return false;
                    }
                }
            }
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

static bool xx_gz_populate_archive_record(xx_archive_record *record, const xx_gz_member *member)
{
    bool result;
    if (!record || !member) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    result = xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME, member->name) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, member->uncompressed_size) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->compressed_size) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32, member->crc32) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, XX_GZ_CM_DEFLATE) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, member->modification_time) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS, member->flags) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
             xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
    if (result && member->comment) {
        result = xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT, member->comment);
    }
    return result;
}

void xx_gz_init(xx_gz *gz, xx_io_device *dev, int64_t base_address)
{
    if (!gz) return;
    xx_mem_zero(gz, sizeof(*gz));
    xx_format_init(&gz->format, dev, base_address);
    gz->format.endian = XX_ENDIAN_LITTLE;
    gz->format.file_type = XX_FILE_TYPE_GZ;
    gz->format.handle_type = XX_FILE_TYPE_GZ;
    gz->format.format_type = XX_TYPE_ARCHIVE;
    gz->format.is_archive = true;
    xx_format_set_mime_type(&gz->format, "application/gzip");
    xx_format_set_extension(&gz->format, "gz");
    gz->format.check_is_valid = xx_gz_check_is_valid;
    gz->format.handle_base_info = xx_gz_handle_base_info;
    gz->format.get_format_size = xx_gz_get_format_size;
    gz->format.get_number_of_archive_records = xx_gz_get_number_of_archive_records;
    gz->format.create_archive_records_reading = xx_gz_create_archive_records_reading;
    gz->format.get_current_archive_record = xx_gz_get_current_archive_record;
    gz->format.unpack_current_archive_record = xx_gz_unpack_current_archive_record;
    gz->format.archive_record_move_to_next = xx_gz_archive_record_move_to_next;
    gz->format.free_archive_records_reading = xx_gz_free_archive_records_reading;
#if !defined(XXFC_ARCHIVE_WRITE_TAR_ONLY) || !XXFC_ARCHIVE_WRITE_TAR_ONLY
    gz->format.create_archive_records_writing = xx_gz_create_archive_records_writing;
    gz->format.pack_archive_record = xx_gz_pack_archive_record;
    gz->format.finalize_archive_records_writing = xx_gz_finalize_archive_records_writing;
    gz->format.free_archive_records_writing = xx_gz_free_archive_records_writing;
#endif
    gz->format.destroy = xx_gz_vtable_destroy;
    gz->stream_end = -1;
}

xx_gz *xx_gz_create(xx_io_device *dev, int64_t base_address)
{
    xx_gz *gz = (xx_gz *)xx_mem_alloc(sizeof(*gz));
    if (gz) xx_gz_init(gz, dev, base_address);
    return gz;
}

void xx_gz_destroy(xx_gz *gz)
{
    if (!gz) return;
    if (gz->internal) {
        xx_gz_private_free((xx_gz_private *)gz->internal);
        gz->internal = NULL;
    }
    if (gz->format.close) gz->format.close(&gz->format);
    xx_format_cleanup_extra_parameters(&gz->format);
}

static void xx_gz_vtable_destroy(Abstractformat *self)
{
    xx_gz_destroy((xx_gz *)self);
}

void xx_gz_free(xx_gz *gz)
{
    if (!gz) return;
    xx_gz_destroy(gz);
    xx_mem_free(gz);
}

bool xx_gz_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    uint8_t header[XX_GZ_HEADER_SIZE];
    int64_t total_size;
    (void)pd;
    if (!self || !self->device || self->base_address < 0) return false;
    total_size = xx_io_total_size(self->device);
    return total_size >= self->base_address && total_size - self->base_address >= (int64_t)(XX_GZ_HEADER_SIZE + XX_GZ_TRAILER_SIZE) &&
           xx_gz_read_exact_at(self->device, self->base_address, header, sizeof(header)) && header[0] == XX_GZ_ID1 && header[1] == XX_GZ_ID2 &&
           header[2] == XX_GZ_CM_DEFLATE && (header[3] & XX_GZ_FLAG_RESERVED) == 0U;
}

bool xx_gz_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_gz *gz;
    xx_gz_private *priv;
    int64_t total_size;
    if (!self || !self->device || !(priv = xx_gz_parse(self, pd))) {
        if (self) {
            gz = (xx_gz *)self;
            if (gz->internal) {
                xx_gz_private_free((xx_gz_private *)gz->internal);
                gz->internal = NULL;
            }
            gz->number_of_members = 0U;
            gz->stream_end = -1;
            self->format_size = -1;
            self->overlay_offset = -1;
            self->overlay_size = 0;
            self->number_of_archive_records = 0U;
            self->is_valid = false;
            self->base_info_handled = false;
        }
        return false;
    }
    gz = (xx_gz *)self;
    if (gz->internal) xx_gz_private_free((xx_gz_private *)gz->internal);
    gz->internal = priv;
    gz->number_of_members = (uint64_t)priv->count;
    gz->stream_end = priv->stream_end;
    self->format_size = priv->stream_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > priv->stream_end) {
        self->overlay_offset = priv->stream_end;
        self->overlay_size = total_size - priv->stream_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = (uint64_t)priv->count;
    self->file_type = XX_FILE_TYPE_GZ;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_gz_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_gz_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_gz *)self)->number_of_members;
}

xx_archive_record_state *xx_gz_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    xx_gz_archive_stream *stream;
    xx_gz_private *priv;
    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) || !self->is_valid ||
        !(priv = (xx_gz_private *)((xx_gz *)self)->internal)) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_gz_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_gz_copy_options(&state->options, options)) {
        xx_mem_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->internal_state = stream;
    state->free_internal = xx_mem_free;
    state->total_records = (int64_t)priv->count;
    if (priv->count == 0U || !xx_gz_populate_archive_record(&state->current_record, &priv->members[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_gz_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_gz_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_gz_private *priv;
    xx_gz_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd)) ||
        !(priv = (xx_gz_private *)((xx_gz *)self)->internal)) {
        return false;
    }
    stream = (xx_gz_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= priv->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_gz_populate_archive_record(&state->current_record, &priv->members[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)stream->index;
    return true;
}

static bool xx_gz_extract_member(Abstractformat *self, const xx_gz_member *member, xx_io_device *target, xx_pd_struct *pd)
{
    int64_t consumed;
    uint64_t written;
    uint32_t crc32;
    return self && member && xx_gz_inflate_member(self->device, member->data_offset, member->compressed_size, target, &consumed, &written, &crc32, NULL, pd) &&
           consumed == member->compressed_size && written == member->uncompressed_size && crc32 == member->crc32;
}

bool xx_gz_unpack_to_device(xx_gz *gz, xx_io_device *destination, xx_pd_struct *pd)
{
    xx_gz_private *priv;
    if (!gz || !destination || (!gz->format.base_info_handled && !xx_format_handle_base_info(&gz->format, pd)) || !gz->format.is_valid ||
        !(priv = (xx_gz_private *)gz->internal)) {
        return false;
    }
    for (size_t i = 0U; i < priv->count; ++i) {
        if (!xx_gz_extract_member(&gz->format, &priv->members[i], destination, pd)) {
            return false;
        }
    }
    return true;
}

typedef struct xx_gz_prefix_sink_s {
    uint8_t *data;
    size_t capacity;
    size_t size;
    bool full;
} xx_gz_prefix_sink;

/* Keeps what fits and refuses the rest, which stops the decoder. */
static ssize_t xx_gz_prefix_write(xx_io_device *device, const void *data, size_t size)
{
    xx_gz_prefix_sink *sink = device ? (xx_gz_prefix_sink *)device->priv : NULL;
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

bool xx_gz_decode_prefix(xx_io_device *device, int64_t base_address, void *buffer, size_t capacity, size_t *size, bool *complete, xx_pd_struct *pd)
{
    xx_gz gz;
    xx_gz_prefix_sink prefix;
    xx_io_device sink;
    int64_t total_size;
    int64_t offset;
    size_t index = 0U;
    bool result = false;
    if (size) *size = 0U;
    if (complete) *complete = false;
    if (!device || !buffer || capacity == 0U || !size || base_address < 0) return false;
    total_size = xx_io_total_size(device);
    if (total_size < base_address || total_size - base_address < (int64_t)(XX_GZ_HEADER_SIZE + XX_GZ_TRAILER_SIZE)) return false;
    xx_mem_zero(&prefix, sizeof(prefix));
    xx_mem_zero(&sink, sizeof(sink));
    prefix.data = (uint8_t *)buffer;
    prefix.capacity = capacity;
    sink.write = xx_gz_prefix_write;
    sink.priv = &prefix;
    xx_gz_init(&gz, device, base_address);
    offset = base_address;
    /* The member walk of xx_gz_parse, decoding into the prefix. */
    for (;;) {
        xx_gz_member member;
        uint8_t trailer[XX_GZ_TRAILER_SIZE];
        int64_t consumed;
        int64_t after_padding;
        uint64_t written;
        uint32_t crc32;
        bool inflated;
        if (pd && xx_pd_is_stopped(pd)) break;
        if (!xx_gz_parse_one(&gz.format, total_size, offset, index++, &member, true, pd)) break;
        inflated = total_size - member.data_offset > (int64_t)XX_GZ_TRAILER_SIZE &&
                   xx_gz_inflate_member(device, member.data_offset, total_size - member.data_offset, &sink, &consumed, &written, &crc32, NULL, pd);
        offset = member.data_offset;
        xx_gz_member_cleanup(&member);
        if (!inflated) {
            result = prefix.full && !(pd && xx_pd_is_stopped(pd));
            break;
        }
        if (consumed > total_size - offset - (int64_t)XX_GZ_TRAILER_SIZE || !xx_gz_read_exact_at(device, offset + consumed, trailer, sizeof(trailer)) ||
            xx_data_get_u32(trailer, sizeof(trailer), 0U, XX_LITTLE_ENDIAN) != crc32 ||
            xx_data_get_u32(trailer, sizeof(trailer), 4U, XX_LITTLE_ENDIAN) != (uint32_t)written) {
            break;
        }
        offset += consumed + (int64_t)XX_GZ_TRAILER_SIZE;
        if (!xx_gz_skip_zero_padding(device, total_size, offset, &after_padding)) break;
        if (!xx_gz_has_magic_at(device, total_size, after_padding)) {
            if (complete) *complete = true;
            result = true;
            break;
        }
        offset = after_padding;
    }
    xx_gz_destroy(&gz);
    *size = prefix.size;
    return result;
}

/* Whether the input ends inside the header of the member at offset, whose
 * bytes so far are a valid start of one: a cut file, not a damaged one. */
static bool xx_gz_header_cut(xx_io_device *device, int64_t total_size, int64_t offset)
{
    uint8_t header[XX_GZ_HEADER_SIZE];
    size_t available;
    int64_t cursor;
    if (!device || offset < 0 || offset > total_size) return false;
    available = total_size - offset < (int64_t)sizeof(header) ? (size_t)(total_size - offset) : sizeof(header);
    if (available != 0U && !xx_gz_read_exact_at(device, offset, header, available)) return false;
    if ((available > 0U && header[0] != XX_GZ_ID1) || (available > 1U && header[1] != XX_GZ_ID2) || (available > 2U && header[2] != XX_GZ_CM_DEFLATE) ||
        (available > 3U && (header[3] & XX_GZ_FLAG_RESERVED) != 0U)) {
        return false;
    }
    if (available < sizeof(header)) return true;
    cursor = offset + XX_GZ_HEADER_SIZE;
    if ((header[3] & XX_GZ_FLAG_FEXTRA) != 0U) {
        uint8_t size_bytes[2];
        if (total_size - cursor < 2) return true;
        if (!xx_gz_read_exact_at(device, cursor, size_bytes, sizeof(size_bytes))) return false;
        cursor += 2 + (int64_t)xx_data_get_u16(size_bytes, sizeof(size_bytes), 0U, XX_LITTLE_ENDIAN);
        if (cursor >= total_size) return true;
    }
    for (uint8_t flag = XX_GZ_FLAG_FNAME; flag != 0U; flag = flag == XX_GZ_FLAG_FNAME ? XX_GZ_FLAG_FCOMMENT : 0U) {
        int64_t length = 0;
        uint8_t byte = 1U;
        if ((header[3] & flag) == 0U) continue;
        while (byte != 0U) {
            if (cursor >= total_size) return true;
            if (length++ > (int64_t)XX_GZ_MAX_OPTIONAL_STRING || !xx_gz_read_exact_at(device, cursor++, &byte, 1U)) return false;
        }
    }
    if ((header[3] & XX_GZ_FLAG_FHCRC) != 0U) cursor += 2;
    /* A whole header: then only a member shorter than the smallest one
     * (xx_gz_parse_one) is cut; any other refusal was of its contents. */
    return cursor >= total_size || total_size - offset < (int64_t)(XX_GZ_HEADER_SIZE + XX_GZ_TRAILER_SIZE);
}

bool xx_gz_decode_stream(xx_io_device *device, int64_t base_address, xx_io_device *destination, int64_t *stream_size, bool *cut, uint64_t *checked, xx_pd_struct *pd)
{
    xx_gz gz;
    int64_t total_size;
    int64_t offset;
    size_t index = 0U;
    uint64_t decoded = 0U;
    bool result = false;
    if (stream_size) *stream_size = -1;
    if (cut) *cut = false;
    if (checked) *checked = 0U;
    if (!device || !destination || !stream_size || base_address < 0) return false;
    total_size = xx_io_total_size(device);
    if (total_size < base_address || total_size - base_address < (int64_t)(XX_GZ_HEADER_SIZE + XX_GZ_TRAILER_SIZE)) return false;
    xx_gz_init(&gz, device, base_address);
    offset = base_address;
    /* The member walk of xx_gz_parse, decoding each member once, into the
     * destination, and checking it against its trailer. */
    for (;;) {
        xx_gz_member member;
        uint8_t trailer[XX_GZ_TRAILER_SIZE];
        uint8_t expected[4];
        int64_t consumed = 0;
        int64_t after_padding;
        int64_t trailer_offset;
        uint64_t written = 0U;
        uint32_t crc32 = 0U;
        bool ran_out = false;
        if (pd && xx_pd_is_stopped(pd)) break;
        if (!xx_gz_parse_one(&gz.format, total_size, offset, index, &member, true, pd)) {
            /* A member after a whole one that the end of the file cuts. */
            if (cut && index != 0U) *cut = xx_gz_header_cut(device, total_size, offset);
            xx_gz_member_cleanup(&member);
            break;
        }
        ++index;
        offset = member.data_offset;
        xx_gz_member_cleanup(&member);
        if (total_size - offset <= 0 || !xx_gz_inflate_member(device, offset, total_size - offset, destination, &consumed, &written, &crc32, &ran_out, pd)) {
            if (cut) *cut = total_size - offset <= 0 || ran_out;
            break;
        }
        trailer_offset = offset + consumed;
        xx_data_set_u32(expected, 4, 0, crc32, false);
        if (total_size - trailer_offset < (int64_t)XX_GZ_TRAILER_SIZE) {
            /* The data is whole and the trailer cut: what is left of its
             * CRC-32 still checks the data. */
            size_t left = (size_t)(total_size - trailer_offset);
            if (cut) {
                *cut = left == 0U || (xx_gz_read_exact_at(device, trailer_offset, trailer, left) &&
                                      xx_rt_memcmp(trailer, expected, left < sizeof(expected) ? left : sizeof(expected)) == 0);
                /* All of its CRC-32 is there. */
                if (*cut && left >= sizeof(expected) && checked) *checked = decoded + written;
            }
            break;
        }
        if (!xx_gz_read_exact_at(device, trailer_offset, trailer, sizeof(trailer)) || xx_rt_memcmp(trailer, expected, sizeof(expected)) != 0 ||
            xx_data_get_u32(trailer, sizeof(trailer), 4U, XX_LITTLE_ENDIAN) != (uint32_t)written) {
            break;
        }
        decoded += written;
        if (checked) *checked = decoded;
        offset = trailer_offset + (int64_t)XX_GZ_TRAILER_SIZE;
        if (!xx_gz_skip_zero_padding(device, total_size, offset, &after_padding)) break;
        if (xx_gz_has_magic_at(device, total_size, after_padding)) {
            offset = after_padding;
            continue;
        }
        if (after_padding == total_size) offset = after_padding;
        *stream_size = offset - base_address;
        result = true;
        break;
    }
    xx_gz_destroy(&gz);
    return result;
}

bool xx_gz_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_gz_private *priv;
    xx_gz_archive_stream *stream;
    const xx_gz_member *member;
    const xx_var *path_value;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    xx_io_device *output = NULL;
    bool result = false;
    bool created = false;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd)) ||
        !(priv = (xx_gz_private *)((xx_gz *)self)->internal)) {
        return false;
    }
    stream = (xx_gz_archive_stream *)state->internal_state;
    if (stream->index >= priv->count) return false;
    member = &priv->members[stream->index];
    if (!xx_gz_safe_name(member->name)) return false;
    path_value = xx_gz_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) return xx_gz_extract_member(self, member, NULL, pd);
    if (path_value->type == XX_VAR_TYPE_STRING || path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING || path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] != '\0' && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", member->name);
    } else {
        destination = xx_str_concat(base, member->name);
    }
    if (!destination || !xx_io_create_dirs_a(destination, false)) goto cleanup;
    output = xx_io_file_open(destination, "wb");
    created = output != NULL;
    if (!output) goto cleanup;
    result = xx_gz_extract_member(self, member, output, pd);
    {
        /* A member that inflated part of the way: under XX_META_ID_OPT_FORCE
         * what the decoder wrote stays (xx_archive_salvage.h). */
        int64_t kept = result ? 0 : xx_io_tell(output);
        if (xx_io_close(output) != 0) {
            result = false;
            kept = 0;
        }
        output = NULL;
        if (!result && created &&
            !(kept > 0 && xx_archive_salvage_wanted(&state->options, pd) &&
              xx_archive_salvage_keep((uint64_t)kept, member->uncompressed_size <= (uint64_t)INT64_MAX ? (int64_t)member->uncompressed_size : -1)))
            xx_rt_remove(destination);
    }

cleanup:
    if (output) xx_io_close(output);
    if (destination) xx_str_free(destination);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_gz_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_gz_get_number_of_members(const xx_gz *gz)
{
    return gz ? gz->number_of_members : 0U;
}

int64_t xx_gz_get_stream_end(const xx_gz *gz)
{
    return gz ? gz->stream_end : -1;
}

xx_archive_write_state *xx_gz_create_archive_records_writing(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    return ss_create(self, options, pd);
}
bool xx_gz_pack_archive_record(Abstractformat *self, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source, xx_pd_struct *pd)
{
    return ss_pack(self, state, record, source, pd);
}
bool xx_gz_finalize_archive_records_writing(Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd)
{
    return ss_finalize(self, state, pd);
}
void xx_gz_free_archive_records_writing(Abstractformat *self, xx_archive_write_state *state)
{
    ss_free(self, state);
}
