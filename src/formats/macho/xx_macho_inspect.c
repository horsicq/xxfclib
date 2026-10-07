#include "xxfclib/formats/macho/xx_macho_inspect.h"
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

/* Native Mach-O inspection helpers used by DIE and other metadata queries.
 *
 * Walks the load commands to collect the LC_LOAD_DYLIB libraries (matched by
 * basename, as MACH_Script does), the sections (by sectname), and the
 * entry-point file offset from LC_MAIN. Fat binaries are a different file type
 * (Mach-O FAT) and are not handled here.
 */


#define MACH_MAGIC 0xFEEDFACEu
#define MACH_MAGIC_64 0xFEEDFACFu
#define MACH_CIGAM 0xCEFAEDFEu
#define MACH_CIGAM_64 0xCFFAEDFEu

#define MACH_LC_SEGMENT 0x1
#define MACH_LC_LOAD_DYLIB 0xC
#define MACH_LC_SEGMENT_64 0x19
#define MACH_LC_MAIN 0x80000028u
#define MACH_LC_VERSION_MIN_MACOSX 0x24
#define MACH_LC_VERSION_MIN_IPHONEOS 0x25
#define MACH_LC_VERSION_MIN_TVOS 0x2F
#define MACH_LC_VERSION_MIN_WATCHOS 0x30
#define MACH_LC_BUILD_VERSION 0x32

/* CPU types (XMACH_DEF). */
#define MACH_CPU_MC680x0 0x6
#define MACH_CPU_I386 0x7
#define MACH_CPU_X86_64 0x1000007
#define MACH_CPU_ARM 0xC
#define MACH_CPU_ARM64 0x100000C
#define MACH_CPU_POWERPC 0x12
#define MACH_CPU_POWERPC64 0x1000012

#define MACH_SUBTYPE_ARM_V6 6
#define MACH_SUBTYPE_ARM_V7 9

/* Platforms (build_version_command). */
#define MACH_PLAT_MACOS 1
#define MACH_PLAT_IOS 2
#define MACH_PLAT_TVOS 3
#define MACH_PLAT_WATCHOS 4
#define MACH_PLAT_BRIDGEOS 5
#define MACH_PLAT_MACCATALYST 6
#define MACH_PLAT_IOSSIMULATOR 7
#define MACH_PLAT_TVOSSIMULATOR 8
#define MACH_PLAT_WATCHOSSIMULATOR 9
#define MACH_PLAT_DRIVERKIT 10
#define MACH_PLAT_FIRMWARE 13
#define MACH_PLAT_SEPOS 14

#define MACH_FULL_VERSION(a, b, c) (((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(c))

/* getHeaderCpuTypesS: CPU type -> arch name. */
static const char *mach_cpu_type_name(uint32_t nType)
{
    switch (nType) {
        case 1: return "VAX";
        case 2: return "ROMP";
        case 4: return "NS32032";
        case 5: return "NS32332";
        case 6: return "MC680x0";
        case 7: return "I386";
        case 0x1000007: return "X86_64";
        case 8: return "MIPS";
        case 9: return "NS32532";
        case 0xB: return "HPPA";
        case 0xC: return "ARM";
        case 0x100000C: return "ARM64";
        case 0x200000C: return "ARM64_32";
        case 0xD: return "MC88000";
        case 0xE: return "SPARC";
        case 0xF: return "I860";
        case 0x10: return "I860_LITTLE";
        case 0x11: return "RS6000";
        case 0x12: return "POWERPC";
        case 0x1000012: return "POWERPC64";
        case 255: return "VEO";
        default: return "Unknown";
    }
}

static const char *mach_mc680_subtype(uint32_t nSub)
{
    switch (nSub) {
        case 1: return "MC68030";
        case 2: return "MC68040";
        case 3: return "MC68030_ONLY";
        default: return NULL;
    }
}

static const char *mach_arm_subtype(uint32_t nSub)
{
    switch (nSub) {
        case 0: return "ARM_ALL";
        case 1: return "ARM_A500_ARCH";
        case 2: return "ARM_A500";
        case 3: return "ARM_A440";
        case 4: return "ARM_M4";
        case 5: return "ARM_V4T";
        case 6: return "ARM_V6";
        case 7: return "ARM_V5TEJ";
        case 8: return "ARM_XSCALE";
        case 9: return "ARM_V7";
        case 10: return "ARM_V7F";
        case 11: return "ARM_V7S";
        case 12: return "ARM_V7K";
        case 14: return "ARM_V6M";
        case 15: return "ARM_V7M";
        case 16: return "ARM_V7EM";
        case 0x80000002: return "ARM64E";
        default: return NULL;
    }
}

/* xx_macho_inspection::_getArch: the CPU-type name, refined by the sub-type for the MC680x0
 * and ARM families. */
static const char *mach_arch_name(uint32_t nType, uint32_t nSub)
{
    const char *pName = mach_cpu_type_name(nType);

    if (nType == MACH_CPU_MC680x0) {
        const char *pSub = mach_mc680_subtype(nSub);

        if (pSub != NULL) {
            pName = pSub;
        }
    } else if ((nType == MACH_CPU_ARM) || (nType == MACH_CPU_ARM64)) {
        if (nSub != 0) {
            const char *pSub = mach_arm_subtype(nSub);

            if (pSub != NULL) {
                pName = pSub;
            }
        }
    }

    return pName;
}

/* XBinary::get_uint32_full_version: "X.Y.Z" from packed nibbles. */
static void mach_full_version(uint32_t nValue, char *pOut, size_t nOutSize)
{
    xx_rt_snprintf(pOut, nOutSize, "%u.%u.%u", (unsigned)((nValue >> 16) & 0xFFFF), (unsigned)((nValue >> 8) & 0xFF), (unsigned)(nValue & 0xFF));
}

static void mach_set_str(char *pDst, size_t nDstSize, const char *pSrc)
{
    xx_rt_strncpy(pDst, pSrc, nDstSize - 1);
    pDst[nDstSize - 1] = 0;
}

/* The Foundation-library current version maps to a macOS/iOS release when no
 * LC_BUILD_VERSION / LC_VERSION_MIN command carries the version. Mirrors the
 * fallback branch of xx_macho_inspection::getFileFormatInfo. */
static void mach_foundation_fallback(xx_macho_inspection *pMach, const char **ppOsName, char *pVer, size_t nVerSize)
{
    uint32_t v = 0;

    if (!xx_macho_inspect_library_present(pMach, "Foundation")) {
        return;
    }

    v = xx_macho_inspect_library_current_version(pMach, "Foundation");

    if ((xx_rt_strcmp(*ppOsName, "Mac OS X") == 0) || (xx_rt_strcmp(*ppOsName, "OS X") == 0) || (xx_rt_strcmp(*ppOsName, "macOS") == 0)) {
        if ((v >= MACH_FULL_VERSION(397, 40, 0)) && (v < MACH_FULL_VERSION(425, 0, 0))) mach_set_str(pVer, nVerSize, "10.0.0");
        else if (v < MACH_FULL_VERSION(567, 0, 0)) mach_set_str(pVer, nVerSize, "10.3.0");
        else if (v < MACH_FULL_VERSION(677, 0, 0)) mach_set_str(pVer, nVerSize, "10.4.0");
        else if (v < MACH_FULL_VERSION(677, 24, 0)) mach_set_str(pVer, nVerSize, "10.5.0");
        else if (v < MACH_FULL_VERSION(751, 0, 0)) mach_set_str(pVer, nVerSize, "10.5.7");
        else if (v < MACH_FULL_VERSION(833, 10, 0)) mach_set_str(pVer, nVerSize, "10.6.0");
        else if (v < MACH_FULL_VERSION(833, 25, 0)) mach_set_str(pVer, nVerSize, "10.7.0");
        else if (v < MACH_FULL_VERSION(945, 18, 0)) mach_set_str(pVer, nVerSize, "10.7.4");
        else if (v < MACH_FULL_VERSION(1151, 16, 0)) mach_set_str(pVer, nVerSize, "10.8.4");
        else if (v < MACH_FULL_VERSION(1200, 0, 0)) mach_set_str(pVer, nVerSize, "10.10.0");

        if (v < MACH_FULL_VERSION(833, 10, 0)) {
            *ppOsName = "Mac OS X";
        }
    } else if ((xx_rt_strcmp(*ppOsName, "iPhone OS") == 0) || (xx_rt_strcmp(*ppOsName, "iOS") == 0) || (xx_rt_strcmp(*ppOsName, "iPadOS") == 0)) {
        if (v < MACH_FULL_VERSION(678, 24, 0)) mach_set_str(pVer, nVerSize, "1.0.0");
        else if (v < MACH_FULL_VERSION(678, 26, 0)) mach_set_str(pVer, nVerSize, "2.0.0");
        else if (v < MACH_FULL_VERSION(678, 29, 0)) mach_set_str(pVer, nVerSize, "2.1.0");
        else if (v < MACH_FULL_VERSION(678, 47, 0)) mach_set_str(pVer, nVerSize, "2.2.0");
        else if (v < MACH_FULL_VERSION(678, 51, 0)) mach_set_str(pVer, nVerSize, "3.0.0");
        else if (v < MACH_FULL_VERSION(678, 60, 0)) mach_set_str(pVer, nVerSize, "3.1.0");
        else if (v < MACH_FULL_VERSION(751, 32, 0)) mach_set_str(pVer, nVerSize, "3.2.0");
        else if (v < MACH_FULL_VERSION(751, 37, 0)) mach_set_str(pVer, nVerSize, "4.0.0");
        else if (v < MACH_FULL_VERSION(751, 49, 0)) mach_set_str(pVer, nVerSize, "4.1.0");
        else if (v < MACH_FULL_VERSION(881, 0, 0)) mach_set_str(pVer, nVerSize, "4.2.0");
        else if (v < MACH_FULL_VERSION(890, 10, 0)) mach_set_str(pVer, nVerSize, "5.0.0");
        else if (v < MACH_FULL_VERSION(992, 0, 0)) mach_set_str(pVer, nVerSize, "5.1.0");
        else if (v < MACH_FULL_VERSION(993, 0, 0)) mach_set_str(pVer, nVerSize, "6.0.0");
        else if (v < MACH_FULL_VERSION(1047, 20, 0)) mach_set_str(pVer, nVerSize, "6.1.0");
        else if (v < MACH_FULL_VERSION(1047, 25, 0)) mach_set_str(pVer, nVerSize, "7.0.0");
        else if (v < MACH_FULL_VERSION(1140, 11, 0)) mach_set_str(pVer, nVerSize, "7.1.0");
        else if (v < MACH_FULL_VERSION(1141, 1, 0)) mach_set_str(pVer, nVerSize, "8.0.0");
        else if (v < MACH_FULL_VERSION(1142, 14, 0)) mach_set_str(pVer, nVerSize, "8.1.0");
        else if (v < MACH_FULL_VERSION(1144, 17, 0)) mach_set_str(pVer, nVerSize, "8.2.0");
        else if (v < MACH_FULL_VERSION(1200, 0, 0)) mach_set_str(pVer, nVerSize, "8.3.0");

        *ppOsName = (v < MACH_FULL_VERSION(751, 32, 0)) ? "iPhone OS" : "iOS";
    }
}

static void mach_compute_os(xx_macho_inspection *pMach, int bBuildVer, uint32_t nPlatform, uint32_t nMinos, int bVersionMin, uint32_t nVersionMinCmd, uint32_t nVersionMinValue)
{
    uint32_t nType = pMach->nCpuType;
    uint32_t nSub = pMach->nCpuSubType;
    const char *pOsName = "Mac OS";
    char sVer[24];

    sVer[0] = 0;

    mach_set_str(pMach->sArch, sizeof(pMach->sArch), mach_arch_name(nType, nSub));

    /* CPU-type default OS + version range. */
    if (nType == MACH_CPU_MC680x0) {
        pOsName = "Mac OS";
        mach_set_str(sVer, sizeof(sVer), "1.0-8.1");
    } else if (nType == MACH_CPU_POWERPC) {
        pOsName = "Mac OS";
        mach_set_str(sVer, sizeof(sVer), "7.1.2-9.22");
    } else if (nType == MACH_CPU_POWERPC64) {
        pOsName = "Mac OS X";
        mach_set_str(sVer, sizeof(sVer), "10.4-10.6");
    } else if ((nType == MACH_CPU_I386) || (nType == MACH_CPU_X86_64)) {
        pOsName = "Mac OS X";
        mach_set_str(sVer, sizeof(sVer), "10.4-10.14");
    } else if ((nType == MACH_CPU_ARM) || (nType == MACH_CPU_ARM64)) {
        pOsName = "iOS";

        if (nSub == MACH_SUBTYPE_ARM_V6) {
            pOsName = "iPhone OS";
            mach_set_str(sVer, sizeof(sVer), "1.0-4.2.1");
        } else if (nSub == MACH_SUBTYPE_ARM_V7) {
            pOsName = "iPhone OS";
            mach_set_str(sVer, sizeof(sVer), "3.0-10.3.4");
        } else if (nType == MACH_CPU_ARM64) {
            pOsName = "iOS";
            mach_set_str(sVer, sizeof(sVer), "7.0-16.0");
        }
    }

    /* A LC_VERSION_MIN command names the OS even before its version is read. */
    if (!bBuildVer && bVersionMin) {
        if (nVersionMinCmd == MACH_LC_VERSION_MIN_IPHONEOS) pOsName = "iOS";
        else if (nVersionMinCmd == MACH_LC_VERSION_MIN_MACOSX) pOsName = "macOS";
        else if (nVersionMinCmd == MACH_LC_VERSION_MIN_TVOS) pOsName = "tvOS";
        else if (nVersionMinCmd == MACH_LC_VERSION_MIN_WATCHOS) pOsName = "watchOS";
    }

    if (bBuildVer) {
        if (nPlatform == MACH_PLAT_MACOS) pOsName = "macOS";
        else if ((nPlatform == MACH_PLAT_IOS) || (nPlatform == MACH_PLAT_IOSSIMULATOR)) pOsName = "iOS";
        else if ((nPlatform == MACH_PLAT_TVOS) || (nPlatform == MACH_PLAT_TVOSSIMULATOR)) pOsName = "tvOS";
        else if ((nPlatform == MACH_PLAT_WATCHOS) || (nPlatform == MACH_PLAT_WATCHOSSIMULATOR)) pOsName = "watchOS";
        else if (nPlatform == MACH_PLAT_BRIDGEOS) pOsName = "bridgeOS";
        else if (nPlatform == MACH_PLAT_MACCATALYST) pOsName = "Mac Catalyst";
        else if (nPlatform == MACH_PLAT_DRIVERKIT) pOsName = "Mac DriverKit";
        else if (nPlatform == MACH_PLAT_FIRMWARE) pOsName = "Mac Firmware";
        else if (nPlatform == MACH_PLAT_SEPOS) pOsName = "sepOS";

        if (nMinos) {
            mach_full_version(nMinos, sVer, sizeof(sVer));
        }
    } else if (bVersionMin) {
        mach_full_version(nVersionMinValue, sVer, sizeof(sVer));
    } else {
        mach_foundation_fallback(pMach, &pOsName, sVer, sizeof(sVer));
    }

    mach_set_str(pMach->sOsName, sizeof(pMach->sOsName), pOsName);
    mach_set_str(pMach->sOsVersion, sizeof(pMach->sOsVersion), sVer);
}


/* basename after the last '/'. */
static char *mach_basename(const char *pPath)
{
    const char *pLast = pPath;
    const char *p = pPath;

    for (; *p; p++) {
        if (*p == '/') {
            pLast = p + 1;
        }
    }

    return xx_str_create(pLast);
}

static int inspect_parse_input(xx_executable_input *pFile, xx_macho_inspection *pMach)
{
    uint32_t nMagic = 0;
    int bBE = 0;
    int b64 = 0;
    uint32_t nCmds = 0;
    int64_t nHeaderSize = 0;
    int64_t commands_end = 0;
    int64_t nOffset = 0;
    uint32_t i = 0;
    xx_list_s vecLibs;
    xx_list_s vecSections;
    int bBuildVer = 0;
    uint32_t nPlatform = 0;
    uint32_t nMinos = 0;
    int bVersionMin = 0;
    uint32_t nVersionMinCmd = 0;
    uint32_t nVersionMinValue = 0;

    xx_rt_memset(pMach, 0, sizeof(xx_macho_inspection));
    pMach->pInput = pFile;
    pMach->nEntryPointOffset = -1;
    xx_list_init(&vecLibs, sizeof(xx_macho_inspect_library), NULL);
    xx_list_init(&vecSections, sizeof(xx_macho_inspect_section), NULL);

    if ((pFile == NULL) || (pFile->size < 0x1C)) {
        return 0;
    }

    nMagic = xx_exec_u32(pFile, 0, false);

    if ((nMagic == MACH_MAGIC) || (nMagic == MACH_MAGIC_64)) {
        bBE = 0;
    } else if ((nMagic == MACH_CIGAM) || (nMagic == MACH_CIGAM_64)) {
        bBE = 1;
    } else {
        return 0;
    }

    b64 = ((nMagic == MACH_MAGIC_64) || (nMagic == MACH_CIGAM_64)) ? 1 : 0;
    pMach->bIs64 = b64;
    pMach->bBigEndian = bBE;
    pMach->nCpuType = xx_exec_u32(pFile, 4, bBE ? true : false);
    pMach->nCpuSubType = xx_exec_u32(pFile, 8, bBE ? true : false);

    nCmds = xx_exec_u32(pFile, 16, bBE ? true : false); /* ncmds */
    nHeaderSize = b64 ? 32 : 28;
    nOffset = nHeaderSize;

    if (pFile->size < nHeaderSize || nCmds > 0x10000) return 0;
    commands_end = nHeaderSize + xx_exec_u32(pFile, 20, bBE != 0);
    if (commands_end > pFile->size || commands_end - nHeaderSize < (int64_t)nCmds * 8) return 0;

    for (i = 0; i < nCmds && !pFile->failed && !xx_pd_is_stopped(pFile->pd); i++) {
        uint32_t nCmd = 0;
        uint32_t nCmdSize = 0;

        if (nOffset + 8 > pFile->size) {
            break;
        }

        nCmd = xx_exec_u32(pFile, nOffset, bBE ? true : false);
        nCmdSize = xx_exec_u32(pFile, nOffset + 4, bBE ? true : false);

        if ((nCmdSize < 8) || (nOffset + nCmdSize > commands_end)) {
            break;
        }

        if (nCmd == MACH_LC_LOAD_DYLIB && nCmdSize >= 24) {
            /* dylib_command: load_command(8) + name(u32 offset from cmd),
             * timestamp, current_version, compatibility_version. */
            uint32_t nNameOffset = xx_exec_u32(pFile, nOffset + 8, bBE ? true : false);
            uint32_t nCurrentVersion = xx_exec_u32(pFile, nOffset + 16, bBE ? true : false);
            char *pFullName;
            if (nNameOffset < 24 || nNameOffset >= nCmdSize) { nOffset += nCmdSize; continue; }
            pFullName = xx_exec_string(pFile, nOffset + (int64_t)nNameOffset, nCmdSize - nNameOffset);
            if (!pFullName) { pFile->failed = true; break; }
            xx_macho_inspect_library library = {0};
            xx_macho_inspect_library *pLib = &library;

            pLib->pName = mach_basename(pFullName);
            if (!pLib->pName) pFile->failed = true;
            pLib->nCurrentVersion = nCurrentVersion;
            xx_mem_free(pFullName);
            if (!xx_list_append(&vecLibs, pLib)) { xx_mem_free(pLib->pName); pFile->failed = true; break; }
        } else if ((nCmd == MACH_LC_SEGMENT) || (nCmd == MACH_LC_SEGMENT_64)) {
            int bSeg64 = (nCmd == MACH_LC_SEGMENT_64) ? 1 : 0;
            int64_t nNsectsOffset = nOffset + (bSeg64 ? 64 : 48);
            uint32_t nNsects = xx_exec_u32(pFile, nNsectsOffset, bBE ? true : false);
            int64_t nSectOffset = nOffset + (bSeg64 ? 72 : 56);
            int64_t nSectSize = bSeg64 ? 80 : 68;
            int64_t nCmdEnd = nOffset + (int64_t)nCmdSize;
            uint32_t s = 0;

            /* xx_macho_inspection::getSectionRecords rejects any nsects that does not fit
             * in a byte, so the walk stays inside the segment command.      */
            if (nNsects & 0xFFFFFF00u) {
                nNsects = 0;
            }

            for (s = 0; s < nNsects; s++) {
                xx_macho_inspect_section section = {0};
                xx_macho_inspect_section *pSection = &section;

                if ((nSectOffset + nSectSize > pFile->size) || (nSectOffset + nSectSize > nCmdEnd)) {
                    break;
                }

                xx_exec_read(pFile, nSectOffset, pSection->sName, 16);
                pSection->sName[16] = 0;

                if (bSeg64) {
                    pSection->nSize = xx_exec_u64(pFile, nSectOffset + 40, bBE ? true : false);
                    pSection->nOffset = xx_exec_u32(pFile, nSectOffset + 48, bBE ? true : false);
                } else {
                    pSection->nSize = xx_exec_u32(pFile, nSectOffset + 36, bBE ? true : false);
                    pSection->nOffset = xx_exec_u32(pFile, nSectOffset + 40, bBE ? true : false);
                }

                if (!xx_list_append(&vecSections, pSection)) { pFile->failed = true; break; }
                nSectOffset += nSectSize;
            }
        } else if (nCmd == MACH_LC_MAIN && nCmdSize >= 24) {
            /* entry_point_command: entryoff is a file offset. */
            pMach->nEntryPointOffset = (int64_t)xx_exec_u64(pFile, nOffset + 8, bBE ? true : false);
        } else if (nCmd == MACH_LC_BUILD_VERSION && nCmdSize >= 24) {
            if (!bBuildVer) {
                bBuildVer = 1;
                nPlatform = xx_exec_u32(pFile, nOffset + 8, bBE ? true : false);
                nMinos = xx_exec_u32(pFile, nOffset + 12, bBE ? true : false);
            }
        } else if ((nCmd == MACH_LC_VERSION_MIN_MACOSX) || (nCmd == MACH_LC_VERSION_MIN_IPHONEOS) || (nCmd == MACH_LC_VERSION_MIN_TVOS) ||
                   (nCmd == MACH_LC_VERSION_MIN_WATCHOS)) {
            if (!bVersionMin && nCmdSize >= 16) {
                bVersionMin = 1;
                nVersionMinCmd = nCmd;
                nVersionMinValue = xx_exec_u32(pFile, nOffset + 8, bBE ? true : false);
            }
        }

        nOffset += nCmdSize;
    }

    mach_compute_os(pMach, bBuildVer, nPlatform, nMinos, bVersionMin, nVersionMinCmd, nVersionMinValue);

    pMach->nLibraryCount = (int)vecLibs.count;
    pMach->pLibraries = (xx_macho_inspect_library *)vecLibs.data;
    pMach->nSectionCount = (int)vecSections.count;
    pMach->pSections = (xx_macho_inspect_section *)vecSections.data;
    vecLibs.data = vecSections.data = NULL;
    vecLibs.count = vecSections.count = vecLibs.capacity = vecSections.capacity = 0;
    xx_list_cleanup(&vecLibs); xx_list_cleanup(&vecSections);

    pMach->bValid = (i == nCmds && nOffset == commands_end);

    return pMach->bValid;
}

void xx_macho_inspect_free(xx_macho_inspection *pMach)
{
    if (!pMach) return;
    int i = 0;

    for (i = 0; i < pMach->nLibraryCount; i++) {
        xx_mem_free(pMach->pLibraries[i].pName);
    }

    xx_mem_free(pMach->pLibraries);
    xx_mem_free(pMach->pSections);
    xx_exec_input_free(pMach->pInput);
    xx_rt_memset(pMach, 0, sizeof(*pMach));
}

int xx_macho_inspect_library_present(xx_macho_inspection *pMach, const char *pName)
{
    if (!pMach || !pName) return 0;
    int i = 0;

    for (i = 0; i < pMach->nLibraryCount; i++) {
        if (xx_rt_strcmp(pMach->pLibraries[i].pName, pName) == 0) {
            return 1;
        }
    }

    return 0;
}

uint32_t xx_macho_inspect_library_current_version(xx_macho_inspection *pMach, const char *pName)
{
    if (!pMach || !pName) return 0;
    int i = 0;

    for (i = 0; i < pMach->nLibraryCount; i++) {
        if (xx_rt_strcmp(pMach->pLibraries[i].pName, pName) == 0) {
            return pMach->pLibraries[i].nCurrentVersion;
        }
    }

    return 0;
}

int xx_macho_inspect_section_number(xx_macho_inspection *pMach, const char *pName)
{
    if (!pMach || !pName) return -1;
    int i = 0;

    for (i = 0; i < pMach->nSectionCount; i++) {
        if (xx_rt_strcmp(pMach->pSections[i].sName, pName) == 0) {
            return i;
        }
    }

    return -1;
}

int xx_macho_inspect_section_present(xx_macho_inspection *pMach, const char *pName)
{
    if (!pMach || !pName) return 0;
    return (xx_macho_inspect_section_number(pMach, pName) != -1) ? 1 : 0;
}

uint64_t xx_macho_inspect_section_offset(xx_macho_inspection *pMach, int nNumber)
{
    if (!pMach) return 0;
    if ((nNumber < 0) || (nNumber >= pMach->nSectionCount)) {
        return 0;
    }

    return pMach->pSections[nNumber].nOffset;
}

uint64_t xx_macho_inspect_section_size(xx_macho_inspection *pMach, int nNumber)
{
    if (!pMach) return 0;
    if ((nNumber < 0) || (nNumber >= pMach->nSectionCount)) {
        return 0;
    }

    return pMach->pSections[nNumber].nSize;
}

int xx_macho_inspect_parse(xx_macho *reader, xx_macho_inspection *state, xx_pd_struct *pd)
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
    if (!input) { if (saved >= 0) xx_io_seek64(reader->format.device, saved, XX_RT_SEEK_SET); return 0; }
    result = inspect_parse_input(input, state);
    if (saved >= 0 && xx_io_seek64(reader->format.device, saved, XX_RT_SEEK_SET) != 0) input->failed = true;
    if (!result || input->failed || xx_pd_is_stopped(pd)) { xx_macho_inspect_free(state); return 0; }
    input->pd = NULL; input->parsing = false; input->read_work = 0;
    return result;
}

int xx_macho_inspect_analyze_from_device(xx_macho_inspection *state, xx_io_device *device, int64_t base, xx_pd_struct *pd)
{
    xx_macho reader = {0};
    reader.format.device = device; reader.format.base_address = base;
    return xx_macho_inspect_parse(&reader, state, pd);
}
