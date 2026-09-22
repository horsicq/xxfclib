/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * The "boo" printable transport encoding: a name line followed by six-bit
 * characters packed four-to-three, with '~' escaping a run of NUL bytes.
 * Ported from XArchive's archives/xboo.cpp; the shape constraints that keep
 * plain text from matching come from U3's own recognition predicate.
 * xx_boo.h documents the transform and the evidence.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/boo/xx_boo.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as BOO is registered there. */
#ifdef BOO
#define XX_BOO_FILE_TYPE XX_FILE_TYPE_BOO
#else
#define XX_BOO_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define BOO_LF 0x0aU
#define BOO_CR 0x0dU
#define BOO_MIN_PROBE_LINES 2
#define BOO_MIN_PROBE_CHARS 128
#define BOO_MIN_PROBE_LINE_WIDTH 32
/* MAKEBOO pads the last group with NULs; at most two bytes of the tail are
 * therefore padding.  Never strip more: real payloads end in genuine NULs. */
#define BOO_MAX_TRAILING_PAD 2

typedef struct boo_stream_s {
    char name[XX_BOO_MAX_NAME_SIZE + 1];
    uint8_t *source;    /**< Whole encoded file, owned. */
    size_t source_size;
    size_t body_offset; /**< First payload byte, relative to base_address. */
    int64_t unpacked_size;
    bool consumed;
} boo_stream;

/* ------------------------------------------------------------- helpers -- */

static bool boo_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static void boo_stream_free(void *opaque) {
    boo_stream *stream = (boo_stream *)opaque;
    if (!stream) return;
    if (stream->source) xx_mem_free(stream->source);
    xx_mem_free(stream);
}

static bool boo_is_payload_char(uint8_t value) {
    return value >= XX_BOO_ALPHABET_FIRST && value <= XX_BOO_ALPHABET_LAST;
}

/* The name reaches the extractor as an output file name, so path traversal
 * has to die here.  '.' is not in the character set, which is what rules out
 * "..", "./" and every other traversal shape. */
static bool boo_is_name_char(uint8_t value, bool stem) {
    if (value >= 'A' && value <= 'Z') return true;
    if (value >= 'a' && value <= 'z') return true;
    if (value >= '0' && value <= '9') return true;
    switch (value) {
        case '_':
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case '(':
        case ')':
        case '-':
        case '@':
        case '^':
        case '{':
        case '}':
            return true;
        case '\'':
        case '`':
        case '~':
            /* Tolerated in the 8.3 stem only.  Keeping them out of the
             * extension is what stops ordinary prose - an apostrophe, a
             * backquoted word - from passing as a name line. */
            return stem;
        default:
            return false;
    }
}

static bool boo_is_valid_name(const char *name, size_t size) {
    size_t dot = size;
    size_t index;

    if (size < 1U || size > XX_BOO_MAX_NAME_SIZE) return false;
    for (index = 0U; index < size; ++index) {
        if (name[index] == '.') {
            dot = index;
            break;
        }
    }
    {
        size_t stem = (dot == size) ? size : dot;
        if (stem < 1U || stem > 8U) return false;
        for (index = 0U; index < stem; ++index) {
            if (!boo_is_name_char((uint8_t)name[index], true)) return false;
        }
    }
    if (dot != size) {
        size_t extension = size - dot - 1U;
        if (extension < 1U || extension > 3U) return false;
        for (index = dot + 1U; index < size; ++index) {
            if (!boo_is_name_char((uint8_t)name[index], false)) return false;
        }
    }
    return true;
}

/* --------------------------------------------------------- name + gate -- */

static bool boo_parse_name_line(const uint8_t *source, size_t size,
                                char *name_out, size_t *body_offset) {
    size_t limit = (size < (size_t)XX_BOO_MAX_NAME_LINE)
                       ? size
                       : (size_t)XX_BOO_MAX_NAME_LINE;
    size_t line_end = limit;
    size_t index;
    size_t length;

    for (index = 0U; index < limit; ++index) {
        if (source[index] == BOO_LF) {
            line_end = index;
            break;
        }
    }
    /* An empty or unterminated first line carries no name. */
    if (line_end == limit || line_end < 1U) return false;
    length = line_end;
    if (source[length - 1U] == BOO_CR) --length;
    /* U3 requires at least three characters in the name line. */
    if (length < 3U || length > (size_t)XX_BOO_MAX_NAME_SIZE) return false;
    for (index = 0U; index < length; ++index)
        name_out[index] = (char)source[index];
    name_out[length] = 0;
    if (!boo_is_valid_name(name_out, length)) return false;
    *body_offset = line_end + 1U;
    return true;
}

/* Cheap bounded shape test.  It runs on every candidate the dispatcher
 * reaches, so its cost must not scale with the file. */
static bool boo_probe_gate(const uint8_t *source, size_t size,
                           size_t body_offset) {
    size_t body_size, window, last_line_end, line_start, index;
    size_t complete_lines = 0U, total_characters = 0U, max_line_width = 0U;
    const uint8_t *body;
    bool have_last = false;

    if (body_offset > size) return false;
    body_size = size - body_offset;
    if (body_size < (size_t)XX_BOO_MIN_BODY_SIZE) return false;
    body = source + body_offset;
    window = (body_size < (size_t)XX_BOO_PROBE_WINDOW)
                 ? body_size
                 : (size_t)XX_BOO_PROBE_WINDOW;

    /* The window can cut a line in half; a truncated fragment is evidence of
     * nothing, so everything after the last line feed is dropped. */
    last_line_end = 0U;
    for (index = window; index > 0U; --index) {
        if (body[index - 1U] == BOO_LF) {
            last_line_end = index - 1U;
            have_last = true;
            break;
        }
    }
    if (!have_last) return false;

    line_start = 0U;
    for (index = 0U; index <= last_line_end; ++index) {
        size_t line_end, line_size, start, position, characters = 0U;
        if (body[index] != BOO_LF) continue;
        line_end = index;
        if (line_end > line_start && body[line_end - 1U] == BOO_CR) --line_end;
        line_size = line_end - line_start;
        start = line_start;
        line_start = index + 1U;
        if (line_size == 0U) continue;

        position = 0U;
        while (position < line_size) {
            uint8_t value = body[start + position];
            if (value == XX_BOO_ESCAPE) {
                uint8_t count;
                /* The count byte always sits on the same line. */
                if (position + 1U >= line_size) return false;
                count = body[start + position + 1U];
                if (count < XX_BOO_ALPHABET_FIRST ||
                    count > XX_BOO_ALPHABET_FIRST + XX_BOO_MAX_ESCAPE_COUNT)
                    return false;
                position += 2U;
                continue;
            }
            if (!boo_is_payload_char(value)) return false;
            ++characters;
            ++position;
        }
        /* Every producer wraps on a whole-group boundary. */
        if ((characters % 4U) != 0U) return false;
        ++complete_lines;
        total_characters += characters;
        if (line_size > max_line_width) max_line_width = line_size;
    }
    return complete_lines >= (size_t)BOO_MIN_PROBE_LINES &&
           total_characters >= (size_t)BOO_MIN_PROBE_CHARS &&
           max_line_width >= (size_t)BOO_MIN_PROBE_LINE_WIDTH;
}

/* -------------------------------------------------------------- decode -- */

/* One decoding pass.  @p output may be NULL, in which case only the length is
 * produced; @p capacity bounds the result in both modes so that the escape
 * expansion cannot drive an unbounded allocation. */
static bool boo_decode(const uint8_t *body, size_t body_size, uint8_t *output,
                       size_t capacity, size_t *written) {
    uint8_t group[4];
    size_t group_count = 0U;
    size_t position = 0U;
    size_t index;
    bool escape_pending = false;
    /* The last two bytes produced, tracked so that the measuring pass (which
     * has no output buffer) can strip the same trailing padding the
     * producing pass does.  Without this the two passes would disagree by up
     * to two bytes and the published uncompressed size would be wrong. */
    uint8_t tail[2] = {0xffU, 0xffU};

    for (index = 0U; index < body_size; ++index) {
        uint8_t value = body[index];

        if (value == BOO_LF || value == BOO_CR) {
            /* A pending escape at a line break is a desynchronised stream,
             * not a run that continues onto the next line. */
            if (escape_pending) return false;
            continue;
        }
        if (escape_pending) {
            size_t count;
            if (value < XX_BOO_ALPHABET_FIRST ||
                value > XX_BOO_ALPHABET_FIRST + XX_BOO_MAX_ESCAPE_COUNT)
                return false;
            count = (size_t)(value - XX_BOO_ALPHABET_FIRST);
            if (count > capacity - position) return false;
            if (output) {
                size_t fill;
                for (fill = 0U; fill < count; ++fill)
                    output[position + fill] = 0U;
            }
            if (count >= 2U) {
                tail[0] = 0U;
                tail[1] = 0U;
            } else if (count == 1U) {
                tail[0] = tail[1];
                tail[1] = 0U;
            }
            position += count;
            escape_pending = false;
            continue;
        }
        if (value == XX_BOO_ESCAPE) {
            escape_pending = true;
            continue;
        }
        if (!boo_is_payload_char(value)) return false;
        group[group_count++] = (uint8_t)(value - XX_BOO_ALPHABET_FIRST);
        if (group_count < 4U) continue;
        group_count = 0U;
        if (capacity - position < 3U) return false;
        {
            uint8_t b0 = (uint8_t)(((group[0] << 2) + (group[1] >> 4)) & 0xffU);
            uint8_t b1 = (uint8_t)(((group[1] << 4) + (group[2] >> 2)) & 0xffU);
            uint8_t b2 = (uint8_t)(((group[2] << 6) + group[3]) & 0xffU);
            if (output) {
                output[position] = b0;
                output[position + 1U] = b1;
                output[position + 2U] = b2;
            }
            tail[0] = b1;
            tail[1] = b2;
        }
        position += 3U;
    }

    /* A truncated escape or a partial group means the transport was cut
     * short; publishing what decoded so far would be a silently truncated
     * file. */
    if (escape_pending || group_count != 0U || position == 0U) return false;

    /* Remove at most two padding NULs from the final group.  The tracked tail
     * is used rather than the buffer, so the measuring pass and the producing
     * pass trim identically and report the same length. */
    {
        int trim;
        for (trim = 0; trim < BOO_MAX_TRAILING_PAD && position > 0U; ++trim) {
            if (tail[1] != 0U) break;
            --position;
            tail[1] = tail[0];
            tail[0] = 0xffU;
        }
    }
    if (position == 0U) return false;
    *written = position;
    return true;
}

/* --------------------------------------------------------------- parse -- */

static bool boo_parse(Abstractformat *format, boo_stream **result,
                      xx_pd_struct *pd) {
    boo_stream *stream = NULL;
    int64_t total, span;
    size_t measured = 0U;

    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    span = total - format->base_address;
    if (span < XX_BOO_MIN_FILE_SIZE || span > XX_BOO_MAX_ENCODED_SIZE)
        return false;

    stream = (boo_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->source_size = (size_t)span;
    stream->source = (uint8_t *)xx_mem_alloc(stream->source_size);
    if (!stream->source) goto fail;
    if (!boo_read_at(format->device, format->base_address, stream->source,
                     stream->source_size))
        goto fail;

    if (!boo_parse_name_line(stream->source, stream->source_size, stream->name,
                             &stream->body_offset))
        goto fail;
    if (!boo_probe_gate(stream->source, stream->source_size,
                        stream->body_offset))
        goto fail;
    /* Measuring pass: bounded, and it is what proves the whole stream is
     * well formed rather than just its first few lines. */
    if (!boo_decode(stream->source + stream->body_offset,
                    stream->source_size - stream->body_offset, NULL,
                    (size_t)XX_BOO_MAX_DECODED_SIZE, &measured))
        goto fail;
    stream->unpacked_size = (int64_t)measured;
    stream->consumed = false;
    *result = stream;
    return true;
fail:
    boo_stream_free(stream);
    return false;
}

/* -------------------------------------------------------------- record -- */

static bool boo_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *boo_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool boo_set_record(xx_archive_record *record,
                           const Abstractformat *format,
                           const boo_stream *stream) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = (int64_t)stream->body_offset;
    record->data_offset = format->base_address + (int64_t)stream->body_offset;
    record->compressed_size =
        (int64_t)(stream->source_size - stream->body_offset);
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSED_SIZE,
               (uint64_t)(stream->source_size - stream->body_offset)) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)stream->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_boo_init(xx_boo *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    /* The 4-to-3 packing fills the output most significant bits first. */
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_BOO_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "text/plain");
    xx_format_set_extension(&archive->format, "boo");
    archive->format.check_is_valid = xx_boo_check_is_valid;
    archive->format.handle_base_info = xx_boo_handle_base_info;
    archive->format.get_format_size = xx_boo_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_boo_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_boo_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_boo_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_boo_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_boo_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_boo_free_archive_records_reading;
    archive->body_offset = -1;
    archive->unpacked_size = -1;
}

xx_boo *xx_boo_create(xx_io_device *device, int64_t base_address) {
    xx_boo *archive = (xx_boo *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_boo_init(archive, device, base_address);
    return archive;
}

void xx_boo_destroy(xx_boo *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_boo_free(xx_boo *archive) {
    if (!archive) return;
    xx_boo_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_boo_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    boo_stream *stream;
    if (!boo_parse(format, &stream, pd)) return false;
    boo_stream_free(stream);
    return true;
}

bool xx_boo_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    boo_stream *stream;
    xx_boo *archive;

    if (!format || !boo_parse(format, &stream, pd)) {
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_boo *)format;
    archive->body_offset = format->base_address + (int64_t)stream->body_offset;
    archive->unpacked_size = stream->unpacked_size;
    format->number_of_archive_records = 1U;
    /* The transport occupies the whole file; there is no trailer. */
    format->format_size = (int64_t)stream->source_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    boo_stream_free(stream);
    return true;
}

int64_t xx_boo_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_boo_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_boo_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_boo_handle_base_info(format, pd))
               ? 1U
               : 0U;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_boo_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    boo_stream *stream;
    xx_archive_record_state *state;

    if (!boo_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        boo_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = boo_stream_free;
    state->total_records = 1;
    if (!boo_copy_options(&state->options, options) ||
        !boo_set_record(&state->current_record, format, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_boo_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_boo_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    boo_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (boo_stream *)state->internal_state)) {
        if (state) state->has_record = false;
        return false;
    }
    stream->consumed = true;
    ++state->current_index;
    state->has_record = false;
    return false;
}

bool xx_boo_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    boo_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    size_t done = 0U;
    bool result = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (boo_stream *)state->internal_state) || stream->consumed ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    if (stream->unpacked_size <= 0 ||
        stream->unpacked_size > XX_BOO_MAX_DECODED_SIZE)
        return false;

    /* The published length is the TRIMMED one, but the decoder writes the
     * final group in full before the padding is removed, so the buffer has
     * to be two bytes longer than the result.  Sizing it to the trimmed
     * length would make the last group fail to fit and the whole member
     * fail to unpack. */
    plain = (uint8_t *)xx_mem_alloc((size_t)stream->unpacked_size +
                                    BOO_MAX_TRAILING_PAD);
    if (!plain) goto done;
    if (!boo_decode(stream->source + stream->body_offset,
                    stream->source_size - stream->body_offset, plain,
                    (size_t)stream->unpacked_size + BOO_MAX_TRAILING_PAD,
                    &written))
        goto done;
    /* Both passes run the identical token loop and the identical trim, so a
     * disagreement here is a defect, not something to paper over. */
    if (written != (size_t)stream->unpacked_size) goto done;

    path_option = boo_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (done < written) {
            ssize_t amount =
                xx_io_write(destination, plain + done, written - done);
            if (amount <= 0 || (size_t)amount > written - done) {
                result = false;
                break;
            }
            done += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_boo_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors -- */

int64_t xx_boo_get_body_offset(const xx_boo *archive) {
    return archive ? archive->body_offset : -1;
}

int64_t xx_boo_get_unpacked_size(const xx_boo *archive) {
    return archive ? archive->unpacked_size : -1;
}
