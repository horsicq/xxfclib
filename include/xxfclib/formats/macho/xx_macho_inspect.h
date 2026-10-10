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

#ifndef XXFCLIB_MACHO_INSPECT_H
#define XXFCLIB_MACHO_INSPECT_H

#include "xxfclib/formats/macho/xx_macho.h"
#include "xxfclib/formats/xx_executable_inspect.h"

// Inspection strings and arrays are owned by this state; initialize it to zero.
// Release with xx_macho_inspect_free before reuse. Offsets are relative to reader base.
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *pName; /* basename after the last '/'   */
    uint32_t nCurrentVersion;
} xx_macho_inspect_library;

typedef struct {
    char sName[17]; /* sectname, NUL-terminated       */
    uint64_t nOffset;
    uint64_t nSize;
} xx_macho_inspect_section;

/* A Mach-O, parsed to the surface the database queries: the loaded dynamic
 * libraries (LC_LOAD_DYLIB), the sections, and the entry-point file offset.
 * Mirrors xx_macho_inspection + MACH_Script.                                              */
typedef struct {
    xx_executable_input *pInput;
    int bValid;
    int bIs64;
    int bBigEndian;

    xx_macho_inspect_library *pLibraries;
    int nLibraryCount;
    xx_macho_inspect_section *pSections;
    int nSectionCount;

    int64_t nEntryPointOffset; /* -1 if none */

    /* Verbose "Operation system" line (xx_macho_inspection::getFileFormatInfo): the OS name,
     * its version (may be a range, or "" when unknown) and the CPU arch. */
    uint32_t nCpuType;
    uint32_t nCpuSubType;
    char sOsName[16];
    char sOsVersion[24];
    char sArch[24];
} xx_macho_inspection;

/* Independent native inspection for callers that do not need the full reader
 * vtable. Borrows device; state owns a read-only view. */
XXFC_API int xx_macho_inspect_analyze_from_device(xx_macho_inspection *state, xx_io_device *device, int64_t base, xx_pd_struct *pd);

XXFC_API int xx_macho_inspect_parse(xx_macho *reader, xx_macho_inspection *pMach, xx_pd_struct *pd);
XXFC_API void xx_macho_inspect_free(xx_macho_inspection *pMach);

XXFC_API int xx_macho_inspect_library_present(xx_macho_inspection *pMach, const char *pName);
XXFC_API uint32_t xx_macho_inspect_library_current_version(xx_macho_inspection *pMach, const char *pName);
XXFC_API int xx_macho_inspect_section_number(xx_macho_inspection *pMach, const char *pName);
XXFC_API int xx_macho_inspect_section_present(xx_macho_inspection *pMach, const char *pName);
XXFC_API uint64_t xx_macho_inspect_section_offset(xx_macho_inspection *pMach, int nNumber);
XXFC_API uint64_t xx_macho_inspect_section_size(xx_macho_inspection *pMach, int nNumber);

#ifdef __cplusplus
}
#endif

#endif
