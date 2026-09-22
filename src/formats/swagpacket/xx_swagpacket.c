/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SWAG packets, as written by SWAGOLX.EXE (GDSOFT, 1993).
 *
 * The whole file is a chain of 128-byte blocks and every field in it is
 * space-padded ASCII -- there is not a single binary integer anywhere.
 *
 * Block 0 is the file header:
 *
 *   +0x00  banner, "SWAGOLX.EXE (c) 1993 GDSOFT  ALL RIGHTS RESERVED" (48)
 *   +0x30  one space
 *   +0x31  snippet count, 5 ASCII digits then spaces
 *   +0x36  nine spaces
 *   +0x3F  packet title (65)        -> 48 + 1 + 5 + 9 + 65 = 128
 *
 * Each member then starts on a 128-byte header block of its own:
 *
 *   +0x00  index, 8 bytes, the 1-based ordinal of the member
 *   +0x08  date, 8 bytes, "MM-DD-YY"
 *   +0x10  time, 5 bytes, "HH:MM"
 *   +0x15  author (25)
 *   +0x2E  contributor (25)
 *   +0x47  subject (25)
 *   +0x60  keyword (20)
 *   +0x74  block count, 7 ASCII digits then spaces
 *   +0x7B  four opaque bytes
 *   +0x7F  one space, a fixed separator
 *
 * The block count covers the member header itself, so the snippet occupies
 * the (count - 1) blocks that follow the header and the next member's header
 * begins count blocks on. The chain has to land exactly on end of file.
 *
 * SWAGOLX stores no member name, so members are numbered NNNN.pas, matching
 * the reference reader. Snippets are stored verbatim; the reference trims the
 * 0x1A terminator and the block padding and maps the 0xE3 line separator to
 * CR for display, but the bytes on disk are the whole trailing blocks and
 * that is what is published here.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/swagpacket/xx_swagpacket.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_SWAGPACKET_COPY_CHUNK (64 * 1024)

typedef struct xx_swagpacket_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_swagpacket_member;

typedef struct xx_swagpacket_stream_s {
    xx_swagpacket_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_swagpacket_stream;

static void xx_swagpacket_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_swagpacket_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_swagpacket_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_swagpacket_path_safe(const char *name) {
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

static void xx_swagpacket_stream_free(void *pointer) {
    xx_swagpacket_stream *stream = (xx_swagpacket_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_swagpacket_add(xx_swagpacket_stream *stream,
                          const xx_swagpacket_member *member) {
    xx_swagpacket_member *grown = (xx_swagpacket_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_swagpacket_decode(Abstractformat *self,
                             const xx_swagpacket_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *buffer;

    *out = NULL;
    *out_size = 0U;
    if (member->compressed_size < 0 ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!buffer) return false;
    if (member->compressed_size != 0 &&
        ((pd && xx_pd_is_stopped(pd)) ||
         !xx_swagpacket_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_SWAGPACKET_BLOCK_SIZE 128
#define XX_SWAGPACKET_BANNER_SIZE 48
#define XX_SWAGPACKET_BANNER_PAD_OFFSET 0x30
#define XX_SWAGPACKET_COUNT_OFFSET 0x31
#define XX_SWAGPACKET_COUNT_SIZE 5
#define XX_SWAGPACKET_GAP_OFFSET 0x36
#define XX_SWAGPACKET_GAP_SIZE 9
#define XX_SWAGPACKET_REC_INDEX_OFFSET 0x00
#define XX_SWAGPACKET_REC_INDEX_SIZE 8
#define XX_SWAGPACKET_REC_DATE_OFFSET 0x08
#define XX_SWAGPACKET_REC_TIME_OFFSET 0x10
#define XX_SWAGPACKET_REC_BLOCKS_OFFSET 0x74
#define XX_SWAGPACKET_REC_BLOCKS_SIZE 7
#define XX_SWAGPACKET_REC_TAIL_OFFSET 0x7F
#define XX_SWAGPACKET_MAX_BLOCKS 9999999
#define XX_SWAGPACKET_MAX_REGION 0x4000000
#define XX_SWAGPACKET_MAX_MEMBERS 99999
#define XX_SWAGPACKET_NAME_SIZE 16
#define XX_SWAGPACKET_NAME_DIGITS 4

/* No little/big-endian readers are declared here on purpose: every numeric
 * field in a SWAG packet is space-padded ASCII decimal, so there is no
 * multi-byte word anywhere in the container to decode. */

static bool xx_swagpacket_is_spaces(const uint8_t *block, int32_t offset,
                                    int32_t size) {
    int32_t i;

    for (i = 0; i < size; ++i) {
        if (block[offset + i] != (uint8_t)' ') return false;
    }
    return true;
}

/* A space-padded unsigned decimal: digits first, spaces after, nothing else.
 * An empty field fails, so a blank block count can never read as zero and
 * stall the walk. */
static bool xx_swagpacket_field_number(const uint8_t *block, int32_t offset,
                                       int32_t size, int64_t *result) {
    int64_t value = 0;
    int32_t digits = 0;
    bool padding = false;
    int32_t i;

    for (i = 0; i < size; ++i) {
        uint8_t character = block[offset + i];

        if (character == (uint8_t)' ') {
            padding = true;
        } else if (character >= (uint8_t)'0' && character <= (uint8_t)'9' &&
                   !padding) {
            value = value * 10 + (int64_t)(character - (uint8_t)'0');
            ++digits;
            if (value > (int64_t)XX_SWAGPACKET_MAX_BLOCKS) return false;
        } else {
            return false;
        }
    }
    if (digits == 0) return false;
    *result = value;
    return true;
}

/* The index field is written with an arbitrary amount of padding on either
 * side, so it is compared trimmed. It must be the plain 1-based ordinal with
 * no leading zeros -- that is what the reference's string compare against the
 * decimal ordinal amounts to. */
static bool xx_swagpacket_index_is(const uint8_t *block, int64_t expected) {
    int64_t value = 0;
    int32_t i = 0;
    int32_t digits = 0;

    while (i < XX_SWAGPACKET_REC_INDEX_SIZE &&
           block[XX_SWAGPACKET_REC_INDEX_OFFSET + i] == (uint8_t)' ') {
        ++i;
    }
    while (i < XX_SWAGPACKET_REC_INDEX_SIZE) {
        uint8_t character = block[XX_SWAGPACKET_REC_INDEX_OFFSET + i];

        if (character < (uint8_t)'0' || character > (uint8_t)'9') break;
        /* A leading zero would not compare equal to the decimal ordinal. */
        if (digits == 0 && character == (uint8_t)'0') return false;
        value = value * 10 + (int64_t)(character - (uint8_t)'0');
        if (value > (int64_t)XX_SWAGPACKET_MAX_MEMBERS) return false;
        ++digits;
        ++i;
    }
    if (digits == 0) return false;
    while (i < XX_SWAGPACKET_REC_INDEX_SIZE) {
        if (block[XX_SWAGPACKET_REC_INDEX_OFFSET + i] != (uint8_t)' ') {
            return false;
        }
        ++i;
    }
    return value == expected;
}

static bool xx_swagpacket_two_digits(const uint8_t *block, int32_t offset) {
    return block[offset] >= (uint8_t)'0' && block[offset] <= (uint8_t)'9' &&
           block[offset + 1] >= (uint8_t)'0' && block[offset + 1] <= (uint8_t)'9';
}

/* "MM-DD-YY" at +0x08 and "HH:MM" at +0x10, both fixed width with fixed
 * separators. The reference parses the components with a permissive integer
 * conversion that would also swallow a space; SWAGOLX always zero-pads, so
 * digits are required here. Calendar validity is deliberately not enforced,
 * matching the reference walk, which only rejects an unparsable stamp. */
static bool xx_swagpacket_stamp_ok(const uint8_t *block) {
    if (block[XX_SWAGPACKET_REC_DATE_OFFSET + 2] != (uint8_t)'-') return false;
    if (block[XX_SWAGPACKET_REC_DATE_OFFSET + 5] != (uint8_t)'-') return false;
    if (!xx_swagpacket_two_digits(block, XX_SWAGPACKET_REC_DATE_OFFSET)) {
        return false;
    }
    if (!xx_swagpacket_two_digits(block, XX_SWAGPACKET_REC_DATE_OFFSET + 3)) {
        return false;
    }
    if (!xx_swagpacket_two_digits(block, XX_SWAGPACKET_REC_DATE_OFFSET + 6)) {
        return false;
    }
    if (block[XX_SWAGPACKET_REC_TIME_OFFSET + 2] != (uint8_t)':') return false;
    if (!xx_swagpacket_two_digits(block, XX_SWAGPACKET_REC_TIME_OFFSET)) {
        return false;
    }
    if (!xx_swagpacket_two_digits(block, XX_SWAGPACKET_REC_TIME_OFFSET + 3)) {
        return false;
    }
    return true;
}

/* The format stores no member name at all, so the ordinal is the only stable
 * naming available; it is zero-padded to four digits like the reference. The
 * digits are emitted by hand rather than through a format string so the width
 * behaviour does not depend on the runtime printf subset. */
static bool xx_swagpacket_make_name(int64_t ordinal, char *buffer) {
    char digits[24];
    int32_t count = 0;
    int32_t length = 0;
    int32_t i;

    if (ordinal <= 0) return false;
    while (ordinal > 0 && count < (int32_t)sizeof(digits)) {
        digits[count++] = (char)('0' + (int32_t)(ordinal % 10));
        ordinal /= 10;
    }
    if (ordinal != 0) return false;
    while (length < XX_SWAGPACKET_NAME_DIGITS - count) buffer[length++] = '0';
    for (i = count; i > 0; --i) buffer[length++] = digits[i - 1];
    buffer[length++] = '.';
    buffer[length++] = 'p';
    buffer[length++] = 'a';
    buffer[length++] = 's';
    buffer[length] = '\0';
    return true;
}

static xx_swagpacket_stream *xx_swagpacket_parse(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    xx_swagpacket_stream *stream = NULL;
    uint8_t block[XX_SWAGPACKET_BLOCK_SIZE];
    char name[XX_SWAGPACKET_NAME_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t count = 0;
    int64_t i;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A packet needs the file header block plus at least one member block. */
    if (span < (int64_t)XX_SWAGPACKET_BLOCK_SIZE * 2) return NULL;

    if (!xx_swagpacket_read_at(self, self->base_address, block,
                               sizeof(block))) {
        return NULL;
    }
    /* The 48-byte banner is the only magic the format has, and it is written
     * verbatim by SWAGOLX.EXE. Everything else in this function is shape, so
     * shortening this compare is what would let noise in. */
    if (xx_rt_memcmp(block, "SWAGOLX.EXE (c) 1993 GDSOFT  ALL RIGHTS RESERVED",
                     XX_SWAGPACKET_BANNER_SIZE) != 0) {
        return NULL;
    }
    if (block[XX_SWAGPACKET_BANNER_PAD_OFFSET] != (uint8_t)' ') return NULL;
    /* Nine fixed spaces between the count and the title: a cheap check that
     * the header block really has this layout and not merely the banner. */
    if (!xx_swagpacket_is_spaces(block, XX_SWAGPACKET_GAP_OFFSET,
                                 XX_SWAGPACKET_GAP_SIZE)) {
        return NULL;
    }
    if (!xx_swagpacket_field_number(block, XX_SWAGPACKET_COUNT_OFFSET,
                                    XX_SWAGPACKET_COUNT_SIZE, &count)) {
        return NULL;
    }
    /* The field is five digits wide, so the cap is the field, not a policy. */
    if (count < 1 || count > (int64_t)XX_SWAGPACKET_MAX_MEMBERS) return NULL;

    stream = (xx_swagpacket_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_SWAGPACKET_BLOCK_SIZE;
    for (i = 0; i < count; ++i) {
        xx_swagpacket_member member;
        int64_t blocks = 0;
        int64_t region;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_swagpacket_range_within(span, offset,
                                        XX_SWAGPACKET_BLOCK_SIZE)) {
            goto fail;
        }
        if (!xx_swagpacket_read_at(self, self->base_address + offset, block,
                                   sizeof(block))) {
            goto fail;
        }

        if (!xx_swagpacket_field_number(block, XX_SWAGPACKET_REC_BLOCKS_OFFSET,
                                        XX_SWAGPACKET_REC_BLOCKS_SIZE,
                                        &blocks)) {
            goto fail;
        }
        /* The block count covers the member header too, so even a payload-less
         * member is two blocks. One block means a corrupt or misidentified
         * file, and zero would never advance the walk. */
        if (blocks < 2) goto fail;
        /* The last byte of the header block is a fixed separator space -- the
         * cheapest structural cross-check the format offers, and the one that
         * catches a header block whose fields happen to parse. */
        if (block[XX_SWAGPACKET_REC_TAIL_OFFSET] != (uint8_t)' ') goto fail;
        /* The index is a plain 1-based ordinal on every member of every known
         * packet. Requiring it turns a chance banner match inside an unrelated
         * file into a reject instead of a garbage listing. */
        if (!xx_swagpacket_index_is(block, i + 1)) goto fail;
        if (!xx_swagpacket_stamp_ok(block)) goto fail;

        region = (blocks - 1) * (int64_t)XX_SWAGPACKET_BLOCK_SIZE;
        /* Per-member sanity cap: the 7-digit field can describe a gigabyte of
         * snippet, which no packet has. */
        if (region > (int64_t)XX_SWAGPACKET_MAX_REGION) goto fail;
        if (!xx_swagpacket_range_within(
                span, offset + XX_SWAGPACKET_BLOCK_SIZE, region)) {
            goto fail;
        }

        if (stream->count >= (size_t)XX_SWAGPACKET_MAX_MEMBERS) goto fail;
        /* Generated from the ordinal, so it is ASCII by construction; there is
         * no stored name whose bytes could need screening. */
        if (!xx_swagpacket_make_name(i + 1, name)) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_SWAGPACKET_BLOCK_SIZE;
        member.data_offset =
            self->base_address + offset + XX_SWAGPACKET_BLOCK_SIZE;
        member.compressed_size = region;
        member.uncompressed_size = region;
        /* The stamp is validated above but not published: the packet records
         * only "MM-DD-YY HH:MM" with a two-digit year and no zone, so there is
         * no epoch value to hand over that would not be invented here. */
        member.timestamp = 0;
        member.is_folder = false;
        if (!xx_swagpacket_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        offset += blocks * (int64_t)XX_SWAGPACKET_BLOCK_SIZE;
        if (offset > span) goto fail;
    }

    if (stream->count == 0U) goto fail;
    /* The declared snippet count and the block chain have to agree with the
     * file size exactly. Every known packet ends on the last member's last
     * block, so a short or long tail is a reject rather than an overlay --
     * and this is the check that makes a banner embedded in a larger file fail
     * instead of listing whatever follows it. */
    if (offset != span) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_swagpacket_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_swagpacket_init(xx_swagpacket *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_SWAGPACKET;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-swag-packet");
    xx_format_set_extension(&archive->format, "swg");
    archive->format.check_is_valid = xx_swagpacket_check_is_valid;
    archive->format.handle_base_info = xx_swagpacket_handle_base_info;
    archive->format.get_format_size = xx_swagpacket_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_swagpacket_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_swagpacket_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_swagpacket_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_swagpacket_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_swagpacket_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_swagpacket_free_archive_records_reading;
    archive->format.destroy = xx_swagpacket_vtable_destroy;
}

xx_swagpacket *xx_swagpacket_create(xx_io_device *device, int64_t base_address) {
    xx_swagpacket *archive = (xx_swagpacket *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_swagpacket_init(archive, device, base_address);
    return archive;
}

void xx_swagpacket_destroy(xx_swagpacket *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_swagpacket_free(xx_swagpacket *archive) {
    if (!archive) return;
    xx_swagpacket_destroy(archive);
    xx_mem_free(archive);
}

static void xx_swagpacket_vtable_destroy(Abstractformat *self) {
    xx_swagpacket_destroy((xx_swagpacket *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_swagpacket_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_swagpacket_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_swagpacket_parse(self, pd);
    if (!stream) return false;
    xx_swagpacket_stream_free(stream);
    return true;
}

bool xx_swagpacket_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_swagpacket *archive = (xx_swagpacket *)self;
    xx_swagpacket_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_swagpacket_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_swagpacket_stream_free(stream);
    return true;
}

int64_t xx_swagpacket_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_swagpacket_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_swagpacket *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_swagpacket_set_record(xx_archive_record *record,
                                 const xx_swagpacket_member *member) {
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

static bool xx_swagpacket_copy_options(xx_list_s *target,
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

static const xx_var *xx_swagpacket_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_swagpacket_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_swagpacket_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_swagpacket_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_swagpacket_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_swagpacket_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_swagpacket_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_swagpacket_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_swagpacket_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_swagpacket_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_swagpacket_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_swagpacket_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_swagpacket_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_swagpacket_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_swagpacket_stream *stream;
    const xx_swagpacket_member *member;
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
    stream = (xx_swagpacket_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_swagpacket_path_safe(member->name)) return false;

    path_option = xx_swagpacket_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_swagpacket_decode(self, member, &plain, &plain_size, pd);
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
        !xx_swagpacket_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_swagpacket_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
