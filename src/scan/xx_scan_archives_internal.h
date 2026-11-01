/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SCAN_ARCHIVES_INTERNAL_H
#define XX_SCAN_ARCHIVES_INTERNAL_H

#include "xxfclib/scan/xx_scan.h"

typedef struct xx_scan_archive_member {
    xx_io_device *device;   /* Borrowed, read-only decoded contents, based at zero. */
    const char *name;       /* Borrowed; no archive-derived pathname is opened. */
    int64_t source_offset; /* Absolute compressed payload/slice offset in the parent. */
    int64_t source_size;   /* Stored source extent; decoded size may differ. */
    int64_t size;          /* Size of device. */
    bool is_compressed;   /* Decoded offsets do not map to parent bytes when true. */
} xx_scan_archive_member;

/* The member, its device and name are lent only for this synchronous call.
 * Returning false aborts enumeration. The callback must not close the device. */
typedef bool (*xx_scan_archive_callback)(void *context, const xx_scan_archive_member *member, xx_pd_struct *pd);

/* A member XScanEngine unpacks but does not scan, with the error string it
 * leaves behind: "Invalid CRC" for decoded or stored data that fails its
 * checksum, "Unknown compression method: Unknown" for a ZIP method XZip does
 * not map. The string is static. */
typedef void (*xx_scan_archive_report_callback)(void *context, const char *message);

/* Visit verified ZIP/LHA payloads and decoded concatenated GZIP/BZIP2 streams
 * within options.offset/size, as XScanEngine walks an archive. ZIP Store and
 * ordinary LHA LH0/LZ4 use bounded xx_io_sub source views after their checksum
 * is verified. Decoded output stays in memory only below
 * xx_get_file_buffer_size(); equality or a larger size uses xx_io_temp_open,
 * including a spill as GZIP/BZIP2 output grows. Temporary storage is cleaned
 * up after the callback and respects an active memory-only policy. Archive
 * child scans use these devices directly without an eager input copy.
 * Other formats, Mach-O FAT included, succeed without callbacks. Empty files
 * and directories are visited as empty members. Unless options.aggressive_scan,
 * only a member whose head can be scannable is visited; every other member is
 * still decoded, into nothing, so its checksum is verified as XScanEngine
 * verifies every member it unpacks. A damaged, encrypted, undecodable or
 * oversized member is skipped and never fails the visit; report (optional)
 * receives the error a checksum mismatch or an unmapped ZIP method leaves.
 * An unreadable directory or later record ends the walk after the members
 * before it. A callback returning false ends it cleanly. Only storage faults,
 * allocation failures and a stop request fail. No archive-derived output path
 * is opened; parent ownership and cursor are preserved. options.archive_max_*
 * bounds member size, total decoded size and count (zero selects defaults).
 * Decoder-internal buffers are independent of the output storage threshold. */
bool xx_scan_visit_archives_ex(xx_io_device *device, const xx_scan_options *options, xx_scan_archive_callback callback,
                               xx_scan_archive_report_callback report, void *context, xx_pd_struct *pd);

/* xx_scan_visit_archives_ex without a report callback. */
bool xx_scan_visit_archives(xx_io_device *device, const xx_scan_options *options,
                            xx_scan_archive_callback callback, void *context, xx_pd_struct *pd);

/* A complete TAR from offset 0, which XFormats types as an archive. The
 * device cursor is preserved. */
bool xx_scan_archive_is_tar(xx_io_device *device);

#endif /* XX_SCAN_ARCHIVES_INTERNAL_H */
