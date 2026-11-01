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

#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xx_scan_resources_internal.h"
#include "xxfclib/formats/xx_format_stream_features.h"
#include "xx_scan_archives_internal.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#ifdef fileType
#undef fileType
#endif

/* Scan ids.
 *
 * An id only has to tell the scans of one process apart, and the CRT-free
 * Windows build has no random source beyond KERNEL32. So each id is a
 * bijective mix of (per-process seed << 32 | call counter): ids are unique
 * for 2^32 calls and look random. The seed is drawn once from the clock and
 * the address-space layout. Both words are updated with 32-bit atomics,
 * which every supported compiler and target provides without a library. */
static volatile long xx_scan_id_seed;
static volatile long xx_scan_id_counter;

static uint32_t xx_scan_atomic_increment(volatile long *value)
{
#if defined(_MSC_VER)
    return (uint32_t)_InterlockedIncrement(value);
#elif defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__atomic_add_fetch(value, 1, __ATOMIC_RELAXED);
#else
    return (uint32_t)++*value;
#endif
}

/* Stores desired when *value is 0; returns the value now in place. */
static uint32_t xx_scan_atomic_init(volatile long *value, long desired)
{
#if defined(_MSC_VER)
    long previous = _InterlockedCompareExchange(value, desired, 0);
    return (uint32_t)(previous ? previous : desired);
#elif defined(__GNUC__) || defined(__clang__)
    long expected = 0;
    if (__atomic_compare_exchange_n(value, &expected, desired, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return (uint32_t)desired;
    return (uint32_t)expected;
#else
    if (!*value) *value = desired;
    return (uint32_t)*value;
#endif
}

/* SplitMix64 finalizer: a bijection on 64-bit values. */
static uint64_t xx_scan_mix64(uint64_t value)
{
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

static uint64_t xx_scan_new_id(void)
{
    uint32_t seed = (uint32_t)xx_scan_id_seed;
    uint64_t id;
    if (!seed) {
        int marker = 0;
        uint64_t entropy = (uint64_t)xx_rt_clock_ms() ^ ((uint64_t)(uintptr_t)&xx_scan_id_counter << 20) ^ ((uint64_t)(uintptr_t)&marker << 7);
        uint32_t candidate = (uint32_t)(xx_scan_mix64(entropy) >> 32);
        seed = xx_scan_atomic_init(&xx_scan_id_seed, (long)(candidate ? candidate : 1u));
    }
    /* The seed is nonzero, so the mixer input is never 0 and, the mixer being
     * a bijection that fixes 0, neither is the id: 0 stays free for "no
     * parent". */
    id = xx_scan_mix64(((uint64_t)seed << 32) | xx_scan_atomic_increment(&xx_scan_id_counter));
    return id;
}

static xx_scan_format_callback xx_scan_select_callback(const xx_scan_engine *engine, xx_file_type_t type)
{
    xx_scan_format_callback callback = NULL;
    size_t index;
    switch (type) {
        case XX_FILE_TYPE_BINARY: callback = engine->scan_binary; break;
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
        case XX_FILE_TYPE_DOTNET: return engine->scan_pe;
        case XX_FILE_TYPE_ELF32:
        case XX_FILE_TYPE_ELF64: callback = engine->scan_elf; break;
        case XX_FILE_TYPE_MACHO32:
        case XX_FILE_TYPE_MACHO64: callback = engine->scan_macho; break;
        case XX_FILE_TYPE_MACHOFAT: callback = engine->scan_machofat; break;
        case XX_FILE_TYPE_JAVA_CLASS: callback = engine->scan_java_class; break;
        case XX_FILE_TYPE_ZIP:
        case XX_FILE_TYPE_ZIP64: callback = engine->scan_zip; break;
        case XX_FILE_TYPE_JAR: callback = engine->scan_jar; break;
        case XX_FILE_TYPE_NPM: callback = engine->scan_npm; break;
        case XX_FILE_TYPE_IPA: callback = engine->scan_ipa; break;
        case XX_FILE_TYPE_ISO9660: callback = engine->scan_iso9660; break;
        case XX_FILE_TYPE_APK: callback = engine->scan_apk; break;
        case XX_FILE_TYPE_AMIGAHUNK: callback = engine->scan_amigahunk; break;
        case XX_FILE_TYPE_ATARIST: callback = engine->scan_atarist; break;
        case XX_FILE_TYPE_CFBF: callback = engine->scan_cfbf; break;
        case XX_FILE_TYPE_COM: callback = engine->scan_com; break;
        case XX_FILE_TYPE_DEX: callback = engine->scan_dex; break;
        case XX_FILE_TYPE_DOS16M: callback = engine->scan_dos16m; break;
        case XX_FILE_TYPE_DOS4G: callback = engine->scan_dos4g; break;
        case XX_FILE_TYPE_JPEG: callback = engine->scan_jpeg; break;
        case XX_FILE_TYPE_PNG: callback = engine->scan_png; break;
        case XX_FILE_TYPE_PDF: callback = engine->scan_pdf; break;
        case XX_FILE_TYPE_NE: callback = engine->scan_ne; break;
        case XX_FILE_TYPE_LE: callback = engine->scan_le; break;
        case XX_FILE_TYPE_LX: callback = engine->scan_lx; break;
        case XX_FILE_TYPE_RAR: callback = engine->scan_rar; break;
        case XX_FILE_TYPE_MSDOS: callback = engine->scan_msdos; break;
        default: break;
    }
    if (callback) return callback;
    for (index = 0; index < engine->format_handler_count; ++index) {
        const xx_scan_format_handler *handler = &engine->format_handlers[index];
        if (handler->file_type == type && handler->callback) return handler->callback;
    }
    return engine->scan_device;
}

static size_t xx_scan_build_plan(const xx_scan_options *options, xx_file_type_t types[3])
{
    size_t count = 0;
    if (options->all_types_scan) {
        switch (options->file_type) {
            case XX_FILE_TYPE_PE32:
            case XX_FILE_TYPE_PE64:
            case XX_FILE_TYPE_DOTNET:
            case XX_FILE_TYPE_NE:
            case XX_FILE_TYPE_LE:
            case XX_FILE_TYPE_LX: types[count++] = XX_FILE_TYPE_MSDOS; break;
            case XX_FILE_TYPE_APK:
            case XX_FILE_TYPE_IPA:
                types[count++] = XX_FILE_TYPE_ZIP;
                types[count++] = XX_FILE_TYPE_JAR;
                break;
            case XX_FILE_TYPE_JAR: types[count++] = XX_FILE_TYPE_ZIP; break;
            case XX_FILE_TYPE_DOS4G: types[count++] = XX_FILE_TYPE_DOS16M; break;
            default: break;
        }
    }
    types[count++] = options->file_type;
    return count;
}

static xx_scan_result *xx_scan_run_plan(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, const xx_scan_result *forbidden_result,
                                        xx_pd_struct *pd)
{
    xx_file_type_t types[3];
    xx_scan_format_callback callbacks[3];
    size_t count = xx_scan_build_plan(options, types);
    size_t index;
    xx_scan_result *result = NULL;

    /* Validate the whole plan before running any scan callbacks. */
    for (index = 0; index < count; ++index) {
        callbacks[index] = xx_scan_select_callback(engine, types[index]);
        if (!callbacks[index]) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG,
                            types[index] == XX_FILE_TYPE_PE32 || types[index] == XX_FILE_TYPE_PE64 || types[index] == XX_FILE_TYPE_DOTNET ? "Missing PE scan callback"
                                                                                                                                          : "Missing scan callback");
            return NULL;
        }
    }
    if (count > 1 && !engine->append_result) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Missing scan result append callback");
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        xx_scan_options pass = *options;
        xx_scan_result *part;
        if (pd && pd->is_stop) goto failed;
        pass.file_type = types[index];
        pass.overlay_scan = false;
        pass.resources_scan = false;
        pass.archives_scan = false;
        if (options->all_types_scan) {
            pass.all_types_scan = false;
            if (xx_io_seek64(device, pass.offset, SEEK_SET) != 0) {
                xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot seek scan pass input");
                goto failed;
            }
        }
        part = callbacks[index](engine, device, &pass, pd);
        if (!part) {
            if (pd && !pd->last_error && !pd->is_stop) xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Scan callback failed");
            goto failed;
        }
        if (part == result || part == forbidden_result) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Scan callbacks reused a result");
            goto failed;
        }
        if (pd && pd->is_stop) {
            engine->free_result(engine, part);
            goto failed;
        }
        if (!result) {
            result = part;
        } else {
            bool appended = engine->append_result(engine, result, part, 0, pd);
            engine->free_result(engine, part);
            if (!appended || (pd && pd->is_stop)) {
                if (pd && !pd->last_error && !pd->is_stop) xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Cannot append scan result");
                goto failed;
            }
        }
    }
    return result;
failed:
    if (result) engine->free_result(engine, result);
    return NULL;
}

static bool xx_scan_validate(xx_scan_engine *engine, const xx_scan_options *options, xx_pd_struct *pd)
{
    if (!engine) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Scan engine is NULL");
        return false;
    }
    if (!engine->get_record_count || !engine->get_record || !engine->free_result) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Incomplete scan engine callbacks");
        return false;
    }
    if (engine->format_handler_count && !engine->format_handlers) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Missing format handler table");
        return false;
    }
    if (options && (options->offset < 0 || options->size < -1 || (options->size >= 0 && options->size > INT64_MAX - options->offset))) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan range");
        return false;
    }
    return !pd || !pd->is_stop;
}

static bool xx_scan_resolve_options(xx_scan_engine *engine, xx_io_device *device, xx_scan_options *options, xx_pd_struct *pd)
{
    if (options->file_type == XX_FILE_TYPE_UNKNOWN) {
        xx_list_t *types;
        if (!engine->get_file_types) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Missing scan file-type callback");
            return false;
        }
        types = engine->get_file_types(engine, device, options, pd);
        if (!types) {
            if (pd && !pd->last_error && !pd->is_stop) xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Scan file-type detection failed");
            return false;
        }
        if (types->elem_size != sizeof(xx_file_type_t) || types->count == 0 || !xx_list_get(types, types->count - 1, &options->file_type)) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan file-type list");
            xx_list_destroy(types);
            return false;
        }
        xx_list_destroy(types);
        if (options->file_type == XX_FILE_TYPE_UNKNOWN) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "No preferred scan file type");
            return false;
        }
    }
    return !pd || !pd->is_stop;
}

/* What one input's scan shares with every child scan below it. */
typedef struct xx_scan_shared {
    /* XScanEngine keeps one progress error string. An archive member it
     * unpacks but does not scan (a checksum mismatch, an unmapped method)
     * sets it, a later one overwrites it, and whichever scan finishes next
     * (the container's or a later member's) reports it as "Last error". */
    const char *pending_error;
    uint64_t archive_member_scans; /* Against XX_SCAN_ARCHIVE_SCAN_BUDGET. */
    bool budget_reported;
} xx_scan_shared;

static xx_scan_result *xx_scan_internal(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, const xx_scan_result *forbidden_result,
                                        uint64_t parent_id, xx_scan_part scan_part, unsigned int archive_depth, xx_scan_shared *shared, xx_pd_struct *pd);

/* Archive members inherit archives_scan, as in XScanEngine, so nested archives
 * expand too. A member this many archive levels below the input is still
 * scanned but not expanded: deeper nesting would only exhaust the stack (the
 * text tree also stops indenting past 20 levels). */
#define XX_SCAN_ARCHIVE_MAX_DEPTH 20U

/* A deliberate cdie safety limit that XScanEngine does not have: the archive
 * members scanned for one input, at every nesting level together. Nesting
 * multiplies XScanEngine's per-archive limits (21 members, 100,000 when
 * aggressive), so a few kilobytes of nested ZIPs could otherwise ask for
 * millions of scans. A real archive tree stays far below: the default cap is
 * 476 nested archives that each yield the full 21 members, and the aggressive
 * cap equals XScanEngine's own limit for a single archive. Reaching it stops
 * the expansion and is reported like other archive errors ("Last error"). */
#define XX_SCAN_ARCHIVE_SCAN_BUDGET UINT64_C(10000)
#define XX_SCAN_ARCHIVE_SCAN_BUDGET_AGGRESSIVE UINT64_C(100000)
static const char xx_scan_budget_message[] = "Archive member scan limit reached (10000)";
static const char xx_scan_budget_message_aggressive[] = "Archive member scan limit reached (100000)";

typedef struct xx_scan_resources_context {
    xx_scan_engine *engine;
    xx_io_device *device;
    const xx_scan_options *options;
    xx_scan_result *result;
    size_t scanned_count;
    unsigned int archive_depth; /* Archive levels between the input and this scan. */
    xx_scan_shared *shared;
} xx_scan_resources_context;

/* XScanEngine expands ZIP, 7Z, RAR, CAB and ISO 9660 containers, so also
 * JAR, APK and IPA, which are ZIP files. cdie reads members of the ZIP family
 * only; the shipped XScanEngine types 7Z and CAB files as Binary and expands
 * neither. GZIP, BZIP2, LHA, Mach-O FAT and the rest are never expanded. */
static bool xx_scan_expands_archive(xx_file_type_t type)
{
    return type == XX_FILE_TYPE_ZIP || type == XX_FILE_TYPE_ZIP64 || type == XX_FILE_TYPE_JAR || type == XX_FILE_TYPE_APK ||
           type == XX_FILE_TYPE_IPA;
}

/* Report the pending archive error in result, as the "Last error" record of
 * XScanEngine::scanProcess, when the engine keeps the standard result model. */
static bool xx_scan_take_pending_error(xx_scan_engine *engine, xx_scan_result *result, xx_scan_shared *shared, xx_pd_struct *pd)
{
    char text[160];
    const char *message;
    if (!shared || !shared->pending_error) return true;
    message = shared->pending_error;
    shared->pending_error = NULL;
    if (engine->append_result != xx_scan_append_result) return true;
    xx_rt_snprintf(text, sizeof(text), "Last error: %s", message);
    return xx_scan_result_add_error(result, text, pd);
}

static bool xx_scan_is_scanable_type(xx_file_type_t type)
{
    switch (type) {
        case XX_FILE_TYPE_MSDOS:
        case XX_FILE_TYPE_DOS4G:
        case XX_FILE_TYPE_DOS16M:
        case XX_FILE_TYPE_NE:
        case XX_FILE_TYPE_LE:
        case XX_FILE_TYPE_LX:
        case XX_FILE_TYPE_PE:
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
        case XX_FILE_TYPE_DOTNET:
        case XX_FILE_TYPE_ELF:
        case XX_FILE_TYPE_ELF32:
        case XX_FILE_TYPE_ELF64:
        case XX_FILE_TYPE_MACHO:
        case XX_FILE_TYPE_MACHO32:
        case XX_FILE_TYPE_MACHO64:
        case XX_FILE_TYPE_MACHOFAT:
        case XX_FILE_TYPE_DEX:
        case XX_FILE_TYPE_PDF:
        case XX_FILE_TYPE_ARCHIVE:
        case XX_FILE_TYPE_ZIP:
        case XX_FILE_TYPE_JAR:
        case XX_FILE_TYPE_APK:
        case XX_FILE_TYPE_IPA:
        case XX_FILE_TYPE_NPM:
        case XX_FILE_TYPE_RAR:
        case XX_FILE_TYPE_ISO9660:
            return true;
        default:
            return false;
    }
}

static bool xx_scan_is_scanable_types(const xx_list_t *types)
{
    size_t i;
    if (!types) return false;
    for (i = 0; i < types->count; ++i) {
        xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
        if (xx_list_get(types, i, &type)) {
            if (xx_scan_is_scanable_type(type)) return true;
        }
    }
    return false;
}

/* XScanEngine::isScanable for a resource or archive member, which a
 * non-aggressive scan scans only when it is an executable, DEX, PDF or
 * archive. XFormats also types a TAR as an archive, which the common
 * detector names Binary. */
static bool xx_scan_child_is_scanable(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd)
{
    xx_list_t *types = engine->get_file_types(engine, device, options, pd);
    bool scanable = xx_scan_is_scanable_types(types);
    if (types) xx_list_destroy(types);
    if (!scanable && !xx_pd_is_stopped(pd) && !(pd && pd->last_error)) scanable = xx_scan_archive_is_tar(device);
    return scanable;
}

#if XXFC_ENABLE_STREAM_RESOURCES
/* The resource reader lends its record only for this call. Scan the payload
 * synchronously and merge before its view and result are released. */
static bool xx_scan_run_resource(void *context, const xx_resource_record *resource, xx_pd_struct *pd)
{
    xx_scan_resources_context *scan = (xx_scan_resources_context *)context;
    xx_scan_engine *engine = scan->engine;
    xx_scan_options child_options = *scan->options;
    xx_io_device *child_device;
    xx_scan_result *part;
    bool appended;
    bool preserve_metadata;
    XFileType parent_type = XFT_UNKNOWN;
    int64_t parent_size = 0;
    char resource_type_id[16];
    size_t limit;

    if (xx_pd_is_stopped(pd)) return false;
    /* XScanEngine scans resources while its count is at most 20 (2000 when
     * aggressive), i.e. 21 (2001) of them. */
    limit = scan->options->aggressive_scan ? 2001 : 21;
    if (scan->scanned_count >= limit) return true;
    if (!engine->get_file_types || !engine->append_result) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, !engine->get_file_types ? "Missing scan file-type callback" : "Missing scan result append callback");
        return false;
    }
    child_device = xx_io_sub_open_ro(scan->device, resource->offset, resource->size);
    if (!child_device) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create scan resource device");
        return false;
    }
    child_options.offset = 0;
    child_options.size = resource->size;
    child_options.file_type = XX_FILE_TYPE_UNKNOWN;
    child_options.file_name = resource->name; /* Borrowed until this synchronous child scan returns. */
    child_options.resource_type = resource->type_name;
    if (!child_options.resource_type && resource->type_id != UINT32_MAX) {
        xx_rt_snprintf(resource_type_id, sizeof(resource_type_id), "%u", (unsigned int)resource->type_id);
        child_options.resource_type = resource_type_id;
    }
    /* As XScanEngine's child scans: archives expand (they inherit the
     * options), while a child's own resources and overlay are not scanned
     * (XScanEngine finds file parts only for the input's type). */
    child_options.recursive_scan = false;
    child_options.overlay_scan = false;
    child_options.resources_scan = false;
    child_options.all_types_scan = false;

    if (!scan->options->aggressive_scan && !xx_scan_child_is_scanable(engine, child_device, &child_options, pd)) {
        xx_io_close(child_device);
        return !xx_pd_is_stopped(pd) && !(pd && pd->last_error);
    }

    part = xx_scan_internal(engine, child_device, &child_options, scan->result, scan->options->scan_id, XX_SCAN_PART_RESOURCE, scan->archive_depth,
                            scan->shared, pd);
    if (!part) {
        xx_io_close(child_device);
        return false;
    }
    if (xx_pd_is_stopped(pd) || (pd && pd->last_error)) {
        engine->free_result(engine, part);
        xx_io_close(child_device);
        return false;
    }
    /* The standard merger interprets offset zero as another parent-format
     * pass. A resource can alias the input's first bytes, even when its scan
     * has no detections, so keep the containing input's metadata in that case.
     * Private result models remain opaque to the common scanner. */
    preserve_metadata = resource->offset == 0 && engine->append_result == xx_scan_append_result;
    if (preserve_metadata) {
        parent_type = scan->result->fileType;
        parent_size = scan->result->nFileSize;
    }
    appended = engine->append_result(engine, scan->result, part, resource->offset, pd);
    if (preserve_metadata) {
        scan->result->fileType = parent_type;
        scan->result->nFileSize = parent_size;
    }
    engine->free_result(engine, part);
    xx_io_close(child_device);
    if (!appended || xx_pd_is_stopped(pd) || (pd && pd->last_error)) {
        if (pd && !pd->last_error && !pd->is_stop) xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Cannot append scan resource result");
        return false;
    }
    scan->scanned_count++;
    return true;
}

static xx_scan_result *xx_scan_run_resources(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_scan_result *result,
                                             unsigned int archive_depth, xx_scan_shared *shared, xx_pd_struct *pd)
{
    xx_scan_resources_context context;
    context.engine = engine;
    context.device = device;
    context.options = options;
    context.result = result;
    context.scanned_count = 0;
    context.archive_depth = archive_depth; /* A resource is no archive level. */
    context.shared = shared;
    if (!xx_scan_visit_resources(device, options->file_type, options->offset, options->size, xx_scan_run_resource, &context, pd)) {
        if (pd && !pd->last_error && !pd->is_stop) xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Scan resource enumeration failed");
        engine->free_result(engine, result);
        return NULL;
    }
    return result;
}

#endif

/* The extraction helper owns the decoded device and lends it only for this
 * synchronous callback. Packed positions and decoded positions stay distinct. */
static bool xx_scan_run_archive_member(void *context, const xx_scan_archive_member *member, xx_pd_struct *pd)
{
    xx_scan_resources_context *scan = (xx_scan_resources_context *)context;
    xx_scan_engine *engine = scan->engine;
    xx_scan_options child_options = *scan->options;
    xx_scan_result *part;
    int64_t source_offset = member->is_compressed ? 0 : member->source_offset;
    XFileType parent_type = XFT_UNKNOWN;
    int64_t parent_size = 0;
    bool preserve_metadata;
    bool appended;
    size_t limit;

    if (xx_pd_is_stopped(pd)) return false;
    limit = scan->options->aggressive_scan ? 100000 : 21;
    if (scan->scanned_count >= limit) return false;
    if (!engine->get_file_types || !engine->append_result) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, !engine->get_file_types ? "Missing scan file-type callback" : "Missing scan result append callback");
        return false;
    }
    child_options.offset = 0;
    child_options.size = member->size;
    child_options.file_type = XX_FILE_TYPE_UNKNOWN;
    child_options.file_name = member->name;
    child_options.resource_type = NULL;
    child_options.archive_member = member->name;
    child_options.archive_offset = member->source_offset;
    child_options.archive_size = member->source_size;
    child_options.archive_compressed = member->is_compressed;
    child_options.archive_method = member->method;
    child_options.recursive_scan = false;
    child_options.overlay_scan = false;
    child_options.resources_scan = false;
    child_options.archives_scan = scan->options->archives_scan && scan->archive_depth + 1U < XX_SCAN_ARCHIVE_MAX_DEPTH;
    child_options.all_types_scan = false;

    if (!scan->options->aggressive_scan && !xx_scan_child_is_scanable(engine, member->device, &child_options, pd))
        return !xx_pd_is_stopped(pd) && !(pd && pd->last_error);
    if (scan->shared) {
        bool aggressive = scan->options->aggressive_scan;
        if (scan->shared->archive_member_scans >= (aggressive ? XX_SCAN_ARCHIVE_SCAN_BUDGET_AGGRESSIVE : XX_SCAN_ARCHIVE_SCAN_BUDGET)) {
            if (!scan->shared->budget_reported) {
                scan->shared->budget_reported = true;
                scan->shared->pending_error = aggressive ? xx_scan_budget_message_aggressive : xx_scan_budget_message;
            }
            return false;
        }
        scan->shared->archive_member_scans++;
    }

    part = xx_scan_internal(engine, member->device, &child_options, scan->result, scan->options->scan_id, XX_SCAN_PART_ARCHIVE,
                            scan->archive_depth + 1U, scan->shared, pd);
    if (!part) return false;
    if (xx_pd_is_stopped(pd) || (pd && pd->last_error)) {
        engine->free_result(engine, part);
        return false;
    }
    /* The standard zero-offset merger normally selects parent-format metadata.
     * Decoded members use that offset to keep positions relative to the member,
     * so preserve the container's metadata explicitly for the standard model. */
    preserve_metadata = source_offset == 0 && engine->append_result == xx_scan_append_result;
    if (preserve_metadata) {
        parent_type = scan->result->fileType;
        parent_size = scan->result->nFileSize;
    }
    appended = engine->append_result(engine, scan->result, part, source_offset, pd);
    if (preserve_metadata) {
        scan->result->fileType = parent_type;
        scan->result->nFileSize = parent_size;
    }
    engine->free_result(engine, part);
    if (!appended || xx_pd_is_stopped(pd) || (pd && pd->last_error)) {
        if (pd && !pd->last_error && !pd->is_stop) xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Cannot append scan archive result");
        return false;
    }
    scan->scanned_count++;
    if (scan->scanned_count >= limit) {
        return false;
    }
    return true;
}

/* A member XScanEngine unpacks but does not scan leaves its error string. */
static void xx_scan_report_archive_member(void *context, const char *message)
{
    xx_scan_resources_context *scan = (xx_scan_resources_context *)context;
    if (scan->shared) scan->shared->pending_error = message;
}

static xx_scan_result *xx_scan_run_archives(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_scan_result *result,
                                            unsigned int archive_depth, xx_scan_shared *shared, xx_pd_struct *pd)
{
    xx_scan_resources_context context;
    context.engine = engine;
    context.device = device;
    context.options = options;
    context.result = result;
    context.scanned_count = 0;
    context.archive_depth = archive_depth;
    context.shared = shared;
    if (!xx_scan_visit_archives_ex(device, options, xx_scan_run_archive_member, xx_scan_report_archive_member, &context, pd)) {
        if (pd && !pd->last_error && !pd->is_stop) xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Scan archive extraction failed");
        engine->free_result(engine, result);
        return NULL;
    }
    return result;
}

static xx_scan_result *xx_scan_run_overlay(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_scan_result *result,
                                           unsigned int archive_depth, xx_scan_shared *shared, xx_pd_struct *pd)
{
    xx_scan_options overlay_options = *options;
    xx_io_device *overlay_device = NULL;
    int64_t offset = -1;
    int64_t size = 0;
    int64_t total_size;
    int64_t range_size;
    xx_scan_result *part;
    bool found;
    bool appended;

    if (pd && pd->is_stop) goto failed;
    found = engine->get_overlay ? engine->get_overlay(engine, device, options, &offset, &size, pd) : xx_scan_get_overlay(engine, device, options, &offset, &size, pd);
    if (!found) {
        if (pd && !pd->last_error && !pd->is_stop) xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Scan overlay detection failed");
        goto failed;
    }
    if (pd && pd->is_stop) goto failed;
    if (size == 0) {
        if (offset != -1) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid empty scan overlay range");
            goto failed;
        }
        return result;
    }

    total_size = xx_io_total_size(device);
    range_size = options->size;
    if (range_size < 0 && total_size >= 0) range_size = total_size - options->offset;
    if (size < 0 || offset <= options->offset || offset > INT64_MAX - size || (total_size >= 0 && (offset > total_size || size > total_size - offset)) ||
        (range_size >= 0 && (offset - options->offset > range_size || size > range_size - (offset - options->offset)))) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Invalid scan overlay range");
        goto failed;
    }
    if (!engine->get_file_types || !engine->append_result) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, !engine->get_file_types ? "Missing scan file-type callback" : "Missing scan result append callback");
        goto failed;
    }

    overlay_device = xx_io_sub_open_ro(device, offset, size);
    if (!overlay_device) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create scan overlay device");
        goto failed;
    }
    overlay_options.offset = 0;
    overlay_options.size = size;
    overlay_options.file_type = XX_FILE_TYPE_UNKNOWN;
    overlay_options.file_name = NULL;
    /* As XScanEngine's child scans: an archive overlay expands (archives_scan
     * is inherited); the overlay's own file parts are not scanned. */
    overlay_options.recursive_scan = false;
    overlay_options.overlay_scan = false;
    overlay_options.resources_scan = false;
    overlay_options.all_types_scan = false;

    /* Use the same detection, buffering and dispatch path as a normal scan.
       The borrowed outer result must not be returned or freed by the child. */
    part = xx_scan_internal(engine, overlay_device, &overlay_options, result, options->scan_id, XX_SCAN_PART_OVERLAY, archive_depth, shared, pd);
    if (!part) goto failed;
    if (pd && pd->is_stop) {
        engine->free_result(engine, part);
        goto failed;
    }
    appended = engine->append_result(engine, result, part, offset, pd);
    engine->free_result(engine, part);
    if (!appended || (pd && pd->is_stop)) {
        if (pd && !pd->last_error && !pd->is_stop) xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Cannot append scan overlay result");
        goto failed;
    }
    xx_io_close(overlay_device);
    return result;
failed:
    if (overlay_device) xx_io_close(overlay_device);
    engine->free_result(engine, result);
    return NULL;
}

/* parent_id is 0 for the caller's input and, for a child, the scan_id of
 * the scan that found it. Every call draws its own scan_id. */
static xx_scan_result *xx_scan_internal(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, const xx_scan_result *forbidden_result,
                                        uint64_t parent_id, xx_scan_part scan_part, unsigned int archive_depth, xx_scan_shared *shared, xx_pd_struct *pd)
{
    xx_scan_options defaults;
    xx_scan_options resolved;
    int64_t device_size;
    xx_io_device *memory_device = NULL;
    xx_io_device *scan_device = device;
    xx_scan_result *result = NULL;
    void *buffer = NULL;

    if (!xx_scan_validate(engine, options, pd)) return NULL;
    if (!device) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Scan device is NULL");
        return NULL;
    }
    if (!options) {
        xx_scan_options_init(&defaults);
        options = &defaults;
    }

    device_size = xx_io_total_size(device);
    if (device_size >= 0 && (options->offset > device_size || (options->size >= 0 && options->size > device_size - options->offset))) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Scan range exceeds input size");
        return NULL;
    }
    if (options->file_type == XX_FILE_TYPE_UNKNOWN && !engine->get_file_types) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Missing scan file-type callback");
        return NULL;
    }
    if (options->file_type != XX_FILE_TYPE_UNKNOWN && !xx_scan_select_callback(engine, options->file_type)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG,
                        options->file_type == XX_FILE_TYPE_PE32 || options->file_type == XX_FILE_TYPE_PE64 || options->file_type == XX_FILE_TYPE_DOTNET
                            ? "Missing PE scan callback"
                            : "Missing scan callback");
        return NULL;
    }

    /* Archive extraction already chooses resident memory, a temporary file,
     * or a borrowed stored-member view. Preserve that storage decision. */
    if (scan_part != XX_SCAN_PART_ARCHIVE && device_size >= 0 && (uint64_t)device_size < (uint64_t)xx_get_file_buffer_size() && !xx_io_is_memory(device)) {
        size_t size = (size_t)device_size;
        size_t copied = 0;
        if (size) {
            buffer = xx_mem_alloc(size);
            if (!buffer) {
                xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate scan buffer");
                goto done;
            }
        }
        if (xx_io_seek64(device, 0, SEEK_SET) != 0) {
            xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot seek scan input");
            goto done;
        }
        while (copied < size) {
            size_t remaining = size - copied;
            ssize_t n = xx_io_read(device, (uint8_t *)buffer + copied, remaining > (size_t)PTRDIFF_MAX ? (size_t)PTRDIFF_MAX : remaining);
            if (n <= 0) {
                xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot read scan input");
                goto done;
            }
            copied += (size_t)n;
            if (pd && pd->is_stop) goto done;
        }
        memory_device = xx_io_mem_open_ro(buffer, size);
        if (!memory_device) {
            xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create scan memory device");
            goto done;
        }
        scan_device = memory_device;
    }

    resolved = *options;
    resolved.scan_id = xx_scan_new_id();
    resolved.parent_id = parent_id;
    resolved.scan_part = scan_part;
    if (scan_part != XX_SCAN_PART_RESOURCE) resolved.resource_type = NULL;
    if (scan_part != XX_SCAN_PART_ARCHIVE) {
        resolved.archive_member = NULL;
        resolved.archive_offset = 0;
        resolved.archive_size = 0;
        resolved.archive_compressed = false;
        resolved.archive_method = NULL;
    }
    if (!xx_scan_resolve_options(engine, scan_device, &resolved, pd)) goto done;
    result = xx_scan_run_plan(engine, scan_device, &resolved, forbidden_result, pd);
    if (result && resolved.resources_scan) {
#if XXFC_ENABLE_STREAM_RESOURCES
        result = xx_scan_run_resources(engine, scan_device, &resolved, result, archive_depth, shared, pd);
#else
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Resource stream support is disabled in this build");
        engine->free_result(engine, result);
        result = NULL;
#endif
    }
    if (result && resolved.archives_scan && xx_scan_expands_archive(resolved.file_type))
        result = xx_scan_run_archives(engine, scan_device, &resolved, result, archive_depth, shared, pd);
    if (result && resolved.overlay_scan) result = xx_scan_run_overlay(engine, scan_device, &resolved, result, archive_depth, shared, pd);
    /* As XScanEngine::scanProcess ends: a pending archive error becomes this
     * scan's "Last error". */
    if (result && !xx_scan_take_pending_error(engine, result, shared, pd)) {
        engine->free_result(engine, result);
        result = NULL;
    }

done:
    if (memory_device) xx_io_close(memory_device);
    xx_mem_free(buffer);
    return result;
}

xx_scan_result *xx_scan(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd)
{
    xx_scan_shared shared;
    xx_mem_zero(&shared, sizeof(shared));
    return xx_scan_internal(engine, device, options, NULL, 0, XX_SCAN_PART_INPUT, 0U, &shared, pd);
}

xx_scan_result *xx_scan_device(xx_scan_engine *engine, xx_io_device *device, const xx_scan_options *options, xx_pd_struct *pd)
{
    return xx_scan(engine, device, options, pd);
}

xx_scan_result *xx_scan_file(xx_scan_engine *engine, const char *path, const xx_scan_options *options, xx_pd_struct *pd)
{
    xx_scan_options file_options;
    xx_io_device *device;
    xx_scan_result *result;

    if (!xx_scan_validate(engine, options, pd)) return NULL;
    if (!path) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Scan path is NULL");
        return NULL;
    }
    if (options) file_options = *options;
    else xx_scan_options_init(&file_options);
    file_options.file_name = path;

    device = xx_io_file_open(path, "rb");
    if (!device) {
        xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot open scan input");
        return NULL;
    }
    result = xx_scan_device(engine, device, &file_options, pd);
    xx_io_close(device);
    return result;
}

xx_scan_result *xx_scan_memory(xx_scan_engine *engine, const void *data, size_t size, const xx_scan_options *options, xx_pd_struct *pd)
{
    xx_io_device *device;
    xx_scan_result *result;

    if (!xx_scan_validate(engine, options, pd)) return NULL;
    if (!data && size > 0) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Scan buffer is NULL");
        return NULL;
    }
    if ((uint64_t)size > INT64_MAX) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Scan buffer is too large");
        return NULL;
    }
    device = xx_io_mem_open_ro(data, size);
    if (!device) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create scan memory device");
        return NULL;
    }
    result = xx_scan_device(engine, device, options, pd);
    xx_io_close(device);
    return result;
}

size_t xx_scan_get_record_count(xx_scan_engine *engine, const xx_scan_result *result)
{
    if (!engine || !engine->get_record_count || !result) return 0;
    if (engine->get_record_count != xx_scan_get_record_count) return engine->get_record_count(engine, result);
    return result->nCount > 0 ? (size_t)result->nCount : 0;
}

const xx_scan_record *xx_scan_get_record(xx_scan_engine *engine, const xx_scan_result *result, size_t index)
{
    if (!engine || !engine->get_record || index >= xx_scan_get_record_count(engine, result)) return NULL;
    if (engine->get_record != xx_scan_get_record) return engine->get_record(engine, result, index);
    return result->nCount > 0 && index < (size_t)result->nCount && result->pRecords ? &result->pRecords[index] : NULL;
}

void xx_scan_free_result(xx_scan_engine *engine, xx_scan_result *result)
{
    if (!engine || !engine->free_result || !result) return;
    if (engine->free_result != xx_scan_free_result) {
        engine->free_result(engine, result);
        return;
    }
    xx_scan_result_free(result);
    xx_rt_free(result);
}

void xx_scan_engine_cleanup(xx_scan_engine *engine)
{
    if (!engine) return;
    if (engine->cleanup && engine->cleanup != xx_scan_engine_cleanup) engine->cleanup(engine);
    xx_mem_zero(engine, sizeof(*engine));
}
