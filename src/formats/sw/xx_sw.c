/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IRIX software distribution images (inst/swmgr "sw" product images).
 *
 *   header, 13 bytes:
 *     0x00  "im001V", 6 bytes
 *     0x06  three ASCII digits, the image format revision
 *     0x09  'P'
 *     0x0a  two ASCII digits, the product revision
 *     0x0c  0x00, closing the 12 byte ASCII magic
 *
 *   members follow from 0x0d, back to back, each:
 *     +0x00  u16 BE name length, 5..255
 *     +0x02  name, that many bytes, an IRIX path such as "usr/lib/foo"
 *     +...   payload, running to the start of the next member header
 *
 * There is no directory and no per-member size field: a member's payload ends
 * where the next member's length/name pair begins, and the last one ends at
 * EOF. The member chain is therefore recovered by scanning, exactly as the
 * reference extractor does - a sliding 64 KiB window with a 256 byte overlap,
 * restarted seven bytes past every hit (two length bytes plus the shortest
 * accepted name).
 *
 * Nothing is ever compressed. A member that holds a compress(1) or pack(1)
 * stream keeps it verbatim; the container itself only stores.
 *
 * What keeps the scan from splitting a payload at a random offset is the
 * requirement that a name start with one of the eight IRIX top level
 * directory prefixes. A bare 16 bit value landing in 5..255 is common in
 * binary data; one immediately followed by "usr/", "var/", "dev/", "etc/",
 * "lib/", "tmp/", "sbin" or "stan" and then only filename-legal bytes is not.
 * Bytes 0x0c..0x0e of the file carry the same idea for whole-file detection:
 * the magic's terminating NUL, then the high half of the first member's name
 * length (always zero, since names are shorter than 256 bytes) and its low
 * half (never zero, since a member always has a name).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sw/xx_sw.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_SW_COPY_CHUNK (64 * 1024)

typedef struct xx_sw_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_sw_member;

typedef struct xx_sw_stream_s {
    xx_sw_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_sw_stream;

static void xx_sw_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_sw_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_sw_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_sw_path_safe(const char *name) {
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

static void xx_sw_stream_free(void *pointer) {
    xx_sw_stream *stream = (xx_sw_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_sw_add(xx_sw_stream *stream,
                          const xx_sw_member *member) {
    xx_sw_member *grown = (xx_sw_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_sw_decode(Abstractformat *self,
                             const xx_sw_member *member, uint8_t **out,
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
         !xx_sw_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_SW_HEADER_SIZE 13
#define XX_SW_SCAN_BLOCK 0x10000
#define XX_SW_SCAN_OVERLAP 0x100
#define XX_SW_MIN_NAME 5
#define XX_SW_MAX_NAME 255
/* The largest image in the reference corpus holds 2896 members; the cap only
 * keeps a pathological input from building an unbounded list. */
#define XX_SW_MAX_MEMBERS 1000000

static uint32_t xx_sw_be16(const uint8_t *data) {
    return ((uint32_t)data[0] << 8) | (uint32_t)data[1];
}

static bool xx_sw_is_digit(uint8_t character) {
    return character >= (uint8_t)'0' && character <= (uint8_t)'9';
}

/* A member name always starts at one of the eight IRIX top level directories
 * the reference scanner recognises. This is what makes the boundary recovery
 * safe: without it a 16 bit length that happens to fall in 5..255 would split
 * a payload at a random offset, and any file at all would "parse". */
static bool xx_sw_known_prefix(const uint8_t *data) {
    static const char *const prefixes[8] = {"usr/", "var/", "dev/", "etc/",
                                            "lib/", "tmp/", "sbin", "stan"};
    int index;

    for (index = 0; index < 8; ++index) {
        if ((char)data[0] == prefixes[index][0] &&
            (char)data[1] == prefixes[index][1] &&
            (char)data[2] == prefixes[index][2] &&
            (char)data[3] == prefixes[index][3]) {
            return true;
        }
    }
    return false;
}

/* The reference scanner rejects a candidate whose name holds a control
 * character or any of " * < > ? \ | - characters that cannot appear in a
 * filename. '/' and ':' are allowed because they are path punctuation, and
 * bytes above 0x7E are allowed because IRIX paths are byte strings: the
 * reference corpus genuinely carries eight-bit names, so the usual
 * 0x20..0x7E rule would drop real members. */
static bool xx_sw_valid_name_byte(uint8_t character) {
    if (character < 0x20U) return false;
    if (character == (uint8_t)'"' || character == (uint8_t)'*' ||
        character == (uint8_t)'<' || character == (uint8_t)'>' ||
        character == (uint8_t)'?' || character == (uint8_t)'\\' ||
        character == (uint8_t)'|') {
        return false;
    }
    return true;
}

/* Names are IRIX paths. They are emitted with '/' separators and stripped of
 * anything that could escape the destination directory, matching what the
 * reference extractor writes. */
static char *xx_sw_make_name(const uint8_t *raw, size_t length) {
    char *name;
    size_t index;
    size_t start = 0U;
    size_t usable;

    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t character = raw[index];
        /* ':' would read as a drive separator on the host and a control byte
         * cannot be written at all; both become '_'. */
        if (character == (uint8_t)':' || character < 0x20U) {
            character = (uint8_t)'_';
        }
        name[index] = (char)character;
    }
    name[length] = '\0';

    while (name[start] == '/') ++start;
    usable = length - start;
    if (start > 0U) {
        for (index = 0U; index <= usable; ++index) {
            name[index] = name[start + index];
        }
    }
    /* Neutralise ".." as a whole path segment rather than as a substring, so
     * a legitimate name like "usr/lib/..foo" survives. */
    for (index = 0U; index + 1U < usable; ++index) {
        if (name[index] == '.' && name[index + 1] == '.' &&
            (index == 0U || name[index - 1] == '/') &&
            (index + 2U == usable || name[index + 2] == '/')) {
            name[index] = '_';
            name[index + 1] = '_';
        }
    }
    if (usable == 0U) {
        xx_str_free(name);
        return NULL;
    }
    return name;
}

static xx_sw_stream *xx_sw_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const uint8_t magic[6] = {'i', 'm', '0', '0', '1', 'V'};
    xx_sw_stream *stream = NULL;
    uint8_t *block = NULL;
    uint8_t header[XX_SW_HEADER_SIZE + 2];
    xx_sw_member pending;
    int64_t pending_data = 0;
    bool has_pending = false;
    int64_t total;
    int64_t span;
    int64_t position;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* The header, one length field and at least a five byte name must fit. */
    if (span < XX_SW_HEADER_SIZE + 2 + XX_SW_MIN_NAME) return NULL;
    if (!xx_sw_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;
    if (!xx_sw_is_digit(header[6]) || !xx_sw_is_digit(header[7]) ||
        !xx_sw_is_digit(header[8])) {
        return NULL;
    }
    if (header[9] != (uint8_t)'P') return NULL;
    if (!xx_sw_is_digit(header[10]) || !xx_sw_is_digit(header[11])) {
        return NULL;
    }
    /* Byte 12 closes the header, byte 13 is the high half of the first
     * member's name length (always zero, names are shorter than 256 bytes)
     * and byte 14 is its low half, which can never be zero. Together with the
     * twelve ASCII magic bytes this is the whole-file defence: it asserts
     * that a member header really does begin at 0x0d, so an unrelated file
     * that happens to start "im001V" does not become an archive. */
    if (header[12] != 0U) return NULL;
    if (header[13] != 0U) return NULL;
    if (header[14] == 0U) return NULL;

    block = (uint8_t *)xx_mem_alloc((size_t)XX_SW_SCAN_BLOCK);
    if (!block) return NULL;
    stream = (xx_sw_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(block);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));
    xx_mem_zero(&pending, sizeof(pending));

    position = XX_SW_HEADER_SIZE;
    for (;;) {
        int64_t available;
        int64_t block_size;
        int64_t limit;
        int64_t cursor;
        int64_t found = -1;
        int64_t name_length;
        int64_t data_offset;
        char *name;
        bool last;
        bool truncated = false;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (position >= span) break;
        available = span - position;
        block_size = (available < XX_SW_SCAN_BLOCK) ? available
                                                    : (int64_t)XX_SW_SCAN_BLOCK;
        /* Below six bytes nothing can hold a length plus a prefix. */
        if (block_size <= 5) break;
        if (!xx_sw_read_at(self, self->base_address + position, block,
                           (size_t)block_size)) {
            goto fail;
        }
        last = (block_size < XX_SW_SCAN_BLOCK);
        /* In a full block the 256 byte tail guarantees that a 255 byte name
         * found at the limit still lies inside the buffer; the next block
         * therefore restarts that much earlier. */
        limit = last ? (block_size - 6)
                     : (block_size - XX_SW_SCAN_OVERLAP - 1);

        for (cursor = 0; cursor <= limit; ++cursor) {
            int64_t candidate;
            int64_t index;
            bool name_ok = true;

            candidate = (int64_t)xx_sw_be16(block + cursor);
            if (candidate < XX_SW_MIN_NAME || candidate > XX_SW_MAX_NAME) {
                continue;
            }
            if (!xx_sw_known_prefix(block + cursor + 2)) continue;
            /* In the final block a name that would run past EOF ends the
             * walk: it is a truncated tail, not a member. */
            if (last && ((block_size - cursor - 2) < candidate)) {
                truncated = true;
                break;
            }
            for (index = 0; index < candidate; ++index) {
                if (!xx_sw_valid_name_byte(block[cursor + 2 + index])) {
                    name_ok = false;
                    break;
                }
            }
            if (name_ok) {
                found = cursor;
                break;
            }
        }

        if (truncated) break;
        if (found < 0) {
            if (last) break;
            position += block_size - XX_SW_SCAN_OVERLAP;
            continue;
        }

        position += found;
        name_length = (int64_t)xx_sw_be16(block + found);
        data_offset = position + 2 + name_length;

        /* The previous member's payload ends where this header starts; only
         * now is its size known. */
        if (has_pending) {
            int64_t size = position - pending_data;

            if (size < 0) {
                /* Headers overlap: the tail is not a member chain any more.
                 * Keep what has been recovered and stop, as the reference
                 * extractor does. */
                xx_str_free(pending.name);
                has_pending = false;
                break;
            }
            if (!xx_sw_range_within(span, pending_data, size)) {
                xx_str_free(pending.name);
                goto fail;
            }
            pending.compressed_size = size;
            pending.uncompressed_size = size;
            if (!xx_sw_add(stream, &pending)) {
                xx_str_free(pending.name);
                goto fail;
            }
            has_pending = false;
        }

        if (stream->count >= (size_t)XX_SW_MAX_MEMBERS) goto fail;
        /* The name was validated inside the block, so it is fully buffered. */
        name = xx_sw_make_name(block + found + 2, (size_t)name_length);
        if (!name) goto fail;

        xx_mem_zero(&pending, sizeof(pending));
        pending.name = name;
        pending.header_offset = self->base_address + position;
        pending.header_size = 2 + name_length;
        pending.data_offset = self->base_address + data_offset;
        pending_data = data_offset;
        has_pending = true;

        /* Restart two length bytes plus the shortest accepted name past the
         * hit, never inside it: a name may itself contain a byte pair that
         * reads as a plausible header. */
        position += 7;
    }

    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* The last member runs to EOF - there is no trailer to stop it. */
    if (has_pending) {
        int64_t size = span - pending_data;

        if (size < 0 || !xx_sw_range_within(span, pending_data, size)) {
            xx_str_free(pending.name);
            goto fail;
        }
        pending.compressed_size = size;
        pending.uncompressed_size = size;
        if (!xx_sw_add(stream, &pending)) {
            xx_str_free(pending.name);
            goto fail;
        }
        has_pending = false;
    }

    /* A valid header with no recoverable member is not an image. */
    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    xx_mem_free(block);
    return stream;

fail:
    xx_mem_free(block);
    xx_sw_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_sw_init(xx_sw *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_SW;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-sgi-inst");
    xx_format_set_extension(&archive->format, "sw");
    archive->format.check_is_valid = xx_sw_check_is_valid;
    archive->format.handle_base_info = xx_sw_handle_base_info;
    archive->format.get_format_size = xx_sw_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sw_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sw_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sw_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sw_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sw_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sw_free_archive_records_reading;
    archive->format.destroy = xx_sw_vtable_destroy;
}

xx_sw *xx_sw_create(xx_io_device *device, int64_t base_address) {
    xx_sw *archive = (xx_sw *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_sw_init(archive, device, base_address);
    return archive;
}

void xx_sw_destroy(xx_sw *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_sw_free(xx_sw *archive) {
    if (!archive) return;
    xx_sw_destroy(archive);
    xx_mem_free(archive);
}

static void xx_sw_vtable_destroy(Abstractformat *self) {
    xx_sw_destroy((xx_sw *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_sw_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_sw_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_sw_parse(self, pd);
    if (!stream) return false;
    xx_sw_stream_free(stream);
    return true;
}

bool xx_sw_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_sw *archive = (xx_sw *)self;
    xx_sw_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_sw_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_sw_stream_free(stream);
    return true;
}

int64_t xx_sw_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_sw_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_sw *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_sw_set_record(xx_archive_record *record,
                                 const xx_sw_member *member) {
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

static bool xx_sw_copy_options(xx_list_s *target,
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

static const xx_var *xx_sw_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_sw_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_sw_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_sw_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_sw_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_sw_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_sw_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_sw_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_sw_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sw_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_sw_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_sw_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_sw_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_sw_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_sw_stream *stream;
    const xx_sw_member *member;
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
    stream = (xx_sw_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_sw_path_safe(member->name)) return false;

    path_option = xx_sw_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_sw_decode(self, member, &plain, &plain_size, pd);
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
        !xx_sw_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_sw_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
