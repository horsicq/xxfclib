/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * FoxPro Distribution Kit archives (.PAK plus .PA1, .PA2, ... siblings).
 *
 *   lead volume, "FPAK":
 *     0x00  4 bytes  "FPAK"
 *     0x04  u16 LE   version
 *     0x06  u32 LE   total packed size of the whole distribution set
 *     0x0a  u32 LE   total raw size of the whole distribution set
 *     0x0e  u16 LE   length of the ASCII description that follows
 *     0x10  ...      description, that many printable bytes
 *
 *   continuation volume, "FPAC": four bytes, then straight into segments.
 *
 *   segment ("FPPF"), 30-byte fixed header then the name then the data:
 *     0x00  4 bytes  "FPPF"
 *     0x04  u16 LE   PKZIP general-purpose flags
 *     0x06  u16 LE   compression method: 0 stored, 6 imploded
 *     0x08  u16 LE   DOS time
 *     0x0a  u16 LE   DOS date
 *     0x0c  u32 LE   CRC-32 of the member plaintext
 *     0x10  u32 LE   packed size of the WHOLE member
 *     0x14  u32 LE   raw size of the WHOLE member
 *     0x18  u32 LE   size of THIS segment's slice of the stream
 *     0x1c  u16 LE   name length
 *
 * A member larger than the space left on a volume is split: consecutive
 * segments repeat the same name, packed size, raw size, CRC and timestamp, and
 * their slice sizes add up to the packed size.  assemble() below is that walk.
 *
 * A CONTINUATION VOLUME opens in the middle of whatever the volume before it
 * was still writing, so its first segment is that member's MIDDLE, not its
 * start.  There is nothing in this file that could decode it - the implode
 * trees are on the previous volume - so it is published as
 * "<name>.fragment" and extracting it fails outright.  Every later
 * incomplete segment does start here and is a genuine prefix.
 *
 * SCOPE: this reader covers ONE device.  The reference joins a lead .PAK with
 * its .PA1 / .PA2 siblings by deriving their paths from the source QFile's
 * name; an xx_io_device exposes no path, so there is nothing to derive from
 * and no sibling is opened.  A member whose stream runs off the end of this
 * volume is published as "<name>.partNN" and extracts to the part this volume
 * holds: LZ decoding is prefix-correct, so the slice yields the member's true
 * leading bytes rather than nothing.  The ".partNN" suffix is what says the
 * file is incomplete - there is no other way to say it, because the stored
 * CRC-32 covers the whole member and cannot speak for a prefix.
 *
 * The flags and method words MUST reach the codec: a stream decoded with the
 * wrong dictionary width or without its literal tree does not fail, it desyncs
 * into plausible garbage.  Only the member CRC-32 catches that, so every
 * extraction of a COMPLETE member verifies it; a partial one instead relies on
 * the decode having stopped by running out of input rather than desyncing.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/fpak/xx_fpak.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/fpak/xx_fpak.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Pending registration in xxfc_defs.h.  Once the enumerator XX_FILE_TYPE_FPAK
 * and its short alias FPAK are added there this fallback switches itself
 * off. */
#ifndef FPAK
#define XX_FILE_TYPE_FPAK XX_FILE_TYPE_UNKNOWN
#endif

#define XX_FPAK_GLOBAL_HEADER_SIZE 16
#define XX_FPAK_SEGMENT_HEADER_SIZE 30
#define XX_FPAK_MAX_SIZE ((int64_t)512 * 1024 * 1024)
#define XX_FPAK_MAX_SEGMENTS 100000
#define XX_FPAK_MAX_NAME 512
#define XX_FPAK_MAX_DESCRIPTION 1024

typedef struct xx_fpak_segment_s {
    int64_t header_offset;
    int64_t header_size; /**< 30 + name length. */
    int64_t data_offset;
    int64_t data_size;  /**< This slice. */
    int64_t packed_size; /**< The whole member. */
    int64_t raw_size;    /**< The whole member. */
    uint32_t crc32;
    uint16_t dos_time;
    uint16_t dos_date;
    uint16_t method;
    uint16_t flags;
    char *name;
    /* The 30 fixed bytes are pinned so extraction can prove the volume has
     * not changed under it since the walk.  The reference pins the name bytes
     * too; the fixed header carries the name length, and re-reading up to
     * 100000 names would cost tens of megabytes, so only the fixed part is
     * kept here. */
    uint8_t pinned_header[XX_FPAK_SEGMENT_HEADER_SIZE];
} xx_fpak_segment;

typedef struct xx_fpak_member_s {
    size_t first_segment; /**< Index into xx_fpak_stream::segments. */
    size_t segment_count;
    int64_t compressed_size; /**< Slices actually present on this volume. */
    int64_t packed_size;
    int64_t raw_size;
    uint32_t crc32;
    uint16_t dos_time;
    uint16_t dos_date;
    uint16_t method;
    uint16_t flags;
    char *name;
    bool complete;
    /** Set for the leading segment of a continuation volume: the slice is a
     *  middle of the member's stream, not its start, so it is neither
     *  complete nor prefix-decodable. */
    bool continuation;
} xx_fpak_member;

typedef struct xx_fpak_stream_s {
    xx_fpak_segment *segments;
    size_t segment_count;
    xx_fpak_member *members;
    size_t member_count;
    size_t index;
    uint16_t version;
    bool is_lead;
    bool is_truncated;
    int64_t volume_size;
    int64_t parsed_size;
    int64_t total_packed;
    int64_t total_raw;
} xx_fpak_stream;

typedef enum xx_fpak_assembly_e {
    XX_FPAK_ASSEMBLY_MALFORMED = 0,
    XX_FPAK_ASSEMBLY_INCOMPLETE,
    XX_FPAK_ASSEMBLY_COMPLETE
} xx_fpak_assembly;

static void xx_fpak_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_fpak_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_fpak_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_fpak_read_at(Abstractformat *self, int64_t offset,
                            uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 || (!buffer && size != 0U) ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

static bool xx_fpak_range_within(int64_t total, int64_t offset,
                                 int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

static bool xx_fpak_printable_ascii(const uint8_t *data, size_t size) {
    size_t index;

    if (!data || size == 0U) return false;
    for (index = 0U; index < size; ++index) {
        if (data[index] < 0x20U || data[index] > 0x7eU) return false;
    }
    return true;
}

/* Field-range validity only, which is what the reference's
 * isValidDosDateTime() amounts to: day 1-31, month 1-12, hour 0-23,
 * minute 0-59, two-second tick 0-29. */
static bool xx_fpak_valid_dos_date_time(uint16_t date, uint16_t time) {
    unsigned day = date & 0x1fU;
    unsigned month = (date >> 5) & 0x0fU;
    unsigned hour = (time >> 11) & 0x1fU;
    unsigned minute = (time >> 5) & 0x3fU;
    unsigned tick = time & 0x1fU;

    return day >= 1U && day <= 31U && month >= 1U && month <= 12U &&
           hour <= 23U && minute <= 59U && tick <= 29U;
}

/* Build the member name from the raw field.  Member names carry DOS
 * SUBDIRECTORY PATHS - most of the names in the reference corpus contain a
 * backslash - so the separator is translated rather than rejected, and every
 * traversal protection is applied per component instead of to the whole
 * string. */
static char *xx_fpak_safe_name(const uint8_t *field, size_t length) {
    char *name;
    size_t index;
    const char *cursor;

    if (!xx_fpak_printable_ascii(field, length)) return NULL;
    /* Leading or trailing blanks, a drive letter, or an absolute path. */
    if (field[0] == ' ' || field[length - 1U] == ' ') return NULL;
    if (field[0] == '\\' || field[0] == '/') return NULL;
    if (field[length - 1U] == '\\' || field[length - 1U] == '/') return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t character = field[index];
        if (character == ':' || character == '*' || character == '?' ||
            character == '"' || character == '<' || character == '>' ||
            character == '|') {
            return NULL;
        }
    }
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index) {
        name[index] = (field[index] == '\\') ? '/' : (char)field[index];
    }
    name[length] = '\0';

    /* Per-component checks: no empty, "." or ".." component, and no component
     * that ends in a space or a dot (which Windows silently strips). */
    cursor = name;
    while (*cursor) {
        const char *end = cursor;
        size_t part;
        while (*end && *end != '/') ++end;
        part = (size_t)(end - cursor);
        if (part == 0U || (part == 1U && cursor[0] == '.') ||
            (part == 2U && cursor[0] == '.' && cursor[1] == '.') ||
            cursor[part - 1U] == ' ' || cursor[part - 1U] == '.') {
            xx_str_free(name);
            return NULL;
        }
        cursor = *end ? end + 1 : end;
    }
    return name;
}

static void xx_fpak_stream_free(void *pointer) {
    xx_fpak_stream *stream = (xx_fpak_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->segment_count; ++index) {
        xx_str_free(stream->segments[index].name);
    }
    for (index = 0U; index < stream->member_count; ++index) {
        xx_str_free(stream->members[index].name);
    }
    xx_mem_free(stream->segments);
    xx_mem_free(stream->members);
    xx_mem_free(stream);
}

static bool xx_fpak_add_segment(xx_fpak_stream *stream,
                                const xx_fpak_segment *segment) {
    xx_fpak_segment *grown;

    if (!stream || !segment ||
        stream->segment_count >= (size_t)XX_FPAK_MAX_SEGMENTS) {
        return false;
    }
    grown = (xx_fpak_segment *)xx_mem_realloc(
        stream->segments, sizeof(*grown) * (stream->segment_count + 1U));
    if (!grown) return false;
    stream->segments = grown;
    stream->segments[stream->segment_count++] = *segment;
    return true;
}

static bool xx_fpak_add_member(xx_fpak_stream *stream,
                               const xx_fpak_member *member) {
    xx_fpak_member *grown;

    if (!stream || !member ||
        stream->member_count >= (size_t)XX_FPAK_MAX_SEGMENTS) {
        return false;
    }
    grown = (xx_fpak_member *)xx_mem_realloc(
        stream->members, sizeof(*grown) * (stream->member_count + 1U));
    if (!grown) return false;
    stream->members = grown;
    stream->members[stream->member_count++] = *member;
    return true;
}

/* Name a member that this volume cannot complete.  The reference appends
 * ".partNN" so the record is still listable and obviously partial. */
static char *xx_fpak_part_name(const char *base, unsigned part) {
    char suffix[16];

    if (!base) return NULL;
    if (xx_rt_snprintf(suffix, sizeof(suffix), ".part%02u", part) <= 0) {
        return NULL;
    }
    return xx_str_concat(base, suffix);
}

/* The first segment of a continuation volume is not part one of anything: it
 * carries the MIDDLE of a member whose stream began on the previous volume.
 * ".partNN" would be a false claim, so it gets its own suffix. */
static char *xx_fpak_fragment_name(const char *base) {
    if (!base) return NULL;
    return xx_str_concat(base, ".fragment");
}

/* ---------------------------------------------------------- volume walk -- */

/* Walk one volume's segment chain into @p stream.
 *
 * @p allow_truncated_tail keeps the segments walked so far when the chain
 * stops making sense before the end of the volume.  It is set only for the
 * volume the caller opened itself: a continuation volume must still account
 * for every one of its bytes, because a short one silently corrupts the
 * assembly of the split members that follow it.  Since this reader never
 * opens a sibling, it is always true here - but the parameter is kept so the
 * distinction survives if sibling support is added. */
static bool xx_fpak_read_volume(Abstractformat *self, xx_fpak_stream *stream,
                                bool allow_truncated_tail,
                                xx_pd_struct *pd) {
    uint8_t header[XX_FPAK_GLOBAL_HEADER_SIZE];
    uint8_t fixed[XX_FPAK_SEGMENT_HEADER_SIZE];
    uint8_t name_field[XX_FPAK_MAX_NAME];
    int64_t total;
    int64_t span;
    int64_t position;
    bool lead;
    bool truncated = false;

    if (!self || !self->device || !stream || self->base_address < 0) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    span = total - self->base_address;
    if (span < 4 || span > XX_FPAK_MAX_SIZE) return false;

    if (!xx_fpak_read_at(self, self->base_address, header, 4U)) return false;
    lead = header[0] == 'F' && header[1] == 'P' && header[2] == 'A' &&
           header[3] == 'K';
    if (!lead && !(header[0] == 'F' && header[1] == 'P' && header[2] == 'A' &&
                   header[3] == 'C')) {
        return false;
    }

    position = 4;
    if (lead) {
        int64_t description_size;
        uint8_t description[XX_FPAK_MAX_DESCRIPTION];

        if (span < XX_FPAK_GLOBAL_HEADER_SIZE) return false;
        if (!xx_fpak_read_at(self, self->base_address, header,
                             sizeof(header))) {
            return false;
        }
        stream->version = xx_fpak_le16(header + 4);
        stream->total_packed = (int64_t)xx_fpak_le32(header + 6);
        stream->total_raw = (int64_t)xx_fpak_le32(header + 10);
        description_size = (int64_t)xx_fpak_le16(header + 14);
        /* +6 is NOT a version count: it is the number of members in the whole
         * distribution set, so a lead volume of a large set carries a large
         * value.  Restricting it rejects most real volumes. */
        if (stream->version < 1U || stream->total_packed < 1 ||
            stream->total_packed > XX_FPAK_MAX_SIZE ||
            stream->total_raw < 1 || stream->total_raw > XX_FPAK_MAX_SIZE ||
            description_size < 1 ||
            description_size > XX_FPAK_MAX_DESCRIPTION ||
            !xx_fpak_range_within(span, XX_FPAK_GLOBAL_HEADER_SIZE,
                                  description_size)) {
            return false;
        }
        if (!xx_fpak_read_at(self,
                             self->base_address + XX_FPAK_GLOBAL_HEADER_SIZE,
                             description, (size_t)description_size) ||
            !xx_fpak_printable_ascii(description, (size_t)description_size)) {
            return false;
        }
        position = XX_FPAK_GLOBAL_HEADER_SIZE + description_size;
    }

    while (position < span) {
        xx_fpak_segment segment;
        uint16_t segment_flags = 0U;
        uint16_t segment_method = 0U;
        uint16_t dos_time = 0U;
        uint16_t dos_date = 0U;
        uint32_t crc32 = 0U;
        int64_t packed_size = 0;
        int64_t raw_size = 0;
        int64_t segment_size = 0;
        int64_t name_size = 0;
        char *name = NULL;
        /* Everything the walk cannot make sense of ends the chain.  Whether
         * that is fatal, or simply the end of the recoverable part of a volume
         * whose middle was damaged, is decided once at the bottom of the
         * block.  Device errors stay fatal either way: they say nothing about
         * the file. */
        bool malformed;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (stream->segment_count >= (size_t)XX_FPAK_MAX_SEGMENTS) {
            return false;
        }

        malformed = !xx_fpak_range_within(span, position,
                                          XX_FPAK_SEGMENT_HEADER_SIZE);
        if (!malformed) {
            if (!xx_fpak_read_at(self, self->base_address + position, fixed,
                                 sizeof(fixed))) {
                return false;
            }
            malformed = !(fixed[0] == 'F' && fixed[1] == 'P' &&
                          fixed[2] == 'P' && fixed[3] == 'F');
        }
        if (!malformed) {
            segment_flags = xx_fpak_le16(fixed + 4);
            segment_method = xx_fpak_le16(fixed + 6);
            dos_time = xx_fpak_le16(fixed + 8);
            dos_date = xx_fpak_le16(fixed + 10);
            crc32 = xx_fpak_le32(fixed + 12);
            packed_size = (int64_t)xx_fpak_le32(fixed + 16);
            raw_size = (int64_t)xx_fpak_le32(fixed + 20);
            segment_size = (int64_t)xx_fpak_le32(fixed + 24);
            name_size = (int64_t)xx_fpak_le16(fixed + 28);
            malformed =
                (segment_method != XX_FPAK_METHOD_STORED &&
                 segment_method != XX_FPAK_METHOD_IMPLODED) ||
                packed_size < 1 || packed_size > XX_FPAK_MAX_SIZE ||
                raw_size < 1 || raw_size > XX_FPAK_MAX_SIZE ||
                segment_size < 1 || segment_size > packed_size ||
                name_size < 1 || name_size > XX_FPAK_MAX_NAME ||
                !xx_fpak_range_within(span,
                                      position + XX_FPAK_SEGMENT_HEADER_SIZE,
                                      name_size + segment_size) ||
                ((dos_time != 0U || dos_date != 0U) &&
                 !xx_fpak_valid_dos_date_time(dos_date, dos_time));
        }
        if (!malformed) {
            if (!xx_fpak_read_at(
                    self,
                    self->base_address + position + XX_FPAK_SEGMENT_HEADER_SIZE,
                    name_field, (size_t)name_size)) {
                return false;
            }
            name = xx_fpak_safe_name(name_field, (size_t)name_size);
            malformed = name == NULL;
        }

        if (malformed) {
            if (!allow_truncated_tail || stream->segment_count == 0U) {
                return false;
            }
            truncated = true;
            break;
        }

        xx_mem_zero(&segment, sizeof(segment));
        segment.header_offset = self->base_address + position;
        segment.header_size = XX_FPAK_SEGMENT_HEADER_SIZE + name_size;
        segment.data_offset = segment.header_offset + segment.header_size;
        segment.data_size = segment_size;
        segment.packed_size = packed_size;
        segment.raw_size = raw_size;
        segment.crc32 = crc32;
        segment.method = segment_method;
        segment.flags = segment_flags;
        segment.dos_time = dos_time;
        segment.dos_date = dos_date;
        segment.name = name;
        xx_rt_memcpy(segment.pinned_header, fixed, sizeof(fixed));
        if (!xx_fpak_add_segment(stream, &segment)) {
            xx_str_free(name);
            return false;
        }
        position = segment.data_offset - self->base_address +
                   segment.data_size;
    }

    if (pd && xx_pd_is_stopped(pd)) return false;
    /* A volume with no segment is not a volume; and a volume that is not
     * truncated must account for every one of its bytes. */
    if (stream->segment_count == 0U || (!truncated && position != span)) {
        return false;
    }
    stream->is_lead = lead;
    stream->is_truncated = truncated;
    stream->volume_size = span;
    stream->parsed_size = position;
    return true;
}

/* ------------------------------------------------------------- assembly -- */

/* Fold the segment chain into members.  Consecutive segments belong to the
 * same member while their repeated fields agree and their slice sizes have not
 * yet added up to the packed size they all declare. */
static xx_fpak_assembly xx_fpak_assemble(xx_fpak_stream *stream,
                                         int64_t expected_packed,
                                         int64_t expected_raw,
                                         xx_fpak_member *partial) {
    int64_t completed_packed = 0;
    int64_t completed_raw = 0;
    xx_fpak_member current;
    size_t index;

    if (partial) xx_mem_zero(partial, sizeof(*partial));
    if (!stream || !partial || stream->segment_count == 0U ||
        expected_packed < 1 || expected_raw < 1) {
        return XX_FPAK_ASSEMBLY_MALFORMED;
    }
    xx_mem_zero(&current, sizeof(current));
    current.first_segment = 0U;

    for (index = 0U; index < stream->segment_count; ++index) {
        const xx_fpak_segment *segment = &stream->segments[index];

        if (current.segment_count == 0U) {
            current.first_segment = index;
            current.packed_size = segment->packed_size;
            current.raw_size = segment->raw_size;
            current.crc32 = segment->crc32;
            current.method = segment->method;
            current.flags = segment->flags;
            current.dos_time = segment->dos_time;
            current.dos_date = segment->dos_date;
            /* The name is borrowed from the first segment until the member is
             * published; ownership is taken at that point. */
            current.name = segment->name;
        } else {
            const xx_fpak_segment *first =
                &stream->segments[current.first_segment];
            if (first->packed_size != segment->packed_size ||
                first->raw_size != segment->raw_size ||
                first->crc32 != segment->crc32 ||
                first->dos_time != segment->dos_time ||
                first->dos_date != segment->dos_date ||
                !xx_str_equals(first->name, segment->name)) {
                return XX_FPAK_ASSEMBLY_MALFORMED;
            }
        }

        if (current.compressed_size >
            current.packed_size - segment->data_size) {
            return XX_FPAK_ASSEMBLY_MALFORMED;
        }
        ++current.segment_count;
        current.compressed_size += segment->data_size;
        if (current.compressed_size == current.packed_size) {
            xx_fpak_member published = current;
            published.complete = true;
            published.name = xx_str_dup(current.name);
            if (!published.name) return XX_FPAK_ASSEMBLY_MALFORMED;
            if (completed_packed > expected_packed - current.packed_size ||
                completed_raw > expected_raw - current.raw_size) {
                xx_str_free(published.name);
                return XX_FPAK_ASSEMBLY_MALFORMED;
            }
            completed_packed += current.packed_size;
            completed_raw += current.raw_size;
            if (!xx_fpak_add_member(stream, &published)) {
                xx_str_free(published.name);
                return XX_FPAK_ASSEMBLY_MALFORMED;
            }
            xx_mem_zero(&current, sizeof(current));
        }
    }

    if (current.segment_count != 0U) *partial = current;
    if (completed_packed > expected_packed || completed_raw > expected_raw) {
        return XX_FPAK_ASSEMBLY_MALFORMED;
    }
    if (current.segment_count == 0U && completed_packed == expected_packed &&
        completed_raw == expected_raw) {
        return XX_FPAK_ASSEMBLY_COMPLETE;
    }
    return XX_FPAK_ASSEMBLY_INCOMPLETE;
}

static xx_fpak_stream *xx_fpak_parse(Abstractformat *self,
                                     xx_pd_struct *pd) {
    xx_fpak_stream *stream;

    if (!self || !self->device) return NULL;
    stream = (xx_fpak_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    if (!xx_fpak_read_volume(self, stream, true, pd)) goto fail;

    if (!stream->is_lead) {
        /* A continuation volume has no global totals to assemble against, so
         * every segment is published on its own.  One that does not carry the
         * whole member is obviously partial. */
        unsigned part = 0U;
        size_t index;
        for (index = 0U; index < stream->segment_count; ++index) {
            const xx_fpak_segment *segment = &stream->segments[index];
            xx_fpak_member member;

            xx_mem_zero(&member, sizeof(member));
            member.first_segment = index;
            member.segment_count = 1U;
            member.compressed_size = segment->data_size;
            member.packed_size = segment->packed_size;
            member.raw_size = segment->raw_size;
            member.crc32 = segment->crc32;
            member.dos_time = segment->dos_time;
            member.dos_date = segment->dos_date;
            /* The codec profile has to be carried across here too: leaving it
             * at a default decodes every member of a continuation volume as
             * method 6 with flags 0, silently losing the 8 KiB dictionary
             * (0x02) and the literal tree (0x04). */
            member.method = segment->method;
            member.flags = segment->flags;
            member.complete = segment->data_size == segment->packed_size;
            /* The volume opens in the middle of whatever the previous volume
             * was still writing, so an incomplete FIRST segment is that
             * member's middle - its Shannon-Fano trees are on the volume
             * before this one.  Every later incomplete segment starts here
             * and really is a prefix. */
            member.continuation = !member.complete && index == 0U;
            member.name = member.complete
                              ? xx_str_dup(segment->name)
                              : (member.continuation
                                     ? xx_fpak_fragment_name(segment->name)
                                     : xx_fpak_part_name(segment->name,
                                                         ++part));
            if (!member.name) goto fail;
            if (!xx_fpak_add_member(stream, &member)) {
                xx_str_free(member.name);
                goto fail;
            }
        }
    } else {
        xx_fpak_member partial;
        xx_fpak_assembly status = xx_fpak_assemble(
            stream, stream->total_packed, stream->total_raw, &partial);

        if (status == XX_FPAK_ASSEMBLY_MALFORMED) goto fail;
        if (status == XX_FPAK_ASSEMBLY_INCOMPLETE) {
            /* No sibling volume can be opened (see the file header comment),
             * so the shortfall is permanent.  A split stream still gives a
             * concrete partial member to expose.  If the global totals instead
             * claim an entirely absent member there is no trustworthy
             * name or header to list, and the lead header is malformed -
             * unless the walk itself already reported where the volume stopped
             * making sense, which explains the shortfall on its own. */
            if (partial.segment_count == 0U) {
                if (!stream->is_truncated) goto fail;
            } else {
                xx_fpak_member published = partial;
                published.complete = false;
                published.name = xx_fpak_part_name(partial.name, 1U);
                if (!published.name) goto fail;
                if (!xx_fpak_add_member(stream, &published)) {
                    xx_str_free(published.name);
                    goto fail;
                }
            }
        }
    }

    if (stream->member_count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;
    return stream;

fail:
    xx_fpak_stream_free(stream);
    return NULL;
}

/* ------------------------------------------------------------ extraction -- */

/* Concatenate the member's slices, re-proving each segment header first. */
static bool xx_fpak_read_member_data(Abstractformat *self,
                                     const xx_fpak_stream *stream,
                                     const xx_fpak_member *member,
                                     uint8_t **out, size_t *out_size,
                                     xx_pd_struct *pd) {
    uint8_t *packed;
    size_t offset = 0U;
    size_t index;

    if (!out || !out_size) return false;
    *out = NULL;
    *out_size = 0U;
    if (!self || !stream || !member || member->segment_count == 0U ||
        member->compressed_size < 1 ||
        member->compressed_size > XX_FPAK_MAX_SIZE) {
        return false;
    }
    if (member->first_segment > stream->segment_count ||
        member->segment_count > stream->segment_count - member->first_segment) {
        return false;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;

    for (index = 0U; index < member->segment_count; ++index) {
        const xx_fpak_segment *segment =
            &stream->segments[member->first_segment + index];
        uint8_t fixed[XX_FPAK_SEGMENT_HEADER_SIZE];

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_fpak_read_at(self, segment->header_offset, fixed,
                             sizeof(fixed)) ||
            xx_rt_memcmp(fixed, segment->pinned_header, sizeof(fixed)) != 0) {
            goto fail;
        }
        if (segment->data_size < 0 ||
            (uint64_t)segment->data_size >
                (uint64_t)member->compressed_size - offset) {
            goto fail;
        }
        if (!xx_fpak_read_at(self, segment->data_offset, packed + offset,
                             (size_t)segment->data_size)) {
            goto fail;
        }
        offset += (size_t)segment->data_size;
    }
    if (offset != (size_t)member->compressed_size) goto fail;
    *out = packed;
    *out_size = offset;
    return true;

fail:
    xx_mem_free(packed);
    return false;
}

/* Decode one member.  A complete one must produce exactly its declared
 * plaintext length AND match its CRC-32; a member this volume cannot complete
 * produces the prefix its slice decodes to, which no CRC can cover, and is
 * published under the ".partNN" name the parse gave it. */
static bool xx_fpak_decode(Abstractformat *self, const xx_fpak_stream *stream,
                           const xx_fpak_member *member, uint8_t **out,
                           size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t packed_size = 0U;
    size_t plain_size;
    size_t written = 0U;

    if (!out || !out_size) return false;
    *out = NULL;
    *out_size = 0U;
    if (!self || !stream || !member || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    if (member->raw_size < 1 || member->raw_size > XX_FPAK_MAX_SIZE) {
        return false;
    }
    /* A mid-stream fragment has no decodable start: its Shannon-Fano trees
     * are on the previous volume, so reading trees out of its first bytes
     * reads the middle of somebody's compressed data.  Almost always that
     * fails the tree reader's completeness test, but "almost always" is not
     * a guarantee, and a tree that parses by accident would write pure
     * garbage into the caller's file.  Refuse before any of that happens -
     * only the sibling volume could make this member decodable, and there is
     * no way to reach one from a path-less device. */
    if (member->continuation) return false;
    plain_size = (size_t)member->raw_size;

    if (!xx_fpak_read_member_data(self, stream, member, &packed, &packed_size,
                                  pd)) {
        return false;
    }
    plain = (uint8_t *)xx_mem_alloc(plain_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* The header's own method and flags, never a guess: a stream decoded with
     * the wrong profile desyncs into plausible garbage instead of failing. */
    if (!member->complete) {
        /* The continuation volume that carries the rest of this member cannot
         * be opened from a path-less device, so only the slice on THIS volume
         * exists.  It decodes to the member's true prefix, which is published
         * under the ".partNN" name the parse gave it; the stored CRC-32
         * covers the whole member and cannot speak for a prefix, so the only
         * checks left are that the decode stopped on exhaustion rather than a
         * desync and that it produced something. */
        if (!xx_fpak_decode_partial_profile(packed, packed_size,
                                            member->method, member->flags,
                                            plain, plain_size, &written) ||
            written == 0U || written > plain_size ||
            (pd && xx_pd_is_stopped(pd))) {
            goto fail;
        }
        xx_mem_free(packed);
        *out = plain;
        *out_size = written;
        return true;
    }
    if (!xx_fpak_decode_memory_profile(packed, packed_size, member->method,
                                       member->flags, plain, plain_size,
                                       &written) ||
        written != plain_size || (pd && xx_pd_is_stopped(pd))) {
        goto fail;
    }
    /* The only thing that catches a wrong-profile decode. */
    if (xx_crc32_calc(0U, plain, plain_size) != member->crc32) goto fail;
    xx_mem_free(packed);
    *out = plain;
    *out_size = plain_size;
    return true;

fail:
    xx_mem_free(plain);
    xx_mem_free(packed);
    return false;
}

/* -------------------------------------------------------------- records -- */

static bool xx_fpak_copy_options(xx_list_s *target,
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

static const xx_var *xx_fpak_get_option(const xx_list_s *options,
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

static bool xx_fpak_set_record(xx_archive_record *record,
                               const xx_fpak_stream *stream,
                               const xx_fpak_member *member) {
    const xx_fpak_segment *first;
    bool ok;

    if (!record || !stream || !member || !member->name ||
        member->segment_count == 0U ||
        member->first_segment >= stream->segment_count) {
        return false;
    }
    first = &stream->segments[member->first_segment];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = first->header_offset;
    record->header_size = first->header_size;
    /* The stream starts at the first slice; a split member's later slices are
     * not contiguous with it, which is why an incomplete member is marked as
     * such rather than being handed to a generic copier. */
    record->data_offset = first->data_offset;
    record->compressed_size = first->data_size;
    ok = xx_archive_record_set_original_name(record, member->name) &&
         xx_archive_record_set_meta_u64(
             record, XX_META_ID_COMPRESSED_SIZE,
             (uint64_t)(member->complete ? member->packed_size
                                         : member->compressed_size)) &&
         xx_archive_record_set_meta_u64(
             record, XX_META_ID_UNCOMPRESSED_SIZE,
             (uint64_t)(member->complete ? member->raw_size
                                         : member->compressed_size)) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        member->method) &&
         /* The PKZIP general-purpose word: only 0x02 (8 KiB dictionary) and
          * 0x04 (literal tree) reach the codec, but a record that names a
          * codec without its profile is not self-describing. */
         xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                        member->flags) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                         false) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false);
    if (ok && member->complete) {
        ok = xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                            member->crc32) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                            member->dos_time) &&
             xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                            member->dos_date) &&
             xx_archive_record_set_meta_u64(
                 record, XX_META_ID_TIMESTAMP,
                 ((uint64_t)member->dos_date << 16) |
                     (uint64_t)member->dos_time);
    }
    return ok;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_fpak_init(xx_fpak *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_FPAK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    archive->format.os = XX_OS_DOS;
    xx_format_set_mime_type(&archive->format, "application/x-foxpro-fpak");
    xx_format_set_extension(&archive->format, "pak");
    archive->format.check_is_valid = xx_fpak_check_is_valid;
    archive->format.handle_base_info = xx_fpak_handle_base_info;
    archive->format.get_format_size = xx_fpak_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_fpak_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_fpak_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_fpak_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_fpak_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_fpak_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_fpak_free_archive_records_reading;
    archive->format.destroy = xx_fpak_vtable_destroy;
    archive->archive_size = -1;
}

xx_fpak *xx_fpak_create(xx_io_device *device, int64_t base_address) {
    xx_fpak *archive = (xx_fpak *)xx_mem_alloc(sizeof(*archive));

    if (archive) xx_fpak_init(archive, device, base_address);
    return archive;
}

void xx_fpak_destroy(xx_fpak *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    archive->number_of_segments = 0U;
    archive->version = 0U;
    archive->is_lead = false;
    archive->is_truncated = false;
    archive->archive_size = -1;
}

static void xx_fpak_vtable_destroy(Abstractformat *self) {
    xx_fpak_destroy((xx_fpak *)self);
}

void xx_fpak_free(xx_fpak *archive) {
    if (!archive) return;
    xx_fpak_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_fpak_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_fpak_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    /* Validity is the volume walk alone, as in the reference: a volume whose
     * members cannot be assembled is still a volume. */
    stream = (xx_fpak_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return false;
    xx_mem_zero(stream, sizeof(*stream));
    if (!xx_fpak_read_volume(self, stream, true, pd)) {
        xx_fpak_stream_free(stream);
        return false;
    }
    xx_fpak_stream_free(stream);
    return true;
}

bool xx_fpak_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_fpak *archive = (xx_fpak *)self;
    xx_fpak_stream *stream;
    int64_t total;

    if (!self) return false;
    stream = xx_fpak_parse(self, pd);
    if (!stream) {
        archive->number_of_records = 0U;
        archive->number_of_segments = 0U;
        archive->version = 0U;
        archive->is_lead = false;
        archive->is_truncated = false;
        archive->archive_size = -1;
        self->format_size = -1;
        self->overlay_offset = -1;
        self->overlay_size = 0;
        self->number_of_archive_records = 0U;
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    archive->number_of_records = stream->member_count;
    archive->number_of_segments = stream->segment_count;
    archive->version = stream->version;
    archive->is_lead = stream->is_lead;
    archive->is_truncated = stream->is_truncated;
    /* Where the chain actually ended, which is the whole volume unless it was
     * cut short. */
    archive->archive_size = stream->parsed_size;
    self->format_size = stream->parsed_size;
    self->number_of_archive_records = stream->member_count;
    total = xx_io_total_size(self->device);
    if (total > self->base_address + stream->parsed_size) {
        self->overlay_offset = self->base_address + stream->parsed_size;
        self->overlay_size = total - self->overlay_offset;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    if (xx_rt_snprintf(self->version, sizeof(self->version), "%u",
                       (unsigned)stream->version) <= 0) {
        self->version[0] = '\0';
    }
    self->file_type = XX_FILE_TYPE_FPAK;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    xx_fpak_stream_free(stream);
    return true;
}

int64_t xx_fpak_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_fpak_get_number_of_archive_records(Abstractformat *self,
                                               xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_fpak *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

xx_archive_record_state *xx_fpak_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_fpak_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_fpak_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_fpak_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_fpak_stream_free;
    state->total_records = (int64_t)stream->member_count;
    if (!xx_fpak_copy_options(&state->options, options) ||
        (stream->member_count != 0U &&
         !xx_fpak_set_record(&state->current_record, stream,
                             &stream->members[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->member_count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_fpak_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_fpak_archive_record_move_to_next(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xx_fpak_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fpak_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->member_count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_fpak_set_record(&state->current_record, stream,
                                           &stream->members[stream->index]);
    return state->has_record;
}

bool xx_fpak_unpack_current_archive_record(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_fpak_stream *stream;
    const xx_fpak_member *member;
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
    stream = (xx_fpak_stream *)state->internal_state;
    if (!stream || stream->index >= stream->member_count) return false;
    member = &stream->members[stream->index];
    /* A member the missing continuation volume would complete is written out
     * as the prefix this volume holds, under the ".partNN" name the parse
     * gave it.  Nothing is silently truncated: the suffix is part of the
     * member's published name, so the caller cannot mistake the file for the
     * whole thing. */

    path_option = xx_fpak_get_option(&state->options,
                                     XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything.  A complete member is CRC-checked on the
         * way through; a ".partNN" one has no CRC that could cover a prefix,
         * so all it proves is that the slice decodes. */
        result = xx_fpak_decode(self, stream, member, &plain, &plain_size, pd);
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

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_fpak_decode(self, stream, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        created = output != NULL;
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
    if (!result) {
        if (created) xx_rt_remove(target_path);
    } else if (member->dos_date != 0U || member->dos_time != 0U) {
        /* Best effort; a file system that refuses the stamp does not make the
         * extraction a failure. */
        (void)xx_io_apply_dos_time_and_attrs_a(target_path, member->dos_date,
                                               member->dos_time, 0U);
    }
    xx_str_free(target_path);
    return result;
}

void xx_fpak_free_archive_records_reading(Abstractformat *self,
                                          xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors -- */

uint64_t xx_fpak_get_number_of_records(const xx_fpak *archive) {
    return archive ? archive->number_of_records : 0U;
}

int64_t xx_fpak_get_archive_size(const xx_fpak *archive) {
    return archive ? archive->archive_size : -1;
}

uint16_t xx_fpak_get_version(const xx_fpak *archive) {
    return archive ? archive->version : 0U;
}
