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

#include "die_engine_bin.h"

#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/entropy/xx_entropy.h"
#include "xxfclib/algo/hash/xx_hash.h"
#include "xxfclib/data/xx_data.h"

/* --------------------------------------------------------------- file --- */

int die_file_open(DieFile *pFile, const char *pFileName)
{
    xx_io_device *pSource = NULL;
    int64_t nTotal = 0;
    size_t nRead = 0;

    x_memset(pFile, 0, sizeof(*pFile));

    pSource = xx_io_file_open(pFileName, "rb");

    if (pSource == NULL) {
        return 0;
    }

    nTotal = xx_io_total_size(pSource);

    /* One allocation holds the file, so the length has to fit a size_t with
     * room for the terminator. */
    if ((nTotal < 0) || ((cd_u64)nTotal >= (cd_u64)(size_t)-1)) {
        xx_io_close(pSource);
        return 0;
    }

    pFile->pData = (unsigned char *)cd_malloc((size_t)nTotal + 1);

    if (nTotal > 0) {
        /* A short read is not an error: the size recorded is what was
         * actually obtained, and the scan runs against that. */
        ssize_t nGot = xx_io_read(pSource, pFile->pData, (size_t)nTotal);

        if (nGot > 0) {
            nRead = (size_t)nGot;
        }
    }

    pFile->pData[nRead] = 0;
    pFile->nSize = (cd_i64)nRead;
    xx_io_close(pSource);

    pFile->pDevice = xx_io_mem_open_ro(pFile->pData, (size_t)pFile->nSize);

    if (pFile->pDevice == NULL) {
        cd_free(pFile->pData);
        x_memset(pFile, 0, sizeof(*pFile));
        return 0;
    }

    pFile->pFileName = cd_strdup(pFileName);

    return 1;
}

int die_file_adopt(DieFile *pFile, unsigned char *pData, cd_i64 nSize,
                   const char *pName)
{
    x_memset(pFile, 0, sizeof(*pFile));

    if ((pData == NULL) || (nSize < 0)) {
        cd_free(pData);
        return 0;
    }

    pFile->pDevice = xx_io_mem_open_ro(pData, (size_t)nSize);

    if (pFile->pDevice == NULL) {
        cd_free(pData);
        return 0;
    }

    pFile->pData = pData;
    pFile->nSize = nSize;
    pFile->pFileName = cd_strdup(pName ? pName : "");

    return 1;
}

void die_file_close(DieFile *pFile)
{
    if (pFile->pDevice != NULL) {
        xx_io_close(pFile->pDevice);
    }

    cd_free(pFile->pData);
    cd_free(pFile->pFileName);
    x_memset(pFile, 0, sizeof(*pFile));
}

/* Overflow-safe range check. Signature scripts can pass arbitrary numbers as
 * offsets (a few database rules genuinely do), so the arithmetic must never
 * wrap around. */
static int die_check(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    if ((nOffset < 0) || (nSize < 0) || (nOffset > pFile->nSize)) {
        return 0;
    }

    return (nSize <= pFile->nSize - nOffset) ? 1 : 0;
}

/* Clamps [nOffset, nOffset + *pnSize) to the file. Returns 0 when the range
 * lies completely outside. */
int die_range_clamp(DieFile *pFile, cd_i64 nOffset, cd_i64 *pnSize)
{
    if ((nOffset < 0) || (nOffset >= pFile->nSize)) {
        return 0;
    }

    if ((*pnSize < 0) || (*pnSize > pFile->nSize - nOffset)) {
        *pnSize = pFile->nSize - nOffset;
    }

    return 1;
}

/* ---------------------------------------------------------- memory map --- */

int die_map_add_part(xx_memory_map *pMap, cd_i64 nOffset, cd_i64 nSize,
                     cd_u64 nAddress, cd_u64 nVirtualSize,
                     xx_file_part_t filePart, const char *pName)
{
    xx_memory_record record;

    if (pMap == NULL) {
        return 0;
    }

    if (nSize > 0) {
        x_memset(&record, 0, sizeof(record));
        record.offset = nOffset;
        /* No virtual extent means no address: an overlay is file content
         * that is not loaded anywhere, and an address lookup must pass over
         * it rather than resolve into it. */
        record.address = (nVirtualSize > 0U) ? nAddress : XX_INVALID_ADDRESS;
        record.size = nSize;
        record.file_part = filePart;
        x_strncpy(record.name, pName ? pName : "", sizeof(record.name) - 1);

        if (!xx_memory_map_add_record(pMap, &record)) {
            return 0;
        }
    }

    /* The part of the virtual extent with no file bytes behind it. Reaching
     * it during an address lookup means the address belongs to this part but
     * has nothing to read, which is an answer, not a reason to keep looking. */
    if ((nVirtualSize > 0U) && (nVirtualSize > (cd_u64)(nSize > 0 ? nSize : 0))) {
        cd_u64 nTailSize = nVirtualSize - (cd_u64)(nSize > 0 ? nSize : 0);

        if (nTailSize > (cd_u64)INT64_MAX) {
            return 0;
        }

        x_memset(&record, 0, sizeof(record));
        record.offset = -1;
        record.address = nAddress + (cd_u64)(nSize > 0 ? nSize : 0);
        record.size = (cd_i64)nTailSize;
        record.file_part = filePart;
        record.is_virtual = true;
        x_strncpy(record.name, pName ? pName : "", sizeof(record.name) - 1);

        if (!xx_memory_map_add_record(pMap, &record)) {
            return 0;
        }
    }

    return 1;
}

/* ------------------------------------------------------------ strings --- */

char *die_ansi_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize)
{
    CDBuf buf;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    if (nMaxSize <= 0) {
        nMaxSize = 0x10000;
    }

    for (i = 0; i < nMaxSize; i++) {
        cd_u8 nChar = 0;

        if (!die_check(pFile, nOffset + i, 1)) {
            break;
        }

        nChar = xx_io_get_u8(pFile->pDevice, nOffset + i);

        if (nChar == 0) {
            break;
        }

        cdbuf_append_ch(&buf, (char)nChar);
    }

    return cdbuf_detach(&buf, NULL);
}

char *die_utf8_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize)
{
    return die_ansi_string(pFile, nOffset, nMaxSize);
}

static void append_utf8(CDBuf *pBuf, unsigned int nCode)
{
    if (nCode < 0x80) {
        cdbuf_append_ch(pBuf, (char)nCode);
    } else if (nCode < 0x800) {
        cdbuf_append_ch(pBuf, (char)(0xC0 | (nCode >> 6)));
        cdbuf_append_ch(pBuf, (char)(0x80 | (nCode & 0x3F)));
    } else {
        cdbuf_append_ch(pBuf, (char)(0xE0 | (nCode >> 12)));
        cdbuf_append_ch(pBuf, (char)(0x80 | ((nCode >> 6) & 0x3F)));
        cdbuf_append_ch(pBuf, (char)(0x80 | (nCode & 0x3F)));
    }
}

char *die_unicode_string_n(DieFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize,
                           int bBigEndian, cd_i64 *pnUnits)
{
    CDBuf buf;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    if (nMaxSize <= 0) {
        nMaxSize = 0x10000;
    }

    for (i = 0; i < nMaxSize; i++) {
        cd_u16 nChar = 0;

        if (!die_check(pFile, nOffset + i * 2, 2)) {
            break;
        }

        nChar = xx_io_get_u16(pFile->pDevice, nOffset + i * 2,
                              bBigEndian ? true : false);

        if (nChar == 0) {
            break;
        }

        append_utf8(&buf, nChar);
    }

    if (pnUnits != NULL) {
        *pnUnits = i;
    }

    return cdbuf_detach(&buf, NULL);
}

char *die_unicode_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize,
                         int bBigEndian)
{
    return die_unicode_string_n(pFile, nOffset, nMaxSize, bBigEndian, NULL);
}

char *die_ucsd_string(DieFile *pFile, cd_i64 nOffset)
{
    CDBuf buf;
    cd_i64 nSize = 0;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    nSize = (cd_i64)xx_io_get_u8(pFile->pDevice, nOffset);

    if (nSize > 0x10000) {
        nSize = 0x10000;
    }

    /* read_uint8 yields 0 past EOF, so the payload is always nSize characters
     * long; every embedded 0x00 (real or out of range) becomes a space. */
    for (i = 0; i < nSize; i++) {
        cd_u8 nByte = xx_io_get_u8(pFile->pDevice, nOffset + 1 + i);

        if (nByte == 0) {
            nByte = 0x20;
        }

        cdbuf_append_ch(&buf, (char)nByte);
    }

    return cdbuf_detach(&buf, NULL);
}

char *die_uuid(DieFile *pFile, cd_i64 nOffset)
{
    /* XBinary::read_UUID with the default little-endian flag: the read and the
     * hex formatting swap by the same flag, so on a little-endian target the
     * two swaps cancel and the first four fields are the little-endian values;
     * the last field is the six bytes in file order. Lower case, hyphenated. */
    static const char *pDigits = "0123456789abcdef";
    CDBuf buf;
    cd_u32 nA = xx_io_get_u32(pFile->pDevice, nOffset + 0, false);
    cd_u32 nB = xx_io_get_u16(pFile->pDevice, nOffset + 4, false);
    cd_u32 nC = xx_io_get_u16(pFile->pDevice, nOffset + 6, false);
    cd_u32 nD = xx_io_get_u16(pFile->pDevice, nOffset + 8, false);
    int nShift = 0;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    for (nShift = 28; nShift >= 0; nShift -= 4) {
        cdbuf_append_ch(&buf, pDigits[(nA >> nShift) & 0xF]);
    }

    cdbuf_append_ch(&buf, '-');

    for (nShift = 12; nShift >= 0; nShift -= 4) {
        cdbuf_append_ch(&buf, pDigits[(nB >> nShift) & 0xF]);
    }

    cdbuf_append_ch(&buf, '-');

    for (nShift = 12; nShift >= 0; nShift -= 4) {
        cdbuf_append_ch(&buf, pDigits[(nC >> nShift) & 0xF]);
    }

    cdbuf_append_ch(&buf, '-');

    for (nShift = 12; nShift >= 0; nShift -= 4) {
        cdbuf_append_ch(&buf, pDigits[(nD >> nShift) & 0xF]);
    }

    cdbuf_append_ch(&buf, '-');

    for (i = 0; i < 6; i++) {
        cd_u8 nByte = xx_io_get_u8(pFile->pDevice, nOffset + 10 + i);

        cdbuf_append_ch(&buf, pDigits[nByte >> 4]);
        cdbuf_append_ch(&buf, pDigits[nByte & 0x0F]);
    }

    return cdbuf_detach(&buf, NULL);
}

char *die_signature_hex(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    static const char *pDigits = "0123456789ABCDEF";
    CDBuf buf;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    if (!die_range_clamp(pFile, nOffset, &nSize)) {
        return cdbuf_detach(&buf, NULL);
    }

    for (i = 0; i < nSize; i++) {
        unsigned char nByte = pFile->pData[nOffset + i];

        cdbuf_append_ch(&buf, pDigits[nByte >> 4]);
        cdbuf_append_ch(&buf, pDigits[nByte & 0x0F]);
    }

    return cdbuf_detach(&buf, NULL);
}

/* ---------------------------------------------------------- searching --- */

cd_i64 die_find_bytes(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize,
                      const unsigned char *pNeedle, cd_i64 nNeedleSize)
{
    cd_i64 nFound = 0;

    if (nNeedleSize <= 0) {
        return -1;
    }

    if (!die_range_clamp(pFile, nOffset, &nSize)) {
        return -1;
    }

    if (nNeedleSize > nSize) {
        return -1;
    }

    /* xx_data_find_bytes_buffer_optimize searches to the end of whatever buffer it is given,
     * so the window is expressed by shortening the buffer rather than by a
     * length argument. The result is relative to that buffer. */
    nFound = xx_data_find_bytes_buffer_optimize(pFile->pData + nOffset, (size_t)nSize, 0,
                                                pNeedle, (size_t)nNeedleSize, NULL);

    return (nFound < 0) ? -1 : (nOffset + nFound);
}

cd_i64 die_find_ansi_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize,
                            const char *pString)
{
    return die_find_bytes(pFile, nOffset, nSize, (const unsigned char *)pString,
                          (cd_i64)x_strlen(pString));
}

cd_i64 die_find_unicode_string(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize,
                               const char *pString, int bBigEndian)
{
    size_t nLength = x_strlen(pString);
    unsigned char *pNeedle = (unsigned char *)cd_malloc(nLength * 2 + 2);
    size_t i = 0;
    cd_i64 nResult = 0;

    for (i = 0; i < nLength; i++) {
        if (bBigEndian) {
            pNeedle[i * 2] = 0;
            pNeedle[i * 2 + 1] = (unsigned char)pString[i];
        } else {
            pNeedle[i * 2] = (unsigned char)pString[i];
            pNeedle[i * 2 + 1] = 0;
        }
    }

    nResult = die_find_bytes(pFile, nOffset, nSize, pNeedle,
                             (cd_i64)(nLength * 2));
    cd_free(pNeedle);

    return nResult;
}

cd_i64 die_find_u8(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u8 nValue)
{
    return die_find_bytes(pFile, nOffset, nSize, &nValue, 1);
}

cd_i64 die_find_u16(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u16 nValue)
{
    unsigned char sBuf[2];

    sBuf[0] = (unsigned char)(nValue & 0xFF);
    sBuf[1] = (unsigned char)(nValue >> 8);

    return die_find_bytes(pFile, nOffset, nSize, sBuf, 2);
}

cd_i64 die_find_u32(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u32 nValue)
{
    unsigned char sBuf[4];

    sBuf[0] = (unsigned char)(nValue & 0xFF);
    sBuf[1] = (unsigned char)((nValue >> 8) & 0xFF);
    sBuf[2] = (unsigned char)((nValue >> 16) & 0xFF);
    sBuf[3] = (unsigned char)((nValue >> 24) & 0xFF);

    return die_find_bytes(pFile, nOffset, nSize, sBuf, 4);
}

/* --------------------------------------------------------- statistics --- */

double die_entropy(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    if ((!die_range_clamp(pFile, nOffset, &nSize)) || (nSize <= 0)) {
        return 0.0;
    }

    return xx_entropy_calculate(pFile->pData + nOffset, (size_t)nSize);
}

int die_is_zero_filled(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    cd_i64 i = 0;

    if ((nSize <= 0) || (!die_check(pFile, nOffset, nSize))) {
        return 0;
    }

    for (i = 0; i < nSize; i++) {
        if (pFile->pData[nOffset + i]) {
            return 0;
        }
    }

    return 1;
}

/* ------------------------------------------------------------ digests --- */

char *die_md5_hex(const void *pData, size_t nSize)
{
    static const char *pDigits = "0123456789ABCDEF";
    unsigned char sDigest[XX_MD5_DIGEST_SIZE];
    char *pResult = (char *)cd_malloc(33);
    int i = 0;

    /* A zero-length range still has a digest -- that of the empty message --
     * and the scripts print it, so this must not shortcut to all zeroes. */
    if (!xx_md5_memory(pData, nSize, sDigest)) {
        x_memset(sDigest, 0, sizeof(sDigest));
    }

    /* Uppercase, unlike xx_hash_to_hex: the reference prints it that way. */
    for (i = 0; i < XX_MD5_DIGEST_SIZE; i++) {
        pResult[i * 2] = pDigits[sDigest[i] >> 4];
        pResult[i * 2 + 1] = pDigits[sDigest[i] & 0x0F];
    }

    pResult[32] = 0;

    return pResult;
}

char *die_md5(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    if (!die_range_clamp(pFile, nOffset, &nSize)) {
        nOffset = 0;
        nSize = 0;
    }

    return die_md5_hex(pFile->pData + nOffset, (size_t)nSize);
}

cd_u32 die_crc32(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u32 nInit)
{
    if (!die_range_clamp(pFile, nOffset, &nSize)) {
        return nInit;
    }

    return xx_crc32_calc(nInit, pFile->pData + nOffset, (size_t)nSize);
}

cd_u32 die_adler32(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    if (!die_range_clamp(pFile, nOffset, &nSize)) {
        return 1;
    }

    return xx_adler32(pFile->pData + nOffset, (size_t)nSize);
}

cd_u16 die_crc16(DieFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u16 nInit)
{
    if (!die_range_clamp(pFile, nOffset, &nSize)) {
        return nInit;
    }

    return xx_crc16_arc_calc(nInit, pFile->pData + nOffset, (size_t)nSize);
}

cd_u32 die_string_crc32c(const char *pString)
{
    size_t nSize = pString ? x_strlen(pString) : 0;

    return xx_crc32c_calc(0xFFFFFFFFu, pString, nSize);
}
