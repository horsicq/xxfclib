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

#ifndef XXFCLIB_ELF_INSPECT_H
#define XXFCLIB_ELF_INSPECT_H

#include "xxfclib/formats/elf/xx_elf.h"
#include "xxfclib/formats/xx_executable_inspect.h"


// Inspection strings and arrays are owned by this state; initialize it to zero.
// Release with xx_elf_inspect_free before reuse. Offsets are relative to reader base.
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t nNameIndex;
    uint32_t nType;
    uint64_t nAddr;
    uint64_t nOffset;
    uint64_t nSize;
    char *pName; /* resolved via .shstrtab */
} xx_elf_inspect_section;

typedef struct {
    uint32_t nType;
    uint64_t nOffset;
    uint64_t nVaddr;
    uint64_t nFileSize;
} xx_elf_inspect_program;

/* An ELF, parsed to the surface the database scripts touch: the header
 * fields, the section table with resolved names, the program headers, the
 * dynamic library list, the general-options string and the entry-point file
 * offset. Mirrors xx_elf_inspection + ELF_Script.                                        */
typedef struct {
    xx_executable_input *pInput;
    int bValid;
    int bIs64;
    int bBigEndian;

    uint16_t nType;
    uint16_t nMachine;
    uint32_t nVersion;
    uint64_t nEntry;
    uint64_t nPhoff;
    uint64_t nShoff;
    uint32_t nFlags;
    uint16_t nEhsize;
    uint16_t nPhentsize;
    uint64_t nPhnum;
    uint16_t nShentsize;
    uint64_t nShnum;
    uint64_t nShstrndx;

    xx_elf_inspect_section *pSections;
    int nSectionCount;
    xx_elf_inspect_program *pPrograms;
    int nProgramCount;

    xx_list_s vecLibraries; /* char *, DT_NEEDED names */
    char *pRunPath;
    char sGeneralOptions[64];

    int64_t nEntryPointOffset; /* -1 if the entry cannot be mapped */
    int64_t nOverlayOffset;
    int64_t nOverlaySize;

    /* Verbose "Operation system" line (xx_elf_inspection::getFileFormatInfo): OS name from
     * the OSABI / interpreter / .comment distro, its version and the arch. */
    uint8_t nOsAbi;
    char sOsName[24];
    char sOsVersion[24];
    char sArch[24];
} xx_elf_inspection;

/* Independent native inspection for callers that do not need the full reader
 * vtable. Borrows device; state owns a read-only view. */
XXFC_API int xx_elf_inspect_analyze_from_device(xx_elf_inspection *state,
    xx_io_device *device, int64_t base, xx_pd_struct *pd);

XXFC_API int xx_elf_inspect_parse(xx_elf *reader, xx_elf_inspection *pElf, xx_pd_struct *pd);
XXFC_API void xx_elf_inspect_free(xx_elf_inspection *pElf);

/* Section helpers (index-based, matching the script API). */
XXFC_API int xx_elf_inspect_section_number(xx_elf_inspection *pElf, const char *pName);   /* -1 if absent */
XXFC_API int xx_elf_inspect_section_present(xx_elf_inspection *pElf, const char *pName);
XXFC_API uint64_t xx_elf_inspect_section_offset(xx_elf_inspection *pElf, int nNumber);
XXFC_API uint64_t xx_elf_inspect_section_size(xx_elf_inspection *pElf, int nNumber);

XXFC_API uint64_t xx_elf_inspect_program_offset(xx_elf_inspection *pElf, int nNumber);
XXFC_API uint64_t xx_elf_inspect_program_size(xx_elf_inspection *pElf, int nNumber);

XXFC_API int xx_elf_inspect_library_present(xx_elf_inspection *pElf, const char *pName);

/* isStringInTablePresent: an exact NUL-delimited string inside a named
 * section (a string table). */
XXFC_API int xx_elf_inspect_string_in_table_present(xx_elf_inspection *pElf, const char *pSectionName, const char *pString);

#ifdef __cplusplus
}
#endif

#endif
