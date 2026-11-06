/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_pe.c - PE script API dispatch and inspection helpers. */

#include "xx_die_engine_api_internal.h"
#include "xxfclib/formats/executable/pe/xx_pe_unwind_validate.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include "../regex/xx_regex_die.h"
#include "../formats/xx_executable_inspect_internal.h"
#include "xxfclib/formats/executable/dotnet/xx_dotnet_reader.h"

static const char *section_name(DieEngine *pEngine, int nNumber, char *pBuffer)
{
    int i;
    size_t nLength = 0;
    pBuffer[0] = 0;
    if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pEngine->pe.nSectionCount)) {
        return pBuffer;
    }

    /* Check for COFF long section name: starts with '/' followed by decimal offset */
    if (pEngine->pe.pSections[nNumber].sName[0] == '/' &&
        pEngine->pe.pSections[nNumber].sName[1] >= '0' &&
        pEngine->pe.pSections[nNumber].sName[1] <= '9') {
        int64_t nOffset = (int64_t)pEngine->pe.nPointerToSymbolTable + (int64_t)pEngine->pe.nNumberOfSymbols * 18;
        if (nOffset > 0) {
            uint32_t nStrTableSize = xx_exec_u32(pEngine->pe.pInput, nOffset, false);
            if (nStrTableSize >= 4) {
                int64_t nStrOffset = 0;
                for (i = 1; i < 8 && pEngine->pe.pSections[nNumber].sName[i] >= '0' && pEngine->pe.pSections[nNumber].sName[i] <= '9'; i++) {
                    nStrOffset = nStrOffset * 10 + (pEngine->pe.pSections[nNumber].sName[i] - '0');
                }
                if (nStrOffset >= 4 && nStrOffset < (int64_t)nStrTableSize) {
                    char temp[256];
                    size_t to_read = (size_t)(nStrTableSize - nStrOffset);
                    if (to_read > sizeof(temp) - 1) to_read = sizeof(temp) - 1;
                    if (xx_exec_read(pEngine->pe.pInput, nOffset + nStrOffset, temp, to_read)) {
                        temp[to_read] = '\0';
                        size_t str_len = xx_rt_strlen(temp);
                        if (str_len > 0) {
                            xx_rt_memcpy(pBuffer, temp, str_len + 1);
                            return pBuffer;
                        }
                    }
                }
            }
        }
    }

    /* xx_pe_inspection reads the eight-byte field as Latin-1, while JS strings and the
     * console output use UTF-8. A raw high byte is not a UTF-8 character. */
    for (i = 0; (i < 8) && pEngine->pe.pSections[nNumber].sName[i]; i++) {
        unsigned char nChar = (unsigned char)pEngine->pe.pSections[nNumber].sName[i];
        if (nChar >= 0x80) {
            pBuffer[nLength++] = (char)(0xC0 | (nChar >> 6));
            pBuffer[nLength++] = (char)(0x80 | (nChar & 0x3F));
        } else {
            pBuffer[nLength++] = (char)nChar;
        }
    }
    pBuffer[nLength] = 0;
    return pBuffer;
}

static int pe_directory_section(DieEngine *pEngine, int nDirectory)
{
    const xx_memory_record *pRecord;
    if (!pEngine->bHasPE) {
        return -1;
    }

    if (pEngine->pe.pDirRVA[nDirectory] == 0) {
        return -1;
    }

    pRecord = xx_memory_map_record_by_address(&pEngine->pe.map, pEngine->pe.nImageBase + pEngine->pe.pDirRVA[nDirectory]);
    return pRecord && pRecord->file_part_number > 0 ? pRecord->file_part_number - 1 : 0;
}

/* PE image types, mirroring xx_pe_inspection::getType() / xx_pe_inspection::typeIdToString(). */
typedef enum {
    PETYPE_APPLICATION = 0,
    PETYPE_XBOX_APPLICATION,
    PETYPE_EFI_APPLICATION,
    PETYPE_GUI,
    PETYPE_CE_GUI,
    PETYPE_CONSOLE,
    PETYPE_DLL,
    PETYPE_DRIVER,
    PETYPE_NATIVE,
    PETYPE_EFI_RUNTIMEDRIVER,
    PETYPE_EFI_BOOTSERVICEDRIVER
} PEType;

static int pe_imports_ntdll(xx_pe_inspection *pPE)
{
    int i = 0;

    for (i = 0; i < pPE->nImportCount; i++) {
        if (cd_stricmp_ascii(pPE->pImports[i].pName, "ntdll.dll") == 0) {
            return 1;
        }
    }

    return 0;
}

static PEType pe_type(xx_pe_inspection *pPE)
{
    PEType result = PETYPE_APPLICATION;

    switch (pPE->nSubsystem) {
        case 1:
        case 8: result = pe_imports_ntdll(pPE) ? PETYPE_NATIVE : PETYPE_DRIVER; break;
        case 3:
        case 5:
        case 7: result = PETYPE_CONSOLE; break;
        case 2: result = PETYPE_GUI; break;
        case 9: result = PETYPE_CE_GUI; break;
        case 14: result = PETYPE_XBOX_APPLICATION; break;
        case 10: result = PETYPE_EFI_APPLICATION; break;
        case 11: result = PETYPE_EFI_BOOTSERVICEDRIVER; break;
        case 12: result = PETYPE_EFI_RUNTIMEDRIVER; break;
        default: break;
    }

    if ((result != PETYPE_DRIVER) && (pPE->nCharacteristics & 0x2000)) {
        result = PETYPE_DLL;
    }

    return result;
}

static const char *pe_type_name(PEType type)
{
    switch (type) {
        case PETYPE_APPLICATION: return "Application";
        case PETYPE_XBOX_APPLICATION: return "XBOX Application";
        case PETYPE_EFI_APPLICATION: return "EFI Application";
        case PETYPE_GUI: return "GUI";
        case PETYPE_CE_GUI: return "CE GUI";
        case PETYPE_CONSOLE: return "Console";
        case PETYPE_DLL: return "DLL";
        case PETYPE_DRIVER: return "Driver";
        case PETYPE_NATIVE: return "Native";
        case PETYPE_EFI_RUNTIMEDRIVER: return "EFI Runtime driver";
        case PETYPE_EFI_BOOTSERVICEDRIVER: return "EFI Boot service driver";
        default: break;
    }

    return "Unknown";
}

static const char *version_value(DieEngine *pEngine, const char *pKey)
{
    int i = 0;

    if (!pEngine->bHasPE) {
        return "";
    }

    for (i = 0; i < pEngine->pe.nVersionCount; i++) {
        if (x_strcmp(pEngine->pe.pVersionRecords[i].pKey, pKey) == 0) {
            return pEngine->pe.pVersionRecords[i].pValue;
        }
    }

    return "";
}

static cd_u64 image_file_header_value(xx_pe_inspection *pPE, const char *pKey)
{
    if (x_strcmp(pKey, "Machine") == 0) return pPE->nMachine;
    if (x_strcmp(pKey, "NumberOfSections") == 0) return pPE->nNumberOfSections;
    if (x_strcmp(pKey, "TimeDateStamp") == 0) return pPE->nTimeDateStamp;
    if (x_strcmp(pKey, "PointerToSymbolTable") == 0) return pPE->nPointerToSymbolTable;
    if (x_strcmp(pKey, "NumberOfSymbols") == 0) return pPE->nNumberOfSymbols;
    if (x_strcmp(pKey, "SizeOfOptionalHeader") == 0) return pPE->nSizeOfOptionalHeader;
    if (x_strcmp(pKey, "Characteristics") == 0) return pPE->nCharacteristics;

    return 0;
}

static cd_u64 image_optional_header_value(xx_pe_inspection *pPE, const char *pKey)
{
    if (x_strcmp(pKey, "Magic") == 0) return pPE->nMagic;
    if (x_strcmp(pKey, "MajorLinkerVersion") == 0) return pPE->nMajorLinkerVersion;
    if (x_strcmp(pKey, "MinorLinkerVersion") == 0) return pPE->nMinorLinkerVersion;
    if (x_strcmp(pKey, "SizeOfCode") == 0) return pPE->nSizeOfCode;
    if (x_strcmp(pKey, "SizeOfInitializedData") == 0) return pPE->nSizeOfInitializedData;
    if (x_strcmp(pKey, "SizeOfUninitializedData") == 0) return pPE->nSizeOfUninitializedData;
    if (x_strcmp(pKey, "AddressOfEntryPoint") == 0) return pPE->nAddressOfEntryPoint;
    if (x_strcmp(pKey, "BaseOfCode") == 0) return pPE->nBaseOfCode;
    if (x_strcmp(pKey, "BaseOfData") == 0) return pPE->nBaseOfData;
    if (x_strcmp(pKey, "ImageBase") == 0) return pPE->nImageBase;
    if (x_strcmp(pKey, "SectionAlignment") == 0) return pPE->nSectionAlignment;
    if (x_strcmp(pKey, "FileAlignment") == 0) return pPE->nFileAlignment;
    if (x_strcmp(pKey, "MajorOperatingSystemVersion") == 0) return pPE->nMajorOperatingSystemVersion;
    if (x_strcmp(pKey, "MinorOperatingSystemVersion") == 0) return pPE->nMinorOperatingSystemVersion;
    if (x_strcmp(pKey, "MajorImageVersion") == 0) return pPE->nMajorImageVersion;
    if (x_strcmp(pKey, "MinorImageVersion") == 0) return pPE->nMinorImageVersion;
    if (x_strcmp(pKey, "MajorSubsystemVersion") == 0) return pPE->nMajorSubsystemVersion;
    if (x_strcmp(pKey, "MinorSubsystemVersion") == 0) return pPE->nMinorSubsystemVersion;
    if (x_strcmp(pKey, "Win32VersionValue") == 0) return pPE->nWin32VersionValue;
    if (x_strcmp(pKey, "SizeOfImage") == 0) return pPE->nSizeOfImage;
    if (x_strcmp(pKey, "SizeOfHeaders") == 0) return pPE->nSizeOfHeaders;
    if (x_strcmp(pKey, "CheckSum") == 0) return pPE->nCheckSum;
    if (x_strcmp(pKey, "Subsystem") == 0) return pPE->nSubsystem;
    if (x_strcmp(pKey, "DllCharacteristics") == 0) return pPE->nDllCharacteristics;
    if (x_strcmp(pKey, "SizeOfStackReserve") == 0) return pPE->nSizeOfStackReserve;
    if (x_strcmp(pKey, "SizeOfStackCommit") == 0) return pPE->nSizeOfStackCommit;
    if (x_strcmp(pKey, "SizeOfHeapReserve") == 0) return pPE->nSizeOfHeapReserve;
    if (x_strcmp(pKey, "SizeOfHeapCommit") == 0) return pPE->nSizeOfHeapCommit;
    if (x_strcmp(pKey, "LoaderFlags") == 0) return pPE->nLoaderFlags;
    if (x_strcmp(pKey, "NumberOfRvaAndSizes") == 0) return pPE->nNumberOfRvaAndSizes;

    return 0;
}

static const ApiEntry g_pe_api_table[] = {
    {"getNumberOfSections", A_getNumberOfSections, 0},
    {"getSectionName", A_getSectionName, 1},
    {"getSectionVirtualSize", A_getSectionVirtualSize, 1},
    {"getSectionVirtualAddress", A_getSectionVirtualAddress, 1},
    {"getSectionFileSize", A_getSectionFileSize, 1},
    {"getSectionFileOffset", A_getSectionFileOffset, 1},
    {"getSectionCharacteristics", A_getSectionCharacteristics, 1},
    {"getNumberOfResources", A_getNumberOfResources, 0},
    {"isSectionNamePresent", A_isSectionNamePresent, 1},
    {"isNET", A_isNET, 0},
    {"isNet", A_isNET, 0},
    {"isPE32", A_isPE32, 0},
    {"isPEPlus", A_isPEPlus, 0},
    {"getGeneralOptions", A_getGeneralOptions, 0},
    {"getResourceIdByNumber", A_getResourceIdByNumber, 1},
    {"getResourceNameByNumber", A_getResourceNameByNumber, 1},
    {"getResourceOffsetByNumber", A_getResourceOffsetByNumber, 1},
    {"getResourceSizeByNumber", A_getResourceSizeByNumber, 1},
    {"getResourceTypeByNumber", A_getResourceTypeByNumber, 1},
    {"isNETStringPresent", A_isNetObjectPresent, 1},
    {"isNetObjectPresent", A_isNetObjectPresent, 1},
    {"isNetStringPresent", A_isNetObjectPresent, 1},
    {"isNETUnicodeStringPresent", A_isNetUStringPresent, 1},
    {"isNetUStringPresent", A_isNetUStringPresent, 1},
    {"isNetUnicodeStringPresent", A_isNetUStringPresent, 1},
    {"isNetGlobalCctorPresent", A_isNetGlobalCctorPresent, 0},
    {"isNetTypePresent", A_isNetTypePresent, 2},
    {"isNetMethodPresent", A_isNetMethodPresent, 3},
    {"getNumberOfNetMethods", A_getNumberOfNetMethods, 0},
    {"getNetMethodInfo", A_getNetMethodInfo, 1},
    {"getNetUserString", A_getNetUserString, 1},
    {"isNetMethodCalledBy", A_isNetMethodCalledBy, 2},
    {"isNetFieldPresent", A_isNetFieldPresent, 3},
    {"findSignatureInBlob_NET", A_findSignatureInBlob_NET, 1},
    {"isSignatureInBlobPresent_NET", A_isSignatureInBlobPresent_NET, 1},
    {"getNetModuleName", A_getNetModuleName, 0},
    {"getNetAssemblyName", A_getNetAssemblyName, 0},
    {"getNetVersion", A_getNETVersion, 0},
    {"getNumberOfImports", A_getNumberOfImports, 0},
    {"getImportLibraryName", A_getImportLibraryName, 1},
    {"isLibraryPresent", A_isLibraryPresent, 2},
    {"isLibraryFunctionPresent", A_isLibraryFunctionPresent, 2},
    {"isFunctionPresent", A_isFunctionPresent, 1},
    {"getImportFunctionName", A_getImportFunctionName, 2},
    {"getImportSection", A_getImportSection, 0},
    {"getExportSection", A_getExportSection, 0},
    {"getResourceSection", A_getResourceSection, 0},
    {"getEntryPointSection", A_getEntryPointSection, 0},
    {"getRelocsSection", A_getRelocsSection, 0},
    {"getTLSSection", A_getTLSSection, 0},
    {"getMajorLinkerVersion", A_getMajorLinkerVersion, 0},
    {"getMinorLinkerVersion", A_getMinorLinkerVersion, 0},
    {"getManifest", A_getManifest, 0},
    {"getVersionStringInfo", A_getVersionStringInfo, 1},
    {"getNumberOfImportThunks", A_getNumberOfImportThunks, 1},
    {"getResourceNameOffset", A_getResourceNameOffset, 1},
    {"isResourceNamePresent", A_isResourceNamePresent, 1},
    {"isResourceGroupNamePresent", A_isResourceGroupNamePresent, 1},
    {"isResourceGroupIdPresent", A_isResourceGroupIdPresent, 1},
    {"getCompilerVersion", A_getCompilerVersion, 0},
    {"isConsole", A_isConsole, 0},
    {"isSignedFile", A_isSignedFile, 0},
    {"getSectionNameCollision", A_getSectionNameCollision, 2},
    {"getSectionNumber", A_getSectionNumber, 1},
    {"isDll", A_isDll, 0},
    {"isDriver", A_isDriver, 0},
    {"getNETVersion", A_getNETVersion, 0},
    {"compareEP_NET", A_compareEP_NET, 2},
    {"getSizeOfCode", A_getSizeOfCode, 0},
    {"getSizeOfUninitializedData", A_getSizeOfUninitializedData, 0},
    {"getPEFileVersion", A_getPEFileVersion, 1},
    {"getFileVersion", A_getFileVersion, 0},
    {"getFileVersionMS", A_getFileVersionMS, 0},
    {"calculateSizeOfHeaders", A_calculateSizeOfHeaders, 0},
    {"isExportFunctionPresent", A_isExportFunctionPresent, 1},
    {"getNumberOfExportFunctions", A_getNumberOfExportFunctions, 0},
    {"getNumberOfExports", A_getNumberOfExportFunctions, 0},
    {"getExportFunctionName", A_getExportFunctionName, 1},
    {"getExportNameByNumber", A_getExportFunctionName, 1},
    {"getNextNamedExportFunctionIndex", A_getNextNamedExportFunctionIndex, 1},
    {"isExportPresent", A_isExportPresent, 0},
    {"isTLSPresent", A_isTLSPresent, 0},
    {"isImportPresent", A_isImportPresent, 0},
    {"isResourcesPresent", A_isResourcesPresent, 0},
    {"getImportHash32", A_getImportHash32, 0},
    {"getImportHash64", A_getImportHash64, 0},
    {"isImportPositionHashPresent", A_isImportPositionHashPresent, 2},
    {"getImageFileHeader", A_getImageFileHeader, 1},
    {"getImageOptionalHeader", A_getImageOptionalHeader, 1},
    {"getNumberOfDebugDataRecords", A_getNumberOfDebugDataRecords, 0},
    {"getDebugDataType", A_getDebugDataType, 1},
    {"getDebugDataOffset", A_getDebugDataOffset, 1},
    {"getDebugDataSize", A_getDebugDataSize, 1},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_pe(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_pe_api_table);
}
bool die_engine_api_dispatch_pe(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    xx_pe_inspection *pPE = &pEngine->pe;
    xx_dotnet_inspection *pDotNet = &pEngine->dotnet;
    xx_io_device *pFile = pEngine->device;

    switch (id) {
        case A_getNumberOfSections:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nSectionCount : 0));
            return true;

        case A_getSectionName: {
            char sName[256];
            *pResult = js_str(pCtx, section_name(pEngine, (int)arg_i64(pCtx, nArgc, pArgv, 0, 0), sName));
            return true;
        }

        case A_getSectionVirtualSize:
        case A_getSectionVirtualAddress:
        case A_getSectionFileSize:
        case A_getSectionFileOffset:
        case A_getSectionCharacteristics: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nSectionCount)) {
                *pResult = js_num(0);
                return true;
            }

            switch (id) {
                case A_getSectionVirtualSize: *pResult = js_num((double)pPE->pSections[nNumber].nVirtualSize); return true;
                case A_getSectionVirtualAddress: *pResult = js_num((double)pPE->pSections[nNumber].nVirtualAddress); return true;
                case A_getSectionFileSize: *pResult = js_num((double)pPE->pSections[nNumber].nSizeOfRawData); return true;
                case A_getSectionFileOffset: *pResult = js_num((double)pPE->pSections[nNumber].nPointerToRawData); return true;
                default: *pResult = js_num((double)pPE->pSections[nNumber].nCharacteristics); return true;
            }
        }

        case A_getNumberOfResources:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nResourceCount : 0));
            return true;

        case A_isSectionNamePresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nSectionCount; i++) {
                    char sName[256];
                    if (x_strcmp(section_name(pEngine, i, sName), pName) == 0) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pName);
            *pResult = js_bool(bResult);
            return true;
        }

        case A_getSectionNumber: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int nResult = -1;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nSectionCount; i++) {
                    char sName[256];
                    if (x_strcmp(section_name(pEngine, i, sName), pName) == 0) {
                        nResult = i;
                        break;
                    }
                }
            }

            cd_free(pName);
            *pResult = js_num((double)nResult);
            return true;
        }

        case A_getSectionNameCollision: {
            char *pString1 = arg_string(pCtx, nArgc, pArgv, 0);
            char *pString2 = arg_string(pCtx, nArgc, pArgv, 1);
            int i = 0;
            char sRoot1[256] = "";
            char sRoot2[256] = "";

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nSectionCount; i++) {
                    char sName[256];
                    const char *pName = section_name(pEngine, i, sName);
                    const char *pMatch = x_strstr(pName, pString1);

                    /* XBinary::getStringCollision retains the prefix before
                     * the first delimiter, then stops at the first second
                     * delimiter after a nonempty first prefix was found. */
                    if (pMatch) {
                        size_t nLength = (size_t)(pMatch - pName);
                        if (nLength > sizeof(sRoot1) - 1) nLength = sizeof(sRoot1) - 1;

                        x_memcpy(sRoot1, pName, nLength);
                        sRoot1[nLength] = 0;
                    }

                    pMatch = x_strstr(pName, pString2);
                    if (sRoot1[0] && pMatch) {
                        size_t nLength = (size_t)(pMatch - pName);
                        if (nLength > sizeof(sRoot2) - 1) nLength = sizeof(sRoot2) - 1;

                        x_memcpy(sRoot2, pName, nLength);
                        sRoot2[nLength] = 0;
                        break;
                    }
                }
            }

            {
                JSVal res = js_str(pCtx, (sRoot1[0] && x_strcmp(sRoot1, sRoot2) == 0) ? sRoot1 : "");

                cd_free(pString1);
                cd_free(pString2);

                *pResult = res;
                return true;
            }
        }

        case A_isNET:
            *pResult = js_bool(pEngine->bHasDotNet && pDotNet->bIsNet);
            return true;
        case A_isPE32:
            *pResult = js_bool(pEngine->bHasPE && (!pPE->bIs64));
            return true;
        case A_isPEPlus:
            *pResult = js_bool(pEngine->bHasPE && pPE->bIs64);
            return true;

        case A_getGeneralOptions: {
            char sBuf[64];

            if (!pEngine->bHasPE) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            x_snprintf(sBuf, sizeof(sBuf), "%s%s", pe_type_name(pe_type(pPE)), pPE->bIs64 ? "64" : "32");
            *pResult = js_str(pCtx, sBuf);
            return true;
        }

        case A_getResourceIdByNumber:
        case A_getResourceNameByNumber:
        case A_getResourceOffsetByNumber:
        case A_getResourceSizeByNumber:
        case A_getResourceTypeByNumber: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nResourceCount)) {
                if (id == A_getResourceNameByNumber) {
                    *pResult = js_str(pCtx, "");
                    return true;
                }

                if (id == A_getResourceOffsetByNumber) {
                    *pResult = js_num(-1);
                    return true;
                }

                *pResult = js_num(0);
                return true;
            }

            switch (id) {
                case A_getResourceIdByNumber: *pResult = js_num((double)pPE->pResources[nNumber].nNameId); return true;
                case A_getResourceNameByNumber:
                    if (pPE->pResources[nNumber].pName) {
                        *pResult = js_str(pCtx, pPE->pResources[nNumber].pName);
                        return true;
                    }

                    *pResult = js_str(pCtx, pPE->pResources[nNumber].pTypeName ? pPE->pResources[nNumber].pTypeName : "");
                    return true;
                case A_getResourceOffsetByNumber: *pResult = js_num((double)pPE->pResources[nNumber].nOffset); return true;
                case A_getResourceSizeByNumber: *pResult = js_num((double)pPE->pResources[nNumber].nSize); return true;
                default: *pResult = js_num((double)pPE->pResources[nNumber].nTypeId); return true;
            }
        }

        case A_getResourceNameOffset:
        case A_isResourceNamePresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nResult = -1;
            int i = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nResourceCount; i++) {
                    if (pPE->pResources[i].pName && (x_strcmp(pPE->pResources[i].pName, pName) == 0)) {
                        nResult = pPE->pResources[i].nOffset;
                        break;
                    }
                }
            }

            cd_free(pName);

            if (id == A_isResourceNamePresent) {
                *pResult = js_bool(nResult != -1);
                return true;
            }

            *pResult = js_num((double)nResult);
            return true;
        }

        case A_isResourceGroupNamePresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = 0;
            int i = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nResourceCount; i++) {
                    if (pPE->pResources[i].pTypeName && (x_strcmp(pPE->pResources[i].pTypeName, pName) == 0)) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pName);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isResourceGroupIdPresent: {
            cd_u32 nId = (cd_u32)arg_i64(pCtx, nArgc, pArgv, 0, 0);
            int i = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nResourceCount; i++) {
                    if ((pPE->pResources[i].pTypeName == NULL) && (pPE->pResources[i].nTypeId == nId)) {
                        *pResult = js_bool(1);
                        return true;
                    }
                }
            }

            *pResult = js_bool(0);
            return true;
        }

        case A_getNumberOfImports:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nImportCount : 0));
            return true;

        case A_getImportLibraryName: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nImportCount)) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            *pResult = js_str(pCtx, pPE->pImports[nNumber].pName);
            return true;
        }

        case A_getNumberOfImportThunks: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nImportCount)) {
                *pResult = js_num(0);
                return true;
            }

            *pResult = js_num((double)pPE->pImports[nNumber].nFunctionCount);
            return true;
        }

        case A_getImportFunctionName: {
            int nImport = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);
            int nFunction = (int)arg_i64(pCtx, nArgc, pArgv, 1, 0);

            if ((!pEngine->bHasPE) || (nImport < 0) || (nImport >= pPE->nImportCount)) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            if ((nFunction < 0) || (nFunction >= pPE->pImports[nImport].nFunctionCount)) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            *pResult = js_str(pCtx, pPE->pImports[nImport].ppFunctions[nFunction]);
            return true;
        }

        case A_isLibraryPresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bCheckCase = arg_bool(pCtx, nArgc, pArgv, 1, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nImportCount; i++) {
                    int bMatch = bCheckCase ? (x_strcmp(pPE->pImports[i].pName, pName) == 0) : (cd_stricmp_ascii(pPE->pImports[i].pName, pName) == 0);
                    if (bMatch) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pName);
            *pResult = js_bool(bResult);
            return true;
        }

        case A_isLibraryFunctionPresent: {
            char *pLibrary = arg_string(pCtx, nArgc, pArgv, 0);
            char *pFunction = arg_string(pCtx, nArgc, pArgv, 1);
            int i = 0;
            int j = 0;
            int bResult = 0;

            if (pEngine->bHasPE) {
                for (i = 0; (i < pPE->nImportCount) && (!bResult); i++) {
                    if (cd_stricmp_ascii(pPE->pImports[i].pName, pLibrary) != 0) {
                        continue;
                    }

                    for (j = 0; j < pPE->pImports[i].nFunctionCount; j++) {
                        if (cd_stricmp_ascii(pPE->pImports[i].ppFunctions[j], pFunction) == 0) {
                            bResult = 1;
                            break;
                        }
                    }
                }
            }

            cd_free(pLibrary);
            cd_free(pFunction);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isFunctionPresent: {
            char *pFunction = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int j = 0;
            int bResult = 0;

            if (pEngine->bHasPE) {
                for (i = 0; (i < pPE->nImportCount) && (!bResult); i++) {
                    for (j = 0; j < pPE->pImports[i].nFunctionCount; j++) {
                        if (x_strcmp(pPE->pImports[i].ppFunctions[j], pFunction) == 0) {
                            bResult = 1;
                            break;
                        }
                    }
                }
            }

            cd_free(pFunction);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_getImportSection:
            *pResult = js_num((double)pe_directory_section(pEngine, XX_PE_INSPECT_DIR_IMPORT));
            return true;
        case A_getExportSection:
            *pResult = js_num((double)pe_directory_section(pEngine, XX_PE_INSPECT_DIR_EXPORT));
            return true;
        case A_getResourceSection:
            *pResult = js_num((double)pe_directory_section(pEngine, XX_PE_INSPECT_DIR_RESOURCE));
            return true;
        case A_getRelocsSection:
            *pResult = js_num((double)pe_directory_section(pEngine, XX_PE_INSPECT_DIR_BASERELOC));
            return true;
        case A_getTLSSection:
            *pResult = js_num((double)pe_directory_section(pEngine, XX_PE_INSPECT_DIR_TLS));
            return true;

        case A_getEntryPointSection: {
            const xx_memory_record *pRecord;
            if ((!pEngine->bHasPE) || (pPE->nAddressOfEntryPoint == 0)) {
                *pResult = js_num(-1);
                return true;
            }

            pRecord = xx_memory_map_record_by_address(&pPE->map, pPE->nEntryPointAddress);
            /* xx_pe_inspection preserves the header/default memory-record number zero;
             * only positive section record numbers are decremented. */
            *pResult = js_num((double)(pRecord && pRecord->file_part_number > 0 ? pRecord->file_part_number - 1 : 0));
            return true;
        }

        case A_getMajorLinkerVersion:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nMajorLinkerVersion : 0));
            return true;
        case A_getMinorLinkerVersion:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nMinorLinkerVersion : 0));
            return true;

        case A_getManifest:
            *pResult = js_str(pCtx, (pEngine->bHasPE && pPE->pManifest) ? pPE->pManifest : "");
            return true;

        case A_getVersionStringInfo: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal res = js_str(pCtx, version_value(pEngine, pKey));

            cd_free(pKey);

            *pResult = res;
            return true;
        }

        case A_getCompilerVersion: {
            char sBuf[64];

            if (!pEngine->bHasPE) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            x_snprintf(sBuf, sizeof(sBuf), "%u.%u", (unsigned)pPE->nMajorLinkerVersion, (unsigned)pPE->nMinorLinkerVersion);

            *pResult = js_str(pCtx, sBuf);
            return true;
        }

        case A_isConsole:
            *pResult = js_bool(pEngine->bHasPE && (pe_type(pPE) == PETYPE_CONSOLE));
            return true;

        case A_isSignedFile: {
            cd_i64 nOffset = 0;
            cd_i64 nSize = 0;

            if (!pEngine->bHasPE) {
                *pResult = js_bool(0);
                return true;
            }

            nOffset = (cd_i64)pPE->pDirRVA[XX_PE_INSPECT_DIR_SECURITY];
            nSize = (cd_i64)pPE->pDirSize[XX_PE_INSPECT_DIR_SECURITY];

            *pResult = js_bool((nSize > 0) && (nOffset > 0) && (nOffset < xx_io_total_size(pFile)) && (nSize <= xx_io_total_size(pFile) - nOffset));
            return true;
        }
        case A_isDll:
            *pResult = js_bool(pEngine->bHasPE && (pe_type(pPE) == PETYPE_DLL));
            return true;
        case A_isDriver:
            *pResult = js_bool(pEngine->bHasPE && (pe_type(pPE) == PETYPE_DRIVER));
            return true;

        case A_getSizeOfCode:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nSizeOfCode : 0));
            return true;
        case A_getSizeOfUninitializedData:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nSizeOfUninitializedData : 0));
            return true;

        case A_getPEFileVersion: {
            char *pFileName = arg_string(pCtx, nArgc, pArgv, 0);
            xx_io_device *source = xx_io_file_open(pFileName, "rb");
            xx_io_device *other = source ? xx_io_buffer_open_auto(source, 0, xx_get_file_memory_limit(), true) : NULL;
            JSVal res = js_str(pCtx, "");

            if (other) {
                xx_pe_inspection otherPE = {0};

                if (xx_pe_inspect_analyze_from_device(&otherPE, other, 0, NULL)) {
                    int i = 0;

                    for (i = 0; i < otherPE.nVersionCount; i++) {
                        if (x_strcmp(otherPE.pVersionRecords[i].pKey, "FileVersion") == 0) {
                            js_release(pCtx, res);
                            res = js_str(pCtx, otherPE.pVersionRecords[i].pValue);
                            break;
                        }
                    }
                }
                xx_pe_inspect_free(&otherPE);

                xx_io_close(other);
            } else if (source) {
                xx_io_close(source);
            }

            cd_free(pFileName);

            *pResult = res;
            return true;
        }

        case A_getFileVersion:
            *pResult = js_str(pCtx, version_value(pEngine, "FileVersion"));
            return true;

        case A_getFileVersionMS: {
            char sBuf[64];

            if (!pEngine->bHasPE) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            x_snprintf(sBuf, sizeof(sBuf), "%u.%u", (unsigned)(pPE->nFileVersionMS >> 16), (unsigned)(pPE->nFileVersionMS & 0xFFFF));

            *pResult = js_str(pCtx, sBuf);
            return true;
        }

        case A_calculateSizeOfHeaders: {
            cd_i64 nSize = 0;

            if (!pEngine->bHasPE) {
                *pResult = js_num(0);
                return true;
            }

            nSize = pPE->nLfanew + 4 + 20 + pPE->nSizeOfOptionalHeader + (cd_i64)pPE->nSectionCount * 40;

            if (pPE->nFileAlignment) {
                nSize = ((nSize + pPE->nFileAlignment - 1) / pPE->nFileAlignment) * pPE->nFileAlignment;
            }

            *pResult = js_num((double)nSize);
            return true;
        }

        case A_isExportFunctionPresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasPE && pName && pName[0] && pPE->ppExportFunctions) {
                for (i = 0; i < pPE->nExportCount; i++) {
                    if (pPE->ppExportFunctions[i] && (x_strcmp(pPE->ppExportFunctions[i], pName) == 0)) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pName);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_getNumberOfExportFunctions:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nExportCount : 0));
            return true;

        case A_getExportFunctionName: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nExportCount)) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            *pResult = js_str(pCtx, (pPE->ppExportFunctions && pPE->ppExportFunctions[nNumber]) ? pPE->ppExportFunctions[nNumber] : "");
            return true;
        }

        case A_getNextNamedExportFunctionIndex: {
            int64_t nPrevious = arg_i64(pCtx, nArgc, pArgv, 0, -1);
            int i = 0;

            if ((!pEngine->bHasPE) || (!pPE->ppExportFunctions) || (nPrevious >= (int64_t)pPE->nExportCount - 1)) {
                *pResult = js_num(-1);
                return true;
            }

            if (nPrevious < -1) nPrevious = -1;

            for (i = (int)(nPrevious + 1); i < pPE->nExportCount; i++) {
                if (pPE->ppExportFunctions[i] && pPE->ppExportFunctions[i][0]) {
                    *pResult = js_num((double)i);
                    return true;
                }
            }

            *pResult = js_num(-1);
            return true;
        }

        case A_isExportPresent:
            *pResult = js_bool(pEngine->bHasPE && (pPE->pDirRVA[XX_PE_INSPECT_DIR_EXPORT] != 0));
            return true;
        case A_isTLSPresent:
            *pResult = js_bool(pEngine->bHasPE && (pPE->pDirRVA[XX_PE_INSPECT_DIR_TLS] != 0));
            return true;
        case A_isImportPresent:
            *pResult = js_bool(pEngine->bHasPE && (pPE->pDirRVA[XX_PE_INSPECT_DIR_IMPORT] != 0));
            return true;
        case A_isResourcesPresent:
            *pResult = js_bool(pEngine->bHasPE && (pPE->pDirRVA[XX_PE_INSPECT_DIR_RESOURCE] != 0));
            return true;

        case A_getImportHash32:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nImportHash32 : 0));
            return true;
        case A_getImportHash64:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nImportHash64 : 0));
            return true;

        case A_isImportPositionHashPresent: {
            cd_i64 nIndex = arg_i64(pCtx, nArgc, pArgv, 0, -1);
            cd_u32 nHash = (cd_u32)arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int i = 0;

            if (!pEngine->bHasPE) {
                *pResult = js_bool(0);
                return true;
            }

            if (nIndex == -1) {
                for (i = 0; i < pPE->nImportCount; i++) {
                    if (pPE->pImports[i].nPositionHash == nHash) {
                        *pResult = js_bool(1);
                        return true;
                    }
                }

                *pResult = js_bool(0);
                return true;
            }

            if ((nIndex >= 0) && (nIndex < pPE->nImportCount)) {
                *pResult = js_bool(pPE->pImports[nIndex].nPositionHash == nHash);
                return true;
            }

            *pResult = js_bool(0);
            return true;
        }

        case A_getImageFileHeader: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal res = js_num(pEngine->bHasPE ? (double)image_file_header_value(pPE, pKey) : 0);

            cd_free(pKey);

            *pResult = res;
            return true;
        }

        case A_getImageOptionalHeader: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal res = js_num(pEngine->bHasPE ? (double)image_optional_header_value(pPE, pKey) : 0);

            cd_free(pKey);

            *pResult = res;
            return true;
        }

        case A_getNumberOfDebugDataRecords:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nDebugCount : 0));
            return true;

        case A_getDebugDataType: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nDebugCount)) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            *pResult = js_str(pCtx, xx_pe_inspect_debug_type_name(pPE->pDebugRecords[nNumber].nType));
            return true;
        }

        case A_getDebugDataOffset:
        case A_getDebugDataSize: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nDebugCount)) {
                *pResult = js_num(-1);
                return true;
            }

            if (id == A_getDebugDataOffset) {
                *pResult = js_num((double)pPE->pDebugRecords[nNumber].nOffset);
                return true;
            }

            *pResult = js_num((double)pPE->pDebugRecords[nNumber].nSize);
            return true;
        }

        default:
            break;
    }

    return false;
}
