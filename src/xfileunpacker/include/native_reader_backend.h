/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XFILEUNPACKER_NATIVE_READER_BACKEND_H
#define XFILEUNPACKER_NATIVE_READER_BACKEND_H

#include "core.h"
#include <xxfclib/io/xx_io.h>

/* Resolve the native selector namespace. A NULL name means automatic format
 * selection; raw requests a single uncompressed file if no stream reader
 * handles the input. Returns false for selectors outside this namespace. */
bool xfu_native_reader_resolve(const char *selection, const char **name, bool *raw);

/* Return an outer compression/envelope type for a raw view. UNKNOWN means
 * the caller should use xfu_native_raw_read instead. */
xx_file_type_t xfu_native_raw_filter_type(const char *archive_path);
/* Same classification for a borrowed seekable device. */
xx_file_type_t xfu_native_raw_filter_type_device(xx_io_device *device);

/* Check an archive member before extraction, including both separator styles.
 * A root-directory dot record is allowed only when is_directory is true.
 * ':' is refused on Windows only, where it names a drive or a stream. */
bool xfu_native_member_name_safe(const char *name, bool is_directory);

/* The most one decoded nested layer or envelope payload may hold; within
 * TEST's memory-only scope, also no more than the RAM that scope has left. */
#define XFU_NATIVE_INNER_MAX_BYTES (UINT64_C(4) * 1024U * 1024U * 1024U)

typedef enum xfu_native_inner_status {
    XFU_NATIVE_INNER_DECODE_ERROR = -1,
    XFU_NATIVE_INNER_NOT_ARCHIVE = 0,
    XFU_NATIVE_INNER_ARCHIVE = 1,
    /* The layer starts like an archive but does not fit the limit above:
     * nothing is kept and the scope is not failed. The caller should say so;
     * it can still read the outer stream as one file. */
    XFU_NATIVE_INNER_TOO_LARGE = 2,
    /* The same for a layer that starts like an archive but has no room for
     * its temporary copy (the temporary folder is full, say). */
    XFU_NATIVE_INNER_NO_ROOM = 3
} xfu_native_inner_status;

/* Decompress one outer stream if it holds an archive. Whether it can is
 * decided from its decoded start (the first 4 MiB): only then, and for text
 * or other structured data that start does not settle, is the whole stream
 * decoded and verified, so a large stream of evenly spread (compressed or
 * encrypted) data costs no copy. ARCHIVE transfers a rewound, seekable
 * device to the caller, which must close it: the layer in memory when it is
 * no longer than that start, else a temporary device. The other statuses
 * leave *inner NULL. Within TEST's memory-only scope the temporary device
 * is RAM-backed and charged to that scope's byte limit. NOT_ARCHIVE does
 * not mean the stream was verified: its own reader does that. Decode
 * failures never silently fall back to a raw stream reader. */
xfu_native_inner_status xfu_native_open_inner(const char *archive_path, xx_file_type_t filter_type, xx_pd_struct *pd, xx_io_device **inner);
/* Same decoding for a borrowed device; the caller retains ownership of the
 * source and closes a returned inner device. */
xfu_native_inner_status xfu_native_open_inner_device(xx_io_device *source, xx_file_type_t filter_type, xx_pd_struct *pd, xx_io_device **inner);
/* The same two, giving with TOO_LARGE the type the layer starts as (an
 * archive or filter type; for text, what its start cut at the last line
 * break is) in *layer_type (optional), else UNKNOWN. */
xfu_native_inner_status xfu_native_open_inner_ex(const char *archive_path, xx_file_type_t filter_type, xx_pd_struct *pd, xx_io_device **inner,
                                                 xx_file_type_t *layer_type);
xfu_native_inner_status xfu_native_open_inner_device_ex(xx_io_device *source, xx_file_type_t filter_type, xx_pd_struct *pd, xx_io_device **inner,
                                                        xx_file_type_t *layer_type);

/* Whether a stream that starts like `type` is whole as far as its headers
 * tell but too large for its reader, which decodes it in memory: zstd and
 * LZ4 take at most *limit (1 GiB). Then it is no damaged stream. */
bool xfu_native_stream_over_limit(xx_io_device *device, xx_file_type_t type, uint64_t *limit);

typedef enum xfu_native_envelope_status {
    XFU_NATIVE_ENVELOPE_DECODE_ERROR = -1,
    XFU_NATIVE_ENVELOPE_NOT_FOUND = 0,
    XFU_NATIVE_ENVELOPE_RAW = 1,
    XFU_NATIVE_ENVELOPE_ARCHIVE = 2,
    /* The payload does not fit XFU_NATIVE_INNER_MAX_BYTES or TEST's RAM:
     * nothing is kept; the ordinary Base64/uuencode reader can stream it. */
    XFU_NATIVE_ENVELOPE_TOO_LARGE = 3,
    /* The payload is longer than 4 MiB and there is no room for its
     * temporary copy: the same, for the same reader. */
    XFU_NATIVE_ENVELOPE_NO_ROOM = 4
} xfu_native_envelope_status;

/* Decode a standard begin-base64 envelope. Only a first line of the form
 * "begin-base64 <octal mode> <name>" (1 to 6 octal digits, at most 1024
 * bytes with its line break: the header rules of xxfclib's Base64 reader,
 * which reads what this leaves), with a name usable as a file name, makes
 * one: anything else is NOT_FOUND. Once it has one, a damaged body is
 * DECODE_ERROR, as are I/O errors and Cancel. On RAW or ARCHIVE, transfers
 * a rewound device (the payload in memory when it is at most 4 MiB, else
 * a temporary device) and an allocated safe member name. Caller closes the
 * device and frees the name. The decoded device obeys the same memory
 * policy and layer limit as xfu_native_open_inner. */
xfu_native_envelope_status xfu_native_base64_open(const char *archive_path, xx_pd_struct *pd, xx_io_device **decoded, char **raw_name);
xfu_native_envelope_status xfu_native_base64_open_device(xx_io_device *source, xx_pd_struct *pd, xx_io_device **decoded, char **raw_name);

/* Decode a single complete uuencoded block with the same ownership and
 * status rules; its first line must be "begin <octal mode> <name>".
 * Multiple valid blocks remain a member archive for the ordinary UUE
 * reader, so this returns NOT_FOUND for them. */
xfu_native_envelope_status xfu_native_uue_open(const char *archive_path, xx_pd_struct *pd, xx_io_device **decoded, char **raw_name);
xfu_native_envelope_status xfu_native_uue_open_device(xx_io_device *source, xx_pd_struct *pd, xx_io_device **decoded, char **raw_name);

/* Read an otherwise unrecognized input as one raw file. The core should give
 * recognized compressed streams to their format readers first. */
int xfu_native_raw_read(const xfu_request *request, const char *unpack_to, xx_pd_struct *pd);

/* Read exactly zero bytes as an archive with zero members. */
int xfu_native_empty_read(const xfu_request *request, xx_pd_struct *pd);

/* Same raw member behavior for a borrowed, rewound decoded device. Its type
 * is the Base64 or uuencode envelope that a RAW payload of the openers above
 * came from, wherever that envelope sits (or the archive's own envelope when
 * the archive file is one), so frontends offer extraction; otherwise BINARY. */
int xfu_native_raw_device_read(const xfu_request *request, const char *unpack_to, xx_pd_struct *pd, xx_io_device *input, const char *name);

#endif
