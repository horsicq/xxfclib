/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_ZSTD_DEC_H
#define XX_ZSTD_DEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool xx_zstd_decode_frames(const void *source, size_t source_size, void *destination, size_t destination_size, size_t *out_written);

bool xx_zstd_decode_frames_bounded(const void *source, size_t source_size, void *destination, size_t destination_capacity, size_t *out_written);

/* Internal retry interface: distinguishes an output-capacity stop from other
 * failures. Later bytes can still be malformed. The caller may grow only on
 * this signal, up to an independently scanned bound. */
bool xx_zstd_decode_frames_bounded_retry(const void *source, size_t source_size, void *destination, size_t destination_capacity, size_t *out_written,
                                         bool *needs_more_output);
/* Internal: xx_zstd_decode_frames_bounded, but for input that may end
 * inside a frame (a cut file). It then fails with *cut set and *out_written
 * the bytes of the whole blocks before that point, and of what there is of a
 * cut raw block; the cut frame's checksum cannot check them. Whole or cut,
 * *checked (optional) gets the bytes from the start that frame checksums
 * vouch for: the frames up to the first one without a checksum, or without
 * all of it. */
bool xx_zstd_decode_frames_prefix(const void *source, size_t source_size, void *destination, size_t destination_size, size_t *out_written, bool *cut, size_t *checked);

/* Streaming decoder input and output. read returns the bytes read, 0 at the
 * end of the input, or -1 on failure. skip (optional: read is used without
 * it) passes over size input bytes and fails when fewer remain. write takes
 * each decoded block; false stops decoding. */
typedef struct xx_zstd_stream_io {
    ptrdiff_t (*read)(void *opaque, void *buffer, size_t size);
    bool (*skip)(void *opaque, uint64_t size);
    bool (*write)(void *opaque, const void *data, size_t size);
    void *opaque;
} xx_zstd_stream_io;

/* Decodes the complete frames read through io, as xx_zstd_decode_frames
 * does, holding one frame's history window in memory (plus up to 16 MiB of
 * room and fixed buffers) instead of the whole input and output. A frame
 * whose window exceeds window_limit fails, unless it declares at most 16 KiB
 * of content. expected_size is the exact total, or UINT64_MAX when unknown.
 * *out_written (optional) gets the bytes written, also on failure. */
bool xx_zstd_decode_stream(const xx_zstd_stream_io *io, uint64_t window_limit, uint64_t expected_size, uint64_t *out_written);

#endif /* XX_ZSTD_DEC_H */
