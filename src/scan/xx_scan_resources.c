/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_resources_internal.h"
#include "xxfclib/formats/executable/elf/xx_elf.h"
#include "xxfclib/formats/executable/pe/xx_pe.h"

/* Bound child scans independently of the format reader's traversal budget.
 * The ELF resource reader uses the same maximum number of leaf records. */
#define XX_SCAN_RESOURCE_LIMIT UINT32_C(65536)

static bool xx_scan_resource_error(xx_pd_struct *pd, int error, const char *message)
{
    if (pd && !pd->last_error && !xx_pd_is_stopped(pd)) xx_pd_set_error(pd, error, message);
    return false;
}

static bool xx_scan_resources_stopped(xx_pd_struct *pd)
{
    return xx_pd_is_stopped(pd) || (pd && pd->last_error);
}

static bool xx_scan_visit_resource_stream(Abstractformat *format, int64_t origin, int64_t range_size, bool unreadable_is_empty, xx_scan_resource_callback callback,
                                          void *context, xx_pd_struct *pd)
{
    xx_resource_state *state;
    const xx_resource_record *record;
    uint32_t count = 0U;
    bool result = false;

    if (xx_scan_resources_stopped(pd)) return false;
    if (!xx_format_handle_base_info(format, pd)) {
        /* The scan types an ELF by its magic and class alone, as XELF::isValid
         * does. One the reader turns down (a truncated file, a packed .so whose
         * section table points past the end) has no resources; it is no error. */
        if (unreadable_is_empty && !xx_scan_resources_stopped(pd)) return true;
        return xx_scan_resource_error(pd, XXFC_ERR_GENERIC, "Cannot read executable resources");
    }
    if (xx_scan_resources_stopped(pd)) return false;
    state = xx_format_create_resources_reading(format, pd);
    if (!state) return xx_scan_resource_error(pd, XXFC_ERR_GENERIC, "Cannot create scan resource reader");

    for (;;) {
        xx_resource_record absolute_record;
        if (xx_scan_resources_stopped(pd) || state->failed) goto done;
        record = xx_format_get_current_resource(format, state);
        if (!record) {
            result = true;
            break;
        }
        if (count == XX_SCAN_RESOURCE_LIMIT) {
            xx_scan_resource_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Scan resource count exceeds bounded limit");
            goto done;
        }
        ++count;
        if (record->size < 0 || (record->size > 0 && (record->offset < 0 || record->offset > range_size || record->size > range_size - record->offset))) {
            xx_scan_resource_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Invalid scan resource range");
            goto done;
        }
        if (record->size > 0) {
            /* The enclosing view has base zero. Translate only the borrowed
             * copy, leaving the iterator's current record unmodified. */
            absolute_record = *record;
            absolute_record.offset += origin;
            if (!callback(context, &absolute_record, pd)) {
                xx_scan_resource_error(pd, XXFC_ERR_GENERIC, "Scan resource callback failed");
                goto done;
            }
            if (xx_scan_resources_stopped(pd)) goto done;
        }
        if (!xx_format_resource_move_to_next(format, state, pd)) {
            result = !state->failed && !xx_scan_resources_stopped(pd);
            break;
        }
    }
done:
    if (!result) xx_scan_resource_error(pd, XXFC_ERR_GENERIC, "Cannot enumerate executable resources");
    xx_format_free_resources_reading(format, state);
    return result;
}

bool xx_scan_visit_resources(xx_io_device *device, xx_file_type_t type, int64_t offset, int64_t size, xx_scan_resource_callback callback, void *context, xx_pd_struct *pd)
{
    xx_io_device *view;
    int64_t total_size;
    int64_t range_size;
    int64_t saved_position;
    bool result;

    if (!device || !callback) return xx_scan_resource_error(pd, XXFC_ERR_NULL_PARAM, "Missing scan resource argument");
    if (xx_scan_resources_stopped(pd)) return false;
    if (offset < 0 || size < -1 || (size >= 0 && size > INT64_MAX - offset)) return xx_scan_resource_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan resource input range");
    switch (type) {
        case XX_FILE_TYPE_PE:
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
        case XX_FILE_TYPE_DOTNET:
        case XX_FILE_TYPE_ELF:
        case XX_FILE_TYPE_ELF32:
        case XX_FILE_TYPE_ELF64: break;
        default: return true;
    }

    saved_position = xx_io_tell(device);
    if (saved_position < 0) return xx_scan_resource_error(pd, XXFC_ERR_IO, "Cannot save scan resource input position");
    total_size = xx_io_total_size(device);
    if ((total_size >= 0 && (offset > total_size || (size >= 0 && size > total_size - offset))) || (total_size < 0 && size == -1)) {
        result = xx_scan_resource_error(pd, total_size < 0 ? XXFC_ERR_IO : XXFC_ERR_OUT_OF_BOUNDS,
                                        total_size < 0 ? "Cannot determine scan resource input size" : "Scan resource range exceeds input");
        goto restore;
    }
    range_size = size >= 0 ? size : total_size - offset;
    view = xx_io_sub_open_ro(device, offset, range_size);
    if (!view) {
        result = xx_scan_resource_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create scan resource input view");
        goto restore;
    }
    if (type == XX_FILE_TYPE_ELF || type == XX_FILE_TYPE_ELF32 || type == XX_FILE_TYPE_ELF64) {
        xx_elf reader;
        xx_elf_init(&reader, view, 0);
        result = xx_scan_visit_resource_stream(&reader.format, offset, range_size, true, callback, context, pd);
        xx_elf_destroy(&reader);
    } else {
        xx_pe reader;
        xx_pe_init(&reader, view, 0);
        result = xx_scan_visit_resource_stream(&reader.format, offset, range_size, false, callback, context, pd);
        xx_pe_destroy(&reader);
    }
    xx_io_close(view);
restore:
    if (xx_io_seek64(device, saved_position, SEEK_SET) != 0) return xx_scan_resource_error(pd, XXFC_ERR_IO, "Cannot restore scan resource input position");
    return result && !xx_scan_resources_stopped(pd);
}
