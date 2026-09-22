/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* The reader table, and opening a file without knowing what it is.
 *
 * xxfclib detects a file type from a device (xx_format_get_file_type_device)
 * and constructs readers by name (xx_zip_create, xx_tar_create, ...), but it
 * has nothing that joins the two. A program handed an arbitrary file has to
 * supply that join itself; this is it.
 */

#ifndef XXFC_READERS_H
#define XXFC_READERS_H

#include <xxfclib/xxfclib.h>
#include <xxfclib/formats/xx_format.h>
#include <xxfclib/io/xx_io.h>

#include <stddef.h>

typedef Abstractformat *(*xxfc_create_fn)(xx_io_device *device,
                                          int64_t base_address);
typedef void (*xxfc_release_fn)(void *reader);

typedef struct {
    const char *name;        /**< Reader directory name, for diagnostics. */
    xxfc_create_fn create;
    xxfc_release_fn release;
    xx_file_type_t type;     /**< Filled in on first use; see xxfc_readers.c. */
} xxfc_reader_entry;

/** Number of readers in the table. */
size_t xxfc_reader_count(void);

/**
 * The table, with every entry's file type resolved.
 *
 * The first call creates each reader once to ask what it reads, so it is the
 * expensive one. Not thread safe: call it once before handing work out.
 */
xxfc_reader_entry *xxfc_reader_table(void);

/** An opened file: the format to work through, and how to let go of it. */
typedef struct {
    Abstractformat *format;
    xxfc_release_fn release;
    const char *reader_name;
    xx_file_type_t type;
} xxfc_opened;

/**
 * Detect what @p device holds and construct the reader for it.
 *
 * Only opens it -- validating and parsing are the caller's next steps, so a
 * caller that wants to report "recognised but broken" separately from "not
 * recognised" can.
 *
 * @return false when the type is unknown or no reader claims it. The device
 *         is not closed either way.
 */
bool xxfc_open(xxfc_opened *out, xx_io_device *device, int64_t base_address);

/** Release a reader opened by xxfc_open(). The device stays the caller's. */
void xxfc_close(xxfc_opened *opened);

/**
 * Construct a reader for writing, chosen by container name.
 *
 * Only the containers xxfclib can write answer to this: "tar", "tar.gz",
 * "tar.bz2", "tar.xz", "tar.zst", "tar.lz4", "zip" and "cpio". Reading is a
 * far wider set -- see the table above -- and the asymmetry is the library's,
 * not this program's.
 *
 * @return NULL when @p kind is not one of them.
 */
Abstractformat *xxfc_make_writer(const char *kind, xx_io_device *device,
                                 xxfc_release_fn *release);

#endif /* XXFC_READERS_H */
