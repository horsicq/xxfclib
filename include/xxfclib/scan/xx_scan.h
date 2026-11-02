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
 * @brief File types, signature databases, scan models and engine callbacks.
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

/**
 * @brief What a file was detected as.
 *
 * The same numbering the signature databases use, so a database's declared
 * type can be compared against a detected one directly.
 */
typedef enum {
    XFT_UNKNOWN = 0,
    XFT_BINARY,
    XFT_COM,
    XFT_MSDOS,
    XFT_NE,
    XFT_LE,
    XFT_LX,
    XFT_PE,
    XFT_PE32,
    XFT_PE64,
    XFT_ELF,
    XFT_ELF32,
    XFT_ELF64,
    XFT_MACHO,
    XFT_MACHO32,
    XFT_MACHO64,
    XFT_ZIP,
    XFT_JAR,
    XFT_APK,
    XFT_IPA,
    XFT_DEX,
    XFT_NPM,
    XFT_MACHOFAT,
    XFT_ARCHIVE,
    XFT_PDF,
    XFT_CFBF,
    XFT_IMAGE,
    XFT_JPEG,
    XFT_PNG,
    XFT_RAR,
    XFT_ISO9660,
    XFT_AMIGAHUNK,
    XFT_ATARIST,
    XFT_JAVACLASS,
    XFT_PYC,
    XFT_DOS16M,
    XFT_DOS4G,
    XFT_CLI_ASSEMBLY,
    XFT_COUNT
} XFileType;

/** @brief The human-readable name of a type, as printed. */
XXFC_API const char *xft_to_string(XFileType type);

/**
 * @brief Does a database's declared file type match a detected one?
 *
 * Signature databases declare a broad type (XFT_PE) and files detect as a
 * specific one (XFT_PE64); this is the rule that decides which scripts run
 * against a file, so a caller driving the engine needs it to predict or
 * filter that set. Mirrors XBinary::checkFileType in the reference.
 */
XXFC_API int xft_check(XFileType databaseType, XFileType fileType);

/* Signature database models. Release loaded records with db_free(). */
typedef enum {
    DB_MAIN = 0,
    /** Deprecated: reserved so DB_CUSTOM stays 2. No cdie frontend loads an
     * extra database any more (Detect It Easy uses only main and custom). */
    DB_EXTRA,
    DB_CUSTOM
} DBKind;

typedef struct {
    char *pName;     /* file name including the extension, e.g. "_PE.0.sg" */
    char *pFilePath; /* absolute path                                      */
    char *pText;     /* script source or precompiled bytecode              */
    size_t nSize;    /* size of pText / bytecode in bytes                  */
    XFileType fileType;
    DBKind databaseType;
    int64_t nElapsedTime; /* elapsed execution time in ms (when profiling/scan-time enabled) */
    char *pTags;          /* owned, normalized semicolon-separated metadata tags             */
} DBSignature;

/** Retained metadata entry. Keeping the catalog on DBase lets metadata loaded
 * with the main database also tag records added later from db_custom. */
typedef struct {
    char *pName;
    char *pTags;
    XFileType fileType;
    unsigned int databaseKinds; /**< Bit set for each DBKind described by this entry. */
} DBSignatureMetadata;

typedef struct {
    DBSignature *pRecords;
    int nCount;
    int nCapacity;
    DBSignatureMetadata *pMetadata;
    int nMetadataCount;
    int nMetadataCapacity;
    /* Private retained JSProgramCache (64 MiB), created by successful loaders.
     * Hand-built databases may leave this NULL. Database loading/freeing
     * require exclusive ownership; concurrent scans share immutable programs. */
    void *pJsProgramCache;
} DBase;

XXFC_API int db_load(DBase *pDb, const char *pPath, DBKind kind);
XXFC_API int db_load_tar(DBase *pDb, const char *pTarPath, DBKind kind);
/** Load the bundled Detect It Easy db/Binary/audio* rules and their helpers.
 * This provides named audio/tracker identification without a separate DIE DB.
 * Like db_load(), call db_free() when finished. */
XXFC_API int db_load_builtin_audio(DBase *pDb, DBKind kind);
/** Load only the framework/helper scripts and Binary/audio.1.sg tracker and
 * chiptune rules from the bundled audio database. This avoids executing the
 * separate voice/container rules when identifying music families. */
XXFC_API int db_load_builtin_music(DBase *pDb, DBKind kind);
XXFC_API int db_create_tar(const char *pDbPath, const char *pTarPath);
XXFC_API int db_create_tar_precompiled(const char *pDbPath, const char *pTarPath);
XXFC_API void db_sort(DBase *pDb);
XXFC_API void db_free(DBase *pDb);
XXFC_API int db_count_for_type(DBase *pDb, XFileType fileType);
/** Exact ASCII case-insensitive membership in a signature's tag list. */
XXFC_API int db_signature_has_tag(const DBSignature *signature, const char *tag);
/** Apply semicolon-separated include/exclude lists. Include is any-match;
 * exclude is any-match and takes precedence. Empty lists impose no rule. */
XXFC_API int db_signature_matches_tag_filters(const DBSignature *signature,
                                              const char *include_tags,
                                              const char *exclude_tags);
/**
 * Return every distinct tag attached to a loaded signature, sorted
 * case-insensitively and joined with ';' (without a trailing separator).
 * Tag identity is ASCII case-insensitive; the spelling from the first loaded
 * signature that uses a tag is preserved. Metadata entries that are not
 * attached to a loaded signature are not included. The caller owns the
 * returned string and must release it with xx_rt_free(). An empty database
 * returns an allocated empty string. NULL is returned only on allocation
 * failure.
 */
XXFC_API char *db_get_used_tags(const DBase *database);

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
    /** Owned database path of the script whose result this is (its file, a TAR
     * member path or "Binary/audio.1.sg"). NULL for synthesized records such
     * as Unknown. Frontends use it to show the signature. */
    const char *signature;
    /** options.archive_method: a static string, never freed or copied; NULL
     * selects "Deflate"/"Store" from archive_compressed. */
    const char *archive_method;
} xx_scan_record;
typedef struct xx_scan_record xx_scan_record_t;

/**
 * @brief Standard result storage used by signature scanners and formatters.
 *
 * Zero-initialise before scanning. Owns its arrays, record strings, errors
 * and file name; release them with xx_scan_result_free(). Engine callbacks
 * may instead manage private result storage, which must only be accessed
 * and freed through that engine. fileType uses the signature XFileType enum;
 * each record's file_type uses xx_file_type_t.
 */
typedef struct xx_scan_result {
    xx_scan_record *pRecords;
    int nCount;
    int nCapacity;
    char **ppErrors;
    int nErrorCount;
    XFileType fileType;
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

typedef struct xx_scan_file_type_set {
    bool types[XFT_COUNT];
} xx_scan_file_type_set;

/**
 * Detect supported signature file types using a borrowed seekable device.
 * file_name may be NULL; it supplies the optional COM suffix. The set includes
 * generic parent types and .NET membership. The device is not closed and its
 * cursor is preserved when the device supports tell/seek. Returns false for
 * a missing device/set or failed detection.
 */
XXFC_API bool xx_scan_file_type_set_detect(xx_io_device *device, const char *file_name, xx_scan_file_type_set *set);

XXFC_API bool xx_scan_file_type_set_contains(const xx_scan_file_type_set *set, XFileType type);

/**
 * Select the preferred signature file type from a detected set. Managed PE
 * retains PE32/PE64 as its preferred type. Unrecognized inputs fall back to
 * COM only for a known size no larger than XX_SCAN_COM_MAX_SIZE and a COM
 * suffix, otherwise BINARY. NULL device/set returns BINARY. No reads or seeks
 * are performed; the borrowed device and set remain unchanged.
 */
XXFC_API XFileType xx_scan_pick_file_type(xx_io_device *device, const char *file_name, const xx_scan_file_type_set *set);

typedef struct xx_scan_engine xx_scan_engine;
typedef struct xx_scan_engine xx_scan_engine_t;

/** A specialized scanner for one resolved file type. */
typedef xx_scan_result *(*xx_scan_format_callback)(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd);

typedef struct xx_scan_format_handler {
    xx_file_type_t file_type;
    xx_scan_format_callback callback;
} xx_scan_format_handler;

/**
 * @brief Abstract scanning engine, implemented with callbacks.
 *
 * Zero-initialise before installing callbacks. get_record_count, get_record
 * and free_result are required. Engines using the standard xx_scan_result
 * model can install xx_scan_get_record_count, xx_scan_get_record and
 * xx_scan_free_result and xx_scan_append_result directly. get_file_types is
 * required when options.file_type is UNKNOWN. Each supported type prefers its dedicated
 * scan_* callback. PE32/PE64 share scan_pe, ELF32/ELF64 share scan_elf, and
 * MACHO32/MACHO64 share scan_macho. A PE scan requires scan_pe; other types
 * fall back to a matching format_handlers entry, then scan_device. cleanup is
 * optional. Every result must be accessed and freed with the engine that
 * created it, before cleanup.
 * The caller owns this struct; priv holds implementation-specific state.
 */
struct xx_scan_engine {
    /**
     * Scan synchronously. Return a result even when no detections were found;
     * NULL means failure or cancellation. Report errors through optional pd,
     * update progress and check pd->is_stop during long operations.
     * The device and options belong to the caller. Do not close the device or
     * retain it, the options, or input buffers after this call returns.
     * Scanning may change the device position.
     */
    xx_scan_result *(*scan_device)(xx_scan_engine *self, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd);

    size_t (*get_record_count)(xx_scan_engine *self, const xx_scan_result *result);

    /** Return NULL for an index outside [0, get_record_count()). */
    const xx_scan_record *(*get_record)(xx_scan_engine *self, const xx_scan_result *result, size_t index);

    void (*free_result)(xx_scan_engine *self, xx_scan_result *result);

    /** Release private state without freeing self. */
    void (*cleanup)(xx_scan_engine *self);

    const char *name; /**< Optional engine name; borrowed. */
    void *priv;

    /**
     * Detect types for an UNKNOWN request. Return an owned xx_list_t of
     * xx_file_type_t values, ordered from generic to preferred (last).
     * The caller destroys the list. Return NULL on failure; use pd for errors.
     * The device is borrowed and may be repositioned.
     */
    xx_list_t *(*get_file_types)(xx_scan_engine *self, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd);

    /** Common handler for PE32 and PE64. options.file_type preserves the width. */
    xx_scan_result *(*scan_pe)(xx_scan_engine *self, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd);

    /** Dedicated PDF and DEX handlers. Same contract as scan_device. */
    xx_scan_result *(*scan_pdf)(xx_scan_engine *self, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd);
    xx_scan_result *(*scan_dex)(xx_scan_engine *self, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd);

    /** Borrowed fallback table. First matching non-NULL callback wins. */
    const xx_scan_format_handler *format_handlers;
    size_t format_handler_count;

    /** Dedicated handlers share the ownership and result contract of scan_device.
     * The resolved file type is supplied in options.file_type. */
    xx_scan_format_callback scan_binary;
    xx_scan_format_callback scan_elf;   /**< ELF32 and ELF64. */
    xx_scan_format_callback scan_macho; /**< MACHO32 and MACHO64. */
    xx_scan_format_callback scan_machofat;
    xx_scan_format_callback scan_java_class;
    xx_scan_format_callback scan_zip;
    xx_scan_format_callback scan_jar;
    xx_scan_format_callback scan_npm;
    xx_scan_format_callback scan_ipa;
    xx_scan_format_callback scan_iso9660;
    xx_scan_format_callback scan_apk;
    xx_scan_format_callback scan_amigahunk;
    xx_scan_format_callback scan_atarist;
    xx_scan_format_callback scan_cfbf;
    xx_scan_format_callback scan_com;
    xx_scan_format_callback scan_dos16m;
    xx_scan_format_callback scan_dos4g;
    xx_scan_format_callback scan_jpeg;
    xx_scan_format_callback scan_png;
    xx_scan_format_callback scan_ne;
    xx_scan_format_callback scan_le;
    xx_scan_format_callback scan_lx;
    xx_scan_format_callback scan_rar;

    /** MSDOS parent scan for PE32/PE64, NE, LE and LX. */
    xx_scan_format_callback scan_msdos;

    /**
     * Append all source records to destination. Required when all_types_scan
     * selects multiple passes or an overlay/resource/archive child is scanned. Add source_offset to
     * each copied record's offset (zero for passes on the same device), and
     * keep its scan_id, parent_id, scan_part and child metadata. Compressed
     * archive children use source_offset zero so decoded positions stay
     * relative to the member; their archive_offset is already physical in
     * the containing input and must not be rebased. Child metadata must not
     * replace the containing input's type or size, even for offset zero.
     * Both handles are borrowed; neither may be freed
     * here. Source is freed immediately after this call, so copy its records
     * and strings into destination-owned storage. Return false on failure and
     * report the error through pd. Destination must remain safe to free after
     * failure. Each scan callback must return a distinct owned result.
     * Standard results can use xx_scan_append_result directly.
     */
    bool (*append_result)(xx_scan_engine *self, xx_scan_result *destination, const xx_scan_result *source, int64_t source_offset, xx_pd_struct *pd);

    /**
     * Optional overlay locator; NULL uses xx_scan_get_overlay. Return true
     * with offset=-1 and size=0 when none exists, false on failure. A nonempty
     * range must lie within options.offset/size and start after options.offset.
     * Offsets are absolute in device. The device and options are borrowed.
     */
    bool (*get_overlay)(xx_scan_engine *self, xx_io_device *device, const xx_scan_options *options, int64_t *offset, int64_t *size, xx_pd_struct *pd);
};

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
 * Adapter for xx_scan_engine.get_file_types. Returns xx_scan_detect_type()'s
 * owned list; engine is unused. Can be installed directly as the callback.
 */
XXFC_API xx_list_t *xx_scan_get_file_types(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd);

/**
 * Locate a trailing overlay with the resolved format's reader. Matches the
 * engine.get_overlay callback. Detection is confined to options.offset/size;
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
 * UNKNOWN resolves through get_file_types; an explicit type bypasses detection.
 * PE32/PE64 use scan_pe, ELF32/ELF64 use scan_elf, and MACHO32/MACHO64 use
 * scan_macho. Other types prefer their dedicated scan_* callback. Non-PE
 * types fall back to a matching format_handlers entry, then scan_device.
 * all_types_scan adds parent passes: MSDOS for PE/NE/LE/LX; ZIP then JAR for
 * APK/IPA; ZIP for JAR; DOS16M for DOS4G. Each pass starts at options.offset
 * with its file_type and all_types_scan cleared. append_result merges results
 * in pass order; all partial results are freed on failure or cancellation.
 * overlay_scan locates an overlay once for the preferred type, detects its
 * type, and recursively invokes the common scanner through a read-only
 * xx_io_sub device. The child starts with file_type UNKNOWN and recursive_scan,
 * overlay_scan, resources_scan and all_types_scan cleared; it keeps
 * archives_scan, so an archive overlay expands as in XScanEngine.
 * get_file_types and append_result are required when an overlay exists;
 * merged offsets remain absolute in the original input. Scan callbacks
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
 * no corresponding child pass. get_file_types and append_result are
 * required when a child exists. Partial results are freed on any failure;
 * only faults reading the input, allocation failures and cancellation fail an
 * archive expansion.
 * Each scan of a device draws a new random nonzero options.scan_id; all of
 * its passes share it. The input's scan has parent_id 0, a child's has
 * the input scan's scan_id. Caller-supplied IDs and child metadata are ignored.
 * The device stays open. NULL is returned for invalid arguments, incomplete
 * callback tables, engine failure, or cancellation. Validation errors are
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
 * Dispatch result access through the engine's callbacks. These functions may
 * also be installed directly as callbacks for the standard xx_scan_result
 * model. NULL engines/results or missing access callbacks yield zero/NULL.
 */
XXFC_API size_t xx_scan_get_record_count(xx_scan_engine *engine, const xx_scan_result *result);
XXFC_API const xx_scan_record *xx_scan_get_record(xx_scan_engine *engine, const xx_scan_result *result, size_t index);

/**
 * Deep-copy standard source records and diagnostics into destination, adding
 * source_offset to record offsets. Neither result is freed or borrowed from
 * the other. Preserve record IDs and destination file name/sort policy;
 * replace destination fileType/nFileSize only when source_offset is zero.
 * Source records are appended after the destination's, in source order; the
 * merged list is never re-sorted, whatever destination.sort says. As in
 * XScanEngine, each pass sorts its own records, so passes, overlays,
 * resources and archive members keep their own order.
 * Return false on invalid arguments, offset overflow, allocation failure or
 * cancellation; destination remains safe to free. pd may be NULL.
 * May be installed as append_result; engine is unused and may be NULL.
 * Private result models must use their own engine's append_result callback.
 */
XXFC_API bool xx_scan_append_result(xx_scan_engine *engine, xx_scan_result *destination, const xx_scan_result *source,
                                  int64_t source_offset, xx_pd_struct *pd);

/**
 * Dispatch result destruction through the engine's callback. When installed
 * directly, release the standard model's contents and its owning allocation
 * with xx_rt_free; the model must be the allocation's first member.
 * NULL engines/results or missing callbacks are ignored. Use the same engine
 * that created the result. For caller-owned storage use xx_scan_result_free.
 */
XXFC_API void xx_scan_free_result(xx_scan_engine *engine, xx_scan_result *result);

/**
 * Invoke custom cleanup and clear the callback table. This function may be
 * installed directly when private state is borrowed and needs no release.
 * The engine struct is caller-owned; NULL is ignored.
 */
XXFC_API void xx_scan_engine_cleanup(xx_scan_engine *engine);

#ifdef __cplusplus
}
#endif

#endif /* XX_SCAN_H */
