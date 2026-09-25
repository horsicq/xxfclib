/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TR-DOS disk image (.TRD) - the raw sector dump of a ZX Spectrum Beta Disk
 * floppy.  Ported from XArchive's XTRDOSArchive.
 *
 *   Track 0 sectors 0..7, i.e. file bytes 0x000..0x7FF, are the catalogue:
 *   128 records of 16 bytes.
 *
 *     +0x00  char[8]  name; byte 0 == 0 ends the catalogue, == 1 means deleted
 *     +0x08  char     the single-character TR-DOS extension
 *     +0x09  u16 LE   start address
 *     +0x0B  u16 LE   length in bytes
 *     +0x0D  u8       sector count
 *     +0x0E  u8       first sector, 0..15
 *     +0x0F  u8       first track
 *
 *   Member data lives at  first_sector * 0x100 + first_track * 0x1000  and is
 *   sector_count * 0x100 bytes of raw, uncompressed sectors.
 *
 * A MEMBER IS NOT EMITTED AS RAW SECTORS.  Like the SCL container, a member is
 * written as a Hobeta file: a 17-byte header - the record's first 13 bytes, a
 * zero byte, the sector count, and a little-endian 16-bit checksum of
 * (sum of those 15 bytes) * 0x101 + 0x69 - followed by the sectors.  The
 * catalogue record is the only place the TR-DOS name, type, start address and
 * length live, so a bare sector dump would lose all of it.
 *
 * DETECTION IS THE HARD PART.  A .TRD has NO MAGIC at offset 0: the loose test
 * is "a file of 0x800..0xA0000 bytes whose first catalogue record is not free",
 * which in a shared detector chain would claim a large share of every other
 * family's samples.  So the TR-DOS DISK DESCRIPTOR in sector 8 is required as
 * well:
 *
 *     +0x8E3  u8  disk type, 0x16..0x19 (80/40 tracks, double/single sided)
 *     +0x8E7  u8  0x10, the TR-DOS identifier
 *
 * Both hold in all 21 reference images and both are part of the on-disk
 * format, not a heuristic.  If a legitimately damaged image ever needs to be
 * read, THAT is the check to relax - not the catalogue walk.
 *
 * Unlike the reference, a record whose sector extent does not lie inside the
 * real file is REJECTED rather than clamped: first_track, first_sector and
 * sector_count are attacker-controlled bytes, and a clamp turns a nonsense
 * catalogue into a published member at a bogus offset.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/trdos/xx_trdos.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Self-healing shim: the file compiles before the enum exists. */
#ifdef TRDOS
#define XX_TRDOS_FILE_TYPE XX_FILE_TYPE_TRDOS
#else
#define XX_TRDOS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_TRDOS_CATALOGUE_SIZE 0x800
#define XX_TRDOS_RECORD_SIZE 0x10
#define XX_TRDOS_RECORD_COUNT 128
#define XX_TRDOS_SECTOR_SIZE 0x100
#define XX_TRDOS_TRACK_SIZE 0x1000
#define XX_TRDOS_PREFIX_SIZE 17
#define XX_TRDOS_DESCRIPTOR_OFFSET 0x8E0
#define XX_TRDOS_DESCRIPTOR_SIZE 8
/* Through the disk descriptor; a shorter file cannot be identified at all. */
#define XX_TRDOS_MIN_SIZE 0x8E8
/* 80 tracks, double sided - the largest geometry TR-DOS describes. */
#define XX_TRDOS_MAX_SIZE 0xA0000
/* 255 sectors is the record's own ceiling, so this is a real bound. */
#define XX_TRDOS_MAX_DATA_SIZE (255 * XX_TRDOS_SECTOR_SIZE)

typedef struct xx_trdos_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint8_t prefix[XX_TRDOS_PREFIX_SIZE];
} xx_trdos_member;

typedef struct xx_trdos_stream_s {
    xx_trdos_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint8_t disk_type;
} xx_trdos_stream;

static void xx_trdos_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_trdos_read_at(Abstractformat *self, int64_t offset,
                             uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
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

static bool xx_trdos_range_within(int64_t total, int64_t offset,
                                  int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_trdos_path_safe(const char *name) {
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

static void xx_trdos_stream_free(void *pointer) {
    xx_trdos_stream *stream = (xx_trdos_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of its name on success. */
static bool xx_trdos_add(xx_trdos_stream *stream,
                         const xx_trdos_member *member) {
    xx_trdos_member *grown;

    if (stream->count >= (size_t)XX_TRDOS_RECORD_COUNT) return false;
    grown = (xx_trdos_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* TR-DOS pads the eight name bytes with spaces; the reference right-trims them
 * with Python's str.rstrip() semantics, which strips ASCII whitespace plus NEL
 * and NBSP.  Corpus names genuinely carry LEADING spaces too - the OBERON
 * disks hold "  oberon", " oberon" and "oberon" side by side - and the
 * reference keeps them.  Here they are trimmed as well, because the reference
 * unpacker (U3) does and because a filename that starts with a space is
 * unopenable through the Win32 path APIs.  The trim only affects the published
 * name; the member bytes are unchanged, and two records that differ only by
 * padding then extract to the same path, exactly as the reference unpacker
 * does. */
static bool xx_trdos_is_trailing_space(uint8_t value) {
    return value == 0x20U || (value >= 0x09U && value <= 0x0DU) ||
           value == 0x85U || value == 0xA0U;
}

/* "NAME    " + type byte -> "NAME.$B".
 *
 * TR-DOS stores the type as a single byte that is normally a letter but is a
 * raw code on some disks; a control byte maps onto '_' rather than being
 * dropped, because the byte still distinguishes two files sharing a name.
 * Bytes that would turn one member into a path, or that no filesystem
 * accepts, are replaced - the catalogue has already been vouched for by the
 * disk descriptor, so a single odd byte is not grounds to reject the image. */
static char xx_trdos_safe_char(uint8_t value) {
    if (value < 0x20U || value == 0x7FU) return '_';
    if (value == '/' || value == '\\' || value == ':' || value == '*' ||
        value == '?' || value == '"' || value == '<' || value == '>' ||
        value == '|') {
        return '_';
    }
    return (char)value;
}

static char *xx_trdos_record_name(const uint8_t *record) {
    char buffer[12];
    size_t begin = 0U;
    size_t end = 8U;
    size_t length;
    size_t index;

    while (end > 0U && xx_trdos_is_trailing_space(record[end - 1U])) --end;
    while (begin < end && xx_trdos_is_trailing_space(record[begin])) ++begin;
    /* A blank name is not a file; the caller has already refused the free and
     * deleted markers, so this can only be whitespace padding. */
    if (begin == end) return NULL;
    length = end - begin;
    for (index = 0U; index < length; ++index) {
        buffer[index] = xx_trdos_safe_char(record[begin + index]);
    }
    buffer[length] = '.';
    buffer[length + 1U] = '$';
    buffer[length + 2U] = xx_trdos_safe_char(record[8]);
    buffer[length + 3U] = '\0';
    return xx_str_dup(buffer);
}

/* Build the Hobeta header the image does not store.
 *
 * The checksum is the byte sum of the 15 bytes in front of it, multiplied by
 * 0x101 and biased by 0x69, truncated to 16 bits.  The multiply is what makes
 * it a checksum rather than a parity byte, and getting it wrong produces a
 * file every Spectrum emulator loads and every Hobeta tool rejects. */
static void xx_trdos_hobeta_prefix(const uint8_t *record, uint8_t sectors,
                                   uint8_t *prefix) {
    uint16_t checksum = 0U;
    size_t index;

    for (index = 0U; index < 13U; ++index) prefix[index] = record[index];
    prefix[13] = 0U;
    prefix[14] = sectors;
    for (index = 0U; index < 15U; ++index) {
        checksum = (uint16_t)(checksum + (uint16_t)prefix[index]);
    }
    checksum = (uint16_t)((uint16_t)(checksum * 0x101U) + 0x69U);
    prefix[15] = (uint8_t)(checksum & 0xFFU);
    prefix[16] = (uint8_t)((checksum >> 8) & 0xFFU);
}

/* --------------------------------------------------------------- parse -- */

static xx_trdos_stream *xx_trdos_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    xx_trdos_stream *stream;
    uint8_t descriptor[XX_TRDOS_DESCRIPTOR_SIZE];
    uint8_t catalogue[XX_TRDOS_CATALOGUE_SIZE];
    int64_t total;
    int64_t span;
    int64_t index;
    uint8_t disk_type;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_TRDOS_MIN_SIZE || span > XX_TRDOS_MAX_SIZE) return NULL;

    /* The disk descriptor is what makes this format identifiable at all. */
    if (!xx_trdos_read_at(self, self->base_address + XX_TRDOS_DESCRIPTOR_OFFSET,
                          descriptor, sizeof(descriptor))) {
        return NULL;
    }
    disk_type = descriptor[3];
    if (descriptor[7] != 0x10U) return NULL;
    if (disk_type < 0x16U || disk_type > 0x19U) return NULL;

    if (!xx_trdos_read_at(self, self->base_address, catalogue,
                          sizeof(catalogue))) {
        return NULL;
    }

    stream = (xx_trdos_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    stream->disk_type = disk_type;

    for (index = 0; index < XX_TRDOS_RECORD_COUNT; ++index) {
        int64_t record_offset = index * XX_TRDOS_RECORD_SIZE;
        const uint8_t *record = catalogue + record_offset;
        xx_trdos_member member;
        int64_t data_offset;
        int64_t data_size;
        size_t byte;
        bool blank = true;

        if (pd && xx_pd_is_stopped(pd)) goto fail;

        /* An all-zero name ends the catalogue; 0x00 or 0x01 in byte 0 alone
         * only means free or deleted, so the two tests are NOT the same. */
        for (byte = 0U; byte < 8U; ++byte) {
            if (record[byte] != 0U) {
                blank = false;
                break;
            }
        }
        if (blank) break;
        if (record[0] <= 1U) continue;

        data_offset = (int64_t)record[0x0E] * XX_TRDOS_SECTOR_SIZE +
                      (int64_t)record[0x0F] * XX_TRDOS_TRACK_SIZE;
        data_size = (int64_t)record[0x0D] * XX_TRDOS_SECTOR_SIZE;
        /* Bound the extent against the real file BEFORE anything is read or
         * allocated from it.  Reject, do not clamp. */
        if (data_size > XX_TRDOS_MAX_DATA_SIZE) goto fail;
        if (!xx_trdos_range_within(span, data_offset, data_size)) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_trdos_record_name(record);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + record_offset;
        member.header_size = XX_TRDOS_RECORD_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        /* The 17-byte Hobeta header is synthesised, not stored, so the member
         * is longer than its sectors by exactly that much. */
        member.uncompressed_size = data_size + XX_TRDOS_PREFIX_SIZE;
        xx_trdos_hobeta_prefix(record, record[0x0D], member.prefix);

        if (!xx_trdos_path_safe(member.name) ||
            !xx_trdos_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }

    /* A catalogue with no live record is not a TR-DOS archive. */
    if (stream->count == 0U) goto fail;
    /* The image is the format: members are scattered over the sector grid, so
     * the extent is the whole dump rather than the end of the last member. */
    stream->archive_size = span;
    return stream;

fail:
    xx_trdos_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

/* Nothing here is compressed: the member is its sectors, preceded by a header
 * synthesised from the catalogue record at parse time. */
static bool xx_trdos_decode(Abstractformat *self,
                            const xx_trdos_member *member, uint8_t **out,
                            size_t *out_size, xx_pd_struct *pd) {
    uint8_t *output;
    size_t index;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 ||
        member->compressed_size > XX_TRDOS_MAX_DATA_SIZE) {
        return false;
    }
    /* The published length is the prefix plus the sector extent; anything
     * else means the parse and the decode disagree. */
    if (member->uncompressed_size !=
        member->compressed_size + XX_TRDOS_PREFIX_SIZE) {
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) return false;
    for (index = 0U; index < (size_t)XX_TRDOS_PREFIX_SIZE; ++index) {
        output[index] = member->prefix[index];
    }
    if (member->compressed_size != 0 &&
        !xx_trdos_read_at(self, member->data_offset,
                          output + XX_TRDOS_PREFIX_SIZE,
                          (size_t)member->compressed_size)) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_trdos_init(xx_trdos *archive, xx_io_device *device,
                   int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_TRDOS_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-spectrum-trd");
    xx_format_set_extension(&archive->format, "trd");
    archive->format.check_is_valid = xx_trdos_check_is_valid;
    archive->format.handle_base_info = xx_trdos_handle_base_info;
    archive->format.get_format_size = xx_trdos_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_trdos_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_trdos_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_trdos_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_trdos_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_trdos_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_trdos_free_archive_records_reading;
    archive->format.destroy = xx_trdos_vtable_destroy;
}

xx_trdos *xx_trdos_create(xx_io_device *device, int64_t base_address) {
    xx_trdos *archive = (xx_trdos *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_trdos_init(archive, device, base_address);
    return archive;
}

void xx_trdos_destroy(xx_trdos *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_trdos_free(xx_trdos *archive) {
    if (!archive) return;
    xx_trdos_destroy(archive);
    xx_mem_free(archive);
}

static void xx_trdos_vtable_destroy(Abstractformat *self) {
    xx_trdos_destroy((xx_trdos *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_trdos_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_trdos_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_trdos_parse(self, pd);
    if (!stream) return false;
    xx_trdos_stream_free(stream);
    return true;
}

bool xx_trdos_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_trdos *archive = (xx_trdos *)self;
    xx_trdos_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_trdos_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    self->file_type = XX_TRDOS_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    archive->number_of_records = stream->count;
    archive->disk_type = stream->disk_type;
    xx_trdos_stream_free(stream);
    return true;
}

int64_t xx_trdos_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_trdos_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_trdos *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_trdos_set_record(xx_archive_record *record,
                                const xx_trdos_member *member) {
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
           /* TR-DOS stores nothing compressed and has no method field. */
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           /* The catalogue carries no timestamps and no directories. */
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_trdos_copy_options(xx_list_s *target,
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

static const xx_var *xx_trdos_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_trdos_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_trdos_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_trdos_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_trdos_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_trdos_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_trdos_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_trdos_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_trdos_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_trdos_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_trdos_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_trdos_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_trdos_set_record(&state->current_record,
                                            &stream->items[stream->index]);
    return state->has_record;
}

bool xx_trdos_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    xx_trdos_stream *stream;
    const xx_trdos_member *member;
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
    stream = (xx_trdos_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_trdos_path_safe(member->name)) return false;

    path_option = xx_trdos_get_option(&state->options,
                                      XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_trdos_decode(self, member, &plain, &plain_size, pd);
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
        !xx_trdos_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_trdos_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
