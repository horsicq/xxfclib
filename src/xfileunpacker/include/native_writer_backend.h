/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XFILEUNPACKER_NATIVE_WRITER_BACKEND_H
#define XFILEUNPACKER_NATIVE_WRITER_BACKEND_H

#include "core.h"
#include <xxfclib/formats/xx_format.h>
#include <xxfclib/io/xx_io.h>

/* The writer validates selectors and sources before publishing its output.
 * Return values follow xfu_run(): 0 success, 1 failed/cancelled, 2 invalid
 * request or unsupported operation. */
int xfu_native_create(const xfu_request *request, const char *format, const char *filter, xx_pd_struct *pd);
bool xfu_native_can_create_format(const char *format);
bool xfu_native_can_create_filter(const char *filter);
/* Caller owns the returned text. */
char *xfu_native_capabilities_text(void);

/* One member of a write: an input, or a file or folder found in an input
 * folder. Borrowed for the writer call. The metadata is what the walk found:
 * for a link named as an input, that of the file or folder it names. */
typedef struct xfu_native_member {
    const char *path; /* The file or folder that was walked. */
    const char *name; /* The member name, '/'-separated, without a final '/'. */
    int64_t size;     /* A file's size when it was walked; 0 for a folder. */
    bool directory;
    int64_t mtime;          /* Seconds since 1970, negative before. */
    uint32_t mode;          /* Permission bits; on Windows 0644 (0444 read-only) or 0755. */
    uint32_t uid, gid;      /* The POSIX owner; 0 on Windows. */
    uint64_t device, inode; /* The POSIX identity the walk found; 0 on Windows. */
    bool walked;            /* Found in a folder (a link there is never followed). */
} xfu_native_member;

/* A file member opened for reading: the file the walk found, or NULL when it
 * is gone, cannot be read or is no longer that file (another file, a link
 * found in a folder, or one of another size), so a file swapped after the
 * walk is never read in its place. Close it with xx_io_close. */
xx_io_device *xfu_native_open_member(const xfu_native_member *member);

/* Why xfu_native_open_member returned NULL, for "<path>: <reason>": given the
 * errno it left (read at once), the system's reason when the file could not
 * be opened, else that it changed since it was found. Written to buffer. */
const char *xfu_native_open_failure(int error, char *buffer, size_t size);

/* Stores the member's permissions and modification time in a record for the
 * xxfclib writer of kind ("tar...", "cpio", "zip", "7z" or "7zip"), in the
 * units that format uses: TAR with the owner and group (where the ustar
 * fields hold them; else 65534, nobody's, never root's 0), ZIP as made on
 * Unix with the mode and DOS time, 7-Zip as Windows attributes with the Unix
 * mode and FILETIME. A time the format cannot hold is clamped to the nearest
 * it can (7-Zip: none is stored); xfu_native_record_warnings says so, and
 * what became of an owner. Other kinds get nothing. False only when the
 * record cannot take a value. */
bool xfu_native_record_metadata(xx_archive_record *record, const char *kind, const xfu_native_member *member);

/* For the record of xfu_native_record_metadata (label names the format):
 * when kind cannot hold the member's modification time, the warning of
 * xfu_native_warn_time (for ZIP in local time, as it stores it); when a TAR
 * kind cannot hold its owner or group id, one line that it is stored as
 * nobody's. */
void xfu_native_record_warnings(const xfu_request *request, const char *kind, const char *label, const xfu_native_member *member);

/* A warning, through the request's log as an error line ("<path>: ..."),
 * that does not count as the reason of a failure: the run goes on, and a
 * writer that fails later still gets its own line. A writer's warning (one
 * xfu_native_create or xfu_native_create_with runs) is logged only once the
 * archive is published, as the member lines are; a run that fails drops it. */
void xfu_native_warn(const xfu_request *request, const char *format, ...);

/* A time in seconds since 1970 (negative before) as a UTC date and time of
 * the proleptic Gregorian calendar, for any value. */
typedef struct xfu_native_time {
    int64_t year;
    unsigned month, day, hour, minute, second;
} xfu_native_time;
void xfu_native_civil(int64_t seconds, xfu_native_time *civil);

/* The one line for a modification time that label (a format's name) cannot
 * hold: "<path>: its modification time (<date>) is earlier|later than <label>
 * can store; stored as <date of stored>", or with kept false "...; stored
 * without one". Both dates are in UTC, with the time of day when they are
 * the same day. */
void xfu_native_warn_time(const xfu_request *request, const char *path, const char *label, int64_t mtime, int64_t stored, bool kept);

/* Whether the xxfclib writer of kind (as xxfc_make_writer names it: "tar",
 * "tar.gz", ..., "cpio", "7z" or "7zip", "wim", "zip", "gz", ...) stores the
 * member, which it would refuse without a reason: false, with the reason in
 * reason (label names the format), for a name its safe-name rule refuses or a
 * file larger than its header holds. */
bool xfu_native_xxfclib_member_fits(const char *kind, const char *label, const xfu_native_member *member, char *reason, size_t capacity);

/* Writes the members, in order, to output (the stage of the archive). It
 * logs the reason for a failure it can explain, but not a Cancel. request is
 * the caller's; its log, progress and cancelled callbacks reach the caller. */
typedef bool (*xfu_native_member_writer)(void *context, const xfu_request *request, const xfu_native_member *members, size_t count, xx_io_device *output,
                                         xx_pd_struct *pd);

/* For a writer that failed: when output (xfu_native_create_with's stage) took
 * a failed write, seek or size query, the reason ("the archive could not be
 * written: <system reason>") in buffer; else NULL, and the writer's own
 * reason stands. */
const char *xfu_native_output_failure(xx_io_device *output, char *buffer, size_t size);

/* Flags of xfu_native_create_with. */
#define XFU_NATIVE_WITH_FOLDERS 1U /* A folder is stored with what it holds. */
#define XFU_NATIVE_WITH_UTF8 2U    /* Member names must be valid UTF-8. */
#define XFU_NATIVE_WITH_STREAM 4U  /* A compressed stream of a file: no folder. */

/* xfu_native_create's handling of the inputs around another writer (core.c's
 * suffix-selected xxfclib writers): the inputs are walked and checked before
 * anything is written (without XFU_NATIVE_WITH_FOLDERS a folder is refused;
 * a link named as an input is followed, one found in a folder is refused),
 * the writer fills a stage beside the destination, which replaces it only on
 * success (with the replaced archive's permissions; a destination that is a
 * symbolic link is followed to the file it names), and the members and
 * "<archive>: N file(s) as <label>" are logged. Returns as xfu_run(). */
int xfu_native_create_with(const xfu_request *request, const char *label, unsigned flags, xfu_native_member_writer writer, void *context, xx_pd_struct *pd);

#endif
