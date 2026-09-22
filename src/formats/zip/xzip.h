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

#ifndef CDIE_XZIP_H
#define CDIE_XZIP_H

#include "../../die_engine/die_engine_bin.h"
#include "../../die_engine/die_engine_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A ZIP-family archive (ZIP/JAR/APK/NPM/IPA) walked far enough for the archive
 * script predicates: the list of central-directory record names, and the
 * decompressed META-INF/MANIFEST.MF text if present. Mirrors the pieces of
 * XArchive::getRecords and Archive_Script the database uses.                */
typedef struct {
    int bValid;
    CDVec vecNames;       /* owned char *: every central-directory record name */
    char *pManifestText;  /* decompressed META-INF/MANIFEST.MF, or NULL        */
    char *pPackageJson;   /* decompressed package/package.json, or NULL        */
    char sJvmVersion[24]; /* Java release of the first *.class member (XJAR:
                           * the verbose "Virtual machine" line), "" if none   */
} XZip;

int xzip_parse(DieFile *pFile, XZip *pZip);
void xzip_free(XZip *pZip);

/* isArchiveRecordPresent: exact, case-sensitive name equality. */
int xzip_record_present(XZip *pZip, const char *pName);

/* getManifestRecord: value of `pKey: ` up to the next newline, '\r' removed,
 * or "" when absent. Owned string. Mirrors JAR_Script::getManifestRecord.   */
char *xzip_manifest_record(XZip *pZip, const char *pKey);

/* getPackageJsonRecord: the top-level JSON value of pKey from
 * package/package.json when it is a string, else "" (matching
 * QJsonObject::value(key).toString(), which is empty for non-strings).
 * Owned string. */
char *xzip_packagejson_record(XZip *pZip, const char *pKey);

#ifdef __cplusplus
}
#endif

#endif
