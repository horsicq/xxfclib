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

#include "../../formats/pe/xpe.h"

#define ALIGN_UP(v, a) (((a) == 0) ? (v) : ((((v) + (a) - 1) / (a)) * (a)))

static const struct {
    cd_u32 nType;
    const char *pName;
} g_debugTypes[] = {{0, "UNKNOWN"},  {1, "COFF"},       {2, "CODEVIEW"},   {3, "FPO"},        {4, "MISC"},   {5, "EXCEPTION"},
                    {6, "FIXUP"},    {7, "OMAP_TO_SRC"}, {8, "OMAP_FROM_SRC"}, {9, "BORLAND"}, {10, "RESERVED10"}, {11, "CLSID"},
                    {12, "VC_FEATURE"}, {13, "POGO"},   {14, "ILTCG"},     {15, "MPX"},       {16, "REPRO"}, {20, "EX_DLLCHARACTERISTICS"}};

const char *xpe_debug_type_name(cd_u32 nType)
{
    size_t i = 0;

    for (i = 0; i < sizeof(g_debugTypes) / sizeof(g_debugTypes[0]); i++) {
        if (g_debugTypes[i].nType == nType) {
            return g_debugTypes[i].pName;
        }
    }

    return "";
}

int xpe_section_number_by_rva(XPE *pPE, cd_u32 nRVA)
{
    int i = 0;

    /* The arithmetic is done in cd_i64, the way build_memory_map does it: a
     * VirtualAddress or VirtualSize near 4 GB wraps in 32 bits.             */
    for (i = 0; i < pPE->nSectionCount; i++) {
        cd_i64 nStart = pPE->pSections[i].nVirtualAddress;
        cd_i64 nSize = pPE->pSections[i].nVirtualSize;

        if (nSize == 0) {
            nSize = pPE->pSections[i].nSizeOfRawData;
        }

        nSize = ALIGN_UP(nSize, (cd_i64)pPE->nSectionAlignment);

        if (((cd_i64)nRVA >= nStart) && ((cd_i64)nRVA < nStart + nSize)) {
            return i;
        }
    }

    return -1;
}

cd_i64 xpe_rva_to_offset(XPE *pPE, cd_u32 nRVA)
{
    if (nRVA == 0) {
        return -1;
    }

    return xx_memory_map_address_to_offset_ex(&pPE->map, pPE->nImageBase + nRVA, XX_MEMORY_MAP_LOOKUP_FIRST_MATCH);
}

/* ------------------------------------------------------------- sections  */

static void parse_sections(XPE *pPE)
{
    cd_i64 nOffset = pPE->nLfanew + 4 + 20 + pPE->nSizeOfOptionalHeader;
    int i = 0;

    if (pPE->nNumberOfSections > 4096) {
        pPE->nNumberOfSections = 4096;
    }

    pPE->nSectionCount = pPE->nNumberOfSections;

    if (pPE->nSectionCount <= 0) {
        pPE->nSectionCount = 0;

        return;
    }

    pPE->pSections = (XPESection *)cd_calloc((size_t)pPE->nSectionCount, sizeof(XPESection));

    for (i = 0; i < pPE->nSectionCount; i++) {
        cd_i64 nBase = nOffset + i * 40;
        int j = 0;

        for (j = 0; j < 8; j++) {
            char nChar = (char)xx_io_get_u8(pPE->pFile->pDevice, nBase + j);

            pPE->pSections[i].sName[j] = nChar;
        }

        pPE->pSections[i].sName[8] = 0;
        pPE->pSections[i].nVirtualSize = xx_io_get_u32(pPE->pFile->pDevice, nBase + 8, false);
        pPE->pSections[i].nVirtualAddress = xx_io_get_u32(pPE->pFile->pDevice, nBase + 12, false);
        pPE->pSections[i].nSizeOfRawData = xx_io_get_u32(pPE->pFile->pDevice, nBase + 16, false);
        pPE->pSections[i].nPointerToRawData = xx_io_get_u32(pPE->pFile->pDevice, nBase + 20, false);
        pPE->pSections[i].nCharacteristics = xx_io_get_u32(pPE->pFile->pDevice, nBase + 36, false);
    }
}

static void build_memory_map(XPE *pPE)
{
    cd_i64 nHeadersSize = 0;
    cd_i64 nRawSize = 0;
    int i = 0;

    xx_memory_map_init(&pPE->map);
    pPE->map.module_address = pPE->nImageBase;
    pPE->map.binary_size = pPE->pFile->nSize;
    pPE->map.file_type = pPE->bIs64 ? XX_FILE_TYPE_PE64 : XX_FILE_TYPE_PE32;
    pPE->map.endian = XX_ENDIAN_LITTLE;
    pPE->map.mode = XX_MEMORY_MAP_MODE_SECTIONS;
    pPE->map.is_image = true;

    nHeadersSize = pPE->nLfanew + 4 + 20 + pPE->nSizeOfOptionalHeader + (cd_i64)pPE->nSectionCount * 40;
    nHeadersSize = ALIGN_UP(nHeadersSize, (cd_i64)pPE->nFileAlignment);

    if (nHeadersSize > pPE->pFile->nSize) {
        nHeadersSize = pPE->pFile->nSize;
    }

    /* nHeadersSize cannot be negative here: nLfanew is validated positive when
     * the header is parsed, the other terms are unsigned header fields with
     * nSectionCount capped at 4096, and the clamp above only lowers the value
     * to the file size. The widening to the cd_u64 virtual size is therefore
     * value-preserving, and made explicit so it is not read as an oversight. */
    die_map_add_part(&pPE->map, 0, nHeadersSize, pPE->nImageBase, (cd_u64)nHeadersSize, XX_FILE_PART_HEADER, "Header");
    nRawSize = nHeadersSize;

    for (i = 0; i < pPE->nSectionCount; i++) {
        XPESection *pSection = &pPE->pSections[i];
        cd_i64 nOffset = pSection->nPointerToRawData;
        cd_i64 nSize = pSection->nSizeOfRawData;
        cd_u64 nVirtualSize = pSection->nVirtualSize ? pSection->nVirtualSize : pSection->nSizeOfRawData;

        nVirtualSize = (cd_u64)ALIGN_UP((cd_i64)nVirtualSize, (cd_i64)pPE->nSectionAlignment);

        if (nOffset > pPE->pFile->nSize) {
            nOffset = pPE->pFile->nSize;
            nSize = 0;
        } else if (nOffset + nSize > pPE->pFile->nSize) {
            nSize = pPE->pFile->nSize - nOffset;
        }

        pSection->nMappedOffset = nOffset;
        pSection->nMappedSize = nSize;

        die_map_add_part(&pPE->map, nOffset, nSize, pPE->nImageBase + pSection->nVirtualAddress, nVirtualSize, XX_FILE_PART_SECTION, pSection->sName);

        /* The raw extent is tracked here rather than read back off the map:
         * a section clamped to zero length still moves it -- its offset is
         * the file size, which is what tells the overlay check there is
         * nothing appended -- and the map cannot hold a zero-length record. */
        if (nOffset + nSize > nRawSize) {
            nRawSize = nOffset + nSize;
        }
    }

    if (nRawSize < pPE->pFile->nSize) {
        die_map_add_part(&pPE->map, nRawSize, pPE->pFile->nSize - nRawSize, 0, 0, XX_FILE_PART_OVERLAY, "Overlay");
        pPE->nOverlayOffset = nRawSize;
        pPE->nOverlaySize = pPE->pFile->nSize - nRawSize;
    } else {
        pPE->nOverlayOffset = nRawSize;
        pPE->nOverlaySize = 0;
    }
}

/* -------------------------------------------------------------- imports  */

static void parse_imports(XPE *pPE)
{
    cd_i64 nOffset = xpe_rva_to_offset(pPE, pPE->pDirRVA[XPE_DIR_IMPORT]);
    int nCount = 0;
    int i = 0;
    int nTotalPositions = 0;
    CDBuf hashBuf;

    if (nOffset == -1) {
        return;
    }

    cdbuf_init(&hashBuf);

    for (nCount = 0; nCount < 4096; nCount++) {
        cd_i64 nBase = nOffset + nCount * 20;
        cd_u32 nOriginalFirstThunk = xx_io_get_u32(pPE->pFile->pDevice, nBase, false);
        cd_u32 nName = xx_io_get_u32(pPE->pFile->pDevice, nBase + 12, false);
        cd_u32 nFirstThunk = xx_io_get_u32(pPE->pFile->pDevice, nBase + 16, false);

        if ((nOriginalFirstThunk == 0) && (nName == 0) && (nFirstThunk == 0)) {
            break;
        }

        if (nBase + 20 > pPE->pFile->nSize) {
            break;
        }
    }

    if (nCount == 0) {
        cdbuf_free(&hashBuf);

        return;
    }

    pPE->pImports = (XPEImport *)cd_calloc((size_t)nCount, sizeof(XPEImport));
    pPE->nImportCount = nCount;

    for (i = 0; i < nCount; i++) {
        cd_i64 nBase = nOffset + i * 20;
        cd_u32 nOriginalFirstThunk = xx_io_get_u32(pPE->pFile->pDevice, nBase, false);
        cd_u32 nName = xx_io_get_u32(pPE->pFile->pDevice, nBase + 12, false);
        cd_u32 nFirstThunk = xx_io_get_u32(pPE->pFile->pDevice, nBase + 16, false);
        cd_i64 nNameOffset = xpe_rva_to_offset(pPE, nName);
        cd_u32 nThunkRVA = nOriginalFirstThunk ? nOriginalFirstThunk : nFirstThunk;
        cd_i64 nThunkOffset = xpe_rva_to_offset(pPE, nThunkRVA);
        CDVec vecFunctions;
        int j = 0;
        int nRemaining = XPE_MAX_IMPORT_POSITIONS - nTotalPositions;
        CDBuf posBuf;

        /* XPE::getImports budgets the positions per library and over the whole
         * import table, so a descriptor list that shares one thunk array
         * cannot multiply out.                                              */
        if (nRemaining > XPE_MAX_POSITIONS_PER_LIBRARY) {
            nRemaining = XPE_MAX_POSITIONS_PER_LIBRARY;
        }

        cdvec_init(&vecFunctions);
        cdbuf_init(&posBuf);

        pPE->pImports[i].pName = (nNameOffset != -1) ? die_ansi_string(pPE->pFile, nNameOffset, 256) : cd_strdup("");

        if (nThunkOffset != -1) {
            for (j = 0; j < nRemaining; j++) {
                cd_u64 nThunk = 0;
                char *pFunctionName = NULL;

                if (pPE->bIs64) {
                    nThunk = xx_io_get_u64(pPE->pFile->pDevice, nThunkOffset + j * 8, false);
                } else {
                    nThunk = xx_io_get_u32(pPE->pFile->pDevice, nThunkOffset + j * 4, false);
                }

                if (nThunk == 0) {
                    break;
                }

                if ((pPE->bIs64 && (nThunk & 0x8000000000000000ull)) || ((!pPE->bIs64) && (nThunk & 0x80000000u))) {
                    char sBuf[32];

                    x_snprintf(sBuf, sizeof(sBuf), "%llu", (unsigned long long)(nThunk & 0xFFFF));
                    pFunctionName = cd_strdup(sBuf);
                } else {
                    cd_i64 nHintOffset = xpe_rva_to_offset(pPE, (cd_u32)nThunk);

                    if (nHintOffset == -1) {
                        pFunctionName = cd_strdup("");
                    } else {
                        pFunctionName = die_ansi_string(pPE->pFile, nHintOffset + 2, 512);
                    }
                }

                cdvec_push(&vecFunctions, pFunctionName);
                cdbuf_append_str(&posBuf, pFunctionName);
                cdbuf_append_str(&hashBuf, pPE->pImports[i].pName);
                cdbuf_append_str(&hashBuf, pFunctionName);
            }
        }

        pPE->pImports[i].nFunctionCount = (int)vecFunctions.nSize;
        pPE->pImports[i].ppFunctions = (char **)cd_malloc((vecFunctions.nSize ? vecFunctions.nSize : 1) * sizeof(char *));

        for (j = 0; j < (int)vecFunctions.nSize; j++) {
            pPE->pImports[i].ppFunctions[j] = (char *)vecFunctions.ppData[j];
        }

        pPE->pImports[i].nPositionHash = die_string_crc32c(posBuf.pData ? posBuf.pData : "");
        nTotalPositions += pPE->pImports[i].nFunctionCount;

        cdbuf_free(&posBuf);
        cdvec_free(&vecFunctions);

        if (nTotalPositions >= XPE_MAX_IMPORT_POSITIONS) {
            pPE->nImportCount = i + 1;

            break;
        }
    }

    pPE->nImportHash32 = die_string_crc32c(hashBuf.pData ? hashBuf.pData : "");

    {
        cd_u64 nHash64 = 0;
        int j = 0;

        for (i = 0; i < pPE->nImportCount; i++) {
            for (j = 0; j < pPE->pImports[i].nFunctionCount; j++) {
                CDBuf record;

                cdbuf_init(&record);
                cdbuf_append_str(&record, pPE->pImports[i].pName);
                cdbuf_append_ch(&record, ' ');
                cdbuf_append_str(&record, pPE->pImports[i].ppFunctions[j]);
                nHash64 += die_string_crc32c(record.pData ? record.pData : "");
                cdbuf_free(&record);
            }
        }

        pPE->nImportHash64 = nHash64;
    }

    cdbuf_free(&hashBuf);
}

/* -------------------------------------------------------------- exports  */

static void parse_exports(XPE *pPE)
{
    cd_i64 nOffset = xpe_rva_to_offset(pPE, pPE->pDirRVA[XPE_DIR_EXPORT]);
    cd_u32 nNumberOfNames = 0;
    cd_u32 nAddressOfNames = 0;
    cd_i64 nNamesOffset = 0;
    cd_u32 i = 0;

    if (nOffset == -1) {
        return;
    }

    nNumberOfNames = xx_io_get_u32(pPE->pFile->pDevice, nOffset + 24, false);
    nAddressOfNames = xx_io_get_u32(pPE->pFile->pDevice, nOffset + 32, false);

    if ((nNumberOfNames == 0) || (nNumberOfNames > 100000)) {
        return;
    }

    nNamesOffset = xpe_rva_to_offset(pPE, nAddressOfNames);

    if (nNamesOffset == -1) {
        return;
    }

    pPE->ppExportFunctions = (char **)cd_calloc(nNumberOfNames, sizeof(char *));

    for (i = 0; i < nNumberOfNames; i++) {
        cd_u32 nNameRVA = xx_io_get_u32(pPE->pFile->pDevice, nNamesOffset + i * 4, false);
        cd_i64 nNameOffset = xpe_rva_to_offset(pPE, nNameRVA);

        pPE->ppExportFunctions[i] = (nNameOffset != -1) ? die_ansi_string(pPE->pFile, nNameOffset, 512) : cd_strdup("");
    }

    pPE->nExportCount = (int)nNumberOfNames;
}

/* ------------------------------------------------------------ resources  */

typedef struct {
    cd_u32 nId;
    char *pName;
} ResIdName;

static ResIdName resource_id_name(XPE *pPE, cd_i64 nResourceOffset, cd_u32 nName)
{
    ResIdName result;

    result.nId = 0;
    result.pName = NULL;

    if (nName & 0x80000000u) {
        cd_i64 nStringOffset = nResourceOffset + (cd_i64)(nName & 0x7FFFFFFFu);
        cd_u16 nLength = xx_io_get_u16(pPE->pFile->pDevice, nStringOffset, false);
        CDBuf buf;
        cd_u16 i = 0;

        cdbuf_init(&buf);

        if (nLength > 1024) {
            nLength = 1024;
        }

        for (i = 0; i < nLength; i++) {
            cd_u16 nChar = xx_io_get_u16(pPE->pFile->pDevice, nStringOffset + 2 + i * 2, false);

            if (nChar < 0x80) {
                cdbuf_append_ch(&buf, (char)nChar);
            } else if (nChar < 0x800) {
                cdbuf_append_ch(&buf, (char)(0xC0 | (nChar >> 6)));
                cdbuf_append_ch(&buf, (char)(0x80 | (nChar & 0x3F)));
            } else {
                cdbuf_append_ch(&buf, (char)(0xE0 | (nChar >> 12)));
                cdbuf_append_ch(&buf, (char)(0x80 | ((nChar >> 6) & 0x3F)));
                cdbuf_append_ch(&buf, (char)(0x80 | (nChar & 0x3F)));
            }
        }

        result.pName = cdbuf_detach(&buf, NULL);
    } else {
        result.nId = nName;
    }

    return result;
}

static void parse_resources(XPE *pPE)
{
    cd_i64 nResourceOffset = xpe_rva_to_offset(pPE, pPE->pDirRVA[XPE_DIR_RESOURCE]);
    CDVec vec;
    int i = 0;
    cd_u16 nNamed0 = 0;
    cd_u16 nId0 = 0;
    cd_i64 nLevel0 = 0;

    if (nResourceOffset == -1) {
        return;
    }

    if (xx_io_get_u32(pPE->pFile->pDevice, nResourceOffset, false) != 0) {
        return; /* Characteristics must be zero */
    }

    cdvec_init(&vec);

    nNamed0 = xx_io_get_u16(pPE->pFile->pDevice, nResourceOffset + 12, false);
    nId0 = xx_io_get_u16(pPE->pFile->pDevice, nResourceOffset + 14, false);

    if ((cd_u32)(nNamed0 + nId0) > 1000) {
        cdvec_free(&vec);

        return;
    }

    nLevel0 = nResourceOffset + 16;

    for (i = 0; i < nNamed0 + nId0; i++) {
        cd_u32 nName0 = xx_io_get_u32(pPE->pFile->pDevice, nLevel0, false);
        cd_u32 nOffsetToDirectory0 = xx_io_get_u32(pPE->pFile->pDevice, nLevel0 + 4, false);
        ResIdName irin0;
        cd_i64 nDir1 = 0;
        cd_u16 nNamed1 = 0;
        cd_u16 nId1 = 0;
        cd_i64 nLevel1 = 0;
        int j = 0;

        if (nOffsetToDirectory0 == 0) {
            break;
        }

        irin0 = resource_id_name(pPE, nResourceOffset, nName0);
        nDir1 = nResourceOffset + (cd_i64)(nOffsetToDirectory0 & 0x7FFFFFFFu);

        if (xx_io_get_u32(pPE->pFile->pDevice, nDir1, false) != 0) {
            cd_free(irin0.pName);
            break;
        }

        nNamed1 = xx_io_get_u16(pPE->pFile->pDevice, nDir1 + 12, false);
        nId1 = xx_io_get_u16(pPE->pFile->pDevice, nDir1 + 14, false);
        nLevel1 = nDir1 + 16;

        if ((cd_u32)(nNamed1 + nId1) > 1000) {
            cd_free(irin0.pName);
            nLevel0 += 8;
            continue;
        }

        for (j = 0; j < nNamed1 + nId1; j++) {
            cd_u32 nName1 = xx_io_get_u32(pPE->pFile->pDevice, nLevel1, false);
            cd_u32 nOffsetToDirectory1 = xx_io_get_u32(pPE->pFile->pDevice, nLevel1 + 4, false);
            ResIdName irin1 = resource_id_name(pPE, nResourceOffset, nName1);
            cd_i64 nDir2 = nResourceOffset + (cd_i64)(nOffsetToDirectory1 & 0x7FFFFFFFu);
            cd_u16 nNamed2 = 0;
            cd_u16 nId2 = 0;
            cd_i64 nLevel2 = 0;
            int k = 0;

            if (xx_io_get_u32(pPE->pFile->pDevice, nDir2, false) != 0) {
                cd_free(irin1.pName);
                break;
            }

            nNamed2 = xx_io_get_u16(pPE->pFile->pDevice, nDir2 + 12, false);
            nId2 = xx_io_get_u16(pPE->pFile->pDevice, nDir2 + 14, false);
            nLevel2 = nDir2 + 16;

            if ((cd_u32)(nNamed2 + nId2) > 1000) {
                cd_free(irin1.pName);
                nLevel1 += 8;
                continue;
            }

            for (k = 0; k < nNamed2 + nId2; k++) {
                cd_u32 nName2 = xx_io_get_u32(pPE->pFile->pDevice, nLevel2, false);
                cd_u32 nOffsetToData = xx_io_get_u32(pPE->pFile->pDevice, nLevel2 + 4, false);
                ResIdName irin2 = resource_id_name(pPE, nResourceOffset, nName2);
                cd_i64 nDataEntry = nResourceOffset + (cd_i64)nOffsetToData;
                XPEResource *pResource = (XPEResource *)cd_calloc(1, sizeof(XPEResource));

                pResource->nTypeId = irin0.nId;
                pResource->pTypeName = irin0.pName ? cd_strdup(irin0.pName) : NULL;
                pResource->nNameId = irin1.nId;
                pResource->pName = irin1.pName ? cd_strdup(irin1.pName) : NULL;
                pResource->nLangId = irin2.nId;
                pResource->nOffset = xpe_rva_to_offset(pPE, xx_io_get_u32(pPE->pFile->pDevice, nDataEntry, false));
                pResource->nSize = xx_io_get_u32(pPE->pFile->pDevice, nDataEntry + 4, false);

                cdvec_push(&vec, pResource);
                cd_free(irin2.pName);

                if (vec.nSize >= 10000) {
                    break;
                }

                nLevel2 += 8;
            }

            cd_free(irin1.pName);

            if (vec.nSize >= 10000) {
                break;
            }

            nLevel1 += 8;
        }

        cd_free(irin0.pName);

        if (vec.nSize >= 10000) {
            break;
        }

        nLevel0 += 8;
    }

    pPE->nResourceCount = (int)vec.nSize;

    if (pPE->nResourceCount) {
        pPE->pResources = (XPEResource *)cd_calloc((size_t)pPE->nResourceCount, sizeof(XPEResource));

        for (i = 0; i < pPE->nResourceCount; i++) {
            XPEResource *pResource = (XPEResource *)vec.ppData[i];

            pPE->pResources[i] = *pResource;
            cd_free(pResource);
        }
    }

    cdvec_free(&vec);
}

static XPEResource *find_resource_by_type(XPE *pPE, cd_u32 nTypeId)
{
    int i = 0;

    for (i = 0; i < pPE->nResourceCount; i++) {
        if ((pPE->pResources[i].nTypeId == nTypeId) && (pPE->pResources[i].pTypeName == NULL)) {
            return &pPE->pResources[i];
        }
    }

    return NULL;
}

/* ------------------------------------------------------- version blocks  */

static void version_add(XPE *pPE, const char *pKey, const char *pValue)
{
    pPE->pVersionRecords = (XPEVersionRecord *)cd_realloc(pPE->pVersionRecords, (size_t)(pPE->nVersionCount + 1) * sizeof(XPEVersionRecord));
    pPE->pVersionRecords[pPE->nVersionCount].pKey = cd_strdup(pKey);
    pPE->pVersionRecords[pPE->nVersionCount].pValue = cd_strdup(pValue);
    pPE->nVersionCount++;
}

static cd_u32 parse_version_block(XPE *pPE, cd_i64 nOffset, cd_i64 nSize, const char *pPrefix, int nLevel)
{
    cd_u16 nLength = 0;
    cd_u16 nValueLength = 0;
    cd_u16 nType = 0;
    char *pTitle = NULL;
    cd_i64 nTitleUnits = 0;
    cd_i64 nDelta = 0;
    CDBuf prefix;
    cd_u32 nResult = 0;

    if (nSize < 6) {
        return 0;
    }

    nLength = xx_io_get_u16(pPE->pFile->pDevice, nOffset, false);
    nValueLength = xx_io_get_u16(pPE->pFile->pDevice, nOffset + 2, false);
    nType = xx_io_get_u16(pPE->pFile->pDevice, nOffset + 4, false);

    (void)nType;

    if ((nLength == 0) || (nLength > nSize)) {
        return 0;
    }

    if (nValueLength >= nLength) {
        return 0;
    }

    /* szKey occupies (units + 1) UTF-16 code units on disk; the UTF-8 length
     * of the converted title is not the same thing for non-ASCII keys.
     * XPE::__getResourcesVersion reads at most 256 units (read_unicodeString's
     * default) and advances by (sTitle.length() + 1) * sizeof(quint16).      */
    pTitle = die_unicode_string_n(pPE->pFile, nOffset + 6, 256, 0, &nTitleUnits);

    nDelta = 6;
    nDelta += (nTitleUnits + 1) * 2;
    nDelta = ALIGN_UP(nDelta, 4);

    cdbuf_init(&prefix);
    cdbuf_append_str(&prefix, pPrefix);

    if (prefix.nSize) {
        cdbuf_append_ch(&prefix, '.');
    }

    cdbuf_append_str(&prefix, pTitle);

    /* A block whose prefix and title are both empty never allocates, so pData
     * is still NULL here -- a malformed resource reaches this with no key.  */
    if (x_strcmp(prefix.pData ? prefix.pData : "", "VS_VERSION_INFO") == 0) {
        if (nValueLength >= 52) {
            pPE->nFileVersionMS = xx_io_get_u32(pPE->pFile->pDevice, nOffset + nDelta + 8, false);
            pPE->nFileVersionLS = xx_io_get_u32(pPE->pFile->pDevice, nOffset + nDelta + 12, false);
        }
    }

    if (nLevel == 3) {
        /* The value is bounded by both wValueLength and what is left of this
         * record - min(wValueLength, (wLength - nDelta) / 2) UTF-16 units,
         * exactly as XPE::__getResourcesVersion computes nValueCharacters.
         * Without that bound a record carrying a short or zero-length value
         * reads on into the bytes of the record that follows it.            */
        cd_i64 nAvailUnits = ((cd_i64)nLength - nDelta) / 2;
        cd_i64 nValueUnits = (cd_i64)nValueLength;
        char *pValue = NULL;

        if (nAvailUnits < 0) {
            nAvailUnits = 0;
        }

        if (nValueUnits > nAvailUnits) {
            nValueUnits = nAvailUnits;
        }

        /* die_unicode_string reads 0x10000 units when given a non-positive
         * limit, while read_unicodeString returns an empty string. */
        pValue = (nValueUnits > 0) ? die_unicode_string(pPE->pFile, nOffset + nDelta, nValueUnits, 0) : cd_strdup("");

        version_add(pPE, pTitle, pValue);
        cd_free(pValue);
    }

    nDelta += nValueLength;

    if (nLevel < 3) {
        cd_i64 nRemaining = nLength - nDelta;

        while (nRemaining > 0) {
            cd_u32 nInner = parse_version_block(pPE, nOffset + nDelta, nLength - nDelta, prefix.pData, nLevel + 1);

            if (nInner == 0) {
                break;
            }

            nInner = (cd_u32)ALIGN_UP((cd_i64)nInner, 4);
            nDelta += nInner;
            nRemaining -= nInner;
        }
    }

    nResult = nLength;

    cdbuf_free(&prefix);
    cd_free(pTitle);

    return nResult;
}

static void parse_version(XPE *pPE)
{
    XPEResource *pResource = find_resource_by_type(pPE, 16); /* RT_VERSION */

    if ((pResource == NULL) || (pResource->nOffset == -1)) {
        return;
    }

    parse_version_block(pPE, pResource->nOffset, pResource->nSize, "", 0);
}

static void parse_manifest(XPE *pPE)
{
    XPEResource *pResource = find_resource_by_type(pPE, 24); /* RT_MANIFEST */

    if ((pResource == NULL) || (pResource->nOffset == -1)) {
        pPE->pManifest = cd_strdup("");

        return;
    }

    {
        cd_i64 nSize = pResource->nSize;

        if (nSize > 4000) {
            nSize = 4000;
        }

        pPE->pManifest = die_ansi_string(pPE->pFile, pResource->nOffset, nSize);
    }
}

/* ----------------------------------------------------------- debug data  */

static void parse_debug(XPE *pPE)
{
    cd_i64 nOffset = xpe_rva_to_offset(pPE, pPE->pDirRVA[XPE_DIR_DEBUG]);
    cd_u32 nSize = pPE->pDirSize[XPE_DIR_DEBUG];
    cd_u32 nMax = 0;
    cd_u32 i = 0;
    int nCount = 0;

    if ((nOffset == -1) || (nSize < 28)) {
        return;
    }

    nMax = nSize / 28;

    if (nMax > 256) {
        nMax = 256;
    }

    pPE->pDebugRecords = (XPEDebugRecord *)cd_calloc(nMax, sizeof(XPEDebugRecord));

    for (i = 0; i < nMax; i++) {
        cd_i64 nBase = nOffset + i * 28;
        cd_u32 nPointerToRawData = xx_io_get_u32(pPE->pFile->pDevice, nBase + 24, false);

        /* XPE::getDebugList stops at the first entry without usable raw
         * data - a trailing REPRO record is therefore not reported.        */
        if ((nPointerToRawData == 0) || ((cd_i64)nPointerToRawData >= pPE->pFile->nSize)) {
            break;
        }

        pPE->pDebugRecords[nCount].nType = xx_io_get_u32(pPE->pFile->pDevice, nBase + 12, false);
        pPE->pDebugRecords[nCount].nSize = xx_io_get_u32(pPE->pFile->pDevice, nBase + 16, false);
        pPE->pDebugRecords[nCount].nOffset = (cd_i64)nPointerToRawData;
        nCount++;
    }

    pPE->nDebugCount = nCount;
}

/* ---------------------------------------------------------- rich header  */

static void parse_rich(XPE *pPE)
{
    cd_i64 nStubOffset = 64;
    cd_i64 nStubSize = pPE->nLfanew - 64;
    cd_i64 nRichOffset = 0;
    cd_u32 nXorKey = 0;
    cd_i64 nCurrent = 0;

    if ((nStubSize <= 0) || (nStubSize > 0x400)) {
        return;
    }

    if (die_find_ansi_string(pPE->pFile, nStubOffset, nStubSize, "Rich") != -1) {
        pPE->bRichPresent = 1;
    } else {
        return;
    }

    nRichOffset = die_find_ansi_string(pPE->pFile, nStubOffset, nStubSize, "Rich");
    nXorKey = xx_io_get_u32(pPE->pFile->pDevice, nRichOffset + 4, false);
    nCurrent = nRichOffset - 4;

    while (nCurrent > nStubOffset) {
        cd_u32 nTemp = xx_io_get_u32(pPE->pFile->pDevice, nCurrent, false) ^ nXorKey;

        if (nTemp == 0x536e6144) { /* "DanS" */
            CDVec vec;
            size_t i = 0;

            cdvec_init(&vec);
            nCurrent += 16;

            for (; nCurrent < nRichOffset; nCurrent += 8) {
                XPERichRecord *pRecord = (XPERichRecord *)cd_calloc(1, sizeof(XPERichRecord));
                cd_u32 nValue1 = xx_io_get_u32(pPE->pFile->pDevice, nCurrent, false) ^ nXorKey;
                cd_u32 nValue2 = xx_io_get_u32(pPE->pFile->pDevice, nCurrent + 4, false) ^ nXorKey;

                pRecord->nId = (cd_u16)(nValue1 >> 16);
                pRecord->nVersion = (cd_u16)(nValue1 & 0xFFFF);
                pRecord->nCount = nValue2;

                cdvec_push(&vec, pRecord);
            }

            pPE->nRichCount = (int)vec.nSize;

            if (pPE->nRichCount) {
                pPE->pRichRecords = (XPERichRecord *)cd_calloc((size_t)pPE->nRichCount, sizeof(XPERichRecord));

                for (i = 0; i < vec.nSize; i++) {
                    pPE->pRichRecords[i] = *(XPERichRecord *)vec.ppData[i];
                    cd_free(vec.ppData[i]);
                }
            }

            cdvec_free(&vec);
            break;
        }

        nCurrent -= 4;
    }
}

/* ------------------------------------------------------------------ .NET */

static cd_u32 read_compressed_uint(DieFile *pFile, cd_i64 nOffset, int *pnBytes)
{
    cd_u8 nFirst = xx_io_get_u8(pFile->pDevice, nOffset);

    if ((nFirst & 0x80) == 0) {
        *pnBytes = 1;

        return nFirst;
    }

    if ((nFirst & 0xC0) == 0x80) {
        *pnBytes = 2;

        return (cd_u32)(((nFirst & 0x3F) << 8) | xx_io_get_u8(pFile->pDevice, nOffset + 1));
    }

    *pnBytes = 4;

    return (cd_u32)(((nFirst & 0x1F) << 24) | (xx_io_get_u8(pFile->pDevice, nOffset + 1) << 16) | (xx_io_get_u8(pFile->pDevice, nOffset + 2) << 8) | xx_io_get_u8(pFile->pDevice, nOffset + 3));
}

/* Computes the row sizes and absolute offsets of every metadata table,
 * mirroring XPE::getCliInfo.                                               */
static void parse_net_tables(XPE *pPE)
{
    XPECli *pCli = &pPE->cli;
    cd_i64 nOffset = pCli->nTablesOffset;
    cd_u8 nHeapOffsetSizes = 0;
    cd_u64 nValid = 0;
    int i = 0;

    if ((pCli->nTablesOffset <= 0) || (pCli->nTablesSize < 24)) {
        return;
    }

    nHeapOffsetSizes = xx_io_get_u8(pPE->pFile->pDevice, nOffset + 6);
    nValid = xx_io_get_u64(pPE->pFile->pDevice, nOffset + 8, false);

    nOffset += 24;

    for (i = 0; i < 64; i++) {
        if (nValid & (((cd_u64)1) << i)) {
            pCli->pRows[i] = xx_io_get_u32(pPE->pFile->pDevice, nOffset, false);
            nOffset += 4;
        } else {
            pCli->pRows[i] = 0;
        }
    }

    pCli->nStringIndexSize = (nHeapOffsetSizes & 0x01) ? 4 : 2;
    pCli->nGuidIndexSize = (nHeapOffsetSizes & 0x02) ? 4 : 2;
    pCli->nBlobIndexSize = (nHeapOffsetSizes & 0x04) ? 4 : 2;

    pCli->nResolutionScopeSize = 2;
    pCli->nTypeDefOrRefSize = 2;
    pCli->nMemberRefParentSize = 2;
    pCli->nHasConstantSize = 2;
    pCli->nHasCustomAttributeSize = 2;
    pCli->nCustomAttributeTypeSize = 2;
    pCli->nHasFieldMarshalSize = 2;
    pCli->nHasDeclSecuritySize = 2;
    pCli->nHasSemanticsSize = 2;
    pCli->nMethodDefOrRefSize = 2;
    pCli->nMemberForwardedSize = 2;

    if ((pCli->pRows[MDT_Module] > 0x3FFF) || (pCli->pRows[MDT_ModuleRef] > 0x3FFF) || (pCli->pRows[MDT_AssemblyRef] > 0x3FFF) ||
        (pCli->pRows[MDT_TypeRef] > 0x3FFF)) {
        pCli->nResolutionScopeSize = 4;
    }

    if ((pCli->pRows[MDT_ModuleRef] > 0x3FFF) || (pCli->pRows[MDT_TypeDef] > 0x3FFF) || (pCli->pRows[MDT_TypeSpec] > 0x3FFF)) {
        pCli->nTypeDefOrRefSize = 4;
    }

    if ((pCli->pRows[MDT_TypeDef] > 0x1FFF) || (pCli->pRows[MDT_TypeRef] > 0x1FFF) || (pCli->pRows[MDT_ModuleRef] > 0x1FFF) ||
        (pCli->pRows[MDT_MethodDef] > 0x1FFF) || (pCli->pRows[MDT_TypeSpec] > 0x1FFF)) {
        pCli->nMemberRefParentSize = 4;
    }

    if ((pCli->pRows[MDT_Field] > 0x3FFF) || (pCli->pRows[MDT_Param] > 0x3FFF) || (pCli->pRows[MDT_Property] > 0x3FFF)) {
        pCli->nHasConstantSize = 4;
    }

    if ((pCli->pRows[MDT_MethodDef] > 0x7FF) || (pCli->pRows[MDT_Field] > 0x7FF) || (pCli->pRows[MDT_TypeRef] > 0x7FF) || (pCli->pRows[MDT_TypeDef] > 0x7FF) ||
        (pCli->pRows[MDT_Param] > 0x7FF) || (pCli->pRows[MDT_InterfaceImpl] > 0x7FF) || (pCli->pRows[MDT_MemberRef] > 0x7FF) || (pCli->pRows[MDT_Module] > 0x7FF) ||
        (pCli->pRows[MDT_Property] > 0x7FF) || (pCli->pRows[MDT_Event] > 0x7FF) || (pCli->pRows[MDT_StandAloneSig] > 0x7FF) || (pCli->pRows[MDT_ModuleRef] > 0x7FF) ||
        (pCli->pRows[MDT_TypeSpec] > 0x7FF) || (pCli->pRows[MDT_Assembly] > 0x7FF)) {
        pCli->nHasCustomAttributeSize = 4;
    }

    if ((pCli->pRows[MDT_MethodDef] > 0x1FFF) || (pCli->pRows[MDT_MemberRef] > 0x1FFF)) {
        pCli->nCustomAttributeTypeSize = 4;
    }

    if ((pCli->pRows[MDT_MethodDef] > 0x7FFF) || (pCli->pRows[MDT_MemberRef] > 0x7FFF)) {
        pCli->nMethodDefOrRefSize = 4;
    }

    if ((pCli->pRows[MDT_Field] > 0x7FFF) || (pCli->pRows[MDT_Param] > 0x7FFF)) {
        pCli->nHasFieldMarshalSize = 4;
    }

    if ((pCli->pRows[MDT_TypeDef] > 0x3FFF) || (pCli->pRows[MDT_MethodDef] > 0x3FFF) || (pCli->pRows[MDT_Assembly] > 0x3FFF)) {
        pCli->nHasDeclSecuritySize = 4;
    }

    if ((pCli->pRows[MDT_Event] > 0x7FFF) || (pCli->pRows[MDT_Property] > 0x7FFF)) {
        pCli->nHasSemanticsSize = 4;
    }

    if ((pCli->pRows[MDT_Field] > 0x7FFF) || (pCli->pRows[MDT_MethodDef] > 0x7FFF)) {
        pCli->nMemberForwardedSize = 4;
    }

    for (i = 0; i < 64; i++) {
        pCli->pIndexSize[i] = (pCli->pRows[i] > 0xFFFF) ? 4 : 2;
    }

    {
        int nStr = pCli->nStringIndexSize;
        int nGuid = pCli->nGuidIndexSize;
        int nBlob = pCli->nBlobIndexSize;

        pCli->pElementSize[MDT_Module] = 2 + nStr + nGuid * 3;
        pCli->pElementSize[MDT_TypeRef] = pCli->nResolutionScopeSize + nStr * 2;
        pCli->pElementSize[MDT_TypeDef] = 4 + nStr * 2 + pCli->nTypeDefOrRefSize + pCli->pIndexSize[MDT_Field] + pCli->pIndexSize[MDT_MethodDef];
        pCli->pElementSize[MDT_Field] = 2 + nStr + nBlob;
        pCli->pElementSize[MDT_MethodPtr] = pCli->pIndexSize[MDT_MethodDef];
        pCli->pElementSize[MDT_MethodDef] = 4 + 2 + 2 + nStr + nBlob + pCli->pIndexSize[MDT_Param];
        pCli->pElementSize[MDT_ParamPtr] = pCli->pIndexSize[MDT_Param];
        pCli->pElementSize[MDT_Param] = 2 + 2 + nStr;
        pCli->pElementSize[MDT_InterfaceImpl] = pCli->pIndexSize[MDT_TypeDef] + pCli->nTypeDefOrRefSize;
        pCli->pElementSize[MDT_MemberRef] = pCli->nMemberRefParentSize + nStr + nBlob;
        pCli->pElementSize[MDT_Constant] = 2 + pCli->nHasConstantSize + nBlob;
        pCli->pElementSize[MDT_CustomAttribute] = pCli->nHasCustomAttributeSize + pCli->nCustomAttributeTypeSize + nBlob;
        pCli->pElementSize[MDT_FieldMarshal] = pCli->nHasFieldMarshalSize + nBlob;
        pCli->pElementSize[MDT_DeclSecurity] = 2 + pCli->nHasDeclSecuritySize + nBlob;
        pCli->pElementSize[MDT_ClassLayout] = 2 + 4 + pCli->pIndexSize[MDT_TypeDef];
        pCli->pElementSize[MDT_FieldLayout] = 4 + pCli->pIndexSize[MDT_Field];
        pCli->pElementSize[MDT_StandAloneSig] = nBlob;
        pCli->pElementSize[MDT_EventMap] = pCli->pIndexSize[MDT_TypeDef] + pCli->pIndexSize[MDT_Event];
        pCli->pElementSize[MDT_EventPtr] = pCli->pIndexSize[MDT_Event];
        pCli->pElementSize[MDT_Event] = 2 + nStr + pCli->nTypeDefOrRefSize;
        pCli->pElementSize[MDT_PropertyMap] = pCli->pIndexSize[MDT_TypeDef] + pCli->pIndexSize[MDT_Property];
        pCli->pElementSize[MDT_PropertyPtr] = 2 + nStr + nBlob;
        pCli->pElementSize[MDT_Property] = 2 + nStr + nBlob;
        pCli->pElementSize[MDT_MethodSemantics] = 2 + pCli->pIndexSize[MDT_MethodDef] + pCli->nHasSemanticsSize;
        pCli->pElementSize[MDT_MethodImpl] = pCli->pIndexSize[MDT_TypeDef] + pCli->nMethodDefOrRefSize * 2;
        pCli->pElementSize[MDT_ModuleRef] = nStr;
        pCli->pElementSize[MDT_TypeSpec] = nBlob;
        pCli->pElementSize[MDT_ImplMap] = 2 + pCli->nMemberForwardedSize + nStr + pCli->pIndexSize[MDT_ModuleRef];
        pCli->pElementSize[MDT_FieldRVA] = 4 + pCli->pIndexSize[MDT_Field];
        pCli->pElementSize[MDT_ENCLog] = 4 + pCli->pIndexSize[MDT_MethodDef];
        pCli->pElementSize[MDT_ENCMap] = 4 + pCli->pIndexSize[MDT_MethodDef];
        pCli->pElementSize[MDT_Assembly] = 4 + 2 + 2 + 2 + 2 + 4 + nBlob + nStr * 2;
        pCli->pElementSize[MDT_AssemblyProcessor] = 4;
        pCli->pElementSize[MDT_AssemblyOS] = 4 * 3;
        pCli->pElementSize[MDT_AssemblyRef] = 4 + 2 + 2 + 2 + 2 + 4 + nBlob + nStr * 2;
        pCli->pElementSize[MDT_AssemblyRefProcessor] = 4 + pCli->pIndexSize[MDT_AssemblyRef];
        pCli->pElementSize[MDT_AssemblyRefOS] = 4 * 3 + pCli->pIndexSize[MDT_AssemblyRef];
        pCli->pElementSize[MDT_File] = 4 + nStr + nBlob;
        pCli->pElementSize[MDT_ExportedType] = 4 + 4 + nStr * 2 + pCli->pIndexSize[MDT_File];
        pCli->pElementSize[MDT_ManifestResource] = 4 + 4 + nStr + pCli->pIndexSize[MDT_File];
        pCli->pElementSize[MDT_NestedClass] = pCli->pIndexSize[MDT_TypeDef] * 2;
        pCli->pElementSize[MDT_GenericParam] = 2 + 2 + pCli->nTypeDefOrRefSize + nStr;
        pCli->pElementSize[MDT_MethodSpec] = pCli->nMethodDefOrRefSize + nBlob;
        /* Owner is an index into GenericParam, not the GenericParam row count. */
        pCli->pElementSize[MDT_GenericParamConstraint] = pCli->pIndexSize[MDT_GenericParam] + pCli->nTypeDefOrRefSize;
    }

    if (nHeapOffsetSizes & 0x40) {
        nOffset += 4;
    }

    for (i = 0; i < 64; i++) {
        if (pCli->pRows[i]) {
            pCli->pTableOffset[i] = nOffset;
            nOffset += (cd_i64)pCli->pElementSize[i] * (cd_i64)pCli->pRows[i];
        }
    }
}

/* Reads a heap/table index of 2 or 4 bytes. */
static cd_u32 md_index(XPE *pPE, cd_i64 nOffset, int nSize)
{
    if (nSize == 4) {
        return xx_io_get_u32(pPE->pFile->pDevice, nOffset, false);
    }

    return xx_io_get_u16(pPE->pFile->pDevice, nOffset, false);
}

/* Reads a string from the #Strings heap (caller frees). */
static char *md_string(XPE *pPE, cd_u32 nIndex)
{
    XPECli *pCli = &pPE->cli;

    if ((pCli->nStringsOffset <= 0) || ((cd_i64)nIndex >= pCli->nStringsSize)) {
        return cd_strdup("");
    }

    return die_ansi_string(pPE->pFile, pCli->nStringsOffset + (cd_i64)nIndex, pCli->nStringsSize - (cd_i64)nIndex);
}

static cd_i64 md_row_offset(XPE *pPE, int nTable, cd_u32 nRow)
{
    XPECli *pCli = &pPE->cli;

    if ((nRow >= pCli->pRows[nTable]) || (pCli->pTableOffset[nTable] == 0)) {
        return -1;
    }

    return pCli->pTableOffset[nTable] + (cd_i64)pCli->pElementSize[nTable] * (cd_i64)nRow;
}

typedef struct {
    cd_u32 nTypeName;
    cd_u32 nTypeNamespace;
    cd_u32 nFieldList;
    cd_u32 nMethodList;
} MDTypeDef;

static int md_typedef(XPE *pPE, cd_u32 nRow, MDTypeDef *pOut)
{
    XPECli *pCli = &pPE->cli;
    cd_i64 nOffset = md_row_offset(pPE, MDT_TypeDef, nRow);

    x_memset(pOut, 0, sizeof(*pOut));

    if (nOffset == -1) {
        return 0;
    }

    nOffset += 4; /* Flags */
    pOut->nTypeName = md_index(pPE, nOffset, pCli->nStringIndexSize);
    nOffset += pCli->nStringIndexSize;
    pOut->nTypeNamespace = md_index(pPE, nOffset, pCli->nStringIndexSize);
    nOffset += pCli->nStringIndexSize;
    nOffset += pCli->nTypeDefOrRefSize; /* Extends */
    pOut->nFieldList = md_index(pPE, nOffset, pCli->pIndexSize[MDT_Field]);
    nOffset += pCli->pIndexSize[MDT_Field];
    pOut->nMethodList = md_index(pPE, nOffset, pCli->pIndexSize[MDT_MethodDef]);

    return 1;
}

static cd_u32 md_methoddef_name(XPE *pPE, cd_u32 nRow)
{
    XPECli *pCli = &pPE->cli;
    cd_i64 nOffset = md_row_offset(pPE, MDT_MethodDef, nRow);

    if (nOffset == -1) {
        return 0;
    }

    return md_index(pPE, nOffset + 4 + 2 + 2, pCli->nStringIndexSize);
}

static cd_u32 md_methodptr(XPE *pPE, cd_u32 nRow)
{
    XPECli *pCli = &pPE->cli;
    cd_i64 nOffset = md_row_offset(pPE, MDT_MethodPtr, nRow);

    if (nOffset == -1) {
        return 0;
    }

    return md_index(pPE, nOffset, pCli->pIndexSize[MDT_MethodDef]);
}

static cd_u32 md_field_name(XPE *pPE, cd_u32 nRow)
{
    XPECli *pCli = &pPE->cli;
    cd_i64 nOffset = md_row_offset(pPE, MDT_Field, nRow);

    if (nOffset == -1) {
        return 0;
    }

    return md_index(pPE, nOffset + 2, pCli->nStringIndexSize);
}

/* XCLIAssembly keeps every row count in a signed qint32, so a count above
 * INT_MAX turns negative there and the walk it drives never runs. Reproduce
 * that narrowing instead of walking 4.29e9 attacker-supplied rows.          */
static cd_i64 md_row_count(cd_u32 nRows)
{
    if (nRows > 0x7FFFFFFFu) {
        return (cd_i64)nRows - 0x100000000ll;
    }

    return (cd_i64)nRows;
}

/* Locates a TypeDef row by namespace and name; returns -1 when absent.
 * An empty argument means "do not compare this component", matching the
 * reference implementation.                                                */
static cd_i64 md_find_typedef(XPE *pPE, const char *pNamespace, const char *pTypeName, MDTypeDef *pOut)
{
    cd_i64 nCount = md_row_count(pPE->cli.pRows[MDT_TypeDef]);
    cd_i64 i = 0;

    for (i = 0; i < nCount; i++) {
        MDTypeDef record;
        char *pName = NULL;
        char *pNs = NULL;
        int bMatch = 0;

        if (!md_typedef(pPE, (cd_u32)i, &record)) {
            break;
        }

        pName = (pTypeName[0]) ? md_string(pPE, record.nTypeName) : cd_strdup("");
        pNs = (pNamespace[0]) ? md_string(pPE, record.nTypeNamespace) : cd_strdup("");

        bMatch = ((x_strcmp(pNamespace, pNs) == 0) && (x_strcmp(pTypeName, pName) == 0)) ? 1 : 0;

        cd_free(pName);
        cd_free(pNs);

        if (bMatch) {
            *pOut = record;

            return i;
        }
    }

    return -1;
}

int xpe_net_type_present(XPE *pPE, const char *pNamespace, const char *pTypeName)
{
    MDTypeDef record;

    if (!pPE->cli.bValid) {
        return 0;
    }

    return (md_find_typedef(pPE, pNamespace, pTypeName, &record) != -1) ? 1 : 0;
}

int xpe_net_method_present(XPE *pPE, const char *pNamespace, const char *pTypeName, const char *pMethodName)
{
    MDTypeDef record;
    cd_i64 nRow = 0;
    cd_u32 nTypeCount = 0;
    cd_i64 nMethodCount = 0;
    cd_i64 j = 0;

    if (!pPE->cli.bValid) {
        return 0;
    }

    nTypeCount = pPE->cli.pRows[MDT_TypeDef];

    if (nTypeCount > 0xFFFF) {
        return 0;
    }

    nRow = md_find_typedef(pPE, pNamespace, pTypeName, &record);

    if (nRow == -1) {
        return 0;
    }

    if ((cd_u32)nRow < nTypeCount - 1) {
        MDTypeDef next;

        if (md_typedef(pPE, (cd_u32)nRow + 1, &next)) {
            nMethodCount = (cd_i64)next.nMethodList - (cd_i64)record.nMethodList;
        }
    } else {
        /* XCLIAssembly::isNetMethodPresent derives the last type's method
         * count from the MethodPtr table alone.                           */
        nMethodCount = md_row_count(pPE->cli.pRows[MDT_MethodPtr]) - (cd_i64)record.nMethodList;
    }

    for (j = 0; j < nMethodCount; j++) {
        cd_u32 nNameIndex = 0;
        char *pName = NULL;
        int bMatch = 0;

        if (record.nMethodList == 0) {
            break;
        }

        if (pPE->cli.pRows[MDT_MethodPtr]) {
            cd_u32 nMethod = md_methodptr(pPE, record.nMethodList + (cd_u32)j - 1);

            if ((nMethod == 0) || (nMethod > pPE->cli.pRows[MDT_MethodDef])) {
                continue;
            }

            nNameIndex = md_methoddef_name(pPE, nMethod - 1);
        } else {
            nNameIndex = md_methoddef_name(pPE, record.nMethodList + (cd_u32)j - 1);
        }

        pName = md_string(pPE, nNameIndex);
        bMatch = (x_strcmp(pMethodName, pName) == 0) ? 1 : 0;
        cd_free(pName);

        if (bMatch) {
            return 1;
        }
    }

    return 0;
}

int xpe_net_field_present(XPE *pPE, const char *pNamespace, const char *pTypeName, const char *pFieldName)
{
    MDTypeDef record;
    cd_i64 nRow = 0;
    cd_u32 nTypeCount = 0;
    cd_i64 nFieldCount = 0;
    cd_i64 j = 0;

    if (!pPE->cli.bValid) {
        return 0;
    }

    nTypeCount = pPE->cli.pRows[MDT_TypeDef];
    nRow = md_find_typedef(pPE, pNamespace, pTypeName, &record);

    if (nRow == -1) {
        return 0;
    }

    if ((cd_u32)nRow < nTypeCount - 1) {
        MDTypeDef next;

        if (md_typedef(pPE, (cd_u32)nRow + 1, &next)) {
            nFieldCount = (cd_i64)next.nFieldList - (cd_i64)record.nFieldList;
        }
    } else {
        nFieldCount = md_row_count(pPE->cli.pRows[MDT_Field]) - (cd_i64)record.nFieldList;
    }

    for (j = 0; j < nFieldCount; j++) {
        char *pName = NULL;
        int bMatch = 0;

        if (record.nFieldList == 0) {
            break;
        }

        pName = md_string(pPE, md_field_name(pPE, record.nFieldList + (cd_u32)j - 1));
        bMatch = (x_strcmp(pFieldName, pName) == 0) ? 1 : 0;
        cd_free(pName);

        if (bMatch) {
            return 1;
        }
    }

    return 0;
}

int xpe_net_global_cctor_present(XPE *pPE)
{
    return xpe_net_method_present(pPE, "", "<Module>", ".cctor");
}

char *xpe_net_module_name(XPE *pPE)
{
    XPECli *pCli = &pPE->cli;
    cd_i64 nOffset = 0;

    if (!pCli->bValid) {
        return cd_strdup("");
    }

    nOffset = md_row_offset(pPE, MDT_Module, 0);

    if (nOffset == -1) {
        return cd_strdup("");
    }

    return md_string(pPE, md_index(pPE, nOffset + 2, pCli->nStringIndexSize));
}

char *xpe_net_assembly_name(XPE *pPE)
{
    XPECli *pCli = &pPE->cli;
    cd_i64 nOffset = 0;

    if (!pCli->bValid) {
        return cd_strdup("");
    }

    nOffset = md_row_offset(pPE, MDT_Assembly, 0);

    if (nOffset == -1) {
        return cd_strdup("");
    }

    /* HashAlgId(4) Major(2) Minor(2) Build(2) Revision(2) Flags(4) PublicKey */
    nOffset += 4 + 2 + 2 + 2 + 2 + 4 + pCli->nBlobIndexSize;

    return md_string(pPE, md_index(pPE, nOffset, pCli->nStringIndexSize));
}

static void parse_net(XPE *pPE)
{
    cd_i64 nCliOffset = xpe_rva_to_offset(pPE, pPE->pDirRVA[XPE_DIR_COMHEADER]);
    cd_u32 nMetaRVA = 0;
    cd_u32 nMetaSize = 0;
    cd_i64 nMetaOffset = 0;
    cd_u32 nVersionLength = 0;
    cd_u16 nStreams = 0;
    cd_i64 nStreamOffset = 0;
    cd_u16 i = 0;
    cd_i64 nStringsOffset = -1;
    cd_i64 nStringsSize = 0;
    cd_i64 nUSOffset = -1;
    cd_i64 nUSSize = 0;

    if ((nCliOffset == -1) || (pPE->pDirSize[XPE_DIR_COMHEADER] < 72)) {
        return;
    }

    nMetaRVA = xx_io_get_u32(pPE->pFile->pDevice, nCliOffset + 8, false);
    nMetaSize = xx_io_get_u32(pPE->pFile->pDevice, nCliOffset + 12, false);
    nMetaOffset = xpe_rva_to_offset(pPE, nMetaRVA);

    if ((nMetaOffset == -1) || (nMetaSize == 0)) {
        return;
    }

    if (xx_io_get_u32(pPE->pFile->pDevice, nMetaOffset, false) != 0x424A5342) {
        return;
    }

    pPE->bIsNet = 1;
    pPE->cli.bValid = 1;
    pPE->cli.nMetaOffset = nMetaOffset;
    pPE->cli.nEntryPointRVA = xx_io_get_u32(pPE->pFile->pDevice, nCliOffset + 20, false);

    nVersionLength = xx_io_get_u32(pPE->pFile->pDevice, nMetaOffset + 12, false);

    if (nVersionLength > 256) {
        nVersionLength = 256;
    }

    pPE->pNetVersion = die_ansi_string(pPE->pFile, nMetaOffset + 16, nVersionLength);

    nStreamOffset = nMetaOffset + 16 + ALIGN_UP((cd_i64)nVersionLength, 4);
    nStreams = xx_io_get_u16(pPE->pFile->pDevice, nStreamOffset + 2, false);
    nStreamOffset += 4;

    if (nStreams > 32) {
        nStreams = 32;
    }

    for (i = 0; i < nStreams; i++) {
        cd_u32 nOffset = xx_io_get_u32(pPE->pFile->pDevice, nStreamOffset, false);
        cd_u32 nSize = xx_io_get_u32(pPE->pFile->pDevice, nStreamOffset + 4, false);
        char *pName = die_ansi_string(pPE->pFile, nStreamOffset + 8, 64);
        size_t nNameSize = x_strlen(pName);
        cd_i64 nStreamStart = nMetaOffset + (cd_i64)nOffset;
        cd_i64 nStreamSize = (cd_i64)nSize;

        /* A malformed file can point a stream outside the image; clamp it the
         * way XCLIAssembly::getCliInfo does, so the heap walks below stay
         * inside the file.                                                   */
        if ((nStreamStart < 0) || (nStreamStart > pPE->pFile->nSize)) {
            nStreamStart = 0;
            nStreamSize = 0;
        } else if (nStreamStart + nStreamSize > pPE->pFile->nSize) {
            nStreamSize = pPE->pFile->nSize - nStreamStart;
        }

        if ((x_strcmp(pName, "#~") == 0) || (x_strcmp(pName, "#-") == 0)) {
            pPE->cli.nTablesOffset = nStreamStart;
            pPE->cli.nTablesSize = nStreamSize;
        } else if (x_strcmp(pName, "#Strings") == 0) {
            if (nStringsOffset == -1) {
                nStringsOffset = nStreamStart;
                nStringsSize = nStreamSize;
                pPE->cli.nStringsOffset = nStringsOffset;
                pPE->cli.nStringsSize = nStreamSize;
            }
        } else if (x_strcmp(pName, "#US") == 0) {
            if (nUSOffset == -1) {
                nUSOffset = nStreamStart;
                nUSSize = nStreamSize;
                pPE->cli.nUSOffset = nUSOffset;
                pPE->cli.nUSSize = nStreamSize;
            }
        } else if (x_strcmp(pName, "#Blob") == 0) {
            pPE->cli.nBlobOffset = nStreamStart;
            pPE->cli.nBlobSize = nStreamSize;
        } else if (x_strcmp(pName, "#GUID") == 0) {
            pPE->cli.nGuidOffset = nStreamStart;
            pPE->cli.nGuidSize = nStreamSize;
        }

        cd_free(pName);
        nStreamOffset += 8 + (cd_i64)ALIGN_UP((cd_i64)(nNameSize + 1), 4);
    }

    parse_net_tables(pPE);

    if ((nStringsOffset != -1) && nStringsSize) {
        CDVec vec;
        cd_i64 nPos = 0;

        cdvec_init(&vec);

        while ((nPos < nStringsSize) && (vec.nSize < 100000)) {
            char *pString = die_ansi_string(pPE->pFile, nStringsOffset + nPos, nStringsSize - nPos);
            size_t nSize = x_strlen(pString);

            if (nSize) {
                cdvec_push(&vec, pString);
            } else {
                cd_free(pString);
            }

            nPos += (cd_i64)nSize + 1;
        }

        pPE->nNetAnsiCount = (int)vec.nSize;
        pPE->ppNetAnsiStrings = (char **)cd_malloc((vec.nSize ? vec.nSize : 1) * sizeof(char *));

        if (vec.nSize) {
            x_memcpy(pPE->ppNetAnsiStrings, vec.ppData, vec.nSize * sizeof(char *));
        }

        cdvec_free(&vec);
    }

    if ((nUSOffset != -1) && nUSSize) {
        CDVec vec;
        cd_i64 nPos = 0;

        cdvec_init(&vec);

        while ((nPos < nUSSize) && (vec.nSize < 100000)) {
            int nBytes = 0;
            cd_u32 nLength = read_compressed_uint(pPE->pFile, nUSOffset + nPos, &nBytes);

            nPos += nBytes;

            if ((nLength == 0) || (nLength > nUSSize)) {
                if (nLength == 0) {
                    continue;
                }

                break;
            }

            {
                CDBuf buf;
                cd_u32 j = 0;

                cdbuf_init(&buf);

                for (j = 0; j + 1 < nLength; j += 2) {
                    cd_u16 nChar = xx_io_get_u16(pPE->pFile->pDevice, nUSOffset + nPos + j, false);

                    if (nChar < 0x80) {
                        cdbuf_append_ch(&buf, (char)nChar);
                    } else if (nChar < 0x800) {
                        cdbuf_append_ch(&buf, (char)(0xC0 | (nChar >> 6)));
                        cdbuf_append_ch(&buf, (char)(0x80 | (nChar & 0x3F)));
                    } else {
                        cdbuf_append_ch(&buf, (char)(0xE0 | (nChar >> 12)));
                        cdbuf_append_ch(&buf, (char)(0x80 | ((nChar >> 6) & 0x3F)));
                        cdbuf_append_ch(&buf, (char)(0x80 | (nChar & 0x3F)));
                    }
                }

                cdvec_push(&vec, cdbuf_detach(&buf, NULL));
            }

            nPos += nLength;
        }

        pPE->nNetUnicodeCount = (int)vec.nSize;
        pPE->ppNetUnicodeStrings = (char **)cd_malloc((vec.nSize ? vec.nSize : 1) * sizeof(char *));

        if (vec.nSize) {
            x_memcpy(pPE->ppNetUnicodeStrings, vec.ppData, vec.nSize * sizeof(char *));
        }

        cdvec_free(&vec);
    }
}

/* ------------------------------------------------------------------ main  */

int xpe_parse(XPE *pPE, DieFile *pFile)
{
    cd_i64 nOptional = 0;
    int i = 0;

    x_memset(pPE, 0, sizeof(*pPE));
    pPE->pFile = pFile;
    pPE->nOverlayOffset = -1;

    if (pFile->nSize < 0x40) {
        return 0;
    }

    if ((xx_io_get_u8(pFile->pDevice, 0) != 'M') || (xx_io_get_u8(pFile->pDevice, 1) != 'Z')) {
        return 0;
    }

    pPE->nLfanew = (cd_i64)xx_io_get_u32(pFile->pDevice, 0x3C, false);

    if ((pPE->nLfanew <= 0) || (pPE->nLfanew + 24 > pFile->nSize)) {
        return 0;
    }

    if (xx_io_get_u32(pFile->pDevice, pPE->nLfanew, false) != 0x00004550) { /* "PE\0\0" */
        return 0;
    }

    pPE->nMachine = xx_io_get_u16(pFile->pDevice, pPE->nLfanew + 4, false);
    pPE->nNumberOfSections = xx_io_get_u16(pFile->pDevice, pPE->nLfanew + 6, false);
    pPE->nTimeDateStamp = xx_io_get_u32(pFile->pDevice, pPE->nLfanew + 8, false);
    pPE->nPointerToSymbolTable = xx_io_get_u32(pFile->pDevice, pPE->nLfanew + 12, false);
    pPE->nNumberOfSymbols = xx_io_get_u32(pFile->pDevice, pPE->nLfanew + 16, false);
    pPE->nSizeOfOptionalHeader = xx_io_get_u16(pFile->pDevice, pPE->nLfanew + 20, false);
    pPE->nCharacteristics = xx_io_get_u16(pFile->pDevice, pPE->nLfanew + 22, false);

    nOptional = pPE->nLfanew + 24;
    pPE->nMagic = xx_io_get_u16(pFile->pDevice, nOptional, false);

    if (pPE->nMagic == 0x20B) {
        pPE->bIs64 = 1;
    } else if (pPE->nMagic == 0x10B) {
        pPE->bIs64 = 0;
    } else {
        return 0;
    }

    pPE->nMajorLinkerVersion = xx_io_get_u8(pFile->pDevice, nOptional + 2);
    pPE->nMinorLinkerVersion = xx_io_get_u8(pFile->pDevice, nOptional + 3);
    pPE->nSizeOfCode = xx_io_get_u32(pFile->pDevice, nOptional + 4, false);
    pPE->nSizeOfInitializedData = xx_io_get_u32(pFile->pDevice, nOptional + 8, false);
    pPE->nSizeOfUninitializedData = xx_io_get_u32(pFile->pDevice, nOptional + 12, false);
    pPE->nAddressOfEntryPoint = xx_io_get_u32(pFile->pDevice, nOptional + 16, false);
    pPE->nBaseOfCode = xx_io_get_u32(pFile->pDevice, nOptional + 20, false);

    if (pPE->bIs64) {
        pPE->nImageBase = xx_io_get_u64(pFile->pDevice, nOptional + 24, false);
        pPE->nSectionAlignment = xx_io_get_u32(pFile->pDevice, nOptional + 32, false);
        pPE->nFileAlignment = xx_io_get_u32(pFile->pDevice, nOptional + 36, false);
        pPE->nMajorOperatingSystemVersion = xx_io_get_u16(pFile->pDevice, nOptional + 40, false);
        pPE->nMinorOperatingSystemVersion = xx_io_get_u16(pFile->pDevice, nOptional + 42, false);
        pPE->nMajorImageVersion = xx_io_get_u16(pFile->pDevice, nOptional + 44, false);
        pPE->nMinorImageVersion = xx_io_get_u16(pFile->pDevice, nOptional + 46, false);
        pPE->nMajorSubsystemVersion = xx_io_get_u16(pFile->pDevice, nOptional + 48, false);
        pPE->nMinorSubsystemVersion = xx_io_get_u16(pFile->pDevice, nOptional + 50, false);
        pPE->nWin32VersionValue = xx_io_get_u32(pFile->pDevice, nOptional + 52, false);
        pPE->nSizeOfImage = xx_io_get_u32(pFile->pDevice, nOptional + 56, false);
        pPE->nSizeOfHeaders = xx_io_get_u32(pFile->pDevice, nOptional + 60, false);
        pPE->nCheckSum = xx_io_get_u32(pFile->pDevice, nOptional + 64, false);
        pPE->nSubsystem = xx_io_get_u16(pFile->pDevice, nOptional + 68, false);
        pPE->nDllCharacteristics = xx_io_get_u16(pFile->pDevice, nOptional + 70, false);
        pPE->nSizeOfStackReserve = xx_io_get_u64(pFile->pDevice, nOptional + 72, false);
        pPE->nSizeOfStackCommit = xx_io_get_u64(pFile->pDevice, nOptional + 80, false);
        pPE->nSizeOfHeapReserve = xx_io_get_u64(pFile->pDevice, nOptional + 88, false);
        pPE->nSizeOfHeapCommit = xx_io_get_u64(pFile->pDevice, nOptional + 96, false);
        pPE->nLoaderFlags = xx_io_get_u32(pFile->pDevice, nOptional + 104, false);
        pPE->nNumberOfRvaAndSizes = xx_io_get_u32(pFile->pDevice, nOptional + 108, false);

        for (i = 0; i < 16; i++) {
            pPE->pDirRVA[i] = xx_io_get_u32(pFile->pDevice, nOptional + 112 + i * 8, false);
            pPE->pDirSize[i] = xx_io_get_u32(pFile->pDevice, nOptional + 112 + i * 8 + 4, false);
        }
    } else {
        pPE->nBaseOfData = xx_io_get_u32(pFile->pDevice, nOptional + 24, false);
        pPE->nImageBase = xx_io_get_u32(pFile->pDevice, nOptional + 28, false);
        pPE->nSectionAlignment = xx_io_get_u32(pFile->pDevice, nOptional + 32, false);
        pPE->nFileAlignment = xx_io_get_u32(pFile->pDevice, nOptional + 36, false);
        pPE->nMajorOperatingSystemVersion = xx_io_get_u16(pFile->pDevice, nOptional + 40, false);
        pPE->nMinorOperatingSystemVersion = xx_io_get_u16(pFile->pDevice, nOptional + 42, false);
        pPE->nMajorImageVersion = xx_io_get_u16(pFile->pDevice, nOptional + 44, false);
        pPE->nMinorImageVersion = xx_io_get_u16(pFile->pDevice, nOptional + 46, false);
        pPE->nMajorSubsystemVersion = xx_io_get_u16(pFile->pDevice, nOptional + 48, false);
        pPE->nMinorSubsystemVersion = xx_io_get_u16(pFile->pDevice, nOptional + 50, false);
        pPE->nWin32VersionValue = xx_io_get_u32(pFile->pDevice, nOptional + 52, false);
        pPE->nSizeOfImage = xx_io_get_u32(pFile->pDevice, nOptional + 56, false);
        pPE->nSizeOfHeaders = xx_io_get_u32(pFile->pDevice, nOptional + 60, false);
        pPE->nCheckSum = xx_io_get_u32(pFile->pDevice, nOptional + 64, false);
        pPE->nSubsystem = xx_io_get_u16(pFile->pDevice, nOptional + 68, false);
        pPE->nDllCharacteristics = xx_io_get_u16(pFile->pDevice, nOptional + 70, false);
        pPE->nSizeOfStackReserve = xx_io_get_u32(pFile->pDevice, nOptional + 72, false);
        pPE->nSizeOfStackCommit = xx_io_get_u32(pFile->pDevice, nOptional + 76, false);
        pPE->nSizeOfHeapReserve = xx_io_get_u32(pFile->pDevice, nOptional + 80, false);
        pPE->nSizeOfHeapCommit = xx_io_get_u32(pFile->pDevice, nOptional + 84, false);
        pPE->nLoaderFlags = xx_io_get_u32(pFile->pDevice, nOptional + 88, false);
        pPE->nNumberOfRvaAndSizes = xx_io_get_u32(pFile->pDevice, nOptional + 92, false);

        for (i = 0; i < 16; i++) {
            pPE->pDirRVA[i] = xx_io_get_u32(pFile->pDevice, nOptional + 96 + i * 8, false);
            pPE->pDirSize[i] = xx_io_get_u32(pFile->pDevice, nOptional + 96 + i * 8 + 4, false);
        }
    }

    if (pPE->nSectionAlignment == 0) {
        pPE->nSectionAlignment = 0x1000;
    }

    if (pPE->nFileAlignment == 0) {
        pPE->nFileAlignment = 0x200;
    }

    pPE->bValid = 1;

    parse_sections(pPE);
    build_memory_map(pPE);

    pPE->nEntryPointAddress = pPE->nImageBase + pPE->nAddressOfEntryPoint;
    pPE->nEntryPointOffset = pPE->nAddressOfEntryPoint ? xpe_rva_to_offset(pPE, pPE->nAddressOfEntryPoint) : -1;

    if ((pPE->nAddressOfEntryPoint == 0) && (pPE->nSectionCount == 0)) {
        pPE->nEntryPointOffset = -1;
    }

    parse_imports(pPE);
    parse_exports(pPE);
    parse_resources(pPE);
    parse_version(pPE);
    parse_manifest(pPE);
    parse_debug(pPE);
    parse_rich(pPE);
    parse_net(pPE);

    return 1;
}

void xpe_free(XPE *pPE)
{
    int i = 0;
    int j = 0;

    for (i = 0; i < pPE->nImportCount; i++) {
        for (j = 0; j < pPE->pImports[i].nFunctionCount; j++) {
            cd_free(pPE->pImports[i].ppFunctions[j]);
        }

        cd_free(pPE->pImports[i].ppFunctions);
        cd_free(pPE->pImports[i].pName);
    }

    cd_free(pPE->pImports);

    for (i = 0; i < pPE->nExportCount; i++) {
        cd_free(pPE->ppExportFunctions[i]);
    }

    cd_free(pPE->ppExportFunctions);

    for (i = 0; i < pPE->nResourceCount; i++) {
        cd_free(pPE->pResources[i].pName);
        cd_free(pPE->pResources[i].pTypeName);
    }

    cd_free(pPE->pResources);

    for (i = 0; i < pPE->nVersionCount; i++) {
        cd_free(pPE->pVersionRecords[i].pKey);
        cd_free(pPE->pVersionRecords[i].pValue);
    }

    cd_free(pPE->pVersionRecords);

    for (i = 0; i < pPE->nNetAnsiCount; i++) {
        cd_free(pPE->ppNetAnsiStrings[i]);
    }

    cd_free(pPE->ppNetAnsiStrings);

    for (i = 0; i < pPE->nNetUnicodeCount; i++) {
        cd_free(pPE->ppNetUnicodeStrings[i]);
    }

    cd_free(pPE->ppNetUnicodeStrings);

    cd_free(pPE->pDebugRecords);
    cd_free(pPE->pRichRecords);
    cd_free(pPE->pManifest);
    cd_free(pPE->pNetVersion);
    cd_free(pPE->pSections);
    xx_memory_map_cleanup(&pPE->map);
    x_memset(pPE, 0, sizeof(*pPE));
}
