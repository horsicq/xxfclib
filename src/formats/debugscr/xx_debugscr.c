/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DOS DEBUG.EXE scripts (*.SCR, *.USR, *.DMP and friends).
 *
 * These are not binary containers at all: they are plain ASCII command
 * scripts meant to be fed to DEBUG.COM with "DEBUG < FILE.SCR". The script
 * dictates a file byte by byte and then tells DEBUG to write it out, so the
 * "archive" is the single binary the script would produce. This reader
 * reconstructs that binary by PARSING the text. Nothing is ever executed.
 *
 * The grammar this reader recognises, one command per line, case
 * insensitive, leading and trailing blanks ignored:
 *
 *   N <name>          name of the file DEBUG will write. Always the first
 *                     command; the corpus has no counter-example.
 *   E <addr> <hh>...  "enter" bytes: <addr> is a 1..4 digit hex offset
 *                     inside the loaded segment, the rest are hex byte
 *                     pairs. DEBUG loads a .COM image at offset 0x100, and
 *                     every script in the corpus starts its first E there.
 *   RCX  /  R CX      select the CX register; the NEXT line is its new
 *                     value in hex. CX:BX is the byte count DEBUG writes,
 *                     and no sample needs BX, so CX alone is the length.
 *   W                 write CX bytes starting at offset 0x100.
 *   Q                 quit.
 *
 * The written length is CX, NOT the extent of the E commands: DEBUG writes
 * from a zero-filled buffer, so a byte the script never enters is a zero in
 * the output, and a byte entered past CX is simply not written. 162 of 170
 * samples have CX exactly equal to the E extent; the remaining eight differ
 * by a few bytes in both directions, which is precisely why CX and not the
 * extent decides the size.
 *
 * Text a script carries after Q -- documentation, mail headers, a signature
 * block -- is common (23 samples) and is ignored, as is any command this
 * grammar does not know. Two samples assemble part of their image with an
 * "A" block instead of entering it; the bytes an A block would produce
 * cannot be recovered without an assembler, so those regions stay zero and
 * the reconstruction is incomplete rather than wrong.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/debugscr/xx_debugscr.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The alias macro is defined next to the enumerator in xxfc_defs.h, so
 * testing for it picks up the real file type as soon as DEBUGSCR is
 * registered there. Until then the reader identifies itself as unknown
 * rather than borrowing another format's id. */
#ifdef DEBUGSCR
#define XX_DEBUGSCR_FILE_TYPE XX_FILE_TYPE_DEBUGSCR
#else
#define XX_DEBUGSCR_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* A script is text a human typed or a tool emitted; the largest in the
 * reference corpus is 114 KiB. The cap keeps a hostile file from being
 * slurped whole. */
#define XX_DEBUGSCR_MAX_TEXT ((int64_t)16 * 1024 * 1024)
/* DEBUG loads at offset 0x100 of a 64 KiB segment, so the image it can write
 * without touching BX tops out here. */
#define XX_DEBUGSCR_LOAD_OFFSET 0x100U
#define XX_DEBUGSCR_MAX_IMAGE (0x10000U - XX_DEBUGSCR_LOAD_OFFSET)
/* A script that enters fewer bytes than this is not a file dictation. The
 * bar is low on purpose: the keyboard-patch scripts in the corpus dictate
 * as few as ten bytes, and the real gate is the N/E/RCX/W/Q shape, not the
 * payload size. */
#define XX_DEBUGSCR_MIN_ENTERED 4U
#define XX_DEBUGSCR_MAX_NAME 128U
#define XX_DEBUGSCR_METHOD_SCRIPT 0U
#define XX_DEBUGSCR_PLACEHOLDER_NAME "debug_image"

typedef struct xx_debugscr_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    bool is_folder;
} xx_debugscr_member;

typedef struct xx_debugscr_stream_s {
    xx_debugscr_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    /* The reconstruction itself. Building it IS the parse, so it is kept
     * rather than redone when the member is extracted. */
    uint8_t *image;
    size_t image_size;
} xx_debugscr_stream;

static void xx_debugscr_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_debugscr_read_at(Abstractformat *self, int64_t offset,
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

/* Refuse anything that would escape the extraction directory. */
static bool xx_debugscr_path_safe(const char *name) {
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

static void xx_debugscr_stream_free(void *pointer) {
    xx_debugscr_stream *stream = (xx_debugscr_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream->image);
    xx_mem_free(stream);
}

static bool xx_debugscr_add(xx_debugscr_stream *stream,
                            const xx_debugscr_member *member) {
    xx_debugscr_member *grown = (xx_debugscr_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool xx_debugscr_is_space(uint8_t c) {
    return c == ' ' || c == '\t';
}

static bool xx_debugscr_is_eol(uint8_t c) {
    return c == '\r' || c == '\n';
}

static int xx_debugscr_hex_digit(uint8_t c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

static uint8_t xx_debugscr_upper(uint8_t c) {
    return (c >= 'a' && c <= 'z') ? (uint8_t)(c - 'a' + 'A') : c;
}

/* Copy the DEBUG "N" operand into a name that is safe to create on disk.
 * DOS names are 8.3 ASCII; a drive letter or a directory prefix is dropped
 * rather than honoured, because the script's idea of where the file goes is
 * none of the extractor's business. */
static char *xx_debugscr_make_name(const uint8_t *text, size_t size) {
    char *name;
    size_t start = 0U;
    size_t output = 0U;
    size_t index;

    if (!text || size == 0U || size > XX_DEBUGSCR_MAX_NAME) return NULL;
    /* Keep only the last path component. */
    for (index = 0U; index < size; ++index) {
        if (text[index] == '/' || text[index] == '\\' || text[index] == ':') {
            start = index + 1U;
        }
    }
    if (start >= size) return NULL;
    name = (char *)xx_mem_alloc(size - start + 1U);
    if (!name) return NULL;
    for (index = start; index < size; ++index) {
        uint8_t c = text[index];
        if (c < 0x20U || c == 0x7fU || c == '"' || c == '*' || c == '<' ||
            c == '>' || c == '?' || c == '|') {
            xx_str_free(name);
            return NULL;
        }
        name[output++] = (char)c;
    }
    while (output != 0U && (name[output - 1U] == ' ' ||
                            name[output - 1U] == '.')) {
        --output;
    }
    name[output] = 0;
    if (output == 0U) {
        xx_str_free(name);
        return NULL;
    }
    return name;
}

/* ---------------------------------------------------------- the parser -- */

typedef struct xx_debugscr_scan_s {
    const uint8_t *text;
    size_t size;
    size_t at;
} xx_debugscr_scan;

/* Take the next line, blanks trimmed from both ends. Returns false at end of
 * text. Empty lines are returned as zero-length and skipped by the caller. */
static bool xx_debugscr_next_line(xx_debugscr_scan *scan, const uint8_t **line,
                                  size_t *length) {
    size_t start, end;

    if (scan->at >= scan->size) return false;
    start = scan->at;
    while (scan->at < scan->size && !xx_debugscr_is_eol(scan->text[scan->at])) {
        ++scan->at;
    }
    end = scan->at;
    /* Consume the terminator, CRLF counting as one. */
    if (scan->at < scan->size) {
        uint8_t c = scan->text[scan->at++];
        if (c == '\r' && scan->at < scan->size && scan->text[scan->at] == '\n') {
            ++scan->at;
        }
    }
    while (start < end && xx_debugscr_is_space(scan->text[start])) ++start;
    while (end > start && xx_debugscr_is_space(scan->text[end - 1U])) --end;
    *line = scan->text + start;
    *length = end - start;
    return true;
}

/* An "E" command: E<blank(s)><addr><blank(s)><hh> <hh> ... The address may
 * touch the E ("E0100"), which two samples do. Bytes stop at the first token
 * that is not a hex pair, which is how trailing comments are tolerated. */
static bool xx_debugscr_parse_enter(const uint8_t *line, size_t length,
                                    uint8_t *image, size_t image_size,
                                    uint32_t *lowest, uint64_t *entered) {
    size_t at = 1U;
    uint32_t address = 0U;
    unsigned digits = 0U;
    uint32_t cursor;

    while (at < length && xx_debugscr_is_space(line[at])) ++at;
    while (at < length && digits < 4U && xx_debugscr_hex_digit(line[at]) >= 0) {
        address = (address << 4U) | (uint32_t)xx_debugscr_hex_digit(line[at]);
        ++at;
        ++digits;
    }
    /* A segment-qualified address ("E DS:0100") is a different command shape
     * than any sample uses; refuse it rather than mis-read the offset. */
    if (digits == 0U || (at < length && !xx_debugscr_is_space(line[at]))) {
        return false;
    }
    if (address < XX_DEBUGSCR_LOAD_OFFSET) return false;
    if (address < *lowest) *lowest = address;
    cursor = address;
    for (;;) {
        int high, low;
        while (at < length && xx_debugscr_is_space(line[at])) ++at;
        if (at >= length) break;
        high = xx_debugscr_hex_digit(line[at]);
        if (high < 0 || at + 1U >= length) break;
        low = xx_debugscr_hex_digit(line[at + 1U]);
        if (low < 0) break;
        /* Three hex digits in a row is not a byte pair: stop, do not
         * silently re-align. */
        if (at + 2U < length && xx_debugscr_hex_digit(line[at + 2U]) >= 0) {
            break;
        }
        at += 2U;
        if (cursor >= 0x10000U) return false;
        if (cursor >= XX_DEBUGSCR_LOAD_OFFSET) {
            size_t position = (size_t)(cursor - XX_DEBUGSCR_LOAD_OFFSET);
            /* DEBUG would write only the first CX bytes, so a byte entered
             * past the end of the image is dropped, not an error. */
            if (position < image_size) {
                image[position] = (uint8_t)((high << 4) | low);
            }
        }
        ++cursor;
        ++*entered;
    }
    return true;
}

/* Does this line start the named single-word command? */
static bool xx_debugscr_is_word(const uint8_t *line, size_t length,
                                const char *word) {
    size_t index = 0U;

    while (word[index]) {
        if (index >= length ||
            xx_debugscr_upper(line[index]) != (uint8_t)word[index]) {
            return false;
        }
        ++index;
    }
    return index == length;
}

/* "RCX" and "R CX" both select CX; the value is on the following line. */
static bool xx_debugscr_is_rcx(const uint8_t *line, size_t length) {
    return xx_debugscr_is_word(line, length, "RCX") ||
           xx_debugscr_is_word(line, length, "R CX");
}

static xx_debugscr_stream *xx_debugscr_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_debugscr_stream *stream = NULL;
    xx_debugscr_member member;
    xx_debugscr_scan scan;
    uint8_t *text = NULL;
    uint8_t *image = NULL;
    char *name = NULL;
    const uint8_t *line;
    size_t length;
    int64_t total, span;
    uint32_t lowest = 0x10000U;
    uint64_t entered = 0U;
    uint32_t count = 0U;
    size_t image_size;
    bool have_name = false;
    bool have_count = false;
    bool want_count = false;
    bool have_write = false;
    bool have_quit = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* "N x" + one E line + RCX/value/W/Q cannot fit in less than this. */
    if (span < 24 || span > XX_DEBUGSCR_MAX_TEXT) return NULL;

    text = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!text) return NULL;
    if (!xx_debugscr_read_at(self, self->base_address, text, (size_t)span)) {
        goto fail;
    }

    scan.text = text;
    scan.size = (size_t)span;
    scan.at = 0U;

    /* Pass one: the name and CX, so the image buffer can be sized before a
     * single byte is entered into it. A header that could ask for a large
     * allocation from a small file is exactly what this ordering prevents:
     * CX is bounded by the segment, not by anything in the file. */
    while (xx_debugscr_next_line(&scan, &line, &length)) {
        if (length == 0U) continue;
        if (want_count) {
            unsigned digits = 0U;
            size_t at = 0U;
            count = 0U;
            while (at < length && digits < 4U &&
                   xx_debugscr_hex_digit(line[at]) >= 0) {
                count = (count << 4U) |
                        (uint32_t)xx_debugscr_hex_digit(line[at]);
                ++at;
                ++digits;
            }
            want_count = false;
            if (digits != 0U && at == length) have_count = true;
            continue;
        }
        if (!have_name) {
            /* The first command must be N: this is the whole of the format's
             * identity, so a script that opens with anything else is not one
             * of these. */
            size_t at = 1U;
            if (xx_debugscr_upper(line[0]) != 'N' || length < 3U ||
                !xx_debugscr_is_space(line[1])) {
                goto fail;
            }
            while (at < length && xx_debugscr_is_space(line[at])) ++at;
            name = xx_debugscr_make_name(line + at, length - at);
            if (!name) goto fail;
            have_name = true;
            continue;
        }
        if (xx_debugscr_is_rcx(line, length)) {
            want_count = true;
            continue;
        }
        if (xx_debugscr_is_word(line, length, "W")) {
            have_write = true;
            continue;
        }
        if (xx_debugscr_is_word(line, length, "Q")) {
            have_quit = true;
            break;
        }
    }
    if (!have_name || !have_count || !have_write || !have_quit) goto fail;
    if (count == 0U || count > XX_DEBUGSCR_MAX_IMAGE) goto fail;

    image_size = (size_t)count;
    image = (uint8_t *)xx_mem_alloc(image_size);
    if (!image) goto fail;
    /* DEBUG writes out of a buffer it never cleared, but in practice that
     * buffer is the freshly loaded program segment; zero is the only
     * defensible fill and is what every extractor uses. */
    xx_mem_zero(image, image_size);

    /* Pass two: the bytes. The RCX value line has to be skipped the same way
     * it was in pass one, because a count such as "EE" is indistinguishable
     * from a malformed E command when read out of context. */
    scan.at = 0U;
    want_count = false;
    while (xx_debugscr_next_line(&scan, &line, &length)) {
        if (length == 0U) continue;
        if (want_count) {
            want_count = false;
            continue;
        }
        if (xx_debugscr_is_rcx(line, length)) {
            want_count = true;
            continue;
        }
        if (xx_debugscr_is_word(line, length, "Q")) break;
        if (xx_debugscr_upper(line[0]) != 'E') continue;
        /* An E command separates its opcode from its address with a blank
         * or joins the two directly ("E0100"); anything else is some other
         * command that merely starts with the letter. */
        if (length < 2U ||
            (!xx_debugscr_is_space(line[1]) &&
             xx_debugscr_hex_digit(line[1]) < 0)) {
            continue;
        }
        if (!xx_debugscr_parse_enter(line, length, image, image_size, &lowest,
                                     &entered)) {
            goto fail;
        }
        if (pd && xx_pd_is_stopped(pd)) goto fail;
    }
    /* A DEBUG dictation of a .COM image always begins at the load offset.
     * Requiring it keeps this reader off unrelated text that happens to open
     * with an N line. */
    if (lowest != XX_DEBUGSCR_LOAD_OFFSET) goto fail;
    if (entered < XX_DEBUGSCR_MIN_ENTERED) goto fail;

    stream = (xx_debugscr_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_mem_zero(stream, sizeof(*stream));
    stream->image = image;
    stream->image_size = image_size;
    image = NULL;

    if (!xx_debugscr_path_safe(name)) {
        xx_str_free(name);
        name = xx_str_dup(XX_DEBUGSCR_PLACEHOLDER_NAME);
        if (!name) goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = 0;
    member.data_offset = self->base_address;
    member.compressed_size = span;
    member.uncompressed_size = (int64_t)stream->image_size;
    member.method = XX_DEBUGSCR_METHOD_SCRIPT;
    member.is_folder = false;
    if (!xx_debugscr_add(stream, &member)) goto fail;
    name = NULL;
    stream->archive_size = span;
    xx_mem_free(text);
    return stream;

fail:
    xx_str_free(name);
    xx_mem_free(image);
    xx_mem_free(text);
    xx_debugscr_stream_free(stream);
    return NULL;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_debugscr_init(xx_debugscr *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_DEBUGSCR_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-debug-script");
    xx_format_set_extension(&archive->format, "scr");
    archive->format.check_is_valid = xx_debugscr_check_is_valid;
    archive->format.handle_base_info = xx_debugscr_handle_base_info;
    archive->format.get_format_size = xx_debugscr_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_debugscr_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_debugscr_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_debugscr_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_debugscr_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_debugscr_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_debugscr_free_archive_records_reading;
    archive->format.destroy = xx_debugscr_vtable_destroy;
}

xx_debugscr *xx_debugscr_create(xx_io_device *device, int64_t base_address) {
    xx_debugscr *archive = (xx_debugscr *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_debugscr_init(archive, device, base_address);
    return archive;
}

void xx_debugscr_destroy(xx_debugscr *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_debugscr_free(xx_debugscr *archive) {
    if (!archive) return;
    xx_debugscr_destroy(archive);
    xx_mem_free(archive);
}

static void xx_debugscr_vtable_destroy(Abstractformat *self) {
    xx_debugscr_destroy((xx_debugscr *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_debugscr_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_debugscr_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_debugscr_parse(self, pd);
    if (!stream) return false;
    xx_debugscr_stream_free(stream);
    return true;
}

bool xx_debugscr_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_debugscr *archive = (xx_debugscr *)self;
    xx_debugscr_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_debugscr_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_debugscr_stream_free(stream);
    return true;
}

int64_t xx_debugscr_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_debugscr_get_number_of_archive_records(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_debugscr *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_debugscr_set_record(xx_archive_record *record,
                                   const xx_debugscr_member *member) {
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
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_debugscr_copy_options(xx_list_s *target,
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

static const xx_var *xx_debugscr_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_debugscr_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_debugscr_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_debugscr_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_debugscr_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_debugscr_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_debugscr_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_debugscr_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_debugscr_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_debugscr_archive_record_move_to_next(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_debugscr_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_debugscr_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_debugscr_set_record(&state->current_record,
                                               &stream->items[stream->index]);
    return state->has_record;
}

bool xx_debugscr_unpack_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    xx_debugscr_stream *stream;
    const xx_debugscr_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_debugscr_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_debugscr_path_safe(member->name)) return false;
    if (!stream->image ||
        stream->image_size != (size_t)member->uncompressed_size) {
        return false;
    }

    path_option = xx_debugscr_get_option(&state->options,
                                         XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: the reconstruction already exists, so there is
         * nothing left to verify and nothing to write. */
        return true;
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

    if (!xx_store_create_dirs_a(target_path, false)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < stream->image_size) {
            ssize_t sent = xx_io_write(output, stream->image + completed,
                                       stream->image_size - completed);
            if (sent <= 0 || (size_t)sent > stream->image_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_debugscr_free_archive_records_reading(Abstractformat *self,
                                              xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
