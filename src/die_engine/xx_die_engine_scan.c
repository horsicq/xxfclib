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

/* xx_die_engine_scan.c - scan orchestration: pick the file type, run the _init scripts and
 * then every applicable detection script in database order.               */

#include "xx_die_engine_internal.h"
#include "xx_die_engine_xdisasm.h"
#include "xxfclib/fs/xx_fs.h"
#include "xxfclib/list/xx_list.h"
#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/strings/xx_string.h"


typedef struct {
    int bTypes[XFT_COUNT];
} DieFileTypeSet;

static const char *g_die_file_type_names[XFT_COUNT] = {
    "Unknown", "Binary", "COM", "MSDOS", "NE", "LE", "LX", "PE",
    "PE32", "PE64", "ELF", "ELF32", "ELF64", "Mach-O", "Mach-O32",
    "Mach-O64", "ZIP", "JAR", "APK", "IPA", "DEX", "NPM", "Mach-O FAT",
    "Archive", "PDF", "CFBF", "Image", "JPEG", "PNG", "RAR", "ISO 9660",
    "Amiga Hunk", "Atari ST", "Java Class", "Python Bytecode", "DOS/16M",
    "DOS/4G", ".NET"
};

const char *xft_to_string(XFileType type)
{
    if ((cd_u32)type < (cd_u32)XFT_COUNT) return g_die_file_type_names[type];
    return "Unknown";
}

int xft_check(XFileType databaseType, XFileType fileType)
{
    if (databaseType == fileType) return 1;
    if (databaseType == XFT_PE && (fileType == XFT_PE32 || fileType == XFT_PE64)) return 1;
    if (databaseType == XFT_ELF && (fileType == XFT_ELF32 || fileType == XFT_ELF64)) return 1;
    if (databaseType == XFT_MACHO && (fileType == XFT_MACHO32 || fileType == XFT_MACHO64)) return 1;
    return 0;
}

static void die_file_type_add(DieFileTypeSet *set, XFileType type)
{
    if ((cd_u32)type < (cd_u32)XFT_COUNT) set->bTypes[type] = 1;
}

static int die_file_type_contains(const DieFileTypeSet *set, XFileType type)
{
    return set && (cd_u32)type < (cd_u32)XFT_COUNT && set->bTypes[type];
}

static int die_file_is_cli_assembly(DieFile *file)
{
    cd_i64 pe_offset;
    cd_i64 optional_offset;
    cd_i64 data_directory;
    cd_u16 magic;
    cd_u32 directory_count;
    if (!file || file->nSize < 0x40) return 0;
    pe_offset = (cd_i64)xx_io_get_u32(file->pDevice, 0x3c, false);
    if (pe_offset < 0 || pe_offset > file->nSize - 24 ||
        xx_io_get_u32(file->pDevice, pe_offset, false) != 0x00004550) return 0;
    optional_offset = pe_offset + 24;
    magic = xx_io_get_u16(file->pDevice, optional_offset, false);
    if (magic == 0x10b) {
        directory_count = xx_io_get_u32(file->pDevice, optional_offset + 92, false);
        data_directory = optional_offset + 96;
    } else if (magic == 0x20b) {
        directory_count = xx_io_get_u32(file->pDevice, optional_offset + 108, false);
        data_directory = optional_offset + 112;
    } else return 0;
    if (directory_count <= 14 || data_directory > file->nSize - (14 * 8 + 8)) return 0;
    return xx_io_get_u32(file->pDevice, data_directory + 14 * 8, false) != 0 &&
           xx_io_get_u32(file->pDevice, data_directory + 14 * 8 + 4, false) != 0;
}

static int die_file_type_set_detect(DieFile *file, DieFileTypeSet *set)
{
    xx_scan_options options;
    xx_list_t *types;
    size_t index;
    uint8_t signature[2];
    if (!file || !file->pDevice || !set) return 0;
    x_memset(set, 0, sizeof(*set));
    die_file_type_add(set, XFT_BINARY);
    x_memset(&options, 0, sizeof(options));
    options.size = -1;
    options.file_name = file->pFileName;
    types = xx_scan_get_file_types(NULL, file->pDevice, &options, NULL);
    if (!types) return 0;
    if (file->nSize >= 2 && xx_io_read_at(file->pDevice, 0, signature, sizeof(signature)) &&
        ((signature[0] == 'M' && signature[1] == 'Z') ||
         (signature[0] == 'Z' && signature[1] == 'M')))
        die_file_type_add(set, XFT_MSDOS);
    for (index = 0; index < xx_list_count(types); ++index) {
        xx_file_type_t type = *(xx_file_type_t *)xx_list_at(types, index);
        switch (type) {
            case XX_FILE_TYPE_PE32:
                die_file_type_add(set, XFT_MSDOS); die_file_type_add(set, XFT_PE);
                die_file_type_add(set, XFT_PE32);
                if (die_file_is_cli_assembly(file)) die_file_type_add(set, XFT_CLI_ASSEMBLY);
                break;
            case XX_FILE_TYPE_PE64:
                die_file_type_add(set, XFT_MSDOS); die_file_type_add(set, XFT_PE);
                die_file_type_add(set, XFT_PE64);
                if (die_file_is_cli_assembly(file)) die_file_type_add(set, XFT_CLI_ASSEMBLY);
                break;
            case XX_FILE_TYPE_ELF32:
                die_file_type_add(set, XFT_ELF); die_file_type_add(set, XFT_ELF32); break;
            case XX_FILE_TYPE_ELF64:
                die_file_type_add(set, XFT_ELF); die_file_type_add(set, XFT_ELF64); break;
            case XX_FILE_TYPE_MACHO32:
                die_file_type_add(set, XFT_MACHO); die_file_type_add(set, XFT_MACHO32); break;
            case XX_FILE_TYPE_MACHO64:
                die_file_type_add(set, XFT_MACHO); die_file_type_add(set, XFT_MACHO64); break;
            case XX_FILE_TYPE_ZIP:
                die_file_type_add(set, XFT_ZIP); die_file_type_add(set, XFT_ARCHIVE); break;
            case XX_FILE_TYPE_JAR:
                die_file_type_add(set, XFT_ZIP); die_file_type_add(set, XFT_ARCHIVE);
                die_file_type_add(set, XFT_JAR); break;
            case XX_FILE_TYPE_APK:
                die_file_type_add(set, XFT_ZIP); die_file_type_add(set, XFT_ARCHIVE);
                die_file_type_add(set, XFT_APK); die_file_type_add(set, XFT_JAR); break;
            case XX_FILE_TYPE_IPA:
                die_file_type_add(set, XFT_ZIP); die_file_type_add(set, XFT_ARCHIVE);
                die_file_type_add(set, XFT_IPA); break;
            case XX_FILE_TYPE_NPM:
                die_file_type_add(set, XFT_NPM); die_file_type_add(set, XFT_ARCHIVE); break;
            case XX_FILE_TYPE_DOS16M: die_file_type_add(set, XFT_DOS16M); break;
            case XX_FILE_TYPE_DOS4G: die_file_type_add(set, XFT_DOS4G); break;
            case XX_FILE_TYPE_NE: die_file_type_add(set, XFT_NE); break;
            case XX_FILE_TYPE_LE: die_file_type_add(set, XFT_LE); break;
            case XX_FILE_TYPE_LX: die_file_type_add(set, XFT_LX); break;
            case XX_FILE_TYPE_MACHOFAT: die_file_type_add(set, XFT_MACHOFAT); break;
            case XX_FILE_TYPE_JAVA_CLASS: die_file_type_add(set, XFT_JAVACLASS); break;
            case XX_FILE_TYPE_DEX: die_file_type_add(set, XFT_DEX); break;
            case XX_FILE_TYPE_PYC: die_file_type_add(set, XFT_PYC); break;
            case XX_FILE_TYPE_PDF: die_file_type_add(set, XFT_PDF); break;
            case XX_FILE_TYPE_CFBF: die_file_type_add(set, XFT_CFBF); break;
            case XX_FILE_TYPE_JPEG:
                die_file_type_add(set, XFT_JPEG); die_file_type_add(set, XFT_IMAGE); break;
            case XX_FILE_TYPE_PNG:
                die_file_type_add(set, XFT_PNG); die_file_type_add(set, XFT_IMAGE); break;
            case XX_FILE_TYPE_RAR:
                die_file_type_add(set, XFT_RAR); die_file_type_add(set, XFT_ARCHIVE); break;
            case XX_FILE_TYPE_ISO9660: die_file_type_add(set, XFT_ISO9660); break;
            case XX_FILE_TYPE_AMIGAHUNK: die_file_type_add(set, XFT_AMIGAHUNK); break;
            case XX_FILE_TYPE_ATARIST: die_file_type_add(set, XFT_ATARIST); break;
            case XX_FILE_TYPE_COM: die_file_type_add(set, XFT_COM); break;
            default: break;
        }
    }
    xx_list_destroy(types);
    return 1;
}

void scan_options_init(ScanOptions *pOptions)
{
    x_memset(pOptions, 0, sizeof(*pOptions));
    pOptions->bShowType = 1;
    pOptions->bShowVersion = 1;
    pOptions->bShowInfo = 1;
    pOptions->bUseCustomDatabase = 1;
    pOptions->bUseExtraDatabase = 1;
    pOptions->bSort = 1;
}

void scan_options_free(ScanOptions *pOptions)
{
    cd_free(pOptions->pMainDatabasePath);
    cd_free(pOptions->pExtraDatabasePath);
    cd_free(pOptions->pCustomDatabasePath);
    x_memset(pOptions, 0, sizeof(*pOptions));
}

void scan_result_free(ScanResult *pResult)
{
    int i = 0;

    for (i = 0; i < pResult->nCount; i++) {
        cd_free(pResult->pRecords[i].pType);
        cd_free(pResult->pRecords[i].pName);
        cd_free(pResult->pRecords[i].pVersion);
        cd_free(pResult->pRecords[i].pInfo);
        cd_free(pResult->pRecords[i].pFormatId);
    }

    cd_free(pResult->pRecords);

    for (i = 0; i < pResult->nErrorCount; i++) {
        cd_free(pResult->ppErrors[i]);
    }

    cd_free(pResult->ppErrors);
    cd_free(pResult->pFileName);
    x_memset(pResult, 0, sizeof(*pResult));
}

static void result_add_error(ScanResult *pResult, const char *pText)
{
    pResult->ppErrors = (char **)cd_realloc(pResult->ppErrors, (size_t)(pResult->nErrorCount + 1) * sizeof(char *));
    pResult->ppErrors[pResult->nErrorCount++] = cd_strdup(pText);
}

static char *signature_normalize(const char *pText)
{
    int bInsideQuote = 0;
    size_t i;

    /* XBinary::convertSignature rejects an unterminated quoted literal.
     * The general buffer notation intentionally accepts partial literals. */
    if (pText) {
        for (i = 0; pText[i]; i++) {
            if (pText[i] == '\'') {
                bInsideQuote = !bInsideQuote;
            }
        }
    }
    if (bInsideQuote) {
        return cd_strdup("");
    }
    return xx_data_sig_normalize(pText);
}

static void signature_set_error(DieEngine *pEngine, const char *pText)
{
    static const char sPrefix[] = "Invalid signature: ";
    size_t nLength = x_strlen(pText);

    cd_free(pEngine->pLastError);
    pEngine->pLastError = (char *)cd_malloc(sizeof(sPrefix) + nLength);
    x_memcpy(pEngine->pLastError, sPrefix, sizeof(sPrefix) - 1);
    x_memcpy(pEngine->pLastError + sizeof(sPrefix) - 1, pText, nLength + 1);

    if (x_getenv("CDIE_TRACE")) {
        x_fprintf(x_stderr(), "[cdie] %s: %s\n",
                  pEngine->pCurrentScript ? pEngine->pCurrentScript : "",
                  pEngine->pLastError);
    }
}

/* The general buffer parser retains partial records for its callers. DiE
 * rejects incomplete signatures, including odd nibble/pointer runs, rather
 * than matching the successfully parsed prefix. */
static int signature_valid(const char *pNormalized, xx_data_signature *pSignature,
                             int *pParserError)
{
    int bValid = xx_data_signature_parse(pSignature, pNormalized) ? 1 : 0;
    size_t i = 0;
    int j = 0;

    if (pParserError) {
        *pParserError = !bValid;
    }

    for (i = 0; pNormalized[i]; i++) {
        char c = pNormalized[i];

        if ((c == '.') || (c == '*') || (c == '$')) {
            size_t nStart = i;
            while (pNormalized[i + 1] == c) {
                i++;
            }
            if ((i - nStart + 1) % 2) {
                bValid = 0;
            }
        } else if (c == '+') {
            while (pNormalized[i + 1] == '+') {
                i++;
            }
            if (!((pNormalized[i + 1] >= '0' && pNormalized[i + 1] <= '9') ||
                  (pNormalized[i + 1] >= 'a' && pNormalized[i + 1] <= 'f'))) {
                bValid = 0;
            }
        } else if (c == '%') {
            if (pNormalized[i + 1] != '%' && pNormalized[i + 1] != '&') {
                bValid = 0;
            } else {
                i++;
            }
        } else if (c == '!' || c == '_') {
            if (pNormalized[i + 1] != '%') {
                bValid = 0;
            } else {
                i++;
            }
        } else if (c == '#') {
            size_t nAddressChars = 0;
            int bInBase = 0;
            size_t k = i;

            for (; pNormalized[k]; k++) {
                char cAddress = pNormalized[k];
                if (cAddress == '#') {
                    nAddressChars++;
                } else if (cAddress == '[') {
                    if (bInBase) bValid = 0;
                    bInBase = 1;
                } else if (cAddress == ']') {
                    if (!bInBase) bValid = 0;
                    bInBase = 0;
                } else if (bInBase) {
                    if (!((cAddress >= '0' && cAddress <= '9') ||
                          (cAddress >= 'a' && cAddress <= 'f'))) bValid = 0;
                } else {
                    break;
                }
            }
            if (bInBase || nAddressChars % 2) bValid = 0;
            i = k - 1;
        } else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
            size_t nStart = i;
            while ((pNormalized[i + 1] >= '0' && pNormalized[i + 1] <= '9') ||
                   (pNormalized[i + 1] >= 'a' && pNormalized[i + 1] <= 'f')) {
                i++;
            }
            if ((i - nStart + 1) % 2) {
                bValid = 0;
                if (pParserError) *pParserError = 1;
            }
        } else {
            bValid = 0;
        }
    }
    for (j = 0; j < pSignature->count; j++) {
        const xx_data_sig_record *pRecord = &pSignature->records[j];
        if (((pRecord->kind == XX_DATA_SIG_REL_OFFSET) ||
             (pRecord->kind == XX_DATA_SIG_ADDRESS)) &&
            (pRecord->address_size != 1) && (pRecord->address_size != 2) &&
            (pRecord->address_size != 4) && (pRecord->address_size != 8)) {
            bValid = 0;
        }
    }
    return bValid;
}

/* The reference's cached path compares hexadecimal characters, not parsed
 * records. It accepts a single wildcard nibble and odd hexadecimal lengths.
 * QString::mid also clips a negative starting position against the cache. */
static int signature_compare_nibbles(DieFile *file, cd_i64 offset,
                                     const char *text, size_t length)
{
    static const char digits[] = "0123456789abcdef";
    size_t total = length / 2 + length % 2, done = 0;
    while (done < total) {
        size_t count = total - done, i;
        const unsigned char *view = die_file_window(file, offset + (cd_i64)done, &count);
        if (!view) return 0;
        for (i = 0; i < count; ++i) {
            size_t position = (done + i) * 2;
            if (text[position] != '.' && text[position] != digits[view[i] >> 4]) return 0;
            if (position + 1 < length && text[position + 1] != '.' &&
                text[position + 1] != digits[view[i] & 15]) return 0;
        }
        done += count;
    }
    return 1;
}

static int signature_cached_compare(DieFile *pFile, cd_i64 nBase,
                                    cd_u64 nCacheBytes, cd_u64 nDistance,
                                    int bNegative, const char *pNormalized,
                                    size_t nSliceBytes)
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
    return signature_compare_nibbles(pFile, (cd_i64)nStart, pNormalized, nLength);
}

/* A normalized literal needs neither allocated records nor an address map.
 * Return -1 for forms that still need the full signature parser. Cached
 * nibble comparisons must run first because they also accept odd lengths. */
static int signature_literal_compare(DieFile *pFile, cd_i64 nOffset,
                                      const char *pNormalized)
{
    size_t nLength = x_strlen(pNormalized);
    size_t i;

    if (!nLength || (nLength & 1) || nLength > 256) return -1;
    for (i = 0; i < nLength; i++) {
        char c = pNormalized[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return -1;
    }
    if (nOffset < 0 || pFile->nSize < 0 ||
        (cd_u64)nOffset > (cd_u64)pFile->nSize ||
        (cd_u64)(nLength / 2) > (cd_u64)pFile->nSize - (cd_u64)nOffset) {
        return 0;
    }
    return signature_compare_nibbles(pFile, nOffset, pNormalized, nLength);
}

int die_engine_signature_compare(DieEngine *pEngine, cd_i64 nOffset,
                                  const char *pText, int nKind,
                                  cd_i64 nCacheBase)
{
    char *pNormalized = signature_normalize(pText);
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
    if (nKind != 3 && nCacheBase >= 0 && nCacheBase < pEngine->file.nSize &&
        (nKind != 2 || nCacheBase > 0)) {
        nCacheBytes = (cd_u64)(pEngine->file.nSize - nCacheBase);
        if (nCacheBytes > 256) nCacheBytes = 256;
        nDistance = bNegative ? (cd_u64)nCacheBase - (cd_u64)nOffset :
                               (cd_u64)nOffset - (cd_u64)nCacheBase;
        nCriterion = nKind == 1 ? nCacheBytes : nCacheBytes * 2;
        if (bNegative) {
            nCacheLimit = nDistance > (cd_u64)-1 - nCriterion ? (cd_u64)-1 :
                nDistance + nCriterion;
        } else if (nDistance < nCriterion) {
            nCacheLimit = nCriterion - nDistance;
        }
    }
    /* Header caching uses normalized length and bytes; EP/overlay caching
     * uses original text length and the hexadecimal cache's character count. */
    nSliceBytes = x_strlen(nKind == 1 ? pNormalized : (pText ? pText : ""));
    if ((cd_u64)nSliceBytes < nCacheLimit &&
        !x_strchr(pNormalized, '$') && !x_strchr(pNormalized, '#') &&
        !x_strchr(pNormalized, '+') && !x_strchr(pNormalized, '%') &&
        !x_strchr(pNormalized, '*')) {
        bResult = signature_cached_compare(&pEngine->file, nCacheBase,
                                           nCacheBytes, nDistance, bNegative,
                                           pNormalized, nSliceBytes);
        xx_str_free(pNormalized);
        return bResult;
    }

    bResult = signature_literal_compare(&pEngine->file, nOffset, pNormalized);
    if (bResult >= 0) {
        xx_str_free(pNormalized);
        return bResult;
    }

    bResult = 0;
    bValid = signature_valid(pNormalized, &signature, NULL);
    if (!bValid || signature.count == 0) {
        if (bReport) {
            signature_set_error(pEngine, pNormalized);
        }
    } else {
        bResult = pEngine->file.pData ?
            xx_data_signature_match(pEngine->file.pData, (size_t)pEngine->file.nSize,
                nOffset, &signature, &pEngine->sigContext, NULL) :
            xx_io_signature_match(pEngine->file.pDevice, nOffset, &signature,
                &pEngine->sigContext, NULL);
    }
    xx_data_signature_free(&signature);
    xx_str_free(pNormalized);
    return bResult;
}

cd_i64 die_engine_signature_find(DieEngine *pEngine, cd_i64 nOffset,
                                  cd_i64 nSize, const char *pText)
{
    char *pNormalized;
    xx_data_signature signature;
    int bValid;
    int bParserError = 0;
    cd_i64 nResult = -1;

    /* The reference rejects an empty search range before parsing. An empty
     * search signature simply has no match and does not set a progress error. */
    if (nOffset < 0 || nOffset >= pEngine->file.nSize || nSize == 0 || nSize < -1) {
        return -1;
    }
    pNormalized = signature_normalize(pText);
    if (!pNormalized) {
        return -1;
    }
    bValid = signature_valid(pNormalized, &signature, &bParserError);
    if (!bValid) {
        /* _getSignatureBytes sets the progress message for lexical errors;
         * other invalid record shapes only make isSignatureValid false. */
        if (bParserError) {
            signature_set_error(pEngine, pNormalized);
        }
    } else if (signature.count == 1 &&
               signature.records[0].kind == XX_DATA_SIG_BYTES) {
        /* Validation already parsed this literal. Reuse its bytes instead
         * of normalizing and parsing the same signature a second time. */
        nResult = die_engine_literal_find(pEngine, nOffset, nSize,
                                           signature.records[0].data,
                                           signature.records[0].data_size);
    } else if (signature.count > 0) {
        nResult = pEngine->file.pData ?
            xx_data_signature_find_text(pEngine->file.pData, (size_t)pEngine->file.nSize,
                nOffset, nSize, pText, &pEngine->sigContext) :
            xx_io_signature_find_text(pEngine->file.pDevice, nOffset, nSize, pText,
                &pEngine->sigContext);
    }
    xx_data_signature_free(&signature);
    xx_str_free(pNormalized);
    return nResult;
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

static int should_execute(DBSignature *pRecord, XFileType fileType, int bIsCliAssembly, ScanOptions *pOptions)
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

    if ((!pOptions->bDeepScan) && ((x_strcmp(sPrefix, "DS") == 0) || (x_strcmp(sPrefix, "EP") == 0))) {
        return 0;
    }

    if ((!pOptions->bHeuristicScan) && (x_strcmp(sPrefix, "HEUR") == 0)) {
        return 0;
    }

    if (x_strcmp(pRecord->pName, "_init") == 0) {
        return 0;
    }

    if (pRecord->databaseType == DB_MAIN) {
        return 1;
    }

    if (pOptions->bUseCustomDatabase && (pRecord->databaseType == DB_CUSTOM)) {
        return 1;
    }

    if (pOptions->bUseExtraDatabase && (pRecord->databaseType == DB_EXTRA)) {
        return 1;
    }

    return 0;
}

static int sort_records(const void *pLeft, const void *pRight)
{
    const ScanRecord *pA = (const ScanRecord *)pLeft;
    const ScanRecord *pB = (const ScanRecord *)pRight;

    if (pA->nPrio != pB->nPrio) {
        return (pA->nPrio < pB->nPrio) ? -1 : 1;
    }

    return 0;
}

/* Stable insertion sort: std::sort in the reference engine behaves like a
 * stable sort for the small result lists produced by a single scan.        */
static void stable_sort_records(ScanRecord *pRecords, int nCount)
{
    int i = 0;
    int j = 0;

    for (i = 1; i < nCount; i++) {
        ScanRecord key = pRecords[i];

        j = i - 1;

        while ((j >= 0) && (sort_records(&pRecords[j], &key) > 0)) {
            pRecords[j + 1] = pRecords[j];
            j--;
        }

        pRecords[j + 1] = key;
    }
}

/* DOS MZ files can carry an archive after the image.  DiE reports the ZIP
 * overlay even when the executable itself is packed (for example PKLITE).
 * The PE parser already exposes overlays; retain the corresponding DOS case
 * here so the C console produces the same record. */
static void add_msdos_zip_overlay(DieEngine *pEngine)
{
    unsigned char header[6];
    unsigned char magic[4];
    cd_i64 nSize = pEngine->file.nSize;
    cd_i64 nOverlay = -1;
    cd_u16 nLastPage = 0;
    cd_u16 nPages = 0;
    ScanResult *pResult = pEngine->pResult;
    ScanRecord *pRecord = NULL;

    if ((pEngine->fileType != XFT_MSDOS) || (nSize < 6) ||
        !die_file_read_at(&pEngine->file, 0, header, sizeof(header)) ||
        (header[0] != 'M') || (header[1] != 'Z')) {
        return;
    }

    nLastPage = (cd_u16)header[2] | ((cd_u16)header[3] << 8);
    nPages = (cd_u16)header[4] | ((cd_u16)header[5] << 8);
    nOverlay = (cd_i64)nPages * 512;
    if (nLastPage) {
        nOverlay -= 512 - nLastPage;
    }

    if ((nOverlay < 0) || (nOverlay > nSize) || (nSize - nOverlay < 4) ||
        !die_file_read_at(&pEngine->file, nOverlay, magic, sizeof(magic)) ||
        (magic[0] != 'P') || (magic[1] != 'K') || (magic[2] != 3) || (magic[3] != 4)) {
        return;
    }

    if (pResult->nCount + 1 > pResult->nCapacity) {
        pResult->nCapacity = pResult->nCapacity ? (pResult->nCapacity * 2) : 16;
        pResult->pRecords = (ScanRecord *)cd_realloc(pResult->pRecords, (size_t)pResult->nCapacity * sizeof(ScanRecord));
    }

    pRecord = &pResult->pRecords[pResult->nCount++];
    x_memset(pRecord, 0, sizeof(*pRecord));
    pRecord->pType = cd_strdup("Overlay");
    pRecord->pName = cd_strdup("ZIP archive");
    pRecord->pVersion = cd_strdup("");
    pRecord->pInfo = cd_strdup("");
    pRecord->nPrio = die_engine_type_to_prio("Overlay");
}

/* XCOM::isValid, whole: a COM image is loaded at 0x100 and has to fit in the
 * 64 KiB segment, so anything no larger than this can be one.              */
#define XCOM_MAX_SIZE (0x10000 - 0x100)

/* XBinary::getDeviceFileSuffix(getDevice()).toUpper() == "COM". */
static int has_com_suffix(const char *pFileName)
{
    char *pSuffix = NULL;
    int bResult = 0;

    if (pFileName == NULL) {
        return 0;
    }

    pSuffix = xx_fs_path_suffix(pFileName);

    if (pSuffix == NULL) {
        return 0;
    }

    bResult = (cd_stricmp_ascii(pSuffix, "COM") == 0) ? 1 : 0;
    xx_str_free(pSuffix);

    return bResult;
}

static XFileType pick_file_type(DieFile *pFile, DieFileTypeSet *pSet)
{
    if (die_file_type_contains(pSet, XFT_PE32)) return XFT_PE32;
    if (die_file_type_contains(pSet, XFT_PE64)) return XFT_PE64;
    if (die_file_type_contains(pSet, XFT_ELF32)) return XFT_ELF32;
    if (die_file_type_contains(pSet, XFT_ELF64)) return XFT_ELF64;
    if (die_file_type_contains(pSet, XFT_MACHO32)) return XFT_MACHO32;
    if (die_file_type_contains(pSet, XFT_MACHO64)) return XFT_MACHO64;
    if (die_file_type_contains(pSet, XFT_LX)) return XFT_LX;
    if (die_file_type_contains(pSet, XFT_LE)) return XFT_LE;
    if (die_file_type_contains(pSet, XFT_NE)) return XFT_NE;
    if (die_file_type_contains(pSet, XFT_DOS16M)) return XFT_DOS16M;
    if (die_file_type_contains(pSet, XFT_DOS4G)) return XFT_DOS4G;
    if (die_file_type_contains(pSet, XFT_MSDOS)) return XFT_MSDOS;
    if (die_file_type_contains(pSet, XFT_APK)) return XFT_APK;
    if (die_file_type_contains(pSet, XFT_IPA)) return XFT_IPA;
    if (die_file_type_contains(pSet, XFT_JAR)) return XFT_JAR;
    if (die_file_type_contains(pSet, XFT_ZIP)) return XFT_ZIP;
    if (die_file_type_contains(pSet, XFT_DEX)) return XFT_DEX;
    if (die_file_type_contains(pSet, XFT_NPM)) return XFT_NPM;
    if (die_file_type_contains(pSet, XFT_MACHOFAT)) return XFT_MACHOFAT;
    if (die_file_type_contains(pSet, XFT_AMIGAHUNK)) return XFT_AMIGAHUNK;
    if (die_file_type_contains(pSet, XFT_PDF)) return XFT_PDF;
    if (die_file_type_contains(pSet, XFT_CFBF)) return XFT_CFBF;
    if (die_file_type_contains(pSet, XFT_RAR)) return XFT_RAR;
    if (die_file_type_contains(pSet, XFT_ISO9660)) return XFT_ISO9660;
    if (die_file_type_contains(pSet, XFT_JPEG)) return XFT_JPEG;
    if (die_file_type_contains(pSet, XFT_PNG)) return XFT_PNG;
    if (die_file_type_contains(pSet, XFT_JAVACLASS)) return XFT_JAVACLASS;
    if (die_file_type_contains(pSet, XFT_PYC)) return XFT_PYC;

    /* g_arrPrefFileTypeOrder puts FT_COM ahead of FT_TEXT/FT_DATA/FT_BINARY,
     * and XBinary::getFileTypes inserts FT_COM only when nothing else was
     * recognised (the set still holds just FT_BINARY) or the file is plain
     * text, the size leaves room for the 0x100-byte PSP, and the file suffix
     * uppercases to "COM". Reaching the fallback below is this port's form of
     * that first condition. A file without the suffix still meets the COM
     * scripts: scan_engine_run's binary arm runs them the way the reference's
     * last scanProcess branch does.                                        */
    if ((pFile->nSize <= XCOM_MAX_SIZE) && has_com_suffix(pFile->pFileName)) {
        return XFT_COM;
    }

    return XFT_BINARY;
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

    pEngine->pCurrentScript = pRecord->pName;
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
        bEvalResult = js_eval_nested_bytecode(pCtx, pRecord->pText, pRecord->nSize, pRecord->pName);
    } else {
        bEvalResult = js_eval_nested(pCtx, pRecord->pText, pRecord->pName);
    }

    if (!bEvalResult) {
        char sBuf[1024];

        x_snprintf(sBuf, sizeof(sBuf), "%s: %s", pRecord->pName, js_error(pCtx));
        result_add_error(pEngine->pResult, sBuf);
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

    args[0] = js_bool(pEngine->pOptions->bShowType);
    args[1] = js_bool(pEngine->pOptions->bShowVersion);
    args[2] = js_bool(pEngine->pOptions->bShowInfo);

    callResult = js_call(pCtx, detect, global, 3, args);

    if (js_has_exception(pCtx)) {
        char sBuf[1024];

        x_snprintf(sBuf, sizeof(sBuf), "%s: %s", pRecord->pName, js_error(pCtx));
        result_add_error(pEngine->pResult, sBuf);
        js_clear_error(pCtx);
    }

    js_release(pCtx, callResult);
    js_release(pCtx, detect);
    js_release(pCtx, global);

    end_script_profile(pEngine, pRecord, nProfileStart);
}

/* One detection pass over an already-populated DieFile for a single file type.
 * The file is borrowed, not closed, so the FT_COM arm below can run the pass
 * twice; the caller sorts the merged record list. bAddUnknown mirrors the
 * reference's _processDetect flag: when it is 0 an empty pass stays empty
 * instead of gaining the "Unknown" record.                                 */
static int scan_run_pass(DieFile *pOpenedFile, XFileType fileType, int bIsCliAssembly, DBase *pDb, ScanOptions *pOptions, ScanResult *pResult,
                         int bAddUnknown)
{
    /* Heap, not stack: DieEngine embeds every parser state by value and is
     * over 3 KB on its own. A frame that size walks past the guard page, and
     * the probe helper MSVC emits to prevent that (__chkstk) is a CRT symbol
     * the CDIE_NO_CRT link has no source for. cd_calloc never returns NULL
     * and zeroes, so it replaces the memset too. */
    DieEngine *pEngine = (DieEngine *)cd_calloc(1, sizeof(DieEngine));
    xx_memory_map binaryMap;
    int i = 0;
    int nGlobalInit = -1;
    int nTypeInit = -1;

    pEngine->file = *pOpenedFile;
    /* Each pass owns its decoder cache while borrowing the input bytes. */
    pEngine->file.pDisasmContext = NULL;

    pEngine->fileType = fileType;
    /* A .NET PE is typed both as PE and CLI assembly; the primary type stays
     * PE, but the CLI-assembly flag drives the DOTNET object and the
     * PE/DOTNET scripts. */
    pEngine->bIsCliAssembly = bIsCliAssembly;
    pEngine->pDb = pDb;
    pEngine->pOptions = pOptions;
    pEngine->pResult = pResult;

    if ((pEngine->fileType == XFT_PE32) || (pEngine->fileType == XFT_PE64)) {
        pEngine->bHasPE = xx_pe_inspect_analyze_from_device(&pEngine->pe, pEngine->file.pDevice, 0, NULL);
    } else if (pEngine->fileType == XFT_JPEG) {
        xx_jpeg_init(&pEngine->jpeg, pEngine->file.pDevice, 0);
        pEngine->bHasJpeg = xx_jpeg_analyze(&pEngine->jpeg, NULL);
    } else if (pEngine->fileType == XFT_PNG) {
        xx_png_init(&pEngine->png, pEngine->file.pDevice, 0);
        pEngine->bHasPng = xx_png_analyze(&pEngine->png, NULL);
    } else if (pEngine->fileType == XFT_APK) {
        xx_apk_init(&pEngine->apk, pEngine->file.pDevice, 0);
        pEngine->bHasApk = xx_apk_analyze(&pEngine->apk, NULL);
    } else if (pEngine->fileType == XFT_PDF) {
        /* The native reader borrows the shared buffered device. Its analysis
         * accepts inspection-only documents separately from archive validation. */
        xx_pdf_init(&pEngine->pdf, pEngine->file.pDevice, 0);
        pEngine->bHasPdf = xx_pdf_analyze(&pEngine->pdf, NULL);
    } else if ((pEngine->fileType == XFT_ELF) || (pEngine->fileType == XFT_ELF32) || (pEngine->fileType == XFT_ELF64)) {
        pEngine->bHasElf = xx_elf_inspect_analyze_from_device(&pEngine->elf, pEngine->file.pDevice, 0, NULL);
    } else if (pEngine->fileType == XFT_DEX) {
        xx_dex_init(&pEngine->dex, pEngine->file.pDevice, 0);
        pEngine->bHasDex = xx_dex_analyze(&pEngine->dex, NULL);
    } else if ((pEngine->fileType == XFT_MACHO) || (pEngine->fileType == XFT_MACHO32) || (pEngine->fileType == XFT_MACHO64)) {
        pEngine->bHasMach = xx_macho_inspect_analyze_from_device(&pEngine->mach, pEngine->file.pDevice, 0, NULL);
    } else if (pEngine->fileType == XFT_PYC) {
        xx_pyc_init(&pEngine->pyc, pEngine->file.pDevice, 0);
        pEngine->bHasPyc = xx_pyc_analyze(&pEngine->pyc, NULL);
    }

    /* Every ZIP-family container (also an APK, which additionally parses its
     * AndroidManifest above) gets its central-directory record list and
     * MANIFEST.MF read for the archive-record and manifest predicates. */
    if ((pEngine->fileType == XFT_APK) || (pEngine->fileType == XFT_JAR) || (pEngine->fileType == XFT_ZIP) ||
        (pEngine->fileType == XFT_NPM) || (pEngine->fileType == XFT_IPA)) {
        xx_zip_init(&pEngine->zip, pEngine->file.pDevice, 0);
        pEngine->bHasZip = xx_zip_analyze(&pEngine->zip, NULL);
    }

    if (pEngine->bHasPE) {
        pEngine->pMap = &pEngine->pe.map;
        pEngine->nBits = pEngine->pe.bIs64 ? 64 : 32;
    } else {
        pEngine->nBits = 32;
        xx_memory_map_init(&binaryMap);
        binaryMap.binary_size = pEngine->file.nSize;
        binaryMap.endian = XX_ENDIAN_LITTLE;
        binaryMap.mode = XX_MEMORY_MAP_MODE_DATA;
        /* Only the two real-mode types change how a signature is evaluated
         * -- they make a relative jump wrap inside its segment -- so those
         * are the ones worth translating out of the engine's own file-type
         * enum. Everything else evaluates the same either way. */
        binaryMap.file_type = (pEngine->fileType == XFT_COM)     ? XX_FILE_TYPE_COM
                              : (pEngine->fileType == XFT_MSDOS) ? XX_FILE_TYPE_MSDOS
                                                                 : XX_FILE_TYPE_BINARY;
        die_map_add_part(&binaryMap, 0, pEngine->file.nSize, 0, (cd_u64)pEngine->file.nSize, XX_FILE_PART_DATA, "Data");
        pEngine->pMap = &binaryMap;
    }

    /* FIRST_MATCH and read_past_end_as_zero together are what this engine
     * has always done, and what the signature databases were written
     * against; both are off by default in the library because refusing is
     * the better behaviour for anything new. */
    xx_data_sig_context_from_memory_map_ex(&pEngine->sigContext, pEngine->pMap,
                                           XX_MEMORY_MAP_LOOKUP_FIRST_MATCH,
                                           true);

    pResult->fileType = pEngine->fileType;

    pEngine->pJs = js_new();
    js_set_user(pEngine->pJs, pEngine);
    die_engine_install_api(pEngine);

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

        if (nGlobalInit >= 0 && !die_file_read_failed(&pEngine->file)) {
            run_script(pEngine, &pDb->pRecords[nGlobalInit], 0);
        }

        if (nTypeInit >= 0 && !die_file_read_failed(&pEngine->file)) {
            run_script(pEngine, &pDb->pRecords[nTypeInit], 0);
        }

        /* The DOTNET _init runs after the PE one, so it can build on it. */
        if (pEngine->bIsCliAssembly && (nCliInit >= 0) && !die_file_read_failed(&pEngine->file)) {
            run_script(pEngine, &pDb->pRecords[nCliInit], 0);
        }
    }

    /* cd_alloc_oom() is always false unless the soft out-of-memory policy is
     * armed, which only the shared library does; there it ends the scan at
     * the next script boundary instead of ending the process. */
    for (i = 0; (i < pDb->nCount) && (!pEngine->bStop) && (!cd_alloc_oom()) &&
         !die_file_read_failed(&pEngine->file); i++) {
        if (should_execute(&pDb->pRecords[i], pEngine->fileType, pEngine->bIsCliAssembly, pOptions)) {
            run_script(pEngine, &pDb->pRecords[i], 1);
        }
    }

    if (bAddUnknown && (pResult->nCount == 0) && !die_file_read_failed(&pEngine->file)) {
        ScanRecord *pRecord = NULL;

        pResult->pRecords = (ScanRecord *)cd_calloc(1, sizeof(ScanRecord));
        pResult->nCapacity = 1;
        pRecord = &pResult->pRecords[0];
        pRecord->pType = cd_strdup("Unknown");
        pRecord->pName = cd_strdup("Unknown");
        pRecord->pVersion = cd_strdup("");
        pRecord->pInfo = cd_strdup("");
        pRecord->nPrio = die_engine_type_to_prio("Unknown");
        pRecord->bIsUnknown = 1;
        pResult->nCount = 1;
    }

    add_msdos_zip_overlay(pEngine);

    if (pEngine->pLastError) {
        static const char sPrefix[] = "Last error: ";
        size_t nLength = x_strlen(pEngine->pLastError);
        char *pError = (char *)cd_malloc(sizeof(sPrefix) + nLength);

        x_memcpy(pError, sPrefix, sizeof(sPrefix) - 1);
        x_memcpy(pError + sizeof(sPrefix) - 1, pEngine->pLastError, nLength + 1);
        result_add_error(pResult, pError);
        cd_free(pError);
        cd_free(pEngine->pLastError);
    }

    js_free(pEngine->pJs);
    die_engine_literal_cache_free(pEngine);

    for (i = 0; i < pEngine->nBlackListCount; i++) {
        cd_free(pEngine->pBlackList[i].pType);
        cd_free(pEngine->pBlackList[i].pName);
    }

    cd_free(pEngine->pBlackList);
    cd_free(pEngine->pCurrentFormatId);
    cd_free(pEngine->pCurrentFormatName);
    die_engine_profile_free(pEngine);

    xx_pe_inspect_free(&pEngine->pe);
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

    xdisasm_close(&pEngine->file);
    cd_free(pEngine);

    return !die_file_read_failed(pOpenedFile);
}

/* Moves every record and error of pFrom into pTo - to the front when bFront
 * is set, otherwise after what is already there - leaving pFrom empty. The
 * record strings change owner, so only the arrays are freed.               */
static void result_merge(ScanResult *pTo, ScanResult *pFrom, int bFront)
{
    int nTotal = 0;

    if (pFrom->nCount > 0) {
        int nAt = bFront ? 0 : pTo->nCount;

        nTotal = pFrom->nCount + pTo->nCount;

        if (nTotal > pTo->nCapacity) {
            pTo->nCapacity = nTotal;
            pTo->pRecords = (ScanRecord *)cd_realloc(pTo->pRecords, (size_t)nTotal * sizeof(ScanRecord));
        }

        if (bFront && (pTo->nCount > 0)) {
            x_memmove(pTo->pRecords + pFrom->nCount, pTo->pRecords, (size_t)pTo->nCount * sizeof(ScanRecord));
        }

        x_memcpy(pTo->pRecords + nAt, pFrom->pRecords, (size_t)pFrom->nCount * sizeof(ScanRecord));
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
static int com_has_non_generic(ScanResult *pResult)
{
    int i = 0;

    for (i = 0; i < pResult->nCount; i++) {
        char *pType = die_engine_translate_type(pResult->pRecords[i].pType);
        int bGeneric = ((cd_stricmp_ascii(pType, "Operation system") == 0) || (cd_stricmp_ascii(pType, "Format") == 0)) ? 1 : 0;

        cd_free(pType);

        if (!bGeneric) {
            return 1;
        }
    }

    return 0;
}

/* The scan proper, over an already-populated DieFile (the struct is taken by
 * value and closed here). Both die_engine_scan_file and die_engine_scan_memory funnel
 * through this so the two entry points share one verified path. */
static int scan_engine_run(DieFile *pOpenedFile, DBase *pDb, ScanOptions *pOptions,
                           XFileType selectedType, ScanResult *pResult)
{
    DieFileTypeSet set;
    XFileType fileType = XFT_BINARY;
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

    if (!die_file_type_set_detect(pOpenedFile, &set)) goto scan_close;
    if (die_file_read_failed(pOpenedFile)) goto scan_close;
    fileType = selectedType == XFT_UNKNOWN ? pick_file_type(pOpenedFile, &set) : selectedType;
    bIsCliAssembly = die_file_type_contains(&set, XFT_CLI_ASSEMBLY);

    if (selectedType != XFT_UNKNOWN) {
        /* Choosing Binary must not silently add a COM pass or .NET scripts.
         * Each explicit choice is an independent interpretation of the file. */
        bIsCliAssembly = bIsCliAssembly && (fileType == XFT_PE32 || fileType == XFT_PE64);
        nResult = scan_run_pass(pOpenedFile, fileType, bIsCliAssembly, pDb, pOptions, pResult, 1);
    } else if (fileType == XFT_COM) {
        /* XScanEngine::scanProcess's FT_COM arm is two passes: a deep scan
         * runs the FT_BINARY scripts first, and the FT_COM pass adds its
         * "Unknown" record only when that produced nothing. The binary
         * records come first in the merged list, and a file that answered as
         * binary is reported as Binary rather than COM.                    */
        ScanResult binResult;
        int bIsBinary = 0;

        x_memset(&binResult, 0, sizeof(binResult));

        if (pOptions->bDeepScan) {
            scan_run_pass(pOpenedFile, XFT_BINARY, bIsCliAssembly, pDb, pOptions, &binResult, 0);
            bIsBinary = (binResult.nCount > 0);
        }

        nResult = scan_run_pass(pOpenedFile, XFT_COM, bIsCliAssembly, pDb, pOptions, pResult, !bIsBinary);

        result_merge(pResult, &binResult, 1);
        pResult->fileType = bIsBinary ? XFT_BINARY : XFT_COM;
    } else if ((fileType == XFT_BINARY) && (pOpenedFile->nSize <= XCOM_MAX_SIZE)) {
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
        ScanResult comResult;
        ScanOptions comOptions = *pOptions;
        int bIsCom = 0;

        x_memset(&comResult, 0, sizeof(comResult));
        comOptions.bVerbose = 0;

        scan_run_pass(pOpenedFile, XFT_COM, bIsCliAssembly, pDb, &comOptions, &comResult, 0);
        bIsCom = com_has_non_generic(&comResult);

        nResult = scan_run_pass(pOpenedFile, XFT_BINARY, bIsCliAssembly, pDb, pOptions, pResult, !bIsCom);

        if (bIsCom) {
            result_merge(pResult, &comResult, 0);
        } else {
            scan_result_free(&comResult);
        }
    } else {
        nResult = scan_run_pass(pOpenedFile, fileType, bIsCliAssembly, pDb, pOptions, pResult, 1);
    }

    if (pOptions->bSort) {
        stable_sort_records(pResult->pRecords, pResult->nCount);
    }

scan_close:
    if (die_file_read_failed(pOpenedFile)) {
        result_add_error(pResult, "File read error");
        nResult = 0;
    }
    die_file_close(pOpenedFile);

#if defined(XXFC_BUILD_SHARED)
    if (bSoftOom) {
        if (cd_alloc_oom()) {
            nResult = 0;
        }

        cd_alloc_end_soft_oom();
    }
#endif

    return nResult;
}

int die_engine_scan_file(const char *pFileName, DBase *pDb, ScanOptions *pOptions, ScanResult *pResult)
{
    return die_engine_scan_file_type(pFileName, pDb, pOptions, XFT_UNKNOWN, pResult);
}

int die_engine_scan_file_type(const char *pFileName, DBase *pDb,
                              ScanOptions *pOptions, XFileType fileType, ScanResult *pResult)
{
    DieFile file;

    if (!pResult) return 0;
    x_memset(pResult, 0, sizeof(*pResult));

    if (!pFileName || !pFileName[0] || !pDb || !pOptions ||
        (cd_u32)fileType >= (cd_u32)XFT_COUNT) return 0;

    if (!die_file_open(&file, pFileName)) {
        return 0;
    }

    pResult->pFileName = cd_strdup(pFileName);
    pResult->nFileSize = file.nSize;

    return scan_engine_run(&file, pDb, pOptions, fileType, pResult);
}

int die_engine_detect_file_types(const char *pFileName,
                                 die_engine_file_type_fn pTypeFn, void *pUserData)
{
    DieFile file;
    DieFileTypeSet set;
    XFileType preferred;
    int i;
    int failed;

    if (!pFileName || !pFileName[0] || !die_file_open(&file, pFileName)) return 0;
    if (!die_file_type_set_detect(&file, &set)) {
        die_file_close(&file);
        return 0;
    }
    preferred = pick_file_type(&file, &set);
    failed = die_file_read_failed(&file);
    die_file_close(&file);
    if (failed) return 0;

    if (pTypeFn) {
        pTypeFn(preferred, pUserData);
        for (i = XFT_COUNT - 1; i > XFT_UNKNOWN; --i) {
            if (i != (int)preferred && die_file_type_contains(&set, (XFileType)i))
                pTypeFn((XFileType)i, pUserData);
        }
    }
    return 1;
}

int die_engine_scan_device(xx_io_device *pDevice, DBase *pDb,
                           ScanOptions *pOptions, ScanResult *pResult)
{
    DieFile file;
    const char *pSourcePath;
    int64_t original;
    int result = 0;

    if (!pResult) return 0;
    x_memset(pResult, 0, sizeof(*pResult));
    if (!pDevice || !pDb || !pOptions) return 0;
    original = xx_io_tell(pDevice);
    if (original < 0 || xx_io_seek64(pDevice, 0, SEEK_SET) != 0) return 0;
    pSourcePath = xx_io_source_path(pDevice);
    if (die_file_open_device(&file, pDevice, 0)) {
        file.pFileName = cd_strdup(pSourcePath ? pSourcePath : "");
        pResult->pFileName = cd_strdup(pSourcePath ? pSourcePath : "");
        pResult->nFileSize = file.nSize;
        result = scan_engine_run(&file, pDb, pOptions, XFT_UNKNOWN, pResult);
    }
    if (xx_io_seek64(pDevice, original, SEEK_SET) != 0) result = 0;
    return result;
}

int die_engine_scan_memory(const void *pData, cd_i64 nSize, DBase *pDb, ScanOptions *pOptions, ScanResult *pResult)
{
    DieFile file;
    unsigned char *pCopy = NULL;

    x_memset(pResult, 0, sizeof(*pResult));
    x_memset(&file, 0, sizeof(file));

    if (nSize < 0) {
        nSize = 0;
    }

    /* A NULL buffer with a non-zero size would reach the x_memcpy below and
     * fault. cdie could treat that as the caller's problem -- it had exactly
     * one caller, DIE_ScanMemory. As an exported library entry point it is
     * reachable by anyone, so it is refused here instead. A NULL buffer with a
     * size of zero stays legal and scans an empty image, which is what the
     * one-byte allocation below is for. */
    if ((pData == NULL) && (nSize > 0)) {
        return 0;
    }

    /* A private copy so die_file_close can free it like a file-backed buffer. The
     * size is the caller's, so this is one of the allocations that has to be
     * allowed to fail rather than end the process; the NULL check below is
     * what makes cd_try_malloc the right primitive here. */
    pCopy = (unsigned char *)cd_try_malloc((size_t)(nSize > 0 ? nSize : 1));

    if (pCopy == NULL) {
        return 0;
    }

    if (nSize > 0) {
        x_memcpy(pCopy, pData, (size_t)nSize);
    }

    /* die_file_adopt attaches the device the accessors read through, and
     * takes the buffer with it. Filling the struct in by hand would leave
     * pDevice NULL, and every field read would quietly answer zero. */
    if (!die_file_adopt(&file, pCopy, nSize, "")) {
        return 0;
    }

    pResult->pFileName = cd_strdup("");
    pResult->nFileSize = nSize;

    return scan_engine_run(&file, pDb, pOptions, XFT_UNKNOWN, pResult);
}

