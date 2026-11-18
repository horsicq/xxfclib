/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Python-zipfile-exact ZIP reader.
 *
 * An in-memory ZIP reader that follows CPython's zipfile module (3.13): the
 * same end-of-central-directory search, entry list, name decoding and
 * sanitizing, local-header checks, supported methods and CRC verification.
 * Tools that must agree with a Python program about which members can be
 * opened (APKiD ports, for example) use it instead of the archive framework
 * in xx_zip.h, whose acceptance rules are DIE's.
 *
 * Deliberate differences from CPython. These keep a whole archive readable
 * where CPython would raise for it:
 *   - a member name whose UTF-8 flag is set but whose bytes are not UTF-8
 *     (UnicodeDecodeError in CPython) gets U+FFFD for each invalid byte;
 *   - a truncated extra record (BadZipFile) ends the extra parsing;
 *   - extract_version > 63 (NotImplementedError) is ignored;
 *   - is_zipfile() answers false where CPython's lets BadZipFile escape (a
 *     ZIP64 locator for several disks).
 * These change a text or a failure class only:
 *   - repr() of names is a readable stand-in: the local name of a "File name
 *     in directory ... and header ... differ." error is shown as text, where
 *     CPython shows bytes (b'...');
 *   - the encrypted-member error names the member as ZipFile.open(name)
 *     does, also when CPython was given a ZipInfo;
 *   - compressed data that fails to decode, when the CRC-32 of the partial
 *     output does not match either, is XX_ZIP_PY_DATA_ERROR ("invalid
 *     compressed data") where CPython may reach the end of the data and
 *     report the CRC-32.
 * tests/formats/zip_py compares this unit with CPython 3.13 on 30 archives.
 *
 * The unit is self-contained: it uses the Deflate, bzip2, LZMA, CRC and
 * SHA-256 codecs, never the archive framework. Texts it returns (errors and
 * warnings, the wording of the matching Python exceptions) are allocated
 * with xx_rt_malloc; release them with xx_zip_py_free_text(). Allocation
 * failures are reported as XX_ZIP_PY_NO_MEMORY.
 *
 * Threads: the unit has no mutable statics, so several threads may read
 * members of one opened archive at once. xxfclib initializes its CPU feature
 * flags and (on Windows) its heap handle lazily and without a lock: call
 * xx_has_sse2() and one xx_rt_malloc()/xx_rt_free() pair before starting
 * worker threads.
 */

#ifndef XXFCLIB_ZIP_PY_H
#define XXFCLIB_ZIP_PY_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XX_ZIP_PY_STORED 0
#define XX_ZIP_PY_DEFLATED 8
#define XX_ZIP_PY_BZIP2 12
#define XX_ZIP_PY_LZMA 14
#define XX_ZIP_PY_XZ 95

/** zipfile.ZipInfo */
typedef struct xx_zip_py_info {
    char *filename;              /* UTF-8, sanitized like ZipInfo.filename */
    char *orig_filename;         /* UTF-8, as decoded from the central directory */
    size_t orig_filename_length; /* may contain NUL characters */
    uint16_t flag_bits;
    uint16_t compress_type;
    uint8_t extract_version;
    uint32_t crc;
    uint64_t compress_size;
    uint64_t file_size;
    int64_t header_offset; /* absolute, prepended data included */
    int64_t end_offset;    /* next local header or the central directory */
} xx_zip_py_info;

/** zipfile.ZipFile over a memory buffer */
typedef struct xx_zip_py {
    const uint8_t *data;
    size_t size;
    xx_zip_py_info *infos;
    size_t count;
} xx_zip_py;

typedef enum xx_zip_py_status {
    XX_ZIP_PY_OK = 0,
    XX_ZIP_PY_BAD,             /* zipfile.BadZipFile */
    XX_ZIP_PY_OVERLAPPED,      /* BadZipFile("Overlapped entries ...") */
    XX_ZIP_PY_NOT_IMPLEMENTED, /* NotImplementedError: method or flag */
    XX_ZIP_PY_ENCRYPTED,       /* RuntimeError: password required */
    XX_ZIP_PY_DATA_ERROR,      /* zlib/bz2/lzma error or EOFError */
    XX_ZIP_PY_NO_MEMORY        /* out of memory */
} xx_zip_py_status;

/** Who owns the bytes xx_zip_py_read() returns. */
typedef enum xx_zip_py_owner {
    XX_ZIP_PY_OWNED,    /* heap memory (xx_rt_malloc) */
    XX_ZIP_PY_BORROWED, /* points into the archive's data */
    XX_ZIP_PY_MAPPED    /* reserved address space */
} xx_zip_py_owner;

/** zipfile.is_zipfile(): an end of central directory record can be found. */
XXFC_API bool xx_zip_py_is_zipfile(const uint8_t *data, size_t size);

/** zipfile.ZipFile(data). The data must outlive the archive. *error (may be
 * NULL) receives the exception text on failure. */
XXFC_API xx_zip_py_status xx_zip_py_open(xx_zip_py *zip, const uint8_t *data, size_t size, char **error);
XXFC_API void xx_zip_py_close(xx_zip_py *zip);

/** ZipInfo.is_dir() */
XXFC_API bool xx_zip_py_info_is_dir(const xx_zip_py_info *info);

/** The checks of ZipFile.open() for reading, in CPython's order. On success
 * *data_offset is the absolute offset of the member's data. A warning about
 * a member sharing its local header with another one is stored in *warning,
 * also when a later check fails (CPython emits a UserWarning and goes on).
 * Methods other than stored, Deflate, bzip2 and LZMA (XZ and Deflate64
 * among them) are XX_ZIP_PY_NOT_IMPLEMENTED, as in zipfile. */
XXFC_API xx_zip_py_status xx_zip_py_open_entry(const xx_zip_py *zip, const xx_zip_py_info *info, uint64_t *data_offset,
                                               char **warning, char **error);

/** ZipExtFile.read(): decompress a member opened by xx_zip_py_open_entry().
 * limit = 0 reads the whole member; a positive limit stops after that many
 * bytes, like read(limit). *complete reports whether the member's end was
 * reached; the CRC-32 is only verified then, as ZipExtFile does.
 * A stored member is not copied: *out then points into the archive's data,
 * which must outlive it, and *owner is XX_ZIP_PY_BORROWED. Large output is
 * written into reserved address space on 64-bit Windows (XX_ZIP_PY_MAPPED).
 * Release *out with xx_zip_py_free(*out, *owner). */
XXFC_API xx_zip_py_status xx_zip_py_read(const xx_zip_py *zip, const xx_zip_py_info *info, uint64_t data_offset,
                                         size_t limit, uint8_t **out, size_t *out_size, bool *complete,
                                         xx_zip_py_owner *owner, char **error);

/** Release bytes from xx_zip_py_read() or xx_zip_py_xz_decompress()
 * (nothing for borrowed ones). */
XXFC_API void xx_zip_py_free(uint8_t *data, xx_zip_py_owner owner);

/** Release a text returned by this unit. */
XXFC_API void xx_zip_py_free_text(char *text);

/** The member's raw data located the way a program reading it by hand does:
 * the local header's name and extra lengths locate the data, compress_size
 * bytes long (cut at the end of the archive). False for a truncated local
 * header. */
XXFC_API bool xx_zip_py_raw_data(const xx_zip_py *zip, const xx_zip_py_info *info, const uint8_t **data, size_t *size);

/** lzma.decompress(data) with the default FORMAT_AUTO: .xz streams whose
 * filter chain is LZMA2 (concatenated streams too) and .lzma (LZMA_Alone)
 * data, with liblzma's error texts. On success *out (owned, release with
 * xx_zip_py_free(*out, XX_ZIP_PY_OWNED)) holds *out_size bytes. */
XXFC_API bool xx_zip_py_xz_decompress(const uint8_t *data, size_t size, uint8_t **out, size_t *out_size, char **error);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ZIP_PY_H */
