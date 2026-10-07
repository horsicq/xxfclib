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

#if defined(_MSC_VER)
#include <intrin.h>
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

static uint32_t xx_scan_atomic_increment(volatile long *value) {
#if defined(_MSC_VER)
    return (uint32_t)_InterlockedIncrement(value);
#elif defined(__GNUC__) || defined(__clang__)
    return (uint32_t)__atomic_add_fetch(value, 1, __ATOMIC_RELAXED);
#else
    return (uint32_t)++*value;
#endif
}

/* Stores desired when *value is 0; returns the value now in place. */
static uint32_t xx_scan_atomic_init(volatile long *value, long desired) {
#if defined(_MSC_VER)
    long previous = _InterlockedCompareExchange(value, desired, 0);
    return (uint32_t)(previous ? previous : desired);
#elif defined(__GNUC__) || defined(__clang__)
    long expected = 0;
    if (__atomic_compare_exchange_n(value, &expected, desired, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return (uint32_t)desired;
    return (uint32_t)expected;
#else
    if (!*value) *value = desired;
    return (uint32_t)*value;
#endif
}

/* SplitMix64 finalizer: a bijection on 64-bit values. */
static uint64_t xx_scan_mix64(uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

static uint64_t xx_scan_new_id(void) {
    uint32_t seed = (uint32_t)xx_scan_id_seed;
    uint64_t id;
    if (!seed) {
        int marker = 0;
        uint64_t entropy = (uint64_t)xx_rt_clock_ms() ^
            ((uint64_t)(uintptr_t)&xx_scan_id_counter << 20) ^
            ((uint64_t)(uintptr_t)&marker << 7);
        uint32_t candidate = (uint32_t)(xx_scan_mix64(entropy) >> 32);
        seed = xx_scan_atomic_init(&xx_scan_id_seed, (long)(candidate ? candidate : 1u));
    }
    /* The seed is nonzero, so the mixer input is never 0 and, the mixer being
     * a bijection that fixes 0, neither is the id: 0 stays free for "no
     * parent". */
    id = xx_scan_mix64(((uint64_t)seed << 32) |
                       xx_scan_atomic_increment(&xx_scan_id_counter));
    return id;
}

static xx_scan_format_callback xx_scan_select_callback(
    const xx_scan_engine *engine, xx_file_type_t type) {
    xx_scan_format_callback callback = NULL;
    size_t index;
    switch (type) {
        case XX_FILE_TYPE_BINARY: callback = engine->scan_binary; break;
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
        case XX_FILE_TYPE_DOTNET:
            return engine->scan_pe;
        case XX_FILE_TYPE_ELF32:
        case XX_FILE_TYPE_ELF64: callback = engine->scan_elf; break;
        case XX_FILE_TYPE_MACHO32:
        case XX_FILE_TYPE_MACHO64: callback = engine->scan_macho; break;
        case XX_FILE_TYPE_MACHOFAT: callback = engine->scan_machofat; break;
        case XX_FILE_TYPE_JAVA_CLASS: callback = engine->scan_java_class; break;
        case XX_FILE_TYPE_ZIP: callback = engine->scan_zip; break;
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
        if (handler->file_type == type && handler->callback)
            return handler->callback;
    }
    return engine->scan_device;
}

static size_t xx_scan_build_plan(const xx_scan_options *options,
                                 xx_file_type_t types[3]) {
    size_t count = 0;
    if (options->all_types_scan) {
        switch (options->file_type) {
            case XX_FILE_TYPE_PE32:
            case XX_FILE_TYPE_PE64:
            case XX_FILE_TYPE_DOTNET:
            case XX_FILE_TYPE_NE:
            case XX_FILE_TYPE_LE:
            case XX_FILE_TYPE_LX:
                types[count++] = XX_FILE_TYPE_MSDOS;
                break;
            case XX_FILE_TYPE_APK:
            case XX_FILE_TYPE_IPA:
                types[count++] = XX_FILE_TYPE_ZIP;
                types[count++] = XX_FILE_TYPE_JAR;
                break;
            case XX_FILE_TYPE_JAR:
                types[count++] = XX_FILE_TYPE_ZIP;
                break;
            case XX_FILE_TYPE_DOS4G:
                types[count++] = XX_FILE_TYPE_DOS16M;
                break;
            default: break;
        }
    }
    types[count++] = options->file_type;
    return count;
}

static xx_scan_result *xx_scan_run_plan(xx_scan_engine *engine,
                                        xx_io_device *device,
                                        const xx_scan_options *options,
                                        const xx_scan_result *forbidden_result,
                                        xx_pd_struct *pd) {
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
                            types[index] == XX_FILE_TYPE_PE32 ||
                            types[index] == XX_FILE_TYPE_PE64
                            || types[index] == XX_FILE_TYPE_DOTNET
                                ? "Missing PE scan callback" : "Missing scan callback");
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
        if (options->all_types_scan) {
            pass.all_types_scan = false;
            if (xx_io_seek64(device, pass.offset, SEEK_SET) != 0) {
                xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot seek scan pass input");
                goto failed;
            }
        }
        part = callbacks[index](engine, device, &pass, pd);
        if (!part) {
            if (pd && !pd->last_error && !pd->is_stop)
                xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Scan callback failed");
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
                if (pd && !pd->last_error && !pd->is_stop)
                    xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Cannot append scan result");
                goto failed;
            }
        }
    }
    return result;
failed:
    if (result) engine->free_result(engine, result);
    return NULL;
}

void xx_scan_options_init(xx_scan_options *options) {
    if (!options) return;
    xx_mem_zero(options, sizeof(*options));
    options->size = -1;
}

static bool xx_scan_validate(xx_scan_engine *engine, const xx_scan_options *options,
                             xx_pd_struct *pd) {
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
    if (options && (options->offset < 0 || options->size < -1 ||
        (options->size >= 0 && options->size > INT64_MAX - options->offset))) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan range");
        return false;
    }
    return !pd || !pd->is_stop;
}

static bool xx_scan_resolve_options(xx_scan_engine *engine, xx_io_device *device,
                                     xx_scan_options *options, xx_pd_struct *pd) {
    if (options->file_type == XX_FILE_TYPE_UNKNOWN) {
        xx_list_t *types;
        if (!engine->get_file_types) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Missing scan file-type callback");
            return false;
        }
        types = engine->get_file_types(engine, device, options, pd);
        if (!types) {
            if (pd && !pd->last_error && !pd->is_stop)
                xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Scan file-type detection failed");
            return false;
        }
        if (types->elem_size != sizeof(xx_file_type_t) || types->count == 0 ||
            !xx_list_get(types, types->count - 1, &options->file_type)) {
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

static xx_scan_result *xx_scan_internal(xx_scan_engine *engine, xx_io_device *device,
                                        const xx_scan_options *options,
                                        const xx_scan_result *forbidden_result,
                                        uint64_t parent_id, xx_pd_struct *pd);

static xx_scan_result *xx_scan_run_overlay(xx_scan_engine *engine,
                                            xx_io_device *device,
                                            const xx_scan_options *options,
                                            xx_scan_result *result,
                                            xx_pd_struct *pd) {
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
    found = engine->get_overlay
        ? engine->get_overlay(engine, device, options, &offset, &size, pd)
        : xx_scan_get_overlay(engine, device, options, &offset, &size, pd);
    if (!found) {
        if (pd && !pd->last_error && !pd->is_stop)
            xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Scan overlay detection failed");
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
    if (range_size < 0 && total_size >= 0)
        range_size = total_size - options->offset;
    if (size < 0 || offset <= options->offset || offset > INT64_MAX - size ||
        (total_size >= 0 && (offset > total_size || size > total_size - offset)) ||
        (range_size >= 0 && (offset - options->offset > range_size ||
            size > range_size - (offset - options->offset)))) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Invalid scan overlay range");
        goto failed;
    }
    if (!engine->get_file_types || !engine->append_result) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG,
                        !engine->get_file_types ? "Missing scan file-type callback"
                                                : "Missing scan result append callback");
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
    overlay_options.recursive_scan = false;
    overlay_options.overlay_scan = false;
    overlay_options.resources_scan = false;
    overlay_options.archives_scan = false;
    overlay_options.all_types_scan = false;

    /* Use the same detection, buffering and dispatch path as a normal scan.
       The borrowed outer result must not be returned or freed by the child. */
    part = xx_scan_internal(engine, overlay_device, &overlay_options, result,
                            options->scan_id, pd);
    if (!part) goto failed;
    if (pd && pd->is_stop) {
        engine->free_result(engine, part);
        goto failed;
    }
    appended = engine->append_result(engine, result, part, offset, pd);
    engine->free_result(engine, part);
    if (!appended || (pd && pd->is_stop)) {
        if (pd && !pd->last_error && !pd->is_stop)
            xx_pd_set_error(pd, XXFC_ERR_GENERIC, "Cannot append scan overlay result");
        goto failed;
    }
    xx_io_close(overlay_device);
    return result;
failed:
    if (overlay_device) xx_io_close(overlay_device);
    engine->free_result(engine, result);
    return NULL;
}

/* parent_id is 0 for the caller's input and, for an overlay, the scan_id of
 * the scan that found it. Every call draws its own scan_id. */
static xx_scan_result *xx_scan_internal(xx_scan_engine *engine, xx_io_device *device,
                                        const xx_scan_options *options,
                                        const xx_scan_result *forbidden_result,
                                        uint64_t parent_id, xx_pd_struct *pd) {
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
    if (device_size >= 0 && (options->offset > device_size ||
        (options->size >= 0 && options->size > device_size - options->offset))) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Scan range exceeds input size");
        return NULL;
    }
    if (options->file_type == XX_FILE_TYPE_UNKNOWN && !engine->get_file_types) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Missing scan file-type callback");
        return NULL;
    }
    if (options->file_type != XX_FILE_TYPE_UNKNOWN &&
        !xx_scan_select_callback(engine, options->file_type)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG,
                        options->file_type == XX_FILE_TYPE_PE32 ||
                        options->file_type == XX_FILE_TYPE_PE64
                        || options->file_type == XX_FILE_TYPE_DOTNET
                            ? "Missing PE scan callback" : "Missing scan callback");
        return NULL;
    }

    if (device_size >= 0 && (uint64_t)device_size < (uint64_t)xx_get_file_buffer_size() &&
        !xx_io_is_memory(device)) {
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
            ssize_t n = xx_io_read(device, (uint8_t *)buffer + copied,
                                   remaining > (size_t)PTRDIFF_MAX
                                       ? (size_t)PTRDIFF_MAX : remaining);
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
    if (!xx_scan_resolve_options(engine, scan_device, &resolved, pd)) goto done;
    result = xx_scan_run_plan(engine, scan_device, &resolved, forbidden_result, pd);
    if (result && resolved.overlay_scan)
        result = xx_scan_run_overlay(engine, scan_device, &resolved, result, pd);

done:
    if (memory_device) xx_io_close(memory_device);
    xx_mem_free(buffer);
    return result;
}

xx_scan_result *xx_scan(xx_scan_engine *engine, xx_io_device *device,
                        const xx_scan_options *options, xx_pd_struct *pd) {
    return xx_scan_internal(engine, device, options, NULL, 0, pd);
}

xx_scan_result *xx_scan_device(xx_scan_engine *engine, xx_io_device *device,
                               const xx_scan_options *options, xx_pd_struct *pd) {
    return xx_scan(engine, device, options, pd);
}

xx_scan_result *xx_scan_file(xx_scan_engine *engine, const char *path,
                             const xx_scan_options *options, xx_pd_struct *pd) {
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

xx_scan_result *xx_scan_memory(xx_scan_engine *engine, const void *data, size_t size,
                               const xx_scan_options *options, xx_pd_struct *pd) {
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

size_t xx_scan_get_record_count(xx_scan_engine *engine, const xx_scan_result *result) {
    return (engine && engine->get_record_count && result)
        ? engine->get_record_count(engine, result) : 0;
}

const xx_scan_record *xx_scan_get_record(xx_scan_engine *engine,
                                         const xx_scan_result *result, size_t index) {
    if (!engine || !engine->get_record ||
        index >= xx_scan_get_record_count(engine, result)) return NULL;
    return engine->get_record(engine, result, index);
}

void xx_scan_free_result(xx_scan_engine *engine, xx_scan_result *result) {
    if (engine && engine->free_result && result) engine->free_result(engine, result);
}

void xx_scan_engine_cleanup(xx_scan_engine *engine) {
    if (!engine) return;
    if (engine->cleanup) engine->cleanup(engine);
    xx_mem_zero(engine, sizeof(*engine));
}
