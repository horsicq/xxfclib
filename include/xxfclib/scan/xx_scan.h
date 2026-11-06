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
 * @file xx_scan.h
 * @brief Engine-neutral scan options, records, traversal and engine ABI.
 */

#ifndef XX_SCAN_H
#define XX_SCAN_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/list/xx_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Stable display spelling used by all common result writers. */
XXFC_API const char *xx_scan_file_type_name(xx_file_type_t type);

/* --------------------------------------------------------------- options  */

/** Origin of a scan within its containing input. */
typedef enum xx_scan_part {
    XX_SCAN_PART_INPUT = 0,
    XX_SCAN_PART_OVERLAY,
    XX_SCAN_PART_RESOURCE,
    XX_SCAN_PART_ARCHIVE
} xx_scan_part;

/**
 * @brief Options shared by all scanning engines.
 *
 * Initialise with xx_scan_options_init(). file_name, engine_options and child
 * metadata strings are borrowed. The database paths (main and custom, plus the
 * deprecated extra path) are owned by the caller's options and
 * released by xx_scan_options_free(). Copies passed to scan callbacks borrow
 * those paths; only free the original owning options. An engine must report
 * XXFC_ERR_INVALID_ARG through pd for an unsupported scan mode.
 */
typedef struct xx_scan_options {
    int64_t offset;           /**< Start in the input device, in bytes. */
    int64_t size;             /**< Bytes to scan; -1 means through EOF. */
    xx_file_type_t file_type; /**< UNKNOWN requests automatic detection. */
    bool deep_scan;
    bool heuristic_scan;
    bool aggressive_scan;
    bool recursive_scan;
    bool overlay_scan; /**< Detect and scan trailing overlays as bounded devices. */
    bool resources_scan; /**< Scan PE and ELF resource payloads as bounded child devices. */
    bool archives_scan; /**< Scan the members of ZIP-family archives (ZIP, JAR, APK, IPA), as XScanEngine. */
    bool all_types_scan; /**< Scan applicable parent formats before the preferred type. */
    bool first_wrapper_only;
    bool verbose;
    const char *file_name;      /**< Optional input name or path. */
    const void *engine_options; /**< Optional engine-specific options. */
    /**
     * Set by the scanner, never by the caller: every scan of a device (the
     * input itself, an overlay, resource or archive member) gets a new nonzero scan_id,
     * and every callback of that scan sees it. parent_id is 0 for the input
     * itself and, for a child, the scan_id of the scan that found it.
     * Engines copy both IDs and child metadata into each record they create.
     */
    uint64_t scan_id;
    uint64_t parent_id;

    /* Signature database selection and result presentation. */
    bool format_result;
    bool hide_unknown;
    bool show_type;
    bool show_version;
    bool show_info;
    bool use_custom_database;
    /** Deprecated: runs DB_EXTRA records when set (default false). Kept for
     * API compatibility; cdie uses only the main and custom databases. */
    bool use_extra_database;
    bool result_as_json;
    bool result_as_xml;
    bool result_as_csv;
    bool result_as_tsv;
    bool result_as_plain_text;
    bool show_messages;
    bool profiling;
    bool sort;
    /** Owned runtime-allocated paths; NULL selects the consumer's defaults. */
    char *main_database_path;
    char *extra_database_path; /**< Deprecated; still freed, no cdie frontend sets it. */
    char *custom_database_path;
    xx_scan_part scan_part; /**< Set by the scanner; caller-supplied values are ignored. */
    const char *resource_type; /**< Borrowed resource type name or decimal ID, otherwise NULL. */
    const char *archive_member; /**< Borrowed member name during a synchronous archive child scan. */
    int64_t archive_offset; /**< Physical member/stream start in the containing input. */
    int64_t archive_size; /**< Packed extent in the containing input. */
    bool archive_compressed; /**< Child offsets are relative to decoded data when true. */
    /** Archive limits; zero selects 4 GiB/member, no total limit and
     * 100,000 entries (XScanEngine's own entry limit). XScanEngine has no
     * member size limit, and no ZIP entry without ZIP64 can declare 4 GiB, so
     * the default member bound only stops a stream that decodes on past
     * anything it could declare. Nonzero values override the defaults;
     * native decoder limits still apply (LHA compressed or masked payloads
     * currently have a 256 MiB decoded-member ceiling). A member over a size
     * limit is skipped, never truncated; the walk ends after the entry
     * limit. */
    uint64_t archive_max_member_size;
    uint64_t archive_max_total_size;
    uint32_t archive_max_members;
    /** Borrowed semicolon-separated signature tag filters. A signature must
     * match at least one include tag when this list is nonempty. Any exclude
     * match rejects it, including when the same tag is also included. */
    const char *include_tags;
    const char *exclude_tags;
    /** Static method label of the archive member being scanned, as
     * XBinary::getHandleMethods names it ("Deflate", "Bzip2", "Imploded 4K/2"...);
     * NULL selects "Deflate"/"Store" from archive_compressed. */
    const char *archive_method;
} xx_scan_options;
typedef struct xx_scan_options xx_scan_options_t;

/* --------------------------------------------------------------- results  */

/**
 * @brief One detection, exposed in the same form by every engine.
 *
 * Strings may be NULL. Offsets are absolute in the input device, except for
 * compressed archive children whose offsets are relative to decoded data; size may
 * be -1 when unknown. The record and its strings belong to the result and
 * remain valid until that result is freed.
 */
typedef struct xx_scan_record {
    /** Concrete engine that produced this record. */
    uint32_t engine_id;
    xx_file_type_t file_type;
    int64_t offset;
    int64_t size;
    const char *type;
    const char *name;
    const char *version;
    const char *info;
    int priority;
    bool is_heuristic;
    bool is_aggressive_heuristic;
    bool is_unknown;
    uint64_t scan_id;   /**< options.scan_id of the scan that produced this record. */
    uint64_t parent_id; /**< options.parent_id of that scan: 0 for the input itself. */
    /** Stable //fmt[...] identifier for bundled Binary/audio.1.sg results.
     * NULL for other scripts and untagged results. Owned by the result. */
    const char *format_id;
    xx_scan_part scan_part; /**< options.scan_part: input, overlay, resource or archive member. */
    const char *archive_member; /**< Owned member name; NULL outside archive children. */
    int64_t archive_offset; /**< Physical packed member/stream start in the containing input. */
    int64_t archive_size; /**< Physical packed member/stream size. */
    bool archive_compressed; /**< offset/size describe decoded bytes when true. */
    /** Optional owned engine-specific source identifier. For DiE this is the
     * database path of the script that produced the record. */
    const char *source_ref;
    /** options.archive_method: a static string, never freed or copied; NULL
     * selects "Deflate"/"Store" from archive_compressed. */
    const char *archive_method;
} xx_scan_record;
typedef struct xx_scan_record xx_scan_record_t;

/**
 * @brief Standard result storage used by signature scanners and formatters.
 *
 * Zero-initialise before scanning. Owns its arrays, record strings, errors
 * and file name; release them with xx_scan_result_free(). Every engine uses
 * this model. Result and record file types use xx_file_type_t.
 */
typedef struct xx_scan_result {
    /** Concrete engine selected for this result. */
    uint32_t engine_id;
    xx_scan_record *pRecords;
    int nCount;
    int nCapacity;
    char **ppErrors;
    int nErrorCount;
    xx_file_type_t file_type;
    /** Deprecated engine-specific type identifier retained for compatibility
     * adapters. Common traversal and writers never interpret it. */
    uint32_t compatibility_file_type;
    char *pFileName;
    int64_t nFileSize;
    bool sort; /**< options.sort of the scan, set by signature scans. Each pass sorted its own records; merging never re-sorts. */
} xx_scan_result;
typedef struct xx_scan_result xx_scan_result_t;

/** Release standard result contents and clear the struct; does not free the struct. */
XXFC_API void xx_scan_result_free(xx_scan_result *pResult);

/** Append an owned copy of a diagnostic to standard result storage.
 * Returns false for invalid arguments or allocation failure, leaving the result
 * unchanged and setting pd's error when supplied. text must not be NULL. */
XXFC_API bool xx_scan_result_add_error(xx_scan_result *result, const char *text, xx_pd_struct *pd);

/** Sort standard records by ascending priority, preserving equal-priority order.
 * NULL storage or fewer than two records is ignored. */
XXFC_API void xx_scan_sort_records(xx_scan_record *records, int count);

/* Format standard result storage. Returned strings are runtime-allocated. */
XXFC_API char *xx_scan_format_text(xx_scan_result *pResult, xx_scan_options *pOptions);
XXFC_API char *xx_scan_format_json(xx_scan_result *pResult, xx_scan_options *pOptions);
XXFC_API char *xx_scan_format_xml(xx_scan_result *pResult, xx_scan_options *pOptions);
XXFC_API char *xx_scan_format_csv(xx_scan_result *pResult, xx_scan_options *pOptions, char nSeparator);

XXFC_API int xx_scan_type_to_prio(const char *pType);
XXFC_API char *xx_scan_translate_type(const char *pType);

/** Largest COM image after the 0x100-byte program segment prefix. */
#define XX_SCAN_COM_MAX_SIZE (0x10000 - 0x100)

typedef struct xx_scan_engine xx_scan_engine;
typedef struct xx_scan_engine xx_scan_engine_t;

/** No automatic or multi-engine selector exists: one concrete ID is chosen. */
typedef enum xx_scan_engine_id {
    XX_SCAN_ENGINE_UNKNOWN = 0,
    XX_SCAN_ENGINE_DIE = 1,
    XX_SCAN_ENGINE_NFD = 2,
    XX_SCAN_ENGINE_YARA = 3,
    XX_SCAN_ENGINE_PEID = 4
} xx_scan_engine_id;

#define XX_SCAN_ENGINE_ABI_VERSION UINT32_C(1)

typedef enum xx_scan_engine_capability {
    XX_SCAN_CAP_BUILTIN_SIGNATURES = UINT64_C(1) << 0,
    XX_SCAN_CAP_EXTERNAL_DATABASE_REQUIRED = UINT64_C(1) << 1,
    XX_SCAN_CAP_EXTERNAL_DATABASE_OPTIONAL = UINT64_C(1) << 2,
    XX_SCAN_CAP_TAGS = UINT64_C(1) << 3,
    XX_SCAN_CAP_DEEP = UINT64_C(1) << 4,
    XX_SCAN_CAP_HEURISTIC = UINT64_C(1) << 5,
    XX_SCAN_CAP_AGGRESSIVE = UINT64_C(1) << 6,
    XX_SCAN_CAP_VERBOSE = UINT64_C(1) << 7,
    XX_SCAN_CAP_FIRST_WRAPPER = UINT64_C(1) << 8,
    XX_SCAN_CAP_TYPE_QUERY = UINT64_C(1) << 9,
    XX_SCAN_CAP_THREAD_SAFE = UINT64_C(1) << 10,
    XX_SCAN_CAP_REENTRANT = UINT64_C(1) << 11
} xx_scan_engine_capability;

/** One bounded, resolved pass. Concrete engines must not traverse children. */
typedef struct xx_scan_region {
    xx_io_device *device;
    xx_file_type_t file_type;
    int64_t absolute_offset;
    int64_t size;
    uint64_t scan_id;
    uint64_t parent_id;
    xx_scan_part part;
    unsigned int archive_depth;
} xx_scan_region;

typedef struct xx_scan_catalog {
    char **items;
    size_t count;
} xx_scan_catalog;

typedef struct xx_scan_engine_ops {
    uint32_t abi_version;
    size_t struct_size;
    xx_scan_engine_id id;
    const char *name;
    const char *display_name;
    uint64_t capabilities;
    bool (*create)(xx_scan_engine *engine, const void *config, xx_pd_struct *pd);
    void (*destroy)(xx_scan_engine *engine);
    bool (*load_database)(xx_scan_engine *engine, const void *database, xx_pd_struct *pd);
    bool (*supports_type)(const xx_scan_engine *engine, xx_file_type_t file_type);
    xx_scan_result *(*scan_region)(xx_scan_engine *engine, const xx_scan_region *region,
                                  const xx_scan_options *options, xx_pd_struct *pd);
    bool (*list_signatures)(const xx_scan_engine *engine, xx_scan_catalog *catalog, xx_pd_struct *pd);
    bool (*list_tags)(const xx_scan_engine *engine, xx_scan_catalog *catalog, xx_pd_struct *pd);
} xx_scan_engine_ops;

struct xx_scan_engine {
    const xx_scan_engine_ops *ops;
    void *context;
};

XXFC_API bool xx_scan_engine_init(xx_scan_engine *engine, const xx_scan_engine_ops *ops,
                                  const void *config, const void *database, xx_pd_struct *pd);
XXFC_API xx_scan_engine_id xx_scan_engine_get_id(const xx_scan_engine *engine);
XXFC_API const char *xx_scan_engine_get_name(const xx_scan_engine *engine);
XXFC_API const char *xx_scan_engine_get_display_name(const xx_scan_engine *engine);
XXFC_API uint64_t xx_scan_engine_get_capabilities(const xx_scan_engine *engine);
XXFC_API bool xx_scan_engine_supports_type(const xx_scan_engine *engine, xx_file_type_t file_type);
XXFC_API void xx_scan_catalog_free(xx_scan_catalog *catalog);

/** Defaults: whole input, automatic file type, optional scan modes disabled;
 * type/version/info display, custom database and sorting enabled; the
 * deprecated extra database disabled. */
XXFC_API void xx_scan_options_init(xx_scan_options *options);

/** Free owned database paths and clear the options. Borrowed pointers are not freed.
 * Safe for NULL or already-cleared options. */
XXFC_API void xx_scan_options_free(xx_scan_options *options);

/**
 * Detect supported file types as a list of xx_file_type_t, generic first and
 * preferred last. A PE32 image returns BINARY, MSDOS, PE, PE32; an APK returns
 * BINARY, ZIP, JAR, APK. A managed image adds DOTNET after its actual PE32/PE64
 * carrier. Unsupported inputs
 * return a one-element BINARY list. COM is only selected for a plausible
 * small file with a .com options.file_name.
 * Detection is confined to options.offset/size when options is supplied.
 * The borrowed device is not closed; its cursor is preserved when tell/seek
 * are supported. Invalid arguments, allocation failure or cancellation return
 * NULL; pd receives an error for invalid arguments or allocation failure.
 * The caller owns the returned list and must destroy it with xx_list_destroy().
 */
XXFC_API xx_list_t *xx_scan_detect_type(xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd);

/**
 * Compatibility wrapper for callers that previously used the engine's type
 * callback. Returns xx_scan_detect_type()'s owned list; engine is unused.
 */
XXFC_API xx_list_t *xx_scan_get_file_types(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd);

/**
 * Locate a trailing overlay with the resolved format's reader. Detection is
 * confined to options.offset/size;
 * the returned offset is absolute in device. Formats without a known boundary
 * return true with offset=-1 and size=0. Output pointers are required.
 */
XXFC_API bool xx_scan_get_overlay(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, int64_t *offset, int64_t *size, xx_pd_struct *pd);

/**
 * Scan an existing device. NULL options selects defaults; pd may be NULL.
 * A non-memory input smaller than xx_get_file_buffer_size() is copied into a
 * temporary memory device before detection and scanning; it is freed before
 * return. Archive child scans retain their selected memory, temporary-file or
 * bounded source device and skip this eager input copy. Callbacks must not
 * retain a child device or its backing buffer.
 * UNKNOWN resolves through common type detection; an explicit type bypasses
 * detection. Every resolved pass invokes exactly one engine scan_region op.
 * all_types_scan adds parent passes: MSDOS for PE/NE/LE/LX; ZIP then JAR for
 * APK/IPA; ZIP for JAR; DOS16M for DOS4G. Each pass starts at options.offset
 * with its file_type and all_types_scan cleared. Common result merging keeps
 * pass order; all partial results are freed on failure or cancellation.
 * overlay_scan locates an overlay once for the preferred type, detects its
 * type, and recursively invokes the common scanner through a read-only
 * xx_io_sub device. The child starts with file_type UNKNOWN and recursive_scan,
 * overlay_scan, resources_scan and all_types_scan cleared; it keeps
 * archives_scan, so an archive overlay expands as in XScanEngine.
 * Merged offsets remain absolute in the original input. Concrete region ops
 * receive overlay_scan, resources_scan and archives_scan cleared.
 * resources_scan visits nonempty PE/.NET resources and ELF GResources once
 * for the preferred type. Payloads are scanned as stored, including compressed
 * GResources; malformed input or more than 65,536 resource leaves fails.
 * archives_scan visits the members of ZIP-family archives (ZIP, JAR, APK,
 * IPA), as XScanEngine expands ZIP, 7Z, RAR, CAB and ISO 9660 containers;
 * GZIP, BZIP2, LHA, Mach-O Fat and other inputs are not expanded. Like
 * XScanEngine, the walk decodes or checksums every member it reaches; a
 * member that fails its CRC, uses a ZIP method XZip does not map, does not
 * decode, is encrypted or exceeds an archive limit is skipped, and the
 * first two leave a "Last error: Invalid CRC" or "Last error: Unknown
 * compression method: Unknown" record in the result of the scan that
 * finishes next (the archive's, or a later member's), as XScanEngine's single
 * progress error string does; the scan still succeeds. A ZIP without end
 * records is read from its local headers. Without aggressive_scan a member is
 * scanned only when XScanEngine::isScanable holds (executables, DEX, PDF,
 * archives, TAR); at most 21 members per archive are scanned (100,000 when
 * aggressive). Compressed output stays in memory only while its decoded size
 * is less than xx_get_file_buffer_size(); a member declared at that size or
 * larger is decoded into an xx_io_temp_open() file, with automatic cleanup
 * after the child scan; a member that is never scannable is decoded into
 * nothing. As in XScanEngine, a member that needs a temporary file and cannot
 * have one is skipped silently, undecoded (an unmapped ZIP method is then not
 * reported either); so is one whose declared size does not fit on the
 * temporary volume (from 16 MiB on, with 1 GiB left free there), or whose
 * temporary file cannot be written, read back or closed. Temporary streams
 * respect an active memory-only policy. ZIP Store members use bounded xx_io_sub views
 * of the source after their CRC is verified. No archive-derived output paths
 * are opened. The storage threshold does not bound decoder-internal buffers.
 * Children get distinct IDs and scan_part RESOURCE or ARCHIVE. A member name
 * is supplied through file_name and archive_member. Resource types are passed
 * through resource_type. At most 21 resources are scanned (2001 when
 * aggressive). Child traversal flags are cleared, so a child's own resources
 * and overlay are not traversed (XScanEngine finds file parts only for the
 * input's type), but resource, overlay and archive member children keep
 * archives_scan, as in XScanEngine: nested archives expand, down to 20 archive
 * levels below the input. As a deliberate cdie safety limit that XScanEngine
 * does not have, at most 10,000 archive members are scanned for one input
 * (100,000 when aggressive), all nesting levels together; reaching it stops
 * the expansion and leaves "Last error: Archive member scan limit reached
 * (10000)" (or "(100000)") in the result. Input passes run first, then
 * resource, archive and overlay children. Unsupported container formats have
 * no corresponding child pass. Partial results are freed on any failure;
 * only faults reading the input, allocation failures and cancellation fail an
 * archive expansion.
 * Each scan of a device draws a new random nonzero options.scan_id; all of
 * its passes share it. The input's scan has parent_id 0, a child's has
 * the input scan's scan_id. Caller-supplied IDs and child metadata are ignored.
 * The device stays open. NULL is returned for invalid arguments, incomplete
 * engine operations, engine failure, or cancellation. Validation errors are
 * reported through pd when supplied.
 */
XXFC_API xx_scan_result *xx_scan(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd);

/** Compatibility name for xx_scan(). */
XXFC_API xx_scan_result *xx_scan_device(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd);

/** Open read-only, scan, then close. The path supplies options.file_name. */
XXFC_API xx_scan_result *xx_scan_file(xx_scan_engine *engine, const char *path, const xx_scan_options *options, xx_pd_struct *pd);

/** Scan a borrowed read-only buffer. NULL data is allowed only for size zero. */
XXFC_API xx_scan_result *xx_scan_memory(xx_scan_engine *engine, const void *data, size_t size, const xx_scan_options *options, xx_pd_struct *pd);

/**
 * Access the standard result model. engine is accepted for source compatibility
 * and ignored. NULL results yield zero/NULL.
 */
XXFC_API size_t xx_scan_get_record_count(xx_scan_engine *engine, const xx_scan_result *result);
XXFC_API const xx_scan_record *xx_scan_get_record(xx_scan_engine *engine, const xx_scan_result *result, size_t index);

/**
 * Deep-copy standard source records and diagnostics into destination, adding
 * source_offset to record offsets. Neither result is freed or borrowed from
 * the other. Preserve record IDs and destination file name/sort policy;
 * replace destination file_type/nFileSize only when source_offset is zero.
 * Source records are appended after the destination's, in source order; the
 * merged list is never re-sorted, whatever destination.sort says. As in
 * XScanEngine, each pass sorts its own records, so passes, overlays,
 * resources and archive members keep their own order.
 * Return false on invalid arguments, offset overflow, allocation failure or
 * cancellation; destination remains safe to free. pd may be NULL.
 * engine is accepted for source compatibility and may be NULL.
 */
XXFC_API bool xx_scan_append_result(xx_scan_engine *engine, xx_scan_result *destination, const xx_scan_result *source,
                                  int64_t source_offset, xx_pd_struct *pd);

/**
 * Release the standard model's contents and its owning allocation. engine is
 * accepted for source compatibility and ignored. For caller-owned storage use
 * xx_scan_result_free(). NULL results are ignored.
 */
XXFC_API void xx_scan_free_result(xx_scan_engine *engine, xx_scan_result *result);

/**
 * Invoke the concrete engine's destroy op and clear the caller-owned engine
 * handle. NULL or already-cleared engines are ignored.
 */
XXFC_API void xx_scan_engine_cleanup(xx_scan_engine *engine);

#ifdef __cplusplus
}
#endif

#endif /* XX_SCAN_H */
