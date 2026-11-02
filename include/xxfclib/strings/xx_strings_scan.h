/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_STRINGS_SCAN_H
#define XX_STRINGS_SCAN_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    XX_STRINGS_ASCII = 1,
    XX_STRINGS_UTF8 = 2,
    XX_STRINGS_UTF16LE = 4,
    XX_STRINGS_UTF16BE = 8,
    XX_STRINGS_ALL = 14
};
enum {
    XX_STRINGS_NO_TEXT = 1, /**< Deliver metadata without allocating decoded text. */
    XX_STRINGS_SCALAR = 2,  /**< Force the portable byte classifier (also useful for verification). */
    XX_STRINGS_DIE_ALPHABET = 4 /**< DIE-compatible ANSI (0x20..0x7F) and wide Latin-1/Cyrillic alphabet. */
};
enum {
    XX_STRINGS_TEXT_TRUNCATED = 1
};

typedef enum xx_strings_scan_status {
    XX_STRINGS_SCAN_OK = 0,
    XX_STRINGS_SCAN_CANCELLED = -1,
    XX_STRINGS_SCAN_INVALID = -2,
    XX_STRINGS_SCAN_IO = -3,
    XX_STRINGS_SCAN_NO_MEMORY = -4,
    XX_STRINGS_SCAN_SINK = -5
} xx_strings_scan_status;

typedef struct xx_strings_scan_options {
    unsigned minimum_length, encodings;
    uint64_t offset, length; /**< Scan this range; UINT64_MAX length means through EOF. */
    size_t chunk_size;       /**< 1..16 MiB, default xx_get_buffer_size() capped at 16 MiB (64 KiB unless set). */
    size_t max_text_bytes;   /**< Maximum UTF-8 prefix per active run, default 1 MiB. */
    unsigned flags;
} xx_strings_scan_options;

typedef struct xx_strings_scan_record {
    uint64_t offset, byte_size, character_length;
    unsigned encoding, flags;
    const char *text; /**< Borrowed NUL-terminated UTF-8 prefix, valid only during the callback. */
    size_t text_size; /**< Prefix bytes, excluding NUL. NULL/zero in metadata-only mode. */
} xx_strings_scan_record;

typedef struct xx_strings_scan_stats {
    uint64_t bytes_scanned, record_count;
} xx_strings_scan_stats;

/** Runs synchronously on the caller's thread. Return false to abort with
 * XX_STRINGS_SCAN_SINK. Records are emitted when runs end, in decoder order
 * within each input chunk; overlapping encoding interpretations are possible
 * and records are not guaranteed sorted. No record ownership is transferred. */
typedef bool (*xx_strings_scan_sink)(const xx_strings_scan_record *record, void *user);

XXFC_API xx_strings_scan_options xx_strings_scan_defaults(void);
XXFC_API const char *xx_strings_scan_status_name(xx_strings_scan_status status);
XXFC_API const char *xx_strings_scan_backend(unsigned flags);

/** Streaming extraction from a seekable, known-size device, preserving its
 * cursor. ASCII and UTF-8 cannot both be selected. UTF-16 scans both byte
 * alignments; ALL additionally requires a minimum-length ASCII streak or BOM
 * for wide runs, reducing false positives in arbitrary binaries. Malformed
 * encodings and nonprintable scalars delimit runs. Text is a bounded prefix;
 * complete byte/character lengths and counts remain 64-bit. A NULL sink counts
 * records with no decoded-text allocation. Memory is O(chunk + 5 * prefix),
 * independent of the number of results. Progress/cancellation use xx_pd, which
 * may be NULL. Stats are optional and include successfully delivered records
 * even on cancellation/error. The device and sink remain caller-owned. */
XXFC_API xx_strings_scan_status xx_strings_scan_device(xx_io_device *device, const xx_strings_scan_options *options, xx_pd_struct *progress, xx_strings_scan_sink sink,
                                                       void *user, xx_strings_scan_stats *stats);

typedef bool (*xx_strings_text_sink)(const char *utf8, size_t bytes, void *user);
/** Decode an already identified string range to bounded UTF-8 chunks. ASCII,
 * UTF-8 and UTF-16 are validated, including surrogate pairing. Chunks may split
 * a UTF-8 sequence; stream consumers must retain any required matching state.
 * No NUL terminator is supplied, no result-sized allocation is made, and the
 * source cursor is preserved. A NULL sink validates without delivering text. */
XXFC_API xx_strings_scan_status xx_strings_decode_device(xx_io_device *device, unsigned encoding, uint64_t offset, uint64_t byte_size, xx_pd_struct *progress,
                                                         xx_strings_text_sink sink, void *user);
/** As decode_device, with an optional DIE_ALPHABET flag matching extraction.
 * The original entry point retains its Unicode printable-scalar validation. */
XXFC_API xx_strings_scan_status xx_strings_decode_device_ex(xx_io_device *device, unsigned encoding, uint64_t offset, uint64_t byte_size, unsigned flags,
                                                            xx_pd_struct *progress, xx_strings_text_sink sink, void *user);

#ifdef __cplusplus
}
#endif
#endif
