/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_SCAN_RESOURCES_INTERNAL_H
#define XX_SCAN_RESOURCES_INTERNAL_H

#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/formats/xx_format_streams.h"

/* The record and its strings are borrowed only for the callback. Its offset
 * is absolute in the device supplied to xx_scan_visit_resources(), and its
 * nonempty payload is wholly inside the requested range. Returning false
 * stops enumeration and propagates failure. The callback may reposition the
 * device but must not close it or change its contents. */
typedef bool (*xx_scan_resource_callback)(void *context, const xx_resource_record *record, xx_pd_struct *pd);

/* Visit PE/PE64/.NET resources and ELF GResources in their stored form using
 * the existing format streams. Other formats succeed without callbacks.
 * size == -1 selects the remainder of the device. Empty payloads are skipped.
 * The device remains owned by the caller and its position is restored on
 * return. Malformed input, cancellation, allocation failure and exceeding
 * the bounded record count fail; a partially visited set is not success. */
bool xx_scan_visit_resources(xx_io_device *device, xx_file_type_t type, int64_t offset, int64_t size, xx_scan_resource_callback callback, void *context,
                             xx_pd_struct *pd);

#endif /* XX_SCAN_RESOURCES_INTERNAL_H */
