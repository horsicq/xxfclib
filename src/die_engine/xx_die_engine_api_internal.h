/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_DIE_ENGINE_API_INTERNAL_H
#define XX_DIE_ENGINE_API_INTERNAL_H

#include "xx_die_engine_internal.h"
#include "xx_die_engine_compat.h"
#include "../js/xx_js_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    A_NONE = 0,

    /* ---- generic binary ---- */
    A_getSize,
    A_compare,
    A_compareEP,
    A_compareOverlay,
    A_readByte,
    A_readSByte,
    A_readWord,
    A_readSWord,
    A_readDword,
    A_readSDword,
    A_readQword,
    A_readSQword,
    A_getString,
    A_isJavaScriptMinified,
    A_findSignature,
    A_findString,
    A_findByte,
    A_findWord,
    A_findDword,
    A_getEntryPointOffset,
    A_getOverlayOffset,
    A_getOverlaySize,
    A_getAddressOfEntryPoint,
    A_isOverlayPresent,
    A_isSignaturePresent,
    A_swapBytes,
    A_RVAToOffset,
    A_VAToOffset,
    A_OffsetToVA,
    A_OffsetToRVA,
    A_getFileDirectory,
    A_getFileBaseName,
    A_getFileCompleteSuffix,
    A_getFileSuffix,
    A_getSignature,
    A_calculateEntropy,
    A_isZeroFilled,
    A_scanBufferForEncryptedPe,
    A_validateAmd64Unwind,
    A_validateGuardCfFunctionTable,
    A_calculateMD5,
    A_calculateCRC32,
    A_crc16,
    A_crc32,
    A_adler32,
    A_isSignatureInSectionPresent,
    A_isAnySignatureInSectionPresent,
    A_getImageBase,
    A_upperCase,
    A_lowerCase,
    A_isPlainText,
    A_isUTF8Text,
    A_isUnicodeText,
    A_isText,
    A_getHeaderString,
    A_getDisasmLength,
    A_getDisasmString,
    A_getDisasmNextAddress,
    A_is16,
    A_is32,
    A_is64,
    A_isDeepScan,
    A_isHeuristicScan,
    A_isFirstWrapperScan,
    A_isAggressiveScan,
    A_isRecursiveScan,
    A_isOverlayScan,
    A_isVerbose,
    A_isProfiling,
    A_getScanID,
    A_getStartOffset,
    A_read_uint8,
    A_read_int8,
    A_read_uint16,
    A_read_int16,
    A_read_uint24,
    A_read_int24,
    A_read_uint32,
    A_read_int32,
    A_read_uint64,
    A_read_int64,
    A_read_ansiString,
    A_read_unicodeString,
    A_read_utf8String,
    A_read_codePageString,
    A_read_float,
    A_read_double,
    A_read_float16,
    A_read_UUID,
    A_read_ucsdString,
    A_isNE,
    A_isLE,
    A_isLX,
    A_getApplicationIdentifier,
    A_getDataPreparerIdentifier,
    A_isArchiveRecordPresent,
    A_isArchiveRecordPresentExp,
    A_getManifestRecord,
    A_getPackageJsonRecord,
    A_isConstPresent,
    A_div64,
    A_divu64,
    A_shlu64,
    A_shru64,
    A_shl64,
    A_shr64,
    A_secondsToTimeStr,
    A_bytesCountToString,
    A_find_ansiString,
    A_find_unicodeString,
    A_find_utf8String,
    A_getOperationSystemName,
    A_getOperationSystemVersion,
    A_getOperationSystemOptions,
    A_getFileFormatName,
    A_getFileFormatVersion,
    A_getFileFormatOptions,
    A_isSigned,
    A_cleanString,
    A_readBytes,
    A_isOverlay,
    A_isResource,
    A_isDebugData,
    A_isFilePart,
    A_isReleaseBuild,
    A_isDebugBuild,
    A_isChecksumCorrect,
    A_isEntryPointCorrect,
    A_isSectionAlignmentCorrect,
    A_isFileAlignmentCorrect,
    A_isHeaderCorrect,
    A_isRelocsTableCorrect,
    A_isImportTableCorrect,
    A_isExportTableCorrect,
    A_isResourcesTableCorrect,
    A_isSectionsTableCorrect,
    A_getFormatMessages,
    A_startTiming,
    A_endTiming,
    A_detectZLIB,
    A_detectGZIP,
    A_detectZIP,
    A_getListOfCompressionMethods,

    /* ---- MSDOS ---- */
    A_isRichSignaturePresent,
    A_getNumberOfRichIDs,
    A_isRichVersionPresent,
    A_getRichVersion,
    A_getRichID,
    A_getRichCount,
    A_getDosStubOffset,
    A_getDosStubSize,
    A_isDosStubPresent,

    /* ---- PE ---- */
    A_getNumberOfSections,
    A_getSectionName,
    A_getSectionVirtualSize,
    A_getSectionVirtualAddress,
    A_getSectionFileSize,
    A_getSectionFileOffset,
    A_getSectionCharacteristics,
    A_getNumberOfResources,
    A_isSectionNamePresent,
    A__isSectionNamePresentExp,
    A_isNET,
    A_isPE32,
    A_isPEPlus,
    A_getGeneralOptions,
    A_getResourceIdByNumber,
    A_getResourceNameByNumber,
    A_getResourceOffsetByNumber,
    A_getResourceSizeByNumber,
    A_getResourceTypeByNumber,
    A_isNetObjectPresent,
    A_isNetUStringPresent,
    A_isNetGlobalCctorPresent,
    A_isNetTypePresent,
    A_isNetMethodPresent,
    A_getNumberOfNetMethods,
    A_getNetMethodInfo,
    A_getNetUserString,
    A_isNetMethodCalledBy,
    A_isNetFieldPresent,
    A_findSignatureInBlob_NET,
    A_isSignatureInBlobPresent_NET,
    A_getNetModuleName,
    A_getNetAssemblyName,
    A_getNumberOfImports,
    A_getImportLibraryName,
    A_isLibraryPresent,
    A_isLibraryFunctionPresent,
    A_isFunctionPresent,
    A_getImportFunctionName,
    A_getImportSection,
    A_getExportSection,
    A_getResourceSection,
    A_getEntryPointSection,
    A_getRelocsSection,
    A_getTLSSection,
    A_getMajorLinkerVersion,
    A_getMinorLinkerVersion,
    A_getManifest,
    A_getVersionStringInfo,
    A_getNumberOfImportThunks,
    A_getResourceNameOffset,
    A_isResourceNamePresent,
    A_isResourceGroupNamePresent,
    A_isResourceGroupIdPresent,
    A_getCompilerVersion,
    A_isConsole,
    A_isSignedFile,
    A_getSectionNameCollision,
    A_getSectionNumber,
    A_getSectionNumberExp,
    A_isDll,
    A_isDriver,
    A_getNETVersion,
    A_compareEP_NET,
    A_getSizeOfCode,
    A_getSizeOfUninitializedData,
    A_getPEFileVersion,
    A_getFileVersion,
    A_getFileVersionMS,
    A_calculateSizeOfHeaders,
    A_isExportFunctionPresent,
    A_getNumberOfExportFunctions,
    A_getExportFunctionName,
    A_getNextNamedExportFunctionIndex,
    A_isExportPresent,
    A_isTLSPresent,
    A_isImportPresent,
    A_isResourcesPresent,
    A_getImportHash32,
    A_getImportHash64,
    A_isImportPositionHashPresent,
    A_getImageFileHeader,
    A_getImageOptionalHeader,
    A_getNumberOfDebugDataRecords,
    A_getDebugDataType,
    A_getDebugDataOffset,
    A_getDebugDataSize,

    /* ---- JPEG ---- */
    A_getComment,
    A_getDqtMD5,
    A_isChunkPresent,
    A_isExifPresent,
    A_getExifCameraName,

    /* ---- APK ---- */
    A_getAndroidManifestRecord,
    A_getAndroidManifest,

    /* ---- PDF ---- */
    A_getHeaderCommentAsHex,
    A_getStringValuesByKey,
    A_getValuesByKey,
    A_isValuesHexByKey,
    A_isEncrypted,
    A_getEncryption,
    A_getPermissions,

    /* ---- DEX ---- */
    A_isDexStringPresent,
    A_isDexItemStringPresent,
    A_getMapItemsHash,

    /* ---- ELF / Mach-O ---- */
    A_isStringInTablePresent,
    A_getNumberOfPrograms,
    A_getProgramFileOffset,
    A_getProgramFileSize,
    A_getElfHeader_type,
    A_getElfHeader_machine,
    A_getElfHeader_entry,
    A_getElfHeader_phoff,
    A_getElfHeader_shoff,
    A_getElfHeader_phnum,
    A_getElfHeader_shnum,
    A_getElfHeader_shentsize,
    A_getElfHeader_phentsize,
    A_getElfHeader_shstrndx,
    A_getRunPath,
    A_isNotePresent,
    A_getLibraryCurrentVersion
} ApiId;

typedef struct {
    const char *pName;
    ApiId id;
    int nArgc;
} ApiEntry;

/* ------------------------------------------------------------- helpers */

static inline DieEngine *die_engine_api_engine_of(JSCtx *pCtx)
{
    return (DieEngine *)js_get_user(pCtx);
}

char *die_engine_api_arg_string(JSCtx *pCtx, int nArgc, JSVal *pArgv, int nIndex);
cd_i64 die_engine_api_arg_i64(JSCtx *pCtx, int nArgc, JSVal *pArgv, int nIndex, cd_i64 nDefault);
int die_engine_api_arg_exact_u32(int nArgc, JSVal *pArgv, int nIndex, cd_u32 *pValue);
cd_u64 die_engine_api_arg_u64(JSCtx *pCtx, int nArgc, JSVal *pArgv, int nIndex);
int die_engine_api_arg_bool(JSCtx *pCtx, int nArgc, JSVal *pArgv, int nIndex, int bDefault);

JSVal die_engine_api_str_take(JSCtx *pCtx, char *pString);
JSVal die_engine_api_str_take_native(JSCtx *pCtx, char *pString);
JSVal die_engine_api_native_str_take(JSCtx *ctx, char *text);

void die_engine_api_value_to_hex_ex(cd_u64 nValue, char *pBuffer, size_t nBufferSize);
char *die_engine_api_clean_string(const char *pString);
int die_engine_api_has_legacy_script_map(const xx_memory_map *map);
cd_i64 die_engine_api_script_entry_point_offset(DieEngine *pEngine);
cd_i64 die_engine_api_legacy_overlay_offset(const xx_memory_map *map);
void die_engine_api_fix_offset_size(DieEngine *pEngine, cd_i64 *pnOffset, cd_i64 *pnSize);

/* Convenience aliases for script API modules */
#define engine_of die_engine_api_engine_of
#define arg_string die_engine_api_arg_string
#define arg_i64 die_engine_api_arg_i64
#define arg_exact_u32 die_engine_api_arg_exact_u32
#define arg_u64 die_engine_api_arg_u64
#define arg_bool die_engine_api_arg_bool
#define str_take die_engine_api_str_take
#define str_take_native die_engine_api_str_take_native
#define native_str_take die_engine_api_native_str_take
#define value_to_hex_ex die_engine_api_value_to_hex_ex
#define clean_string die_engine_api_clean_string
#define has_legacy_script_map die_engine_api_has_legacy_script_map
#define script_entry_point_offset die_engine_api_script_entry_point_offset
#define legacy_overlay_offset die_engine_api_legacy_overlay_offset
#define fix_offset_size die_engine_api_fix_offset_size

/* Sub-dispatchers */
bool die_engine_api_dispatch_binary(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult);
bool die_engine_api_dispatch_pe(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult);
bool die_engine_api_dispatch_msdos(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult);
bool die_engine_api_dispatch_elf_macho(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult);
bool die_engine_api_dispatch_formats(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult);

#endif /* XX_DIE_ENGINE_API_INTERNAL_H */
