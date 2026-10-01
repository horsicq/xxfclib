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

/** Construct the reader for a caller-selected type without detecting again.
 * BINARY/UNKNOWN have no archive reader. Validate and parse after opening. */
bool xxfc_open_type(xxfc_opened *out, xx_io_device *device,
                    int64_t base_address, xx_file_type_t type);

/**
 * Try every reader whose declared extension matches @p source_path (longest
 * suffix first). A candidate is returned only after its format-specific
 * validity check and base-info parser both succeed; those implementations
 * remain in each reader's own format source file. If distinct file types
 * validate for the same suffix, the result is ambiguous and no reader wins.
 * Use this only when the signature detector reports BINARY or UNKNOWN.
 * The device remains owned by the caller and its cursor is restored.
 */
bool xxfc_open_extension(xxfc_opened *out, xx_io_device *device,
                         int64_t base_address, const char *source_path,
                         xx_pd_struct *pd);

/** Validate only the default reader selected by the static extension hint.
 * No content detector or competing reader probes are run. The returned
 * reader has already handled its base info. False means the caller should
 * use the legacy detector; it does not mean the file is necessarily invalid. */
bool xxfc_open_extension_fast(xxfc_opened *out, xx_io_device *device,
                              int64_t base_address, const char *source_path,
                              xx_pd_struct *pd);

/** Select a reader by its table name, for raw or ambiguous images.
 * Validation and base-info parsing remain the caller's responsibility. */
bool xxfc_open_named(xxfc_opened *out, xx_io_device *device,
                     int64_t base_address, const char *name);

/** Release a reader opened by xxfc_open(). The device stays the caller's. */
void xxfc_close(xxfc_opened *opened);

/** After base-info parsing, report readers that recovered an incomplete chain. */
bool xxfc_is_incomplete(const xxfc_opened *opened);

/** Attach supported sibling data files after parsing (currently CUE sheets).
 * The reader owns files it opens; referenced names stay within the source folder. */
void xxfc_attach_source_files(xxfc_opened *opened, const char *source_path);

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
