#include "xxfclib/formats/pe/xx_pe_inspect.h"
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


#define ALIGN_UP(v, a) (((a) == 0) ? (v) : ((((v) + (a) - 1) / (a)) * (a)))

static const struct {
    uint32_t nType;
    const char *pName;
} g_debugTypes[] = {{0, "UNKNOWN"},  {1, "COFF"},       {2, "CODEVIEW"},   {3, "FPO"},        {4, "MISC"},   {5, "EXCEPTION"},
                    {6, "FIXUP"},    {7, "OMAP_TO_SRC"}, {8, "OMAP_FROM_SRC"}, {9, "BORLAND"}, {10, "RESERVED10"}, {11, "CLSID"},
                    {12, "VC_FEATURE"}, {13, "POGO"},   {14, "ILTCG"},     {15, "MPX"},       {16, "REPRO"}, {20, "EX_DLLCHARACTERISTICS"}};

const char *xx_pe_inspect_debug_type_name(uint32_t nType)
{
    size_t i = 0;

    for (i = 0; i < sizeof(g_debugTypes) / sizeof(g_debugTypes[0]); i++) {
        if (g_debugTypes[i].nType == nType) {
            return g_debugTypes[i].pName;
        }
    }

    return "";
}

int xx_pe_inspect_section_number_by_rva(xx_pe_inspection *pPE, uint32_t nRVA)
{
    if (!pPE) return -1;
    int i = 0;

    /* The arithmetic is done in int64_t, the way build_memory_map does it: a
     * VirtualAddress or VirtualSize near 4 GB wraps in 32 bits.             */
    for (i = 0; i < pPE->nSectionCount; i++) {
        int64_t nStart = pPE->pSections[i].nVirtualAddress;
        int64_t nSize = pPE->pSections[i].nVirtualSize;

        if (nSize == 0) {
            nSize = pPE->pSections[i].nSizeOfRawData;
        }

        nSize = ALIGN_UP(nSize, (int64_t)pPE->nSectionAlignment);

        if (((int64_t)nRVA >= nStart) && ((int64_t)nRVA < nStart + nSize)) {
            return i;
        }
    }

    return -1;
}

static uint64_t inspect_rva_address(const xx_pe_inspection *pPE, uint64_t rva)
{
    if (!pPE || pPE->map.module_address == XX_INVALID_ADDRESS ||
        rva >= XX_INVALID_ADDRESS - pPE->map.module_address) return XX_INVALID_ADDRESS;
    return pPE->map.module_address + rva;
}

int64_t xx_pe_inspect_rva_to_offset(xx_pe_inspection *pPE, uint32_t nRVA)
{
    if (!pPE) return -1;
    if (nRVA == 0) {
        return -1;
    }

    return xx_memory_map_address_to_offset_ex(&pPE->map, inspect_rva_address(pPE, nRVA), XX_MEMORY_MAP_LOOKUP_FIRST_MATCH);
}

/* ------------------------------------------------------------- sections  */

static void parse_sections(xx_pe_inspection *pPE, const xx_pe *reader)
{
    uint16_t i;
    pPE->nSectionCount = reader ? reader->number_of_sections : pPE->nNumberOfSections;
    if (pPE->nSectionCount > 4096) pPE->nSectionCount = 4096;
    if (!pPE->nSectionCount) return;
    pPE->pSections = (xx_pe_inspect_section *)xx_mem_calloc((size_t)pPE->nSectionCount, sizeof(*pPE->pSections));
    if (!pPE->pSections) { pPE->pInput->failed = true; pPE->nSectionCount = 0; return; }
    for (i = 0; i < (uint16_t)pPE->nSectionCount; ++i) {
        xx_pe_section local = {0};
        const xx_pe_section *section;
        if (reader) section = &reader->sections[i];
        else {
            int64_t offset = pPE->nLfanew + 24 + pPE->nSizeOfOptionalHeader + (int64_t)i * 40;
            xx_exec_read(pPE->pInput, offset, local.name, 8);
            local.virtual_size = xx_exec_u32(pPE->pInput, offset + 8, false);
            local.virtual_address = xx_exec_u32(pPE->pInput, offset + 12, false);
            local.raw_size = xx_exec_u32(pPE->pInput, offset + 16, false);
            local.raw_offset = xx_exec_u32(pPE->pInput, offset + 20, false);
            local.characteristics = xx_exec_u32(pPE->pInput, offset + 36, false);
            section = &local;
        }
        xx_rt_memcpy(pPE->pSections[i].sName, section->name, sizeof(section->name));
        pPE->pSections[i].nVirtualSize = section->virtual_size;
        pPE->pSections[i].nVirtualAddress = section->virtual_address;
        pPE->pSections[i].nSizeOfRawData = section->raw_size;
        pPE->pSections[i].nPointerToRawData = section->raw_offset;
        pPE->pSections[i].nCharacteristics = section->characteristics;
    }

}

static bool build_memory_map(xx_pe_inspection *pPE, const xx_pe *reader)
{
    const xx_memory_map *source;
    size_t i;
    int section;
    int64_t base;
    if (!reader || pPE->pInput->failed) return false;
    base = reader->format.base_address;
    source = xx_format_get_memory_map((Abstractformat *)&reader->format,
                                     XX_MEMORY_MAP_MODE_UNKNOWN, pPE->pInput->pd);
    if (!source) return false;
    /* Own a copy of the reader's map with offsets relative to the borrowed
     * input view. This keeps disk images, memory dumps and overlay extents
     * identical across the reader and inspection APIs. */
    pPE->map = *source;
    pPE->map.records = NULL;
    pPE->map.record_count = pPE->map.record_capacity = 0;
    pPE->map.binary_offset = 0;
    pPE->map.start_load_offset = source->start_load_offset >= base
                                    ? source->start_load_offset - base : -1;
    for (section = 0; section < pPE->nSectionCount; ++section) {
        pPE->pSections[section].nMappedOffset = pPE->pInput->size;
        pPE->pSections[section].nMappedSize = 0;
    }
    for (i = 0; i < source->record_count; ++i) {
        xx_memory_record record = source->records[i];
        if (!record.is_virtual) {
            if (record.offset < base) return false;
            record.offset -= base;
        }
        if (!xx_memory_map_add_record(&pPE->map, &record)) return false;
        if (!record.is_virtual && record.file_part == XX_FILE_PART_SECTION &&
            record.file_part_number > 0 && record.file_part_number <= pPE->nSectionCount) {
            xx_pe_inspect_section *item = &pPE->pSections[record.file_part_number - 1];
            item->nMappedOffset = record.offset;
            item->nMappedSize = record.size;
        }
    }
    pPE->nOverlayOffset = reader->format.overlay_offset >= base
                             ? reader->format.overlay_offset - base : -1;
    pPE->nOverlaySize = reader->format.overlay_size;
    return xx_memory_map_finalize(&pPE->map);
}

/* -------------------------------------------------------------- imports  */

static void parse_imports(xx_pe_inspection *pPE)
{
    uint32_t nImportRVA = pPE->pDirRVA[XX_PE_INSPECT_DIR_IMPORT];
    int64_t nOffset = nImportRVA ? xx_memory_map_address_to_offset(&pPE->map,
        inspect_rva_address(pPE, nImportRVA)) : -1;
    int bPartialFirst = nImportRVA && xx_memory_map_address_to_offset(&pPE->map,
        inspect_rva_address(pPE, (uint64_t)nImportRVA + 18)) == -1;
    int nCount = 0;
    int i = 0;
    int nTotalPositions = 0;
    xx_buf_t hashBuf;

    if (nOffset == -1) {
        return;
    }

    xx_buf_init(&hashBuf);

    for (nCount = 0; nCount < 4096 && !xx_pd_is_stopped(pPE->pInput->pd); nCount++) {
        int64_t nBase = xx_memory_map_address_to_offset(&pPE->map,
            inspect_rva_address(pPE, (uint64_t)nImportRVA + (uint64_t)nCount * 20));
        if (nBase < 0 || nBase > pPE->pInput->size - 20) break;
        uint32_t nOriginalFirstThunk = xx_exec_u32(pPE->pInput, nBase, false);
        uint32_t nName = xx_exec_u32(pPE->pInput, nBase + 12, false);
        uint64_t nDescriptorAddress = inspect_rva_address(pPE, (uint64_t)nImportRVA + (uint64_t)nCount * 20);
        int64_t nNameOffset;
        char *pName;

        if ((nOriginalFirstThunk == 0) && (nName == 0)) {
            break;
        }

        /* Native imports require a contiguous mapped descriptor, except for
         * the first partially mapped descriptor used by some Upack stubs. */
        if (nBase > pPE->pInput->size - 20 ||
            xx_memory_map_address_to_offset(&pPE->map, nDescriptorAddress) != nBase ||
            (!(nCount == 0 && bPartialFirst) &&
             xx_memory_map_address_to_offset(&pPE->map, nDescriptorAddress + 19) != nBase + 19)) {
            break;
        }
        nNameOffset = xx_memory_map_address_to_offset(&pPE->map, inspect_rva_address(pPE, nName));
        if (nNameOffset == -1) {
            break;
        }
        pName = xx_exec_string(pPE->pInput, nNameOffset, 2048);
        if (!pName || !pName[0]) {
            xx_mem_free(pName);
            break;
        }
        xx_mem_free(pName);
    }

    if (nCount == 0) {
        xx_buf_free(&hashBuf);

        return;
    }

    pPE->pImports = (xx_pe_inspect_import *)xx_mem_calloc((size_t)nCount, sizeof(xx_pe_inspect_import));
    if (!pPE->pImports) { pPE->pInput->failed = true; xx_buf_free(&hashBuf); return; }
    pPE->nImportCount = nCount;

    for (i = 0; i < nCount && !pPE->pInput->failed && !xx_pd_is_stopped(pPE->pInput->pd); i++) {
        int64_t nBase = xx_memory_map_address_to_offset(&pPE->map,
            inspect_rva_address(pPE, (uint64_t)nImportRVA + (uint64_t)i * 20));
        if (nBase < 0) break;
        uint32_t nOriginalFirstThunk = xx_exec_u32(pPE->pInput, nBase, false);
        uint32_t nName = xx_exec_u32(pPE->pInput, nBase + 12, false);
        uint32_t nFirstThunk = xx_exec_u32(pPE->pInput, nBase + 16, false);
        int64_t nNameOffset = xx_memory_map_address_to_offset(&pPE->map, inspect_rva_address(pPE, nName));
        if (bPartialFirst) nFirstThunk &= 0xFFFF;
        uint32_t nThunkRVA = nOriginalFirstThunk ? nOriginalFirstThunk : nFirstThunk;
        int64_t nThunkOffset = xx_memory_map_address_to_offset(&pPE->map, inspect_rva_address(pPE, nThunkRVA));
        xx_list_s vecFunctions;
        int j = 0;
        int nRemaining = XX_PE_INSPECT_MAX_IMPORT_POSITIONS - nTotalPositions;
        xx_buf_t posBuf;

        /* xx_pe_inspection::getImports budgets the positions per library and over the whole
         * import table, so a descriptor list that shares one thunk array
         * cannot multiply out.                                              */
        if (nRemaining > XX_PE_INSPECT_MAX_POSITIONS_PER_LIBRARY) {
            nRemaining = XX_PE_INSPECT_MAX_POSITIONS_PER_LIBRARY;
        }

        xx_list_init(&vecFunctions, sizeof(void *), NULL);
        xx_buf_init(&posBuf);

        pPE->pImports[i].pName = xx_exec_string(pPE->pInput, nNameOffset, 2048);

        if (nThunkOffset != -1) {
            for (j = 0; j < nRemaining && !xx_pd_is_stopped(pPE->pInput->pd); j++) {
                uint64_t nThunk = 0;
                char *pFunctionName = NULL;
                int64_t nWidth = pPE->bIs64 ? 8 : 4;
                uint64_t nAddress = inspect_rva_address(pPE, (uint64_t)nThunkRVA + (uint64_t)j * nWidth);
                int64_t nCurrent = xx_memory_map_address_to_offset(&pPE->map, nAddress);

                if (nCurrent < 0 || nCurrent > pPE->pInput->size - nWidth ||
                    xx_memory_map_address_to_offset(&pPE->map, nAddress) != nCurrent ||
                    xx_memory_map_address_to_offset(&pPE->map, nAddress + nWidth - 1) != nCurrent + nWidth - 1) {
                    break;
                }

                if (pPE->bIs64) {
                    nThunk = xx_exec_u64(pPE->pInput, nCurrent, false);
                } else {
                    nThunk = xx_exec_u32(pPE->pInput, nCurrent, false);
                }

                if (nThunk == 0) {
                    break;
                }

                if ((pPE->bIs64 && (nThunk & 0x8000000000000000ull)) || ((!pPE->bIs64) && (nThunk & 0x80000000u))) {
                    char sBuf[32];

                    xx_rt_snprintf(sBuf, sizeof(sBuf), "%llu", (unsigned long long)(nThunk & (pPE->bIs64 ? 0x7FFFFFFFFFFFFFFFull : 0x7FFFFFFFull)));
                    pFunctionName = xx_str_create(sBuf);
                } else {
                    int64_t nHintOffset = xx_memory_map_address_to_offset(&pPE->map, inspect_rva_address(pPE, nThunk));

                    if (nHintOffset < 0 || nHintOffset > pPE->pInput->size - 3) {
                        break;
                    }
                    pFunctionName = xx_exec_string(pPE->pInput, nHintOffset + 2, 2048);
                    if (!pFunctionName || !pFunctionName[0]) {
                        xx_mem_free(pFunctionName);
                        break;
                    }
                }

                if (!pFunctionName || !xx_list_append(&vecFunctions, &pFunctionName)) { xx_mem_free(pFunctionName); pPE->pInput->failed = true; break; }
                xx_buf_append_str(&posBuf, pFunctionName);
                xx_buf_append_str(&hashBuf, pPE->pImports[i].pName);
                xx_buf_append_str(&hashBuf, pFunctionName);
            }
        }

        pPE->pImports[i].nFunctionCount = (int)vecFunctions.count;
        pPE->pImports[i].ppFunctions = (char **)vecFunctions.data;
        vecFunctions.data = NULL; vecFunctions.count = vecFunctions.capacity = 0;

        pPE->pImports[i].nPositionHash = xx_exec_string_crc32c(posBuf.data ? posBuf.data : "");
        nTotalPositions += pPE->pImports[i].nFunctionCount;

        if (!xx_buf_ok(&posBuf)) pPE->pInput->failed = true;
        xx_buf_free(&posBuf);
        xx_list_cleanup(&vecFunctions);

        if (nTotalPositions >= XX_PE_INSPECT_MAX_IMPORT_POSITIONS) {
            pPE->nImportCount = i + 1;

            break;
        }
    }

    pPE->nImportHash32 = xx_exec_string_crc32c(hashBuf.data ? hashBuf.data : "");

    {
        uint64_t nHash64 = 0;
        int j = 0;

        for (i = 0; i < pPE->nImportCount; i++) {
            for (j = 0; j < pPE->pImports[i].nFunctionCount; j++) {
                xx_buf_t record;

                xx_buf_init(&record);
                xx_buf_append_str(&record, pPE->pImports[i].pName);
                xx_buf_append_char(&record, ' ');
                xx_buf_append_str(&record, pPE->pImports[i].ppFunctions[j]);
                nHash64 += xx_exec_string_crc32c(record.data ? record.data : "");
                xx_buf_free(&record);
            }
        }

        pPE->nImportHash64 = nHash64;
    }

    if (!xx_buf_ok(&hashBuf)) pPE->pInput->failed = true;
    xx_buf_free(&hashBuf);
}

/* -------------------------------------------------------------- exports  */

static void parse_exports(xx_pe_inspection *pPE)
{
    int64_t nOffset = xx_pe_inspect_rva_to_offset(pPE, pPE->pDirRVA[XX_PE_INSPECT_DIR_EXPORT]);
    uint32_t nNumberOfFunctions = 0;
    uint32_t nNumberOfNames = 0;
    uint32_t nAddressOfFunctions = 0;
    uint32_t nAddressOfNames = 0;
    uint32_t nAddressOfOrdinals = 0;
    int64_t nNamesOffset = 0;
    int64_t nOrdinalsOffset = 0;
    uint32_t i = 0;

    if (nOffset == -1) {
        return;
    }

    nNumberOfFunctions = xx_exec_u32(pPE->pInput, nOffset + 20, false);
    nNumberOfNames = xx_exec_u32(pPE->pInput, nOffset + 24, false);
    nAddressOfFunctions = xx_exec_u32(pPE->pInput, nOffset + 28, false);
    nAddressOfNames = xx_exec_u32(pPE->pInput, nOffset + 32, false);
    nAddressOfOrdinals = xx_exec_u32(pPE->pInput, nOffset + 36, false);

    /* PE_Script uses getExport(false): every function position is present,
     * including ordinal-only and zero-RVA positions. Names are associated
     * through AddressOfNameOrdinals, not returned as a separate list. */
    if ((nNumberOfFunctions == 0) || (nNumberOfFunctions >= 0xFFFF) || (nNumberOfNames >= 0xFFFF)) {
        return;
    }

    nNamesOffset = xx_memory_map_address_to_offset(&pPE->map, inspect_rva_address(pPE, nAddressOfNames));
    nOrdinalsOffset = xx_memory_map_address_to_offset(&pPE->map, inspect_rva_address(pPE, nAddressOfOrdinals));

    if ((nNamesOffset == -1) || (nOrdinalsOffset == -1) ||
        (xx_memory_map_address_to_offset(&pPE->map, inspect_rva_address(pPE, nAddressOfFunctions)) == -1)) {
        return;
    }

    pPE->ppExportFunctions = (char **)xx_mem_calloc(nNumberOfFunctions, sizeof(char *));
    if (!pPE->ppExportFunctions) { pPE->pInput->failed = true; return; }
    for (i = 0; i < nNumberOfFunctions; i++) {
        pPE->ppExportFunctions[i] = xx_exec_copy_string(pPE->pInput, "");
        if (!pPE->ppExportFunctions[i]) {
            pPE->nExportCount = (int)i;
            pPE->pInput->failed = true;
            return;
        }
    }

    for (i = 0; i < nNumberOfNames; i++) {
        uint16_t nIndex = xx_exec_u16(pPE->pInput, nOrdinalsOffset + i * 2, false);
        uint32_t nNameRVA = xx_exec_u32(pPE->pInput, nNamesOffset + i * 4, false);
        int64_t nNameOffset = xx_memory_map_address_to_offset(&pPE->map, inspect_rva_address(pPE, nNameRVA));

        if (nIndex < nNumberOfFunctions) {
            xx_mem_free(pPE->ppExportFunctions[nIndex]);
            pPE->ppExportFunctions[nIndex] = (nNameOffset != -1) ? xx_exec_string(pPE->pInput, nNameOffset, 2048) : xx_exec_copy_string(pPE->pInput, "");
        }
    }

    pPE->nExportCount = (int)nNumberOfFunctions;
}

/* ------------------------------------------------------------ resources  */

typedef struct {
    uint32_t nId;
    char *pName;
} ResIdName;

static ResIdName resource_id_name(xx_pe_inspection *pPE, int64_t nResourceOffset, uint32_t nName)
{
    ResIdName result;

    result.nId = 0;
    result.pName = NULL;

    if (nName & 0x80000000u) {
        int64_t nStringOffset = nResourceOffset + (int64_t)(nName & 0x7FFFFFFFu);
        uint16_t nLength = xx_exec_u16(pPE->pInput, nStringOffset, false);
        if (nLength > 1024) {
            nLength = 1024;
        }
        result.pName = xx_exec_unicode_units(pPE->pInput, nStringOffset + 2, nLength, 0);
    } else {
        result.nId = nName;
    }

    return result;
}

static void parse_resources(xx_pe_inspection *pPE)
{
    int64_t nResourceOffset = xx_pe_inspect_rva_to_offset(pPE, pPE->pDirRVA[XX_PE_INSPECT_DIR_RESOURCE]);
    xx_list_s vec;
    int i = 0;
    uint16_t nNamed0 = 0;
    uint16_t nId0 = 0;
    int64_t nLevel0 = 0;

    if (nResourceOffset == -1) {
        return;
    }

    if (xx_exec_u32(pPE->pInput, nResourceOffset, false) != 0) {
        return; /* Characteristics must be zero */
    }

    xx_list_init(&vec, sizeof(xx_pe_inspect_resource), NULL);

    nNamed0 = xx_exec_u16(pPE->pInput, nResourceOffset + 12, false);
    nId0 = xx_exec_u16(pPE->pInput, nResourceOffset + 14, false);

    if ((uint32_t)(nNamed0 + nId0) > 1000) {
        xx_list_cleanup(&vec);

        return;
    }

    nLevel0 = nResourceOffset + 16;

    for (i = 0; i < nNamed0 + nId0; i++) {
        uint32_t nName0 = xx_exec_u32(pPE->pInput, nLevel0, false);
        uint32_t nOffsetToDirectory0 = xx_exec_u32(pPE->pInput, nLevel0 + 4, false);
        ResIdName irin0;
        int64_t nDir1 = 0;
        uint16_t nNamed1 = 0;
        uint16_t nId1 = 0;
        int64_t nLevel1 = 0;
        int j = 0;

        if (nOffsetToDirectory0 == 0) {
            break;
        }

        irin0 = resource_id_name(pPE, nResourceOffset, nName0);
        nDir1 = nResourceOffset + (int64_t)(nOffsetToDirectory0 & 0x7FFFFFFFu);

        if (xx_exec_u32(pPE->pInput, nDir1, false) != 0) {
            xx_mem_free(irin0.pName);
            break;
        }

        nNamed1 = xx_exec_u16(pPE->pInput, nDir1 + 12, false);
        nId1 = xx_exec_u16(pPE->pInput, nDir1 + 14, false);
        nLevel1 = nDir1 + 16;

        if ((uint32_t)(nNamed1 + nId1) > 1000) {
            xx_mem_free(irin0.pName);
            nLevel0 += 8;
            continue;
        }

        for (j = 0; j < nNamed1 + nId1; j++) {
            uint32_t nName1 = xx_exec_u32(pPE->pInput, nLevel1, false);
            uint32_t nOffsetToDirectory1 = xx_exec_u32(pPE->pInput, nLevel1 + 4, false);
            ResIdName irin1 = resource_id_name(pPE, nResourceOffset, nName1);
            int64_t nDir2 = nResourceOffset + (int64_t)(nOffsetToDirectory1 & 0x7FFFFFFFu);
            uint16_t nNamed2 = 0;
            uint16_t nId2 = 0;
            int64_t nLevel2 = 0;
            int k = 0;

            if (xx_exec_u32(pPE->pInput, nDir2, false) != 0) {
                xx_mem_free(irin1.pName);
                break;
            }

            nNamed2 = xx_exec_u16(pPE->pInput, nDir2 + 12, false);
            nId2 = xx_exec_u16(pPE->pInput, nDir2 + 14, false);
            nLevel2 = nDir2 + 16;

            if ((uint32_t)(nNamed2 + nId2) > 1000) {
                xx_mem_free(irin1.pName);
                nLevel1 += 8;
                continue;
            }

            for (k = 0; k < nNamed2 + nId2; k++) {
                uint32_t nName2 = xx_exec_u32(pPE->pInput, nLevel2, false);
                uint32_t nOffsetToData = xx_exec_u32(pPE->pInput, nLevel2 + 4, false);
                ResIdName irin2 = resource_id_name(pPE, nResourceOffset, nName2);
                int64_t nDataEntry = nResourceOffset + (int64_t)nOffsetToData;
                xx_pe_inspect_resource resource = {0};
                xx_pe_inspect_resource *pResource = &resource;

                pResource->nTypeId = irin0.nId;
                pResource->pTypeName = irin0.pName ? xx_exec_copy_string(pPE->pInput, irin0.pName) : NULL;
                pResource->nNameId = irin1.nId;
                pResource->pName = irin1.pName ? xx_exec_copy_string(pPE->pInput, irin1.pName) : NULL;
                pResource->nLangId = irin2.nId;
                /* A payload RVA of zero maps to the image header. Unlike an
                 * absent directory, it must not be turned into -1. */
                pResource->nOffset = xx_memory_map_address_to_offset(&pPE->map,
                    inspect_rva_address(pPE, xx_exec_u32(pPE->pInput, nDataEntry, false)));
                pResource->nSize = xx_exec_u32(pPE->pInput, nDataEntry + 4, false);

                if (pPE->pInput->failed || !xx_list_append(&vec, pResource)) {
                    xx_mem_free(resource.pName); xx_mem_free(resource.pTypeName);
                    pPE->pInput->failed = true;
                }
                xx_mem_free(irin2.pName);

                if (vec.count >= 10000 || pPE->pInput->failed || xx_pd_is_stopped(pPE->pInput->pd)) {
                    break;
                }

                nLevel2 += 8;
            }

            xx_mem_free(irin1.pName);

            if (vec.count >= 10000 || pPE->pInput->failed || xx_pd_is_stopped(pPE->pInput->pd)) {
                break;
            }

            nLevel1 += 8;
        }

        xx_mem_free(irin0.pName);

        if (vec.count >= 10000 || pPE->pInput->failed || xx_pd_is_stopped(pPE->pInput->pd)) {
            break;
        }

        nLevel0 += 8;
    }

    pPE->nResourceCount = (int)vec.count;

    pPE->pResources = (xx_pe_inspect_resource *)vec.data;
    vec.data = NULL; vec.count = vec.capacity = 0;

    xx_list_cleanup(&vec);
}

static xx_pe_inspect_resource *find_resource_by_type(xx_pe_inspection *pPE, uint32_t nTypeId)
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

static void version_add(xx_pe_inspection *pPE, const char *pKey, const char *pValue)
{
    xx_pe_inspect_version_record record;
    xx_pe_inspect_version_record *records;
    record.pKey = xx_exec_copy_string(pPE->pInput, pKey);
    record.pValue = xx_exec_copy_string(pPE->pInput, pValue);
    if (!record.pKey || !record.pValue) {
        xx_mem_free(record.pKey); xx_mem_free(record.pValue);
        pPE->pInput->failed = true; return;
    }
    records = (xx_pe_inspect_version_record *)xx_mem_realloc(pPE->pVersionRecords, (size_t)(pPE->nVersionCount + 1) * sizeof(xx_pe_inspect_version_record));
    if (!records) {
        xx_mem_free(record.pKey); xx_mem_free(record.pValue);
        pPE->pInput->failed = true; return;
    }
    pPE->pVersionRecords = records;
    pPE->pVersionRecords[pPE->nVersionCount] = record;
    pPE->nVersionCount++;
}

static uint32_t parse_version_block(xx_pe_inspection *pPE, int64_t nOffset, int64_t nSize, const char *pPrefix, int nLevel)
{
    uint16_t nLength = 0;
    uint16_t nValueLength = 0;
    uint16_t nType = 0;
    char *pTitle = NULL;
    int64_t nTitleUnits = 0;
    int64_t nDelta = 0;
    xx_buf_t prefix;
    uint32_t nResult = 0;

    if (nSize < 6) {
        return 0;
    }

    nLength = xx_exec_u16(pPE->pInput, nOffset, false);
    nValueLength = xx_exec_u16(pPE->pInput, nOffset + 2, false);
    nType = xx_exec_u16(pPE->pInput, nOffset + 4, false);

    (void)nType;

    if ((nLength == 0) || (nLength > nSize)) {
        return 0;
    }

    if (nValueLength >= nLength) {
        return 0;
    }

    /* szKey occupies (units + 1) UTF-16 code units on disk; the UTF-8 length
     * of the converted title is not the same thing for non-ASCII keys.
     * xx_pe_inspection::__getResourcesVersion reads at most 256 units (read_unicodeString's
     * default) and advances by (sTitle.length() + 1) * sizeof(quint16).      */
    pTitle = xx_exec_unicode_n(pPE->pInput, nOffset + 6, 256, 0, &nTitleUnits);
    if (!pTitle) { pPE->pInput->failed = true; return 0; }

    nDelta = 6;
    nDelta += (nTitleUnits + 1) * 2;
    nDelta = ALIGN_UP(nDelta, 4);

    xx_buf_init(&prefix);
    xx_buf_append_str(&prefix, pPrefix);

    if (prefix.size) {
        xx_buf_append_char(&prefix, '.');
    }

    xx_buf_append_str(&prefix, pTitle);

    /* A block whose prefix and title are both empty never allocates, so pData
     * is still NULL here -- a malformed resource reaches this with no key.  */
    if (xx_rt_strcmp(prefix.data ? prefix.data : "", "VS_VERSION_INFO") == 0) {
        if (nValueLength >= 52) {
            pPE->nFileVersionMS = xx_exec_u32(pPE->pInput, nOffset + nDelta + 8, false);
            pPE->nFileVersionLS = xx_exec_u32(pPE->pInput, nOffset + nDelta + 12, false);
        }
    }

    if (nLevel == 3) {
        /* The value is bounded by both wValueLength and what is left of this
         * record - min(wValueLength, (wLength - nDelta) / 2) UTF-16 units,
         * exactly as xx_pe_inspection::__getResourcesVersion computes nValueCharacters.
         * Without that bound a record carrying a short or zero-length value
         * reads on into the bytes of the record that follows it.            */
        int64_t nAvailUnits = ((int64_t)nLength - nDelta) / 2;
        int64_t nValueUnits = (int64_t)nValueLength;
        char *pValue = NULL;

        if (nAvailUnits < 0) {
            nAvailUnits = 0;
        }

        if (nValueUnits > nAvailUnits) {
            nValueUnits = nAvailUnits;
        }

        /* xx_exec_unicode reads 0x10000 units when given a non-positive
         * limit, while read_unicodeString returns an empty string. */
        pValue = (nValueUnits > 0) ? xx_exec_unicode(pPE->pInput, nOffset + nDelta, nValueUnits, 0) : xx_str_create("");

        version_add(pPE, pTitle, pValue);
        xx_mem_free(pValue);
    }

    nDelta += nValueLength;

    if (nLevel < 3) {
        int64_t nRemaining = nLength - nDelta;

        while (nRemaining > 0) {
            uint32_t nInner = parse_version_block(pPE, nOffset + nDelta, nLength - nDelta, prefix.data, nLevel + 1);

            if (nInner == 0) {
                break;
            }

            nInner = (uint32_t)ALIGN_UP((int64_t)nInner, 4);
            nDelta += nInner;
            nRemaining -= nInner;
        }
    }

    nResult = nLength;

    xx_buf_free(&prefix);
    xx_mem_free(pTitle);

    return nResult;
}

static void parse_version(xx_pe_inspection *pPE)
{
    xx_pe_inspect_resource *pResource = find_resource_by_type(pPE, 16); /* RT_VERSION */

    if ((pResource == NULL) || (pResource->nOffset == -1)) {
        return;
    }

    parse_version_block(pPE, pResource->nOffset, pResource->nSize, "", 0);
}

static void parse_manifest(xx_pe_inspection *pPE)
{
    xx_pe_inspect_resource *pResource = find_resource_by_type(pPE, 24); /* RT_MANIFEST */

    if ((pResource == NULL) || (pResource->nOffset == -1)) {
        pPE->pManifest = xx_str_create("");

        return;
    }

    {
        int64_t nSize = pResource->nSize;

        if (nSize > 4000) {
            nSize = 4000;
        }

        pPE->pManifest = nSize > 0
                            ? xx_exec_string(pPE->pInput, pResource->nOffset, nSize)
                            : xx_exec_copy_string(pPE->pInput, "");
    }
}

/* ----------------------------------------------------------- debug data  */

static void parse_debug(xx_pe_inspection *pPE)
{
    int64_t nOffset = xx_pe_inspect_rva_to_offset(pPE, pPE->pDirRVA[XX_PE_INSPECT_DIR_DEBUG]);
    uint32_t nSize = pPE->pDirSize[XX_PE_INSPECT_DIR_DEBUG];
    uint32_t nMax = 0;
    uint32_t i = 0;
    int nCount = 0;

    if ((nOffset == -1) || (nSize == 0)) {
        return;
    }

    /* xx_pe_inspection::getDebugList visits a header while its starting displacement is
     * below Size, including a final header with fewer than 28 declared bytes. */
    nMax = nSize / 28 + (nSize % 28 != 0);

    if (nMax > 256) {
        nMax = 256;
    }

    pPE->pDebugRecords = (xx_pe_inspect_debug_record *)xx_mem_calloc(nMax, sizeof(xx_pe_inspect_debug_record));
    if (!pPE->pDebugRecords) { pPE->pInput->failed = true; return; }

    for (i = 0; i < nMax; i++) {
        int64_t nBase = nOffset + i * 28;
        uint32_t nPointerToRawData = xx_exec_u32(pPE->pInput, nBase + 24, false);

        /* xx_pe_inspection::getDebugList stops at the first entry without usable raw
         * data - a trailing REPRO record is therefore not reported.        */
        if ((nPointerToRawData == 0) || ((int64_t)nPointerToRawData >= pPE->pInput->size)) {
            break;
        }

        pPE->pDebugRecords[nCount].nType = xx_exec_u32(pPE->pInput, nBase + 12, false);
        pPE->pDebugRecords[nCount].nSize = xx_exec_u32(pPE->pInput, nBase + 16, false);
        pPE->pDebugRecords[nCount].nOffset = (int64_t)nPointerToRawData;
        nCount++;
    }

    pPE->nDebugCount = nCount;
}

/* ---------------------------------------------------------- rich header  */

static void parse_rich(xx_pe_inspection *pPE)
{
    int64_t nStubOffset = 64;
    int64_t nStubSize = pPE->nLfanew - 64;
    int64_t nRichOffset = 0;
    uint32_t nXorKey = 0;
    int64_t nCurrent = 0;

    if ((nStubSize <= 0) || (nStubSize > 0x400)) {
        return;
    }

    if (xx_exec_find_string(pPE->pInput, nStubOffset, nStubSize, "Rich") != -1) {
        pPE->bRichPresent = 1;
    } else {
        return;
    }

    nRichOffset = xx_exec_find_string(pPE->pInput, nStubOffset, nStubSize, "Rich");
    nXorKey = xx_exec_u32(pPE->pInput, nRichOffset + 4, false);
    nCurrent = nRichOffset - 4;

    while (nCurrent > nStubOffset) {
        uint32_t nTemp = xx_exec_u32(pPE->pInput, nCurrent, false) ^ nXorKey;

        if (nTemp == 0x536e6144) { /* "DanS" */
            xx_list_s vec;
            xx_list_init(&vec, sizeof(xx_pe_inspect_rich_record), NULL);
            nCurrent += 16;

            for (; nCurrent < nRichOffset; nCurrent += 8) {
                xx_pe_inspect_rich_record record = {0};
                xx_pe_inspect_rich_record *pRecord = &record;
                uint32_t nValue1 = xx_exec_u32(pPE->pInput, nCurrent, false) ^ nXorKey;
                uint32_t nValue2 = xx_exec_u32(pPE->pInput, nCurrent + 4, false) ^ nXorKey;

                pRecord->nId = (uint16_t)(nValue1 >> 16);
                pRecord->nVersion = (uint16_t)(nValue1 & 0xFFFF);
                pRecord->nCount = nValue2;

                if (!xx_list_append(&vec, pRecord)) { pPE->pInput->failed = true; break; }
            }

            pPE->nRichCount = (int)vec.count;

            pPE->pRichRecords = (xx_pe_inspect_rich_record *)vec.data;
            vec.data = NULL; vec.count = vec.capacity = 0;

            xx_list_cleanup(&vec);
            break;
        }

        nCurrent -= 4;
    }
}

static int inspect_parse_input(xx_pe_inspection *pPE, xx_executable_input *pFile, const xx_pe *reader)
{
    int64_t nOptional = 0;
    int i = 0;

    xx_rt_memset(pPE, 0, sizeof(*pPE));
    pPE->pInput = pFile;
    pPE->nOverlayOffset = -1;

    if (pFile->size < 0x40) {
        return 0;
    }

    if ((xx_exec_u8(pFile, 0) != 'M') || (xx_exec_u8(pFile, 1) != 'Z')) {
        return 0;
    }

    pPE->nLfanew = (int64_t)xx_exec_u32(pFile, 0x3C, false);

    if ((pPE->nLfanew <= 0) || (pPE->nLfanew + 24 > pFile->size)) {
        return 0;
    }

    if (xx_exec_u32(pFile, pPE->nLfanew, false) != 0x00004550) { /* "PE\0\0" */
        return 0;
    }

    pPE->nMachine = xx_exec_u16(pFile, pPE->nLfanew + 4, false);
    pPE->nNumberOfSections = xx_exec_u16(pFile, pPE->nLfanew + 6, false);
    pPE->nTimeDateStamp = xx_exec_u32(pFile, pPE->nLfanew + 8, false);
    pPE->nPointerToSymbolTable = xx_exec_u32(pFile, pPE->nLfanew + 12, false);
    pPE->nNumberOfSymbols = xx_exec_u32(pFile, pPE->nLfanew + 16, false);
    pPE->nSizeOfOptionalHeader = xx_exec_u16(pFile, pPE->nLfanew + 20, false);
    pPE->nCharacteristics = xx_exec_u16(pFile, pPE->nLfanew + 22, false);

    nOptional = pPE->nLfanew + 24;
    pPE->nMagic = xx_exec_u16(pFile, nOptional, false);

    if (pPE->nMagic == 0x20B) {
        pPE->bIs64 = 1;
    } else if (pPE->nMagic == 0x10B) {
        pPE->bIs64 = 0;
    } else {
        return 0;
    }

    if (pPE->nSizeOfOptionalHeader < (pPE->bIs64 ? 112 : 96) ||
        nOptional > pFile->size - pPE->nSizeOfOptionalHeader ||
        (int64_t)pPE->nNumberOfSections * 40 > pFile->size - nOptional - pPE->nSizeOfOptionalHeader) return 0;

    pPE->nMajorLinkerVersion = xx_exec_u8(pFile, nOptional + 2);
    pPE->nMinorLinkerVersion = xx_exec_u8(pFile, nOptional + 3);
    pPE->nSizeOfCode = xx_exec_u32(pFile, nOptional + 4, false);
    pPE->nSizeOfInitializedData = xx_exec_u32(pFile, nOptional + 8, false);
    pPE->nSizeOfUninitializedData = xx_exec_u32(pFile, nOptional + 12, false);
    pPE->nAddressOfEntryPoint = xx_exec_u32(pFile, nOptional + 16, false);
    pPE->nBaseOfCode = xx_exec_u32(pFile, nOptional + 20, false);

    if (pPE->bIs64) {
        pPE->nImageBase = xx_exec_u64(pFile, nOptional + 24, false);
        pPE->nSectionAlignment = xx_exec_u32(pFile, nOptional + 32, false);
        pPE->nFileAlignment = xx_exec_u32(pFile, nOptional + 36, false);
        pPE->nMajorOperatingSystemVersion = xx_exec_u16(pFile, nOptional + 40, false);
        pPE->nMinorOperatingSystemVersion = xx_exec_u16(pFile, nOptional + 42, false);
        pPE->nMajorImageVersion = xx_exec_u16(pFile, nOptional + 44, false);
        pPE->nMinorImageVersion = xx_exec_u16(pFile, nOptional + 46, false);
        pPE->nMajorSubsystemVersion = xx_exec_u16(pFile, nOptional + 48, false);
        pPE->nMinorSubsystemVersion = xx_exec_u16(pFile, nOptional + 50, false);
        pPE->nWin32VersionValue = xx_exec_u32(pFile, nOptional + 52, false);
        pPE->nSizeOfImage = xx_exec_u32(pFile, nOptional + 56, false);
        pPE->nSizeOfHeaders = xx_exec_u32(pFile, nOptional + 60, false);
        pPE->nCheckSum = xx_exec_u32(pFile, nOptional + 64, false);
        pPE->nSubsystem = xx_exec_u16(pFile, nOptional + 68, false);
        pPE->nDllCharacteristics = xx_exec_u16(pFile, nOptional + 70, false);
        pPE->nSizeOfStackReserve = xx_exec_u64(pFile, nOptional + 72, false);
        pPE->nSizeOfStackCommit = xx_exec_u64(pFile, nOptional + 80, false);
        pPE->nSizeOfHeapReserve = xx_exec_u64(pFile, nOptional + 88, false);
        pPE->nSizeOfHeapCommit = xx_exec_u64(pFile, nOptional + 96, false);
        pPE->nLoaderFlags = xx_exec_u32(pFile, nOptional + 104, false);
        pPE->nNumberOfRvaAndSizes = xx_exec_u32(pFile, nOptional + 108, false);

        for (i = 0; i < 16; i++) {
            pPE->pDirRVA[i] = reader ? reader->data_directory_rva[i] : ((uint32_t)i < pPE->nNumberOfRvaAndSizes && pPE->nSizeOfOptionalHeader >= 112 + (i + 1) * 8 ? xx_exec_u32(pFile, nOptional + 112 + i * 8, false) : 0);
            pPE->pDirSize[i] = reader ? reader->data_directory_size[i] : ((uint32_t)i < pPE->nNumberOfRvaAndSizes && pPE->nSizeOfOptionalHeader >= 112 + (i + 1) * 8 ? xx_exec_u32(pFile, nOptional + 116 + i * 8, false) : 0);
        }
    } else {
        pPE->nBaseOfData = xx_exec_u32(pFile, nOptional + 24, false);
        pPE->nImageBase = xx_exec_u32(pFile, nOptional + 28, false);
        pPE->nSectionAlignment = xx_exec_u32(pFile, nOptional + 32, false);
        pPE->nFileAlignment = xx_exec_u32(pFile, nOptional + 36, false);
        pPE->nMajorOperatingSystemVersion = xx_exec_u16(pFile, nOptional + 40, false);
        pPE->nMinorOperatingSystemVersion = xx_exec_u16(pFile, nOptional + 42, false);
        pPE->nMajorImageVersion = xx_exec_u16(pFile, nOptional + 44, false);
        pPE->nMinorImageVersion = xx_exec_u16(pFile, nOptional + 46, false);
        pPE->nMajorSubsystemVersion = xx_exec_u16(pFile, nOptional + 48, false);
        pPE->nMinorSubsystemVersion = xx_exec_u16(pFile, nOptional + 50, false);
        pPE->nWin32VersionValue = xx_exec_u32(pFile, nOptional + 52, false);
        pPE->nSizeOfImage = xx_exec_u32(pFile, nOptional + 56, false);
        pPE->nSizeOfHeaders = xx_exec_u32(pFile, nOptional + 60, false);
        pPE->nCheckSum = xx_exec_u32(pFile, nOptional + 64, false);
        pPE->nSubsystem = xx_exec_u16(pFile, nOptional + 68, false);
        pPE->nDllCharacteristics = xx_exec_u16(pFile, nOptional + 70, false);
        pPE->nSizeOfStackReserve = xx_exec_u32(pFile, nOptional + 72, false);
        pPE->nSizeOfStackCommit = xx_exec_u32(pFile, nOptional + 76, false);
        pPE->nSizeOfHeapReserve = xx_exec_u32(pFile, nOptional + 80, false);
        pPE->nSizeOfHeapCommit = xx_exec_u32(pFile, nOptional + 84, false);
        pPE->nLoaderFlags = xx_exec_u32(pFile, nOptional + 88, false);
        pPE->nNumberOfRvaAndSizes = xx_exec_u32(pFile, nOptional + 92, false);

        for (i = 0; i < 16; i++) {
            pPE->pDirRVA[i] = reader ? reader->data_directory_rva[i] : ((uint32_t)i < pPE->nNumberOfRvaAndSizes && pPE->nSizeOfOptionalHeader >= 96 + (i + 1) * 8 ? xx_exec_u32(pFile, nOptional + 96 + i * 8, false) : 0);
            pPE->pDirSize[i] = reader ? reader->data_directory_size[i] : ((uint32_t)i < pPE->nNumberOfRvaAndSizes && pPE->nSizeOfOptionalHeader >= 96 + (i + 1) * 8 ? xx_exec_u32(pFile, nOptional + 100 + i * 8, false) : 0);
        }
    }

    if (pPE->nSectionAlignment == 0) {
        pPE->nSectionAlignment = 0x1000;
    }

    if (pPE->nFileAlignment == 0) {
        pPE->nFileAlignment = 0x200;
    }

    pPE->bValid = 1;

    parse_sections(pPE, reader);
    if (!build_memory_map(pPE, reader)) { pFile->failed = true; return 0; }

    pPE->nEntryPointAddress = inspect_rva_address(pPE, pPE->nAddressOfEntryPoint);
    pPE->nEntryPointOffset = pPE->nAddressOfEntryPoint ? xx_pe_inspect_rva_to_offset(pPE, pPE->nAddressOfEntryPoint) : -1;

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

    return 1;
}

void xx_pe_inspect_free(xx_pe_inspection *pPE)
{
    if (!pPE) return;
    int i = 0;
    int j = 0;

    for (i = 0; i < pPE->nImportCount; i++) {
        for (j = 0; j < pPE->pImports[i].nFunctionCount; j++) {
            xx_mem_free(pPE->pImports[i].ppFunctions[j]);
        }

        xx_mem_free(pPE->pImports[i].ppFunctions);
        xx_mem_free(pPE->pImports[i].pName);
    }

    xx_mem_free(pPE->pImports);

    for (i = 0; i < pPE->nExportCount; i++) {
        xx_mem_free(pPE->ppExportFunctions[i]);
    }

    xx_mem_free(pPE->ppExportFunctions);

    for (i = 0; i < pPE->nResourceCount; i++) {
        xx_mem_free(pPE->pResources[i].pName);
        xx_mem_free(pPE->pResources[i].pTypeName);
    }

    xx_mem_free(pPE->pResources);

    for (i = 0; i < pPE->nVersionCount; i++) {
        xx_mem_free(pPE->pVersionRecords[i].pKey);
        xx_mem_free(pPE->pVersionRecords[i].pValue);
    }

    xx_mem_free(pPE->pVersionRecords);

    xx_mem_free(pPE->pDebugRecords);
    xx_mem_free(pPE->pRichRecords);
    xx_mem_free(pPE->pManifest);
    xx_mem_free(pPE->pSections);
    xx_memory_map_cleanup(&pPE->map);
    xx_exec_input_free(pPE->pInput);
    xx_rt_memset(pPE, 0, sizeof(*pPE));
}

int xx_pe_inspect_parse(xx_pe_inspection *pPE, xx_pe *reader, xx_pd_struct *pd)
{
    xx_executable_input *input;
    int result;
    int64_t saved;
    if (!pPE) return 0;
    xx_rt_memset(pPE, 0, sizeof(*pPE));
    if (!reader || !reader->format.device) return 0;
    saved = xx_io_tell(reader->format.device);
    if (!reader->format.base_info_handled &&
        (!reader->format.handle_base_info || !reader->format.handle_base_info(&reader->format, pd))) {
        if (saved >= 0) xx_io_seek64(reader->format.device, saved, XX_RT_SEEK_SET);
        return 0;
    }
    input = xx_exec_input_create(&reader->format, pd);
    if (!input) { if (saved >= 0) xx_io_seek64(reader->format.device, saved, XX_RT_SEEK_SET); return 0; }
    result = inspect_parse_input(pPE, input, reader->format.base_info_handled ? reader : NULL);
    if (saved >= 0 && xx_io_seek64(reader->format.device, saved, XX_RT_SEEK_SET) != 0) input->failed = true;
    if (!result || input->failed || xx_pd_is_stopped(pd)) { xx_pe_inspect_free(pPE); return 0; }
    input->pd = NULL; input->parsing = false; input->read_work = 0;
    return result;
}

int xx_pe_inspect_analyze_from_device(xx_pe_inspection *state, xx_io_device *device, int64_t base, xx_pd_struct *pd)
{
    xx_pe reader;
    int result;
    xx_pe_init(&reader, device, base);
    result = xx_pe_inspect_parse(state, &reader, pd);
    xx_pe_destroy(&reader);
    return result;
}
