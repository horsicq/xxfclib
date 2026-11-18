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

/**
 * @file xx_deflate.h
 * @brief Deflate (RFC 1951) and Deflate64 (Enhanced Deflate) compression and decompression.
 */

#ifndef XX_DEFLATE_H
#define XX_DEFLATE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compression levels */
#define XX_DEFLATE_LEVEL_STORED 0
#define XX_DEFLATE_LEVEL_FASTEST 1
#define XX_DEFLATE_LEVEL_DEFAULT 6
#define XX_DEFLATE_LEVEL_BEST 9

/** Per-operation DEFLATE packing settings. A positive max_candidates
 * overrides the compression level, including level zero. Zero (or NULL
 * options) retains the existing level behavior. The count limits hash-chain
 * candidates per position; actual history may contain fewer candidates.
 * Explicit compression methods such as ZIP STORE remain unchanged. */
typedef struct xx_deflate_pack_options {
    size_t max_candidates;
} xx_deflate_pack_options;

/** Extended packing options. The original options struct and APIs retain
 * their ABI. max_candidates follows xx_deflate_pack_options. lazy_match_limit
 * is a match-length threshold, not the number of lookahead bytes: 0 disables
 * lazy parsing; otherwise a match shorter than this value is deferred by one
 * byte if a strictly longer match is available at the next position. Values
 * above the method's maximum match length have the same effect as that maximum.
 * Candidate count and level selection remain independent of this threshold. */
typedef struct xx_deflate_pack_options_ex {
    size_t max_candidates;
    size_t lazy_match_limit;
    bool short_match_filter; /**< Reject length-3 matches beyond distance 4096; default false. */
    size_t nice_match; /**< Stop main search at this length; zero disables. */
    size_t good_match; /**< Quarter main candidate budget after reaching this length; zero disables. */
} xx_deflate_pack_options_ex;


/* ========================================================================= */
/* --- Deflate / Deflate64 Decompression (Unpacking)                     --- */
/* ========================================================================= */

/**
 * @brief Unpack a raw Deflate or Deflate64 stream from src_dev to dst_dev.
 * @param src_dev Source I/O device containing compressed data.
 * @param src_offset Start byte offset in src_dev (-1 to use current seek position).
 * @param comp_size Compressed stream size in bytes (-1 if unbounded until stream end).
 * @param dst_dev Destination I/O device receiving decompressed data.
 * @param is_deflate64 True for Deflate64 (64KB window), false for standard Deflate (32KB window).
 * @param pd Optional progress and cancellation monitor.
 * @return True on success, false on format error or cancellation.
 */
XXFC_API bool xx_deflate_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size, xx_io_device *dst_dev, bool is_deflate64, xx_pd_struct *pd);

/** Decode one finite raw stream with an optional preset history. window_size
 * is zero for the native window, or a power of two from 256 through 32768
 * (65536 for Deflate64). Dictionary bytes are prior history, never output;
 * dictionary_size must not exceed the selected window. out_consumed, when
 * supplied, receives the exact stream length rounded up to its last byte,
 * excluding input read-ahead and trailing bytes. It is zero on failure.
 * comp_size must be nonnegative. The caller owns both devices. */
XXFC_API bool xx_deflate_unpack_device_ex(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size, xx_io_device *dst_dev, bool is_deflate64, size_t window_size,
                                          const void *dictionary, size_t dictionary_size, int64_t *out_consumed, xx_pd_struct *pd);

/**
 * @brief Unpack a Deflate / Deflate64 stream directly to a disk file (UTF-8 path).
 */
XXFC_API bool xx_deflate_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size, const char *dst_file_path, bool is_deflate64,
                                               xx_pd_struct *pd);

/**
 * @brief Unpack a Deflate / Deflate64 stream directly to a disk file (wchar_t path).
 */
XXFC_API bool xx_deflate_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size, const wchar_t *dst_file_path_w, bool is_deflate64,
                                                 xx_pd_struct *pd);

/**
 * @brief Unpack a Deflate / Deflate64 stream to a memory buffer.
 */
XXFC_API bool xx_deflate_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size, void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                                 bool is_deflate64, xx_pd_struct *pd);

/**
 * @brief Unpack a Deflate / Deflate64 stream from memory to an I/O device.
 */
XXFC_API bool xx_deflate_unpack_memory_to_device(const void *src_buf, size_t comp_size, xx_io_device *dst_dev, bool is_deflate64, xx_pd_struct *pd);

/**
 * @brief Decode one raw Deflate stream and report the number of source bytes
 * consumed through its final block.  Bytes after the stream are left for the
 * caller, which is needed by container formats such as zlib.
 */
XXFC_API bool xx_deflate_unpack_memory_to_device_ex(const void *src_buf, size_t comp_size, xx_io_device *dst_dev, size_t *out_consumed, bool is_deflate64,
                                                    xx_pd_struct *pd);

/**
 * @brief Pure in-memory decompression of a Deflate or Deflate64 buffer.
 */
XXFC_API bool xx_deflate_decompress_memory(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written, bool is_deflate64);

/** Decode a raw Deflate stream whose back-references may use a prior 32 KiB
 * history, as in consecutive MSZIP cabinet blocks. The dictionary is not
 * included in the output. */
XXFC_API bool xx_deflate_decompress_memory_with_dictionary(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                                           const void *dictionary, size_t dictionary_size, bool is_deflate64);

/** Decoding flags of the *_flags entry points below. Zero gives exactly the
 * entry points without flags; a bit that is not defined here fails the call.
 *
 * XX_DEFLATE_UNPACK_ZLIB_STRICT: a dynamic block's header is accepted only
 * where zlib's inflate accepts it. Without the flag a code length set may
 * leave codes unused (the block fails only when the data uses one); with it
 * the block fails, as in zlib, when
 *   - the code length code is not complete ("invalid code lengths set"; one
 *     without any code fails in both modes),
 *   - the end-of-block code 256 has no code ("invalid code -- missing
 *     end-of-block"),
 *   - the literal/length set is incomplete and is not a single code of one
 *     bit ("invalid literal/lengths set"),
 *   - the distance set is incomplete and is neither empty nor a single code
 *     of one bit ("invalid distances set").
 * Over-subscribed sets and more than 286 literal/length or 30 distance codes
 * (32 in Deflate64) fail in both modes. With the flag, a stream is decoded
 * successfully exactly when zlib's raw inflate reaches its end without an
 * error, and the bytes are the same. The flag costs nothing outside block
 * headers. */
#define XX_DEFLATE_UNPACK_ZLIB_STRICT 0x1U

/** xx_deflate_unpack_device_ex() with decoding flags. */
XXFC_API bool xx_deflate_unpack_device_flags(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size, xx_io_device *dst_dev, bool is_deflate64,
                                             size_t window_size, const void *dictionary, size_t dictionary_size, int64_t *out_consumed, unsigned flags,
                                             xx_pd_struct *pd);
/** xx_deflate_unpack_memory_to_device_ex() with decoding flags. */
XXFC_API bool xx_deflate_unpack_memory_to_device_flags(const void *src_buf, size_t comp_size, xx_io_device *dst_dev, size_t *out_consumed, bool is_deflate64,
                                                       unsigned flags, xx_pd_struct *pd);
/** xx_deflate_decompress_memory() with decoding flags. */
XXFC_API bool xx_deflate_decompress_memory_flags(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written, bool is_deflate64,
                                                 unsigned flags);

/* ========================================================================= */
/* --- Deflate / Deflate64 Compression (Packing)                         --- */
/* ========================================================================= */

/**
 * @brief Compress data from src_dev into dst_dev using Deflate or Deflate64.
 * @param src_dev Source I/O device with uncompressed data.
 * @param src_offset Start byte offset in src_dev (-1 to use current seek position).
 * @param uncomp_size Number of bytes to read and compress (-1 for all remaining).
 * @param dst_dev Destination device receiving the raw compressed stream.
 * @param level Compression level (0 = stored, 1 = fastest, 6 = default, 9 = best).
 * @param is_deflate64 True to produce Deflate64 (64KB window), false for Deflate (32KB).
 * @param pd Optional progress and cancellation monitor.
 * @return True on success, false on error or cancellation.
 */
XXFC_API bool xx_deflate_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size, xx_io_device *dst_dev, int level, bool is_deflate64,
                                     xx_pd_struct *pd);

/** Compress with a selected LZ77 window. window_size is zero for the native
 * window, or a power of two from 256 through 32768 (65536 for Deflate64).
 * Smaller windows limit emitted back-reference distances. uncomp_size must
 * be nonnegative. The caller owns both devices. */
XXFC_API bool xx_deflate_pack_device_ex(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size, xx_io_device *dst_dev, int level, bool is_deflate64,
                                        size_t window_size, xx_pd_struct *pd);

/**
 * @brief Inspect an input source, compute its uncompressed size and CRC32, and compress
 * into dst_dev. Resets src_dev seek position upon completion.
 */
XXFC_API bool xx_deflate_pack_source(xx_io_device *src_dev, const char *src_file_path, int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                                     xx_io_device *dst_dev, int level, bool is_deflate64, xx_pd_struct *pd);

/**
 * @brief Pure in-memory compression of a buffer using Deflate or Deflate64.
 */
XXFC_API bool xx_deflate_compress_memory(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written, int level, bool is_deflate64);

/** Options-aware variants; existing packing APIs delegate with NULL
 * options. window_size follows xx_deflate_pack_device_ex(). A positive
 * candidate count selects the normal compression path regardless of level;
 * Deflate64 also uses its normal long-match limit instead of low-level caps. */
XXFC_API bool xx_deflate_pack_device_with_options(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size, xx_io_device *dst_dev,
                                                  int level, bool is_deflate64, size_t window_size, const xx_deflate_pack_options *options, xx_pd_struct *pd);
XXFC_API bool xx_deflate_pack_source_with_options(xx_io_device *src_dev, const char *src_file_path, int64_t *out_uncomp_size, int64_t *out_comp_size,
                                                  uint32_t *out_crc32, xx_io_device *dst_dev, int level, bool is_deflate64,
                                                  const xx_deflate_pack_options *options, xx_pd_struct *pd);
XXFC_API bool xx_deflate_compress_memory_with_options(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                                      int level, bool is_deflate64, const xx_deflate_pack_options *options);

/** Extended options-aware entry points. NULL options preserves the
 * original defaults. Old options-aware entry points always disable lazy
 * parsing and read only the original struct's max_candidates field. */
XXFC_API bool xx_deflate_pack_device_with_options_ex(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size, xx_io_device *dst_dev,
                                                     int level, bool is_deflate64, size_t window_size, const xx_deflate_pack_options_ex *options, xx_pd_struct *pd);
XXFC_API bool xx_deflate_pack_source_with_options_ex(xx_io_device *src_dev, const char *src_file_path, int64_t *out_uncomp_size, int64_t *out_comp_size,
                                                     uint32_t *out_crc32, xx_io_device *dst_dev, int level, bool is_deflate64,
                                                     const xx_deflate_pack_options_ex *options, xx_pd_struct *pd);
XXFC_API bool xx_deflate_compress_memory_with_options_ex(const void *src_buf, size_t src_size, void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                                         int level, bool is_deflate64, const xx_deflate_pack_options_ex *options);

/** The named DEFLATE packing settings of xx_deflate_pack_options_ex. Every
 * writer that offers them (ZIP per archive or per member, the raw codec tools
 * by string) sets them through the functions below, so names, ranges and
 * defaults live in one place. Zero, or false, is the default of each. */
typedef enum xx_deflate_pack_option_id {
    XX_DEFLATE_OPTION_CANDIDATES,         /**< "candidates": hash-chain candidates per position; positive overrides the level */
    XX_DEFLATE_OPTION_LAZY_MATCH,         /**< "lazy-match": lazy match-length threshold; 0 is greedy */
    XX_DEFLATE_OPTION_SHORT_MATCH_FILTER, /**< "short-match-filter": 0 or 1; skip length-3 matches beyond distance 4096 */
    XX_DEFLATE_OPTION_NICE_MATCH,         /**< "nice-match": stop the search at this length; 0 disables */
    XX_DEFLATE_OPTION_GOOD_MATCH,         /**< "good-match": quarter the candidate budget at this length; 0 disables */
    XX_DEFLATE_OPTION_COUNT
} xx_deflate_pack_option_id;

/** The string key of a setting, or NULL for an unknown id. */
XXFC_API const char *xx_deflate_pack_option_name(xx_deflate_pack_option_id id);
/** Look a key up (case-insensitive); returns XX_DEFLATE_OPTION_COUNT when unknown. */
XXFC_API xx_deflate_pack_option_id xx_deflate_pack_option_find(const char *key, size_t key_length);
/** Validate and store one setting: size settings take 0..SIZE_MAX, the
 * filter takes 0 or 1. Returns false, leaving options unchanged, otherwise. */
XXFC_API bool xx_deflate_pack_option_set(xx_deflate_pack_options_ex *options, xx_deflate_pack_option_id id, uint64_t value);
/** Read one setting back. */
XXFC_API bool xx_deflate_pack_option_get(const xx_deflate_pack_options_ex *options, xx_deflate_pack_option_id id, uint64_t *value);
/** Parse "key=value[;key=value...]" (',' also separates; spaces around keys
 * and values are ignored). Keys are the setting names above, plus "level"
 * (0..9) when level is not NULL. Values are decimal; the filter also accepts
 * true/false. On failure returns false, points *error_key (when not NULL) at
 * the offending pair and leaves options and level unchanged. */
XXFC_API bool xx_deflate_pack_options_parse(xx_deflate_pack_options_ex *options, int *level, const char *text, const char **error_key);
/** Write the settings as a string accepted by the parser (all five keys).
 * Returns the length needed, excluding the terminator; writes at most
 * capacity bytes including the terminator when buffer is not NULL. */
XXFC_API size_t xx_deflate_pack_options_format(const xx_deflate_pack_options_ex *options, char *buffer, size_t capacity);

/* ========================================================================= */
/* --- zlib stream wrapper (RFC 1950)                                    --- */
/* ========================================================================= */

/**
 * @brief Upper bound for a complete RFC 1950 stream produced by
 * xx_zlib_stream_encode_memory().
 *
 * SIZE_MAX is returned when the bound cannot be represented.
 */
XXFC_API size_t xx_zlib_stream_compress_bound(size_t source_size);

/**
 * @brief Encode a complete RFC 1950 stream with the native Deflate encoder.
 *
 * The output contains the two-byte zlib header, raw Deflate payload and
 * big-endian Adler-32 trailer. Levels use the same 0..9 convention as the
 * Deflate encoder. Use xx_zlib_stream_compress_bound() to size @p output.
 */
XXFC_API bool xx_zlib_stream_encode_memory(const uint8_t *input, size_t input_size,
                                           uint8_t *output, size_t output_capacity,
                                           size_t *written, int level);

/**
 * @brief Whether @p input opens with a well-formed zlib header.
 *
 * Checks the method, the window size, the header's multiple-of-31 rule and
 * the preset-dictionary flag. A stream requesting a preset dictionary is
 * reported invalid: the dictionary is not in the stream, so it cannot be
 * decoded here.
 */
XXFC_API bool xx_zlib_stream_header_is_valid(const uint8_t *input, size_t input_size);

/**
 * @brief Decode a zlib stream: header check, then raw Deflate.
 *
 * The trailing Adler-32 is not required to be present, because a container
 * that records a member's exact compressed length may cut the stream at its
 * last Deflate byte. Use xx_zlib_stream_trailer_matches() where it is.
 */
XXFC_API bool xx_zlib_stream_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written);

/** @brief Adler-32 of @p data, as zlib defines it. */
XXFC_API uint32_t xx_zlib_stream_adler32(const uint8_t *data, size_t size);

/**
 * @brief Whether the zlib trailer in @p input matches @p plain.
 *
 * False when the stream is too short to carry a trailer at all, so a caller
 * must only use this where the container guarantees the full stream.
 */
XXFC_API bool xx_zlib_stream_trailer_matches(const uint8_t *input, size_t input_size, const uint8_t *plain, size_t plain_size);

#ifdef __cplusplus
}
#endif

#endif /* XX_DEFLATE_H */
