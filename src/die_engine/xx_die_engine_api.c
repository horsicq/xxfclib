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

#include "xx_die_engine_api_internal.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/fs/xx_fs.h"
#include "xxfclib/strings/xx_string.h"
#include "../js/xx_js_internal.h"
#include "platforms/xx_die_engine_platform.h"

#include <limits.h>

/* ------------------------------------------------------------ profiling  */

/* -l/--profiling. DiE_Script brackets every detection script and
 * Binary_Script every search primitive with a QElapsedTimer, and both report
 * through warningMessage, which the reference console prints to stdout as
 * "[WARNING] <text>"; a measured step appends " [<n> ms]".
 *
 * Two deliberate differences from the reference. The trace follows -l on its
 * own, where diec also wants -M, because a flag documented as showing
 * profiling information that shows none is the defect this closes; and
 * endTiming() does not report an unknown handle, because nothing in the
 * database calls it and the error would be a new line in -M output.        */

cd_i64 die_engine_profile_start(DieEngine *pEngine)
{
    if (!pEngine->pOptions->profiling) {
        return -1;
    }

    return (cd_i64)x_clock_ms();
}

/* The debugger receives owned execution-thread output through its callback.
 * Its UI adapter copies/sanitizes the borrowed line before returning. */
static void die_engine_debug_log_line(DieEngine *pEngine, const char *prefix, const char *text, const char *suffix)
{
    size_t a = x_strlen(prefix), b = x_strlen(text), c = x_strlen(suffix);
    char *line;
    if (a > SIZE_MAX - b || a + b > SIZE_MAX - c - 1) return;
    line = (char *)cd_malloc(a + b + c + 1);
    if (!line) return;
    x_memcpy(line, prefix, a);
    x_memcpy(line + a, text, b);
    x_memcpy(line + a + b, suffix, c + 1);
    pEngine->pDebug->log(line, pEngine->pDebug->log_user);
    cd_free(line);
}

void die_engine_profile_text(DieEngine *pEngine, const char *pText)
{
    if (!pEngine->pOptions->profiling) {
        return;
    }

    if (pEngine->pDebug && pEngine->pDebug->log)
        die_engine_debug_log_line(pEngine, "[WARNING] ", pText, "\n");
    else
        x_printf("[WARNING] %s\n", pText);
}

X_PRINTF_LIKE(3, 4) void die_engine_profile_end(DieEngine *pEngine, cd_i64 nStart, const char *pFormat, ...)
{
    char sInfo[1024];
    X_VA_LIST args;

    if ((!pEngine->pOptions->profiling) || (nStart < 0)) {
        return;
    }

    X_VA_START(args, pFormat);
    x_vsnprintf(sInfo, sizeof(sInfo), pFormat, args);
    X_VA_END(args);

    if (pEngine->pDebug && pEngine->pDebug->log) {
        char suffix[64];
        x_snprintf(suffix, sizeof(suffix), " [%lld ms]\n", (long long)((cd_i64)x_clock_ms() - nStart));
        die_engine_debug_log_line(pEngine, "[WARNING] ", sInfo, suffix);
    } else {
        x_printf("[WARNING] %s [%lld ms]\n", sInfo, (long long)((cd_i64)x_clock_ms() - nStart));
    }
}

void die_engine_profile_free(DieEngine *pEngine)
{
    cd_free(pEngine->pTimings);
    pEngine->pTimings = NULL;
    pEngine->nTimingCount = 0;
    pEngine->nTimingCapacity = 0;
}

/* -------------------------------------------------------------- helpers  */

char *die_engine_api_arg_string(JSCtx *pCtx, int nArgc, JSVal *pArgv, int nIndex)
{
    if (nIndex >= nArgc) {
        return cd_strdup("");
    }

    return js_to_cstr(pCtx, pArgv[nIndex]);
}

cd_i64 die_engine_api_arg_i64(JSCtx *pCtx, int nArgc, JSVal *pArgv, int nIndex, cd_i64 nDefault)
{
    if ((nIndex >= nArgc) || (pArgv[nIndex].tag == JT_UNDEF)) {
        return nDefault;
    }

    return js_to_int64(pCtx, pArgv[nIndex]);
}

int die_engine_api_arg_exact_u32(int nArgc, JSVal *pArgv, int nIndex, cd_u32 *pValue)
{
    double dValue;
    cd_u32 nValue;
    if (!pValue || nIndex >= nArgc || pArgv[nIndex].tag != JT_NUM) return 0;
    dValue = pArgv[nIndex].u.n;
    if (!(dValue >= 0.0 && dValue <= 4294967295.0)) return 0;
    nValue = (cd_u32)dValue;
    if ((double)nValue != dValue) return 0;
    *pValue = nValue;
    return 1;
}

/* A JS number coerced to unsigned 64-bit the way the Qt engine hands a double
 * to a quint64 slot parameter: truncate toward zero, saturating out of range.
 * Used by the Util 64-bit helpers. */
cd_u64 die_engine_api_arg_u64(JSCtx *pCtx, int nArgc, JSVal *pArgv, int nIndex)
{
    double dValue = 0;

    if ((nIndex >= nArgc) || (pArgv[nIndex].tag == JT_UNDEF)) {
        return 0;
    }

    dValue = js_to_number(pCtx, pArgv[nIndex]);

    if (dValue != dValue) {
        return 0; /* NaN */
    }

    if (dValue <= 0.0) {
        if (dValue <= -9223372036854775808.0) {
            return (cd_u64)0x8000000000000000ULL; /* <= -2^63 or -inf: out of int64 range */
        }

        return (cd_u64)(cd_i64)dValue; /* two's-complement of the truncated value */
    }

    if (dValue >= 18446744073709551616.0) {
        return (cd_u64)0xFFFFFFFFFFFFFFFFULL; /* >= 2^64 */
    }

    return (cd_u64)dValue;
}

int die_engine_api_arg_bool(JSCtx *pCtx, int nArgc, JSVal *pArgv, int nIndex, int bDefault)
{
    if ((nIndex >= nArgc) || (pArgv[nIndex].tag == JT_UNDEF)) {
        return bDefault;
    }

    return js_to_bool(pCtx, pArgv[nIndex]);
}

JSVal die_engine_api_str_take(JSCtx *pCtx, char *pString)
{
    JSVal result = js_str(pCtx, pString ? pString : "");

    cd_free(pString);

    return result;
}

/* Release shared-library strings through their native allocator. */
JSVal die_engine_api_str_take_native(JSCtx *pCtx, char *pString)
{
    JSVal result = js_str(pCtx, pString ? pString : "");

    xx_str_free(pString);

    return result;
}

JSVal die_engine_api_native_str_take(JSCtx *ctx, char *text)
{
    JSVal result = js_str(ctx, text ? text : "");
    xx_str_free(text);
    return result;
}

/* XBinary::valueToHexEx: the field is only as wide as the value needs, in
 * lowercase - two hex digits below 0xFF, then four, eight and sixteen.     */
void die_engine_api_value_to_hex_ex(cd_u64 nValue, char *pBuffer, size_t nBufferSize)
{
    int nWidth = 2;

    if (nValue >= 0xFFFFFFFFull) {
        nWidth = 16;
    } else if (nValue >= 0xFFFFull) {
        nWidth = 8;
    } else if (nValue >= 0xFFull) {
        nWidth = 4;
    }

    x_snprintf(pBuffer, nBufferSize, "%0*llx", nWidth, (unsigned long long)nValue);
}

char *die_engine_api_clean_string(const char *pString)
{
    CDBuf buf;
    size_t i = 0;
    size_t nSize = pString ? x_strlen(pString) : 0;

    cdbuf_init(&buf);

    for (i = 0; i < nSize; i++) {
        unsigned char nChar = (unsigned char)pString[i];

        if ((nChar >= 0x20) && (nChar != 0x7F)) {
            cdbuf_append_ch(&buf, (char)nChar);
        }
    }

    return cdbuf_detach(&buf, NULL);
}

/* ------------------------------------------------------ format accessors */

int die_engine_api_has_legacy_script_map(const xx_memory_map *map)
{
    return map->file_type == XX_FILE_TYPE_NE ||
           (map->file_type == XX_FILE_TYPE_MSDOS && map->mode == XX_MEMORY_MAP_MODE_SEGMENTS);
}

cd_i64 die_engine_api_script_entry_point_offset(DieEngine *pEngine)
{
    if (has_legacy_script_map(pEngine->pMap)) {
        /* XBinary::getEntryPointOffset: the map's entry address through the
         * map. The DOS script map already holds 0x10000000 + CS:IP, the
         * address of the entry inside its loaded image (as XMSDOS does).
         * Mapping also rejects an entry that falls into an overlay. */
        return xx_memory_map_address_to_offset(pEngine->pMap, pEngine->pMap->entry_point_address);
    }

    if (pEngine->bHasElf) {
        return pEngine->elf.nEntryPointOffset;
    }

    if (pEngine->bHasMach) {
        return pEngine->mach.nEntryPointOffset;
    }

    if (!pEngine->bHasPE) {
        return -1;
    }

    /* Binary_Script captures its entry-point cache through addressToOffset,
     * whose reverse physical lookup also maps RVA zero to the image header. */
    return xx_memory_map_address_to_offset(&pEngine->pe.map, pEngine->pe.nImageBase + pEngine->pe.nAddressOfEntryPoint);
}

cd_i64 die_engine_api_legacy_overlay_offset(const xx_memory_map *map)
{
    cd_i64 end = 0;
    size_t i;

    for (i = 0; i < map->record_count; ++i) {
        const xx_memory_record *record = &map->records[i];
        if (record->offset >= 0 && !record->is_virtual && record->file_part != XX_FILE_PART_OVERLAY && record->offset + record->size > end) {
            end = record->offset + record->size;
        }
    }
    /* The reference returns EOF, rather than -1, for an empty overlay. */
    return end > 0 && end <= map->binary_size ? end : -1;
}

/* Clamps an offset/size pair to the file, mirroring _fixOffsetAndSize.
 * Scripts may pass arbitrary numbers, so the arithmetic is overflow-safe.  */
void die_engine_api_fix_offset_size(DieEngine *pEngine, cd_i64 *pnOffset, cd_i64 *pnSize)
{
    if ((*pnOffset >= 0) && (*pnOffset < xx_io_total_size(pEngine->device))) {
        if ((*pnSize < 0) || (*pnSize > xx_io_total_size(pEngine->device) - *pnOffset)) {
            *pnSize = xx_io_total_size(pEngine->device) - *pnOffset;
        }
    }
}

/* ------------------------------------------------------------ dispatcher */

static JSVal api_dispatch(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    ApiId id = (ApiId)(size_t)pUser;
    JSVal result = js_undefined();

    (void)thisVal;

    if (id >= A_getSize && id <= A_getListOfCompressionMethods) {
        if (die_engine_api_dispatch_binary(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
        if (die_engine_api_dispatch_msdos(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
        if (die_engine_api_dispatch_iso9660(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
        if (die_engine_api_dispatch_archive(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
        if (die_engine_api_dispatch_jar(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
        if (die_engine_api_dispatch_npm(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
        if (die_engine_api_dispatch_pyc(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    } else if (id >= A_isRichSignaturePresent && id <= A_isDosStubPresent) {
        if (die_engine_api_dispatch_msdos(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    } else if (id >= A_getNumberOfSections && id <= A_getDebugDataSize) {
        if (die_engine_api_dispatch_dotnet(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
        if (pEngine->bHasElf && die_engine_api_dispatch_elf(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
        if (pEngine->bHasMach && die_engine_api_dispatch_mach(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
        if (die_engine_api_dispatch_pe(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    } else if (id >= A_getComment && id <= A_getExifCameraName) {
        if (die_engine_api_dispatch_jpeg(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    } else if (id >= A_getAndroidManifestRecord && id <= A_getAndroidManifest) {
        if (die_engine_api_dispatch_apk(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    } else if (id >= A_getHeaderCommentAsHex && id <= A_getPermissions) {
        if (die_engine_api_dispatch_pdf(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    } else if (id >= A_isDexStringPresent && id <= A_getMapItemsHash) {
        if (die_engine_api_dispatch_dex(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    } else if (id >= A_isStringInTablePresent && id <= A_isNotePresent) {
        if (die_engine_api_dispatch_elf(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    } else if (id == A_getLibraryCurrentVersion) {
        if (die_engine_api_dispatch_mach(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    }

    if (die_engine_api_dispatch_binary(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (die_engine_api_dispatch_msdos(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (die_engine_api_dispatch_dotnet(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (pEngine->bHasElf && die_engine_api_dispatch_elf(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (pEngine->bHasMach && die_engine_api_dispatch_mach(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (die_engine_api_dispatch_pe(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (die_engine_api_dispatch_jpeg(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (die_engine_api_dispatch_apk(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (die_engine_api_dispatch_pdf(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (die_engine_api_dispatch_dex(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (die_engine_api_dispatch_iso9660(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (die_engine_api_dispatch_archive(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (die_engine_api_dispatch_jar(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (die_engine_api_dispatch_npm(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;
    if (die_engine_api_dispatch_pyc(pEngine, pCtx, id, nArgc, pArgv, &result)) return result;

    return js_undefined();
}


void die_engine_api_register_entries(JSCtx *pCtx, JSVal object, const ApiEntry *pEntries)
{
    int i = 0;

    for (i = 0; pEntries[i].pName; i++) {
        js_def_method(pCtx, object, pEntries[i].pName, api_dispatch, pEntries[i].nArgc, (void *)(size_t)pEntries[i].id);
    }
}


/* ------------------------------------------------------- global helpers  */

static void engine_add_result(DieEngine *pEngine, const char *pType, const char *pName, const char *pVersion, const char *pInfo, const char *pFormatId);

/* The generated bundled audio rule evaluates each marked sName/sname RHS
 * through this function. It returns the original value, while remembering
 * the name prefix to prevent a later unmarked branch inheriting a stale tag.
 * PSF and NSF append a version-specific suffix after this assignment. */
static JSVal fn_music_fmt(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pId = arg_string(pCtx, nArgc, pArgv, 0);
    char *pName = arg_string(pCtx, nArgc, pArgv, 1);
    (void)thisVal;
    (void)pUser;
    if (pEngine && pEngine->pCurrentScript && x_strcmp(pEngine->pCurrentScript, "audio.1.sg") == 0) {
        cd_free(pEngine->pCurrentFormatId);
        cd_free(pEngine->pCurrentFormatName);
        pEngine->pCurrentFormatId = pId;
        pEngine->pCurrentFormatName = pName;
    } else {
        cd_free(pId);
        cd_free(pName);
    }
    return nArgc > 1 ? js_dup(pArgv[1]) : js_undefined();
}

static JSVal fn_set_result(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pType = arg_string(pCtx, nArgc, pArgv, 0);
    char *pName = arg_string(pCtx, nArgc, pArgv, 1);
    char *pVersion = arg_string(pCtx, nArgc, pArgv, 2);
    char *pInfo = arg_string(pCtx, nArgc, pArgv, 3);

    (void)thisVal;
    (void)pUser;

    engine_add_result(pEngine, pType, pName, pVersion, pInfo,
                      pEngine->pCurrentFormatId && pEngine->pCurrentFormatName && pEngine->pCurrentFormatName[0] &&
                              x_strncmp(pName, pEngine->pCurrentFormatName, x_strlen(pEngine->pCurrentFormatName)) == 0
                          ? pEngine->pCurrentFormatId
                          : NULL);
    cd_free(pEngine->pCurrentFormatId);
    cd_free(pEngine->pCurrentFormatName);
    pEngine->pCurrentFormatId = NULL;
    pEngine->pCurrentFormatName = NULL;

    cd_free(pType);
    cd_free(pName);
    cd_free(pVersion);
    cd_free(pInfo);

    return js_undefined();
}

static JSVal fn_is_result_present(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pType = arg_string(pCtx, nArgc, pArgv, 0);
    char *pName = arg_string(pCtx, nArgc, pArgv, 1);
    int i = 0;
    int bResult = 0;

    (void)thisVal;
    (void)pUser;

    for (i = 0; i < pEngine->pResult->nCount; i++) {
        xx_scan_record *pRecord = &pEngine->pResult->pRecords[i];

        if ((cd_stricmp_ascii(pRecord->type, pType) == 0) && ((cd_stricmp_ascii(pRecord->name, pName) == 0) || (pName[0] == 0))) {
            bResult = 1;
            break;
        }
    }

    cd_free(pType);
    cd_free(pName);

    return js_bool(bResult);
}

static JSVal fn_get_number_of_results(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pType = arg_string(pCtx, nArgc, pArgv, 0);
    int i = 0;
    int nResult = 0;

    (void)thisVal;
    (void)pUser;

    for (i = 0; i < pEngine->pResult->nCount; i++) {
        if ((cd_stricmp_ascii(pEngine->pResult->pRecords[i].type, pType) == 0) || (pType[0] == 0)) {
            nResult++;
        }
    }

    cd_free(pType);

    return js_num((double)nResult);
}

static JSVal fn_remove_result(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pType = arg_string(pCtx, nArgc, pArgv, 0);
    char *pName = arg_string(pCtx, nArgc, pArgv, 1);
    int i = 0;

    (void)thisVal;
    (void)pUser;

    pEngine->pBlackList = (BLRecord *)cd_realloc(pEngine->pBlackList, (size_t)(pEngine->nBlackListCount + 1) * sizeof(BLRecord));
    pEngine->pBlackList[pEngine->nBlackListCount].pType = cd_strdup(pType);
    pEngine->pBlackList[pEngine->nBlackListCount].pName = cd_strdup(pName);
    pEngine->nBlackListCount++;

    for (i = 0; i < pEngine->pResult->nCount; i++) {
        xx_scan_record *pRecord = &pEngine->pResult->pRecords[i];

        if ((cd_stricmp_ascii(pRecord->type, pType) == 0) && ((cd_stricmp_ascii(pRecord->name, pName) == 0) || (pRecord->name[0] == 0))) {
            cd_free((void *)pRecord->type);
            cd_free((void *)pRecord->name);
            cd_free((void *)pRecord->version);
            cd_free((void *)pRecord->info);
            cd_free((void *)pRecord->format_id);
            cd_free((void *)pRecord->archive_member);
            cd_free((void *)pRecord->source_ref);
            x_memmove(pEngine->pResult->pRecords + i, pEngine->pResult->pRecords + i + 1, (size_t)(pEngine->pResult->nCount - i - 1) * sizeof(xx_scan_record));
            pEngine->pResult->nCount--;
            break;
        }
    }

    cd_free(pType);
    cd_free(pName);

    return js_undefined();
}

static JSVal fn_include_script(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pScript = arg_string(pCtx, nArgc, pArgv, 0);
    int i = 0;
    int bFound = 0;

    (void)thisVal;
    (void)pUser;

    for (i = 0; i < pEngine->pDb->nCount; i++) {
        DBSignature *pRecord = &pEngine->pDb->pRecords[i];

        if ((pRecord->fileType == XFT_UNKNOWN) && (cd_stricmp_ascii(pRecord->pName, pScript) == 0)) {
            const char *source_name = pEngine->pDebug && pRecord->pFilePath && pRecord->pFilePath[0]
                ? pRecord->pFilePath : pRecord->pName;
            if (js_is_bytecode(pRecord->pText, pRecord->nSize)) {
                js_eval_nested_bytecode(pCtx, pRecord->pText, pRecord->nSize, source_name);
            } else {
                js_eval_nested(pCtx, pRecord->pText, source_name);
            }
            bFound = 1;
            break;
        }
    }

    if ((!bFound) && pEngine->pOptions->show_messages) {
        x_fprintf(x_stderr(), "Cannot find: %s\n", pScript);
    }

    cd_free(pScript);

    return js_undefined();
}

static JSVal fn_log(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pText = arg_string(pCtx, nArgc, pArgv, 0);

    (void)thisVal;
    (void)pUser;

    if (pEngine->pOptions->profiling) {
        cd_i64 now = (cd_i64)x_clock_ms();
        cd_i64 delta = (pEngine->nLastLogTime > 0) ? (now - pEngine->nLastLogTime) : 0;
        pEngine->nLastLogTime = now;
        if (pEngine->pDebug && pEngine->pDebug->log) {
            char suffix[64];
            x_snprintf(suffix, sizeof(suffix), " [+%lld ms]\n", (long long)delta);
            die_engine_debug_log_line(pEngine, "[WARNING] LOG ", pText ? pText : "", suffix);
        } else {
            x_printf("[WARNING] LOG %s [+%lld ms]\n", pText ? pText : "", (long long)delta);
        }
    }

    if (pEngine->pDebug && pEngine->pDebug->log) {
        die_engine_debug_log_line(pEngine, "", pText ? pText : "", "\n");
    } else if (pEngine->pOptions->show_messages) {
        x_fprintf(x_stderr(), "%s\n", pText ? pText : "");
    }

    cd_free(pText);

    return js_undefined();
}

static JSVal fn_is_stop(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_bool(pEngine->bStop || xx_pd_is_stopped(pEngine->pd));
}

static JSVal fn_break_scan(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);

    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    pEngine->bStop = 1;

    return js_undefined();
}

static JSVal fn_true(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pCtx;
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;

    return js_bool((int)(size_t)pUser);
}

static JSVal fn_engine_version(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_str(pCtx, X_APPLICATIONVERSION);
}

static JSVal fn_get_os(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_str(pCtx, xx_die_engine_platform_os_name());
}

static JSVal fn_noop(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pCtx;
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_undefined();
}

/* ------------------------------------------------------- result handling */

static void engine_add_result(DieEngine *pEngine, const char *pType, const char *pName, const char *pVersion, const char *pInfo, const char *pFormatId)
{
    xx_scan_result *pResult = pEngine->pResult;
    xx_scan_record *pRecord = NULL;
    int i = 0;

    for (i = 0; i < pEngine->nBlackListCount; i++) {
        if ((cd_stricmp_ascii(pEngine->pBlackList[i].pType, pType) == 0) &&
            ((cd_stricmp_ascii(pEngine->pBlackList[i].pName, pName) == 0) || (pEngine->pBlackList[i].pName[0] == 0))) {
            return;
        }
    }

    if (pResult->nCount + 1 > pResult->nCapacity) {
        pResult->nCapacity = pResult->nCapacity ? (pResult->nCapacity * 2) : 16;
        pResult->pRecords = (xx_scan_record *)cd_realloc(pResult->pRecords, (size_t)pResult->nCapacity * sizeof(xx_scan_record));
    }

    pRecord = &pResult->pRecords[pResult->nCount++];
    die_engine_record_init(pEngine, pRecord);
    pRecord->type = cd_strdup(pType);
    pRecord->name = cd_strdup(pName);
    pRecord->version = cd_strdup(pVersion);
    pRecord->info = cd_strdup(pInfo);
    pRecord->format_id = pFormatId ? cd_strdup(pFormatId) : NULL;
    pRecord->source_ref = pEngine->pCurrentScriptPath ? cd_strdup(pEngine->pCurrentScriptPath) : NULL;
    pRecord->priority = xx_scan_type_to_prio(pType);
    pRecord->is_heuristic = ((x_strlen(pType) > 1) && (pType[0] == '~')) ? 1 : 0;
    pRecord->is_aggressive_heuristic = ((x_strlen(pType) > 1) && (pType[0] == '!')) ? 1 : 0;
}

/* ------------------------------------------------------------- install  */



void die_engine_install_api(DieEngine *pEngine)
{
    JSCtx *pCtx = pEngine->pJs;
    JSVal object = js_new_object(pCtx);
    JSVal global = js_global(pCtx);
    die_engine_api_register_amiga(pCtx, object);
    die_engine_api_register_apk(pCtx, object);
    die_engine_api_register_archive(pCtx, object);
    die_engine_api_register_atarist(pCtx, object);
    die_engine_api_register_binary(pCtx, object);
    die_engine_api_register_cfbf(pCtx, object);
    die_engine_api_register_com(pCtx, object);
    die_engine_api_register_dex(pCtx, object);
    die_engine_api_register_dos16m(pCtx, object);
    die_engine_api_register_dos4g(pCtx, object);
    die_engine_api_register_dotnet(pCtx, object);
    die_engine_api_register_elf(pCtx, object);
    die_engine_api_register_image(pCtx, object);
    die_engine_api_register_ipa(pCtx, object);
    die_engine_api_register_iso9660(pCtx, object);
    die_engine_api_register_jar(pCtx, object);
    die_engine_api_register_javaclass(pCtx, object);
    die_engine_api_register_jpeg(pCtx, object);
    die_engine_api_register_le(pCtx, object);
    die_engine_api_register_lx(pCtx, object);
    die_engine_api_register_mach(pCtx, object);
    die_engine_api_register_machofat(pCtx, object);
    die_engine_api_register_msdos(pCtx, object);
    die_engine_api_register_ne(pCtx, object);
    die_engine_api_register_npm(pCtx, object);
    die_engine_api_register_pdf(pCtx, object);
    die_engine_api_register_pe(pCtx, object);
    die_engine_api_register_png(pCtx, object);
    die_engine_api_register_pyc(pCtx, object);
    die_engine_api_register_zip(pCtx, object);

    /* The scripts address the format object by several names: always
     * `Binary`, plus the name of the detected format so that the per-format
     * `_init` file (`var File = PE;`) resolves.                            */
    js_set(pCtx, global, "Binary", js_dup(object));

    {
        static const struct {
            XFileType type;
            const char *pName;
        } pAliases[] = {{XFT_PE, "PE"},
                        {XFT_PE32, "PE"},
                        {XFT_PE64, "PE"},
                        {XFT_MSDOS, "MSDOS"},
                        {XFT_NE, "NE"},
                        {XFT_LE, "LE"},
                        {XFT_LX, "LX"},
                        {XFT_COM, "COM"},
                        {XFT_ELF, "ELF"},
                        {XFT_ELF32, "ELF"},
                        {XFT_ELF64, "ELF"},
                        {XFT_MACHO, "MACH"},
                        {XFT_MACHO32, "MACH"},
                        {XFT_MACHO64, "MACH"},
                        {XFT_MACHOFAT, "MACHOFAT"},
                        {XFT_ZIP, "ZIP"},
                        {XFT_JAR, "JAR"},
                        {XFT_APK, "APK"},
                        {XFT_IPA, "IPA"},
                        {XFT_NPM, "NPM"},
                        {XFT_DEX, "DEX"},
                        {XFT_PDF, "PDF"},
                        {XFT_CFBF, "CFBF"},
                        {XFT_JPEG, "Jpeg"},
                        {XFT_PNG, "PNG"},
                        {XFT_RAR, "RAR"},
                        {XFT_ISO9660, "ISO9660"},
                        {XFT_ARCHIVE, "Archive"},
                        {XFT_IMAGE, "Image"},
                        {XFT_AMIGAHUNK, "Amiga"},
                        {XFT_ATARIST, "AtariST"},
                        {XFT_JAVACLASS, "JavaClass"},
                        {XFT_PYC, "PYC"},
                        {XFT_DOS16M, "DOS16M"},
                        {XFT_DOS4G, "DOS4G"},
                        {XFT_UNKNOWN, NULL}};
        int j = 0;

        for (j = 0; pAliases[j].pName; j++) {
            if (pAliases[j].type == pEngine->fileType) {
                js_set(pCtx, global, pAliases[j].pName, js_dup(object));
            }
        }

        /* A PE file is also an MSDOS file for the script API. */
        if ((pEngine->fileType == XFT_PE32) || (pEngine->fileType == XFT_PE64)) {
            js_set(pCtx, global, "MSDOS", js_dup(object));
        }

        /* A .NET PE additionally answers to DOTNET, so the PE/DOTNET scripts
         * can call DOTNET.isNetTypePresent(...) etc. The .NET methods are on
         * the same object. */
        if (pEngine->bIsCliAssembly) {
            js_set(pCtx, global, "DOTNET", js_dup(object));
        }
    }

    {
        /* Util exposes the seven public slots of Util_script. */
        JSVal util = js_new_object(pCtx);

        js_def_method(pCtx, util, "div64", api_dispatch, 2, (void *)(size_t)A_div64);
        js_def_method(pCtx, util, "divu64", api_dispatch, 2, (void *)(size_t)A_divu64);
        js_def_method(pCtx, util, "shlu64", api_dispatch, 2, (void *)(size_t)A_shlu64);
        js_def_method(pCtx, util, "shru64", api_dispatch, 2, (void *)(size_t)A_shru64);
        js_def_method(pCtx, util, "shl64", api_dispatch, 2, (void *)(size_t)A_shl64);
        js_def_method(pCtx, util, "shr64", api_dispatch, 2, (void *)(size_t)A_shr64);
        js_def_method(pCtx, util, "secondsToTimeStr", api_dispatch, 1, (void *)(size_t)A_secondsToTimeStr);
        js_set(pCtx, global, "Util", util);
        js_release(pCtx, util);
    }

    js_release(pCtx, object);
    js_release(pCtx, global);

    js_def_fn(pCtx, "_setResult", fn_set_result, 4, NULL);
    js_def_fn(pCtx, "_musicFmt", fn_music_fmt, 2, NULL);
    js_def_fn(pCtx, "_isResultPresent", fn_is_result_present, 2, NULL);
    js_def_fn(pCtx, "_getNumberOfResults", fn_get_number_of_results, 1, NULL);
    js_def_fn(pCtx, "_removeResult", fn_remove_result, 2, NULL);
    js_def_fn(pCtx, "includeScript", fn_include_script, 1, NULL);
    js_def_fn(pCtx, "_log", fn_log, 1, NULL);
    js_def_fn(pCtx, "_isStop", fn_is_stop, 0, NULL);
    js_def_fn(pCtx, "_breakScan", fn_break_scan, 0, NULL);
    js_def_fn(pCtx, "_encodingList", fn_noop, 0, NULL);
    js_def_fn(pCtx, "_isConsoleMode", fn_true, 0, (void *)(size_t)1);
    js_def_fn(pCtx, "_isGuiMode", fn_true, 0, (void *)(size_t)0);
    js_def_fn(pCtx, "_isLiteMode", fn_true, 0, (void *)(size_t)0);
    js_def_fn(pCtx, "_isLibraryMode", fn_true, 0, (void *)(size_t)0);
    js_def_fn(pCtx, "_getEngineVersion", fn_engine_version, 0, NULL);
    js_def_fn(pCtx, "_getOS", fn_get_os, 0, NULL);
    js_def_fn(pCtx, "_getQtVersion", fn_engine_version, 0, NULL);
}
