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

#ifndef CDIE_XISO9660_H
#define CDIE_XISO9660_H

#include "../../die_engine/die_engine_bin.h"
#include "../../die_engine/die_engine_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A field of the ISO 9660 Primary Volume Descriptor (always at sector 16,
 * offset 0x8000). Reads nSize Latin-1 bytes at 0x8000+nFieldOffset, stops at
 * the first NUL, trims leading/trailing whitespace and returns the result as
 * an owned UTF-8 string. Mirrors
 *   QString::fromLatin1(read_array(nPVDOffset + off, size)).trimmed()
 * so getApplicationIdentifier uses (574, 128) and getDataPreparerIdentifier
 * uses (446, 128).                                                          */
char *iso9660_identifier(DieFile *pFile, cd_i64 nFieldOffset, cd_i64 nFieldSize);

#ifdef __cplusplus
}
#endif

#endif
