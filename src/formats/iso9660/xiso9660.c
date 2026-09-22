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

#include "xiso9660.h"

#define ISO9660_PVD_OFFSET 0x8000

/* The characters QChar::isSpace() reports true for within the Latin-1 range:
 * the tab..CR controls, the NEL control (0x85), the ordinary space and the
 * no-break space. QString::trimmed() strips these from both ends.           */
static int iso_is_space(cd_u8 nByte)
{
    return (nByte == 0x09) || (nByte == 0x0A) || (nByte == 0x0B) || (nByte == 0x0C) ||
           (nByte == 0x0D) || (nByte == 0x20) || (nByte == 0x85) || (nByte == 0xA0);
}

static void append_utf8(CDBuf *pBuf, cd_u8 nByte)
{
    /* Latin-1 code point nByte -> UTF-8 (one byte below 0x80, else two). */
    if (nByte < 0x80) {
        cdbuf_append_ch(pBuf, (char)nByte);
    } else {
        cdbuf_append_ch(pBuf, (char)(0xC0 | (nByte >> 6)));
        cdbuf_append_ch(pBuf, (char)(0x80 | (nByte & 0x3F)));
    }
}

char *iso9660_identifier(DieFile *pFile, cd_i64 nFieldOffset, cd_i64 nFieldSize)
{
    cd_i64 nBase = ISO9660_PVD_OFFSET + nFieldOffset;
    cd_i64 nLength = 0;
    cd_i64 nStart = 0;
    cd_i64 nEnd = 0;
    cd_i64 i = 0;
    CDBuf buf;

    /* Latin-1 decode up to the first NUL (fromLatin1 over the byte array). */
    for (nLength = 0; nLength < nFieldSize; nLength++) {
        if (xx_io_get_u8(pFile->pDevice, nBase + nLength) == 0) {
            break;
        }
    }

    /* trimmed(): drop leading and trailing whitespace. */
    nStart = 0;
    nEnd = nLength;

    while ((nStart < nEnd) && iso_is_space(xx_io_get_u8(pFile->pDevice, nBase + nStart))) {
        nStart++;
    }

    while ((nEnd > nStart) && iso_is_space(xx_io_get_u8(pFile->pDevice, nBase + nEnd - 1))) {
        nEnd--;
    }

    cdbuf_init(&buf);

    for (i = nStart; i < nEnd; i++) {
        append_utf8(&buf, xx_io_get_u8(pFile->pDevice, nBase + i));
    }

    return cdbuf_detach(&buf, NULL);
}
