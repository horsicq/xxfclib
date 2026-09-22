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

/* result.c - result priorities, type names and output formatting. */

#include "die_engine_internal.h"

static char *lowercase_without_marks(const char *pType)
{
    CDBuf buf;
    size_t i = 0;

    cdbuf_init(&buf);

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

int die_engine_type_to_prio(const char *pType)
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
    else if ((x_strcmp(pClean, "tool") == 0) || (x_strcmp(pClean, "pe tool") == 0) || (x_strcmp(pClean, "sign tool") == 0) || (x_strcmp(pClean, "apk tool") == 0)) nResult = 60;
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
} g_typeNames[] = {
    {"Unknown", "Unknown"},
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

char *die_engine_translate_type(const char *pType)
{
    const char *pInput = pType;
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

static void append_record(CDBuf *pBuf, ScanRecord *pRecord, ScanOptions *pOptions)
{
    if (pRecord->bIsHeuristic) {
        cdbuf_append_str(pBuf, "(Heur)");

        if (pOptions->bFormatResult) {
            cdbuf_append_ch(pBuf, ' ');
        }
    } else if (pRecord->bIsAHeuristic) {
        cdbuf_append_str(pBuf, "(A-Heur)");

        if (pOptions->bFormatResult) {
            cdbuf_append_ch(pBuf, ' ');
        }
    }

    if (pOptions->bShowType) {
        char *pType = die_engine_translate_type(pRecord->pType);

        cdbuf_append_str(pBuf, pType);
        cdbuf_append_str(pBuf, ": ");
        cd_free(pType);
    }

    cdbuf_append_str(pBuf, pRecord->pName);

    if (pOptions->bShowVersion && pRecord->pVersion[0]) {
        if (pOptions->bFormatResult) {
            cdbuf_append_ch(pBuf, ' ');
        }

        cdbuf_append_ch(pBuf, '(');
        cdbuf_append_str(pBuf, pRecord->pVersion);
        cdbuf_append_ch(pBuf, ')');
    }

    if (pOptions->bShowInfo && pRecord->pInfo[0]) {
        if (pOptions->bFormatResult) {
            cdbuf_append_ch(pBuf, ' ');
        }

        cdbuf_append_ch(pBuf, '[');
        cdbuf_append_str(pBuf, pRecord->pInfo);
        cdbuf_append_ch(pBuf, ']');
    }
}

char *die_engine_format_text(ScanResult *pResult, ScanOptions *pOptions)
{
    CDBuf buf;
    int i = 0;

    cdbuf_init(&buf);
    cdbuf_append_str(&buf, xft_to_string(pResult->fileType));
    cdbuf_append_ch(&buf, '\n');

    for (i = 0; i < pResult->nCount; i++) {
        if (pResult->pRecords[i].bIsUnknown && pOptions->bHideUnknown) {
            continue;
        }

        cdbuf_append_str(&buf, "    ");
        append_record(&buf, &pResult->pRecords[i], pOptions);
        cdbuf_append_ch(&buf, '\n');
    }

    return cdbuf_detach(&buf, NULL);
}

/* How many records the structured formats will actually emit. */
static int visible_count(ScanResult *pResult, ScanOptions *pOptions)
{
    int nResult = 0;
    int i = 0;

    for (i = 0; i < pResult->nCount; i++) {
        if (pResult->pRecords[i].bIsUnknown && pOptions->bHideUnknown) {
            continue;
        }

        nResult++;
    }

    return nResult;
}

static void json_escape(CDBuf *pBuf, const char *pString)
{
    size_t i = 0;

    for (i = 0; pString[i]; i++) {
        unsigned char nChar = (unsigned char)pString[i];

        switch (nChar) {
            case '"': cdbuf_append_str(pBuf, "\\\""); break;
            case '\\': cdbuf_append_str(pBuf, "\\\\"); break;
            case '\n': cdbuf_append_str(pBuf, "\\n"); break;
            case '\r': cdbuf_append_str(pBuf, "\\r"); break;
            case '\t': cdbuf_append_str(pBuf, "\\t"); break;
            default:
                if (nChar < 0x20) {
                    cdbuf_appendf(pBuf, "\\u%04x", nChar);
                } else {
                    cdbuf_append_ch(pBuf, (char)nChar);
                }
                break;
        }
    }
}

/* The record type as the structured formats spell it: lower case, so
 * "Sign tool" becomes "sign tool". The text format keeps the capital.     */
static char *format_type_lower(const char *pType)
{
    char *pResult = die_engine_translate_type(pType);
    size_t i = 0;

    for (i = 0; pResult[i]; i++) {
        if ((pResult[i] >= 'A') && (pResult[i] <= 'Z')) {
            pResult[i] = (char)(pResult[i] - 'A' + 'a');
        }
    }

    return pResult;
}

/* Reproduces DiE's QJsonDocument output: four space indent, and object keys
 * in alphabetical order because that is what QJsonObject does. The detects
 * array holds one entry per scanned file part; this port scans a single
 * part, so there is always exactly one, named "Header".                   */
char *die_engine_format_json(ScanResult *pResult, ScanOptions *pOptions)
{
    CDBuf buf;
    int i = 0;
    int nWritten = 0;

    cdbuf_init(&buf);

    /* With nothing to report DiE drops the file-part wrapper and emits one
     * bare record whose "string" is the file type.                         */
    if (visible_count(pResult, pOptions) == 0) {
        cdbuf_append_str(&buf, "{\n    \"detects\": [\n        {\n            \"info\": \"\",\n            \"name\": \"\",\n            \"string\": \"");
        json_escape(&buf, xft_to_string(pResult->fileType));
        cdbuf_append_str(&buf, "\",\n            \"type\": \"\",\n            \"version\": \"\"\n        }\n    ]\n}\n");

        return cdbuf_detach(&buf, NULL);
    }

    cdbuf_append_str(&buf, "{\n    \"detects\": [\n        {\n            \"filetype\": \"");
    json_escape(&buf, xft_to_string(pResult->fileType));
    cdbuf_append_str(&buf, "\",\n            \"info\": \"\",\n            \"offset\": \"0\",\n            \"parentfilepart\": \"Header\",\n");
    cdbuf_appendf(&buf, "            \"size\": \"%lld\",\n            \"values\": [\n", (long long)pResult->nFileSize);

    for (i = 0; i < pResult->nCount; i++) {
        ScanRecord *pRecord = &pResult->pRecords[i];
        CDBuf line;
        char *pType = NULL;

        if (pRecord->bIsUnknown && pOptions->bHideUnknown) {
            continue;
        }

        if (nWritten > 0) {
            cdbuf_append_str(&buf, ",\n");
        }

        pType = format_type_lower(pRecord->pType);
        cdbuf_init(&line);
        append_record(&line, pRecord, pOptions);

        cdbuf_append_str(&buf, "                {\n                    \"info\": \"");
        json_escape(&buf, pRecord->pInfo);
        cdbuf_append_str(&buf, "\",\n                    \"name\": \"");
        json_escape(&buf, pRecord->pName);
        cdbuf_append_str(&buf, "\",\n                    \"string\": \"");
        json_escape(&buf, line.pData ? line.pData : "");
        cdbuf_append_str(&buf, "\",\n                    \"type\": \"");
        json_escape(&buf, pType);
        cdbuf_append_str(&buf, "\",\n                    \"version\": \"");
        json_escape(&buf, pRecord->pVersion);
        cdbuf_append_str(&buf, "\"\n                }");

        cdbuf_free(&line);
        cd_free(pType);
        nWritten++;
    }

    cdbuf_append_str(&buf, "\n            ]\n        }\n    ]\n}\n");

    return cdbuf_detach(&buf, NULL);
}

static void xml_escape(CDBuf *pBuf, const char *pString)
{
    size_t i = 0;

    for (i = 0; pString[i]; i++) {
        switch (pString[i]) {
            case '<': cdbuf_append_str(pBuf, "&lt;"); break;
            case '>': cdbuf_append_str(pBuf, "&gt;"); break;
            case '&': cdbuf_append_str(pBuf, "&amp;"); break;
            case '"': cdbuf_append_str(pBuf, "&quot;"); break;
            default: cdbuf_append_ch(pBuf, pString[i]); break;
        }
    }
}

/* DiE writes the file part as an element named after the file type, with the
 * result string as the element text rather than an attribute. The leading
 * blank line is what QXmlStreamWriter emits and is reproduced here.       */
char *die_engine_format_xml(ScanResult *pResult, ScanOptions *pOptions)
{
    CDBuf buf;
    int i = 0;
    const char *pFileType = xft_to_string(pResult->fileType);

    cdbuf_init(&buf);

    /* Same as the JSON: no file-part element, one empty detect carrying the
     * file type as its text.                                               */
    if (visible_count(pResult, pOptions) == 0) {
        cdbuf_append_str(&buf, "\n<Result>\n    <detect type=\"\" name=\"\" version=\"\" info=\"\">");
        xml_escape(&buf, pFileType);
        cdbuf_append_str(&buf, "</detect>\n</Result>\n");

        return cdbuf_detach(&buf, NULL);
    }

    cdbuf_append_str(&buf, "\n<Result>\n    <");
    xml_escape(&buf, pFileType);
    cdbuf_append_str(&buf, " parentfilepart=\"Header\" filetype=\"");
    xml_escape(&buf, pFileType);
    cdbuf_appendf(&buf, "\" info=\"\" offset=\"0\" size=\"%lld\">\n", (long long)pResult->nFileSize);

    for (i = 0; i < pResult->nCount; i++) {
        ScanRecord *pRecord = &pResult->pRecords[i];
        CDBuf line;
        char *pType = NULL;

        if (pRecord->bIsUnknown && pOptions->bHideUnknown) {
            continue;
        }

        pType = format_type_lower(pRecord->pType);
        cdbuf_init(&line);
        append_record(&line, pRecord, pOptions);

        cdbuf_append_str(&buf, "        <detect type=\"");
        xml_escape(&buf, pType);
        cdbuf_append_str(&buf, "\" name=\"");
        xml_escape(&buf, pRecord->pName);
        cdbuf_append_str(&buf, "\" version=\"");
        xml_escape(&buf, pRecord->pVersion);
        cdbuf_append_str(&buf, "\" info=\"");
        xml_escape(&buf, pRecord->pInfo);
        cdbuf_append_str(&buf, "\">");
        xml_escape(&buf, line.pData ? line.pData : "");
        cdbuf_append_str(&buf, "</detect>\n");

        cdbuf_free(&line);
        cd_free(pType);
    }

    cdbuf_append_str(&buf, "    </");
    xml_escape(&buf, pFileType);
    cdbuf_append_str(&buf, ">\n</Result>\n");

    return cdbuf_detach(&buf, NULL);
}

char *die_engine_format_csv(ScanResult *pResult, ScanOptions *pOptions, char nSeparator)
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
        ScanRecord *pRecord = &pResult->pRecords[i];
        char *pType = NULL;

        if (pRecord->bIsUnknown && pOptions->bHideUnknown) {
            continue;
        }

        {
            CDBuf line;

            pType = format_type_lower(pRecord->pType);
            cdbuf_init(&line);
            append_record(&line, pRecord, pOptions);

            /* Five fields, the last being the assembled result string. */
            cdbuf_append_str(&buf, pType);
            cdbuf_append_ch(&buf, nSeparator);
            cdbuf_append_str(&buf, pRecord->pName);
            cdbuf_append_ch(&buf, nSeparator);
            cdbuf_append_str(&buf, pRecord->pVersion);
            cdbuf_append_ch(&buf, nSeparator);
            cdbuf_append_str(&buf, pRecord->pInfo);
            cdbuf_append_ch(&buf, nSeparator);
            cdbuf_append_str(&buf, line.pData ? line.pData : "");
            cdbuf_append_ch(&buf, '\n');

            cdbuf_free(&line);
            cd_free(pType);
        }
    }

    return cdbuf_detach(&buf, NULL);
}
