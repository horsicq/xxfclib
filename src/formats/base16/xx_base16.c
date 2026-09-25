/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Base16 / plain hex text.  xx_base16.h carries the grammar and the
 * acceptance rules.
 *
 * The decoding rule (hex digits of either case, high nibble first, space /
 * TAB / CR / LF ignored even between the two digits of a byte, a dangling
 * last nibble dropped) follows Deark's de_decode_base16()
 * (deark-1.7.3/src/deark-data.c, MIT licence, Copyright (C) 2016 Jason
 * Summers).  Deark never auto-detects the format and silently skips any
 * other byte; the detection window and its rules below are this reader's
 * own, because a magic-less text format needs them to stay out of ordinary
 * text files.
 *
 * Detection (check_is_valid) looks at no more than the first 64 KiB plus a
 * 1 KiB padding tail, so the late probe is cheap: binary input fails on its
 * first byte.  handle_base_info applies the same window rules and then walks
 * the rest of the file only to find where the text ends; it never allocates
 * more than one fixed read buffer.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/base16/xx_base16.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as BASE16 is registered there. */
#ifdef BASE16
#define XX_BASE16_FILE_TYPE XX_FILE_TYPE_BASE16
#else
#define XX_BASE16_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* Bytes of text the acceptance rules are applied to. */
#define BASE16_WINDOW ((int64_t)64 * 1024)
/* Fewest hex digits the window must hold (four decoded bytes). */
#define BASE16_MIN_DIGITS 8U
/* Longest run of 0x1A / 0x00 padding tolerated between the text and EOF
 * when the text ends inside the window (CP/M and DOS record padding). */
#define BASE16_MAX_PADDING 1024
/* Read buffer for scanning; decoding uses the same buffer size. */
#define BASE16_CHUNK 8192U
#define BASE16_PAYLOAD_NAME "payload"

enum {
    BASE16_FOREIGN = 0,
    BASE16_SPACE,
    BASE16_DECIMAL,
    BASE16_LOWER,
    BASE16_UPPER
};

/* Class of one text byte; `nibble` receives a digit's value. */
static int base16_class(uint8_t byte, uint8_t *nibble) {
    if (byte >= '0' && byte <= '9') {
        *nibble = (uint8_t)(byte - '0');
        return BASE16_DECIMAL;
    }
    if (byte >= 'a' && byte <= 'f') {
        *nibble = (uint8_t)(byte - 'a' + 10);
        return BASE16_LOWER;
    }
    if (byte >= 'A' && byte <= 'F') {
        *nibble = (uint8_t)(byte - 'A' + 10);
        return BASE16_UPPER;
    }
    if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n')
        return BASE16_SPACE;
    return BASE16_FOREIGN;
}

/* Resumable forward scan over the text, relative to the base address. */
typedef struct base16_scan_s {
    int64_t position;     /**< Next byte to examine. */
    int64_t pair_end;     /**< Just past the last byte of accepted text. */
    uint64_t digits;      /**< Every digit seen before `position`. */
    uint64_t pair_digits; /**< Digits inside [0, pair_end): always even. */
    uint64_t run;         /**< Length of the digit run in progress. */
    bool pending;         /**< A high nibble waits for its partner. */
    bool odd_run;         /**< Some closed run had odd length. */
    bool has_decimal;
    bool has_lower;
    bool has_upper;
    bool foreign;         /**< Stopped on a byte outside the alphabet. */
} base16_scan;

typedef struct base16_context_s {
    int64_t input_size;
    int64_t text_size;
    uint64_t digit_count;
    uint64_t unpacked_size;
    bool is_uppercase;
} base16_context;

typedef struct base16_stream_s {
    base16_context context;
    size_t index;
    size_t count;
} base16_stream;

static bool base16_read_at(xx_io_device *device, int64_t offset,
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

static void base16_close_run(base16_scan *scan) {
    if (scan->run & 1U) scan->odd_run = true;
    scan->run = 0U;
}

/* Continue `scan` up to relative offset `limit` (never past the first
 * foreign byte).  False only on a read error or a stop request. */
static bool base16_scan_run(Abstractformat *format, base16_scan *scan,
                            int64_t limit, xx_pd_struct *pd) {
    uint8_t buffer[BASE16_CHUNK];
    while (!scan->foreign && scan->position < limit) {
        int64_t left = limit - scan->position;
        size_t want = left < (int64_t)sizeof(buffer) ? (size_t)left
                                                      : sizeof(buffer);
        size_t index;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!base16_read_at(format->device,
                            format->base_address + scan->position, buffer,
                            want))
            return false;
        for (index = 0U; index < want; ++index) {
            uint8_t nibble = 0U;
            int kind = base16_class(buffer[index], &nibble);
            if (kind == BASE16_FOREIGN) {
                base16_close_run(scan);
                scan->foreign = true;
                break;
            }
            if (kind == BASE16_SPACE) {
                base16_close_run(scan);
                if (!scan->pending)
                    scan->pair_end = scan->position + (int64_t)index + 1;
                continue;
            }
            if (kind == BASE16_DECIMAL) scan->has_decimal = true;
            else if (kind == BASE16_LOWER) scan->has_lower = true;
            else scan->has_upper = true;
            ++scan->digits;
            ++scan->run;
            if (scan->pending) {
                scan->pending = false;
                scan->pair_digits += 2U;
                scan->pair_end = scan->position + (int64_t)index + 1;
            } else {
                scan->pending = true;
            }
        }
        scan->position += (int64_t)index;
    }
    return true;
}

/* True when [offset, size) is at most BASE16_MAX_PADDING bytes of 0x1A and
 * 0x00 only. */
static bool base16_padding_tail(Abstractformat *format, int64_t offset,
                                int64_t size) {
    uint8_t buffer[BASE16_MAX_PADDING];
    int64_t length = size - offset;
    size_t index;
    if (offset < 0 || length <= 0 || length > BASE16_MAX_PADDING) return false;
    if (!base16_read_at(format->device, format->base_address + offset,
                        buffer, (size_t)length))
        return false;
    for (index = 0U; index < (size_t)length; ++index)
        if (buffer[index] != 0x1AU && buffer[index] != 0x00U) return false;
    return true;
}

/* Apply the window rules; with `measure`, also find the end of the text. */
static bool base16_parse(Abstractformat *format, base16_context *out,
                         bool measure, xx_pd_struct *pd) {
    base16_scan scan;
    base16_context context;
    int64_t total, size, window;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)BASE16_MIN_DIGITS) return false;
    window = size < BASE16_WINDOW ? size : BASE16_WINDOW;

    xx_mem_zero(&scan, sizeof(scan));
    if (!base16_scan_run(format, &scan, window, pd)) return false;
    if (scan.foreign) {
        /* The text stops inside the window: only padding may follow. */
        if (!base16_padding_tail(format, scan.position, size)) return false;
    } else if (scan.position == size) {
        /* The whole input fitted the window: its last run is closed too. */
        base16_close_run(&scan);
    }
    if (scan.digits < BASE16_MIN_DIGITS || !scan.has_decimal ||
        !(scan.has_lower || scan.has_upper) ||
        (scan.has_lower && scan.has_upper) || scan.odd_run)
        return false;

    xx_mem_zero(&context, sizeof(context));
    context.input_size = size;
    context.is_uppercase = scan.has_upper;
    if (measure) {
        if (!scan.foreign && scan.position < size &&
            !base16_scan_run(format, &scan, size, pd))
            return false;
        if (scan.pair_end <= 0 || scan.pair_digits < 2U) return false;
        context.text_size = scan.pair_end;
        context.digit_count = scan.pair_digits;
        context.unpacked_size = scan.pair_digits / 2U;
    }
    *out = context;
    return true;
}

static bool base16_write_all(xx_io_device *destination, const uint8_t *data,
                             size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Decode the measured text to `destination`.  Every byte inside the text was
 * already classified, so a foreign byte or a count mismatch here means the
 * input changed underneath and is reported as failure. */
static bool base16_decode(Abstractformat *format,
                          const base16_context *context,
                          xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t input[BASE16_CHUNK];
    uint8_t output[BASE16_CHUNK / 2U + 1U];
    int64_t position = 0;
    uint64_t written = 0U;
    uint8_t high = 0U;
    bool pending = false;
    if (!format || !context || !destination || context->text_size <= 0)
        return false;
    while (position < context->text_size) {
        int64_t left = context->text_size - position;
        size_t want = left < (int64_t)sizeof(input) ? (size_t)left
                                                     : sizeof(input);
        size_t index, produced = 0U;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!base16_read_at(format->device, format->base_address + position,
                            input, want))
            return false;
        for (index = 0U; index < want; ++index) {
            uint8_t nibble = 0U;
            int kind = base16_class(input[index], &nibble);
            if (kind == BASE16_FOREIGN) return false;
            if (kind == BASE16_SPACE) continue;
            if (pending) {
                output[produced++] = (uint8_t)((high << 4U) | nibble);
                pending = false;
            } else {
                high = nibble;
                pending = true;
            }
        }
        if (produced != 0U) {
            if ((uint64_t)produced > context->unpacked_size - written ||
                !base16_write_all(destination, output, produced))
                return false;
            written += (uint64_t)produced;
        }
        position += (int64_t)want;
    }
    return !pending && written == context->unpacked_size;
}

static bool base16_copy_options(xx_list_s *destination,
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

static const xx_var *base16_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool base16_set_record(Abstractformat *format,
                              xx_archive_record *record,
                              const base16_context *context) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = 0;
    record->data_offset = format->base_address;
    record->compressed_size = context->text_size;
    return xx_archive_record_set_original_name(record, BASE16_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)context->text_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          context->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void base16_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

void xx_base16_init(xx_base16 *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_BASE16_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "text/plain");
    xx_format_set_extension(&archive->format, "hex");
    archive->format.check_is_valid = xx_base16_check_is_valid;
    archive->format.handle_base_info = xx_base16_handle_base_info;
    archive->format.get_format_size = xx_base16_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_base16_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_base16_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_base16_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_base16_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_base16_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_base16_free_archive_records_reading;
}

xx_base16 *xx_base16_create(xx_io_device *device, int64_t base_address) {
    xx_base16 *archive = (xx_base16 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_base16_init(archive, device, base_address);
    return archive;
}

void xx_base16_destroy(xx_base16 *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_base16_free(xx_base16 *archive) {
    if (!archive) return;
    xx_base16_destroy(archive);
    xx_mem_free(archive);
}

bool xx_base16_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    base16_context context;
    return base16_parse(format, &context, false, pd);
}

bool xx_base16_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    base16_context context;
    xx_base16 *archive;
    if (!format || !base16_parse(format, &context, true, pd)) return false;
    archive = (xx_base16 *)format;
    archive->number_of_records = 1U;
    archive->unpacked_size = context.unpacked_size;
    archive->digit_count = context.digit_count;
    archive->text_size = context.text_size;
    archive->is_uppercase = context.is_uppercase;
    format->number_of_archive_records = 1U;
    format->format_size = context.text_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_base16_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_base16_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_base16_get_number_of_archive_records(Abstractformat *format,
                                                 xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_base16_handle_base_info(format, pd))
               ? ((xx_base16 *)format)->number_of_records : 0U;
}

uint64_t xx_base16_get_unpacked_size(xx_base16 *archive) {
    if (!archive) return 0U;
    if (!archive->format.base_info_handled &&
        !xx_base16_handle_base_info(&archive->format, NULL))
        return 0U;
    return archive->unpacked_size;
}

bool xx_base16_unpack_to_device(xx_base16 *archive, xx_io_device *destination,
                                xx_pd_struct *pd) {
    base16_context context;
    if (!archive || !destination ||
        !base16_parse(&archive->format, &context, true, pd))
        return false;
    return base16_decode(&archive->format, &context, destination, pd);
}

xx_archive_record_state *xx_base16_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    base16_stream *stream;
    xx_archive_record_state *state;
    base16_context context;
    if (!base16_parse(format, &context, true, pd)) return NULL;
    stream = (base16_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->context = context;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = base16_stream_free;
    state->total_records = 1U;
    if (!base16_copy_options(&state->options, options) ||
        !base16_set_record(format, &state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_base16_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_base16_archive_record_move_to_next(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    base16_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (base16_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_base16_unpack_current_archive_record(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    base16_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (base16_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = base16_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: prove the text still parses and report that. */
        return base16_parse(format, &stream->context, true, pd);
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
               ? xx_str_concat3(base, "/", BASE16_PAYLOAD_NAME)
               : xx_str_concat(base, BASE16_PAYLOAD_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = base16_decode(format, &stream->context, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_base16_free_archive_records_reading(Abstractformat *format,
                                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
