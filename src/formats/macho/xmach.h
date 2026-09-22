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

#ifndef CDIE_XMACH_H
#define CDIE_XMACH_H

#include "../../die_engine/die_engine_bin.h"
#include "../../die_engine/die_engine_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *pName;        /* basename after the last '/'   */
    cd_u32 nCurrentVersion;
} XMachLibrary;

typedef struct {
    char sName[17];     /* sectname, NUL-terminated       */
    cd_u64 nOffset;
    cd_u64 nSize;
} XMachSection;

/* A Mach-O, parsed to the surface the database queries: the loaded dynamic
 * libraries (LC_LOAD_DYLIB), the sections, and the entry-point file offset.
 * Mirrors XMACH + MACH_Script.                                              */
typedef struct {
    int bValid;
    int bIs64;
    int bBigEndian;

    XMachLibrary *pLibraries;
    int nLibraryCount;
    XMachSection *pSections;
    int nSectionCount;

    cd_i64 nEntryPointOffset; /* -1 if none */

    /* Verbose "Operation system" line (XMACH::getFileFormatInfo): the OS name,
     * its version (may be a range, or "" when unknown) and the CPU arch. */
    cd_u32 nCpuType;
    cd_u32 nCpuSubType;
    char sOsName[16];
    char sOsVersion[24];
    char sArch[24];
} XMACH;

int xmach_parse(DieFile *pFile, XMACH *pMach);
void xmach_free(XMACH *pMach);

int xmach_library_present(XMACH *pMach, const char *pName);
cd_u32 xmach_library_current_version(XMACH *pMach, const char *pName);
int xmach_section_number(XMACH *pMach, const char *pName);
int xmach_section_present(XMACH *pMach, const char *pName);
cd_u64 xmach_section_offset(XMACH *pMach, int nNumber);
cd_u64 xmach_section_size(XMACH *pMach, int nNumber);

#ifdef __cplusplus
}
#endif

#endif
