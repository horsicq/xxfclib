/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the NoteTab (Fookes Software) Clipbook Library /
 * Outline document.  Layout ported from XArchive's documents/xnotetab.cpp.
 *
 * The document is plain text.  Line 1 is the declaration: "= V4 " or
 * "= V5 " followed by keywords ("Outline", "MultiLine", "NoSorting",
 * "TabWidth", "AutoReplace").  Line 2 is reserved and never inspected.  The
 * body is a run of clips; each clip is opened by a heading line and runs up
 * to the first byte of the next heading line or to end of file.  Nothing is
 * compressed - every clip is stored - and the extracted member is the
 * heading, a blank line, then the body.
 *
 * TWO HEADING DIALECTS exist and they are decided PER DOCUMENT, never per
 * line:
 *
 *   quoted     H="name"     the V4/V5 dialect the tool writes today
 *   unquoted   H=name       the older dialect, e.g. NoteTab's own
 *                           CLIPHELP.CLH, where the heading runs to the end
 *                           of the line
 *
 * The distinction matters because a clip BODY may legitimately contain a
 * line that starts with H= - CLIPHELP.CLH stores exactly such lines - so
 * treating a bare H= as a heading everywhere would split quoted documents in
 * the wrong places.  notetab_pick_dialect() therefore scans the whole body
 * once: if any line opens with H=" the document is quoted and a bare H= is
 * ordinary text, otherwise a bare H= opens a clip.  A document that has
 * neither is not a clipbook and is refused.
 *
 * A PREAMBLE - comment or blank lines between the reserved line and the
 * first heading - is part of no clip.  Real libraries carry one (PPWizard's
 * ppwizard.clb opens with 33 lines of ';' comments), so those lines are
 * folded into the header extent instead of failing the document; only the
 * run of clips that follows is published.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/notetab/xx_notetab.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef NOTETAB
#define XX_NOTETAB_FILE_TYPE XX_FILE_TYPE_NOTETAB
#else
#define XX_NOTETAB_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Declaration tag: "= V" + one of two version digits + a space. */
#define NOTETAB_TAG_SIZE 5U
#define NOTETAB_HEADING_TAG "H=\""
#define NOTETAB_HEADING_TAG_SIZE 3U
/* The older dialect opens a clip with a bare H= and runs the heading to the
 * end of the line. */
#define NOTETAB_HEADING_TAG_PLAIN "H="
#define NOTETAB_HEADING_TAG_PLAIN_SIZE 2U
/* The original tool truncates the stored heading at 0x100 bytes. */
#define NOTETAB_MAX_NAME 0x100
#define NOTETAB_DECLARATION_LIMIT 4096
#define NOTETAB_MAX_CLIPS 500000U
/* A NoteTab document is text; anything beyond this is not one, and the
 * whole-file parse must not be talked into a huge allocation. */
#define NOTETAB_MAX_INPUT (64 * 1024 * 1024)

typedef struct notetab_clip_s {
    char *name;         /* sanitised heading + ".txt" */
    uint8_t *prefix;    /* heading + CRLF CRLF, prepended on extraction */
    size_t prefix_size;
    int64_t body_offset;
    int64_t body_size;
} notetab_clip;

typedef struct notetab_stream_s {
    notetab_clip *items;
    size_t count;
    size_t index;
    int64_t input_size;
    int64_t header_size;
} notetab_stream;

static bool notetab_read_at(xx_io_device *device, int64_t offset,
                            void *buffer, size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool notetab_is_version_digit(uint8_t digit) {
    return digit == (uint8_t)'4' || digit == (uint8_t)'5';
}

static bool notetab_contains(const uint8_t *data, size_t size,
                             const char *needle) {
    size_t needle_size = xx_rt_strlen(needle);
    size_t index;
    if (needle_size == 0U || size < needle_size) return false;
    for (index = 0U; index + needle_size <= size; ++index)
        if (xx_rt_memcmp(data + index, needle, needle_size) == 0) return true;
    return false;
}

/* The declaration line alone ("= V4 ") is far too weak a signature, so the
 * keyword gate of the reference reader is reproduced exactly. */
static bool notetab_check_declaration(const uint8_t *data, int64_t size) {
    int64_t probe, line_end, at;
    static const char *const keywords[] = {"Outline", "MultiLine",
                                           "NoSorting", "TabWidth",
                                           "AutoReplace"};
    size_t index;
    if (!data || size < (int64_t)NOTETAB_TAG_SIZE) return false;
    if (xx_rt_memcmp(data, "= V", 3U) != 0) return false;
    if (!notetab_is_version_digit(data[3])) return false;
    if (data[4] != (uint8_t)' ') return false;
    probe = size < NOTETAB_DECLARATION_LIMIT ? size
                                             : NOTETAB_DECLARATION_LIMIT;
    line_end = probe;
    for (at = 0; at < probe; ++at) {
        if (data[at] == (uint8_t)'\r' || data[at] == (uint8_t)'\n') {
            line_end = at;
            break;
        }
    }
    for (index = 0U; index < sizeof(keywords) / sizeof(keywords[0]); ++index)
        if (notetab_contains(data, (size_t)line_end, keywords[index]))
            return true;
    return false;
}

/* Returns false at end of input.  A CRLF or LFCR pair counts as one
 * terminator; a doubled CR or a doubled LF does not. */
static bool notetab_read_line(const uint8_t *data, int64_t size,
                              int64_t *position, int64_t *line_offset,
                              int64_t *line_size) {
    int64_t start = *position;
    int64_t current;
    *line_offset = start;
    *line_size = 0;
    if (start < 0 || start >= size) return false;
    current = start;
    while (current < size) {
        uint8_t c = data[current];
        if (c == (uint8_t)'\r' || c == (uint8_t)'\n') {
            ++current;
            if (current < size) {
                uint8_t next = data[current];
                if ((c == (uint8_t)'\r' && next == (uint8_t)'\n') ||
                    (c == (uint8_t)'\n' && next == (uint8_t)'\r'))
                    ++current;
            }
            break;
        }
        ++current;
    }
    *position = current;
    *line_size = current - start;
    return true;
}

/* @p tag_size is 3 for the quoted dialect and 2 for the unquoted one; the
 * caller has already decided which this document uses. */
static bool notetab_is_heading(const uint8_t *data, int64_t size,
                               int64_t line_offset, int64_t line_size,
                               size_t tag_size) {
    const char *tag = (tag_size == NOTETAB_HEADING_TAG_PLAIN_SIZE)
                          ? NOTETAB_HEADING_TAG_PLAIN
                          : NOTETAB_HEADING_TAG;
    if (line_size < (int64_t)tag_size) return false;
    if (line_offset < 0 || line_offset > size - (int64_t)tag_size) return false;
    return xx_rt_memcmp(data + line_offset, tag, tag_size) == 0;
}

/* Elect the dialect once for the whole document: a single H=" anywhere in
 * the body makes it the quoted dialect, and a bare H= is then ordinary body
 * text rather than a heading.  Deciding per line instead would split quoted
 * documents whose clips store H= lines.  Returns 0 when the body opens no
 * clip at all in either dialect, which is not a clipbook. */
static size_t notetab_pick_dialect(const uint8_t *data, int64_t size,
                                   int64_t body_start) {
    int64_t position = body_start;
    int64_t line_offset, line_size;
    bool saw_plain = false;
    while (notetab_read_line(data, size, &position, &line_offset,
                             &line_size)) {
        if (notetab_is_heading(data, size, line_offset, line_size,
                               NOTETAB_HEADING_TAG_SIZE))
            return NOTETAB_HEADING_TAG_SIZE;
        if (!saw_plain &&
            notetab_is_heading(data, size, line_offset, line_size,
                               NOTETAB_HEADING_TAG_PLAIN_SIZE))
            saw_plain = true;
    }
    return saw_plain ? NOTETAB_HEADING_TAG_PLAIN_SIZE : 0U;
}

/* Clip headings are free text and the corpus carries ':', '/', '?', '*' and
 * tabs inside them.  Folding the reserved set to '_' reproduces the output
 * names of the reference tool and keeps the member from being read as a
 * path. */
static char *notetab_build_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t index, output = 0U;
    if (size > SIZE_MAX - 8U) return NULL;
    name = (char *)xx_mem_alloc(size + 8U);
    if (!name) return NULL;
    for (index = 0U; index < size; ++index) {
        uint8_t c = bytes[index];
        bool reserved = c < 0x20U || c == (uint8_t)'<' || c == (uint8_t)'>' ||
                        c == (uint8_t)':' || c == (uint8_t)'"' ||
                        c == (uint8_t)'/' || c == (uint8_t)'\\' ||
                        c == (uint8_t)'|' || c == (uint8_t)'?' ||
                        c == (uint8_t)'*';
        name[output++] = reserved ? '_' : (char)c;
    }
    while (output != 0U && (name[output - 1U] == ' ')) --output;
    if (output == 0U) name[output++] = '_';
    name[output++] = '.';
    name[output++] = 't';
    name[output++] = 'x';
    name[output++] = 't';
    name[output] = 0;
    return name;
}

static bool notetab_safe_output_name(const char *name) {
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    for (at = name; *at; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '/' || c == '\\' || c < 0x20U)
            return false;
    }
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2])))
        return false;
    return true;
}

static void notetab_stream_free(void *opaque) {
    notetab_stream *stream = (notetab_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
        if (stream->items[index].prefix)
            xx_mem_free(stream->items[index].prefix);
    }
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool notetab_add_clip(notetab_stream *stream,
                             const notetab_clip *clip) {
    notetab_clip *grown;
    if (!stream || !clip || stream->count >= NOTETAB_MAX_CLIPS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (notetab_clip *)xx_mem_realloc(stream->items,
                                           (stream->count + 1U) *
                                               sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *clip;
    return true;
}

static bool notetab_emit(notetab_stream *stream, const uint8_t *data,
                         int64_t name_offset, int64_t name_size,
                         int64_t body_offset, int64_t body_end) {
    notetab_clip clip;
    uint8_t *prefix;
    size_t prefix_size;
    if (name_size < 0 || body_end < body_offset) return false;
    xx_mem_zero(&clip, sizeof(clip));
    clip.body_offset = body_offset;
    clip.body_size = body_end - body_offset;
    prefix_size = (size_t)name_size + 4U;
    prefix = (uint8_t *)xx_mem_alloc(prefix_size);
    if (!prefix) return false;
    if (name_size != 0)
        xx_rt_memcpy(prefix, data + name_offset, (size_t)name_size);
    prefix[name_size] = (uint8_t)'\r';
    prefix[name_size + 1] = (uint8_t)'\n';
    prefix[name_size + 2] = (uint8_t)'\r';
    prefix[name_size + 3] = (uint8_t)'\n';
    clip.prefix = prefix;
    clip.prefix_size = prefix_size;
    clip.name = notetab_build_name(data + name_offset, (size_t)name_size);
    if (!clip.name) {
        xx_mem_free(prefix);
        return false;
    }
    if (!notetab_add_clip(stream, &clip)) {
        xx_mem_free(prefix);
        xx_str_free(clip.name);
        return false;
    }
    return true;
}

static bool notetab_parse(Abstractformat *format, notetab_stream **result) {
    notetab_stream *stream = NULL;
    uint8_t *data = NULL;
    int64_t total, size, position, line_offset, line_size;
    int64_t name_offset = -1, name_size = 0, body_offset = -1;
    int64_t body_start;
    size_t tag_size;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)NOTETAB_TAG_SIZE || size > NOTETAB_MAX_INPUT)
        return false;
    data = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!data) return false;
    if (!notetab_read_at(format->device, format->base_address, data,
                         (size_t)size) ||
        !notetab_check_declaration(data, size)) {
        xx_mem_free(data);
        return false;
    }
    stream = (notetab_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) {
        xx_mem_free(data);
        return false;
    }
    stream->input_size = size;
    position = 0;
    /* Line 1 is the declaration, line 2 is skipped unconditionally. */
    if (!notetab_read_line(data, size, &position, &line_offset, &line_size))
        goto fail;
    if (!notetab_read_line(data, size, &position, &line_offset, &line_size))
        goto fail;
    body_start = position;
    /* One decision for the whole document, taken before a single clip is
     * cut; see the dialect note at the top of this file. */
    tag_size = notetab_pick_dialect(data, size, body_start);
    if (tag_size == 0U) goto fail;
    /* Everything between the reserved line and the first heading belongs to
     * no clip: it is the library's preamble (';' comments and blank lines),
     * and it is counted as header rather than made a parse error. */
    position = body_start;
    while (notetab_read_line(data, size, &position, &line_offset,
                             &line_size)) {
        if (notetab_is_heading(data, size, line_offset, line_size, tag_size))
            break;
        body_start = position;
    }
    stream->header_size = body_start;
    position = body_start;
    while (notetab_read_line(data, size, &position, &line_offset,
                             &line_size)) {
        bool heading =
            notetab_is_heading(data, size, line_offset, line_size, tag_size);
        if (heading) {
            /* A heading closes the clip that was open; that clip's body ends
             * at the first byte of this line. */
            if (name_offset >= 0 && body_offset >= 0 &&
                !notetab_emit(stream, data, name_offset, name_size,
                              body_offset, line_offset)) goto fail;
            name_offset = -1;
            body_offset = -1;
        }
        if (name_offset < 0) {
            int64_t available, probe, at;
            /* Past the preamble, only a heading line may sit outside a clip:
             * the dialect has been fixed, so anything else is a document
             * this grammar does not describe. */
            if (!heading) goto fail;
            available = line_size - (int64_t)tag_size;
            probe = available < NOTETAB_MAX_NAME + 1 ? available
                                                     : NOTETAB_MAX_NAME + 1;
            name_offset = line_offset + (int64_t)tag_size;
            name_size = -1;
            if (tag_size == NOTETAB_HEADING_TAG_SIZE) {
                for (at = 0; at < probe; ++at) {
                    if (data[name_offset + at] == (uint8_t)'"') {
                        name_size = at;
                        break;
                    }
                }
            }
            /* No closing quote inside the probe window - always the case in
             * the unquoted dialect - so the heading runs to the end of the
             * line.  The line terminator is not part of the name: the quoted
             * dialect never reaches here with one attached (the closing quote
             * stops the scan first), and letting it through would put a CR LF
             * into every unquoted clip's file name. */
            if (name_size < 0) {
                name_size = available;
                while (name_size > 0 &&
                       (data[name_offset + name_size - 1] == (uint8_t)'\r' ||
                        data[name_offset + name_size - 1] == (uint8_t)'\n'))
                    --name_size;
                if (name_size > NOTETAB_MAX_NAME) name_size = NOTETAB_MAX_NAME;
            }
            if (name_size > NOTETAB_MAX_NAME) name_size = NOTETAB_MAX_NAME;
            if (name_size < 0) name_size = 0;
        } else if (body_offset < 0) {
            body_offset = line_offset;
        }
    }
    /* The document ends without a closing heading: the final clip's body
     * runs to end of file. */
    if (name_offset >= 0 && body_offset >= 0 &&
        !notetab_emit(stream, data, name_offset, name_size, body_offset,
                      size)) goto fail;
    if (stream->count == 0U) goto fail;
    xx_mem_free(data);
    *result = stream;
    return true;
fail:
    xx_mem_free(data);
    notetab_stream_free(stream);
    return false;
}

static bool notetab_copy_options(xx_list_s *destination,
                                 const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *notetab_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool notetab_set_record(xx_archive_record *record,
                               const notetab_clip *clip) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = clip->body_offset;
    record->header_size = 0;
    record->data_offset = clip->body_offset;
    record->compressed_size = clip->body_size;
    return xx_archive_record_set_original_name(record, clip->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)clip->body_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)clip->body_size +
                                              clip->prefix_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

void xx_notetab_init(xx_notetab *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_NOTETAB_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-notetab");
    xx_format_set_extension(&archive->format, "clb");
    archive->format.check_is_valid = xx_notetab_check_is_valid;
    archive->format.handle_base_info = xx_notetab_handle_base_info;
    archive->format.get_format_size = xx_notetab_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_notetab_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_notetab_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_notetab_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_notetab_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_notetab_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_notetab_free_archive_records_reading;
}

xx_notetab *xx_notetab_create(xx_io_device *device, int64_t base_address) {
    xx_notetab *archive = (xx_notetab *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_notetab_init(archive, device, base_address);
    return archive;
}

void xx_notetab_destroy(xx_notetab *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_notetab_free(xx_notetab *archive) {
    if (!archive) return;
    xx_notetab_destroy(archive);
    xx_mem_free(archive);
}

bool xx_notetab_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    notetab_stream *stream;
    (void)pd;
    if (!notetab_parse(format, &stream)) return false;
    notetab_stream_free(stream);
    return true;
}

bool xx_notetab_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    notetab_stream *stream;
    xx_notetab *archive;
    (void)pd;
    if (!format || !notetab_parse(format, &stream)) return false;
    archive = (xx_notetab *)format;
    archive->number_of_records = stream->count;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->input_size;
    format->is_valid = true;
    format->base_info_handled = true;
    notetab_stream_free(stream);
    return true;
}

int64_t xx_notetab_get_format_size(Abstractformat *format,
                                   xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_notetab_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_notetab_get_number_of_archive_records(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_notetab_handle_base_info(format, pd))
               ? ((xx_notetab *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_notetab_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    notetab_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!notetab_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        notetab_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = notetab_stream_free;
    state->total_records = stream->count;
    if (!notetab_copy_options(&state->options, options) ||
        !notetab_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_notetab_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_notetab_archive_record_move_to_next(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    notetab_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (notetab_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = notetab_set_record(&state->current_record,
                                           &stream->items[stream->index]);
    return state->has_record;
}

bool xx_notetab_unpack_current_archive_record(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    notetab_stream *stream;
    notetab_clip *clip;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *body = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (notetab_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    clip = &stream->items[stream->index];
    if (!notetab_safe_output_name(clip->name) || clip->body_size < 0 ||
        (uint64_t)clip->body_size > (uint64_t)SIZE_MAX) return false;
    if (clip->body_size != 0) {
        body = (uint8_t *)xx_mem_alloc((size_t)clip->body_size);
        if (!body ||
            !notetab_read_at(format->device,
                             format->base_address + clip->body_offset, body,
                             (size_t)clip->body_size)) goto done;
    }
    path_option = notetab_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", clip->name)
               : xx_str_concat(base, clip->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        size_t written = 0U;
        if (!destination) goto done;
        result = true;
        while (written < clip->prefix_size) {
            ssize_t amount = xx_io_write(destination, clip->prefix + written,
                                         clip->prefix_size - written);
            if (amount <= 0 || (size_t)amount > clip->prefix_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        written = 0U;
        while (result && written < (size_t)clip->body_size) {
            ssize_t amount = xx_io_write(destination, body + written,
                                         (size_t)clip->body_size - written);
            if (amount <= 0 ||
                (size_t)amount > (size_t)clip->body_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (body) xx_mem_free(body);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_notetab_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
