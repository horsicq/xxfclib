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
 * @brief Common options and callback interface for scanning engines.
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

typedef struct xx_scan_engine xx_scan_engine;
typedef struct xx_scan_engine xx_scan_engine_t;

/** Opaque handle to engine-owned result storage. */
typedef struct xx_scan_result xx_scan_result;
typedef struct xx_scan_result xx_scan_result_t;

/**
 * @brief Options shared by all scanning engines.
 *
 * Initialise with xx_scan_options_init(). All pointers are borrowed for the
 * duration of the scan. An engine must report XXFC_ERR_INVALID_ARG through
 * pd if a requested scan mode is unsupported, rather than silently ignore it.
 */
typedef struct xx_scan_options {
    int64_t offset;                 /**< Start in the input device, in bytes. */
    int64_t size;                   /**< Bytes to scan; -1 means through EOF. */
    xx_file_type_t file_type;       /**< UNKNOWN requests automatic detection. */
    bool deep_scan;
    bool heuristic_scan;
    bool aggressive_scan;
    bool recursive_scan;
    bool overlay_scan;            /**< Detect and scan trailing overlays as bounded devices. */
    bool resources_scan;
    bool archives_scan;
    bool all_types_scan;           /**< Scan applicable parent formats before the preferred type. */
    bool first_wrapper_only;
    bool verbose;
    const char *file_name;          /**< Optional input name or path. */
    const void *engine_options;     /**< Optional engine-specific options. */
} xx_scan_options;
typedef struct xx_scan_options xx_scan_options_t;

/**
 * @brief One detection, exposed in the same form by every engine.
 *
 * Strings may be NULL. Offsets are absolute in the input device; size may
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
} xx_scan_record;
typedef struct xx_scan_record xx_scan_record_t;

/** A specialized scanner for one resolved file type. */
typedef xx_scan_result *(*xx_scan_format_callback)(
    xx_scan_engine *engine, xx_io_device *device,
    const xx_scan_options *options, xx_pd_struct *pd);

typedef struct xx_scan_format_handler {
    xx_file_type_t file_type;
    xx_scan_format_callback callback;
} xx_scan_format_handler;

/**
 * @brief Abstract scanning engine, implemented with callbacks.
 *
 * Zero-initialise before installing callbacks. get_record_count, get_record
 * and free_result are required. get_file_types is required when
 * options.file_type is UNKNOWN. Each supported type prefers its dedicated
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
    xx_scan_result *(*scan_device)(xx_scan_engine *self, xx_io_device *device,
                                  const xx_scan_options *options, xx_pd_struct *pd);

    size_t (*get_record_count)(xx_scan_engine *self, const xx_scan_result *result);

    /** Return NULL for an index outside [0, get_record_count()). */
    const xx_scan_record *(*get_record)(xx_scan_engine *self,
                                       const xx_scan_result *result, size_t index);

    void (*free_result)(xx_scan_engine *self, xx_scan_result *result);

    /** Release private state without freeing self. */
    void (*cleanup)(xx_scan_engine *self);

    const char *name;              /**< Optional engine name; borrowed. */
    void *priv;

    /**
     * Detect types for an UNKNOWN request. Return an owned xx_list_t of
     * xx_file_type_t values, ordered from generic to preferred (last).
     * The caller destroys the list. Return NULL on failure; use pd for errors.
     * The device is borrowed and may be repositioned.
     */
    xx_list_t *(*get_file_types)(xx_scan_engine *self, xx_io_device *device,
                                const xx_scan_options *options, xx_pd_struct *pd);

    /** Common handler for PE32 and PE64. options.file_type preserves the width. */
    xx_scan_result *(*scan_pe)(xx_scan_engine *self, xx_io_device *device,
                               const xx_scan_options *options, xx_pd_struct *pd);

    /** Dedicated PDF and DEX handlers. Same contract as scan_device. */
    xx_scan_result *(*scan_pdf)(xx_scan_engine *self, xx_io_device *device,
                                const xx_scan_options *options, xx_pd_struct *pd);
    xx_scan_result *(*scan_dex)(xx_scan_engine *self, xx_io_device *device,
                                const xx_scan_options *options, xx_pd_struct *pd);

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
     * selects multiple passes or an overlay is scanned. Add source_offset to
     * each copied record's offset (zero for passes on the same device).
     * Both handles are borrowed; neither may be freed
     * here. Source is freed immediately after this call, so copy its records
     * and strings into destination-owned storage. Return false on failure and
     * report the error through pd. Destination must remain safe to free after
     * failure. Each scan callback must return a distinct owned result.
     */
    bool (*append_result)(xx_scan_engine *self, xx_scan_result *destination,
                          const xx_scan_result *source, int64_t source_offset,
                          xx_pd_struct *pd);

    /**
     * Optional overlay locator; NULL uses xx_scan_get_overlay. Return true
     * with offset=-1 and size=0 when none exists, false on failure. A nonempty
     * range must lie within options.offset/size and start after options.offset.
     * Offsets are absolute in device. The device and options are borrowed.
     */
    bool (*get_overlay)(xx_scan_engine *self, xx_io_device *device,
                        const xx_scan_options *options, int64_t *offset,
                        int64_t *size, xx_pd_struct *pd);
};

/** Defaults: whole input, automatic file type, all optional modes disabled. */
XXFC_API void xx_scan_options_init(xx_scan_options *options);

/**
 * Detect the scan engine's supported file types, generic first and preferred
 * last. This matches xx_scan_engine.get_file_types and can be installed there
 * directly (engine.get_file_types = xx_scan_get_file_types). Unsupported
 * inputs return a one-element BINARY list. COM is only
 * selected for a plausible small file with a .com options.file_name.
 * Detection is confined to options.offset/size when options is supplied.
 * The caller owns the returned list and must destroy it with xx_list_destroy().
 */
XXFC_API xx_list_t *xx_scan_get_file_types(xx_scan_engine *engine,
                                           xx_io_device *device,
                                           const xx_scan_options *options,
                                           xx_pd_struct *pd);

/**
 * Locate a trailing overlay with the resolved format's reader. Matches the
 * engine.get_overlay callback. Detection is confined to options.offset/size;
 * the returned offset is absolute in device. Formats without a known boundary
 * return true with offset=-1 and size=0. Output pointers are required.
 */
XXFC_API bool xx_scan_get_overlay(xx_scan_engine *engine, xx_io_device *device,
                                  const xx_scan_options *options, int64_t *offset,
                                  int64_t *size, xx_pd_struct *pd);

/**
 * Scan an existing device. NULL options selects defaults; pd may be NULL.
 * A non-memory input smaller than xx_get_file_buffer_size() is copied into a
 * temporary memory device before detection and scanning; it is freed before
 * return. Callbacks must not retain that device or its backing buffer.
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
 * overlay_scan, resources_scan, archives_scan and all_types_scan cleared.
 * get_file_types and append_result are required when an overlay exists;
 * merged offsets remain absolute in the original input. Scan callbacks
 * receive overlay_scan cleared.
 * The device stays open. NULL is returned for invalid arguments, incomplete
 * callback tables, engine failure, or cancellation. Validation errors are
 * reported through pd when supplied.
 */
XXFC_API xx_scan_result *xx_scan(xx_scan_engine *engine, xx_io_device *device,
                                const xx_scan_options *options, xx_pd_struct *pd);

/** Compatibility name for xx_scan(). */
XXFC_API xx_scan_result *xx_scan_device(xx_scan_engine *engine, xx_io_device *device,
                                       const xx_scan_options *options, xx_pd_struct *pd);

/** Open read-only, scan, then close. The path supplies options.file_name. */
XXFC_API xx_scan_result *xx_scan_file(xx_scan_engine *engine, const char *path,
                                     const xx_scan_options *options, xx_pd_struct *pd);

/** Scan a borrowed read-only buffer. NULL data is allowed only for size zero. */
XXFC_API xx_scan_result *xx_scan_memory(xx_scan_engine *engine, const void *data, size_t size,
                                       const xx_scan_options *options, xx_pd_struct *pd);

/** NULL engines/results or missing access callbacks yield zero/NULL. */
XXFC_API size_t xx_scan_get_record_count(xx_scan_engine *engine, const xx_scan_result *result);
XXFC_API const xx_scan_record *xx_scan_get_record(xx_scan_engine *engine,
                                                 const xx_scan_result *result, size_t index);

/** NULL result is ignored. Use the same engine that created the result. */
XXFC_API void xx_scan_free_result(xx_scan_engine *engine, xx_scan_result *result);

/** Release private state and clear the callback table. NULL is ignored. */
XXFC_API void xx_scan_engine_cleanup(xx_scan_engine *engine);

#ifdef __cplusplus
}
#endif

#endif /* XX_SCAN_H */
