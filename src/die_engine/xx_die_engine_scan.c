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

/* xx_die_engine_scan.c - scan orchestration: resolve the file type through xx_scan, run the _init scripts and
 * then every applicable detection script in database order.               */

#include "xx_die_engine_internal.h"
#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/sig/xx_sig.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include "../io/xx_io_buffer.h"
#include <limits.h>

static void signature_set_error(DieEngine *pEngine, const char *pText)
{
    static const char sPrefix[] = "Invalid signature: ";
    size_t nLength = x_strlen(pText);

    cd_free(pEngine->pLastError);
    pEngine->pLastError = (char *)cd_malloc(sizeof(sPrefix) + nLength);
    x_memcpy(pEngine->pLastError, sPrefix, sizeof(sPrefix) - 1);
    x_memcpy(pEngine->pLastError + sizeof(sPrefix) - 1, pText, nLength + 1);

    if (x_getenv("CDIE_TRACE")) {
        x_fprintf(x_stderr(), "[cdie] %s: %s\n", pEngine->pCurrentScript ? pEngine->pCurrentScript : "", pEngine->pLastError);
    }
}

static int signature_cached_compare(xx_io_device *pFile, cd_i64 nBase, cd_u64 nCacheBytes, cd_u64 nDistance, int bNegative, const char *pNormalized, size_t nSliceBytes)
{
    size_t nLength = x_strlen(pNormalized);
    cd_u64 nAvailable;
    cd_u64 nStart;

    if (!nLength || !nCacheBytes) return 0;
    if (bNegative) {
        if (nDistance >= (cd_u64)nSliceBytes) return 0;
        nSliceBytes -= (size_t)nDistance;
        nStart = (cd_u64)nBase;
        nAvailable = nCacheBytes;
    } else {
        if (nDistance >= nCacheBytes) return 0;
        nStart = (cd_u64)nBase + nDistance;
        nAvailable = nCacheBytes - nDistance;
    }
    if (nAvailable > (cd_u64)nSliceBytes) nAvailable = (cd_u64)nSliceBytes;
    if ((cd_u64)(nLength / 2 + nLength % 2) > nAvailable) return 0;
    return xx_sig_compare_nibbles(pFile, (cd_i64)nStart, pNormalized, nLength);
}

/* A normalized literal needs neither allocated records nor an address map.
 * Return -1 for forms that still need the full signature parser. Cached
 * nibble comparisons must run first because they also accept odd lengths. */
static int signature_literal_compare(xx_io_device *pFile, cd_i64 nOffset, const char *pNormalized)
{
    size_t nLength = x_strlen(pNormalized);
    size_t i;

    if (!nLength || (nLength & 1) || nLength > 256) return -1;
    for (i = 0; i < nLength; i++) {
        char c = pNormalized[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return -1;
    }
    if (nOffset < 0 || xx_io_total_size(pFile) < 0 || (cd_u64)nOffset > (cd_u64)xx_io_total_size(pFile) || (cd_u64)(nLength / 2) > (cd_u64)xx_io_total_size(pFile) - (cd_u64)nOffset) {
        return 0;
    }
    return xx_sig_compare_nibbles(pFile, nOffset, pNormalized, nLength);
}

int die_engine_signature_compare(DieEngine *pEngine, cd_i64 nOffset, const char *pText, int nKind, cd_i64 nCacheBase)
{
    char *pNormalized = xx_sig_normalize(pText);
    xx_data_signature signature;
    int bValid = 0;
    int bResult = 0;
    int bReport = (nKind != 0 && nKind != 3);
    cd_u64 nCacheBytes = 0;
    cd_u64 nDistance = 0;
    int bNegative = nOffset < nCacheBase;
    size_t nSliceBytes;
    cd_u64 nCriterion;
    cd_u64 nCacheLimit = 0;

    if (!pNormalized) {
        return 0;
    }
    if (nKind != 3 && nCacheBase >= 0 && nCacheBase < xx_io_total_size(pEngine->device) && (nKind != 2 || nCacheBase > 0)) {
        nCacheBytes = (cd_u64)(xx_io_total_size(pEngine->device) - nCacheBase);
        if (nCacheBytes > 256) nCacheBytes = 256;
        nDistance = bNegative ? (cd_u64)nCacheBase - (cd_u64)nOffset : (cd_u64)nOffset - (cd_u64)nCacheBase;
        nCriterion = nKind == 1 ? nCacheBytes : nCacheBytes * 2;
        if (bNegative) {
            nCacheLimit = nDistance > (cd_u64)-1 - nCriterion ? (cd_u64)-1 : nDistance + nCriterion;
        } else if (nDistance < nCriterion) {
            nCacheLimit = nCriterion - nDistance;
        }
    }
    /* Header caching uses normalized length and bytes; EP/overlay caching
     * uses original text length and the hexadecimal cache's character count. */
    nSliceBytes = x_strlen(nKind == 1 ? pNormalized : (pText ? pText : ""));
    if ((cd_u64)nSliceBytes < nCacheLimit && !x_strchr(pNormalized, '$') && !x_strchr(pNormalized, '#') && !x_strchr(pNormalized, '+') && !x_strchr(pNormalized, '%') &&
        !x_strchr(pNormalized, '*')) {
        bResult = signature_cached_compare(pEngine->device, nCacheBase, nCacheBytes, nDistance, bNegative, pNormalized, nSliceBytes);
        xx_str_free(pNormalized);
        return bResult;
    }

    /* Binary_Script's cached QString comparison precedes the overlay range
     * check. Keep that behavior for short literal patterns, but reject DOS/NE
     * extended signatures seeking before or beyond the actual overlay. */
    if (nKind == 2 && (pEngine->pMap->file_type == XX_FILE_TYPE_NE ||
                       (pEngine->pMap->file_type == XX_FILE_TYPE_MSDOS && pEngine->pMap->mode == XX_MEMORY_MAP_MODE_SEGMENTS)) &&
        (nCacheBase <= 0 || nOffset < nCacheBase || nOffset >= xx_io_total_size(pEngine->device))) {
        xx_str_free(pNormalized);
        return 0;
    }

    bResult = signature_literal_compare(pEngine->device, nOffset, pNormalized);
    if (bResult >= 0) {
        xx_str_free(pNormalized);
        return bResult;
    }

    bResult = 0;
    bValid = xx_sig_valid(pNormalized, &signature, NULL);
    if (!bValid || signature.count == 0) {
        if (bReport) {
            signature_set_error(pEngine, pNormalized);
        }
    } else {
        const unsigned char *pWhole = xx_io_buffer_whole(pEngine->device, 0);
        bResult = pWhole ? xx_data_signature_match(pWhole, (size_t)xx_io_total_size(pEngine->device), nOffset, &signature, &pEngine->sigContext, NULL)
                         : xx_io_signature_match(pEngine->device, nOffset, &signature, &pEngine->sigContext, NULL);
    }
    xx_data_signature_free(&signature);
    xx_str_free(pNormalized);
    return bResult;
}

cd_i64 die_engine_signature_find(DieEngine *pEngine, cd_i64 nOffset, cd_i64 nSize, const char *pText)
{
    char *pNormalized;
    xx_data_signature signature;
    int bValid;
    int bParserError = 0;
    cd_i64 nResult = -1;

    /* The reference rejects an empty search range before parsing. An empty
     * search signature simply has no match and does not set a progress error. */
    if (nOffset < 0 || nOffset >= xx_io_total_size(pEngine->device) || nSize == 0 || nSize < -1) {
        return -1;
    }
    pNormalized = xx_sig_normalize(pText);
    if (!pNormalized) {
        return -1;
    }
    bValid = xx_sig_valid(pNormalized, &signature, &bParserError);
    if (!bValid) {
        /* _getSignatureBytes sets the progress message for lexical errors;
         * other invalid record shapes only make isSignatureValid false. */
        if (bParserError) {
            signature_set_error(pEngine, pNormalized);
        }
    } else if (signature.count > 0) {
        nResult = pEngine->bStop ? -1 : xx_io_signature_find(pEngine->device, nOffset, nSize, &signature, &pEngine->sigContext, pEngine->pd);
    }
    xx_data_signature_free(&signature);
    xx_str_free(pNormalized);
    return nResult;
}

int die_engine_signature_find_any(DieEngine *pEngine, cd_i64 nOffset, cd_i64 nSize, const char *const *ppTexts, size_t nCount)
{
    xx_data_signature *pSignatures;
    size_t i;
    int bResult = 0;

    if (!pEngine || !pEngine->device || !ppTexts || !nCount || nCount > (size_t)-1 / sizeof(*pSignatures) ||
        nOffset < 0 || nOffset >= xx_io_total_size(pEngine->device) || nSize == 0 || nSize < -1 ||
        pEngine->bStop || xx_pd_is_stopped(pEngine->pd)) {
        return 0;
    }
    pSignatures = (xx_data_signature *)xx_mem_calloc(nCount, sizeof(*pSignatures));
    if (!pSignatures) return 0;

    /* Validate every alternative before searching: a malformed later entry
     * must not become a successful match through an earlier valid prefix. */
    for (i = 0; i < nCount; i++) {
        char *pNormalized;
        int bParserError = 0;
        int bValid;

        if (!ppTexts[i] || pEngine->bStop || xx_pd_is_stopped(pEngine->pd)) goto cleanup;
        pNormalized = xx_sig_normalize(ppTexts[i]);
        if (!pNormalized) goto cleanup;
        bValid = xx_sig_valid(pNormalized, &pSignatures[i], &bParserError);
        if (!bValid && bParserError) signature_set_error(pEngine, pNormalized);
        xx_str_free(pNormalized);
        if (!bValid || pSignatures[i].count == 0) goto cleanup;
    }
    bResult = xx_io_signature_find_any(pEngine->device, nOffset, nSize, pSignatures, nCount, &pEngine->sigContext, pEngine->pd) ? 1 : 0;

cleanup:
    for (i = 0; i < nCount; i++) xx_data_signature_free(&pSignatures[i]);
    xx_mem_free(pSignatures);
    return bResult;
}

/* Returns the first path segment of the signature name, uppercased. */
static void signature_prefix(const char *pName, char *pBuf, size_t nBufSize)
{
    size_t i = 0;

    for (i = 0; (i + 1 < nBufSize) && pName[i] && (pName[i] != '.'); i++) {
        char nChar = pName[i];

        if ((nChar >= 'a') && (nChar <= 'z')) {
            nChar = (char)(nChar - 'a' + 'A');
        }

        pBuf[i] = nChar;
    }

    pBuf[i] = 0;
}

static int should_execute(DBSignature *pRecord, XFileType fileType, int bIsCliAssembly, xx_scan_options *pOptions)
{
    char sPrefix[64];

    /* CLI-assembly (PE/DOTNET) scripts run only when the file is a .NET PE;
     * the primary file type is still PE, so they cannot go through the plain
     * xft_check. Everything else matches the picked type as usual. */
    if (pRecord->fileType == XFT_CLI_ASSEMBLY) {
        if (!bIsCliAssembly) {
            return 0;
        }
    } else if (!xft_check(pRecord->fileType, fileType)) {
        return 0;
    }

    signature_prefix(pRecord->pName, sPrefix, sizeof(sPrefix));

    if ((!pOptions->deep_scan) && ((x_strcmp(sPrefix, "DS") == 0) || (x_strcmp(sPrefix, "EP") == 0))) {
        return 0;
    }

    if ((!pOptions->heuristic_scan) && (x_strcmp(sPrefix, "HEUR") == 0)) {
        return 0;
    }

    if (x_strcmp(pRecord->pName, "_init") == 0) {
        return 0;
    }

    if (!db_signature_matches_tag_filters(pRecord, pOptions->include_tags, pOptions->exclude_tags)) {
        return 0;
    }

    if (pRecord->databaseType == DB_MAIN) {
        return 1;
    }

    if (pOptions->use_custom_database && (pRecord->databaseType == DB_CUSTOM)) {
        return 1;
    }

    if (pOptions->use_extra_database && (pRecord->databaseType == DB_EXTRA)) {
        return 1;
    }

    return 0;
}

void die_engine_record_init(DieEngine *pEngine, xx_scan_record *pRecord)
{
    x_memset(pRecord, 0, sizeof(*pRecord));
    pRecord->file_type = die_engine_native_from_xft(pEngine->fileType);
    pRecord->size = xx_io_total_size(pEngine->device);
    pRecord->scan_id = pEngine->pOptions->scan_id;
    pRecord->parent_id = pEngine->pOptions->parent_id;
    pRecord->scan_part = pEngine->pOptions->scan_part;
    pRecord->archive_member = pEngine->pOptions->archive_member ? cd_strdup(pEngine->pOptions->archive_member) : NULL;
    pRecord->archive_offset = pEngine->pOptions->archive_offset;
    pRecord->archive_size = pEngine->pOptions->archive_size;
    pRecord->archive_compressed = pEngine->pOptions->archive_compressed;
    pRecord->archive_method = pEngine->pOptions->archive_method;
}

static void end_script_profile(DieEngine *pEngine, DBSignature *pRecord, cd_i64 nStart)
{
    die_engine_profile_end(pEngine, nStart, "%s:", pRecord->pName);

    if (nStart >= 0) {
        pRecord->nElapsedTime = (int64_t)((cd_i64)x_clock_ms() - nStart);
    }
}

static void run_script(DieEngine *pEngine, DBSignature *pRecord, int bCallDetect)
{
    JSCtx *pCtx = pEngine->pJs;
    JSVal global;
    JSVal detect;
    JSVal args[3];
    JSVal callResult;
    cd_i64 nProfileStart = -1;
    int bEvalResult = 0;
    const char *source_name = pEngine->pDebug && pRecord->pFilePath && pRecord->pFilePath[0]
        ? pRecord->pFilePath : pRecord->pName;

    pEngine->pCurrentScript = pRecord->pName;
    pEngine->pCurrentScriptPath = pRecord->pFilePath && pRecord->pFilePath[0] ? pRecord->pFilePath : pRecord->pName;
    cd_free(pEngine->pCurrentFormatId);
    cd_free(pEngine->pCurrentFormatName);
    pEngine->pCurrentFormatId = NULL;
    pEngine->pCurrentFormatName = NULL;

    /* Setting CDIE_TRACE traces the script order, which is the quickest way
     * to find the culprit when a rule misbehaves on an unusual input.      */
    if (x_getenv("CDIE_TRACE")) {
        x_fprintf(x_stderr(), "[cdie] %s\n", pRecord->pName);
        x_fflush(x_stderr());
    }

    /* DiE_Script::_executeSignature announces the script, times it and
     * reports "<name>: [n ms]" afterwards. Only detection scripts go through
     * it; the _init scripts are evaluated elsewhere and are not measured.  */
    if (bCallDetect) {
        die_engine_profile_text(pEngine, pRecord->pName);
        nProfileStart = die_engine_profile_start(pEngine);
        if (nProfileStart >= 0) {
            pRecord->nElapsedTime = 0;
        }
    }

    js_clear_error(pCtx);

    if (js_is_bytecode(pRecord->pText, pRecord->nSize)) {
        bEvalResult = js_eval_nested_bytecode(pCtx, pRecord->pText, pRecord->nSize, source_name);
    } else {
        bEvalResult = js_eval_nested(pCtx, pRecord->pText, source_name);
    }

    if (!bEvalResult) {
        char sBuf[1024];

        x_snprintf(sBuf, sizeof(sBuf), "%s: %s", pRecord->pName, js_error(pCtx));
        (void)xx_scan_result_add_error(pEngine->pResult, sBuf, pEngine->pd);
        js_clear_error(pCtx);
        end_script_profile(pEngine, pRecord, nProfileStart);

        return;
    }

    if (!bCallDetect) {
        return;
    }

    global = js_global(pCtx);
    detect = js_get(pCtx, global, "detect");

    if (!js_is_callable(detect)) {
        js_release(pCtx, detect);
        js_release(pCtx, global);
        end_script_profile(pEngine, pRecord, nProfileStart);

        return;
    }

    args[0] = js_bool(pEngine->pOptions->show_type);
    args[1] = js_bool(pEngine->pOptions->show_version);
    args[2] = js_bool(pEngine->pOptions->show_info);

    callResult = js_call(pCtx, detect, global, 3, args);

    if (js_has_exception(pCtx)) {
        char sBuf[1024];

        x_snprintf(sBuf, sizeof(sBuf), "%s: %s", pRecord->pName, js_error(pCtx));
        (void)xx_scan_result_add_error(pEngine->pResult, sBuf, pEngine->pd);
        js_clear_error(pCtx);
    }

    js_release(pCtx, callResult);
    js_release(pCtx, detect);
    js_release(pCtx, global);

    end_script_profile(pEngine, pRecord, nProfileStart);
}

/* XMSDOS uses a signature-visible layout distinct from the native parser:
 * the page count bounds the image, the header is clipped to it, and the
 * loadable bytes occupy the synthetic address range beginning at 0x10000000.
 * Keep malformed last-page and header handling identical to XMSDOS. Like
 * XMSDOS (and DIE 3.x), the module base is that 0x10000000 load address and
 * the entry point is module base + CS:IP, so getAddressOfEntryPoint,
 * getEntryPointOffset and compareEP all resolve through the same mapping. */
static int script_msdos_memory_map(xx_io_device *device, xx_memory_map *map, xx_pd_struct *pd)
{
    cd_i64 size = xx_io_total_size(device);
    cd_i64 image_size = size;
    cd_i64 header_size;
    cd_u16 magic;
    cd_u16 pages;
    cd_u16 last;
    cd_u32 entry;

    if (size < 2) return 0;
    magic = xx_io_get_u16(device, 0, false);
    if (magic != 0x5a4d && magic != 0x4d5a) return 0;
    pages = size >= 6 ? xx_io_get_u16(device, 4, false) : 0;
    last = size >= 4 ? xx_io_get_u16(device, 2, false) : 0;
    if (pages) {
        image_size = (cd_i64)pages * 512 - ((512 - (last & 511U)) & 511U);
        if (image_size > size) image_size = size;
    }
    header_size = size >= 10 ? (cd_i64)xx_io_get_u16(device, 8, false) * 16 : 0;
    if (header_size > image_size) header_size = image_size;
    entry = size >= 24 ? (cd_u32)xx_io_get_u16(device, 22, false) * 16U : 0;
    entry += size >= 22 ? xx_io_get_u16(device, 20, false) : 0;
    if (entry >= 0x100000U) entry -= 0x100000U;

    map->binary_size = size;
    map->module_address = UINT64_C(0x10000000);
    map->entry_point_address = UINT64_C(0x10000000) + entry;
    map->start_load_offset = header_size;
    map->code_base = 0;
    map->file_type = XX_FILE_TYPE_MSDOS;
    map->endian = XX_ENDIAN_LITTLE;
    map->mode = XX_MEMORY_MAP_MODE_SEGMENTS;
    if (!xx_memory_map_add_part(map, 0, header_size, XX_INVALID_ADDRESS, 0, XX_FILE_PART_HEADER, 0, "MSDOS Header", false) ||
        !xx_memory_map_add_part(map, header_size, image_size - header_size, UINT64_C(0x10000000), image_size - header_size,
                                XX_FILE_PART_SEGMENT, 1, "MSDOS image", false) ||
        !xx_memory_map_add_part(map, image_size, size - image_size, XX_INVALID_ADDRESS, 0, XX_FILE_PART_OVERLAY, 2, "Overlay", false) ||
        xx_pd_is_stopped(pd) || !xx_memory_map_finalize(map)) return -1;
    /* Finalization derives a module base from mapped addresses (none when the
     * header covers the whole image). XMSDOS's module base is the 0x10000000
     * load address in every case. */
    map->module_address = UINT64_C(0x10000000);
    map->image_size = image_size;
    return 1;
}

/* Binary_Script derives its PE overlay from header and section bytes only.
 * The native reader also counts certificates, debug data and COFF tables in
 * its format extent. Keep that metadata policy out of the signature cache. */
static void script_pe_overlay(xx_pe_inspection *pe)
{
    cd_i64 size = pe->map.binary_size;
    cd_i64 end = pe->nSizeOfHeaders < size ? pe->nSizeOfHeaders : size;
    int i;
    /* XPE::getFileParts limits the signature map to 96 sections. */
    for (i = 0; i < pe->nSectionCount && i < 96; ++i) {
        cd_i64 offset = pe->pSections[i].nPointerToRawData;
        cd_i64 section_size = pe->pSections[i].nSizeOfRawData;
        /* XPE retains the raw pointer even for zero-byte sections, which
         * have no physical record in the native map. Invalid pointers reset
         * to zero before the section range is clipped to the file. */
        if (offset > size) offset = 0;
        if (section_size > size - offset) section_size = size - offset;
        if (offset + section_size > end) end = offset + section_size;
    }
    pe->nOverlayOffset = end > 0 && end <= size ? end : -1;
    pe->nOverlaySize = pe->nOverlayOffset >= 0 ? size - pe->nOverlayOffset : 0;
}

/* The script view follows XNE's segmented memory map. The native NE parser
 * also counts relocation/resource/name tables when finding its format end;
 * XNE's script overlay starts after the header and physical segments only. */
static int script_ne_memory_map(xx_io_device *device, xx_memory_map *map, xx_pd_struct *pd)
{
    cd_i64 size = xx_io_total_size(device);
    cd_i64 header;
    cd_i64 header_size;
    cd_i64 table;
    cd_i64 end;
    cd_u32 csip;
    unsigned count;
    unsigned shift;
    unsigned index;
    unsigned parts = 1;

    if (size < 0x40) return 0;
    header = xx_io_get_i32(device, 0x3c, false);
    if (header < 0 || header > size - 2 || xx_io_get_u16(device, header, false) != 0x454e) return 0;
    header_size = size - header < 0x40 ? size : header + 0x40;
    csip = header <= size - 0x18 ? xx_io_get_u32(device, header + 0x14, false) : 0;
    count = header <= size - 0x1e ? xx_io_get_u16(device, header + 0x1c, false) : 0;
    table = header + (header <= size - 0x24 ? xx_io_get_u16(device, header + 0x22, false) : 0);
    shift = header <= size - 0x34 ? xx_io_get_u16(device, header + 0x32, false) : 0;
    if (table >= size) count = 0;
    else if ((cd_i64)count > (size - table) / 8) count = (unsigned)((size - table) / 8);

    map->binary_size = size;
    map->module_address = 0x10000;
    map->entry_point_address = UINT64_C(0x10000) + csip;
    map->file_type = XX_FILE_TYPE_NE;
    map->endian = XX_ENDIAN_LITTLE;
    map->mode = XX_MEMORY_MAP_MODE_SEGMENTS;
    if (!xx_memory_map_add_part(map, 0, header_size, 0x10000, 0x200, XX_FILE_PART_HEADER, 0, "Header", false)) return -1;
    end = header_size;
    /* XBinary asks XNE for at most 1,000 parts, including the header. Keep
     * segment indices even when an invalid physical segment is skipped. */
    for (index = 0; index < count && parts < 1000 && !xx_pd_is_stopped(pd); ++index) {
        cd_i64 record = table + (cd_i64)index * 8;
        cd_u64 sector = xx_io_get_u16(device, record, false);
        cd_i64 offset;
        cd_i64 length = xx_io_get_u16(device, record + 2, false);
        cd_u64 address = UINT64_C(0x10000) * (index + 2U);
        char name[32];

        if (shift > 62 || sector > ((cd_u64)(size - 1) >> shift)) continue;
        offset = (cd_i64)(sector << shift);
        if (!length) length = 0x10000;
        if (length > size - offset) length = size - offset;
        x_snprintf(name, sizeof(name), "Segment %u", index + 1U);
        if (!xx_memory_map_add_part(map, offset, length, address, 0x10000, XX_FILE_PART_SEGMENT, (int)parts, name, false)) return -1;
        ++parts;
        if (offset + length > end) end = offset + length;
    }
    if (parts < 1000 && end < size &&
        !xx_memory_map_add_part(map, end, size - end, XX_INVALID_ADDRESS, 0, XX_FILE_PART_OVERLAY, (int)parts, "Overlay", false)) return -1;
    return !xx_pd_is_stopped(pd) && xx_memory_map_finalize(map) ? 1 : -1;
}

/* Each pass owns its hook/context. No process-global hook state is used. */
static int die_engine_script_debug_hook(JSCtx *context, const JSDebugEvent *event, void *user)
{
    DieEngine *engine = (DieEngine *)user;
    const die_engine_debug_config *debug = engine->pDebug;
    if (engine->bStop || xx_pd_is_stopped(engine->pd) || !debug->hook(context, event, debug->hook_user)) {
        engine->bStop = 1;
        xx_pd_stop(engine->pd);
        return 0;
    }
    return 1;
}

/* One detection pass over a borrowed device for a single file type.
 * The buffered device is owned by this pass; the source stays open
 * for subsequent COM/Binary passes. bAddUnknown mirrors the
 * reference's _processDetect flag: when it is 0 an empty pass stays empty
 * instead of gaining the "Unknown" record.                                 */
static int scan_run_pass(xx_io_device *device, XFileType fileType, int bIsCliAssembly, DBase *pDb, xx_scan_options *pOptions, xx_scan_result *pResult, int bAddUnknown, xx_pd_struct *pd, const die_engine_debug_config *debug)
{
    /* Heap, not stack: DieEngine embeds every parser state by value and is
     * over 3 KB on its own. Keeping it off the stack reduces pressure during
     * nested scans. cd_calloc never returns NULL and zeroes the allocation. */
    DieEngine *pEngine = (DieEngine *)cd_calloc(1, sizeof(DieEngine));
    xx_memory_map binaryMap;
    int i = 0;
    int read_failed;
    int alloc_failed;
    /* A file-buffer-sized allocation that fails anywhere in this pass costs a
     * reader bytes it needed: the pass fails rather than detect less. */
    uintptr_t nAllocFailures = xx_io_alloc_failures();
    int nGlobalInit = -1;
    int nTypeInit = -1;
    int progress;
    const char *name = pOptions->file_name ? pOptions->file_name : xx_io_source_path(device);

    if (xx_pd_is_stopped(pd)) {
        cd_free(pEngine);
        return 0;
    }
    /* Archive members retain their extraction storage: small decoded bytes
     * are already resident, large ones stay in a temporary file, and stored
     * members stay borrowed views. Cache reads without whole-file copies. */
    if (xx_io_seek64(device, 0, SEEK_SET) != 0 ||
        !(pEngine->device = pOptions->scan_part == XX_SCAN_PART_ARCHIVE
                               ? xx_io_buffer_open(device, 0, 0, false)
                               : xx_io_buffer_open_auto(device, 0, xx_get_file_memory_limit(), false))) {
        if (xx_io_alloc_failures() != nAllocFailures) xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate DIE signature input buffer");
        else xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot open DIE signature input");
        cd_free(pEngine);
        return 0;
    }
    pEngine->file_name = name ? name : "";
    progress = xx_pd_enter_level(pd, (uint64_t)pDb->nCount, "DIE signatures");

    pEngine->fileType = fileType;
    /* A .NET PE is typed both as PE and CLI assembly; the primary type stays
     * PE, but the CLI-assembly flag drives the DOTNET object and the
     * PE/DOTNET scripts. */
    pEngine->bIsCliAssembly = bIsCliAssembly;
    pEngine->pDb = pDb;
    pEngine->pOptions = pOptions;
    pEngine->pResult = pResult;
    pEngine->pd = pd;
    pEngine->pDebug = debug;

    if ((pEngine->fileType == XFT_PE32) || (pEngine->fileType == XFT_PE64)) {
        pEngine->bHasPE = xx_pe_inspect_analyze_from_device(&pEngine->pe, pEngine->device, 0, NULL);
        if (pEngine->bHasPE && pEngine->bIsCliAssembly) {
            pEngine->bHasDotNet = xx_dotnet_inspect_analyze_from_device(&pEngine->dotnet, pEngine->device, 0, NULL);
        }
    } else if (pEngine->fileType == XFT_JPEG) {
        xx_jpeg_init(&pEngine->jpeg, pEngine->device, 0);
        pEngine->bHasJpeg = xx_jpeg_analyze(&pEngine->jpeg, NULL);
    } else if (pEngine->fileType == XFT_PNG) {
        xx_png_init(&pEngine->png, pEngine->device, 0);
        pEngine->bHasPng = xx_png_analyze(&pEngine->png, NULL);
    } else if (pEngine->fileType == XFT_APK) {
        xx_apk_init(&pEngine->apk, pEngine->device, 0);
        pEngine->bHasApk = xx_apk_analyze(&pEngine->apk, NULL);
    } else if (pEngine->fileType == XFT_PDF) {
        /* The native reader borrows the shared buffered device. Its analysis
         * accepts inspection-only documents separately from archive validation. */
        xx_pdf_init(&pEngine->pdf, pEngine->device, 0);
        pEngine->bHasPdf = xx_pdf_analyze(&pEngine->pdf, NULL);
    } else if ((pEngine->fileType == XFT_ELF) || (pEngine->fileType == XFT_ELF32) || (pEngine->fileType == XFT_ELF64)) {
        pEngine->bHasElf = xx_elf_inspect_analyze_from_device(&pEngine->elf, pEngine->device, 0, NULL);
    } else if (pEngine->fileType == XFT_DEX) {
        xx_dex_init(&pEngine->dex, pEngine->device, 0);
        pEngine->bHasDex = xx_dex_analyze(&pEngine->dex, NULL);
    } else if ((pEngine->fileType == XFT_MACHO) || (pEngine->fileType == XFT_MACHO32) || (pEngine->fileType == XFT_MACHO64)) {
        pEngine->bHasMach = xx_macho_inspect_analyze_from_device(&pEngine->mach, pEngine->device, 0, NULL);
    } else if (pEngine->fileType == XFT_PYC) {
        xx_pyc_init(&pEngine->pyc, pEngine->device, 0);
        pEngine->bHasPyc = xx_pyc_analyze(&pEngine->pyc, NULL);
    }

    /* Every ZIP-family container (also an APK, which additionally parses its
     * AndroidManifest above) gets its central-directory record list and
     * MANIFEST.MF read for the archive-record and manifest predicates. */
    if ((pEngine->fileType == XFT_APK) || (pEngine->fileType == XFT_JAR) || (pEngine->fileType == XFT_ZIP) || (pEngine->fileType == XFT_NPM) ||
        (pEngine->fileType == XFT_IPA)) {
        xx_zip_init(&pEngine->zip, pEngine->device, 0);
        pEngine->bHasZip = xx_zip_analyze(&pEngine->zip, NULL);
    }

    if (pEngine->bHasPE) {
        script_pe_overlay(&pEngine->pe);
        pEngine->pMap = &pEngine->pe.map;
        pEngine->nBits = pEngine->pe.bIs64 ? 64 : 32;
    } else {
        int script_map = 0;
        xx_memory_map_init(&binaryMap);
        if (pEngine->fileType == XFT_NE) script_map = script_ne_memory_map(pEngine->device, &binaryMap, pd);
        else if (pEngine->fileType == XFT_MSDOS) script_map = script_msdos_memory_map(pEngine->device, &binaryMap, pd);
        if (script_map < 0) {
            xx_memory_map_cleanup(&binaryMap);
            xx_io_close(pEngine->device);
            cd_free(pEngine);
            xx_pd_leave_level(pd, progress);
            if (!xx_pd_is_stopped(pd)) xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot initialize executable signature map");
            return 0;
        }
        pEngine->nBits = script_map ? 16 : 32;
        if (!script_map) {
            /* Forced-format callbacks still run on nonmatching input. Keep
             * their previous flat-map fallback when no matching header exists. */
            binaryMap.binary_size = xx_io_total_size(pEngine->device);
            binaryMap.endian = XX_ENDIAN_LITTLE;
            binaryMap.mode = XX_MEMORY_MAP_MODE_DATA;
            binaryMap.file_type = (pEngine->fileType == XFT_COM) ? XX_FILE_TYPE_COM : (pEngine->fileType == XFT_MSDOS) ? XX_FILE_TYPE_MSDOS : XX_FILE_TYPE_BINARY;
            xx_memory_map_add_part(&binaryMap, 0, binaryMap.binary_size, 0, binaryMap.binary_size, XX_FILE_PART_DATA, 0, "Data", false);
        }
        pEngine->pMap = &binaryMap;
    }

    /* NE branch/pointer signatures use the same reverse physical lookup as
     * XBinary's address conversions, including segments overlapping headers.
     * Other formats retain their existing signature lookup policy. */
    xx_data_sig_context_from_memory_map_ex(&pEngine->sigContext, pEngine->pMap,
                                         pEngine->pMap->file_type == XX_FILE_TYPE_NE ? XX_MEMORY_MAP_LOOKUP_LAST_PHYSICAL : XX_MEMORY_MAP_LOOKUP_FIRST_MATCH, true);

    pResult->file_type = die_engine_native_from_xft(pEngine->fileType);
    pResult->compatibility_file_type = (uint32_t)pEngine->fileType;

    pEngine->pJs = js_new();
    js_set_program_cache(pEngine->pJs, (JSProgramCache *)pDb->pJsProgramCache);
    js_set_user(pEngine->pJs, pEngine);
    die_engine_install_api(pEngine);
    if (debug && debug->hook) js_debug_set_hook(pEngine->pJs, die_engine_script_debug_hook, pEngine);

    /* Locate the global, per-format and (for a .NET PE) DOTNET _init scripts. */
    {
        int nCliInit = -1;

        for (i = 0; i < pDb->nCount; i++) {
            if (x_strcmp(pDb->pRecords[i].pName, "_init") != 0) {
                continue;
            }

            if (pDb->pRecords[i].fileType == XFT_UNKNOWN) {
                nGlobalInit = i;
            }

            if (pDb->pRecords[i].fileType == XFT_CLI_ASSEMBLY) {
                nCliInit = i;
            } else if (xft_check(pDb->pRecords[i].fileType, pEngine->fileType)) {
                nTypeInit = i;
            }
        }

        if (nGlobalInit >= 0 && !xx_io_buffer_failed(pEngine->device) && !xx_pd_is_stopped(pd)) {
            run_script(pEngine, &pDb->pRecords[nGlobalInit], 0);
        }

        if (nTypeInit >= 0 && !xx_io_buffer_failed(pEngine->device) && !xx_pd_is_stopped(pd)) {
            run_script(pEngine, &pDb->pRecords[nTypeInit], 0);
        }

        /* The DOTNET _init runs after the PE one, so it can build on it. */
        if (pEngine->bIsCliAssembly && (nCliInit >= 0) && !xx_io_buffer_failed(pEngine->device) && !xx_pd_is_stopped(pd)) {
            run_script(pEngine, &pDb->pRecords[nCliInit], 0);
        }
    }

    /* cd_alloc_oom() is always false unless the soft out-of-memory policy is
     * armed, which only the shared library does; there it ends the scan at
     * the next script boundary instead of ending the process. */
    if (debug && debug->signature_source) {
        DBSignature edited = {0};
        edited.pName = (char *)(debug->signature_name && debug->signature_name[0] ? debug->signature_name : "untitled.sg");
        edited.pFilePath = edited.pName;
        edited.pText = (char *)debug->signature_source;
        edited.nSize = x_strlen(debug->signature_source);
        edited.fileType = pEngine->fileType;
        edited.databaseType = DB_CUSTOM;
        if (!pEngine->bStop && !cd_alloc_oom() && !xx_io_buffer_failed(pEngine->device) && !xx_pd_is_stopped(pd))
            run_script(pEngine, &edited, 1);
        pEngine->pCurrentScript = NULL;
        pEngine->pCurrentScriptPath = NULL;
        i = pDb->nCount;
    } else {
        for (i = 0; (i < pDb->nCount) && (!pEngine->bStop) && (!cd_alloc_oom()) && !xx_io_buffer_failed(pEngine->device) && !xx_pd_is_stopped(pd); i++) {
            xx_pd_set_current(pd, progress, (uint64_t)i);
            if (xx_pd_is_stopped(pd)) break;
            if (should_execute(&pDb->pRecords[i], pEngine->fileType, pEngine->bIsCliAssembly, pOptions)) {
                if (debug) {
                    DBSignature record = pDb->pRecords[i];
                    run_script(pEngine, &record, 1);
                } else {
                    run_script(pEngine, &pDb->pRecords[i], 1);
                }
            }
        }
    }
    /* Results added from here on (Unknown) have no script. */
    pEngine->pCurrentScriptPath = NULL;

    if (bAddUnknown && (pResult->nCount == 0) && !xx_io_buffer_failed(pEngine->device) && !xx_pd_is_stopped(pd)) {
        xx_scan_record *pRecord = NULL;

        pResult->pRecords = (xx_scan_record *)cd_calloc(1, sizeof(xx_scan_record));
        pResult->nCapacity = 1;
        pRecord = &pResult->pRecords[0];
        die_engine_record_init(pEngine, pRecord);
        pRecord->type = cd_strdup("Unknown");
        pRecord->name = cd_strdup("Unknown");
        pRecord->version = cd_strdup("");
        pRecord->info = cd_strdup("");
        pRecord->priority = xx_scan_type_to_prio("Unknown");
        pRecord->is_unknown = 1;
        pResult->nCount = 1;
    }

    /* DiE sorts each pass before merging independent interpretations. */
    if (pOptions->sort) xx_scan_sort_records(pResult->pRecords, pResult->nCount);

    if (pEngine->pLastError) {
        static const char sPrefix[] = "Last error: ";
        size_t nLength = x_strlen(pEngine->pLastError);
        char *pError = (char *)cd_malloc(sizeof(sPrefix) + nLength);

        x_memcpy(pError, sPrefix, sizeof(sPrefix) - 1);
        x_memcpy(pError + sizeof(sPrefix) - 1, pEngine->pLastError, nLength + 1);
        (void)xx_scan_result_add_error(pResult, pError, pd);
        cd_free(pError);
        cd_free(pEngine->pLastError);
    }

    js_free(pEngine->pJs);

    for (i = 0; i < pEngine->nBlackListCount; i++) {
        cd_free(pEngine->pBlackList[i].pType);
        cd_free(pEngine->pBlackList[i].pName);
    }

    cd_free(pEngine->pBlackList);
    cd_free(pEngine->pCurrentFormatId);
    cd_free(pEngine->pCurrentFormatName);
    die_engine_profile_free(pEngine);

    xx_pe_inspect_free(&pEngine->pe);
    xx_dotnet_inspect_free(&pEngine->dotnet);
    if (!pEngine->bHasPE) xx_memory_map_cleanup(&binaryMap);

    xx_jpeg_destroy(&pEngine->jpeg);
    xx_png_destroy(&pEngine->png);

    xx_apk_destroy(&pEngine->apk);

    if (pEngine->fileType == XFT_PDF) {
        /* Initialization precedes analysis, including a failed analysis. */
        xx_pdf_destroy(&pEngine->pdf);
    }

    xx_elf_inspect_free(&pEngine->elf);

    xx_dex_destroy(&pEngine->dex);

    xx_macho_inspect_free(&pEngine->mach);

    xx_pyc_destroy(&pEngine->pyc);

    xx_zip_destroy(&pEngine->zip);

    read_failed = xx_io_buffer_failed(pEngine->device);
    alloc_failed = xx_io_alloc_failures() != nAllocFailures;
    xx_io_close(pEngine->device);
    cd_free(pEngine);

    xx_pd_set_current(pd, progress, (uint64_t)i);
    xx_pd_leave_level(pd, progress);
    if (alloc_failed) {
        (void)xx_scan_result_add_error(pResult, "Out of memory", pd);
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate DIE signature input buffer");
    } else if (read_failed) {
        (void)xx_scan_result_add_error(pResult, "File read error", pd);
        xx_pd_set_error(pd, XXFC_ERR_IO, "DIE signature input read failed");
    }
    return !read_failed && !alloc_failed && !xx_pd_is_stopped(pd);
}

/* Moves every record and error of pFrom into pTo - to the front when bFront
 * is set, otherwise after what is already there - leaving pFrom empty. The
 * record strings change owner, so only the arrays are freed.               */
static void result_merge(xx_scan_result *pTo, xx_scan_result *pFrom, int bFront)
{
    int nTotal = 0;

    if (pFrom->nCount > 0) {
        int nAt = bFront ? 0 : pTo->nCount;

        nTotal = pFrom->nCount + pTo->nCount;

        if (nTotal > pTo->nCapacity) {
            pTo->nCapacity = nTotal;
            pTo->pRecords = (xx_scan_record *)cd_realloc(pTo->pRecords, (size_t)nTotal * sizeof(xx_scan_record));
        }

        if (bFront && (pTo->nCount > 0)) {
            x_memmove(pTo->pRecords + pFrom->nCount, pTo->pRecords, (size_t)pTo->nCount * sizeof(xx_scan_record));
        }

        x_memcpy(pTo->pRecords + nAt, pFrom->pRecords, (size_t)pFrom->nCount * sizeof(xx_scan_record));
        pTo->nCount = nTotal;
    }

    if (pFrom->nErrorCount > 0) {
        int nErrors = pFrom->nErrorCount + pTo->nErrorCount;
        int nAt = bFront ? 0 : pTo->nErrorCount;
        int i = 0;

        pTo->ppErrors = (char **)cd_realloc(pTo->ppErrors, (size_t)nErrors * sizeof(char *));

        if (bFront && (pTo->nErrorCount > 0)) {
            x_memmove(pTo->ppErrors + pFrom->nErrorCount, pTo->ppErrors, (size_t)pTo->nErrorCount * sizeof(char *));
        }

        for (i = 0; i < pFrom->nErrorCount; i++) {
            pTo->ppErrors[nAt + i] = pFrom->ppErrors[i];
        }

        pTo->nErrorCount = nErrors;
    }

    cd_free(pFrom->pRecords);
    cd_free(pFrom->ppErrors);
    cd_free(pFrom->pFileName);
    x_memset(pFrom, 0, sizeof(*pFrom));
}

/* XScanEngine's hasNonGenericCOMRecords. _COM.0.sg answers for every file
 * that reaches it, so a COM pass only counts when it produced something
 * beyond the operating-system and format lines it always emits.            */
static int com_has_non_generic(xx_scan_result *pResult)
{
    int i = 0;

    for (i = 0; i < pResult->nCount; i++) {
        char *pType = xx_scan_translate_type(pResult->pRecords[i].type);
        int bGeneric = ((cd_stricmp_ascii(pType, "Operation system") == 0) || (cd_stricmp_ascii(pType, "Format") == 0)) ? 1 : 0;

        cd_free(pType);

        if (!bGeneric) {
            return 1;
        }
    }

    return 0;
}

/* Preserve detection I/O failures even when a weak format probe falls back
 * to Binary. The forwarding device borrows its source and never closes it. */
typedef struct scan_detection_device {
    xx_io_device device;
    xx_io_device *source;
    bool failed;
} scan_detection_device;

static ssize_t scan_detection_read(xx_io_device *device, void *buffer, size_t size)
{
    scan_detection_device *state = (scan_detection_device *)device->priv;
    int64_t position = xx_io_tell(state->source);
    ssize_t count = xx_io_read(state->source, buffer, size);
    if (count < 0 || (uint64_t)count > (uint64_t)size ||
        (size && !count && position >= 0 && position < xx_io_total_size(state->source))) state->failed = true;
    return count;
}

static int scan_detection_seek64(xx_io_device *device, int64_t offset, int whence)
{
    scan_detection_device *state = (scan_detection_device *)device->priv;
    int result = xx_io_seek64(state->source, offset, whence);
    if (result != 0) state->failed = true;
    return result;
}

static int scan_detection_seek(xx_io_device *device, long offset, int whence)
{
    return scan_detection_seek64(device, (int64_t)offset, whence);
}

static int64_t scan_detection_tell(xx_io_device *device)
{
    scan_detection_device *state = (scan_detection_device *)device->priv;
    return xx_io_tell(state->source);
}

static int64_t scan_detection_size(xx_io_device *device)
{
    scan_detection_device *state = (scan_detection_device *)device->priv;
    return xx_io_total_size(state->source);
}

/* Orchestrate signature passes over the borrowed, bounded input device.
 * Type detection, input metadata and pass selection use xx_io directly;
 * only scan_run_pass creates the buffered input device. */
static int scan_engine_run(xx_io_device *device, DBase *pDb, xx_scan_options *pOptions, XFileType selectedType, xx_scan_result *pResult, xx_pd_struct *pd, const die_engine_debug_config *debug)
{
    xx_scan_file_type_set set;
    scan_detection_device detection = {0};
    XFileType fileType = XFT_BINARY;
    const char *file_name;
    int64_t device_size;
    int bIsCliAssembly = 0;
    int nResult = 0;
#if defined(XXFC_BUILD_SHARED)
    /* Inside a host process an allocation failure must not take the process
     * with it, so the shared library runs the scan under the soft policy and
     * reports the failure as a failed scan. The static library keeps the
     * abort, and with it cdie's exact behaviour.
     *
     * This was spelled DIE_BUILD_SHARED in cdie, which nothing in xxfclib
     * defines -- leaving the policy silently off in every build here,
     * including the shared one. XXFC_BUILD_SHARED is the same switch under
     * xxfclib's name (CMakeLists.txt sets it on the shared target). */
    int bSoftOom = cd_alloc_begin_soft_oom();
#endif

    if (!device || !pDb || !pOptions || !pResult) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Missing DIE scan argument");
        goto scan_done;
    }
    if (xx_pd_is_stopped(pd)) goto scan_done;
    device_size = xx_io_total_size(device);
    if (device_size < 0) {
        xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot determine DIE input size");
        goto scan_done;
    }
    pResult->sort = pOptions->sort;
    file_name = pOptions->file_name ? pOptions->file_name : xx_io_source_path(device);
    pResult->pFileName = cd_strdup(file_name ? file_name : "");
    pResult->nFileSize = device_size;
    detection.source = device;
    detection.device.priv = &detection;
    detection.device.read = scan_detection_read;
    detection.device.seek = scan_detection_seek;
    detection.device.seek64 = scan_detection_seek64;
    detection.device.tell = scan_detection_tell;
    detection.device.total_size = scan_detection_size;
    if (!xx_scan_file_type_set_detect(&detection.device, file_name, &set) || detection.failed) {
        xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot detect DIE input type");
        goto scan_done;
    }
    fileType = selectedType == XFT_UNKNOWN ? xx_scan_pick_file_type(device, file_name, &set) : selectedType;
    if (fileType == XFT_CLI_ASSEMBLY || fileType == XFT_PE) {
        if (xx_scan_file_type_set_contains(&set, XFT_PE32)) fileType = XFT_PE32;
        else if (xx_scan_file_type_set_contains(&set, XFT_PE64)) fileType = XFT_PE64;
        else if (fileType == XFT_CLI_ASSEMBLY) {
            xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Managed scan requires a PE image");
            goto scan_done;
        }
    } else if (fileType == XFT_ELF) {
        if (xx_scan_file_type_set_contains(&set, XFT_ELF32)) fileType = XFT_ELF32;
        else if (xx_scan_file_type_set_contains(&set, XFT_ELF64)) fileType = XFT_ELF64;
    } else if (fileType == XFT_MACHO) {
        if (xx_scan_file_type_set_contains(&set, XFT_MACHO32)) fileType = XFT_MACHO32;
        else if (xx_scan_file_type_set_contains(&set, XFT_MACHO64)) fileType = XFT_MACHO64;
    }
    bIsCliAssembly = xx_scan_file_type_set_contains(&set, XFT_CLI_ASSEMBLY);

    if (selectedType != XFT_UNKNOWN) {
        /* Choosing Binary must not silently add a COM pass or .NET scripts.
         * Each explicit choice is an independent interpretation of the file. */
        bIsCliAssembly = bIsCliAssembly && (fileType == XFT_PE32 || fileType == XFT_PE64);
        nResult = scan_run_pass(device, fileType, bIsCliAssembly, pDb, pOptions, pResult, 1, pd, debug);
    } else if (fileType == XFT_COM) {
        /* XScanEngine::scanProcess's FT_COM arm is two passes: a deep scan
         * runs the FT_BINARY scripts first, and the FT_COM pass adds its
         * "Unknown" record only when that produced nothing. The binary
         * records come first in the merged list. Each pass retains its own
         * output header, while the initial file type remains COM.          */
        xx_scan_result binResult;
        int bIsBinary = 0;

        x_memset(&binResult, 0, sizeof(binResult));

        if (pOptions->deep_scan) {
            if (!scan_run_pass(device, XFT_BINARY, bIsCliAssembly, pDb, pOptions, &binResult, 0, pd, debug)) {
                xx_scan_result_free(&binResult);
                goto scan_done;
            }
            bIsBinary = (binResult.nCount > 0);
        }

        nResult = scan_run_pass(device, XFT_COM, bIsCliAssembly, pDb, pOptions, pResult, !bIsBinary, pd, debug);

        result_merge(pResult, &binResult, 1);
        pResult->file_type = XX_FILE_TYPE_COM;
        pResult->compatibility_file_type = (uint32_t)XFT_COM;
    } else if ((fileType == XFT_BINARY) && (device_size <= XX_SCAN_COM_MAX_SIZE)) {
        /* XScanEngine::scanProcess's last arm - everything no format claimed
         * - offers the file to the COM scripts before the binary ones, which
         * is the only way the 248 COM signatures run on a file that is not
         * named *.com. XCOM::isValid is nothing but the size test above: a
         * COM image loads at 0x100 and cannot cross the 64 KiB segment.
         *
         * The reference copies the options and clears the verbose flag for
         * that pass, so the COM "Operation system" line stays out, and never
         * lets it add "Unknown". Its records are kept only when one of them
         * is neither an operating system nor a format, and that same answer
         * is what suppresses the binary pass's own "Unknown".              */
        xx_scan_result comResult;
        xx_scan_options comOptions = *pOptions;
        int bIsCom = 0;

        x_memset(&comResult, 0, sizeof(comResult));
        comOptions.verbose = 0;

        if (!scan_run_pass(device, XFT_COM, bIsCliAssembly, pDb, &comOptions, &comResult, 0, pd, debug)) {
            xx_scan_result_free(&comResult);
            goto scan_done;
        }
        bIsCom = com_has_non_generic(&comResult);

        nResult = scan_run_pass(device, XFT_BINARY, bIsCliAssembly, pDb, pOptions, pResult, !bIsCom, pd, debug);

        if (bIsCom) {
            result_merge(pResult, &comResult, 0);
        } else {
            xx_scan_result_free(&comResult);
        }
    } else {
        nResult = scan_run_pass(device, fileType, bIsCliAssembly, pDb, pOptions, pResult, 1, pd, debug);
    }

scan_done:

#if defined(XXFC_BUILD_SHARED)
    if (bSoftOom) {
        if (cd_alloc_oom()) {
            xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot allocate DIE scan state");
            nResult = 0;
        }

        cd_alloc_end_soft_oom();
    }
#endif

    return nResult;
}

int die_engine_detect_file_types(const char *pFileName, die_engine_file_type_fn pTypeFn, void *pUserData)
{
    xx_io_device *source;
    xx_io_device *device;
    xx_scan_file_type_set set;
    XFileType preferred;
    int i;
    int failed;

    if (!pFileName || !pFileName[0]) return 0;
    source = xx_io_file_open(pFileName, "rb");
    if (!source) return 0;
    device = xx_io_buffer_open_auto(source, 0, xx_get_file_memory_limit(), true);
    if (!device) {
        xx_io_close(source);
        return 0;
    }
    if (!xx_scan_file_type_set_detect(device, pFileName, &set)) {
        xx_io_close(device);
        return 0;
    }
    preferred = xx_scan_pick_file_type(device, pFileName, &set);
    failed = xx_io_buffer_failed(device);
    xx_io_close(device);
    if (failed) return 0;

    if (pTypeFn) {
        pTypeFn(preferred, pUserData);
        for (i = XFT_COUNT - 1; i > XFT_UNKNOWN; --i) {
            if (i != (int)preferred && xx_scan_file_type_set_contains(&set, (XFileType)i)) pTypeFn((XFileType)i, pUserData);
        }
    }
    return 1;
}

int die_engine_scan_pass_device(xx_io_device *device, DBase *database, xx_scan_options *options,
                                XFileType type, xx_scan_result *result, xx_pd_struct *pd)
{
    return scan_engine_run(device, database, options, type, result, pd, NULL);
}

/* A debugger owns a single target/rule execution, not archive recursion. Keep
 * the standard scan-pass construction, format objects and cleanup unchanged. */
int die_engine_scan_file_type_debug_pd(const char *file_name, DBase *database,
    xx_scan_options *options, XFileType file_type, xx_scan_result *result,
    xx_pd_struct *progress, const die_engine_debug_config *config)
{
    xx_scan_options local;
    xx_pd_struct private_progress = {0};
    xx_pd_struct *pd = progress ? progress : &private_progress;
    xx_io_device *source = NULL;
    xx_io_device *view = NULL;
    int64_t total, size;
    int scanned = 0;
    int i;

    if (!result) return 0;
    x_memset(result, 0, sizeof(*result));
    if (!file_name || !file_name[0] || !database || !options || !config) {
        xx_pd_set_error(pd, XXFC_ERR_NULL_PARAM, "Missing DIE debugger argument");
        goto done;
    }
    if (config->size < sizeof(*config) || config->version != DIE_ENGINE_DEBUG_VERSION ||
        (unsigned int)file_type >= (unsigned int)XFT_COUNT) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid DIE debugger configuration");
        goto done;
    }
    local = *options;
    local.file_name = file_name;
    if (local.recursive_scan || local.first_wrapper_only || local.all_types_scan ||
        local.overlay_scan || local.resources_scan || local.archives_scan) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "DIE debugger requires a single input pass");
        goto done;
    }
    if (local.offset < 0 || local.size < -1 ||
        (local.size >= 0 && local.offset > INT64_MAX - local.size)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid DIE debugger input range");
        goto done;
    }
    if (xx_pd_is_stopped(pd)) goto done;
    source = xx_io_file_open(file_name, "rb");
    if (!source) {
        xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot open DIE debugger input");
        goto done;
    }
    total = xx_io_total_size(source);
    if (total < 0) {
        xx_pd_set_error(pd, XXFC_ERR_IO, "Cannot determine DIE debugger input size");
        goto done;
    }
    size = local.size < 0 ? total - local.offset : local.size;
    if (local.offset > total || size > total - local.offset) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "DIE debugger range exceeds input");
        goto done;
    }
    view = xx_io_sub_open_ro(source, local.offset, size);
    if (!view) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot create DIE debugger input view");
        goto done;
    }
    scanned = scan_engine_run(view, database, &local, file_type, result, pd, config);
    for (i = 0; i < result->nCount; ++i) {
        xx_scan_record *record = &result->pRecords[i];
        if (record->offset < 0 || record->offset > INT64_MAX - local.offset) {
            xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "DIE debugger record offset overflows");
            scanned = 0;
            break;
        }
        record->offset += local.offset;
    }
done:
    if (view) xx_io_close(view);
    if (source) xx_io_close(source);
    if (!scanned && pd->error_string[0]) (void)xx_scan_result_add_error(result, pd->error_string, NULL);
    return scanned && !xx_pd_is_stopped(pd);
}
