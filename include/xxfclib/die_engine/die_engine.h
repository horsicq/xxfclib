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

/* die_engine.h - the Detect It Easy scan engine.
 *
 * Moved here from cdie. Load a signature database, set scan options, scan a
 * file or a memory image, and render the result as text, JSON, XML or CSV.
 *
 * The engine's own state (DieEngine) and the binary-format parsers behind it
 * are deliberately NOT exposed: they live in src/die_engine/ and change with
 * the signature format. A consumer needs the four types below and nothing
 * more -- which is exactly what cdie's own console, GUI and shared library
 * used.
 */

#ifndef XX_DIE_ENGINE_H
#define XX_DIE_ENGINE_H

#include "xxfclib/xxfc_defs.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------- types  */

/**
 * @brief What a file was detected as.
 *
 * The same numbering the signature databases use, so a database's declared
 * type can be compared against a detected one directly.
 */
typedef enum {
    XFT_UNKNOWN = 0,
    XFT_BINARY,
    XFT_COM,
    XFT_MSDOS,
    XFT_NE,
    XFT_LE,
    XFT_LX,
    XFT_PE,
    XFT_PE32,
    XFT_PE64,
    XFT_ELF,
    XFT_ELF32,
    XFT_ELF64,
    XFT_MACHO,
    XFT_MACHO32,
    XFT_MACHO64,
    XFT_ZIP,
    XFT_JAR,
    XFT_APK,
    XFT_IPA,
    XFT_DEX,
    XFT_NPM,
    XFT_MACHOFAT,
    XFT_ARCHIVE,
    XFT_PDF,
    XFT_CFBF,
    XFT_IMAGE,
    XFT_JPEG,
    XFT_PNG,
    XFT_RAR,
    XFT_ISO9660,
    XFT_AMIGAHUNK,
    XFT_ATARIST,
    XFT_JAVACLASS,
    XFT_PYC,
    XFT_DOS16M,
    XFT_DOS4G,
    XFT_CLI_ASSEMBLY,
    XFT_COUNT
} XFileType;

/** @brief The human-readable name of a type, as printed. */
XXFC_API const char *xft_to_string(XFileType type);

/**
 * @brief Does a database's declared file type match a detected one?
 *
 * Signature databases declare a broad type (XFT_PE) and files detect as a
 * specific one (XFT_PE64); this is the rule that decides which scripts run
 * against a file, so a caller driving the engine needs it to predict or
 * filter that set. Mirrors XBinary::checkFileType in the reference.
 */
XXFC_API int xft_check(XFileType databaseType, XFileType fileType);

/** @brief Opaque engine state. Defined in src/die_engine/. */
typedef struct DieEngine DieEngine;

typedef enum { DB_MAIN = 0, DB_EXTRA, DB_CUSTOM } DBKind;

typedef struct {
    char *pName;     /* file name including the extension, e.g. "_PE.0.sg" */
    char *pFilePath; /* absolute path                                      */
    char *pText;     /* script source or precompiled bytecode              */
    size_t nSize;    /* size of pText / bytecode in bytes                  */
    XFileType fileType;
    DBKind databaseType;
    int64_t nElapsedTime; /* elapsed execution time in ms (when profiling/scan-time enabled) */
} DBSignature;

typedef struct {
    DBSignature *pRecords;
    int nCount;
    int nCapacity;
} DBase;

XXFC_API int db_load(DBase *pDb, const char *pPath, DBKind kind);
XXFC_API int db_load_tar(DBase *pDb, const char *pTarPath, DBKind kind);
XXFC_API int db_create_tar(const char *pDbPath, const char *pTarPath);
XXFC_API int db_create_tar_precompiled(const char *pDbPath, const char *pTarPath);
XXFC_API void db_sort(DBase *pDb);
XXFC_API void db_free(DBase *pDb);
XXFC_API int db_count_for_type(DBase *pDb, XFileType fileType);

/* --------------------------------------------------------------- options  */

typedef struct {
    int bDeepScan;
    int bHeuristicScan;
    int bAggressiveScan;
    int bRecursiveScan;
    int bVerbose;
    int bAllTypesScan; /* die_library flag mapping only; the console -a option
                        * was removed as it is not implemented in the engine */
    int bFormatResult;
    int bHideUnknown;
    int bShowType;
    int bShowVersion;
    int bShowInfo;
    int bUseCustomDatabase;
    int bUseExtraDatabase;
    int bResultAsJSON;
    int bResultAsXML;
    int bResultAsCSV;
    int bResultAsTSV;
    int bResultAsPlainText;
    int bShowMessages;
    int bProfiling;
    int bSort;
    char *pMainDatabasePath;
    char *pExtraDatabasePath;
    char *pCustomDatabasePath;
} ScanOptions;

XXFC_API void scan_options_init(ScanOptions *pOptions);
XXFC_API void scan_options_free(ScanOptions *pOptions);

/* --------------------------------------------------------------- results  */

typedef struct {
    char *pType;
    char *pName;
    char *pVersion;
    char *pInfo;
    int nPrio;
    int bIsHeuristic;
    int bIsAHeuristic;
    int bIsUnknown;
} ScanRecord;

typedef struct {
    ScanRecord *pRecords;
    int nCount;
    int nCapacity;
    char **ppErrors;
    int nErrorCount;
    XFileType fileType;
    char *pFileName;
    int64_t nFileSize;
} ScanResult;

XXFC_API void scan_result_free(ScanResult *pResult);


/* ---------------------------------------------------------------- scan  */

XXFC_API int die_engine_scan_file(const char *pFileName, DBase *pDb, ScanOptions *pOptions, ScanResult *pResult);

/* Scans an in-memory buffer (a private copy is taken). Backs DIE_ScanMemory. */
XXFC_API int die_engine_scan_memory(const void *pData, int64_t nSize, DBase *pDb, ScanOptions *pOptions, ScanResult *pResult);

/* --------------------------------------------------------- profiling (-l)  */

/* Output helpers (result.c). */
XXFC_API char *die_engine_format_text(ScanResult *pResult, ScanOptions *pOptions);
XXFC_API char *die_engine_format_json(ScanResult *pResult, ScanOptions *pOptions);
XXFC_API char *die_engine_format_xml(ScanResult *pResult, ScanOptions *pOptions);
XXFC_API char *die_engine_format_csv(ScanResult *pResult, ScanOptions *pOptions, char nSeparator);

XXFC_API int die_engine_type_to_prio(const char *pType);
XXFC_API char *die_engine_translate_type(const char *pType);

#ifdef __cplusplus
}
#endif

#endif /* XX_DIE_ENGINE_H */
