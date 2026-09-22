/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xx_rarx_internal.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>

struct xx_rarx_decoder {
    uint64_t max_window_size;
    xx_rarx_status_t status;
    xx_rarx_method_t active_method;
    uint64_t active_window_size;
    bool chain_started;
    bool poisoned;
    xx_rarx15_state *rar15;
    xx_rarx20_state *rar20;
    xx_rarx29_state *rar29;
    xx_rarx50_state *rar50;
};

static void xx_rarx_clear_result(xx_rarx_result *result) {
    if (result) {
        result->input_consumed = 0;
        result->output_written = 0;
        result->status = XX_RARX_STATUS_INVALID_ARGUMENT;
    }
}

static xx_rarx_status_t xx_rarx_set_status(xx_rarx_decoder *decoder,
                                            xx_rarx_result *result,
                                            xx_rarx_status_t status) {
    if (decoder) {
        decoder->status = status;
    }
    if (result) {
        result->status = status;
    }
    return status;
}

xx_rarx_decoder *xx_rarx_decoder_create(uint64_t max_window_size) {
    xx_rarx_decoder *decoder;

    if (max_window_size == 0) {
        max_window_size = XX_RARX_DEFAULT_MAX_WINDOW;
    }
    if (max_window_size > (uint64_t)SIZE_MAX) {
        return NULL;
    }

    decoder = (xx_rarx_decoder *)xx_mem_calloc(1, sizeof(*decoder));
    if (!decoder) {
        return NULL;
    }
    decoder->max_window_size = max_window_size;
    decoder->status = XX_RARX_STATUS_OK;
    return decoder;
}

void xx_rarx_decoder_reset(xx_rarx_decoder *decoder) {
    if (!decoder) {
        return;
    }
    if (decoder->rar15) xx_rarx15_reset(decoder->rar15);
    if (decoder->rar20) xx_rarx20_reset(decoder->rar20);
    if (decoder->rar29) xx_rarx29_reset(decoder->rar29);
    if (decoder->rar50) xx_rarx50_reset(decoder->rar50);
    decoder->active_method = 0;
    decoder->active_window_size = 0;
    decoder->chain_started = false;
    decoder->poisoned = false;
    decoder->status = XX_RARX_STATUS_OK;
}

void xx_rarx_decoder_free(xx_rarx_decoder *decoder) {
    if (!decoder) {
        return;
    }
    xx_rarx15_destroy(decoder->rar15);
    xx_rarx20_destroy(decoder->rar20);
    xx_rarx29_destroy(decoder->rar29);
    xx_rarx50_destroy(decoder->rar50);
    xx_rarx_clear_free(decoder, sizeof(*decoder));
}

xx_rarx_status_t xx_rarx_decoder_last_status(const xx_rarx_decoder *decoder) {
    return decoder ? decoder->status : XX_RARX_STATUS_INVALID_ARGUMENT;
}

static xx_rarx_status_t xx_rarx_decode_memory(xx_rarx_decoder *decoder,
                                               const uint8_t *source,
                                               size_t source_size,
                                               uint8_t *destination,
                                               size_t destination_size,
                                               xx_rarx_method_t method,
                                               size_t window_size,
                                               bool solid,
                                               size_t *source_used,
                                               xx_pd_struct *progress) {
    switch (method) {
        case XX_RARX_METHOD_15:
            if (!decoder->rar15) decoder->rar15 = xx_rarx15_create();
            if (!decoder->rar15) return XX_RARX_STATUS_NO_MEMORY;
            return xx_rarx15_decode(decoder->rar15, source, source_size,
                                    destination, destination_size, window_size,
                                    solid, source_used, progress);
        case XX_RARX_METHOD_20:
            if (!decoder->rar20) decoder->rar20 = xx_rarx20_create();
            if (!decoder->rar20) return XX_RARX_STATUS_NO_MEMORY;
            return xx_rarx20_decode(decoder->rar20, source, source_size,
                                    destination, destination_size, window_size,
                                    solid, source_used, progress);
        case XX_RARX_METHOD_29:
            if (!decoder->rar29) decoder->rar29 = xx_rarx29_create();
            if (!decoder->rar29) return XX_RARX_STATUS_NO_MEMORY;
            return xx_rarx29_decode(decoder->rar29, source, source_size,
                                    destination, destination_size, window_size,
                                    (size_t)decoder->max_window_size, solid,
                                    source_used, progress);
        case XX_RARX_METHOD_50:
            if (!decoder->rar50) decoder->rar50 = xx_rarx50_create();
            if (!decoder->rar50) return XX_RARX_STATUS_NO_MEMORY;
            return xx_rarx50_decode(decoder->rar50, source, source_size,
                                    destination, destination_size, window_size,
                                    solid, source_used, progress);
        case XX_RARX_METHOD_70:
        default:
            return XX_RARX_STATUS_UNSUPPORTED_VERSION;
    }
}

bool xx_rarx_decoder_unpack_memory(xx_rarx_decoder *decoder,
                                   const void *source, size_t compressed_size,
                                   void *destination, size_t expected_size,
                                   xx_rarx_method_t method, uint64_t window_size,
                                   bool solid_continuation,
                                   xx_rarx_result *result,
                                   xx_pd_struct *progress) {
    xx_rarx_status_t status;
    size_t source_used = 0;

    xx_rarx_clear_result(result);
    if (!decoder || (!source && compressed_size != 0) ||
        (!destination && expected_size != 0) || window_size == 0 ||
        window_size > decoder->max_window_size ||
        window_size > (uint64_t)SIZE_MAX) {
        if (decoder && window_size > decoder->max_window_size) {
            xx_rarx_set_status(decoder, result, XX_RARX_STATUS_LIMIT);
        } else {
            xx_rarx_set_status(decoder, result, XX_RARX_STATUS_INVALID_ARGUMENT);
        }
        return false;
    }
    if (progress && xx_pd_is_stopped(progress)) {
        xx_rarx_set_status(decoder, result, XX_RARX_STATUS_CANCELLED);
        return false;
    }
    if (decoder->poisoned && solid_continuation) {
        xx_rarx_set_status(decoder, result, XX_RARX_STATUS_POISONED);
        return false;
    }
    if (solid_continuation &&
        (!decoder->chain_started || decoder->active_method != method ||
         decoder->active_window_size != window_size)) {
        xx_rarx_set_status(decoder, result, XX_RARX_STATUS_CORRUPT);
        return false;
    }
    if (!solid_continuation) {
        xx_rarx_decoder_reset(decoder);
    }

    /* Empty compressed members carry no bit stream, but they still delimit or
     * continue a solid chain.  Treat them as a successful state transition
     * instead of asking a method decoder to read a block header that cannot
     * exist. */
    if (compressed_size == 0 && expected_size == 0) {
        decoder->active_method = method;
        decoder->active_window_size = window_size;
        decoder->chain_started = true;
        decoder->poisoned = false;
        xx_rarx_set_status(decoder, result, XX_RARX_STATUS_OK);
        return true;
    }

    status = xx_rarx_decode_memory(decoder, (const uint8_t *)source,
                                   compressed_size, (uint8_t *)destination,
                                   expected_size, method, (size_t)window_size,
                                   solid_continuation, &source_used, progress);
    xx_rarx_set_status(decoder, result, status);
    if (result && (uint64_t)source_used <= (uint64_t)INT64_MAX) {
        result->input_consumed = (int64_t)source_used;
    }
    if (status != XX_RARX_STATUS_OK) {
        decoder->poisoned = true;
        return false;
    }

    decoder->active_method = method;
    decoder->active_window_size = window_size;
    decoder->chain_started = true;
    decoder->poisoned = false;
    if (result) {
        result->output_written = (int64_t)expected_size;
    }
    return true;
}

static bool xx_rarx_read_exact(xx_io_device *source, uint8_t *buffer,
                               size_t size, xx_pd_struct *progress) {
    size_t done = 0;
    while (done < size) {
        ssize_t amount;
        size_t chunk = size - done;
        if (chunk > 65536u) chunk = 65536u;
        if (progress && xx_pd_is_stopped(progress)) return false;
        amount = xx_io_read(source, buffer + done, chunk);
        if (amount <= 0 || (size_t)amount > chunk) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_rarx_write_exact(xx_io_device *destination,
                                const uint8_t *buffer, size_t size,
                                xx_pd_struct *progress) {
    size_t done = 0;
    while (done < size) {
        ssize_t amount;
        size_t chunk = size - done;
        if (chunk > 65536u) chunk = 65536u;
        if (progress && xx_pd_is_stopped(progress)) return false;
        amount = xx_io_write(destination, buffer + done, chunk);
        if (amount <= 0 || (size_t)amount > chunk) return false;
        done += (size_t)amount;
    }
    return true;
}

bool xx_rarx_decoder_unpack_device(xx_rarx_decoder *decoder,
                                   xx_io_device *source,
                                   int64_t source_offset,
                                   int64_t compressed_size,
                                   xx_io_device *destination,
                                   int64_t expected_size,
                                   xx_rarx_method_t method,
                                   uint64_t window_size,
                                   bool solid_continuation,
                                   xx_rarx_result *result,
                                   xx_pd_struct *progress) {
    uint8_t *packed = NULL;
    uint8_t *unpacked = NULL;
    bool ok = false;

    xx_rarx_clear_result(result);
    if (!decoder || !source || !destination || source_offset < 0 ||
        compressed_size < 0 || expected_size < 0 ||
        (uint64_t)compressed_size > (uint64_t)SIZE_MAX ||
        (uint64_t)expected_size > (uint64_t)SIZE_MAX) {
        xx_rarx_set_status(decoder, result, XX_RARX_STATUS_INVALID_ARGUMENT);
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc(compressed_size > 0
                                         ? (size_t)compressed_size : 1u);
    unpacked = (uint8_t *)xx_mem_alloc(expected_size > 0
                                           ? (size_t)expected_size : 1u);
    if (!packed || !unpacked) {
        xx_rarx_set_status(decoder, result, XX_RARX_STATUS_NO_MEMORY);
        goto cleanup;
    }
    if (xx_io_seek64(source, source_offset, SEEK_SET) != 0 ||
        !xx_rarx_read_exact(source, packed, (size_t)compressed_size, progress)) {
        xx_rarx_set_status(decoder, result,
                           progress && xx_pd_is_stopped(progress)
                               ? XX_RARX_STATUS_CANCELLED
                               : XX_RARX_STATUS_IO_ERROR);
        goto cleanup;
    }
    if (!xx_rarx_decoder_unpack_memory(decoder, packed,
                                       (size_t)compressed_size, unpacked,
                                       (size_t)expected_size, method,
                                       window_size, solid_continuation, result,
                                       progress)) {
        goto cleanup;
    }
    if (!xx_rarx_write_exact(destination, unpacked, (size_t)expected_size,
                             progress)) {
        decoder->poisoned = true;
        xx_rarx_set_status(decoder, result,
                           progress && xx_pd_is_stopped(progress)
                               ? XX_RARX_STATUS_CANCELLED
                               : XX_RARX_STATUS_IO_ERROR);
        goto cleanup;
    }
    ok = true;

cleanup:
    xx_rarx_clear_free(unpacked, expected_size > 0 ? (size_t)expected_size : 1u);
    xx_rarx_clear_free(packed, compressed_size > 0 ? (size_t)compressed_size : 1u);
    return ok;
}

bool xx_rarx_unpack_device(xx_io_device *source, int64_t source_offset,
                           int64_t compressed_size,
                           xx_io_device *destination,
                           int64_t expected_size,
                           xx_rarx_method_t method,
                           uint64_t window_size,
                           xx_rarx_result *result,
                           xx_pd_struct *progress) {
    xx_rarx_decoder *decoder = xx_rarx_decoder_create(0);
    bool ok;
    if (!decoder) {
        xx_rarx_clear_result(result);
        if (result) result->status = XX_RARX_STATUS_NO_MEMORY;
        return false;
    }
    ok = xx_rarx_decoder_unpack_device(decoder, source, source_offset,
                                       compressed_size, destination,
                                       expected_size, method, window_size,
                                       false, result, progress);
    xx_rarx_decoder_free(decoder);
    return ok;
}
