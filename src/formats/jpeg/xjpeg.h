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

/* xjpeg.h - JPEG segment walker plus the small TIFF/EXIF reader needed to
 * recover the camera make and model.                                       */

#ifndef XJPEG_H
#define XJPEG_H

#include "../../die_engine/die_engine_bin.h"

typedef struct {
    cd_u8 nId;
    int bEntropyCodedData;
    cd_i64 nDataOffset;
    cd_i64 nDataSize;
} XJpegChunk;

typedef struct {
    DieFile *pFile;
    int bValid;

    XJpegChunk *pChunks;
    int nChunkCount;

    char *pVersion;        /* JFIF version, e.g. "1.1" */
    char *pComment;        /* concatenated COM segments */
    char *pDqtMD5;         /* lowercase MD5 of all DQT payloads */
    char *pExifCameraName; /* "Make(Model)" */

    cd_i64 nExifOffset;
    cd_i64 nExifSize;
} XJpeg;

int xjpeg_parse(XJpeg *pJpeg, DieFile *pFile);
void xjpeg_free(XJpeg *pJpeg);
int xjpeg_is_chunk_present(XJpeg *pJpeg, cd_u8 nId);

#endif /* XJPEG_H */
