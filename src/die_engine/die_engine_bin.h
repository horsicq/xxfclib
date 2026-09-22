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

/* die_engine_bin.h - the file under scan, and the readers with no xx_ twin.
 *
 * This replaces the former xb.h. Everything xb offered that xxfclib already
 * implements is gone rather than relocated: scalar fields are read with
 * xx_io_get_*, searches with xx_io/xx_data, and the four checksums with
 * xx_crc / xx_adler32 / xx_hash. What is left here are the DIE presentation
 * helpers -- the string shapes and the two statistics the script API exposes
 * -- which have no general-purpose equivalent because they are not general
 * purpose: their clamping, their defaults and their hex casing are part of
 * the output diec prints.
 *
 * DieFile keeps both a device and the buffer behind it. The device is what
 * every field read goes through; the buffer exists because the script API
 * hands whole ranges to the JS side and because the signature engine matches
 * against contiguous bytes. They are two views of one allocation, never two
 * copies.
 */

#ifndef DIE_ENGINE_BIN_H
#define DIE_ENGINE_BIN_H

#include "die_engine_compat.h"
/* XFileType and the XFT_* constants: the parsers that read a DieFile all
 * classify what they found. */
#include "xxfclib/die_engine/die_engine.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_data.h"   /* xx_io_get_* -- the field readers */
#include "xxfclib/formats/xx_memory_map.h"

/* A whole file loaded into memory, with a read-only device over it. */
typedef struct {
    xx_io_device *pDevice; /**< Read-only view of pData. Field reads go here. */
    unsigned char *pData;  /**< The bytes. Owned. NUL-terminated for safety. */
    cd_i64 nSize;          /**< Bytes actually read, which may be short. */
    char *pFileName;
} DieFile;

/** Clamp [nOffset, nOffset + *pnSize) to the file, so a negative size means
 *  "to the end". @return 0 when the range lies wholly outside the file. */
int die_range_clamp(DieFile *pFile, cd_i64 nOffset, cd_i64 *pnSize);

/** @return 1 on success. On failure the struct is zeroed and 0 is returned. */
int die_file_open(DieFile *pFile, const char *pFileName);

/**
 * Take over a buffer the caller already allocated and give it a device, so
 * an in-memory scan is the same object as a file-backed one. Ownership of
 * @p pData passes to the DieFile; on failure it is released here.
 *
 * This exists so that nothing outside this file ever fills a DieFile in by
 * hand: a struct with pData set but pDevice left NULL reads as a zero-length
 * file through every accessor, which is a silent wrong answer rather than an
 * error.
 *
 * @return 1 on success; 0 with the struct zeroed on failure.
 */
int die_file_adopt(DieFile *pFile, unsigned char *pData, cd_i64 nSize,
                   const char *pName);

void die_file_close(DieFile *pFile);

/* ---------------------------------------------------------- memory map -- */

/**
 * Add one part to a map the DIE way: a raw extent and a virtual extent in a
 * single call.
 *
 * xx_memory_map_add_part does almost this, and the difference is the point of
 * the wrapper. A part with no virtual extent at all -- an overlay -- is not
 * merely unmapped here, it must be invisible to an address lookup, so its
 * address is stored as XX_INVALID_ADDRESS rather than as the zero the caller
 * passes. That is what makes xx_memory_map_address_to_offset_ex() under
 * XX_MEMORY_MAP_LOOKUP_FIRST_MATCH answer exactly as the DIE map did.
 *
 * @return 0 if the map refused the records.
 */
int die_map_add_part(xx_memory_map *pMap, cd_i64 nOffset, cd_i64 nSize,
                     cd_u64 nAddress, cd_u64 nVirtualSize,
                     xx_file_part_t filePart, const char *pName);

/* ------------------------------------------------------------- strings -- */

/* A NUL-terminated Latin-1 string. A non-positive @p nMaxSize means the DIE
 * default of 64 KiB, not "unbounded" -- the scripts rely on the cap. Never
 * returns NULL: a bad offset yields an empty string, which is what the
 * reference produces and what the callers print. Caller frees. */
char *die_ansi_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize);
/* Same bytes, same rules; separate name because the scripts distinguish. */
char *die_utf8_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize);
/* A NUL-terminated UTF-16 string converted to UTF-8. Caller frees. */
char *die_unicode_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize,
                         int bBigEndian);
/* As die_unicode_string, and also reports how many UTF-16 code units the
 * string occupies on disk -- which is not the length of the UTF-8 result. */
char *die_unicode_string_n(DieFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize,
                           int bBigEndian, cd_i64 *pnUnits);
/* UCSD/Pascal string: a uint8 length then that many bytes, with embedded
 * NULs shown as spaces (XBinary::read_ucsdString). Caller frees. */
char *die_ucsd_string(DieFile *pFile, cd_i64 nOffset);
/* A 16-byte GUID as "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", lower case. */
char *die_uuid(DieFile *pFile, cd_i64 nOffset);
/* Uppercase hex of a region. Caller frees. */
char *die_signature_hex(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize);

/* ----------------------------------------------------------- searching -- */

/* All of these clamp [nOffset, nOffset + nSize) to the file first, so a
 * negative nSize means "to the end", and return an absolute offset or -1. */
cd_i64 die_find_bytes(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize,
                      const unsigned char *pNeedle, cd_i64 nNeedleSize);
cd_i64 die_find_ansi_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize,
                            const char *pString);
cd_i64 die_find_unicode_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize,
                               const char *pString, int bBigEndian);
cd_i64 die_find_u8(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u8 nValue);
cd_i64 die_find_u16(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize,
                    cd_u16 nValue);
cd_i64 die_find_u32(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize,
                    cd_u32 nValue);

/* --------------------------------------------------------- statistics -- */

/** Shannon entropy in bits per byte over a clamped range; 0 for an empty one. */
double die_entropy(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize);
/** Unlike the rest, this does NOT clamp: a range past the end is not zeroes. */
int die_is_zero_filled(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize);

/* ------------------------------------------------------------- digests -- */

/* Uppercase MD5 hex of a clamped range, as the scripts print it. Computed by
 * xx_hash; only the casing is local. Caller frees. */
char *die_md5(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize);
/** The same rendering over a plain buffer, for callers assembling one. */
char *die_md5_hex(const void *pData, size_t nSize);

/* Thin range-clamping adapters over the shared checksums. Each was its own
 * loop in xb.c; the equivalences are covered by probe/xbdiff. */
cd_u32 die_crc32(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u32 nInit);
cd_u32 die_adler32(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize);
cd_u16 die_crc16(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u16 nInit);

/* The DIE import hash: CRC-32C over a string, starting from a zero register
 * rather than the usual all-ones. xx_crc32c_calc complements its seed, so
 * 0xFFFFFFFF is what produces that start -- hence the wrapper. */
cd_u32 die_string_crc32c(const char *pString);

#endif /* DIE_ENGINE_BIN_H */
