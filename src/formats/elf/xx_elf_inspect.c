#include "xxfclib/formats/elf/xx_elf_inspect.h"
#include "../xx_executable_inspect_internal.h"
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

/* Native ELF inspection helpers used by DIE and other metadata queries.
 *
 * Covers what the ELF database directory queries: the header, the section
 * table (with names resolved through .shstrtab), the program headers, the
 * dynamic library list (DT_NEEDED via the mapped string table), the
 * "TYPE MACHINE-BITS" general-options string, the entry-point file offset for
 * compareEP, and a raw-size/overlay estimate.
 */

#define ELF_PT_NOTE 4
#define ELF_SHT_NOTE 7

/* _TABLE_XELF_Machines: e_machine -> arch name (xx_elf_inspection::getArch). */
static const char *elf_machine_name(uint16_t m)
{
    switch (m) {
        case 0: return "NONE";
        case 1: return "M32";
        case 2: return "SPARC";
        case 3: return "386";
        case 4: return "68K";
        case 5: return "88K";
        case 6: return "486";
        case 7: return "860";
        case 8: return "MIPS";
        case 9: return "S370";
        case 10: return "MIPS_RS3_LE";
        case 11: return "RS6000";
        case 15: return "PARISC";
        case 16: return "nCUBE";
        case 17: return "VPP500";
        case 18: return "SPARC32PLUS";
        case 19: return "960";
        case 20: return "PPC";
        case 21: return "PPC64";
        case 22: return "S390";
        case 23: return "SPU";
        case 36: return "V800";
        case 37: return "FR20";
        case 38: return "RH32";
        case 39: return "RCE";
        case 40: return "ARM";
        case 41: return "ALPHA";
        case 42: return "SH";
        case 43: return "SPARCV9";
        case 44: return "TRICORE";
        case 45: return "ARC";
        case 46: return "H8_300";
        case 47: return "H8_300H";
        case 48: return "H8S";
        case 49: return "H8_500";
        case 50: return "IA_64";
        case 51: return "MIPS_X";
        case 52: return "COLDFIRE";
        case 53: return "68HC12";
        case 54: return "MMA";
        case 55: return "PCP";
        case 56: return "NCPU";
        case 57: return "NDR1";
        case 58: return "STARCORE";
        case 59: return "ME16";
        case 60: return "ST100";
        case 61: return "TINYJ";
        case 62: return "AMD64";
        case 63: return "PDSP";
        case 66: return "FX66";
        case 67: return "ST9PLUS";
        case 68: return "ST7";
        case 69: return "68HC16";
        case 70: return "68HC11";
        case 71: return "68HC08";
        case 72: return "68HC05";
        case 73: return "SVX";
        case 74: return "ST19";
        case 75: return "VAX";
        case 76: return "CRIS";
        case 77: return "JAVELIN";
        case 78: return "FIREPATH";
        case 79: return "ZSP";
        case 80: return "MMIX";
        case 81: return "HUANY";
        case 82: return "PRISM";
        case 83: return "AVR";
        case 84: return "FR30";
        case 85: return "D10V";
        case 86: return "D30V";
        case 87: return "V850";
        case 88: return "M32R";
        case 89: return "MN10300";
        case 90: return "MN10200";
        case 91: return "PJ";
        case 92: return "OPENRISC";
        case 93: return "ARC_A5";
        case 94: return "XTENSA";
        case 95: return "VIDEOCORE";
        case 96: return "TMM_GPP";
        case 97: return "NS32K";
        case 98: return "TPC";
        case 99: return "SNP1K";
        case 100: return "ST200";
        case 101: return "IP2K";
        case 102: return "MAX";
        case 103: return "CR";
        case 104: return "F2MC16";
        case 105: return "MSP430";
        case 106: return "BLACKFIN";
        case 107: return "SE_C33";
        case 108: return "SEP";
        case 109: return "ARCA";
        case 110: return "UNICORE";
        case 111: return "EXCESS";
        case 112: return "DXP";
        case 113: return "ALTERA_NIOS2";
        case 114: return "CRX";
        case 115: return "XGATE";
        case 116: return "C166";
        case 117: return "M16C";
        case 118: return "DSPIC30F";
        case 119: return "CE";
        case 120: return "M32C";
        case 140: return "TI_C6000";
        case 183: return "AARCH64";
        case 243: return "RISC_V";
        case 258: return "LOONGARCH";
        case 0x5441: return "FRV";
        case 0x18ad: return "AVR32";
        case 0x9026: return "ALPHA";
        case 0x9080: return "CYGNUS_V850";
        case 0x9041: return "CYGNUS_M32R";
        case 0xA390: return "S390_OLD";
        case 0xbeef: return "CYGNUS_MN10300";
        default: return "Unknown";
    }
}

/* e_ident[EI_OSABI] -> OS name, or NULL when it leaves the default "Unix". */
static const char *elf_osabi_osname(uint8_t nOsAbi)
{
    switch (nOsAbi) {
        case 1: return "Hewlett-Packard HP-UX";
        case 2: return "NetBSD";
        case 3: return "Linux";
        case 6: return "Sun Solaris";
        case 7: return "AIX";
        case 8: return "IRIX";
        case 9: return "FreeBSD";
        case 10: return "Compaq TRU64 UNIX";
        case 11: return "Novell Modesto";
        case 12: return "OpenBSD";
        case 13: return "Open VMS";
        case 14: return "Hewlett-Packard Non-Stop Kernel";
        case 15: return "Amiga Research OS";
        case 16: return "FenixOS";
        case 18: return "Open VOS";
        default: return NULL;
    }
}

/* XBinary::getAndroidVersionFromApi (used by the ELF Android note). */
static const char *elf_android_version(uint32_t nApi)
{
    switch (nApi) {
        case 1: return "1.0";
        case 2: return "1.1";
        case 3: return "1.5";
        case 4: return "1.6";
        case 5: return "2.0";
        case 6: return "2.0.1";
        case 7: return "2.1";
        case 8: return "2.2.X";
        case 9: return "2.3-2.3.2";
        case 10: return "2.3.3-2.3.7";
        case 11: return "3.0";
        case 12: return "3.1";
        case 13: return "3.2.X";
        case 14: return "4.0.1-4.0.2";
        case 15: return "4.0.3-4.0.4";
        case 16: return "4.1.X";
        case 17: return "4.2.X";
        case 18: return "4.3.X";
        case 19: return "4.4-4.4.4";
        case 20: return "4.4W";
        case 21: return "5.0";
        case 22: return "5.1";
        case 23: return "6.0";
        case 24: return "7.0";
        case 25: return "7.1";
        case 26: return "8.0";
        case 27: return "8.1";
        case 28: return "9.0";
        case 29: return "10.0";
        case 30: return "11.0";
        case 31: return "12.0";
        case 32: return "12.1";
        case 33: return "13.0";
        case 34: return "14.0";
        case 35: return "15.0";
        case 36: return "16.0";
        default: return "Unknown";
    }
}

static void elf_set_str(char *pDst, size_t nDstSize, const char *pSrc)
{
    xx_rt_strncpy(pDst, pSrc, nDstSize - 1);
    pDst[nDstSize - 1] = 0;
}

/* appendText(sVer, sABI, ", "): join with ", " when both are non-empty. */
static void elf_append_version(char *pVer, size_t nVerSize, const char *pAdd)
{
    size_t nLen = 0;

    if ((pAdd == NULL) || (pAdd[0] == 0)) {
        return;
    }

    nLen = xx_rt_strlen(pVer);

    if (nLen > 0) {
        xx_rt_strncpy(pVer + nLen, ", ", nVerSize - nLen - 1);
        pVer[nVerSize - 1] = 0; /* xx_rt_strncpy need not NUL-terminate when full */
        nLen = xx_rt_strlen(pVer);
    }

    xx_rt_strncpy(pVer + nLen, pAdd, nVerSize - nLen - 1);
    pVer[nVerSize - 1] = 0;
}

static int elf_section_index(xx_elf_inspection *pElf, const char *pName)
{
    int i = 0;

    for (i = 0; i < pElf->nSectionCount; i++) {
        if ((pElf->pSections[i].pName != NULL) && (xx_rt_strcmp(pElf->pSections[i].pName, pName) == 0)) {
            return i;
        }
    }

    return -1;
}

/* The program interpreter (PT_INTERP), else the .interp section. Owned. */
static char *elf_interp(xx_executable_input *pFile, xx_elf_inspection *pElf)
{
    int i = 0;
    int nSec = -1;

    for (i = 0; i < pElf->nProgramCount; i++) {
        if (pElf->pPrograms[i].nType == 3 /* PT_INTERP */) {
            return xx_exec_string(pFile, (int64_t)pElf->pPrograms[i].nOffset, (int64_t)pElf->pPrograms[i].nFileSize);
        }
    }

    nSec = elf_section_index(pElf, ".interp");

    if (nSec >= 0) {
        return xx_exec_string(pFile, (int64_t)pElf->pSections[nSec].nOffset, (int64_t)pElf->pSections[nSec].nSize);
    }

    return xx_str_create("");
}

/* Clamps a raw offset/size pair taken from a header to the end of the file.
 * The test is done in uint64_t because adding the two 64-bit fields as int64_t
 * overflows on a malformed header. */
static int64_t elf_clamp_end(xx_executable_input *pFile, uint64_t nOffset, uint64_t nSize)
{
    if ((nOffset > (uint64_t)pFile->size) || (nSize > (uint64_t)pFile->size - nOffset)) {
        return pFile->size;
    }

    return (int64_t)(nOffset + nSize);
}

/* Finds an ELF note by (type, name); nWantType < 0 matches any type. Notes are
 * taken from the PT_NOTE segments, or the SHT_NOTE sections when there is no
 * PT_NOTE. On a match returns 1 and the descriptor offset/size. */
static int elf_scan_range_note(xx_executable_input *pFile, int64_t nStart, int64_t nEnd, int bBE, int nWantType, const char *pWantName, int64_t *pnDescOff,
                               uint32_t *pnDescSize)
{
    int64_t p = nStart;

    /* A malformed 64-bit ELF can store an offset with bit 63 set, which is a
     * large negative i64. Reject it so the walk stays bounded (nEnd is already
     * clamped to the file size by the callers). */
    if (nStart < 0) {
        return 0;
    }

    while (p <= nEnd - 12 && !xx_pd_is_stopped(pFile->pd)) {
        uint32_t nNameSz = xx_exec_u32(pFile, p, bBE ? true : false);
        uint32_t nDescSz = xx_exec_u32(pFile, p + 4, bBE ? true : false);
        uint32_t nType = xx_exec_u32(pFile, p + 8, bBE ? true : false);
        int64_t nNameOff = p + 12;
        int64_t nDescOff = nNameOff + (((int64_t)nNameSz + 3) & ~INT64_C(3));
        int64_t nNext = nDescOff + (((int64_t)nDescSz + 3) & ~INT64_C(3));

        if ((nNameSz > 0x100) || (nDescSz > 0x10000) || (nNext <= p) || (nDescOff > nEnd) || (nNext > nEnd)) {
            break;
        }

        if (((nWantType < 0) || ((uint32_t)nWantType == nType)) && (nNameSz > 0)) {
            char *pName = xx_exec_string(pFile, nNameOff, nNameSz);
            int bMatch = pName && (xx_rt_strcmp(pName, pWantName) == 0);

            xx_mem_free(pName);

            if (bMatch) {
                *pnDescOff = nDescOff;
                *pnDescSize = nDescSz;

                return 1;
            }
        }

        p = nNext;
    }

    return 0;
}

static int elf_find_note(xx_executable_input *pFile, xx_elf_inspection *pElf, int nWantType, const char *pWantName, int64_t *pnDescOff, uint32_t *pnDescSize)
{
    int i = 0;
    int bHavePtNote = 0;

    for (i = 0; i < pElf->nProgramCount; i++) {
        if (pElf->pPrograms[i].nType == ELF_PT_NOTE) {
            int64_t nStart = (int64_t)pElf->pPrograms[i].nOffset;
            int64_t nEnd = elf_clamp_end(pFile, pElf->pPrograms[i].nOffset, pElf->pPrograms[i].nFileSize);

            bHavePtNote = 1;

            if (elf_scan_range_note(pFile, nStart, nEnd, pElf->bBigEndian, nWantType, pWantName, pnDescOff, pnDescSize)) {
                return 1;
            }
        }
    }

    if (bHavePtNote) {
        return 0;
    }

    for (i = 0; i < pElf->nSectionCount; i++) {
        if (pElf->pSections[i].nType == ELF_SHT_NOTE) {
            int64_t nStart = (int64_t)pElf->pSections[i].nOffset;
            int64_t nEnd = elf_clamp_end(pFile, pElf->pSections[i].nOffset, pElf->pSections[i].nSize);

            if (elf_scan_range_note(pFile, nStart, nEnd, pElf->bBigEndian, nWantType, pWantName, pnDescOff, pnDescSize)) {
                return 1;
            }
        }
    }

    return 0;
}

/* Copies the .comment slice after pAfter up to (but not including) the first
 * pStop character (or end of the string) into pOut. */
static void elf_section_after(const char *pComment, const char *pAfter, char cStop, char *pOut, size_t nOutSize)
{
    char *pAt = xx_rt_strstr(pComment, pAfter);

    pOut[0] = 0;

    if (pAt != NULL) {
        const char *pStart = pAt + xx_rt_strlen(pAfter);
        const char *pEnd = pStart;

        while ((*pEnd != 0) && (*pEnd != cStop)) {
            pEnd++;
        }

        {
            size_t nLen = (size_t)(pEnd - pStart);

            if (nLen >= nOutSize) {
                nLen = nOutSize - 1;
            }

            xx_rt_memcpy(pOut, pStart, nLen);
            pOut[nLen] = 0;
        }
    }
}

/* Walks the .comment strings and sets the distribution name/version. Mirrors
 * the xx_elf_inspection::getFileFormatInfo .comment loop. Returns 1 on a match. */
static int elf_comment_distro(xx_executable_input *pFile, xx_elf_inspection *pElf, const char **ppOsName, char *pVer, size_t nVerSize)
{
    int nSec = elf_section_index(pElf, ".comment");
    int64_t nStart = 0;
    int64_t nEnd = 0;
    int64_t p = 0;

    if (nSec < 0) {
        return 0;
    }

    nStart = (int64_t)pElf->pSections[nSec].nOffset;
    nEnd = elf_clamp_end(pFile, pElf->pSections[nSec].nOffset, pElf->pSections[nSec].nSize);

    if (nStart < 0) {
        return 0; /* a bit-63 offset in a malformed 64-bit ELF */
    }

    for (p = nStart; p < nEnd;) {
        char *pC = xx_exec_string(pFile, p, nEnd - p);
        size_t nAdvance;
        if (!pC) {
            pFile->failed = true;
            return 0;
        }
        nAdvance = xx_rt_strlen(pC) + 1;
        int bFound = 0;

        if (xx_rt_strstr(pC, "Ubuntu") || xx_rt_strstr(pC, "ubuntu")) {
            *ppOsName = "Ubuntu Linux";

            if (xx_rt_strstr(pC, "ubuntu1~")) {
                elf_section_after(pC, "ubuntu1~", ')', pVer, nVerSize);
            }

            bFound = 1;
        } else if (xx_rt_strstr(pC, "Debian") || xx_rt_strstr(pC, "debian")) {
            *ppOsName = "Debian Linux";
            bFound = 1;
        } else if (xx_rt_strstr(pC, "StartOS")) {
            *ppOsName = "StartOS Linux";
            bFound = 1;
        } else if (xx_rt_strstr(pC, "Gentoo")) {
            *ppOsName = "Gentoo Linux";
            bFound = 1;
        } else if (xx_rt_strstr(pC, "Alpine")) {
            *ppOsName = "Alpine Linux";
            bFound = 1;
        } else if (xx_rt_strstr(pC, "Wind River Linux")) {
            *ppOsName = "Wind River Linux";
            bFound = 1;
        } else if (xx_rt_strstr(pC, "SuSE") || xx_rt_strstr(pC, "SUSE Linux")) {
            *ppOsName = "SUSE Linux";
            bFound = 1;
        } else if (xx_rt_strstr(pC, "Mandrakelinux") || xx_rt_strstr(pC, "Linux-Mandrake") || xx_rt_strstr(pC, "Mandrake Linux")) {
            *ppOsName = "Mandrake Linux";
            bFound = 1;
        } else if (xx_rt_strstr(pC, "ASPLinux")) {
            *ppOsName = "ASPLinux";
            bFound = 1;
        } else if (xx_rt_strstr(pC, "Red Hat")) {
            *ppOsName = "Red Hat Linux";
            bFound = 1;
        } else if (xx_rt_strstr(pC, "Hancom Linux")) {
            *ppOsName = "Hancom Linux";
            bFound = 1;
        } else if (xx_rt_strstr(pC, "TurboLinux")) {
            *ppOsName = "Turbolinux";
            bFound = 1;
        } else if (xx_rt_strstr(pC, "Vine Linux")) {
            *ppOsName = "Vine Linux";
            bFound = 1;
        }

        if (xx_rt_strcmp(*ppOsName, "Linux") != 0) {
            if (xx_rt_strstr(pC, "SunOS")) {
                *ppOsName = "SunOS";

                if (xx_rt_strstr(pC, "@(#)SunOS ")) {
                    elf_section_after(pC, "@(#)SunOS ", 0, pVer, nVerSize);
                }

                bFound = 1;
            }
        }

        xx_mem_free(pC);

        if (bFound) {
            return 1;
        }

        if (nAdvance == 0) {
            break;
        }

        p += (int64_t)nAdvance;
    }

    return 0;
}

/* Fills pElf->sOsName / sOsVersion / sArch for the verbose "Operation system"
 * line, following xx_elf_inspection::getFileFormatInfo. */
static void elf_compute_os(xx_executable_input *pFile, xx_elf_inspection *pElf)
{
    const char *pOsName = "Unix";
    const char *pAbi = elf_osabi_osname(pElf->nOsAbi);
    char sVer[24];
    char *pInterp = NULL;
    int64_t nDescOff = 0;
    uint32_t nDescSize = 0;
    int bBE = pElf->bBigEndian;

    xx_rt_memset(sVer, 0, sizeof(sVer));

    elf_set_str(pElf->sArch, sizeof(pElf->sArch), elf_machine_name(pElf->nMachine));

    if (pAbi != NULL) {
        pOsName = pAbi;
    }

    pInterp = elf_interp(pFile, pElf);
    if (!pInterp) {
        pFile->failed = true;
        return;
    }

    if ((xx_rt_strcmp(pOsName, "Unix") == 0) && xx_rt_strstr(pInterp, "ld-elf.so")) pOsName = "FreeBSD";
    if ((xx_rt_strcmp(pOsName, "Unix") == 0) && xx_rt_strstr(pInterp, "linux")) pOsName = "Linux";
    if ((xx_rt_strcmp(pOsName, "Unix") == 0) && xx_rt_strstr(pInterp, "ldqnx")) pOsName = "QNX";
    if ((xx_rt_strcmp(pOsName, "Unix") == 0) && xx_rt_strstr(pInterp, "uClibc")) pOsName = "mClinux";

    if ((xx_rt_strcmp(pOsName, "Unix") == 0) || (xx_rt_strcmp(pOsName, "Linux") == 0)) {
        elf_comment_distro(pFile, pElf, &pOsName, sVer, sizeof(sVer));
    }

    if (xx_rt_strcmp(pOsName, "FreeBSD") == 0) {
        int nSec = elf_section_index(pElf, ".comment");

        if (nSec >= 0) {
            char *pC = xx_exec_string(pFile, (int64_t)pElf->pSections[nSec].nOffset, (int64_t)pElf->pSections[nSec].nSize);

            if (pC && xx_rt_strstr(pC, "FreeBSD: release/")) {
                elf_section_after(pC, "FreeBSD: release/", '/', sVer, sizeof(sVer));
            }

            xx_mem_free(pC);
        }
    }

    /* Android: an "Android" note, liblog.so, or the Android linker. */
    if (xx_rt_strcmp(pOsName, "Unix") == 0) {
        if (elf_find_note(pFile, pElf, -1, "Android", &nDescOff, &nDescSize)) {
            pOsName = "Android";

            if (nDescSize >= 4) {
                elf_set_str(sVer, sizeof(sVer), elf_android_version(xx_exec_u32(pFile, nDescOff, bBE ? true : false)));
            }
        } else if (xx_elf_inspect_library_present(pElf, "liblog.so") || (xx_rt_strcmp(pInterp, "system/bin/linker") == 0) ||
                   (xx_rt_strcmp(pInterp, "system/bin/linker64") == 0)) {
            pOsName = "Android";
        }
    }

    /* GNU note (type 1): OS id when still Unix, plus the "ABI: x.y.z" suffix. */
    if (elf_find_note(pFile, pElf, 1, "GNU", &nDescOff, &nDescSize) && (nDescSize >= 16)) {
        uint32_t nOS = xx_exec_u32(pFile, nDescOff, bBE ? true : false);
        uint32_t nMajor = xx_exec_u32(pFile, nDescOff + 4, bBE ? true : false);
        uint32_t nMinor = xx_exec_u32(pFile, nDescOff + 8, bBE ? true : false);
        uint32_t nSub = xx_exec_u32(pFile, nDescOff + 12, bBE ? true : false);
        char sAbi[32];

        if (xx_rt_strcmp(pOsName, "Unix") == 0) {
            if (nOS == 0) pOsName = "Linux";
            else if (nOS == 2) pOsName = "Sun Solaris";
            else if (nOS == 3) pOsName = "FreeBSD";
            else if (nOS == 4) pOsName = "NetBSD";
            else if (nOS == 5) pOsName = "Syllable";
        }

        xx_rt_snprintf(sAbi, sizeof(sAbi), "ABI: %u.%u.%u", (unsigned)nMajor, (unsigned)nMinor, (unsigned)nSub);
        elf_append_version(sVer, sizeof(sVer), sAbi);
    }

    if (xx_rt_strcmp(pOsName, "Unix") == 0) {
        if (elf_section_index(pElf, ".note.android.ident") >= 0) pOsName = "Android";
        else if (elf_section_index(pElf, ".note.minix.ident") >= 0) pOsName = "Minix";
        else if (elf_section_index(pElf, ".note.netbsd.ident") >= 0) pOsName = "NetBSD";
        else if (elf_section_index(pElf, ".note.openbsd.ident") >= 0) pOsName = "OpenBSD";
    }

    if (xx_rt_strcmp(pOsName, "Unix") == 0) {
        xx_rt_snprintf(sVer, sizeof(sVer), "%u", (unsigned)pElf->nOsAbi);
    }

    xx_mem_free(pInterp);

    elf_set_str(pElf->sOsName, sizeof(pElf->sOsName), pOsName);
    elf_set_str(pElf->sOsVersion, sizeof(pElf->sOsVersion), sVer);
}

/* ELF constants used here. */
#define ELF_SHT_NOBITS 8
#define ELF_PT_LOAD 1
#define ELF_PT_DYNAMIC 2
#define ELF_DT_NULL 0
#define ELF_DT_NEEDED 1
#define ELF_DT_STRTAB 5
#define ELF_DT_STRSZ 10
#define ELF_DT_RUNPATH 29

typedef struct {
    uint32_t nId;
    const char *pName;
} ElfIdName;

static const ElfIdName g_elfTypes[] = {{0, "NONE"}, {1, "REL"}, {2, "EXEC"}, {3, "DYN"}, {4, "CORE"}, {5, "NUM"}, {0xff00, "LOPROC"}, {0xffff, "HIPROC"}};

static const ElfIdName g_elfMachines[] = {{0, "NONE"},
                                          {1, "M32"},
                                          {2, "SPARC"},
                                          {3, "386"},
                                          {4, "68K"},
                                          {5, "88K"},
                                          {6, "486"},
                                          {7, "860"},
                                          {8, "MIPS"},
                                          {9, "S370"},
                                          {10, "MIPS_RS3_LE"},
                                          {11, "RS6000"},
                                          {15, "PARISC"},
                                          {16, "nCUBE"},
                                          {17, "VPP500"},
                                          {18, "SPARC32PLUS"},
                                          {19, "960"},
                                          {20, "PPC"},
                                          {21, "PPC64"},
                                          {22, "S390"},
                                          {23, "SPU"},
                                          {36, "V800"},
                                          {37, "FR20"},
                                          {38, "RH32"},
                                          {39, "RCE"},
                                          {40, "ARM"},
                                          {41, "ALPHA"},
                                          {42, "SH"},
                                          {43, "SPARCV9"},
                                          {44, "TRICORE"},
                                          {45, "ARC"},
                                          {46, "H8_300"},
                                          {47, "H8_300H"},
                                          {48, "H8S"},
                                          {49, "H8_500"},
                                          {50, "IA_64"},
                                          {51, "MIPS_X"},
                                          {52, "COLDFIRE"},
                                          {53, "68HC12"},
                                          {54, "MMA"},
                                          {55, "PCP"},
                                          {56, "NCPU"},
                                          {57, "NDR1"},
                                          {58, "STARCORE"},
                                          {59, "ME16"},
                                          {60, "ST100"},
                                          {61, "TINYJ"},
                                          {62, "AMD64"},
                                          {63, "PDSP"},
                                          {66, "FX66"},
                                          {67, "ST9PLUS"},
                                          {68, "ST7"},
                                          {69, "68HC16"},
                                          {70, "68HC11"},
                                          {71, "68HC08"},
                                          {72, "68HC05"},
                                          {73, "SVX"},
                                          {74, "ST19"},
                                          {75, "VAX"},
                                          {76, "CRIS"},
                                          {77, "JAVELIN"},
                                          {78, "FIREPATH"},
                                          {79, "ZSP"},
                                          {80, "MMIX"},
                                          {81, "HUANY"},
                                          {82, "PRISM"},
                                          {83, "AVR"},
                                          {84, "FR30"},
                                          {85, "D10V"},
                                          {86, "D30V"},
                                          {87, "V850"},
                                          {88, "M32R"},
                                          {89, "MN10300"},
                                          {90, "MN10200"},
                                          {91, "PJ"},
                                          {92, "OPENRISC"},
                                          {93, "ARC_A5"},
                                          {94, "XTENSA"},
                                          {95, "VIDEOCORE"},
                                          {96, "TMM_GPP"},
                                          {97, "NS32K"},
                                          {98, "TPC"},
                                          {99, "SNP1K"},
                                          {100, "ST200"},
                                          {101, "IP2K"},
                                          {102, "MAX"},
                                          {103, "CR"},
                                          {104, "F2MC16"},
                                          {105, "MSP430"},
                                          {106, "BLACKFIN"},
                                          {107, "SE_C33"},
                                          {108, "SEP"},
                                          {109, "ARCA"},
                                          {110, "UNICORE"},
                                          {111, "EXCESS"},
                                          {112, "DXP"},
                                          {113, "ALTERA_NIOS2"},
                                          {114, "CRX"},
                                          {115, "XGATE"},
                                          {116, "C166"},
                                          {117, "M16C"},
                                          {118, "DSPIC30F"},
                                          {119, "CE"},
                                          {120, "M32C"},
                                          {140, "TI_C6000"},
                                          {183, "AARCH64"},
                                          {243, "RISC_V"},
                                          {258, "LOONGARCH"},
                                          {0x5441, "FRV"},
                                          {0x18ad, "AVR32"},
                                          {0x9026, "ALPHA"},
                                          {0x9080, "CYGNUS_V850"},
                                          {0x9041, "CYGNUS_M32R"},
                                          {0xA390, "S390_OLD"},
                                          {0xbeef, "CYGNUS_MN10300"}};

static const char *elf_lookup(const ElfIdName *pTable, size_t nCount, uint32_t nId)
{
    size_t i = 0;

    for (i = 0; i < nCount; i++) {
        if (pTable[i].nId == nId) {
            return pTable[i].pName;
        }
    }

    return "";
}

/* Walks the PT_LOAD segments to turn a virtual address into a file offset;
 * returns -1 when no segment covers it. */
static int64_t elf_addr_to_offset(xx_elf_inspection *pElf, uint64_t nAddress)
{
    int i = 0;

    for (i = 0; i < pElf->nProgramCount; i++) {
        xx_elf_inspect_program *pProgram = &pElf->pPrograms[i];

        if ((pProgram->nType == ELF_PT_LOAD) && (pProgram->nFileSize > 0)) {
            if ((nAddress >= pProgram->nVaddr) && (nAddress < pProgram->nVaddr + pProgram->nFileSize)) {
                return (int64_t)(pProgram->nOffset + (nAddress - pProgram->nVaddr));
            }
        }
    }

    return -1;
}

/* Reads a NUL-terminated string at nOffset, bounded by the file. */

static void elf_parse_dynamic(xx_executable_input *pFile, xx_elf_inspection *pElf)
{
    int i = 0;
    int64_t nDynOffset = -1;
    int64_t nDynSize = 0;
    int64_t nStrTabOffset = -1;
    int64_t nStrTabSize = 0;
    int64_t nRunPathValue = -1;
    int64_t nStep = pElf->bIs64 ? 16 : 8;

    for (i = 0; i < pElf->nProgramCount; i++) {
        if (pElf->pPrograms[i].nType == ELF_PT_DYNAMIC) {
            nDynOffset = (int64_t)pElf->pPrograms[i].nOffset;
            nDynSize = (int64_t)pElf->pPrograms[i].nFileSize;

            break;
        }
    }

    if ((nDynOffset < 0) || (nDynOffset > pFile->size)) {
        return;
    }

    if (nDynSize > pFile->size - nDynOffset) nDynSize = pFile->size - nDynOffset;

    /* First pass: locate the string table and its size. */
    {
        int64_t nOffset = nDynOffset;
        int64_t nRemaining = nDynSize;

        while (nRemaining >= nStep && !pFile->failed && !xx_pd_is_stopped(pFile->pd)) {
            int64_t nTag = 0;
            uint64_t nValue = 0;

            if (pElf->bIs64) {
                nTag = (int64_t)xx_exec_u64(pFile, nOffset, pElf->bBigEndian ? true : false);
                nValue = xx_exec_u64(pFile, nOffset + 8, pElf->bBigEndian ? true : false);
            } else {
                nTag = (int64_t)xx_exec_u32(pFile, nOffset, pElf->bBigEndian ? true : false);
                nValue = xx_exec_u32(pFile, nOffset + 4, pElf->bBigEndian ? true : false);
            }

            if (nTag == ELF_DT_NULL) {
                break;
            }

            if (nTag == ELF_DT_STRTAB) {
                nStrTabOffset = elf_addr_to_offset(pElf, nValue);
            } else if (nTag == ELF_DT_STRSZ) {
                nStrTabSize = (int64_t)nValue;
            }

            nOffset += nStep;
            nRemaining -= nStep;
        }
    }

    if ((nStrTabOffset < 0) || (nStrTabOffset >= pFile->size) || (nStrTabSize <= 0)) {
        return;
    }

    if (nStrTabSize > pFile->size - nStrTabOffset) nStrTabSize = pFile->size - nStrTabOffset;

    /* Second pass: collect DT_NEEDED names (and DT_RUNPATH). */
    {
        int64_t nOffset = nDynOffset;
        int64_t nRemaining = nDynSize;

        while (nRemaining >= nStep && !pFile->failed && !xx_pd_is_stopped(pFile->pd)) {
            int64_t nTag = 0;
            uint64_t nValue = 0;

            if (pElf->bIs64) {
                nTag = (int64_t)xx_exec_u64(pFile, nOffset, pElf->bBigEndian ? true : false);
                nValue = xx_exec_u64(pFile, nOffset + 8, pElf->bBigEndian ? true : false);
            } else {
                nTag = (int64_t)xx_exec_u32(pFile, nOffset, pElf->bBigEndian ? true : false);
                nValue = xx_exec_u32(pFile, nOffset + 4, pElf->bBigEndian ? true : false);
            }

            if (nTag == ELF_DT_NULL) {
                break;
            }

            if ((nTag == ELF_DT_NEEDED) && ((int64_t)nValue >= 0) && ((int64_t)nValue < nStrTabSize)) {
                {
                    char *item = xx_exec_string(pFile, nStrTabOffset + (int64_t)nValue, nStrTabSize - (int64_t)nValue);
                    if (!item || !xx_list_append(&pElf->vecLibraries, &item)) {
                        xx_mem_free(item);
                        pFile->failed = true;
                        break;
                    }
                }
            } else if (nTag == ELF_DT_RUNPATH) {
                nRunPathValue = (int64_t)nValue;
            }

            nOffset += nStep;
            nRemaining -= nStep;
        }
    }

    if ((nRunPathValue >= 0) && (nRunPathValue < nStrTabSize)) {
        pElf->pRunPath = xx_exec_string(pFile, nStrTabOffset + nRunPathValue, nStrTabSize - nRunPathValue);
    }
}

/* Raw size = the highest file offset any header or section reaches; anything
 * past it is overlay. */
static void elf_compute_overlay(xx_elf_inspection *pElf, int64_t nFileSize)
{
    int64_t nRawSize = pElf->nEhsize;
    int i = 0;

    if ((int64_t)(pElf->nShoff + (uint64_t)pElf->nShnum * pElf->nShentsize) > nRawSize) {
        nRawSize = (int64_t)(pElf->nShoff + (uint64_t)pElf->nShnum * pElf->nShentsize);
    }

    if ((int64_t)(pElf->nPhoff + (uint64_t)pElf->nPhnum * pElf->nPhentsize) > nRawSize) {
        nRawSize = (int64_t)(pElf->nPhoff + (uint64_t)pElf->nPhnum * pElf->nPhentsize);
    }

    for (i = 0; i < pElf->nSectionCount; i++) {
        if (pElf->pSections[i].nType != ELF_SHT_NOBITS) {
            int64_t nEnd = (int64_t)(pElf->pSections[i].nOffset + pElf->pSections[i].nSize);

            if (nEnd > nRawSize) {
                nRawSize = nEnd;
            }
        }
    }

    for (i = 0; i < pElf->nProgramCount; i++) {
        int64_t nEnd = (int64_t)(pElf->pPrograms[i].nOffset + pElf->pPrograms[i].nFileSize);

        if (nEnd > nRawSize) {
            nRawSize = nEnd;
        }
    }

    if ((nRawSize > 0) && (nRawSize < nFileSize)) {
        pElf->nOverlayOffset = nRawSize;
        pElf->nOverlaySize = nFileSize - nRawSize;
    } else {
        pElf->nOverlayOffset = 0;
        pElf->nOverlaySize = 0;
    }
}

static int inspect_parse_input(xx_executable_input *pFile, xx_elf_inspection *pElf, const xx_elf *reader)
{
    int b64 = reader->elf_class == 2;
    int i = 0;
    int64_t nShStrTabOffset = -1;
    uint64_t nShStrTabSize = 0;
    xx_rt_memset(pElf, 0, sizeof(*pElf));
    xx_list_init(&pElf->vecLibraries, sizeof(char *), NULL);
    pElf->pInput = pFile;
    pElf->nEntryPointOffset = -1;
    if (reader->program_header_count > 65536 || reader->section_header_count > 65536) return 0;
    pElf->bIs64 = b64;
    pElf->bBigEndian = reader->data_encoding == 2;
    pElf->nType = reader->type;
    pElf->nMachine = reader->machine;
    pElf->nOsAbi = reader->os_abi;
    pElf->nVersion = reader->version;
    pElf->nEntry = reader->entry_point;
    pElf->nPhoff = reader->program_header_offset;
    pElf->nShoff = reader->section_header_offset;
    pElf->nFlags = reader->flags;
    pElf->nEhsize = reader->header_size;
    pElf->nPhentsize = reader->program_header_entry_size;
    pElf->nShentsize = reader->section_header_entry_size;
    pElf->nPhnum = reader->program_header_count;
    pElf->nShnum = reader->section_header_count;
    pElf->nShstrndx = reader->section_name_index;
    if (pElf->nPhnum) {
        pElf->pPrograms = (xx_elf_inspect_program *)xx_mem_calloc((size_t)pElf->nPhnum, sizeof(*pElf->pPrograms));
        if (!pElf->pPrograms) return 0;
        for (i = 0; (uint64_t)i < pElf->nPhnum; ++i) {
            const xx_elf_program_header *program = &reader->program_headers[i];
            pElf->pPrograms[i].nType = program->type;
            pElf->pPrograms[i].nOffset = program->offset;
            pElf->pPrograms[i].nVaddr = program->virtual_address;
            pElf->pPrograms[i].nFileSize = program->file_size;
            ++pElf->nProgramCount;
        }
    }
    if (pElf->nShnum) {
        pElf->pSections = (xx_elf_inspect_section *)xx_mem_calloc((size_t)pElf->nShnum, sizeof(*pElf->pSections));
        if (!pElf->pSections) return 0;
        for (i = 0; (uint64_t)i < pElf->nShnum; ++i) {
            const xx_elf_section_header *section = &reader->section_headers[i];
            pElf->pSections[i].nNameIndex = section->name_offset;
            pElf->pSections[i].nType = section->type;
            pElf->pSections[i].nAddr = section->address;
            pElf->pSections[i].nOffset = section->offset;
            pElf->pSections[i].nSize = section->size;
            ++pElf->nSectionCount;
        }
    }
    /* Resolve section names via the .shstrtab section. */
    if (pElf->nShstrndx < (uint64_t)pElf->nSectionCount) {
        nShStrTabOffset = (int64_t)pElf->pSections[pElf->nShstrndx].nOffset;
        nShStrTabSize = pElf->pSections[pElf->nShstrndx].nSize;
    }

    for (i = 0; i < pElf->nSectionCount && !pFile->failed; i++) {
        if (nShStrTabOffset >= 0 && pElf->pSections[i].nNameIndex < nShStrTabSize) {
            pElf->pSections[i].pName =
                xx_exec_string(pFile, nShStrTabOffset + (int64_t)pElf->pSections[i].nNameIndex, (int64_t)(nShStrTabSize - pElf->pSections[i].nNameIndex));
        } else {
            pElf->pSections[i].pName = xx_str_create("");
        }
    }

    elf_parse_dynamic(pFile, pElf);

    /* General options: "TYPE MACHINE-BITS", e.g. "DYN AMD64-64". */
    xx_rt_snprintf(pElf->sGeneralOptions, sizeof(pElf->sGeneralOptions), "%s %s-%s", elf_lookup(g_elfTypes, sizeof(g_elfTypes) / sizeof(g_elfTypes[0]), pElf->nType),
                   elf_lookup(g_elfMachines, sizeof(g_elfMachines) / sizeof(g_elfMachines[0]), pElf->nMachine), b64 ? "64" : "32");

    pElf->nEntryPointOffset = elf_addr_to_offset(pElf, pElf->nEntry);
    elf_compute_overlay(pElf, pFile->size);

    elf_compute_os(pFile, pElf);

    pElf->bValid = 1;

    return 1;
}

void xx_elf_inspect_free(xx_elf_inspection *pElf)
{
    if (!pElf) return;
    int i = 0;
    size_t j = 0;

    for (i = 0; i < pElf->nSectionCount; i++) {
        xx_mem_free(pElf->pSections[i].pName);
    }

    xx_mem_free(pElf->pSections);
    xx_mem_free(pElf->pPrograms);

    for (j = 0; j < pElf->vecLibraries.count; j++) {
        xx_mem_free(((void **)pElf->vecLibraries.data)[j]);
    }

    xx_list_cleanup(&pElf->vecLibraries);
    xx_mem_free(pElf->pRunPath);
    xx_exec_input_free(pElf->pInput);
    xx_rt_memset(pElf, 0, sizeof(*pElf));
}

int xx_elf_inspect_section_number(xx_elf_inspection *pElf, const char *pName)
{
    if (!pElf || !pName) return -1;
    int i = 0;

    for (i = 0; i < pElf->nSectionCount; i++) {
        if (pElf->pSections[i].pName && (xx_rt_strcmp(pElf->pSections[i].pName, pName) == 0)) {
            return i;
        }
    }

    return -1;
}

int xx_elf_inspect_section_present(xx_elf_inspection *pElf, const char *pName)
{
    if (!pElf || !pName) return 0;
    return (xx_elf_inspect_section_number(pElf, pName) != -1) ? 1 : 0;
}

uint64_t xx_elf_inspect_section_offset(xx_elf_inspection *pElf, int nNumber)
{
    if (!pElf) return 0;
    if ((nNumber < 0) || (nNumber >= pElf->nSectionCount)) {
        return 0;
    }

    return pElf->pSections[nNumber].nOffset;
}

uint64_t xx_elf_inspect_section_size(xx_elf_inspection *pElf, int nNumber)
{
    if (!pElf) return 0;
    if ((nNumber < 0) || (nNumber >= pElf->nSectionCount)) {
        return 0;
    }

    return pElf->pSections[nNumber].nSize;
}

uint64_t xx_elf_inspect_program_offset(xx_elf_inspection *pElf, int nNumber)
{
    if (!pElf) return 0;
    if ((nNumber < 0) || (nNumber >= pElf->nProgramCount)) {
        return 0;
    }

    return pElf->pPrograms[nNumber].nOffset;
}

uint64_t xx_elf_inspect_program_size(xx_elf_inspection *pElf, int nNumber)
{
    if (!pElf) return 0;
    if ((nNumber < 0) || (nNumber >= pElf->nProgramCount)) {
        return 0;
    }

    return pElf->pPrograms[nNumber].nFileSize;
}

int xx_elf_inspect_library_present(xx_elf_inspection *pElf, const char *pName)
{
    if (!pElf || !pName) return 0;
    size_t i = 0;

    for (i = 0; i < pElf->vecLibraries.count; i++) {
        if (xx_rt_strcmp((const char *)((void **)pElf->vecLibraries.data)[i], pName) == 0) {
            return 1;
        }
    }

    return 0;
}

int xx_elf_inspect_string_in_table_present(xx_elf_inspection *pElf, const char *pSectionName, const char *pString)
{
    if (!pElf || !pSectionName || !pString) return 0;
    xx_executable_input *pFile = pElf ? pElf->pInput : NULL;
    int nSection = xx_elf_inspect_section_number(pElf, pSectionName);
    int64_t nOffset = 0;
    int64_t nEnd = 0;
    size_t nQueryLen = 0;

    if (!pFile || !pString || nSection < 0) {
        return 0;
    }

    pFile->read_work = 0;
    nOffset = (int64_t)pElf->pSections[nSection].nOffset;
    nQueryLen = xx_rt_strlen(pString);

    /* Range-checked in uint64_t: adding the two raw header fields as int64_t
     * overflows on a malformed section header. */
    if ((pElf->pSections[nSection].nOffset > (uint64_t)pFile->size) || (pElf->pSections[nSection].nSize > (uint64_t)pFile->size - pElf->pSections[nSection].nOffset)) {
        return 0;
    }

    nEnd = nOffset + (int64_t)pElf->pSections[nSection].nSize;

    /* Walk the NUL-delimited entries; the match must be a whole entry. */
    while (nOffset < nEnd && !pFile->failed) {
        int64_t nStart = nOffset;

        while ((nOffset < nEnd) && (xx_exec_u8(pFile, nOffset) != 0)) {
            nOffset++;
        }

        if (nOffset < nEnd && ((size_t)(nOffset - nStart) == nQueryLen) && (xx_exec_match(pFile, nStart, pString, nQueryLen))) {
            return 1;
        }

        nOffset++; /* skip the NUL */
    }

    return 0;
}

int xx_elf_inspect_parse(xx_elf *reader, xx_elf_inspection *state, xx_pd_struct *pd)
{
    xx_executable_input *input;
    int result;
    int64_t saved;
    if (!state) return 0;
    xx_rt_memset(state, 0, sizeof(*state));
    if (!reader || !reader->format.device) return 0;
    saved = xx_io_tell(reader->format.device);
    if (reader->format.handle_base_info && !reader->format.handle_base_info(&reader->format, pd)) {
        if (saved >= 0) xx_io_seek64(reader->format.device, saved, XX_RT_SEEK_SET);
        return 0;
    }
    input = xx_exec_input_create(&reader->format, pd);
    if (!input) {
        if (saved >= 0) xx_io_seek64(reader->format.device, saved, XX_RT_SEEK_SET);
        return 0;
    }
    result = inspect_parse_input(input, state, reader);
    if (saved >= 0 && xx_io_seek64(reader->format.device, saved, XX_RT_SEEK_SET) != 0) input->failed = true;
    if (!result || input->failed || xx_pd_is_stopped(pd)) {
        xx_elf_inspect_free(state);
        return 0;
    }
    input->pd = NULL;
    input->parsing = false;
    input->read_work = 0;
    return result;
}

int xx_elf_inspect_analyze_from_device(xx_elf_inspection *state, xx_io_device *device, int64_t base, xx_pd_struct *pd)
{
    xx_elf reader = {0};
    xx_executable_input *input;
    bool be, wide;
    uint64_t i;
    int result = 0;
    if (!state) return 0;
    xx_rt_memset(state, 0, sizeof(*state));
    reader.format.device = device;
    reader.format.base_address = base;
    input = xx_exec_input_create(&reader.format, pd);
    if (!input) return 0;
    if (!xx_exec_match(input, 0, "\177ELF", 4)) goto done;
    reader.elf_class = xx_exec_u8(input, 4);
    reader.data_encoding = xx_exec_u8(input, 5);
    if ((reader.elf_class != 1 && reader.elf_class != 2) || (reader.data_encoding != 1 && reader.data_encoding != 2) || xx_exec_u8(input, 6) != 1) goto done;
    wide = reader.elf_class == 2;
    be = reader.data_encoding == 2;
    if (input->size < (wide ? 64 : 52)) goto done;
    reader.os_abi = xx_exec_u8(input, 7);
    reader.type = xx_exec_u16(input, 16, be);
    reader.machine = xx_exec_u16(input, 18, be);
    reader.version = xx_exec_u32(input, 20, be);
    reader.entry_point = wide ? xx_exec_u64(input, 24, be) : xx_exec_u32(input, 24, be);
    reader.program_header_offset = wide ? xx_exec_u64(input, 32, be) : xx_exec_u32(input, 28, be);
    reader.section_header_offset = wide ? xx_exec_u64(input, 40, be) : xx_exec_u32(input, 32, be);
    reader.flags = xx_exec_u32(input, wide ? 48 : 36, be);
    reader.header_size = xx_exec_u16(input, wide ? 52 : 40, be);
    reader.program_header_entry_size = xx_exec_u16(input, wide ? 54 : 42, be);
    reader.program_header_count = xx_exec_u16(input, wide ? 56 : 44, be);
    reader.section_header_entry_size = xx_exec_u16(input, wide ? 58 : 46, be);
    reader.section_header_count = xx_exec_u16(input, wide ? 60 : 48, be);
    reader.section_name_index = xx_exec_u16(input, wide ? 62 : 50, be);
    if (reader.header_size < (wide ? 64 : 52) || reader.header_size > input->size) goto done;
    if (reader.section_header_offset && (!reader.section_header_count || reader.program_header_count == 0xffff || reader.section_name_index == 0xffff)) {
        uint64_t offset = reader.section_header_offset;
        if (offset > (uint64_t)input->size || (uint64_t)(wide ? 64 : 40) > (uint64_t)input->size - offset) goto done;
        if (!reader.section_header_count)
            reader.section_header_count = wide ? xx_exec_u64(input, (int64_t)offset + 32, be) : xx_exec_u32(input, (int64_t)offset + 20, be);
        if (reader.program_header_count == 0xffff) reader.program_header_count = xx_exec_u32(input, (int64_t)offset + (wide ? 44 : 28), be);
        if (reader.section_name_index == 0xffff) reader.section_name_index = xx_exec_u32(input, (int64_t)offset + (wide ? 40 : 24), be);
    }
    if (reader.program_header_count > 65536 || reader.section_header_count > 65536) goto done;
    if (reader.program_header_count) {
        if (!reader.program_header_offset || reader.program_header_entry_size < (wide ? 56 : 32) || reader.program_header_offset > (uint64_t)input->size ||
            reader.program_header_count * reader.program_header_entry_size > (uint64_t)input->size - reader.program_header_offset)
            goto done;
        reader.program_headers = (xx_elf_program_header *)xx_mem_calloc((size_t)reader.program_header_count, sizeof(*reader.program_headers));
        if (!reader.program_headers) goto done;
        for (i = 0; i < reader.program_header_count && !xx_pd_is_stopped(pd); ++i) {
            int64_t offset = (int64_t)(reader.program_header_offset + i * reader.program_header_entry_size);
            xx_elf_program_header *program = &reader.program_headers[i];
            program->type = xx_exec_u32(input, offset, be);
            program->offset = wide ? xx_exec_u64(input, offset + 8, be) : xx_exec_u32(input, offset + 4, be);
            program->virtual_address = wide ? xx_exec_u64(input, offset + 16, be) : xx_exec_u32(input, offset + 8, be);
            program->file_size = wide ? xx_exec_u64(input, offset + 32, be) : xx_exec_u32(input, offset + 16, be);
            if (program->offset > (uint64_t)input->size || program->file_size > (uint64_t)input->size - program->offset) goto done;
        }
    }
    if (reader.section_header_count) {
        if (!reader.section_header_offset || reader.section_header_entry_size < (wide ? 64 : 40) || reader.section_header_offset > (uint64_t)input->size ||
            reader.section_header_count * reader.section_header_entry_size > (uint64_t)input->size - reader.section_header_offset ||
            reader.section_name_index >= reader.section_header_count)
            goto done;
        reader.section_headers = (xx_elf_section_header *)xx_mem_calloc((size_t)reader.section_header_count, sizeof(*reader.section_headers));
        if (!reader.section_headers) goto done;
        for (i = 0; i < reader.section_header_count && !xx_pd_is_stopped(pd); ++i) {
            int64_t offset = (int64_t)(reader.section_header_offset + i * reader.section_header_entry_size);
            xx_elf_section_header *section = &reader.section_headers[i];
            section->name_offset = xx_exec_u32(input, offset, be);
            section->type = xx_exec_u32(input, offset + 4, be);
            section->address = wide ? xx_exec_u64(input, offset + 16, be) : xx_exec_u32(input, offset + 12, be);
            section->offset = wide ? xx_exec_u64(input, offset + 24, be) : xx_exec_u32(input, offset + 16, be);
            section->size = wide ? xx_exec_u64(input, offset + 32, be) : xx_exec_u32(input, offset + 20, be);
            if (section->type != ELF_SHT_NOBITS && section->type != 0 &&
                (section->offset > (uint64_t)input->size || section->size > (uint64_t)input->size - section->offset))
                goto done;
        }
    }
    if (!input->failed && !xx_pd_is_stopped(pd)) result = inspect_parse_input(input, state, &reader);
done:
    xx_mem_free(reader.program_headers);
    xx_mem_free(reader.section_headers);
    if (!result || input->failed || xx_pd_is_stopped(pd)) {
        if (state->pInput) xx_elf_inspect_free(state);
        else xx_exec_input_free(input);
        return 0;
    }
    input->pd = NULL;
    input->parsing = false;
    input->read_work = 0;
    return result;
}
