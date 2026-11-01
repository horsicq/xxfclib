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

/* xx_scan_result.c - result ownership, merging and output formatting. */

#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/scan/xx_scan_console.h"
#include "xxfclib/terminal/xx_terminal.h"
#include "xxfclib/formats/xx_format.h"
#undef fileType /* The scan model retains its public XFileType member name. */
#include "../die_engine/xx_die_engine_compat.h"
#include <limits.h>

void xx_scan_result_free(xx_scan_result *pResult)
{
    int i = 0;

    if (!pResult) return;

    for (i = 0; i < pResult->nCount; i++) {
        xx_rt_free((void *)pResult->pRecords[i].type);
        xx_rt_free((void *)pResult->pRecords[i].name);
        xx_rt_free((void *)pResult->pRecords[i].version);
        xx_rt_free((void *)pResult->pRecords[i].info);
        xx_rt_free((void *)pResult->pRecords[i].format_id);
        xx_rt_free((void *)pResult->pRecords[i].archive_member);
        xx_rt_free((void *)pResult->pRecords[i].signature);
    }

    xx_rt_free(pResult->pRecords);

    for (i = 0; i < pResult->nErrorCount; i++) {
        xx_rt_free(pResult->ppErrors[i]);
    }

    xx_rt_free(pResult->ppErrors);
    xx_rt_free(pResult->pFileName);
    xx_rt_memset(pResult, 0, sizeof(*pResult));
}

static bool xx_scan_copy_string(const char *source, const char **destination)
{
    size_t size;
    char *copy;
    *destination = NULL;
    if (!source) return true;
    size = xx_rt_strlen(source) + 1;
    copy = (char *)xx_rt_malloc(size);
    if (!copy) return false;
    xx_rt_memcpy(copy, source, size);
    *destination = copy;
    return true;
}

bool xx_scan_result_add_error(xx_scan_result *result, const char *text, xx_pd_struct *pd)
{
    const char *copy;
    char **errors;
    size_t count;

    if (!result || !text || result->nErrorCount < 0 || result->nErrorCount == INT_MAX ||
        (result->nErrorCount && !result->ppErrors)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan result error arguments");
        return false;
    }
    count = (size_t)result->nErrorCount + 1;
    if (count > SIZE_MAX / sizeof(char *)) goto out_of_memory;
    if (!xx_scan_copy_string(text, &copy)) goto out_of_memory;
    errors = (char **)xx_rt_realloc(result->ppErrors, count * sizeof(char *));
    if (!errors) {
        xx_rt_free((void *)copy);
        goto out_of_memory;
    }
    result->ppErrors = errors;
    result->ppErrors[result->nErrorCount++] = (char *)copy;
    return true;

out_of_memory:
    xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot add scan result error");
    return false;
}

bool xx_scan_append_result(xx_scan_engine *engine, xx_scan_result *destination, const xx_scan_result *source,
                           int64_t source_offset, xx_pd_struct *pd)
{
    xx_scan_result copy = {0};
    xx_scan_record *records;
    char **errors;
    int count;
    int error_count;
    int i;
    (void)engine;
    if (!destination || !source || destination == source || source_offset < 0 ||
        source->nCount < 0 || source->nErrorCount < 0 || destination->nCount < 0 || destination->nErrorCount < 0 ||
        (source->nCount && !source->pRecords) || (source->nErrorCount && !source->ppErrors) ||
        (destination->nCount && !destination->pRecords) || (destination->nErrorCount && !destination->ppErrors) ||
        source->nCount > INT_MAX - destination->nCount || source->nErrorCount > INT_MAX - destination->nErrorCount) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid scan result append arguments");
        return false;
    }
    if (xx_pd_is_stopped(pd)) return false;
    count = destination->nCount + source->nCount;
    error_count = destination->nErrorCount + source->nErrorCount;
    if ((size_t)count > SIZE_MAX / sizeof(xx_scan_record) || (size_t)error_count > SIZE_MAX / sizeof(char *)) goto out_of_memory;
    if (source->nCount) {
        copy.pRecords = (xx_scan_record *)xx_rt_calloc((size_t)source->nCount, sizeof(xx_scan_record));
        if (!copy.pRecords) goto out_of_memory;
    }
    for (i = 0; i < source->nCount; ++i) {
        const xx_scan_record *input = &source->pRecords[i];
        xx_scan_record *output = &copy.pRecords[i];
        if (xx_pd_is_stopped(pd)) goto failed;
        if (input->offset < 0 || input->offset > INT64_MAX - source_offset) {
            xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS, "Scan record offset overflows");
            goto failed;
        }
        *output = *input;
        output->offset += source_offset;
        output->type = output->name = output->version = output->info = output->format_id = output->archive_member = output->signature = NULL;
        copy.nCount++;
        if (!xx_scan_copy_string(input->type, &output->type) || !xx_scan_copy_string(input->name, &output->name) ||
            !xx_scan_copy_string(input->version, &output->version) || !xx_scan_copy_string(input->info, &output->info) ||
            !xx_scan_copy_string(input->format_id, &output->format_id) ||
            !xx_scan_copy_string(input->archive_member, &output->archive_member) ||
            !xx_scan_copy_string(input->signature, &output->signature)) goto out_of_memory;
    }
    if (source->nErrorCount) {
        copy.ppErrors = (char **)xx_rt_calloc((size_t)source->nErrorCount, sizeof(char *));
        if (!copy.ppErrors) goto out_of_memory;
    }
    for (i = 0; i < source->nErrorCount; ++i) {
        const char *error;
        if (!xx_scan_copy_string(source->ppErrors[i], &error)) goto out_of_memory;
        copy.ppErrors[copy.nErrorCount++] = (char *)error;
    }
    if (xx_pd_is_stopped(pd)) goto failed;
    if (source->nCount) {
        records = (xx_scan_record *)xx_rt_realloc(destination->pRecords, (size_t)count * sizeof(xx_scan_record));
        if (!records) goto out_of_memory;
        destination->pRecords = records;
        destination->nCapacity = count;
    }
    if (source->nErrorCount) {
        errors = (char **)xx_rt_realloc(destination->ppErrors, (size_t)error_count * sizeof(char *));
        if (!errors) goto out_of_memory;
        destination->ppErrors = errors;
    }
    if (source->nCount) xx_rt_memcpy(destination->pRecords + destination->nCount, copy.pRecords, (size_t)source->nCount * sizeof(xx_scan_record));
    if (source->nErrorCount) xx_rt_memcpy(destination->ppErrors + destination->nErrorCount, copy.ppErrors, (size_t)source->nErrorCount * sizeof(char *));
    destination->nCount = count;
    destination->nErrorCount = error_count;
    if (!source_offset) {
        destination->fileType = source->fileType;
        destination->nFileSize = source->nFileSize;
    }
    xx_rt_free(copy.pRecords);
    xx_rt_free(copy.ppErrors);
    return true;
out_of_memory:
    xx_pd_set_error(pd, XXFC_ERR_OUT_OF_MEMORY, "Cannot copy scan results");
failed:
    xx_scan_result_free(&copy);
    return false;
}

static char *lowercase_without_marks(const char *pType)
{
    CDBuf buf;
    size_t i = 0;

    cdbuf_init(&buf);
    if (!pType) pType = "";

    for (i = 0; pType[i]; i++) {
        char nChar = pType[i];

        if ((nChar == '~') || (nChar == '!')) {
            continue;
        }

        if ((nChar >= 'A') && (nChar <= 'Z')) {
            nChar = (char)(nChar - 'A' + 'a');
        }

        cdbuf_append_ch(&buf, nChar);
    }

    return cdbuf_detach(&buf, NULL);
}

int xx_scan_type_to_prio(const char *pType)
{
    char *pClean = lowercase_without_marks(pType);
    int nResult = 1000;

    if ((x_strcmp(pClean, "operation system") == 0) || (x_strcmp(pClean, "virtual machine") == 0)) nResult = 10;
    else if (x_strcmp(pClean, "format") == 0) nResult = 12;
    else if ((x_strcmp(pClean, "platform") == 0) || (x_strcmp(pClean, "dos extender") == 0)) nResult = 14;
    else if (x_strcmp(pClean, "linker") == 0) nResult = 20;
    else if (x_strcmp(pClean, "compiler") == 0) nResult = 30;
    else if (x_strcmp(pClean, "language") == 0) nResult = 40;
    else if (x_strcmp(pClean, "library") == 0) nResult = 50;
    else if ((x_strcmp(pClean, "tool") == 0) || (x_strcmp(pClean, "pe tool") == 0) || (x_strcmp(pClean, "sign tool") == 0) || (x_strcmp(pClean, "apk tool") == 0))
        nResult = 60;
    else if ((x_strcmp(pClean, "protector") == 0) || (x_strcmp(pClean, "cryptor") == 0) || (x_strcmp(pClean, "crypter") == 0)) nResult = 70;
    else if ((x_strcmp(pClean, ".net obfuscator") == 0) || (x_strcmp(pClean, "apk obfuscator") == 0) || (x_strcmp(pClean, "jar obfuscator") == 0)) nResult = 80;
    else if ((x_strcmp(pClean, "dongle protection") == 0) || (x_strcmp(pClean, "protection") == 0)) nResult = 90;
    else if ((x_strcmp(pClean, "packer") == 0) || (x_strcmp(pClean, ".net compressor") == 0)) nResult = 100;
    else if (x_strcmp(pClean, "joiner") == 0) nResult = 110;
    else if ((x_strcmp(pClean, "sfx") == 0) || (x_strcmp(pClean, "installer") == 0)) nResult = 120;
    else if ((x_strcmp(pClean, "virus") == 0) || (x_strcmp(pClean, "malware") == 0) || (x_strcmp(pClean, "trojan") == 0) || (x_strcmp(pClean, "corrupted data") == 0) ||
             (x_strcmp(pClean, "personal data") == 0) || (x_strcmp(pClean, "author") == 0))
        nResult = 70;
    else if (x_strcmp(pClean, "debug data") == 0) nResult = 200;

    cd_free(pClean);

    return nResult;
}

static const struct {
    const char *pKey;
    const char *pTranslated;
} g_typeNames[] = {{"Unknown", "Unknown"},
                   {"APK obfuscator", "APK Obfuscator"},
                   {"APK Tool", "APK Tool"},
                   {"Archive", "Archive"},
                   {"Author", "Author"},
                   {"Certificate", "Certificate"},
                   {"Compiler", "Compiler"},
                   {"Compressor", "Compressor"},
                   {"Converter", "Converter"},
                   {"Corrupted data", "Corrupted data"},
                   {"Creator", "Creator"},
                   {"Crypter", "Crypter"},
                   {"Cryptor", "Cryptor"},
                   {"Data", "Data"},
                   {"Database", "Database"},
                   {"Debug data", "Debug data"},
                   {"Document", "Document"},
                   {"Dongle protection", "Dongle Protection"},
                   {"DOS extender", "DOS Extender"},
                   {"Format", "Format"},
                   {"Generic", "Generic"},
                   {"Image", "Image"},
                   {"Installer", "Installer"},
                   {"Installer data", "Installer data"},
                   {"JAR obfuscator", "JAR Obfuscator"},
                   {"Joiner", "Joiner"},
                   {"Language", "Language"},
                   {"Library", "Library"},
                   {"Licensing", "Licensing"},
                   {"Linker", "Linker"},
                   {"Loader", "Loader"},
                   {"Malware", "Malware"},
                   {".NET compressor", ".NET Compressor"},
                   {".NET obfuscator", ".NET Obfuscator"},
                   {"Obfuscator", "Obfuscator"},
                   {"Operation system", "Operation system"},
                   {"Overlay", "Overlay"},
                   {"Package", "Package"},
                   {"Packer", "Packer"},
                   {"Personal data", "Personal data"},
                   {"PE Tool", "PE Tool"},
                   {"Platform", "Platform"},
                   {"Player", "Player"},
                   {"Producer", "Producer"},
                   {"Protection", "Protection"},
                   {"Protector", "Protector"},
                   {"Protector data", "Protector data"},
                   {"ROM", "ROM"},
                   {"SFX", "SFX"},
                   {"SFX data", "SFX data"},
                   {"Sign tool", "Sign tool"},
                   {"Source code", "Source code"},
                   {"Stub", "Stub"},
                   {"Tool", "Tool"},
                   {"Trojan", "Trojan"},
                   {"Virtual machine", "Virtual machine"},
                   {"Virus", "Virus"},
                   {NULL, NULL}};

char *xx_scan_translate_type(const char *pType)
{
    const char *pInput = pType ? pType : "";
    char *pResult = NULL;
    int i = 0;

    /* Strip the heuristic markers exactly like XScanEngine::translateType. */
    if ((x_strlen(pInput) > 1) && (pInput[0] == '~')) {
        pInput++;
    }

    if ((x_strlen(pInput) > 1) && (pInput[0] == '!')) {
        pInput++;
    }

    for (i = 0; g_typeNames[i].pKey; i++) {
        if (cd_stricmp_ascii(g_typeNames[i].pKey, pInput) == 0) {
            pResult = cd_strdup(g_typeNames[i].pTranslated);
            break;
        }
    }

    if (pResult == NULL) {
        pResult = cd_strdup(pInput);
    }

    if (pResult[0] && (pResult[0] >= 'a') && (pResult[0] <= 'z')) {
        pResult[0] = (char)(pResult[0] - 'a' + 'A');
    }

    return pResult;
}

static void append_record(CDBuf *pBuf, xx_scan_record *pRecord, xx_scan_options *pOptions)
{
    if (pRecord->is_heuristic) {
        cdbuf_append_str(pBuf, "(Heur)");

        if (pOptions->format_result) {
            cdbuf_append_ch(pBuf, ' ');
        }
    } else if (pRecord->is_aggressive_heuristic) {
        cdbuf_append_str(pBuf, "(A-Heur)");

        if (pOptions->format_result) {
            cdbuf_append_ch(pBuf, ' ');
        }
    }

    if (pOptions->show_type) {
        char *pType = xx_scan_translate_type(pRecord->type);

        cdbuf_append_str(pBuf, pType);
        cdbuf_append_str(pBuf, ": ");
        cd_free(pType);
    }

    cdbuf_append_str(pBuf, pRecord->name ? pRecord->name : "");

    if (pOptions->show_version && pRecord->version && pRecord->version[0]) {
        if (pOptions->format_result) {
            cdbuf_append_ch(pBuf, ' ');
        }

        cdbuf_append_ch(pBuf, '(');
        cdbuf_append_str(pBuf, pRecord->version);
        cdbuf_append_ch(pBuf, ')');
    }

    if (pOptions->show_info && pRecord->info && pRecord->info[0]) {
        if (pOptions->format_result) {
            cdbuf_append_ch(pBuf, ' ');
        }

        cdbuf_append_ch(pBuf, '[');
        cdbuf_append_str(pBuf, pRecord->info);
        cdbuf_append_ch(pBuf, ']');
    }
}

/* Each pass owns its file-type header, even when several interpretations of
 * the same bytes are merged. Unset metadata remains compatible with callers
 * constructing the older, single-part model by hand. */
static const char *record_file_type(const xx_scan_result *result, const xx_scan_record *record)
{
    if (!record || record->file_type == XX_FILE_TYPE_UNKNOWN) return xft_to_string(result->fileType);
    switch (record->file_type) {
        case XX_FILE_TYPE_BINARY: return "Binary";
        case XX_FILE_TYPE_MACHO32: return "Mach-O32";
        case XX_FILE_TYPE_MACHO64: return "Mach-O64";
        case XX_FILE_TYPE_MACHOFAT: return "Mach-O FAT";
        case XX_FILE_TYPE_ISO9660: return "ISO 9660";
        case XX_FILE_TYPE_AMIGAHUNK: return "Amiga Hunk";
        case XX_FILE_TYPE_ATARIST: return "Atari ST";
        case XX_FILE_TYPE_JAVA_CLASS: return "Java Class";
        case XX_FILE_TYPE_PYC: return "Python Bytecode";
        case XX_FILE_TYPE_DOS16M: return "DOS/16M";
        case XX_FILE_TYPE_DOS4G: return "DOS/4G";
        case XX_FILE_TYPE_DOTNET: return ".NET";
        case XX_FILE_TYPE_JPEG: return "JPEG";
        case XX_FILE_TYPE_PNG: return "PNG";
        default: return xx_format_file_type_to_string(record->file_type);
    }
}

static int same_record_group(const xx_scan_record *left, const xx_scan_record *right)
{
    return left->file_type == right->file_type && left->offset == right->offset && left->size == right->size &&
           left->scan_id == right->scan_id && left->parent_id == right->parent_id && left->scan_part == right->scan_part &&
           (left->scan_part != XX_SCAN_PART_ARCHIVE ||
            (left->archive_offset == right->archive_offset && left->archive_size == right->archive_size &&
             left->archive_compressed == right->archive_compressed &&
             xx_rt_strcmp(left->archive_member ? left->archive_member : "", right->archive_member ? right->archive_member : "") == 0));
}

static void scan_value_to_hex_ex(uint64_t val, char *buf, size_t buf_size)
{
    int width = 2;
    if (val >= 0xFFFFFFFFull) width = 16;
    else if (val >= 0xFFFFull) width = 8;
    else if (val >= 0xFFull) width = 4;
    xx_rt_snprintf(buf, buf_size, "%0*llx", width, (unsigned long long)val);
}

static void append_group_header(CDBuf *buf, const xx_scan_result *result, const xx_scan_record *record)
{
    if (record && record->scan_part == XX_SCAN_PART_ARCHIVE) {
        char sOffset[32];
        char sSize[32];
        const char *method = record->archive_method ? record->archive_method : record->archive_compressed ? "Deflate" : "Store";
        scan_value_to_hex_ex((uint64_t)record->archive_offset, sOffset, sizeof(sOffset));
        scan_value_to_hex_ex((uint64_t)record->archive_size, sSize, sizeof(sSize));
        if (record->archive_member && record->archive_member[0]) {
            cdbuf_appendf(buf, "Stream [%s]: %s (\"%s\") [Offset = 0x%s, Size = 0x%s]",
                          method, record_file_type(result, record),
                          record->archive_member, sOffset, sSize);
        } else {
            cdbuf_appendf(buf, "Stream [%s]: %s [Offset = 0x%s, Size = 0x%s]",
                          method, record_file_type(result, record),
                          sOffset, sSize);
        }
    } else if (record && (record->scan_part == XX_SCAN_PART_OVERLAY || record->scan_part == XX_SCAN_PART_RESOURCE)) {
        char sOffset[32];
        char sSize[32];
        scan_value_to_hex_ex((uint64_t)record->offset, sOffset, sizeof(sOffset));
        scan_value_to_hex_ex((uint64_t)record->size, sSize, sizeof(sSize));
        cdbuf_appendf(buf, "%s: %s [Offset = 0x%s, Size = 0x%s]",
                      record->scan_part == XX_SCAN_PART_OVERLAY ? "Overlay" : "Resource",
                      record_file_type(result, record), sOffset, sSize);
    } else {
        cdbuf_append_str(buf, record_file_type(result, record));
    }
}

static int first_record_in_group(const xx_scan_result *result, int index)
{
    int i;
    for (i = 0; i < index; ++i) {
        if (same_record_group(&result->pRecords[i], &result->pRecords[index])) return 0;
    }
    return 1;
}

/* Only ancestors present in this result add a level. A parent_id naming no
 * record here (a caller-supplied ID on a direct engine scan) stays at the top
 * level, as in the GUI result tree. */
static int record_depth(const xx_scan_result *result, const xx_scan_record *record)
{
    int depth = 0;
    uint64_t current_parent = record ? record->parent_id : 0;
    while (current_parent != 0) {
        int found = 0;
        int k;
        for (k = 0; k < result->nCount; ++k) {
            if (result->pRecords[k].scan_id == current_parent) {
                current_parent = result->pRecords[k].parent_id;
                found = 1;
                break;
            }
        }
        if (!found || ++depth > 20) break;
    }
    return depth;
}

static void append_indent(CDBuf *buf, int count)
{
    int k;
    for (k = 0; k < count; ++k) {
        cdbuf_append_str(buf, "    ");
    }
}

char *xx_scan_format_text(xx_scan_result *pResult, xx_scan_options *pOptions)
{
    CDBuf buf;
    int i = 0;
    int j;

    cdbuf_init(&buf);
    if (!pResult->nCount) {
        cdbuf_append_str(&buf, xft_to_string(pResult->fileType));
        cdbuf_append_ch(&buf, '\n');
    }

    for (i = 0; i < pResult->nCount; i++) {
        int depth;
        if (!first_record_in_group(pResult, i)) continue;
        depth = record_depth(pResult, &pResult->pRecords[i]);
        append_indent(&buf, depth);
        append_group_header(&buf, pResult, &pResult->pRecords[i]);
        cdbuf_append_ch(&buf, '\n');
        for (j = i; j < pResult->nCount; ++j) {
            if (!same_record_group(&pResult->pRecords[i], &pResult->pRecords[j])) continue;
            if (pResult->pRecords[j].is_unknown && pOptions->hide_unknown) continue;
            append_indent(&buf, depth + 1);
            append_record(&buf, &pResult->pRecords[j], pOptions);
            cdbuf_append_ch(&buf, '\n');
        }
    }

    return cdbuf_detach(&buf, NULL);
}

xxfc_status_t xx_scan_print_text(xx_scan_result *result, xx_scan_options *options,
    const xx_terminal_state *terminal, const xx_scan_color_options *colors)
{
    xx_scan_color_options defaults;
    xxfc_status_t status;
    int i, j;
    if (!result || !options || !terminal) return XXFC_ERR_NULL_PARAM;
    if (result->nCount < 0 || (result->nCount && !result->pRecords)) return XXFC_ERR_INVALID_ARG;
    if (!colors) { xx_scan_color_options_init(&defaults, 0); colors = &defaults; }
    status = XXFC_OK;
    if (!result->nCount) {
        status = xx_terminal_print(terminal, xft_to_string(result->fileType));
        if (status == XXFC_OK) status = xx_terminal_print(terminal, "\n");
    }
    for (i = 0; status == XXFC_OK && i < result->nCount; ++i) {
        int depth;
        if (!first_record_in_group(result, i)) continue;
        depth = record_depth(result, &result->pRecords[i]);
        {
            CDBuf header;
            char *text;
            cdbuf_init(&header);
            append_indent(&header, depth);
            append_group_header(&header, result, &result->pRecords[i]);
            text = cdbuf_detach(&header, NULL);
            if (!text) return XXFC_ERR_OUT_OF_MEMORY;
            status = xx_terminal_print(terminal, text);
            cd_free(text);
        }
        if (status == XXFC_OK) status = xx_terminal_print(terminal, "\n");
        for (j = i; status == XXFC_OK && j < result->nCount; ++j) {
            xx_scan_record *record = &result->pRecords[j];
            xx_scan_color_record color;
            CDBuf buf;
            char *text;
            size_t size;
            int k;
            if (!same_record_group(&result->pRecords[i], record)) continue;
            if (record->is_unknown && options->hide_unknown) continue;
            cdbuf_init(&buf);
            append_record(&buf, record, options);
            text = cdbuf_detach(&buf, &size);
            if (!text) return XXFC_ERR_OUT_OF_MEMORY;
            color = xx_scan_type_to_color_record(record->type, colors);
            for (k = 0; k < depth + 1 && status == XXFC_OK; ++k) {
                status = xx_terminal_print(terminal, "    ");
            }
            if (status == XXFC_OK)
                status = xx_terminal_write_color(terminal, text, size, color.foreground, color.background);
            cd_free(text);
            if (status == XXFC_OK) status = xx_terminal_print(terminal, "\n");
        }
    }
    return status;
}

/* How many records the structured formats will actually emit. */
static int visible_count(xx_scan_result *pResult, xx_scan_options *pOptions)
{
    int nResult = 0;
    int i = 0;

    for (i = 0; i < pResult->nCount; i++) {
        if (pResult->pRecords[i].is_unknown && pOptions->hide_unknown) {
            continue;
        }

        nResult++;
    }

    return nResult;
}

static xxfc_status_t structured_buffer_sink(void *pContext,
                                            const char *pData,
                                            size_t nSize)
{
    cdbuf_append((CDBuf *)pContext, pData, nSize);
    return XXFC_OK;
}

static void json_string(CDBuf *pBuf, const char *pString)
{
    xx_terminal_writer writer;

    xx_terminal_writer_init(&writer, structured_buffer_sink, pBuf);
    (void)xx_terminal_writer_json_string(&writer, pString);
}

static void xml_attribute(CDBuf *pBuf, const char *pString)
{
    xx_terminal_writer writer;

    xx_terminal_writer_init(&writer, structured_buffer_sink, pBuf);
    (void)xx_terminal_writer_xml_attribute(&writer, pString);
}

static void xml_text(CDBuf *pBuf, const char *pString)
{
    xx_terminal_writer writer;

    xx_terminal_writer_init(&writer, structured_buffer_sink, pBuf);
    (void)xx_terminal_writer_xml_text(&writer, pString);
}

/* Lowercase display label used by the XML and delimited formatters. */
static char *format_type_lower(const char *pType)
{
    char *pResult = xx_scan_translate_type(pType);
    size_t i = 0;

    for (i = 0; pResult[i]; i++) {
        if ((pResult[i] >= 'A') && (pResult[i] <= 'Z')) {
            pResult[i] = (char)(pResult[i] - 'A' + 'a');
        }
    }

    return pResult;
}

/* Reproduces DiE's QJsonDocument output, including one header for every
 * independent pass. Groups keep their first encounter order, and detections
 * keep their order within the group even when callers interleave records. */
char *xx_scan_format_json(xx_scan_result *pResult, xx_scan_options *pOptions)
{
    CDBuf buf;
    int i, j;
    int nGroups = 0;

    cdbuf_init(&buf);
    cdbuf_append_str(&buf, "{\n    \"detects\": [\n");

    for (i = 0; i < pResult->nCount || (i == 0 && !pResult->nCount); ++i) {
        xx_scan_record *header = pResult->nCount ? &pResult->pRecords[i] : NULL;
        int nVisible = 0;
        int nWritten = 0;
        if (header && !first_record_in_group(pResult, i)) continue;
        for (j = i; j < pResult->nCount; ++j) {
            if (same_record_group(header, &pResult->pRecords[j]) &&
                !(pResult->pRecords[j].is_unknown && pOptions->hide_unknown)) ++nVisible;
        }
        if (nGroups++) cdbuf_append_str(&buf, ",\n");

        /* ScanItemModel leaves a hidden-only group's header as a bare leaf. */
        if (!nVisible && (!header || header->scan_part != XX_SCAN_PART_ARCHIVE)) {
            cdbuf_append_str(&buf, "        {\n            \"info\": \"\",\n            \"name\": \"\",\n            \"string\": ");
            json_string(&buf, record_file_type(pResult, header));
            cdbuf_append_str(&buf, ",\n            \"type\": \"\",\n            \"version\": \"\"\n        }");
            continue;
        }

        cdbuf_append_str(&buf, "        {\n            \"filetype\": ");
        json_string(&buf, record_file_type(pResult, header));
        cdbuf_append_str(&buf, ",\n            \"info\": \"\",\n");
        cdbuf_appendf(&buf, "            \"offset\": \"%lld\",\n            \"parentfilepart\": \"%s\",\n",
                      (long long)(header->file_type == XX_FILE_TYPE_UNKNOWN ? 0 : header->offset),
                      header->scan_part == XX_SCAN_PART_ARCHIVE ? "Archive" :
                      header->scan_part == XX_SCAN_PART_RESOURCE ? "Resource" :
                      header->scan_part == XX_SCAN_PART_OVERLAY ? "Overlay" : "Header");
        if (header->scan_part == XX_SCAN_PART_ARCHIVE) {
            cdbuf_append_str(&buf, "            \"archive_member\": ");
            json_string(&buf, header->archive_member);
            cdbuf_appendf(&buf, ",\n            \"archive_offset\": \"%lld\",\n            \"archive_size\": \"%lld\",\n"
                                "            \"archive_compressed\": %s,\n            \"offsetbasis\": \"%s\",\n",
                          (long long)header->archive_offset, (long long)header->archive_size,
                          header->archive_compressed ? "true" : "false", header->archive_compressed ? "member" : "input");
        }
        cdbuf_appendf(&buf, "            \"size\": \"%lld\",\n            \"values\": [\n",
                      (long long)(header->file_type == XX_FILE_TYPE_UNKNOWN ? pResult->nFileSize : header->size));
        for (j = i; j < pResult->nCount; ++j) {
            xx_scan_record *pRecord = &pResult->pRecords[j];
            CDBuf line;
            if (!same_record_group(header, pRecord)) continue;
            if (pRecord->is_unknown && pOptions->hide_unknown) continue;
            if (nWritten++) cdbuf_append_str(&buf, ",\n");
            cdbuf_init(&line);
            append_record(&line, pRecord, pOptions);
            cdbuf_append_str(&buf, "                {\n                    \"info\": ");
            json_string(&buf, pRecord->info);
            cdbuf_append_str(&buf, ",\n                    \"name\": ");
            json_string(&buf, pRecord->name);
            cdbuf_append_str(&buf, ",\n                    \"string\": ");
            json_string(&buf, line.pData ? line.pData : "");
            cdbuf_append_str(&buf, ",\n                    \"type\": ");
            /* ScanItemModel writes the raw signature type, including case
             * and heuristic markers; only the rendered string translates. */
            json_string(&buf, pRecord->type);
            cdbuf_append_str(&buf, ",\n                    \"version\": ");
            json_string(&buf, pRecord->version);
            cdbuf_append_str(&buf, "\n                }");
            cdbuf_free(&line);
        }
        cdbuf_append_str(&buf, "\n            ]\n        }");
    }
    cdbuf_append_str(&buf, "\n    ]\n}\n");
    return cdbuf_detach(&buf, NULL);
}

/* A file type is data, not an XML element name: values such as "Mach-O FAT"
 * and "DOS/16M" contain characters that XML names cannot contain. */
char *xx_scan_format_xml(xx_scan_result *pResult, xx_scan_options *pOptions)
{
    CDBuf buf;
    int i = 0;
    const char *pFileType = xft_to_string(pResult->fileType);

    cdbuf_init(&buf);

    cdbuf_append_str(&buf, "\n<Result>\n    <file-part type=\"");
    xml_attribute(&buf, pFileType);
    cdbuf_append_str(&buf, "\" parentfilepart=\"Header\" filetype=\"");
    xml_attribute(&buf, pFileType);
    cdbuf_appendf(&buf, "\" info=\"\" offset=\"0\" size=\"%lld\">\n", (long long)pResult->nFileSize);

    if (visible_count(pResult, pOptions) == 0) {
        cdbuf_append_str(&buf, "        <detect type=\"\" name=\"\" version=\"\" info=\"\">");
        xml_text(&buf, pFileType);
        cdbuf_append_str(&buf, "</detect>\n    </file-part>\n</Result>\n");

        return cdbuf_detach(&buf, NULL);
    }

    for (i = 0; i < pResult->nCount; i++) {
        xx_scan_record *pRecord = &pResult->pRecords[i];
        CDBuf line;
        char *pType = NULL;

        if (pRecord->is_unknown && pOptions->hide_unknown) {
            continue;
        }

        pType = format_type_lower(pRecord->type);
        cdbuf_init(&line);
        append_record(&line, pRecord, pOptions);

        cdbuf_append_str(&buf, "        <detect type=\"");
        xml_attribute(&buf, pType);
        cdbuf_append_str(&buf, "\" name=\"");
        xml_attribute(&buf, pRecord->name);
        cdbuf_append_str(&buf, "\" version=\"");
        xml_attribute(&buf, pRecord->version);
        cdbuf_append_str(&buf, "\" info=\"");
        xml_attribute(&buf, pRecord->info);
        cdbuf_append_str(&buf, "\">");
        xml_text(&buf, line.pData ? line.pData : "");
        cdbuf_append_str(&buf, "</detect>\n");

        cdbuf_free(&line);
        cd_free(pType);
    }

    cdbuf_append_str(&buf, "    </file-part>\n</Result>\n");

    return cdbuf_detach(&buf, NULL);
}

char *xx_scan_format_csv(xx_scan_result *pResult, xx_scan_options *pOptions, char nSeparator)
{
    CDBuf buf;
    int i = 0;

    cdbuf_init(&buf);

    /* Four empty fields then the file type, matching the other formats. */
    if (visible_count(pResult, pOptions) == 0) {
        for (i = 0; i < 4; i++) {
            cdbuf_append_ch(&buf, nSeparator);
        }

        cdbuf_append_str(&buf, xft_to_string(pResult->fileType));
        cdbuf_append_ch(&buf, '\n');

        return cdbuf_detach(&buf, NULL);
    }

    for (i = 0; i < pResult->nCount; i++) {
        xx_scan_record *pRecord = &pResult->pRecords[i];
        char *pType = NULL;

        if (pRecord->is_unknown && pOptions->hide_unknown) {
            continue;
        }

        {
            CDBuf line;

            pType = format_type_lower(pRecord->type);
            cdbuf_init(&line);
            append_record(&line, pRecord, pOptions);

            /* Five fields, the last being the assembled result string. */
            cdbuf_append_str(&buf, pType);
            cdbuf_append_ch(&buf, nSeparator);
            cdbuf_append_str(&buf, pRecord->name ? pRecord->name : "");
            cdbuf_append_ch(&buf, nSeparator);
            cdbuf_append_str(&buf, pRecord->version ? pRecord->version : "");
            cdbuf_append_ch(&buf, nSeparator);
            cdbuf_append_str(&buf, pRecord->info ? pRecord->info : "");
            cdbuf_append_ch(&buf, nSeparator);
            cdbuf_append_str(&buf, line.pData ? line.pData : "");
            cdbuf_append_ch(&buf, '\n');

            cdbuf_free(&line);
            cd_free(pType);
        }
    }

    return cdbuf_detach(&buf, NULL);
}
