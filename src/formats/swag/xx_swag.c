/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SWAG collections (.SWG), the "-sw1-" packing used by the SWAG
 * Reader's own archiver.
 *
 * The member header is an LHA level-0 header with a fixed, oversized
 * extension area, so the layout is rigid rather than variable:
 *
 *   0x00  u8      header size, counted from 0x02; always name size + 0xBB
 *   0x01  u8      LHA level-0 checksum: low byte of the sum of the
 *                 "header size" bytes that follow this one
 *   0x02  char[5] "-sw1-", the method tag
 *   0x07  i32 LE  compressed size
 *   0x0B  i32 LE  uncompressed size (0 means the stream is stored)
 *   0x0F  u16 LE  MS-DOS time
 *   0x11  u16 LE  MS-DOS date
 *   0x13  u16 LE  MS-DOS attributes
 *   0x15  u32 LE  CRC-32 register *before* its final inversion
 *   0x19  ShortString[0x28]  name of the archive the snippet came from
 *   0x41..0xB9   unused extension area, zero filled
 *   0xBA  u8      member name length, 1..12
 *   0xBB  char[]  member name, MS-DOS 8.3, no terminator
 *   +hs   u16 LE  CRC-16 of the decoded member
 *   +hs+2         the stream, compressed size bytes
 *
 * The file ends with a 0x81-byte footer:
 *
 *   0x00  ShortString[60]  copyright line
 *   0x3D  ShortString[65]  collection title
 *   0x7F  u16 LE           number of members
 *
 * The walk starts at offset 0 and follows header + stream to the next
 * header, footer-declared count times. The reference implementation stops
 * the walk rather than failing the file when a header does not parse or a
 * stream runs past EOF: two corpus archives end in a truncated member and
 * everything before it is still good. At least one member must parse.
 *
 * Detection rests on three things together, and none of them alone: the
 * literal "-sw1-" at 0x02, the level-0 checksum over ~195 bytes, and the
 * hard equality header size == name size + 0xBB, which is what the fixed
 * extension area buys. The footer count merely bounds the walk.
 *
 * Note the unrelated SWAGPACKET format (SWAGOLX.EXE packets): it is keyed
 * on a 48-byte ASCII banner at offset 0, whose bytes at 0x02 are "AGO",
 * never "-sw1-", so the two cannot shadow each other.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/swag/xx_swag.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzh/xx_lzh.h"

#include <stdio.h>

#define XX_SWAG_COPY_CHUNK (64 * 1024)

typedef struct xx_swag_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_swag_member;

typedef struct xx_swag_stream_s {
    xx_swag_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_swag_stream;

static void xx_swag_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_swag_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_swag_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_swag_path_safe(const char *name) {
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

static void xx_swag_stream_free(void *pointer) {
    xx_swag_stream *stream = (xx_swag_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_swag_add(xx_swag_stream *stream,
                          const xx_swag_member *member) {
    xx_swag_member *grown = (xx_swag_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_SWAG_FIXED_HEADER_SIZE 0xBB
#define XX_SWAG_NAMELENGTH_OFFSET 0xBA
#define XX_SWAG_MAX_NAME_SIZE 12
#define XX_SWAG_FOOTER_SIZE 0x81
#define XX_SWAG_FOOTER_COUNT_OFFSET 0x7F
#define XX_SWAG_MAX_HEADER_BLOCK 257
#define XX_SWAG_MAX_MEMBERS 65535
#define XX_SWAG_MAX_UNCOMPRESSED 0x4000000
#define XX_SWAG_METHOD_STORE_P 0U
#define XX_SWAG_METHOD_LZH1_P 1U
#define XX_SWAG_MAX_DECODED (256 * 1024 * 1024)
#define XX_SWAG_METHOD_STORE 0U
#define XX_SWAG_METHOD_LZH1 1U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_swag_le16(const uint8_t *data);
static uint32_t xx_swag_le32(const uint8_t *data);
static bool xx_swag_name_valid(const uint8_t *name, size_t size);
static xx_swag_stream *xx_swag_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_swag_decode(Abstractformat *self, const xx_swag_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Everything up to and including the name length byte at 0xBA. */
/* MS-DOS 8.3, which is also the bound the reference archiver enforces. */
/* ShortString[60] copyright + ShortString[65] title + u16 member count. */
/* header size is a single byte, so the whole header block fits this. */
/* The footer count is a u16; this only keeps a corrupt one bounded. */
/* 64 MB per-member sanity cap, as in the reference. */


static uint16_t xx_swag_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_swag_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* SWAG member names are plain MS-DOS 8.3 with no directory component, so
 * printable ASCII only and none of the characters a DOS path cannot carry.
 * A space is genuinely legal inside such a name, hence the 0x20 floor. */
static bool xx_swag_name_valid(const uint8_t *name, size_t size) {
    size_t index;

    if (size == 0U || size > (size_t)XX_SWAG_MAX_NAME_SIZE) return false;
    for (index = 0U; index < size; ++index) {
        uint8_t byte = name[index];

        if (byte < 0x20U || byte > 0x7EU) return false;
        if (byte == '"' || byte == '*' || byte == '<' || byte == '>' ||
            byte == '?' || byte == '|' || byte == ':' || byte == '/' ||
            byte == '\\') {
            return false;
        }
    }
    return true;
}

static xx_swag_stream *xx_swag_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_swag_stream *stream = NULL;
    uint8_t footer[XX_SWAG_FOOTER_SIZE];
    uint8_t header[XX_SWAG_MAX_HEADER_BLOCK];
    char name[XX_SWAG_MAX_NAME_SIZE + 1];
    int64_t total;
    int64_t span;
    int64_t offset = 0;
    int32_t declared_count;
    int32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* The smallest possible archive: one header with a one-character name
     * (0xBB + 1 + 2 bytes) and the footer. */
    if (span < XX_SWAG_FIXED_HEADER_SIZE + 3 + XX_SWAG_FOOTER_SIZE) {
        return NULL;
    }

    if (!xx_swag_read_at(self, self->base_address + span - XX_SWAG_FOOTER_SIZE,
                         footer, (size_t)XX_SWAG_FOOTER_SIZE)) {
        return NULL;
    }
    declared_count = (int32_t)xx_swag_le16(footer + XX_SWAG_FOOTER_COUNT_OFFSET);
    /* A collection with no members is not something the archiver writes, and
     * accepting zero here would make the walk below decide nothing at all. */
    if (declared_count <= 0 || declared_count > XX_SWAG_MAX_MEMBERS) {
        return NULL;
    }

    stream = (xx_swag_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    for (index = 0; index < declared_count; ++index) {
        xx_swag_member member;
        int64_t header_size;
        int64_t name_size;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t data_offset;
        uint32_t sum = 0U;
        int64_t scan;
        size_t copy;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (offset + XX_SWAG_FIXED_HEADER_SIZE > span) break;
        if (!xx_swag_read_at(self, self->base_address + offset, header,
                             (size_t)XX_SWAG_FIXED_HEADER_SIZE)) {
            goto fail;
        }

        header_size = (int64_t)header[0];
        name_size = (int64_t)header[XX_SWAG_NAMELENGTH_OFFSET];
        /* The whole point of the "-sw1-" layout: the header is exactly the
         * fixed 0xBB-byte block plus the name, so this is a hard structural
         * rule and not a heuristic. It also guarantees the name length byte
         * just read really lies inside the header. */
        if (header_size != name_size + XX_SWAG_FIXED_HEADER_SIZE) break;
        if (name_size <= 0 || name_size > XX_SWAG_MAX_NAME_SIZE) break;
        if (offset + header_size + 2 > span) break;

        if (!xx_swag_read_at(self, self->base_address + offset, header,
                             (size_t)(header_size + 2))) {
            goto fail;
        }
        /* The literal method tag: one of the three things detection rests
         * on, and the first a later reader would be tempted to drop. */
        if (xx_rt_memcmp(header + 2, "-sw1-", 5) != 0) break;

        /* LHA level-0 checksum: low byte of the sum of the header_size bytes
         * following the size and checksum fields. Roughly 195 summed bytes
         * make a false positive a 1-in-256 event on top of the tag match,
         * which is what keeps this format apart from anything else that
         * happens to carry those five characters. */
        for (scan = 0; scan < header_size; ++scan) sum += header[2 + scan];
        if ((uint8_t)(sum & 0xFFU) != header[1]) break;

        compressed_size = (int64_t)(int32_t)xx_swag_le32(header + 7);
        uncompressed_size = (int64_t)(int32_t)xx_swag_le32(header + 0x0B);
        if (compressed_size < 0 || uncompressed_size < 0) break;
        if (uncompressed_size > XX_SWAG_MAX_UNCOMPRESSED) break;

        if (!xx_swag_name_valid(header + XX_SWAG_FIXED_HEADER_SIZE,
                                (size_t)name_size)) {
            break;
        }

        data_offset = offset + header_size + 2;
        /* A truncated tail member: two of the reference archives end this
         * way, and everything before it is still good, so stop the walk
         * rather than rejecting the whole file. */
        if (!xx_swag_range_within(span, data_offset, compressed_size)) break;

        for (copy = 0U; copy < (size_t)name_size; ++copy) {
            name[copy] = (char)header[XX_SWAG_FIXED_HEADER_SIZE + copy];
        }
        name[(size_t)name_size] = '\0';

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = header_size + 2;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        if (uncompressed_size == 0) {
            /* This container spells "stored" as an uncompressed size of
             * zero, and then states no plain length at all, so the stream's
             * own length is it. */
            member.method = XX_SWAG_METHOD_STORE_P;
            member.uncompressed_size = compressed_size;
        } else {
            member.method = XX_SWAG_METHOD_LZH1_P;
            member.uncompressed_size = uncompressed_size;
        }
        /* Raw MS-DOS time and date, packed time | (date << 16). */
        member.timestamp = (uint64_t)xx_swag_le16(header + 0x0F) |
                           ((uint64_t)xx_swag_le16(header + 0x11) << 16);
        member.is_folder = false;
        if (!xx_swag_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        offset = data_offset + compressed_size;
    }

    /* The walk is allowed to stop early, but not before it has proved the
     * format once: no member means nothing verified the tag or checksum. */
    if (stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    stream->archive_size = span;
    return stream;

fail:
    xx_swag_stream_free(stream);
    return NULL;
}


/* The stated uncompressed size is attacker-controlled; refuse rather than
 * attempt an allocation above this, whatever the header claims. */

/* The container states one method tag, "-sw1-", for every member. What it
 * varies is the uncompressed size: zero means the stream was kept verbatim.
 * parse turns that pair into these two values. */

static bool xx_swag_decode(Abstractformat *self, const xx_swag_member *member,
                           uint8_t **out, size_t *out_size,
                           xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t alloc_size = 0U;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size < 0 ||
        member->uncompressed_size > XX_SWAG_MAX_DECODED ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    /* Anything but the two values parse can produce would mean a later
     * reader added a method and forgot this switch; falling through to the
     * stored path would hand the caller LZHUF bytes dressed up as data. */
    if (member->method != XX_SWAG_METHOD_STORE &&
        member->method != XX_SWAG_METHOD_LZH1) {
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_swag_read_at(self, member->data_offset, packed,
                         (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    /* A genuinely empty member is legal, and a zero-byte allocation is not
     * something to rely on, so keep one spare byte the decoder can never
     * reach: the capacity passed below is still the stated size. */
    alloc_size = (size_t)member->uncompressed_size;
    if (alloc_size == 0U) alloc_size = 1U;
    plain = (uint8_t *)xx_mem_alloc(alloc_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == XX_SWAG_METHOD_STORE) {
        size_t index;
        /* parse sets uncompressed_size = compressed_size for a stored
         * member, so the whole stream is the data. */
        for (index = 0U; index < (size_t)member->uncompressed_size; ++index) {
            plain[index] = packed[index];
        }
        written = (size_t)member->uncompressed_size;
    } else if (!xx_lzh1_decode_memory(packed, (size_t)member->compressed_size,
                                      plain,
                                      (size_t)member->uncompressed_size,
                                      &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* A short decode reported as success is the one failure the caller
     * cannot detect, so the produced length must match the header exactly. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_swag_init(xx_swag *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_SWAG;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-swag");
    xx_format_set_extension(&archive->format, "swg");
    archive->format.check_is_valid = xx_swag_check_is_valid;
    archive->format.handle_base_info = xx_swag_handle_base_info;
    archive->format.get_format_size = xx_swag_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_swag_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_swag_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_swag_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_swag_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_swag_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_swag_free_archive_records_reading;
    archive->format.destroy = xx_swag_vtable_destroy;
}

xx_swag *xx_swag_create(xx_io_device *device, int64_t base_address) {
    xx_swag *archive = (xx_swag *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_swag_init(archive, device, base_address);
    return archive;
}

void xx_swag_destroy(xx_swag *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_swag_free(xx_swag *archive) {
    if (!archive) return;
    xx_swag_destroy(archive);
    xx_mem_free(archive);
}

static void xx_swag_vtable_destroy(Abstractformat *self) {
    xx_swag_destroy((xx_swag *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_swag_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_swag_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_swag_parse(self, pd);
    if (!stream) return false;
    xx_swag_stream_free(stream);
    return true;
}

bool xx_swag_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_swag *archive = (xx_swag *)self;
    xx_swag_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_swag_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_swag_stream_free(stream);
    return true;
}

int64_t xx_swag_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_swag_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_swag *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_swag_set_record(xx_archive_record *record,
                                 const xx_swag_member *member) {
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

static bool xx_swag_copy_options(xx_list_s *target,
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

static const xx_var *xx_swag_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_swag_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_swag_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_swag_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_swag_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_swag_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_swag_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_swag_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_swag_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_swag_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_swag_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_swag_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_swag_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_swag_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_swag_stream *stream;
    const xx_swag_member *member;
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
    stream = (xx_swag_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_swag_path_safe(member->name)) return false;

    path_option = xx_swag_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_swag_decode(self, member, &plain, &plain_size, pd);
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
        !xx_swag_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_swag_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
