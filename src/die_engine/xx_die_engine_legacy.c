/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Compatibility entry points for caller-owned standard result storage. */
#include "xxfclib/die_engine/die_engine.h"
#include "xxfclib/rt/xx_rt.h"
#include "xx_die_engine_engine.h"
#include <limits.h>

static void die_engine_legacy_error(xx_scan_result *result, const char *message)
{
    char **errors;
    char *text;
    size_t length;
    if (!message || !*message) return;
    length = xx_rt_strlen(message);
    errors = (char **)xx_rt_malloc(sizeof(*errors));
    text = (char *)xx_rt_malloc(length + 1);
    if (!errors || !text) {
        xx_rt_free(errors);
        xx_rt_free(text);
        return;
    }
    xx_rt_memcpy(text, message, length + 1);
    errors[0] = text;
    result->ppErrors = errors;
    result->nErrorCount = 1;
}

/* The deprecated entry points retain their historical automatic Binary/COM
 * pass policy. This scans one bounded input only; child traversal remains in
 * src/scan and uses the versioned typed-region op. */
static xx_scan_result *die_engine_legacy_region(xx_scan_engine *engine, xx_io_device *device,
                                                const xx_scan_options *options, xx_pd_struct *pd)
{
    xx_io_device *view;
    xx_scan_result *result;
    XFileType type;
    int64_t total = xx_io_total_size(device);
    int64_t size;
    int i;
    if (options->first_wrapper_only) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "DIE does not support first-wrapper-only scans");
        return NULL;
    }
    if (total < 0 || options->offset < 0 || options->size < -1 ||
        (options->size >= 0 && options->offset > INT64_MAX - options->size)) return NULL;
    size = options->size < 0 ? total - options->offset : options->size;
    if (options->offset > total || size < 0 || size > total - options->offset) return NULL;
    view = xx_io_sub_open_ro(device, options->offset, size);
    if (!view) return NULL;
    result = (xx_scan_result *)xx_rt_calloc(1, sizeof(*result));
    if (!result) {
        xx_io_close(view);
        return NULL;
    }
    type = die_engine_xft_from_native(options->file_type);
    if (!die_engine_scan_pass_device(view, (DBase *)engine->context, (xx_scan_options *)options, type, result, pd)) {
        xx_io_close(view);
        xx_scan_free_result(engine, result);
        return NULL;
    }
    xx_io_close(view);
    for (i = 0; i < result->nCount; ++i) {
        if (result->pRecords[i].offset < 0 || result->pRecords[i].offset > INT64_MAX - options->offset) {
            xx_scan_free_result(engine, result);
            return NULL;
        }
        result->pRecords[i].offset += options->offset;
        result->pRecords[i].engine_id = XX_SCAN_ENGINE_DIE;
    }
    result->engine_id = XX_SCAN_ENGINE_DIE;
    return result;
}

/* progress, when not NULL, is the caller's: it receives the engine's levels
 * and its stop request ends the scan. Otherwise a private one is used. */
static int die_engine_legacy_scan(xx_io_device *device, DBase *database, const xx_scan_options *options, xx_scan_result *result, xx_pd_struct *progress)
{
    xx_scan_engine engine;
    xx_scan_options local = *options;
    xx_pd_struct private_pd;
    xx_pd_struct *pd = progress ? progress : &private_pd;
    xx_scan_result *owned;

    xx_rt_memset(&engine, 0, sizeof(engine));
    xx_rt_memset(&private_pd, 0, sizeof(private_pd));
    if (!die_engine_init(&engine, database)) return 0;

    /* The console expands directories before calling these entry points.
     * Its recursive flag does not request recursion inside this one input. */
    local.recursive_scan = false;
    if (local.all_types_scan || local.overlay_scan || local.resources_scan || local.archives_scan) {
        owned = xx_scan_device(&engine, device, &local, pd);
    } else {
        owned = die_engine_legacy_region(&engine, device, &local, pd);
    }
    if (owned) {
        *result = *owned;
        xx_rt_memset(owned, 0, sizeof(*owned));
        xx_scan_free_result(&engine, owned);
    } else {
        die_engine_legacy_error(result, pd->error_string);
    }
    xx_scan_engine_cleanup(&engine);
    return owned != NULL;
}

int die_engine_scan_file(const char *file_name, DBase *database, xx_scan_options *options, xx_scan_result *result)
{
    return die_engine_scan_file_type(file_name, database, options, XFT_UNKNOWN, result);
}

int die_engine_scan_file_type(const char *file_name, DBase *database, xx_scan_options *options, XFileType file_type, xx_scan_result *result)
{
    return die_engine_scan_file_type_pd(file_name, database, options, file_type, result, NULL);
}

int die_engine_scan_file_type_pd(const char *file_name, DBase *database, xx_scan_options *options, XFileType file_type, xx_scan_result *result,
                                 xx_pd_struct *progress)
{
    xx_scan_options local;
    xx_io_device *device;
    int scanned;

    if (!result) return 0;
    xx_rt_memset(result, 0, sizeof(*result));
    if (!file_name || !*file_name || !database || !options || (unsigned int)file_type >= (unsigned int)XFT_COUNT) return 0;
    local = *options;
    local.file_name = file_name;
    if (file_type != XFT_UNKNOWN) local.file_type = die_engine_native_from_xft(file_type);
    device = xx_io_file_open(file_name, "rb");
    if (!device) return 0;
    scanned = die_engine_legacy_scan(device, database, &local, result, progress);
    xx_io_close(device);
    return scanned;
}

int die_engine_scan_device(xx_io_device *device, DBase *database, xx_scan_options *options, xx_scan_result *result)
{
    xx_scan_options local;
    int64_t original_position;
    int scanned;

    if (!result) return 0;
    xx_rt_memset(result, 0, sizeof(*result));
    if (!device || !database || !options) return 0;
    original_position = xx_io_tell(device);
    if (original_position < 0 || xx_io_seek64(device, 0, SEEK_SET) != 0) return 0;
    local = *options;
    if (!local.file_name) local.file_name = xx_io_source_path(device);
    scanned = die_engine_legacy_scan(device, database, &local, result, NULL);
    if (xx_io_seek64(device, original_position, SEEK_SET) != 0) scanned = 0;
    return scanned;
}

int die_engine_scan_memory(const void *data, int64_t size, DBase *database, xx_scan_options *options, xx_scan_result *result)
{
    xx_io_device *device;
    int scanned;

    if (!result) return 0;
    xx_rt_memset(result, 0, sizeof(*result));
    if (!database || !options) return 0;
    if (size < 0) size = 0;
    if ((!data && size > 0) || (uint64_t)size > (uint64_t)SIZE_MAX) return 0;
    device = xx_io_mem_open_ro(data, (size_t)size);
    if (!device) return 0;
    scanned = die_engine_legacy_scan(device, database, options, result, NULL);
    xx_io_close(device);
    return scanned;
}
