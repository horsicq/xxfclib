/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_dex.c - DEX script API dispatch. */

#include "xx_die_engine_api_internal.h"
#include "xxfclib/regex/xx_regex.h"
#include "../js/xx_js_internal.h"

static const ApiEntry g_dex_api_table[] = {{"isDexStringPresent", A_isDexStringPresent, 1},
                                           {"isDexItemStringPresent", A_isDexItemStringPresent, 1},
                                           {"getMapItemsHash", A_getMapItemsHash, 0},
                                           /* APKiD DEX */
                                           {"isStringPoolSorted", A_isStringPoolSorted, 0},
                                           {"getUnicodeMemberNameCount", A_getUnicodeMemberNameCount, 1},
                                           {"isDexStringPresentExp", A_isDexStringPresentExp, 1},
                                           {"isStringPoolSortedByHeader", A_isStringPoolSortedByHeader, 0},
                                           {NULL, A_NONE, 0}};

void die_engine_api_register_dex(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_dex_api_table);
}

/* ------------------------------------------------------------ APKiD DEX */

/* A 32-bit value of the scanned DEX, 0 when it is not wholly inside the
 * file (XBinary::read_uint32 reads nothing there). */
static cd_u32 dex_u32(DieEngine *pEngine, cd_i64 nOffset, int bBigEndian)
{
    cd_u8 raw[4];
    cd_i64 nTotal = xx_io_total_size(pEngine->device);

    if ((nOffset < 0) || (nTotal < 4) || (nOffset > nTotal - 4) || !xx_io_read_at(pEngine->device, nOffset, raw, sizeof(raw))) {
        return 0;
    }

    if (bBigEndian) {
        return ((cd_u32)raw[0] << 24) | ((cd_u32)raw[1] << 16) | ((cd_u32)raw[2] << 8) | (cd_u32)raw[3];
    }

    return (cd_u32)raw[0] | ((cd_u32)raw[1] << 8) | ((cd_u32)raw[2] << 16) | ((cd_u32)raw[3] << 24);
}

static cd_u32 dex_u16(DieEngine *pEngine, cd_i64 nOffset, int bBigEndian)
{
    cd_u8 raw[2];
    cd_i64 nTotal = xx_io_total_size(pEngine->device);

    if ((nOffset < 0) || (nTotal < 2) || (nOffset > nTotal - 2) || !xx_io_read_at(pEngine->device, nOffset, raw, sizeof(raw))) {
        return 0;
    }

    return bBigEndian ? (((cd_u32)raw[0] << 8) | raw[1]) : ((cd_u32)raw[0] | ((cd_u32)raw[1] << 8));
}

/* DEX.isStringPoolSorted(): XDEX::isStringPoolSorted over XDEX::getMapItems,
 * as the Qt DEX_Script computes it. The string_ids table is the first map
 * item of type TYPE_STRING_ID_ITEM; its count is clamped to the file; the
 * string data offsets are compared as signed 32-bit values starting from 0,
 * and the pool is sorted unless one offset is below the previous one. */
static int dex_is_string_pool_sorted(DieEngine *pEngine)
{
    cd_i64 nSize = xx_io_total_size(pEngine->device);
    int bBigEndian = (dex_u32(pEngine, 0x28, 0) == 0x78563412U);
    cd_i64 nMapOff = (cd_i64)dex_u32(pEngine, 0x34, bBigEndian);
    cd_u32 nDeclared = 0;
    cd_i64 nAvail = 0;
    cd_i64 nItems = 0;
    cd_i64 nStringsCount = 0;
    cd_i64 nStringsOffset = 0;
    cd_i64 nCount = 0;
    cd_i64 i = 0;
    cd_i32 nPrev = 0;

    if ((nMapOff == 0) || (nMapOff > nSize - 4)) {
        return 1;
    }

    nDeclared = dex_u32(pEngine, nMapOff, bBigEndian);
    nAvail = nSize - (nMapOff + 4);
    nItems = (nAvail >= 0) ? (nAvail / 12) : 0;
    if (nItems > 0x10000) nItems = 0x10000;
    if (nItems > (cd_i64)nDeclared) nItems = (cd_i64)nDeclared;

    for (i = 0; i < nItems; i++) {
        cd_i64 nItem = nMapOff + 4 + i * 12;

        if (dex_u16(pEngine, nItem, bBigEndian) == 0x0001) {
            nStringsCount = (cd_i64)dex_u32(pEngine, nItem + 4, bBigEndian);
            nStringsOffset = (cd_i64)dex_u32(pEngine, nItem + 8, bBigEndian);
            break;
        }
    }

    if ((nStringsOffset <= 0) || (nStringsOffset >= nSize)) {
        return 1;
    }

    nCount = (nSize - nStringsOffset) / 4;
    if (nCount > nStringsCount) nCount = nStringsCount;

    for (i = 0; (i < nCount) && !xx_pd_is_stopped(pEngine->pd); i++) {
        cd_i32 nValue = (cd_i32)dex_u32(pEngine, nStringsOffset + i * 4, bBigEndian);

        if (nValue < nPrev) {
            return 0;
        }

        nPrev = nValue;
    }

    return 1;
}

/* The YARA 3.11 legacy dex module values of the whole scanned buffer
 * (xx_dex_legacy_analyze), computed once per scan. */
static const xx_dex_legacy *dex_legacy(DieEngine *pEngine)
{
    if (!pEngine->bDexLegacyDone) {
        cd_i64 nSize = xx_io_total_size(pEngine->device);

        pEngine->bDexLegacyDone = 1;
        x_memset(&pEngine->dexLegacy, 0, sizeof(pEngine->dexLegacy));

        /* Files above 2 GiB - 1 are not analysed (the Qt engine cannot read them in
         * one QByteArray); both engines then report 0. */
        if ((nSize > 0) && (nSize <= 0x7FFFFFFF) && ((cd_u64)nSize <= (cd_u64)SIZE_MAX)) {
            cd_u8 *pData = (cd_u8 *)cd_malloc((size_t)nSize);

            if (pData) {
                if (xx_io_read_at(pEngine->device, 0, pData, (size_t)nSize)) {
                    if (!xx_dex_legacy_analyze(pData, (size_t)nSize, &pEngine->dexLegacy)) {
                        x_memset(&pEngine->dexLegacy, 0, sizeof(pEngine->dexLegacy));
                    }
                }

                cd_free(pData);
            }
        }
    }

    return &pEngine->dexLegacy;
}

/* DEX.isDexStringPresentExp(pattern): true if a string of the DEX string
 * table matches the regular expression with a non-empty captured(0), the
 * XBinary::isStringInListPresentExp test Qt runs over XDEX::getStrings.
 * Strings are passed as their MUTF-8 bytes. */
static int dex_string_present_exp(DieEngine *pEngine, const char *pPattern)
{
    xx_regex *pRegExp = NULL;
    char *pError = NULL;
    cd_i32 *pnCaps = NULL;
    char *pBuffer = NULL;
    size_t nCapacity = 0;
    cd_u32 nCount = 0;
    cd_u32 i = 0;
    int bFound = 0;

    if (!pEngine->bHasDex || (pPattern == NULL)) {
        return 0;
    }

    pRegExp = js_compile_regexp(pPattern, "", &pError);

    if (pError != NULL) {
        cd_free(pError);
    }

    if (pRegExp == NULL) {
        return 0;
    }

    pnCaps = (cd_i32 *)cd_malloc((size_t)(xx_regex_ngroups(pRegExp) + 1) * 2 * sizeof(cd_i32));
    nCount = pEngine->dex.header.string_ids_size;

    for (i = 0; pnCaps && (i < nCount) && !bFound && !pEngine->bStop && !xx_pd_is_stopped(pEngine->pd); i++) {
        xx_dex_string_info info;

        if (!xx_dex_read_string_info(&pEngine->dex, i, (size_t)1 << 24, &info, pEngine->pd)) {
            continue;
        }

        if (info.byte_size + 1 > nCapacity) {
            char *pNew = (char *)cd_malloc(info.byte_size + 1);

            if (!pNew) {
                break;
            }

            cd_free(pBuffer);
            pBuffer = pNew;
            nCapacity = info.byte_size + 1;
        }

        if (!xx_dex_read_string(&pEngine->dex, i, pBuffer, nCapacity, NULL, pEngine->pd)) {
            continue;
        }

        if (xx_regex_exec(pRegExp, pBuffer, info.byte_size, 0, pnCaps)) {
            if (pnCaps[1] > pnCaps[0]) { /* captured(0) non-empty */
                bFound = 1;
            }
        }
    }

    cd_free(pBuffer);
    cd_free(pnCaps);
    xx_regex_free(pRegExp);

    return bFound;
}

bool die_engine_api_dispatch_dex(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    switch (id) {
        case A_isDexStringPresent: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasDex ? xx_dex_string_present(&pEngine->dex, pString) : 0;

            cd_free(pString);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isDexItemStringPresent: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasDex ? xx_dex_item_string_present(&pEngine->dex, pString) : 0;

            cd_free(pString);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_getMapItemsHash: *pResult = js_num((double)(pEngine->bHasDex ? xx_dex_get_map_hash(&pEngine->dex) : 0)); return true;

        /* APKiD DEX */
        case A_isStringPoolSorted: *pResult = js_bool(dex_is_string_pool_sorted(pEngine)); return true;

        case A_getUnicodeMemberNameCount: {
            char *pKind = arg_string(pCtx, nArgc, pArgv, 0);
            const xx_dex_legacy *pLegacy = dex_legacy(pEngine);
            cd_i64 nResult = 0;

            if (pKind && (x_strcmp(pKind, "field") == 0)) {
                nResult = (cd_i64)pLegacy->unicode_field_names;
            } else if (pKind && (x_strcmp(pKind, "method") == 0)) {
                nResult = (cd_i64)pLegacy->unicode_method_names;
            }

            cd_free(pKind);

            *pResult = js_num((double)nResult);
            return true;
        }

        /* DEX.isStringPoolSortedByHeader(): false only when YARA 3.11's dex module
         * parses the file (magic dex 035..038, whole header) and a valid string_ids
         * entry of the header's table has a lower data offset than the previous
         * valid one (CXAPKiD's unsorted_string_pool, APKiD dexlib 1.x). Unlike
         * isStringPoolSorted() it does not go through the map list. */
        case A_isStringPoolSortedByHeader: *pResult = js_bool(!dex_legacy(pEngine)->unsorted_string_pool); return true;

        case A_isDexStringPresentExp: {
            char *pPattern = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = dex_string_present_exp(pEngine, pPattern);

            cd_free(pPattern);

            *pResult = js_bool(bResult);
            return true;
        }

        default: break;
    }

    return false;
}
