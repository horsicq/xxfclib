/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * The legacy LZ4 frame ("lz4demo", still written by `lz4 -l`): a magic word
 * and then a chain of length-prefixed LZ4 blocks that tiles the file.
 * xx_lz4demo.h carries the field table.
 *
 * The container stores neither a content size nor a checksum, so the only
 * anchor it offers is the block grammar itself: every block has to decode
 * with its declared compressed length consumed exactly and its output inside
 * the format's 8 MiB block ceiling, and the chain has to end exactly on the
 * last byte of the file.  Detection trial-decodes the first block; the size
 * pass decodes the rest.
 *
 * A declared block length above LZ4_COMPRESSBOUND(8 MiB) cannot be a block:
 * lz4io reads such a word as the next frame's magic.  Only the legacy and
 * skippable magics are followed here, so any other oversize word ends the
 * grammar and the file is refused -- which also bounds the packed-block
 * buffer at about 8 MiB whatever the file claims.
 *
 * `lz4 -l` on empty input writes the magic alone; that decodes to an empty
 * payload and is accepted as such.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lz4demo/xx_lz4demo.h"

#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as LZ4DEMO is registered there. */
#ifdef LZ4DEMO
#define XX_LZ4DEMO_FILE_TYPE XX_FILE_TYPE_LZ4DEMO
#else
#define XX_LZ4DEMO_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define LZ4DEMO_MAGIC UINT32_C(0x184c2102)
#define LZ4DEMO_SKIP_MAGIC_LOW UINT32_C(0x184d2a50)
#define LZ4DEMO_SKIP_MAGIC_HIGH UINT32_C(0x184d2a5f)
/* The legacy format fixes the uncompressed block at 8 MiB. */
#define LZ4DEMO_BLOCK_SIZE ((size_t)8U * 1024U * 1024U)
/* LZ4_COMPRESSBOUND(LZ4DEMO_BLOCK_SIZE): the largest a stored block can be. */
#define LZ4DEMO_MAX_PACKED \
    ((int64_t)(LZ4DEMO_BLOCK_SIZE + LZ4DEMO_BLOCK_SIZE / 255U + 16U))
#define LZ4DEMO_MAX_BLOCKS 262144U
/* Every word the walk consumes (block, repeated magic, skippable frame)
 * counts here, so a file of nothing but magics cannot spin the walk. */
#define LZ4DEMO_MAX_CHAIN (2U * LZ4DEMO_MAX_BLOCKS)
/* The size pass stops measuring (and reports the size as unknown) past this
 * much output; it never rejects a frame for being large. */
#define LZ4DEMO_MAX_OUTPUT ((uint64_t)1024U * 1024U * 1024U)
/* Above this much packed input the size pass is skipped: a listing is worth
 * more than a multi-gigabyte decode nobody asked for. */
#define LZ4DEMO_MAX_SIZE_PASS ((int64_t)64 * 1024 * 1024)
#define LZ4DEMO_PAYLOAD_NAME "payload"

typedef struct lz4demo_context_s {
    int64_t input_size;
    int64_t archive_size;
    int64_t stream_offset; /**< First block-size word, past the magic. */
    int64_t stream_size;
    uint64_t block_count;
    uint64_t unpacked_size; /**< 0 when the size pass was skipped. */
} lz4demo_context;

typedef struct lz4demo_stream_s {
    lz4demo_context context;
    size_t index;
    size_t count;
} lz4demo_stream;

static uint32_t lz4demo_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool lz4demo_read_at(xx_io_device *device, int64_t offset,
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

/* A 4 KiB read window over the stream.  Size words, skippable-frame headers
 * and small blocks are served from it, so a chain of tiny elements costs one
 * device read per window instead of a seek and a read per word; spans longer
 * than the window are read directly. */
#define LZ4DEMO_WINDOW 4096U

typedef struct lz4demo_window_s {
    xx_io_device *device;
    int64_t base;   /**< Device offset of stream position 0. */
    int64_t size;   /**< Stream length. */
    int64_t start;  /**< Stream position of buffer[0]. */
    size_t length;  /**< Valid bytes in buffer. */
    uint8_t buffer[LZ4DEMO_WINDOW];
} lz4demo_window;

static void lz4demo_window_init(lz4demo_window *window, xx_io_device *device,
                                int64_t base, int64_t size) {
    window->device = device;
    window->base = base;
    window->size = size;
    window->start = 0;
    window->length = 0U;
}

static bool lz4demo_fetch(lz4demo_window *window, int64_t position,
                          uint8_t *out, size_t count) {
    int64_t offset;
    if (position < 0 || position > window->size ||
        (uint64_t)count > (uint64_t)(window->size - position))
        return false;
    if (count > LZ4DEMO_WINDOW)
        return lz4demo_read_at(window->device, window->base + position, out,
                               count);
    offset = position - window->start;
    if (offset < 0 || (uint64_t)offset > (uint64_t)window->length ||
        count > window->length - (size_t)offset) {
        int64_t available = window->size - position;
        size_t want = available < (int64_t)LZ4DEMO_WINDOW
                          ? (size_t)available : (size_t)LZ4DEMO_WINDOW;
        window->length = 0U;
        if (!lz4demo_read_at(window->device, window->base + position,
                             window->buffer, want)) return false;
        window->start = position;
        window->length = want;
        offset = 0;
    }
    xx_rt_memcpy(out, window->buffer + (size_t)offset, count);
    return true;
}

/* Advance to the next LZ4 block of the chain, consuming repeated legacy
 * magics (concatenated streams) and skippable frames on the way.  Returns 1
 * with the block's stream offset and packed size, 0 at the exact end of the
 * stream, -1 on anything the grammar does not allow. */
static int lz4demo_next_block(lz4demo_window *window, int64_t *position,
                              uint64_t *chain, int64_t *block_offset,
                              int64_t *block_size) {
    for (;;) {
        uint8_t header[4];
        uint32_t word;
        int64_t declared;
        if (*position == window->size) return 0;
        if (++*chain > (uint64_t)LZ4DEMO_MAX_CHAIN ||
            !lz4demo_fetch(window, *position, header, sizeof(header)))
            return -1;
        word = lz4demo_le32(header);
        *position += 4;
        if (word == LZ4DEMO_MAGIC) continue;
        if (word >= LZ4DEMO_SKIP_MAGIC_LOW && word <= LZ4DEMO_SKIP_MAGIC_HIGH) {
            /* A skippable frame: its length word, then that many bytes. */
            if (!lz4demo_fetch(window, *position, header, sizeof(header)))
                return -1;
            declared = (int64_t)lz4demo_le32(header);
            *position += 4;
            if (declared > window->size - *position) return -1;
            *position += declared;
            continue;
        }
        declared = (int64_t)word;
        if (declared <= 0 || declared > LZ4DEMO_MAX_PACKED ||
            declared > window->size - *position) return -1;
        *block_offset = *position;
        *block_size = declared;
        *position += declared;
        return 1;
    }
}

static bool lz4demo_load_block(lz4demo_window *window, int64_t offset,
                               int64_t size, uint8_t **packed,
                               size_t *capacity) {
    if ((size_t)size > *capacity) {
        uint8_t *grown = (uint8_t *)(*packed
                                         ? xx_mem_realloc(*packed, (size_t)size)
                                         : xx_mem_alloc((size_t)size));
        if (!grown) return false;
        *packed = grown;
        *capacity = (size_t)size;
    }
    return lz4demo_fetch(window, offset, *packed, (size_t)size);
}

/* Walk the frame.  `scratch`, when given, receives each block's plaintext and
 * the decoded lengths are summed into `unpacked`; without it the walk is
 * purely structural apart from the first block, which is always trial
 * decoded because a bare u32 chain is far too weak to accept on its own.
 * Past LZ4DEMO_MAX_OUTPUT the size pass gives up measuring (the size is then
 * reported as 0, "unknown") but the structural walk still runs to the end. */
static bool lz4demo_walk(Abstractformat *format, int64_t base, int64_t size,
                         uint8_t *scratch, uint64_t *unpacked,
                         uint64_t *block_count, xx_pd_struct *pd) {
    lz4demo_window window;
    uint8_t *packed = NULL;
    size_t packed_capacity = 0U;
    int64_t position = 0;
    uint64_t blocks = 0U, total = 0U, chain = 0U;
    bool first = true, result = false, measured = scratch != NULL;
    lz4demo_window_init(&window, format->device, base, size);
    for (;;) {
        int64_t block_offset = 0, block_size = 0;
        int step;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        step = lz4demo_next_block(&window, &position, &chain, &block_offset,
                                  &block_size);
        if (step < 0) goto done;
        if (step == 0) break;
        if (blocks >= (uint64_t)LZ4DEMO_MAX_BLOCKS) goto done;
        if (scratch || first) {
            size_t written = 0U;
            if (!lz4demo_load_block(&window, block_offset, block_size, &packed,
                                    &packed_capacity)) goto done;
            if (!scratch) {
                /* Detection path: one block is enough to prove the codec. */
                uint8_t *probe = (uint8_t *)xx_mem_alloc(LZ4DEMO_BLOCK_SIZE);
                bool decoded;
                if (!probe) goto done;
                decoded = xx_lz4_decompress_block(packed, (size_t)block_size,
                                                  probe, LZ4DEMO_BLOCK_SIZE,
                                                  &written);
                xx_mem_free(probe);
                if (!decoded || written == 0U) goto done;
            } else {
                if (!xx_lz4_decompress_block(packed, (size_t)block_size,
                                             scratch, LZ4DEMO_BLOCK_SIZE,
                                             &written) ||
                    written == 0U) goto done;
                if ((uint64_t)written > LZ4DEMO_MAX_OUTPUT - total) {
                    /* Too large to be worth measuring: stop decoding, keep
                     * validating the structure. */
                    scratch = NULL;
                    measured = false;
                } else {
                    total += (uint64_t)written;
                }
            }
            first = false;
        }
        ++blocks;
    }
    /* No blocks at all is the empty stream `lz4 -l` writes for empty input:
     * the magic alone (plus, at most, further magics / skippable frames). */
    if (block_count) *block_count = blocks;
    if (unpacked) *unpacked = measured ? total : 0U;
    result = true;
done:
    if (packed) xx_mem_free(packed);
    return result;
}

static void lz4demo_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool lz4demo_parse(Abstractformat *format, lz4demo_context *out,
                          bool measure, xx_pd_struct *pd) {
    uint8_t magic[4];
    lz4demo_context context;
    uint8_t *scratch = NULL;
    int64_t total, size;
    bool walked;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    /* At least the magic; a bare magic is an empty stream. */
    if (size < 4 ||
        !lz4demo_read_at(format->device, format->base_address, magic,
                         sizeof(magic))) return false;
    if (lz4demo_le32(magic) != LZ4DEMO_MAGIC) return false;
    xx_mem_zero(&context, sizeof(context));
    context.input_size = size;
    context.archive_size = size;
    context.stream_offset = format->base_address + 4;
    context.stream_size = size - 4;
    if (measure && context.stream_size <= LZ4DEMO_MAX_SIZE_PASS) {
        scratch = (uint8_t *)xx_mem_alloc(LZ4DEMO_BLOCK_SIZE);
        if (!scratch) return false;
    }
    walked = lz4demo_walk(format, context.stream_offset, context.stream_size,
                          scratch, &context.unpacked_size,
                          &context.block_count, pd);
    if (scratch) xx_mem_free(scratch);
    if (!walked) return false;
    *out = context;
    return true;
}

static bool lz4demo_copy_options(xx_list_s *destination,
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

static const xx_var *lz4demo_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool lz4demo_set_record(xx_archive_record *record,
                               const lz4demo_context *context) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = context->stream_offset - 4;
    record->header_size = 4;
    record->data_offset = context->stream_offset;
    record->compressed_size = context->stream_size;
    return xx_archive_record_set_original_name(record,
                                               LZ4DEMO_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)context->stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          context->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_lz4demo_init(xx_lz4demo *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_LZ4DEMO_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lz4");
    xx_format_set_extension(&archive->format, "lz4");
    archive->format.check_is_valid = xx_lz4demo_check_is_valid;
    archive->format.handle_base_info = xx_lz4demo_handle_base_info;
    archive->format.get_format_size = xx_lz4demo_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lz4demo_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lz4demo_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lz4demo_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lz4demo_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lz4demo_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lz4demo_free_archive_records_reading;
}

xx_lz4demo *xx_lz4demo_create(xx_io_device *device, int64_t base_address) {
    xx_lz4demo *archive = (xx_lz4demo *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lz4demo_init(archive, device, base_address);
    return archive;
}

void xx_lz4demo_destroy(xx_lz4demo *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_lz4demo_free(xx_lz4demo *archive) {
    if (!archive) return;
    xx_lz4demo_destroy(archive);
    xx_mem_free(archive);
}

bool xx_lz4demo_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    lz4demo_context context;
    return lz4demo_parse(format, &context, false, pd);
}

bool xx_lz4demo_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    lz4demo_context context;
    xx_lz4demo *archive;
    if (!format || !lz4demo_parse(format, &context, true, pd)) return false;
    archive = (xx_lz4demo *)format;
    archive->number_of_records = 1U;
    archive->unpacked_size = context.unpacked_size;
    archive->block_count = context.block_count;
    format->number_of_archive_records = 1U;
    format->format_size = context.archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_lz4demo_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lz4demo_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_lz4demo_get_number_of_archive_records(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lz4demo_handle_base_info(format, pd))
               ? ((xx_lz4demo *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_lz4demo_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    lz4demo_stream *stream;
    xx_archive_record_state *state;
    lz4demo_context context;
    if (!lz4demo_parse(format, &context, true, pd)) return NULL;
    stream = (lz4demo_stream *)xx_mem_calloc(1U, sizeof(*stream));
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
    state->free_internal = lz4demo_stream_free;
    state->total_records = 1U;
    if (!lz4demo_copy_options(&state->options, options) ||
        !lz4demo_set_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_lz4demo_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_lz4demo_archive_record_move_to_next(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    lz4demo_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (lz4demo_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

/* Decode the whole frame to `destination`, block by block.  The scratch
 * buffer is the format's own 8 MiB block ceiling and the packed buffer is
 * capped at LZ4DEMO_MAX_PACKED, so a frame of any length costs about 16 MiB
 * of memory beyond the output file. */
static bool lz4demo_unpack_to_device(Abstractformat *format,
                                     const lz4demo_context *context,
                                     xx_io_device *destination,
                                     xx_pd_struct *pd) {
    lz4demo_window window;
    uint8_t *packed = NULL;
    uint8_t *scratch = NULL;
    size_t packed_capacity = 0U;
    int64_t position = 0;
    uint64_t blocks = 0U, chain = 0U;
    bool result = false;
    scratch = (uint8_t *)xx_mem_alloc(LZ4DEMO_BLOCK_SIZE);
    if (!scratch) return false;
    lz4demo_window_init(&window, format->device, context->stream_offset,
                        context->stream_size);
    for (;;) {
        int64_t block_offset = 0, block_size = 0;
        size_t written = 0U, done = 0U;
        int step;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        step = lz4demo_next_block(&window, &position, &chain, &block_offset,
                                  &block_size);
        if (step < 0) goto done;
        if (step == 0) break;
        if (++blocks > (uint64_t)LZ4DEMO_MAX_BLOCKS ||
            !lz4demo_load_block(&window, block_offset, block_size, &packed,
                                &packed_capacity) ||
            !xx_lz4_decompress_block(packed, (size_t)block_size, scratch,
                                     LZ4DEMO_BLOCK_SIZE, &written) ||
            written == 0U) goto done;
        while (done < written) {
            ssize_t amount = xx_io_write(destination, scratch + done,
                                         written - done);
            if (amount <= 0 || (size_t)amount > written - done) goto done;
            done += (size_t)amount;
        }
    }
    result = true;
done:
    if (packed) xx_mem_free(packed);
    if (scratch) xx_mem_free(scratch);
    return result;
}

bool xx_lz4demo_unpack_current_archive_record(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    lz4demo_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (lz4demo_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = lz4demo_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: prove the frame decodes and report that. */
        return lz4demo_parse(format, &stream->context, true, pd);
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
               ? xx_str_concat3(base, "/", LZ4DEMO_PAYLOAD_NAME)
               : xx_str_concat(base, LZ4DEMO_PAYLOAD_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = lz4demo_unpack_to_device(format, &stream->context,
                                          destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_lz4demo_free_archive_records_reading(Abstractformat *format,
                                             xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
