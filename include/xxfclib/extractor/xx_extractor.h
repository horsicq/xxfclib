/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_EXTRACTOR_H
#define XX_EXTRACTOR_H
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/io/xx_io.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_extractor_mode { XX_EXTRACTOR_HEURISTIC = 0, XX_EXTRACTOR_FORMAT = 1, XX_EXTRACTOR_RAW = 2 } xx_extractor_mode;
typedef enum xx_extractor_status {
    XX_EXTRACTOR_OK = 0,
    XX_EXTRACTOR_CANCELLED = -1,
    XX_EXTRACTOR_INVALID = -2,
    XX_EXTRACTOR_IO = -3,
    XX_EXTRACTOR_NO_MEMORY = -4,
    XX_EXTRACTOR_UNSUPPORTED = -5,
    XX_EXTRACTOR_SINK = -6,
    XX_EXTRACTOR_LIMIT = -7
} xx_extractor_status;
typedef enum xx_extractor_method { XX_EXTRACTOR_STORE = 0, XX_EXTRACTOR_ZIP_MEMBER = 1, XX_EXTRACTOR_PDF_STREAM = 2 } xx_extractor_method;
typedef struct xx_extractor_type {
    xx_file_type_t file_type;
    const char* name;
    bool enabled_by_default;
} xx_extractor_type;
typedef struct xx_extractor_options {
    xx_extractor_mode mode;
    xx_file_type_t file_type; /**< UNKNOWN detects the outer file automatically. */
    const xx_file_type_t* types; /**< Borrowed selected payload types; NULL selects defaults. */
    size_t type_count;
    bool all_types, deep_scan;
    uint64_t offset, length; /**< UINT64_MAX length means through EOF. */
    uint64_t result_limit; /**< Zero means unlimited. A reached limit is a
                              successful truncated scan. */
    uint64_t max_member_size; /**< Zero or the default (UINT64_MAX): no limit.
                                 A member is decoded into memory below
                                 xx_get_file_buffer_size(), otherwise into a
                                 temporary file that must fit and, from 16 MiB
                                 on, leave 1 GiB free; a
                                 member that cannot have that file, or whose
                                 write to it fails, is left out of the
                                 inventory. A smaller value fails the scan with
                                 XX_EXTRACTOR_LIMIT at the first member over
                                 it. Native reader limits still apply (PDF
                                 streams decode up to 256 MiB). */
    size_t chunk_size; /**< Default 64 KiB; bounded to 1..16 MiB. */
} xx_extractor_options;
typedef struct xx_extractor_record {
    uint64_t offset, size, unpacked_size;
    xx_file_type_t file_type, container_type;
    xx_extractor_method method;
    uint64_t container_offset, container_size, archive_index;
    uint64_t max_member_size;
    uint32_t crc32;
    bool has_crc32;
    const char *name, *info, *extension; /**< Borrowed only for the sink call. */
    uint64_t compression_method; /**< Native ZIP method; zero for stored or non-ZIP records. */
    char compression[128]; /**< Display method or bounded PDF filter chain, e.g. Store/Deflate/FlateDecode. */
} xx_extractor_record;
typedef struct xx_extractor_stats {
    uint64_t bytes_scanned, record_count;
    xx_extractor_mode resolved_mode;
    bool limit_reached;
} xx_extractor_stats;
typedef bool (*xx_extractor_sink)(const xx_extractor_record* record, void* user);
XXFC_API xx_extractor_options xx_extractor_defaults(void);
XXFC_API size_t xx_extractor_type_count(void);
XXFC_API const xx_extractor_type* xx_extractor_type_at(size_t index);
XXFC_API const char* xx_extractor_status_name(xx_extractor_status status);
XXFC_API bool xx_extractor_format_available(xx_file_type_t type);
/** Catalog-bounded outer-file detection. UNKNOWN on I/O failure/cancellation;
 * Binary when no offered native reader recognizes the input. Cursor preserved.
 */
XXFC_API xx_file_type_t xx_extractor_detect_device(xx_io_device* device, xx_pd_struct* progress);
/** Synchronous, cursor-preserving, bounded-memory extraction inventory. Raw
 * validates signatures with native readers; Deep searches within found files.
 * Format enumerates PE resources or ZIP/TAR/PDF streams. Heuristic chooses
 * Format when available and Raw otherwise. Results are in source-offset order
 * in Raw mode and native record order in Format mode. Cancellation/failure may
 * deliver partial results; callers needing transactions must stage them.
 * No output files are created. The device must not change until extraction. */
XXFC_API xx_extractor_status xx_extractor_scan_device(xx_io_device* device, const xx_extractor_options* options, xx_pd_struct* progress,
    xx_extractor_sink sink, void* user, xx_extractor_stats* stats);
/** Decode one inventoried record to a caller-owned writable device. Source
 * cursor is preserved. The record's source metadata and CRC are rechecked;
 * archive-derived paths are never opened. Failed writes can partially modify
 * destination; use extract_file for atomic publication. */
XXFC_API xx_extractor_status xx_extractor_extract_device(
    xx_io_device* source, const xx_extractor_record* record, xx_io_device* destination, xx_pd_struct* progress);
/** Atomic same-directory staging; failures/cancellation preserve an existing
 * destination and remove the staging file. overwrite=false preserves existing
 * files. The full destination path is supplied by the caller. */
XXFC_API xx_extractor_status xx_extractor_extract_file(
    xx_io_device* source, const xx_extractor_record* record, const char* path, bool overwrite, xx_pd_struct* progress);
/** Build a deterministic single filename (no directory components) suitable
 * for Dump-all. Sanitizes archive names and adds offset, CRC and extension to
 * avoid traversal and accidental collisions. Returns false if capacity is too
 * small. The directory must still be chosen by the application. */
XXFC_API bool xx_extractor_record_filename(const xx_extractor_record* record, char* buffer, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
