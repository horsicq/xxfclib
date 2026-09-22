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

#ifndef CDIE_XELF_H
#define CDIE_XELF_H

#include "../../die_engine/die_engine_bin.h"
#include "../../die_engine/die_engine_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    cd_u32 nNameIndex;
    cd_u32 nType;
    cd_u64 nAddr;
    cd_u64 nOffset;
    cd_u64 nSize;
    char *pName; /* resolved via .shstrtab */
} XElfSection;

typedef struct {
    cd_u32 nType;
    cd_u64 nOffset;
    cd_u64 nVaddr;
    cd_u64 nFileSize;
} XElfProgram;

/* An ELF, parsed to the surface the database scripts touch: the header
 * fields, the section table with resolved names, the program headers, the
 * dynamic library list, the general-options string and the entry-point file
 * offset. Mirrors XELF + ELF_Script.                                        */
typedef struct {
    int bValid;
    int bIs64;
    int bBigEndian;

    cd_u16 nType;
    cd_u16 nMachine;
    cd_u32 nVersion;
    cd_u64 nEntry;
    cd_u64 nPhoff;
    cd_u64 nShoff;
    cd_u32 nFlags;
    cd_u16 nEhsize;
    cd_u16 nPhentsize;
    cd_u16 nPhnum;
    cd_u16 nShentsize;
    cd_u16 nShnum;
    cd_u16 nShstrndx;

    XElfSection *pSections;
    int nSectionCount;
    XElfProgram *pPrograms;
    int nProgramCount;

    CDVec vecLibraries; /* char *, DT_NEEDED names */
    char *pRunPath;
    char sGeneralOptions[64];

    cd_i64 nEntryPointOffset; /* -1 if the entry cannot be mapped */
    cd_i64 nOverlayOffset;
    cd_i64 nOverlaySize;

    /* Verbose "Operation system" line (XELF::getFileFormatInfo): OS name from
     * the OSABI / interpreter / .comment distro, its version and the arch. */
    cd_u8 nOsAbi;
    char sOsName[24];
    char sOsVersion[24];
    char sArch[24];
} XELF;

int xelf_parse(DieFile *pFile, XELF *pElf);
void xelf_free(XELF *pElf);

/* Section helpers (index-based, matching the script API). */
int xelf_section_number(XELF *pElf, const char *pName);   /* -1 if absent */
int xelf_section_present(XELF *pElf, const char *pName);
cd_u64 xelf_section_offset(XELF *pElf, int nNumber);
cd_u64 xelf_section_size(XELF *pElf, int nNumber);

cd_u64 xelf_program_offset(XELF *pElf, int nNumber);
cd_u64 xelf_program_size(XELF *pElf, int nNumber);

int xelf_library_present(XELF *pElf, const char *pName);

/* isStringInTablePresent: an exact NUL-delimited string inside a named
 * section (a string table). */
int xelf_string_in_table_present(DieFile *pFile, XELF *pElf, const char *pSectionName, const char *pString);

#ifdef __cplusplus
}
#endif

#endif
