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

#ifndef CDIE_XDEX_H
#define CDIE_XDEX_H

#include "../../die_engine/die_engine_bin.h"
#include "../../die_engine/die_engine_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A Dalvik executable, parsed as far as the database queries need: the version
 * from the magic, the map-items hash that becomes the format detail, and the
 * two string lists the `isDexStringPresent` / `isDexItemStringPresent`
 * predicates scan. Mirrors XDEX + DEX_Script.                               */
typedef struct {
    int bValid;
    int bBigEndian;
    char sVersion[8];  /* "035", "037", ... */
    cd_u32 nMapHash;

    CDVec vecStrings;      /* every string_id string (char *) */
    CDVec vecItemStrings;  /* the type-descriptor strings     */
} XDEX;

int xdex_parse(DieFile *pFile, XDEX *pDex);
void xdex_free(XDEX *pDex);

/* getFileFormatOptions: the map hash as 8 lowercase hex digits. Heap string. */
char *xdex_map_hash_hex(XDEX *pDex);

/* Membership tests (exact match), matching isStringInListPresent. */
int xdex_string_present(XDEX *pDex, const char *pString);
int xdex_item_string_present(XDEX *pDex, const char *pString);

/* The Android release for the verbose "Operation system" line: the DEX
 * version maps to an API level and then to a release string (XDEX::getOsVersion
 * via getAndroidVersionFromApi); an unlisted version returns itself. */
const char *xdex_android_version(XDEX *pDex);

#ifdef __cplusplus
}
#endif

#endif
