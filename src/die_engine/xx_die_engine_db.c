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

/* xx_die_engine_db.c - loads the Detect It Easy signature database.
 *
 * The layout mirrors the original engine: the database root holds shared
 * include scripts (file type "unknown") and one subdirectory per format.
 * Only regular files with the ".sg" extension or no extension at all are
 * treated as signatures, and subdirectories below a format directory are
 * ignored - exactly like XScanEngine::_loadDatabaseFromPath.              */

#include "xxfclib/die_engine/die_engine_db.h"
#include "xx_die_engine_db_internal.h"
#include "../sort/xx_sort_scan_internal.h"
#include "xxfclib/js/xx_js.h"
#include "xx_die_engine_compat.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/fs/xx_fs.h"
#include "xxfclib/list/xx_list.h"
#include "xxfclib/strings/xx_string.h"
#include "../io/platforms/xx_io_platform.h"

static const struct {
    const char *pDirectory;
    XFileType fileType;
} g_directories[] = {{"", XFT_UNKNOWN},
                     {"Binary", XFT_BINARY},
                     {"COM", XFT_COM},
                     {"Archive", XFT_ARCHIVE},
                     {"ZIP", XFT_ZIP},
                     {"JAR", XFT_JAR},
                     {"APK", XFT_APK},
                     {"IPA", XFT_IPA},
                     {"NPM", XFT_NPM},
                     {"MACHOFAT", XFT_MACHOFAT},
                     {"DEX", XFT_DEX},
                     {"MSDOS", XFT_MSDOS},
                     {"LE", XFT_LE},
                     {"LX", XFT_LX},
                     {"NE", XFT_NE},
                     {"PE", XFT_PE},
                     {"PE/DOTNET", XFT_CLI_ASSEMBLY},
                     {"ELF", XFT_ELF},
                     {"MACH", XFT_MACHO},
                     {"DOS16M", XFT_DOS16M},
                     {"DOS4G", XFT_DOS4G},
                     {"Amiga", XFT_AMIGAHUNK},
                     {"AtariST", XFT_ATARIST},
                     {"JavaClass", XFT_JAVACLASS},
                     {"PYC", XFT_PYC},
                     {"PDF", XFT_PDF},
                     {"CFBF", XFT_CFBF},
                     {"Image", XFT_IMAGE},
                     {"JPEG", XFT_JPEG},
                     {"PNG", XFT_PNG},
                     {"RAR", XFT_RAR},
                     {"ISO9660", XFT_ISO9660},
                     {NULL, XFT_UNKNOWN}};

typedef struct db_metadata_blob {
    XFileType fileType;
    char *pText;
    size_t nSize;
} db_metadata_blob;

static char *db_normalize_metadata_tags(const char *pText, size_t nSize)
{
    char *pResult = (char *)cd_malloc(nSize + 1U);
    size_t nOut = 0;
    size_t nPosition = 0;
    while (nPosition < nSize) {
        size_t nStart;
        size_t nEnd;
        while (nPosition < nSize && (pText[nPosition] == ',' || pText[nPosition] == ';' || db_is_tag_space(pText[nPosition]))) ++nPosition;
        nStart = nPosition;
        while (nPosition < nSize && pText[nPosition] != ',' && pText[nPosition] != ';') ++nPosition;
        nEnd = nPosition;
        while (nEnd > nStart && db_is_tag_space(pText[nEnd - 1])) --nEnd;
        while (nStart < nEnd && db_is_tag_space(pText[nStart])) ++nStart;
        if (nEnd > nStart) {
            if (nOut) pResult[nOut++] = ';';
            x_memcpy(pResult + nOut, pText + nStart, nEnd - nStart);
            nOut += nEnd - nStart;
        }
        if (nPosition < nSize) ++nPosition;
    }
    pResult[nOut] = '\0';
    return pResult;
}

static int db_metadata_matches(const DBSignatureMetadata *pMetadata,
                               XFileType fileType, const char *pName, size_t nNameSize)
{
    return pMetadata && pMetadata->fileType == fileType && pMetadata->pName &&
           db_ascii_span_equal(pMetadata->pName, xx_rt_strlen(pMetadata->pName), pName, nNameSize);
}

static unsigned int db_metadata_kind_mask(DBKind kind)
{
    /* The authoritative db/_metadata files are generated from db plus
     * db_custom. A database loaded as the deprecated DB_EXTRA kind (no cdie
     * frontend loads one) owns its metadata independently. */
    if ((unsigned int)kind > (unsigned int)DB_CUSTOM) return 0U;
    if (kind == DB_MAIN) return (1U << DB_MAIN) | (1U << DB_CUSTOM);
    return 1U << (unsigned int)kind;
}

static void db_apply_metadata_entry(DBase *pDb, const DBSignatureMetadata *pMetadata, int nFirstRecord)
{
    int i;
    if (!pDb || !pMetadata || !pMetadata->pName) return;
    if (nFirstRecord < 0) nFirstRecord = 0;
    for (i = nFirstRecord; i < pDb->nCount; ++i) {
        DBSignature *pRecord = &pDb->pRecords[i];
        if ((unsigned int)pRecord->databaseType > (unsigned int)DB_CUSTOM) continue;
        if ((pMetadata->databaseKinds & (1U << (unsigned int)pRecord->databaseType)) == 0U) continue;
        if (!db_metadata_matches(pMetadata, pRecord->fileType,
                                 pRecord->pName ? pRecord->pName : "",
                                 pRecord->pName ? xx_rt_strlen(pRecord->pName) : 0U)) continue;
        cd_free(pRecord->pTags);
        pRecord->pTags = cd_strdup(pMetadata->pTags ? pMetadata->pTags : "");
    }
}

static void db_apply_stored_metadata(DBase *pDb, int nFirstRecord)
{
    int i;
    if (!pDb) return;
    for (i = 0; i < pDb->nMetadataCount; ++i) {
        db_apply_metadata_entry(pDb, &pDb->pMetadata[i], nFirstRecord);
    }
}

static DBSignatureMetadata *db_store_metadata(DBase *pDb, XFileType fileType,
                                               const char *pName, size_t nNameSize,
                                               char *pNormalizedTags, unsigned int databaseKinds)
{
    DBSignatureMetadata *pMetadata;
    int i;
    if (!pDb || !pName || !nNameSize || !pNormalizedTags) return NULL;
    for (i = 0; i < pDb->nMetadataCount; ++i) {
        pMetadata = &pDb->pMetadata[i];
        if (pMetadata->databaseKinds != databaseKinds ||
            !db_metadata_matches(pMetadata, fileType, pName, nNameSize)) continue;
        cd_free(pMetadata->pTags);
        pMetadata->pTags = pNormalizedTags;
        return pMetadata;
    }
    if (pDb->nMetadataCount == pDb->nMetadataCapacity) {
        int nNewCapacity = pDb->nMetadataCapacity ? pDb->nMetadataCapacity * 2 : 64;
        pDb->pMetadata = (DBSignatureMetadata *)cd_realloc(
            pDb->pMetadata, (size_t)nNewCapacity * sizeof(*pDb->pMetadata));
        pDb->nMetadataCapacity = nNewCapacity;
    }
    pMetadata = &pDb->pMetadata[pDb->nMetadataCount++];
    pMetadata->pName = (char *)cd_malloc(nNameSize + 1U);
    x_memcpy(pMetadata->pName, pName, nNameSize);
    pMetadata->pName[nNameSize] = '\0';
    pMetadata->pTags = pNormalizedTags;
    pMetadata->fileType = fileType;
    pMetadata->databaseKinds = databaseKinds;
    return pMetadata;
}

static void db_apply_metadata_text(DBase *pDb, DBKind metadataKind, XFileType fileType,
                                   const char *pText, size_t nTextSize)
{
    size_t nPosition = 0;
    if (!pDb || !pText) return;
    while (nPosition < nTextSize) {
        size_t nLineStart = nPosition;
        size_t nLineEnd;
        size_t nHash = SIZE_MAX;
        const char *pName;
        size_t nNameSize;
        XFileType recordFileType = fileType;
        const char *pTags = NULL;
        size_t nTagsSize = 0;
        char *pNormalized;
        DBSignatureMetadata *pMetadata;
        size_t i;

        while (nPosition < nTextSize && pText[nPosition] != '\n') ++nPosition;
        nLineEnd = nPosition;
        if (nPosition < nTextSize) ++nPosition;
        while (nLineEnd > nLineStart && (pText[nLineEnd - 1] == '\r' || pText[nLineEnd - 1] == '\n')) --nLineEnd;
        while (nLineStart < nLineEnd && db_is_tag_space(pText[nLineStart])) ++nLineStart;
        while (nLineEnd > nLineStart && db_is_tag_space(pText[nLineEnd - 1])) --nLineEnd;
        if (nLineStart == nLineEnd) continue;
        if (nLineStart == 0 && nLineEnd - nLineStart >= 3 &&
            (unsigned char)pText[0] == 0xefU && (unsigned char)pText[1] == 0xbbU && (unsigned char)pText[2] == 0xbfU) nLineStart += 3;

        for (i = nLineStart; i < nLineEnd; ++i) {
            if (pText[i] == '#') nHash = (size_t)i;
        }
        pName = pText + nLineStart;
        nNameSize = (nHash == SIZE_MAX ? nLineEnd : nHash) - nLineStart;
        db_trim_span(&pName, &nNameSize);
        if (!nNameSize) continue;
        /* PE metadata is recursive. Its DOTNET/foo.sg names correspond to
         * records loaded from db/PE/DOTNET with the CLI-assembly type. */
        if (fileType == XFT_PE && nNameSize > 7U &&
            db_ascii_span_equal(pName, 7U, "DOTNET/", 7U)) {
            recordFileType = XFT_CLI_ASSEMBLY;
            pName += 7U;
            nNameSize -= 7U;
        }
        if (nHash != SIZE_MAX) {
            pTags = pText + nHash + 1U;
            nTagsSize = nLineEnd - nHash - 1U;
            db_trim_span(&pTags, &nTagsSize);
        }
        pNormalized = db_normalize_metadata_tags(pTags ? pTags : "", nTagsSize);
        pMetadata = db_store_metadata(pDb, recordFileType, pName, nNameSize, pNormalized,
                                      db_metadata_kind_mask(metadataKind));
        if (pMetadata) db_apply_metadata_entry(pDb, pMetadata, 0);
        else cd_free(pNormalized);
    }
}

static int db_metadata_name_type(const char *pName, XFileType *pFileType)
{
    size_t nNameSize;
    size_t nBaseSize;
    int i;
    if (!pName || !pFileType) return 0;
    nNameSize = xx_rt_strlen(pName);
    if (nNameSize <= 4U || !db_ascii_span_equal(pName + nNameSize - 4U, 4U, ".txt", 4U)) return 0;
    nBaseSize = nNameSize - 4U;
    for (i = 0; g_directories[i].pDirectory; ++i) {
        const char *pDirectory = g_directories[i].pDirectory;
        size_t nDirectorySize;
        if (!pDirectory[0] || xx_rt_strchr(pDirectory, '/')) continue;
        nDirectorySize = xx_rt_strlen(pDirectory);
        if (db_ascii_span_equal(pName, nBaseSize, pDirectory, nDirectorySize)) {
            *pFileType = g_directories[i].fileType;
            return 1;
        }
    }
    return 0;
}

static int db_metadata_tar_path_type(const char *pPath, XFileType *pFileType)
{
    const char *pStart = pPath;
    const char *pLastSlash;
    const char *pDirectoryStart;
    const char *pCursor;
    size_t nDirectorySize;
    if (!pPath) return 0;
    while (pStart[0] == '.' && (pStart[1] == '/' || pStart[1] == '\\')) pStart += 2;
    while (*pStart == '/' || *pStart == '\\') ++pStart;
    pLastSlash = xx_rt_strrchr(pStart, '/');
    if (!pLastSlash) pLastSlash = xx_rt_strrchr(pStart, '\\');
    if (!pLastSlash) return 0;
    pDirectoryStart = pStart;
    for (pCursor = pStart; pCursor < pLastSlash; ++pCursor) {
        if (*pCursor == '/' || *pCursor == '\\') pDirectoryStart = pCursor + 1;
    }
    nDirectorySize = (size_t)(pLastSlash - pDirectoryStart);
    if (!db_ascii_span_equal(pDirectoryStart, nDirectorySize, "_metadata", 9U)) return 0;
    return db_metadata_name_type(pLastSlash + 1, pFileType);
}

/* QDir's default Name | IgnoreCase order is also used by the reference TAR
 * loader. Establish that input order before its pair-dependent priority sort. */
static int db_directory_order(XFileType fileType)
{
    int i;
    for (i = 0; g_directories[i].pDirectory; ++i) {
        if (g_directories[i].fileType == fileType) return i;
    }
    return i;
}

static void db_sort_loaded_records(DBase *pDb, int nBefore, DBKind kind)
{
    int i;
    DBase added = {0};

    /* An empty load of the deprecated DB_EXTRA kind (a C-only legacy path;
     * no cdie frontend loads one) must not reorder the main database. */
    if (nBefore == pDb->nCount && kind == DB_EXTRA) return;

    for (i = nBefore + 1; i < pDb->nCount; ++i) {
        DBSignature value = pDb->pRecords[i];
        int order = db_directory_order(value.fileType);
        int hole = i;
        while (hole > nBefore) {
            DBSignature *previous = &pDb->pRecords[hole - 1];
            int previousOrder = previous->fileType == value.fileType ? order : db_directory_order(previous->fileType);
            if (previousOrder < order ||
                (previousOrder == order && cd_stricmp_ascii(previous->pName, value.pName) <= 0)) break;
            pDb->pRecords[hole] = *previous;
            --hole;
        }
        pDb->pRecords[hole] = value;
    }

    if (pDb->nCount > nBefore) {
        added.pRecords = pDb->pRecords + nBefore;
        added.nCount = pDb->nCount - nBefore;
        xx_scan_sort_database_records(&added);
    }
    /* loadDatabase sorts the newly loaded list, appends it, and sorts the
     * complete list once more, even for an existing empty custom directory. */
    xx_scan_sort_database_records(pDb);
}

static int is_signature_path(const char *pPath)
{
    const char *pName = pPath;
    const char *pCursor;
    const char *pDot;

    if (!pPath) {
        return 0;
    }

    /* Match xx_fs_path_suffix without allocating a name and suffix for
     * every entry. Both path separators, leading dots and an empty trailing
     * suffix retain the filesystem helper's semantics. */
    for (pCursor = pPath; *pCursor; ++pCursor) {
        if (xx_fs_is_separator(*pCursor)) pName = pCursor + 1;
    }
    pDot = xx_rt_strrchr(pName, '.');
    if (pDot && pDot != pName && pDot[1] && cd_stricmp_ascii(pDot + 1, "sg") != 0) {
        return 0;
    }

    return 1;
}

static int is_signature_name(const char *pName)
{
    const char *pDot = NULL;

    if (!pName || !pName[0]) {
        return 0;
    }

    pDot = xx_rt_strrchr(pName, '.');
    if (!pDot) {
        return 1; /* No extension: _init, _runtime_helpers, read, etc. */
    }

    return (cd_stricmp_ascii(pDot + 1, "sg") == 0) ? 1 : 0;
}

/* Loaded databases own one bounded parsed-program cache. Hand-built
 * database views retain their existing ownership contract and need no cache. */
static void db_prepare_program_cache(DBase *pDb)
{
    if (!pDb->pJsProgramCache) pDb->pJsProgramCache = js_program_cache_new(64U * 1024U * 1024U);
}

static void db_add(DBase *pDb, const char *pName, const char *pPath, char *pText, size_t nSize, XFileType fileType, DBKind kind)
{
    DBSignature *pRecord = NULL;

    if (pDb->nCount + 1 > pDb->nCapacity) {
        pDb->nCapacity = pDb->nCapacity ? (pDb->nCapacity * 2) : 256;
        pDb->pRecords = (DBSignature *)cd_realloc(pDb->pRecords, (size_t)pDb->nCapacity * sizeof(DBSignature));
    }

    pRecord = &pDb->pRecords[pDb->nCount++];
    pRecord->pName = cd_strdup(pName);
    pRecord->pFilePath = cd_strdup(pPath);
    pRecord->pText = pText;
    pRecord->nSize = nSize;
    pRecord->fileType = fileType;
    pRecord->databaseType = kind;
    pRecord->nElapsedTime = 0;
    pRecord->pTags = cd_strdup("");
}

/* Read directly into the allocator owned by DBSignature. Using the common
 * file device preserves I/O policy while avoiding an intermediate text
 * allocation and copy through xx_fs_read_file. */
static char *db_read_signature(const char *pPath, size_t *pSize)
{
    xx_io_device *pFile = xx_io_file_open(pPath, "rb");
    int64_t nTotal;
    ssize_t nRead;
    char *pText;

    *pSize = 0;
    if (!pFile) return NULL;
    nTotal = xx_io_total_size(pFile);
    if (nTotal < 0 || (uint64_t)nTotal >= (uint64_t)SIZE_MAX) {
        xx_io_close(pFile);
        return NULL;
    }
    pText = (char *)cd_try_malloc((size_t)nTotal + 1U);
    if (!pText) {
        xx_io_close(pFile);
        return NULL;
    }
    /* A newly opened device is at zero; its size query preserves position. */
    nRead = nTotal > 0 ? xx_io_read(pFile, pText, (size_t)nTotal) : 0;
    xx_io_close(pFile);
    if (nRead < 0) {
        cd_free(pText);
        return NULL;
    }
    /* Preserve xx_fs_read_file's short-read behavior for shrinking files. */
    pText[(size_t)nRead] = '\0';
    *pSize = (size_t)nRead;
    return pText;
}

/* Reuse a directory descriptor for a batch of small files. The platform
 * reader applies the same read policy and validates regular files before
 * and after opening; no FILE buffer or temporary device is needed. */
static char *db_read_signature_at(intptr_t nDirectory, const char *pName, size_t *pSize)
{
    int64_t nTotal;
    intptr_t nFile = xx_io_platform_read_regular_at(nDirectory, pName, &nTotal);
    ssize_t nRead;
    char *pText;

    *pSize = 0;
    if (nFile == -1) return NULL;
    if ((uint64_t)nTotal >= (uint64_t)SIZE_MAX) {
        xx_io_platform_close_descriptor(nFile);
        return NULL;
    }
    pText = (char *)cd_try_malloc((size_t)nTotal + 1U);
    if (!pText) {
        xx_io_platform_close_descriptor(nFile);
        return NULL;
    }
    nRead = nTotal > 0 ? xx_io_platform_read_descriptor(nFile, pText, (size_t)nTotal) : 0;
    xx_io_platform_close_descriptor(nFile);
    if (nRead < 0) {
        cd_free(pText);
        return NULL;
    }
    pText[(size_t)nRead] = '\0';
    *pSize = (size_t)nRead;
    return pText;
}

static void db_load_metadata_directory(DBase *pDb, const char *pDatabasePath, DBKind kind)
{
    char *pMetadataPath;
    xx_list_t list;
    intptr_t nDirectory;
    size_t nCount;
    size_t i;

    if (!pDb || !pDatabasePath) return;
    pMetadataPath = xx_fs_path_join(pDatabasePath, "_metadata");
    if (!pMetadataPath || !xx_fs_is_dir(pMetadataPath)) {
        xx_str_free(pMetadataPath);
        return;
    }
    if (!xx_list_init(&list, sizeof(xx_fs_entry_t *), NULL)) {
        xx_str_free(pMetadataPath);
        return;
    }
    if (!xx_fs_list_dir_sorted(pMetadataPath, &list, XX_FS_SORT_NAME)) {
        nCount = xx_list_count(&list);
        for (i = 0; i < nCount; ++i) {
            xx_fs_entry_t *pEntry = *(xx_fs_entry_t **)xx_list_at(&list, i);
            if (pEntry) xx_fs_entry_free(pEntry);
        }
        xx_list_cleanup(&list);
        xx_str_free(pMetadataPath);
        return;
    }
    nCount = xx_list_count(&list);
    nDirectory = nCount ? xx_io_platform_read_directory_open(pMetadataPath) : -1;
    for (i = 0; i < nCount; ++i) {
        xx_fs_entry_t *pEntry = *(xx_fs_entry_t **)xx_list_at(&list, i);
        XFileType fileType;
        if (pEntry && db_metadata_name_type(pEntry->name, &fileType)) {
            char *pText = NULL;
            size_t nSize = 0;
            if (nDirectory != -1) pText = db_read_signature_at(nDirectory, pEntry->name, &nSize);
            else if (xx_fs_is_file(pEntry->path)) pText = db_read_signature(pEntry->path, &nSize);
            if (pText) {
                db_apply_metadata_text(pDb, kind, fileType, pText, nSize);
                cd_free(pText);
            }
        }
        if (pEntry) xx_fs_entry_free(pEntry);
    }
    if (nDirectory != -1) xx_io_platform_close_descriptor(nDirectory);
    xx_list_cleanup(&list);
    xx_str_free(pMetadataPath);
}

static void db_load_directory(DBase *pDb, const char *pPath, XFileType fileType, DBKind kind)
{
    xx_list_t list;
    intptr_t nDirectory;
    size_t nCount = 0;
    size_t i = 0;

    if (!xx_list_init(&list, sizeof(xx_fs_entry_t *), NULL)) {
        return;
    }

    /* Name order, not the directories-first order xx_fs_list_dir gives: the
     * reference loads the signature files in the directory's own name order
     * and that order decides which of two scripts claiming the same file
     * wins. */
    if (!xx_fs_list_dir_sorted(pPath, &list, XX_FS_SORT_NAME)) {
        nCount = xx_list_count(&list);

        for (i = 0; i < nCount; i++) {
            xx_fs_entry_free(*(xx_fs_entry_t **)xx_list_at(&list, i));
        }

        xx_list_cleanup(&list);

        return;
    }

    nCount = xx_list_count(&list);
    /* Unsupported platforms and directories that cannot be opened retain
     * the existing file-device path. Keep the process working directory. */
    nDirectory = nCount ? xx_io_platform_read_directory_open(pPath) : -1;

    for (i = 0; i < nCount; i++) {
        xx_fs_entry_t *pEntry = *(xx_fs_entry_t **)xx_list_at(&list, i);

        if (pEntry == NULL) {
            continue;
        }

        if (pEntry->type == XX_FS_ENTRY_FILE && is_signature_path(pEntry->path)) {
            size_t nSize = 0;
            char *pText = NULL;

            if (nDirectory != -1) {
                pText = db_read_signature_at(nDirectory, pEntry->name, &nSize);
            } else if (xx_fs_is_file(pEntry->path)) {
                pText = db_read_signature(pEntry->path, &nSize);
            }

            if (pText) {
                db_add(pDb, pEntry->name, pEntry->path, pText, nSize, fileType, kind);
            }
        }

        xx_fs_entry_free(pEntry);
    }

    if (nDirectory != -1) xx_io_platform_close_descriptor(nDirectory);
    xx_list_cleanup(&list);
}

/* ------------------------------------------------------------- tar utils  */

static uint64_t tar_parse_octal(const char *p, size_t n)
{
    uint64_t val = 0;
    while (n && (*p == ' ' || *p == '\0')) {
        p++;
        n--;
    }
    while (n && (*p >= '0' && *p <= '7')) {
        val = (val << 3) + (uint64_t)(*p - '0');
        p++;
        n--;
    }
    return val;
}

static int tar_write_header(xx_io_device *pFile, const char *pName, size_t nSize)
{
    char header[512];
    size_t i;
    unsigned int nChkSum = 0;

    x_memset(header, 0, sizeof(header));

    xx_rt_strncpy(header, pName, 100);
    xx_rt_snprintf(&header[100], 8, "%07o", 0644);
    xx_rt_snprintf(&header[108], 8, "%07o", 0);
    xx_rt_snprintf(&header[116], 8, "%07o", 0);
    xx_rt_snprintf(&header[124], 12, "%011llo", (unsigned long long)nSize);
    xx_rt_snprintf(&header[136], 12, "%011llo", (unsigned long long)0);

    for (i = 0; i < 8; i++) {
        header[148 + i] = ' ';
    }

    header[156] = '0';
    x_memcpy(&header[257], "ustar\0", 6);
    x_memcpy(&header[263], "00", 2);

    for (i = 0; i < 512; i++) {
        nChkSum += (unsigned char)header[i];
    }

    xx_rt_snprintf(&header[148], 8, "%06o", nChkSum);
    header[154] = '\0';
    header[155] = ' ';

    return (xx_io_write(pFile, header, 512) == 512) ? 1 : 0;
}

static int tar_write_data(xx_io_device *pFile, const void *pData, size_t nSize)
{
    static const char zeroes[512] = {0};
    size_t nPad = (512 - (nSize % 512)) % 512;

    if (nSize > 0) {
        if (xx_io_write(pFile, pData, nSize) != (ssize_t)nSize) {
            return 0;
        }
    }
    if (nPad > 0) {
        if (xx_io_write(pFile, zeroes, nPad) != (ssize_t)nPad) {
            return 0;
        }
    }
    return 1;
}

static int tar_write_end(xx_io_device *pFile)
{
    static const char zeroes[1024] = {0};
    return (xx_io_write(pFile, zeroes, sizeof(zeroes)) == (ssize_t)sizeof(zeroes)) ? 1 : 0;
}

static int db_write_metadata_tar(xx_io_device *pTar, const char *pDbPath)
{
    char *pMetadataPath = xx_fs_path_join(pDbPath, "_metadata");
    xx_list_t list;
    intptr_t nDirectory;
    size_t nCount;
    size_t i;
    int bResult = 1;

    if (!pMetadataPath || !xx_fs_is_dir(pMetadataPath)) {
        xx_str_free(pMetadataPath);
        return 1;
    }
    if (!xx_list_init(&list, sizeof(xx_fs_entry_t *), NULL)) {
        xx_str_free(pMetadataPath);
        return 0;
    }
    if (!xx_fs_list_dir_sorted(pMetadataPath, &list, XX_FS_SORT_NAME)) {
        nCount = xx_list_count(&list);
        for (i = 0; i < nCount; ++i) {
            xx_fs_entry_t *pEntry = *(xx_fs_entry_t **)xx_list_at(&list, i);
            if (pEntry) xx_fs_entry_free(pEntry);
        }
        xx_list_cleanup(&list);
        xx_str_free(pMetadataPath);
        return 0;
    }
    nCount = xx_list_count(&list);
    nDirectory = nCount ? xx_io_platform_read_directory_open(pMetadataPath) : -1;
    for (i = 0; i < nCount; ++i) {
        xx_fs_entry_t *pEntry = *(xx_fs_entry_t **)xx_list_at(&list, i);
        XFileType ignoredType;
        if (bResult && pEntry && db_metadata_name_type(pEntry->name, &ignoredType)) {
            char *pText = NULL;
            size_t nSize = 0;
            char sRelativePath[256];
            if (nDirectory != -1) pText = db_read_signature_at(nDirectory, pEntry->name, &nSize);
            else if (xx_fs_is_file(pEntry->path)) pText = db_read_signature(pEntry->path, &nSize);
            if (!pText) {
                bResult = 0;
            } else {
                xx_rt_snprintf(sRelativePath, sizeof(sRelativePath), "_metadata/%s", pEntry->name);
                sRelativePath[sizeof(sRelativePath) - 1] = '\0';
                bResult = tar_write_header(pTar, sRelativePath, nSize) && tar_write_data(pTar, pText, nSize);
                cd_free(pText);
            }
        }
        if (pEntry) xx_fs_entry_free(pEntry);
    }
    if (nDirectory != -1) xx_io_platform_close_descriptor(nDirectory);
    xx_list_cleanup(&list);
    xx_str_free(pMetadataPath);
    return bResult;
}

static int tar_strnicmp_ascii(const char *pLeft, const char *pRight, size_t n)
{
    while (n > 0) {
        char a = *pLeft++;
        char b = *pRight++;
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) {
            return (a < b) ? -1 : 1;
        }
        if (a == '\0') {
            break;
        }
        n--;
    }
    return 0;
}

static XFileType file_type_from_tar_path(const char *pTarPath, const char **ppBaseName)
{
    const char *pDirStart = pTarPath;
    const char *pSlash = NULL;
    int i = 0;
    size_t dirLen = 0;

    while (pDirStart[0] == '.' && (pDirStart[1] == '/' || pDirStart[1] == '\\')) {
        pDirStart += 2;
    }
    while (*pDirStart == '/' || *pDirStart == '\\') {
        pDirStart++;
    }

    pSlash = xx_rt_strrchr(pDirStart, '/');
    if (!pSlash) {
        pSlash = xx_rt_strrchr(pDirStart, '\\');
    }

    if (!pSlash) {
        if (ppBaseName) *ppBaseName = pDirStart;
        return XFT_UNKNOWN;
    }

    if (ppBaseName) *ppBaseName = pSlash + 1;

    dirLen = (size_t)(pSlash - pDirStart);

    /* 1. Exact directory match */
    for (i = 0; g_directories[i].pDirectory; i++) {
        size_t entryLen = 0;
        if (g_directories[i].pDirectory[0] == 0) continue;
        entryLen = xx_rt_strlen(g_directories[i].pDirectory);
        if ((dirLen == entryLen) && (tar_strnicmp_ascii(pDirStart, g_directories[i].pDirectory, dirLen) == 0)) {
            return g_directories[i].fileType;
        }
    }

    /* 2. Suffix directory match if archive has top-level prefix like "db/PE/..." */
    {
        const char *pFirstSlash = xx_rt_strchr(pDirStart, '/');
        if (!pFirstSlash) pFirstSlash = xx_rt_strchr(pDirStart, '\\');
        if (pFirstSlash && pFirstSlash < pSlash) {
            const char *pSub = pFirstSlash + 1;
            size_t subLen = (size_t)(pSlash - pSub);
            for (i = 0; g_directories[i].pDirectory; i++) {
                size_t entryLen = 0;
                if (g_directories[i].pDirectory[0] == 0) continue;
                entryLen = xx_rt_strlen(g_directories[i].pDirectory);
                if ((subLen == entryLen) && (tar_strnicmp_ascii(pSub, g_directories[i].pDirectory, subLen) == 0)) {
                    return g_directories[i].fileType;
                }
            }
        }
    }

    return XFT_UNKNOWN;
}

static int db_create_tar_internal(const char *pDbPath, const char *pTarPath, int bPrecompile)
{
    xx_io_device *pTar = NULL;
    JSCtx *pJs = NULL;
    int i = 0;
    int bResult = 1;

    if (!pDbPath || !pTarPath || !xx_fs_is_dir(pDbPath)) {
        return 0;
    }

    pTar = xx_io_file_open(pTarPath, "wb");
    if (!pTar) {
        return 0;
    }

    if (bPrecompile) {
        pJs = js_new();
    }

    for (i = 0; g_directories[i].pDirectory; i++) {
        char *pDirPath = NULL;
        xx_list_t list;
        size_t nCount = 0;
        size_t j = 0;

        if (g_directories[i].pDirectory[0] == 0) {
            pDirPath = cd_strdup(pDbPath);
        } else {
            pDirPath = xx_fs_path_join(pDbPath, g_directories[i].pDirectory);
        }

        if (!pDirPath || !xx_fs_is_dir(pDirPath)) {
            cd_free(pDirPath);
            continue;
        }

        if (!xx_list_init(&list, sizeof(xx_fs_entry_t *), NULL)) {
            cd_free(pDirPath);
            bResult = 0;
            break;
        }

        if (xx_fs_list_dir_sorted(pDirPath, &list, XX_FS_SORT_NAME)) {
            intptr_t nDirectory;
            nCount = xx_list_count(&list);
            nDirectory = nCount ? xx_io_platform_read_directory_open(pDirPath) : -1;
            for (j = 0; j < nCount; j++) {
                xx_fs_entry_t *pEntry = *(xx_fs_entry_t **)xx_list_at(&list, j);
                if (bResult && pEntry && pEntry->type == XX_FS_ENTRY_FILE && is_signature_path(pEntry->path)) {
                    size_t nFileSize = 0;
                    char *pText = NULL;
                    /* Compilation uses the same regular-file reader and
                     * database-owned buffer as directory loading. */
                    if (nDirectory != -1) {
                        pText = db_read_signature_at(nDirectory, pEntry->name, &nFileSize);
                    } else if (xx_fs_is_file(pEntry->path)) {
                        pText = db_read_signature(pEntry->path, &nFileSize);
                    }
                    if (pText) {
                        char sRelPath[256];
                        char *pC = NULL;

                        if (g_directories[i].pDirectory[0] == 0) {
                            xx_rt_strncpy(sRelPath, pEntry->name, sizeof(sRelPath) - 1);
                        } else {
                            xx_rt_snprintf(sRelPath, sizeof(sRelPath), "%s/%s", g_directories[i].pDirectory, pEntry->name);
                        }
                        sRelPath[sizeof(sRelPath) - 1] = '\0';

                        for (pC = sRelPath; *pC; pC++) {
                            if (*pC == '\\') *pC = '/';
                        }

                        if (bPrecompile && pJs && is_signature_name(pEntry->name)) {
                            size_t nBcSize = 0;
                            void *pBc = js_compile_to_bytecode(pJs, pText, pEntry->name, &nBcSize);
                            if (pBc && nBcSize > 0) {
                                bResult = tar_write_header(pTar, sRelPath, nBcSize) &&
                                          tar_write_data(pTar, pBc, nBcSize);
                                xx_rt_free(pBc);
                            } else {
                                bResult = tar_write_header(pTar, sRelPath, (size_t)nFileSize) &&
                                          tar_write_data(pTar, pText, (size_t)nFileSize);
                            }
                        } else {
                            bResult = tar_write_header(pTar, sRelPath, (size_t)nFileSize) &&
                                      tar_write_data(pTar, pText, (size_t)nFileSize);
                        }

                        cd_free(pText);
                    } else {
                        bResult = 0;
                    }
                }
                if (pEntry) {
                    xx_fs_entry_free(pEntry);
                }
            }
            if (nDirectory != -1) xx_io_platform_close_descriptor(nDirectory);
        } else {
            nCount = xx_list_count(&list);
            for (j = 0; j < nCount; ++j) {
                xx_fs_entry_t *pEntry = *(xx_fs_entry_t **)xx_list_at(&list, j);
                if (pEntry) xx_fs_entry_free(pEntry);
            }
            bResult = 0;
        }

        xx_list_cleanup(&list);
        cd_free(pDirPath);
        if (!bResult) break;
    }

    if (bResult) bResult = db_write_metadata_tar(pTar, pDbPath);
    if (bResult) bResult = tar_write_end(pTar);
    xx_io_close(pTar);

    if (pJs) {
        js_free(pJs);
    }

    return bResult;
}

int db_create_tar(const char *pDbPath, const char *pTarPath)
{
    return db_create_tar_internal(pDbPath, pTarPath, 0);
}

int db_create_tar_precompiled(const char *pDbPath, const char *pTarPath)
{
    return db_create_tar_internal(pDbPath, pTarPath, 1);
}

#include "xx_die_audio_bundle.inc"

static int db_load_builtin_audio_subset(DBase *pDb, DBKind kind, int bMusicOnly)
{
    int nBefore;
    size_t i;

    if (!pDb) return 0;
    nBefore = pDb->nCount;
    for (i = 0; i < sizeof(xx_die_audio_entries) / sizeof(xx_die_audio_entries[0]); i++) {
        const xx_die_audio_entry *entry = &xx_die_audio_entries[i];
        const char *name = xx_rt_strrchr(entry->path, '/');
        char *text;
        size_t size = 0;

        if (bMusicOnly && entry->file_type == XFT_BINARY && x_strcmp(entry->path, "Binary/_init") != 0 && x_strcmp(entry->path, "Binary/audio.1.sg") != 0) continue;
        if (entry->original_size == SIZE_MAX || entry->compressed_size == 0) break;
        text = (char *)cd_malloc(entry->original_size + 1U);
        if (!xx_deflate_decompress_memory(entry->deflated, entry->compressed_size, text, entry->original_size, &size, false) || size != entry->original_size) {
            cd_free(text);
            break;
        }
        text[size] = '\0';
        db_add(pDb, name ? name + 1 : entry->path, entry->path, text, size, entry->file_type, kind);
    }
    if (i != sizeof(xx_die_audio_entries) / sizeof(xx_die_audio_entries[0])) {
        while (pDb->nCount > nBefore) {
            DBSignature *record = &pDb->pRecords[--pDb->nCount];
            cd_free(record->pName);
            cd_free(record->pFilePath);
            cd_free(record->pText);
            cd_free(record->pTags);
        }
        return 0;
    }
    db_apply_stored_metadata(pDb, nBefore);
    xx_scan_sort_database_records(pDb);
    if (pDb->nCount > nBefore) db_prepare_program_cache(pDb);
    return 1;
}

int db_load_builtin_audio(DBase *pDb, DBKind kind)
{
    return db_load_builtin_audio_subset(pDb, kind, 0);
}

int db_load_builtin_music(DBase *pDb, DBKind kind)
{
    return db_load_builtin_audio_subset(pDb, kind, 1);
}

int db_load_tar(DBase *pDb, const char *pTarPath, DBKind kind)
{
    xx_io_device *pFile = NULL;
    int nBefore = 0;
    char header[512];
    db_metadata_blob *pMetadata = NULL;
    size_t nMetadataCount = 0;
    size_t nMetadataCapacity = 0;
    int bReadError = 0;

    if (!pDb || !pTarPath || !pTarPath[0]) {
        return 0;
    }

    pFile = xx_io_file_open(pTarPath, "rb");
    if (!pFile) {
        return 0;
    }

    /* TAR members are read in order. A bounded read-ahead buffer amortizes
     * the small header/payload reads without retaining the whole archive.
     * Failed wrapping leaves the original file device owned here. */
    {
        xx_io_device *pBuffered = xx_io_buffer_open(pFile, 64U * 1024U, 0, true);
        if (pBuffered) pFile = pBuffered;
    }

    nBefore = pDb->nCount;

    for (;;) {
        ssize_t nHeaderRead = xx_io_read(pFile, header, 512);
        uint64_t nSize = 0;
        char typeflag = 0;
        char sName[256];
        char *pC = NULL;
        char *pData = NULL;
        size_t nPad = 0;
        const char *pBaseName = NULL;
        XFileType fileType = XFT_UNKNOWN;

        if (nHeaderRead == 0) break;
        if (nHeaderRead != 512) {
            bReadError = 1;
            break;
        }
        if (header[0] == '\0') {
            break;
        }

        nSize = tar_parse_octal(&header[124], 12);
        typeflag = header[156];

        if (header[345] != '\0') {
            xx_rt_snprintf(sName, sizeof(sName), "%s/%s", &header[345], header);
        } else {
            xx_rt_strncpy(sName, header, 100);
            sName[100] = '\0';
        }
        sName[sizeof(sName) - 1] = '\0';

        for (pC = sName; *pC; pC++) {
            if (*pC == '\\') *pC = '/';
        }

        if (nSize >= (uint64_t)SIZE_MAX) {
            bReadError = 1;
            break;
        }
        pData = (char *)cd_malloc((size_t)nSize + 1U);
        if (!pData) {
            bReadError = 1;
            break;
        }

        if (nSize > 0) {
            if (xx_io_read(pFile, pData, (size_t)nSize) != (ssize_t)nSize) {
                cd_free(pData);
                bReadError = 1;
                break;
            }
        }
        pData[nSize] = '\0';

        nPad = (512 - (nSize % 512)) % 512;
        if (nPad > 0) {
            /* The header fields are already copied. Consume padding into the
             * spare header buffer so sequential reads keep stdio's buffer. */
            if (xx_io_read(pFile, header, nPad) != (ssize_t)nPad) {
                cd_free(pData);
                bReadError = 1;
                break;
            }
        }

        if (typeflag == '5') {
            cd_free(pData);
            continue;
        }

        fileType = file_type_from_tar_path(sName, &pBaseName);

        if (db_metadata_tar_path_type(sName, &fileType)) {
            if (nMetadataCount == nMetadataCapacity) {
                nMetadataCapacity = nMetadataCapacity ? nMetadataCapacity * 2U : 8U;
                pMetadata = (db_metadata_blob *)cd_realloc(pMetadata, nMetadataCapacity * sizeof(*pMetadata));
            }
            pMetadata[nMetadataCount].fileType = fileType;
            pMetadata[nMetadataCount].pText = pData;
            pMetadata[nMetadataCount].nSize = (size_t)nSize;
            ++nMetadataCount;
        } else if (is_signature_name(pBaseName)) {
            db_add(pDb, pBaseName, sName, pData, (size_t)nSize, fileType, kind);
        } else {
            cd_free(pData);
        }
    }

    xx_io_close(pFile);

    {
        size_t i;
        if (!bReadError && pDb->nCount > nBefore) {
            db_apply_stored_metadata(pDb, nBefore);
            for (i = 0; i < nMetadataCount; ++i) {
                db_apply_metadata_text(pDb, kind, pMetadata[i].fileType,
                                       pMetadata[i].pText, pMetadata[i].nSize);
            }
        }
        for (i = 0; i < nMetadataCount; ++i) {
            cd_free(pMetadata[i].pText);
        }
        cd_free(pMetadata);
    }

    if (bReadError) {
        while (pDb->nCount > nBefore) {
            DBSignature *pRecord = &pDb->pRecords[--pDb->nCount];
            cd_free(pRecord->pName);
            cd_free(pRecord->pFilePath);
            cd_free(pRecord->pText);
            cd_free(pRecord->pTags);
        }
        return 0;
    }

    if (pDb->nCount > nBefore) db_sort_loaded_records(pDb, nBefore, kind);

    if (pDb->nCount > nBefore) db_prepare_program_cache(pDb);
    return (pDb->nCount > nBefore) ? 1 : 0;
}

int db_load(DBase *pDb, const char *pPath, DBKind kind)
{
    int i = 0;
    int nBefore;

    if (!pDb) return 0;
    nBefore = pDb->nCount;

    if ((pPath == NULL) || (pPath[0] == 0)) {
        return 0;
    }

    if (xx_fs_is_file(pPath)) {
        return db_load_tar(pDb, pPath, kind);
    }

    if (!xx_fs_is_dir(pPath)) {
        return 0;
    }

    for (i = 0; g_directories[i].pDirectory; i++) {
        if (g_directories[i].pDirectory[0] == 0) {
            db_load_directory(pDb, pPath, g_directories[i].fileType, kind);
        } else {
            /* Always a non-empty right side -- the empty entry takes the
             * branch above -- so xx_fs_path_join's rule of only inserting a
             * separator when there is something to separate never differs
             * here from cdie's of always inserting one. */
            char *pSubPath = xx_fs_path_join(pPath, g_directories[i].pDirectory);

            db_load_directory(pDb, pSubPath, g_directories[i].fileType, kind);
            xx_str_free(pSubPath);
        }
    }

    /* Metadata from the main database also describes db_custom. Apply the
     * retained catalog before reading an optional local metadata directory. */
    if (pDb->nCount > nBefore) {
        db_apply_stored_metadata(pDb, nBefore);
        db_load_metadata_directory(pDb, pPath, kind);
    }

    db_sort_loaded_records(pDb, nBefore, kind);

    if (pDb->nCount > nBefore) db_prepare_program_cache(pDb);
    return (pDb->nCount > nBefore) ? 1 : 0;
}

void db_free(DBase *pDb)
{
    int i = 0;

    for (i = 0; i < pDb->nCount; i++) {
        cd_free(pDb->pRecords[i].pName);
        cd_free(pDb->pRecords[i].pFilePath);
        cd_free(pDb->pRecords[i].pText);
        cd_free(pDb->pRecords[i].pTags);
    }

    for (i = 0; i < pDb->nMetadataCount; i++) {
        cd_free(pDb->pMetadata[i].pName);
        cd_free(pDb->pMetadata[i].pTags);
    }

    js_program_cache_free((JSProgramCache *)pDb->pJsProgramCache);
    cd_free(pDb->pRecords);
    cd_free(pDb->pMetadata);
    x_memset(pDb, 0, sizeof(*pDb));
}
