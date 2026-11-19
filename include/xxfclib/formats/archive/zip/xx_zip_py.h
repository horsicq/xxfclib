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
 * Reading a member follows ZipExtFile step by step: the member's data is
 * taken in the chunks ZipExtFile reads (at least 4096 bytes), and three
 * decoders inside the unit decode each chunk as far as the zlib (1.3.1),
 * libbz2 and liblzma of CPython 3.13 would, with their checks at the same
 * points and CPython's own handling of their output on top. So a read
 * returns what CPython returns, sees the end of the member (and checks the
 * CRC-32) when CPython does, and fails with CPython's exception, including
 * the cases that depend on a chunk boundary: bzip2 and LZMA data is decoded
 * a whole chunk at a time whatever was asked for, a declared size smaller
 * than the data cuts it, data that ends early is left to the CRC-32, and
 * data that is invalid raises the decompressor's error.
 *
 * Deliberate differences from CPython. These keep a whole archive readable
 * where CPython would raise for it:
 *   - a member name whose UTF-8 flag is set but whose bytes are not UTF-8
 *     (UnicodeDecodeError in CPython) gets U+FFFD for each invalid byte;
 *   - a truncated extra record (BadZipFile "Corrupt extra field") ends the
 *     extra parsing;
 *   - extract_version > 63 (NotImplementedError) is ignored;
 *   - is_zipfile() answers false where CPython's lets BadZipFile escape (a
 *     ZIP64 locator for several disks).
 * The archive is read as zipfile reads io.BytesIO, not a file object. They
 * differ when the end record lies less than 76 bytes into the data and a
 * ZIP64 locator is found before it: io.BytesIO seeks to 0 where a file fails
 * to seek. xx_zip_py_file_seek_fails() tells the case a file answers with
 * "File is not a zip file"; with the end record less than 20 bytes in, a
 * file skips the locator io.BytesIO reads at offset 0 (not reported).
 * A member whose header offset comes out negative (a central directory
 * offset that is too large) is ValueError("negative seek value -N"), as
 * io.BytesIO.seek() raises it; a file object raises OSError(EINVAL) there.
 * These change a text only:
 *   - the encrypted-member error names the member as ZipFile.open(name)
 *     does, also when CPython was given a ZipInfo;
 *   - EOFError has no text in CPython; here it is "EOFError".
 * Not reproduced: MemoryError where liblzma cannot allocate the dictionary
 * an LZMA member declares (the dictionary grows with the output here), and
 * what libbz2 does with code tables that no encoder writes and that make it
 * read memory it never set.
 * tests/formats/zip_py compares this unit with CPython 3.13 on 62 archives.
 *
 * The unit is self-contained: it uses the Deflate (for whole members whose
 * stream is sound, in its zlib-strict mode), CRC and SHA-256 codecs, never
 * the archive framework. Texts it returns (errors and warnings, the wording of
 * the matching Python exceptions, names as Python's repr() shows them) are
 * allocated with xx_rt_malloc; release
 * them with xx_zip_py_free_text(). Allocation failures are reported as
 * XX_ZIP_PY_NO_MEMORY.
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
    char **warnings; /* the UserWarnings ZipFile() emitted, in order */
    size_t warning_count;
} xx_zip_py;

typedef enum xx_zip_py_status {
    XX_ZIP_PY_OK = 0,
    XX_ZIP_PY_BAD,             /* zipfile.BadZipFile */
    XX_ZIP_PY_OVERLAPPED,      /* BadZipFile("Overlapped entries ...") */
    XX_ZIP_PY_NOT_IMPLEMENTED, /* NotImplementedError: method or flag */
    XX_ZIP_PY_ENCRYPTED,       /* RuntimeError: password required */
    XX_ZIP_PY_DATA_ERROR,      /* zlib.error, OSError (bz2), LZMAError or EOFError */
    XX_ZIP_PY_NO_MEMORY,       /* out of memory */
    XX_ZIP_PY_VALUE_ERROR      /* ValueError: a negative header offset */
} xx_zip_py_status;

/** Who owns the bytes xx_zip_py_read() returns. */
typedef enum xx_zip_py_owner {
    XX_ZIP_PY_OWNED,    /* heap memory (xx_rt_malloc) */
    XX_ZIP_PY_BORROWED, /* points into the archive's data */
    XX_ZIP_PY_MAPPED    /* reserved address space */
} xx_zip_py_owner;

/** zipfile.is_zipfile(): an end of central directory record can be found. */
XXFC_API bool xx_zip_py_is_zipfile(const uint8_t *data, size_t size);

/** Whether zipfile reads the same data differently from a file object than
 * from io.BytesIO, which is what this unit follows: a ZIP64 locator stands
 * so near the start that the ZIP64 end record it announces would begin
 * before it. Seeking there fails on a file (is_zipfile(file) is False,
 * ZipFile(file) raises BadZipFile("File is not a zip file")); io.BytesIO
 * seeks to 0 and reads on. */
XXFC_API bool xx_zip_py_file_seek_fails(const uint8_t *data, size_t size);

/** zipfile.ZipFile(data). The data must outlive the archive. *error (may be
 * NULL) receives the exception text on failure. On success zip->warnings
 * holds the texts of the UserWarnings CPython emits while it reads the
 * central directory ("Empty unicode path extra field (0x7075)"). */
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

/** ZipExtFile.read() on a member opened by xx_zip_py_open_entry(), from the
 * member's start: limit = 0 is read(), a positive limit is read(limit).
 * *complete is ZipExtFile._eof after the call: the end of the member was
 * seen, and only then was the CRC-32 checked. read() always gets there.
 * read(limit) gets there when the chunks it had to take (at least 4096
 * bytes of the member's data each; bzip2 and LZMA decode them whole) reach
 * the end; *out then holds everything decoded, which can be more than limit
 * bytes: the first limit bytes are what read(limit) returns, the others are
 * what ZipExtFile keeps buffered and a following read() returns, so *out is
 * the whole member. When *complete is false, *out holds exactly limit bytes.
 * Errors are those CPython raises in the same call: XX_ZIP_PY_BAD for
 * "Bad CRC-32 for file ...", XX_ZIP_PY_DATA_ERROR with the exception's text
 * for zlib.error ("Error -3 while decompressing data: ..."), OSError from
 * bz2 ("Invalid data stream"), LZMAError ("Corrupt input data", "Invalid or
 * unsupported options") and EOFError ("EOFError").
 * A stored member is not copied: *out then points into the archive's data,
 * which must outlive it, and *owner is XX_ZIP_PY_BORROWED. Large output is
 * written into reserved address space on 64-bit Windows (XX_ZIP_PY_MAPPED).
 * Release *out with xx_zip_py_free(*out, *owner). */
XXFC_API xx_zip_py_status xx_zip_py_read(const xx_zip_py *zip, const xx_zip_py_info *info, uint64_t data_offset,
                                         size_t limit, uint8_t **out, size_t *out_size, bool *complete,
                                         xx_zip_py_owner *owner, char **error);

/** ZipExtFile.read(first) followed by read() on the same ZipExtFile: the
 * whole member (both results, one after the other), for a program that
 * looks at a member's first bytes before it reads the rest. The two reads
 * take the member's data in other chunks than one read() does, so data that
 * ends early or lies about its size can give another result than
 * xx_zip_py_read() with limit 0; everything else about xx_zip_py_read()
 * applies. first = 0 is read() alone. */
XXFC_API xx_zip_py_status xx_zip_py_read_rest(const xx_zip_py *zip, const xx_zip_py_info *info, uint64_t data_offset,
                                              size_t first, uint8_t **out, size_t *out_size, xx_zip_py_owner *owner,
                                              char **error);

/** Release bytes from xx_zip_py_read(), xx_zip_py_read_rest() or xx_zip_py_xz_decompress()
 * (nothing for borrowed ones). */
XXFC_API void xx_zip_py_free(uint8_t *data, xx_zip_py_owner owner);

/** Release a text returned by this unit. */
XXFC_API void xx_zip_py_free_text(char *text);

/** The member's raw data located the way a program reading it by hand does:
 * the local header's name and extra lengths locate the data, compress_size
 * bytes long (cut at the end of the archive). False for a truncated local
 * header. */
XXFC_API bool xx_zip_py_raw_data(const xx_zip_py *zip, const xx_zip_py_info *info, const uint8_t **data, size_t *size);

/** lzma.decompress(data) with the default FORMAT_AUTO: .xz streams (LZMA2,
 * alone or after the delta and BCJ filters of liblzma 5.2: x86, PowerPC,
 * IA-64, ARM, ARM-Thumb, SPARC) and .lzma (LZMA_Alone) data, one stream after
 * the other. The data is decoded in liblzma's order, so the result is CPython's
 * also for data that is cut or damaged: the texts of LZMAError ("Corrupt
 * input data", "Input format not supported by decoder", "Invalid or
 * unsupported options"), and "Compressed data ended before the end-of-stream
 * marker was reached" wherever a stream, the first or a later one, is not
 * all there when the data ends without a fault (no data at all, and a few
 * zeros after a stream, included). A fault in a later stream ends the
 * decoding and what was decoded before is returned.
 * Filters CPython 3.13's liblzma does not have (ARM64, RISC-V) are "Invalid
 * or unsupported options", as there. Not reproduced: MemoryError for a
 * dictionary liblzma cannot allocate (the dictionary grows with the output
 * here).
 * On success *out (owned, release with xx_zip_py_free(*out,
 * XX_ZIP_PY_OWNED)) holds *out_size bytes. */
XXFC_API bool xx_zip_py_xz_decompress(const uint8_t *data, size_t size, uint8_t **out, size_t *out_size, char **error);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ZIP_PY_H */
