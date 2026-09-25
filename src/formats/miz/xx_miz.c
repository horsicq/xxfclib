/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MIZ containers.
 *
 *   header, 12 bytes, no slack between the fields:
 *     0x00  "DKCL", 4 bytes
 *     0x04  u16 LE version, only 1 exists
 *     0x06  "SBRW", 4 bytes
 *     0x0a  u16 LE name FIELD length, 2..64
 *
 *   0x0c  name field, `name field length` bytes: a NUL-terminated 8.3 name
 *         followed by NUL padding out to the field length. The stored value
 *         is a FIELD width, not a string length, so the terminator must be
 *         present and every byte past it must be NUL.
 *
 *   record, 12 bytes, at 0x0c + name field length:
 *     0x00  u16 LE DOS time
 *     0x02  u16 LE DOS date
 *     0x04  i32 LE uncompressed size
 *     0x08  i32 LE compressed size
 *
 *   data, `compressed size` bytes, immediately after the record: a raw
 *   PKWARE DCL implode stream whose first two bytes are the literal mode
 *   (0 or 1) and the dictionary exponent (4, 5 or 6).
 *
 *   footer, 4 bytes: "MJDK". The archive ends there; anything beyond is
 *   overlay.
 *
 * The container holds EXACTLY ONE member - there is no count field and no
 * second record - so the member list this reader publishes always has one
 * entry. No method number is stored either: the payload is always DCL
 * imploded, so the reader records method 10 (the ZIP numbering for PKWARE
 * DCL implode) to keep a listing honest about what the decode switch does.
 *
 * "DKCL" alone is four bytes and cheap to hit by accident. What actually
 * gates this format is the conjunction of the second magic "SBRW" at 0x06,
 * the version word between them, the shape of the name field, and the
 * trailing "MJDK" landing exactly where the two size fields put it.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/miz/xx_miz.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_MIZ_COPY_CHUNK (64 * 1024)

typedef struct xx_miz_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_miz_member;

typedef struct xx_miz_stream_s {
    xx_miz_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_miz_stream;

static void xx_miz_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_miz_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_miz_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_miz_path_safe(const char *name) {
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

static void xx_miz_stream_free(void *pointer) {
    xx_miz_stream *stream = (xx_miz_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_miz_add(xx_miz_stream *stream,
                          const xx_miz_member *member) {
    xx_miz_member *grown = (xx_miz_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_MIZ_MAGIC_SIZE 12
#define XX_MIZ_RECORD_SIZE 12
#define XX_MIZ_FOOTER_SIZE 4
#define XX_MIZ_VERSION 1U
#define XX_MIZ_MAX_NAME_FIELD 64
#define XX_MIZ_MAX_SIZE 0x40000000
#define XX_MIZ_MAX_MEMBERS 1
#define XX_MIZ_METHOD_DCL 10U
#define XX_MIZ_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_miz_le16(const uint8_t *data);
static uint32_t xx_miz_le32(const uint8_t *data);
static uint64_t xx_miz_dos_to_unix(uint16_t dos_date, uint16_t dos_time);
static xx_miz_stream *xx_miz_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_miz_decode(Abstractformat *self, const xx_miz_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The writer only ever emits 8.3 names, so "12345678.123" plus its NUL is
 * the real width. A generous ceiling is kept, but the field must still hold
 * a terminator - this is a FIELD length, not a string length. */
/* 1 GB sanity cap; the reference refuses either size field with its sign bit
 * set, and this is the same rule with a tighter bound. */
/* The container carries exactly one member. The cap exists so the shape of
 * this reader matches every other one, not because a count is read. */

static uint16_t xx_miz_le16(const uint8_t *data) {
    return (uint16_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8));
}

static uint32_t xx_miz_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* DOS date/time -> Unix seconds. Written out rather than taken from a helper
 * because there is no CRT here; an out-of-range field yields 0 (unknown)
 * instead of a bogus instant. */
static uint64_t xx_miz_dos_to_unix(uint16_t dos_date, uint16_t dos_time) {
    static const int32_t days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int32_t year;
    int32_t month;
    int32_t day;
    int32_t hour;
    int32_t minute;
    int32_t second;
    int32_t cursor;
    int64_t days;

    year = 1980 + (int32_t)((dos_date >> 9) & 0x7f);
    month = (int32_t)((dos_date >> 5) & 0x0f);
    day = (int32_t)(dos_date & 0x1f);
    hour = (int32_t)((dos_time >> 11) & 0x1f);
    minute = (int32_t)((dos_time >> 5) & 0x3f);
    second = (int32_t)((dos_time & 0x1f) * 2);

    if (month < 1 || month > 12) return 0U;
    if (day < 1 || day > 31) return 0U;
    if (hour > 23 || minute > 59 || second > 59) return 0U;

    days = 0;
    for (cursor = 1970; cursor < year; ++cursor) {
        bool leap = ((cursor % 4) == 0 && (cursor % 100) != 0) ||
                    ((cursor % 400) == 0);
        days += leap ? 366 : 365;
    }
    days += days_before_month[month - 1];
    if (month > 2 && (((year % 4) == 0 && (year % 100) != 0) ||
                      ((year % 400) == 0))) {
        days += 1;
    }
    days += day - 1;

    return (uint64_t)(days * 86400 + hour * 3600 + minute * 60 + second);
}

static xx_miz_stream *xx_miz_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const uint8_t magic_head[4] = {'D', 'K', 'C', 'L'};
    static const uint8_t magic_tail[4] = {'S', 'B', 'R', 'W'};
    static const uint8_t magic_foot[4] = {'M', 'J', 'D', 'K'};
    xx_miz_stream *stream;
    xx_miz_member member;
    uint8_t header[XX_MIZ_MAGIC_SIZE];
    uint8_t name_field[XX_MIZ_MAX_NAME_FIELD];
    uint8_t record[XX_MIZ_RECORD_SIZE];
    uint8_t footer[XX_MIZ_FOOTER_SIZE];
    uint8_t preamble[2];
    char *name = NULL;
    int64_t total;
    int64_t span;
    int64_t name_field_size;
    int64_t record_offset;
    int64_t data_offset;
    int64_t footer_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    int64_t terminator;
    int64_t cursor;
    uint16_t dos_time;
    uint16_t dos_date;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Smallest conceivable container: magic + a two-byte name field + record
     * + a bare stream + footer. */
    if (span < XX_MIZ_MAGIC_SIZE + 2 + XX_MIZ_RECORD_SIZE + XX_MIZ_FOOTER_SIZE) {
        return NULL;
    }
    if (!xx_miz_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    /* The two magics and the version word are contiguous and must ALL hold.
     * "DKCL" on its own is a four-byte match; "SBRW" six bytes later with
     * version 1 wedged between them is what makes this format identifiable.
     * Dropping either magic turns this reader into a four-byte grep. */
    if (xx_rt_memcmp(header, magic_head, sizeof(magic_head)) != 0) return NULL;
    if (xx_rt_memcmp(header + 6, magic_tail, sizeof(magic_tail)) != 0) {
        return NULL;
    }
    if ((uint32_t)xx_miz_le16(header + 4) != XX_MIZ_VERSION) return NULL;

    name_field_size = (int64_t)xx_miz_le16(header + 10);
    if (name_field_size < 2 || name_field_size > XX_MIZ_MAX_NAME_FIELD) {
        return NULL;
    }
    if (!xx_miz_range_within(span, XX_MIZ_MAGIC_SIZE, name_field_size)) {
        return NULL;
    }
    if (!xx_miz_read_at(self, self->base_address + XX_MIZ_MAGIC_SIZE,
                        name_field, (size_t)name_field_size)) {
        return NULL;
    }

    /* The field is a NUL-TERMINATED name inside a fixed-width slot, so read
     * the whole field and never scan one byte short: the terminator must
     * exist, the name before it must be non-empty printable ASCII, and every
     * byte after it must be NUL. That last clause is the cheap structural
     * check that keeps the magic from firing on unrelated data. */
    terminator = -1;
    for (cursor = 0; cursor < name_field_size; ++cursor) {
        if (name_field[cursor] == 0U) {
            terminator = cursor;
            break;
        }
    }
    if (terminator <= 0) return NULL;
    for (cursor = 0; cursor < terminator; ++cursor) {
        if (name_field[cursor] < 0x20U || name_field[cursor] > 0x7EU) {
            return NULL;
        }
    }
    for (cursor = terminator; cursor < name_field_size; ++cursor) {
        if (name_field[cursor] != 0U) return NULL;
    }

    record_offset = XX_MIZ_MAGIC_SIZE + name_field_size;
    if (!xx_miz_range_within(span, record_offset, XX_MIZ_RECORD_SIZE)) {
        return NULL;
    }
    if (!xx_miz_read_at(self, self->base_address + record_offset, record,
                        sizeof(record))) {
        return NULL;
    }

    dos_time = xx_miz_le16(record + 0);
    dos_date = xx_miz_le16(record + 2);
    /* Read through int32_t on purpose: a size field with its top bit set is
     * corrupt, not a two-gigabyte quantity. */
    uncompressed_size = (int64_t)(int32_t)xx_miz_le32(record + 4);
    compressed_size = (int64_t)(int32_t)xx_miz_le32(record + 8);
    if (uncompressed_size < 0 || compressed_size < 0) return NULL;
    if (uncompressed_size > XX_MIZ_MAX_SIZE ||
        compressed_size > XX_MIZ_MAX_SIZE) {
        return NULL;
    }

    data_offset = record_offset + XX_MIZ_RECORD_SIZE;
    /* A member whose extent runs past EOF is a rejection, not a short read. */
    if (!xx_miz_range_within(span, data_offset, compressed_size)) return NULL;

    footer_offset = data_offset + compressed_size;
    if (!xx_miz_range_within(span, footer_offset, XX_MIZ_FOOTER_SIZE)) {
        return NULL;
    }
    if (!xx_miz_read_at(self, self->base_address + footer_offset, footer,
                        sizeof(footer))) {
        return NULL;
    }
    /* "MJDK" closes every container, and it has to land exactly where the
     * compressed size puts it. That coupling - a magic at an offset the
     * header computes - is the strongest false-positive defence the format
     * has, and the one a later reader will be tempted to make optional. */
    if (xx_rt_memcmp(footer, magic_foot, sizeof(magic_foot)) != 0) return NULL;

    if (compressed_size >= 2) {
        if (!xx_miz_read_at(self, self->base_address + data_offset, preamble,
                            sizeof(preamble))) {
            return NULL;
        }
        /* The payload is a raw DCL stream whose first two bytes are plain
         * text: literal mode 0 or 1, then the dictionary exponent, which the
         * decoder only accepts as 4, 5 or 6. Two bytes that rule out a
         * container carrying a foreign payload. */
        if (preamble[0] > 1U) return NULL;
        if (preamble[1] < 4U || preamble[1] > 6U) return NULL;
    } else if (uncompressed_size > 0) {
        /* A non-empty member cannot fit in fewer than the two preamble
         * bytes. */
        return NULL;
    }

    if (pd && xx_pd_is_stopped(pd)) return NULL;

    stream = (xx_miz_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = (char *)xx_mem_alloc((size_t)terminator + 1U);
    if (!name) goto fail;
    for (cursor = 0; cursor < terminator; ++cursor) {
        name[cursor] = (char)name_field[cursor];
    }
    name[terminator] = '\0';
    if (!xx_miz_path_safe(name)) {
        xx_str_free(name);
        goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = data_offset;
    member.data_offset = self->base_address + data_offset;
    member.compressed_size = compressed_size;
    member.uncompressed_size = uncompressed_size;
    member.method = XX_MIZ_METHOD_DCL;
    member.timestamp = xx_miz_dos_to_unix(dos_date, dos_time);
    member.is_folder = false;
    if (!xx_miz_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }

    /* The archive ends at the footer; trailing bytes are overlay, not part
     * of the container. */
    stream->archive_size = footer_offset + XX_MIZ_FOOTER_SIZE;
    return stream;

fail:
    xx_miz_stream_free(stream);
    return NULL;
}


/* MIZ stores no method number: the payload is always a raw PKWARE DCL
 * implode stream. Recorded as 10, the ZIP numbering for that codec, so a
 * listing shows the method the switch below actually dispatches on. */
/* The stored uncompressed size is attacker-controlled; refuse rather than
 * attempt an allocation the container merely claims to need. */

static bool xx_miz_decode(Abstractformat *self, const xx_miz_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    /* Any method other than the one this reader implements fails here:
     * silently treating an unknown method as stored produces garbage that
     * looks like data. */
    if (member->method != XX_MIZ_METHOD_DCL) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > (int64_t)XX_MIZ_MAX_DECODED ||
        member->compressed_size > (int64_t)XX_MIZ_MAX_DECODED) {
        return false;
    }
    if ((uint64_t)member->uncompressed_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    /* A zero-length member is legal: the writer emits an empty stream for an
     * empty file. Parse has already refused a zero-length stream paired with
     * a non-zero uncompressed size, so this really is nothing to decode. */
    if (member->uncompressed_size == 0) {
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        *out = plain;
        *out_size = 0U;
        return true;
    }

    /* Two preamble bytes plus at least one byte of codes. */
    if (member->compressed_size < 3) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_miz_read_at(self, member->data_offset, packed,
                        (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    if (!xx_dcl_decode_memory(packed, (size_t)member->compressed_size, plain,
                              (size_t)member->uncompressed_size, &written)) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    /* Exactly the promised length or nothing: a partially decoded member
     * reported as success is the one failure a caller cannot detect. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_miz_init(xx_miz *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_MIZ;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-miz");
    xx_format_set_extension(&archive->format, "miz");
    archive->format.check_is_valid = xx_miz_check_is_valid;
    archive->format.handle_base_info = xx_miz_handle_base_info;
    archive->format.get_format_size = xx_miz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_miz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_miz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_miz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_miz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_miz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_miz_free_archive_records_reading;
    archive->format.destroy = xx_miz_vtable_destroy;
}

xx_miz *xx_miz_create(xx_io_device *device, int64_t base_address) {
    xx_miz *archive = (xx_miz *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_miz_init(archive, device, base_address);
    return archive;
}

void xx_miz_destroy(xx_miz *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_miz_free(xx_miz *archive) {
    if (!archive) return;
    xx_miz_destroy(archive);
    xx_mem_free(archive);
}

static void xx_miz_vtable_destroy(Abstractformat *self) {
    xx_miz_destroy((xx_miz *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_miz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_miz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_miz_parse(self, pd);
    if (!stream) return false;
    xx_miz_stream_free(stream);
    return true;
}

bool xx_miz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_miz *archive = (xx_miz *)self;
    xx_miz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_miz_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_miz_stream_free(stream);
    return true;
}

int64_t xx_miz_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_miz_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_miz *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_miz_set_record(xx_archive_record *record,
                                 const xx_miz_member *member) {
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

static bool xx_miz_copy_options(xx_list_s *target,
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

static const xx_var *xx_miz_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_miz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_miz_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_miz_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_miz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_miz_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_miz_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_miz_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_miz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_miz_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_miz_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_miz_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_miz_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_miz_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_miz_stream *stream;
    const xx_miz_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_miz_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_miz_path_safe(member->name)) return false;

    path_option = xx_miz_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_miz_decode(self, member, &plain, &plain_size, pd);
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
        !xx_miz_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
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
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_miz_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
