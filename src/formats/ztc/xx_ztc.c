/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZTC (Zortech C / Symantec C++ distribution) archives.
 *
 *   file header, 10 bytes:
 *     0x00  u32 LE 0x01abbbd6 magic
 *     0x04  4 bytes, unexamined
 *     0x08  u16 LE volume number
 *
 *   Records follow from 0x0a, chained by their own total size. There is no
 *   member count and no directory; the archive closes with a two byte FFFF
 *   trailer, which the walk recognises by the next record starting exactly
 *   two bytes before EOF.
 *
 *   record, 0x12 bytes:
 *     0x00  i32 LE uncompressed size
 *     0x04  i32 LE stamp; the FIRST record's copy must be 0x00003bc4
 *     0x08  i32 LE total size: this record, its name, its check word and
 *           its payload - i.e. the distance to the next record
 *     0x0c  u32 LE time, format unidentified
 *     0x10  u16 LE name size, in bytes, at least 1
 *     0x12  name, `name size` bytes, NUL terminated inside the field
 *     ....  u32 LE check word
 *     ....  payload, total size - name size - 0x16 bytes
 *
 * The check word is the format's per-record self-test, and it is a peculiar
 * one: it sums only the LOW BYTE of the uncompressed size, the LOW BYTE of
 * the time, and every byte of the name. Weak as a checksum, it is still a
 * 32 bit value that has to come out right for every record in the file,
 * which is what lets a walk with no member count trust its own cursor.
 *
 * The payload is LZHUF, but NOT a flat stream: it is paged, with a four byte
 * little endian plain sum of each page stored inline after it. The pages are
 * 0x1000 bytes except the last, which is short by exactly four because the
 * check bytes are subtracted from the remaining length before the page
 * length is chosen. xx_lzhuf_ztc_decode_memory() walks and verifies that
 * framing itself, so the member's payload is handed over exactly as stored -
 * feeding it to the plain LZHUF entry point decodes the first 4096 bytes
 * correctly and then garbage, which a small member hides completely.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ztc/xx_ztc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzhuf/xx_lzhuf.h"

#include <stdio.h>

#define XX_ZTC_COPY_CHUNK (64 * 1024)

typedef struct xx_ztc_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_ztc_member;

typedef struct xx_ztc_stream_s {
    xx_ztc_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_ztc_stream;

static void xx_ztc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ztc_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_ztc_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_ztc_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_ztc_stream_free(void *pointer) {
    xx_ztc_stream *stream = (xx_ztc_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_ztc_add(xx_ztc_stream *stream,
                          const xx_ztc_member *member) {
    xx_ztc_member *grown = (xx_ztc_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_ZTC_HEADER_SIZE 10
#define XX_ZTC_RECORD_SIZE 0x12
#define XX_ZTC_RECORD_OVERHEAD 0x16
#define XX_ZTC_MAGIC 0x01abbbd6UL
#define XX_ZTC_STAMP 0x00003bc4L
#define XX_ZTC_MAX_MEMBERS 100000
#define XX_ZTC_MAX_NAME_SIZE 4096
#define XX_ZTC_TRAILER_SIZE 2
#define XX_ZTC_METHOD_LZHUF 0U
#define XX_ZTC_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_ztc_le16(const uint8_t *data);
static uint32_t xx_ztc_le32(const uint8_t *data);
static xx_ztc_stream *xx_ztc_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_ztc_decode(Abstractformat *self, const xx_ztc_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The record plus its trailing four byte check word. */
/* A fixed value the first record always carries; later records reuse the
 * field for something else. */
/* The archive closes with a two byte FFFF trailer. */
/* No method field exists: every member is the paged LZHUF stream, and this
 * number is synthesised so the decode switch has something to key on. */

static uint16_t xx_ztc_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_ztc_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_ztc_stream *xx_ztc_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_ztc_stream *stream;
    uint8_t header[XX_ZTC_HEADER_SIZE];
    uint8_t *name_field = NULL;
    int64_t total;
    int64_t span;
    int64_t offset;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)(XX_ZTC_HEADER_SIZE + XX_ZTC_RECORD_SIZE)) return NULL;
    if (!xx_ztc_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_ztc_le32(header) != (uint32_t)XX_ZTC_MAGIC) return NULL;

    stream = (xx_ztc_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_ZTC_HEADER_SIZE;

    while ((offset + XX_ZTC_RECORD_SIZE) <= span) {
        xx_ztc_member member;
        uint8_t record[XX_ZTC_RECORD_SIZE];
        uint8_t check[4];
        char *name;
        int64_t uncompressed_size;
        int32_t stamp;
        int64_t total_size;
        int64_t name_size;
        int64_t data_offset;
        int64_t compressed_size;
        int64_t cursor;
        int64_t name_length;
        uint32_t wanted;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_ZTC_MAX_MEMBERS) goto fail;

        if (!xx_ztc_read_at(self, self->base_address + offset, record,
                            sizeof(record))) {
            goto fail;
        }

        /* Signed on purpose: a size field with the top bit set is corrupt,
         * not a two-gigabyte quantity. */
        uncompressed_size = (int64_t)(int32_t)xx_ztc_le32(record);
        stamp = (int32_t)xx_ztc_le32(record + 4);
        total_size = (int64_t)(int32_t)xx_ztc_le32(record + 8);
        name_size = (int64_t)xx_ztc_le16(record + 0x10);

        if (uncompressed_size < 0) goto fail;
        /* Every member is named; a zero length name means the cursor is not
         * on a record. */
        if (name_size == 0 || name_size > (int64_t)XX_ZTC_MAX_NAME_SIZE) {
            goto fail;
        }
        /* The total size is what advances the walk, so it has to cover at
         * least the fixed part, the name and the check word - otherwise the
         * loop could stall or step backwards. */
        if (total_size < (name_size + XX_ZTC_RECORD_OVERHEAD)) goto fail;
        /* Half of the whole-file gate. The magic is four bytes, which is
         * cheap to hit; this second fixed 32 bit value at a fixed offset in
         * the FIRST record is what makes a stray 0xd6bbab01 unlikely to
         * survive. Later records reuse the field, so it can only be demanded
         * once - which is exactly why a later reader must not "simplify"
         * this into checking every record or none. */
        if (stream->count == 0U && stamp != (int32_t)XX_ZTC_STAMP) goto fail;
        if (!xx_ztc_range_within(span, offset,
                                 name_size + XX_ZTC_RECORD_OVERHEAD)) {
            goto fail;
        }

        name_field = (uint8_t *)xx_mem_alloc((size_t)name_size);
        if (!name_field) goto fail;
        if (!xx_ztc_read_at(self, self->base_address + offset +
                                      XX_ZTC_RECORD_SIZE,
                            name_field, (size_t)name_size)) {
            goto fail;
        }
        if (!xx_ztc_read_at(self, self->base_address + offset +
                                      XX_ZTC_RECORD_SIZE + name_size,
                            check, sizeof(check))) {
            goto fail;
        }

        /* The other half of the gate, and the per-record one: the check word
         * sums the LOW BYTE of the uncompressed size, the LOW BYTE of the
         * time, and every byte of the name. It is a weak checksum, but it is
         * 32 bits that must come out right in EVERY record, and this walk has
         * no member count to fall back on - without it a misread total size
         * would quietly resynchronise on payload bytes and publish members
         * made of nothing. */
        wanted = (uint32_t)record[0] + (uint32_t)record[0x0c];
        for (cursor = 0; cursor < name_size; ++cursor) {
            wanted += (uint32_t)name_field[cursor];
        }
        if (xx_ztc_le32(check) != wanted) goto fail;

        data_offset = offset + XX_ZTC_RECORD_OVERHEAD + name_size;
        compressed_size = total_size - name_size - XX_ZTC_RECORD_OVERHEAD;
        if (!xx_ztc_range_within(span, data_offset, compressed_size)) {
            /* A final member whose declared payload runs past EOF is clamped
             * rather than rejected: these archives are volume sets and the
             * last member of a volume is legitimately cut short. The record
             * itself still had to fit, which is checked above. */
            if (data_offset > span) goto fail;
            compressed_size = span - data_offset;
        }

        /* NUL terminated inside the field, and NOT necessarily filling it. */
        name_length = 0;
        while (name_length < name_size && name_field[name_length] != 0U) {
            ++name_length;
        }
        if (name_length == 0) goto fail;
        name = (char *)xx_mem_alloc((size_t)name_length + 1U);
        if (!name) goto fail;
        for (cursor = 0; cursor < name_length; ++cursor) {
            uint8_t byte = name_field[cursor];
            /* ZTC names are DOS paths: printable ASCII only. */
            if (byte < 0x20U || byte > 0x7EU) {
                xx_str_free(name);
                goto fail;
            }
            /* The DOS separator, converted the way every path-carrying
             * reader here converts it. */
            name[cursor] = (byte == (uint8_t)'\\') ? '/' : (char)byte;
        }
        name[name_length] = '\0';
        if (!xx_ztc_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }
        xx_mem_free(name_field);
        name_field = NULL;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_ZTC_RECORD_OVERHEAD + name_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = XX_ZTC_METHOD_LZHUF;
        /* The u32 at 0x0c is a time of some kind, but its epoch and layout
         * are unidentified, so no instant is published rather than a
         * fabricated one. Its low byte still feeds the check word above. */
        member.timestamp = 0U;
        member.is_folder = false;

        if (!xx_ztc_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        offset += total_size;
        /* The FFFF trailer: the next record would start exactly two bytes
         * before EOF, so the chain is complete. */
        if ((offset + XX_ZTC_TRAILER_SIZE) == span) break;
    }

    if (stream->count == 0U) goto fail;
    stream->archive_size = (offset < span) ? offset : span;
    return stream;

fail:
    xx_mem_free(name_field);
    xx_ztc_stream_free(stream);
    return NULL;
}


/* A member's uncompressed size is attacker-controlled; refuse rather than
 * attempt an allocation the container merely claims to need. */

static bool xx_ztc_decode(Abstractformat *self, const xx_ztc_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > (int64_t)XX_ZTC_MAX_DECODED ||
        member->compressed_size > (int64_t)XX_ZTC_MAX_DECODED) {
        return false;
    }
    if ((uint64_t)member->uncompressed_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    /* Every ZTC member is LZHUF; the container has no method field, so this
     * is the number parse synthesises. Anything else did not come from this
     * reader, and guessing "probably stored" would emit compressed bytes as
     * if they were data. */
    if (member->method != XX_ZTC_METHOD_LZHUF) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_ztc_read_at(self, member->data_offset, packed,
                        (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* The payload goes in exactly as stored, page sums and all: this entry
     * point owns the paging, verifying every page's inline sum - including
     * pages past the point the codec stops reading - before a single bit is
     * decoded. That verification is the only integrity check this format
     * offers on member DATA (the record check word covers the name and two
     * stray bytes, nothing else), so stripping the pages here and calling
     * the flat LZHUF entry point would both corrupt the stream after the
     * first page and throw away the only thing that would have noticed. */
    if (!xx_lzhuf_ztc_decode_memory(packed, (size_t)member->compressed_size,
                                    plain, (size_t)member->uncompressed_size,
                                    &written)) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    /* The codec has no end symbol: the requested length is the only stop
     * condition, so a short result means the stream disagreed with the
     * record and must not be reported as success. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_ztc_init(xx_ztc *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ZTC;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ztc");
    xx_format_set_extension(&archive->format, "ztc");
    archive->format.check_is_valid = xx_ztc_check_is_valid;
    archive->format.handle_base_info = xx_ztc_handle_base_info;
    archive->format.get_format_size = xx_ztc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ztc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ztc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ztc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ztc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ztc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ztc_free_archive_records_reading;
    archive->format.destroy = xx_ztc_vtable_destroy;
}

xx_ztc *xx_ztc_create(xx_io_device *device, int64_t base_address) {
    xx_ztc *archive = (xx_ztc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ztc_init(archive, device, base_address);
    return archive;
}

void xx_ztc_destroy(xx_ztc *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ztc_free(xx_ztc *archive) {
    if (!archive) return;
    xx_ztc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ztc_vtable_destroy(Abstractformat *self) {
    xx_ztc_destroy((xx_ztc *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ztc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ztc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ztc_parse(self, pd);
    if (!stream) return false;
    xx_ztc_stream_free(stream);
    return true;
}

bool xx_ztc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ztc *archive = (xx_ztc *)self;
    xx_ztc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ztc_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_ztc_stream_free(stream);
    return true;
}

int64_t xx_ztc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ztc_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ztc *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ztc_set_record(xx_archive_record *record,
                                 const xx_ztc_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_ztc_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_ztc_get_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_ztc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ztc_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ztc_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ztc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ztc_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ztc_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ztc_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ztc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ztc_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ztc_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ztc_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ztc_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ztc_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ztc_stream *stream;
    const xx_ztc_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ztc_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ztc_path_safe(member->name)) return false;

    path_option = xx_ztc_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ztc_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_ztc_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_ztc_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
