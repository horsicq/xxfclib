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

#include "../../formats/zip/xzip.h"
#include "../../die_engine/inflate.h"
#include "../../die_engine/die_engine_compat.h"

#include "xxfclib/json/xx_json.h"
#include "xxfclib/strings/xx_string.h"

/* XArchive::getRecords stops after this many members. */
#define XZIP_MAX_RECORDS 20000

/* XJAR::getFileFormatInfo reads only the head of each *.class candidate:
 * XArchive::decompress(&record, pPdStruct, 0, 0x100). */
#define XZIP_CLASS_PROBE_SIZE 0x100

/* Decompresses the member described by a central-directory entry (only the
 * STORE and DEFLATE methods, which is all the manifest ever uses). nMaxSize
 * caps the produced bytes the way XArchive::decompress's nDecompressedLimit
 * does; 0 means the whole member. */
static int zip_read_member(DieFile *pFile, cd_u16 nMethod, cd_u32 nCompSize, cd_u32 nUncompSize, cd_u32 nLocalOffset, size_t nMaxSize, CDBuf *pOut)
{
    cd_i64 nSize = pFile->nSize;
    const unsigned char *pData = pFile->pData;
    cd_u16 nLocalNameLen = 0;
    cd_u16 nLocalExtraLen = 0;
    cd_i64 nDataOffset = 0;

    if (((cd_i64)nLocalOffset + 30) > nSize) {
        return 0;
    }

    if (!((pData[nLocalOffset] == 0x50) && (pData[nLocalOffset + 1] == 0x4B) && (pData[nLocalOffset + 2] == 0x03) && (pData[nLocalOffset + 3] == 0x04))) {
        return 0;
    }

    nLocalNameLen = xx_io_get_u16(pFile->pDevice, (cd_i64)nLocalOffset + 26, false);
    nLocalExtraLen = xx_io_get_u16(pFile->pDevice, (cd_i64)nLocalOffset + 28, false);
    nDataOffset = (cd_i64)nLocalOffset + 30 + nLocalNameLen + nLocalExtraLen;

    if ((nDataOffset + (cd_i64)nCompSize) > nSize) {
        return 0;
    }

    if (nMethod == 0) {
        size_t nCopy = nCompSize;

        if ((nMaxSize != 0) && (nCopy > nMaxSize)) {
            nCopy = nMaxSize;
        }

        cdbuf_append(pOut, pData + nDataOffset, nCopy);

        return 1;
    }

    if (nMethod == 8) {
        return inflate_raw(pData + nDataOffset, nCompSize, nUncompSize, nMaxSize, pOut);
    }

    return 0;
}

/* XJavaClass::_getJDKVersion: class-file major version -> Java release name, or
 * NULL for an unrecognised major (the reference leaves sResult empty). */
static const char *jvm_base_version(cd_u16 nMajor)
{
    switch (nMajor) {
        case 0x2D: return "JDK 1.1";
        case 0x2E: return "JDK 1.2";
        case 0x2F: return "JDK 1.3";
        case 0x30: return "JDK 1.4";
        case 0x31: return "Java SE 5.0";
        case 0x32: return "Java SE 6";
        case 0x33: return "Java SE 7";
        case 0x34: return "Java SE 8";
        case 0x35: return "Java SE 9";
        case 0x36: return "Java SE 10";
        case 0x37: return "Java SE 11";
        case 0x38: return "Java SE 12";
        case 0x39: return "Java SE 13";
        case 0x3A: return "Java SE 14";
        case 0x3B: return "Java SE 15";
        case 0x3C: return "Java SE 16";
        case 0x3D: return "Java SE 17";
        case 0x3E: return "Java SE 18";
        case 0x3F: return "Java SE 19";
        case 0x40: return "Java SE 20";
        case 0x41: return "Java SE 21";
        case 0x42: return "Java SE 22";
        case 0x43: return "Java SE 23";
        case 0x44: return "Java SE 24";
        case 0x45: return "Java SE 25";
        case 0x46: return "Java SE 26";
        case 0x47: return "Java SE 27";
        case 0x48: return "Java SE 28";
        case 0x49: return "Java SE 29";
        case 0x4A: return "Java SE 30";
        default: break;
    }

    return NULL;
}

/* Formats _getJDKVersion(nMajor, nMinor) into pOut: the release name, with
 * ".<minor>" appended when the name is known and nMinor is non-zero. */
static void jvm_version(cd_u16 nMajor, cd_u16 nMinor, char *pOut, size_t nOutSize)
{
    const char *pBase = jvm_base_version(nMajor);

    if (pBase == NULL) {
        if (nOutSize > 0) {
            pOut[0] = 0;
        }

        return;
    }

    if (nMinor != 0) {
        x_snprintf(pOut, nOutSize, "%s.%u", pBase, (unsigned int)nMinor);
    } else {
        x_snprintf(pOut, nOutSize, "%s", pBase);
    }
}

/* XArchive::RECORD::sRecordName.section(".", -1, -1) == "class": the text after
 * the final '.' (the whole name when it has no '.') equals "class". */
static int name_ext_is_class(const char *pName)
{
    const char *pDot = NULL;
    const char *p = NULL;
    const char *pExt = NULL;

    if (pName == NULL) {
        return 0;
    }

    for (p = pName; *p; p++) {
        if (*p == '.') {
            pDot = p;
        }
    }

    pExt = (pDot != NULL) ? (pDot + 1) : pName;

    return x_strcmp(pExt, "class") == 0;
}

int xzip_parse(DieFile *pFile, XZip *pZip)
{
    cd_i64 nSize = 0;
    const unsigned char *pData = NULL;
    cd_i64 nEocd = -1;
    cd_i64 i = 0;
    cd_i64 nCentralOffset = 0;
    cd_u32 nEntries = 0;
    int bJvmFound = 0;

    x_memset(pZip, 0, sizeof(*pZip));
    cdvec_init(&pZip->vecNames);

    if (pFile == NULL) {
        return 0;
    }

    nSize = pFile->nSize;
    pData = pFile->pData;

    if (nSize < 22) {
        return 0;
    }

    /* Locate the End Of Central Directory record from the tail. */
    for (i = nSize - 22; i >= 0; i--) {
        if ((nSize - i) > (65536 + 22)) {
            break;
        }

        if ((pData[i] == 0x50) && (pData[i + 1] == 0x4B) && (pData[i + 2] == 0x05) && (pData[i + 3] == 0x06)) {
            nEocd = i;

            break;
        }
    }

    if (nEocd < 0) {
        return 0;
    }

    nEntries = xx_io_get_u16(pFile->pDevice, nEocd + 10, false);
    nCentralOffset = xx_io_get_u32(pFile->pDevice, nEocd + 16, false);

    for (i = 0; ((cd_u32)i < nEntries) && (i < XZIP_MAX_RECORDS); i++) {
        cd_u16 nMethod = 0;
        cd_u32 nCompSize = 0;
        cd_u32 nUncompSize = 0;
        cd_u16 nNameLen = 0;
        cd_u16 nExtraLen = 0;
        cd_u16 nCommentLen = 0;
        cd_u32 nLocalOffset = 0;
        cd_i64 nNameOffset = 0;
        char *pName = NULL;

        if ((nCentralOffset + 46) > nSize) {
            break;
        }

        if (!((pData[nCentralOffset] == 0x50) && (pData[nCentralOffset + 1] == 0x4B) && (pData[nCentralOffset + 2] == 0x01) && (pData[nCentralOffset + 3] == 0x02))) {
            break;
        }

        nMethod = xx_io_get_u16(pFile->pDevice, nCentralOffset + 10, false);
        nCompSize = xx_io_get_u32(pFile->pDevice, nCentralOffset + 20, false);
        nUncompSize = xx_io_get_u32(pFile->pDevice, nCentralOffset + 24, false);
        nNameLen = xx_io_get_u16(pFile->pDevice, nCentralOffset + 28, false);
        nExtraLen = xx_io_get_u16(pFile->pDevice, nCentralOffset + 30, false);
        nCommentLen = xx_io_get_u16(pFile->pDevice, nCentralOffset + 32, false);
        nLocalOffset = xx_io_get_u32(pFile->pDevice, nCentralOffset + 42, false);
        nNameOffset = nCentralOffset + 46;

        if ((nNameOffset + nNameLen) > nSize) {
            break;
        }

        pName = cd_strndup((const char *)(pData + nNameOffset), nNameLen);
        cdvec_push(&pZip->vecNames, pName);

        if ((pZip->pManifestText == NULL) && (nNameLen == 20) && (x_memcmp(pData + nNameOffset, "META-INF/MANIFEST.MF", 20) == 0)) {
            CDBuf manifest;

            cdbuf_init(&manifest);

            if (zip_read_member(pFile, nMethod, nCompSize, nUncompSize, nLocalOffset, 0, &manifest)) {
                pZip->pManifestText = cdbuf_detach(&manifest, NULL);
            } else {
                cdbuf_free(&manifest);
            }
        }

        if ((pZip->pPackageJson == NULL) && (nNameLen == 20) && (x_memcmp(pData + nNameOffset, "package/package.json", 20) == 0)) {
            CDBuf json;

            cdbuf_init(&json);

            if (zip_read_member(pFile, nMethod, nCompSize, nUncompSize, nLocalOffset, 0, &json)) {
                pZip->pPackageJson = cdbuf_detach(&json, NULL);
            } else {
                cdbuf_free(&json);
            }
        }

        /* XJAR::getFileFormatInfo: the Java release is read from the first
         * *.class member whose decompressed head carries the 0xCAFEBABE magic
         * (major at +6, minor at +4, both big-endian). */
        if ((!bJvmFound) && name_ext_is_class(pName)) {
            CDBuf klass;

            cdbuf_init(&klass);

            if (zip_read_member(pFile, nMethod, nCompSize, nUncompSize, nLocalOffset, XZIP_CLASS_PROBE_SIZE, &klass) && (klass.nSize > 10)) {
                const unsigned char *pClass = (const unsigned char *)klass.pData;
                cd_u32 nMagic = ((cd_u32)pClass[0] << 24) | ((cd_u32)pClass[1] << 16) | ((cd_u32)pClass[2] << 8) | (cd_u32)pClass[3];

                if (nMagic == 0xCAFEBABE) {
                    cd_u16 nMinor = (cd_u16)(((cd_u16)pClass[4] << 8) | pClass[5]);
                    cd_u16 nMajor = (cd_u16)(((cd_u16)pClass[6] << 8) | pClass[7]);

                    jvm_version(nMajor, nMinor, pZip->sJvmVersion, sizeof(pZip->sJvmVersion));
                    bJvmFound = 1;
                }
            }

            cdbuf_free(&klass);
        }

        nCentralOffset += 46 + nNameLen + nExtraLen + nCommentLen;
    }

    pZip->bValid = 1;

    return 1;
}

void xzip_free(XZip *pZip)
{
    size_t i = 0;

    for (i = 0; i < pZip->vecNames.nSize; i++) {
        cd_free(pZip->vecNames.ppData[i]);
    }

    cdvec_free(&pZip->vecNames);

    if (pZip->pManifestText) {
        cd_free(pZip->pManifestText);
        pZip->pManifestText = NULL;
    }

    if (pZip->pPackageJson) {
        cd_free(pZip->pPackageJson);
        pZip->pPackageJson = NULL;
    }

    pZip->bValid = 0;
}

int xzip_record_present(XZip *pZip, const char *pName)
{
    size_t i = 0;

    if ((pZip == NULL) || (pName == NULL)) {
        return 0;
    }

    for (i = 0; i < pZip->vecNames.nSize; i++) {
        const char *pRecord = (const char *)pZip->vecNames.ppData[i];

        if ((pRecord != NULL) && (x_strcmp(pRecord, pName) == 0)) {
            return 1;
        }
    }

    return 0;
}

/* ------------------------------------------------- package.json (minimal) */

/*
 * The value of a top-level string key in the embedded package.json, or "".
 *
 * This used to carry its own JSON scanner; it now uses xxfclib's shared
 * cursor, which the npm and ASAR readers also use. The observable contract is
 * unchanged and has to stay that way -- the result is printed, so it is part
 * of the byte-for-byte output comparison against the reference:
 *
 *   - a document whose root is not an object yields "";
 *   - a key whose value is not a string yields "", because the reference
 *     reaches this through QJsonValue::toString(), which is empty for
 *     anything but a string;
 *   - the first matching key wins;
 *   - malformed input yields "" rather than failing the scan.
 *
 * The shared parser hands back xx_mem-pool strings while the engine frees
 * with cd_free, so the result is copied across the boundary rather than
 * relying on the two pools happening to coincide.
 *
 * ONE behaviour change, measured rather than assumed. A differential test over
 * 160 document/key pairs found the old scanner and this one agreeing on 157;
 * the three that differ all have the same cause -- a RAW control character
 * (tab, newline, 0x07) sitting unescaped inside a JSON string. The old scanner
 * accepted those and returned the value; this one rejects the document and
 * returns "".
 *
 * Strict is the right side to land on. RFC 8259 section 7 requires U+0000
 * through U+001F to be escaped, and the Qt reference this engine is measured
 * against parses with QJsonDocument, which enforces that -- so the old
 * leniency was a way for cdie to DISAGREE with the reference, not agree with
 * it. No package.json written by npm contains one.
 */
char *xzip_packagejson_record(XZip *pZip, const char *pKey)
{
    xx_json json;
    char *pResult = NULL;

    if ((pZip == NULL) || (pZip->pPackageJson == NULL) || (pKey == NULL)) {
        return cd_strdup("");
    }

    xx_json_init(&json, pZip->pPackageJson, x_strlen(pZip->pPackageJson));

    if (!xx_json_object_begin(&json)) {
        return cd_strdup(""); /* root is not an object */
    }
    if (xx_json_object_empty(&json)) {
        return cd_strdup("");
    }

    for (;;) {
        char *pJsonKey = NULL;
        int bMatch;

        if (!xx_json_object_key(&json, &pJsonKey)) {
            break; /* malformed */
        }
        bMatch = (x_strcmp(pJsonKey ? pJsonKey : "", pKey) == 0);
        xx_str_free(pJsonKey);

        if (bMatch) {
            if (xx_json_peek(&json) == XX_JSON_TYPE_STRING) {
                char *pValue = NULL;

                if (xx_json_string(&json, &pValue) && (pValue != NULL)) {
                    pResult = cd_strdup(pValue);
                }
                xx_str_free(pValue);
            }
            /* A match settles it either way: a non-string value is "". */
            break;
        }
        if (!xx_json_skip(&json)) {
            break; /* malformed */
        }
        if (xx_json_more(&json)) {
            continue;
        }
        break;
    }

    return (pResult != NULL) ? pResult : cd_strdup("");
}

char *xzip_manifest_record(XZip *pZip, const char *pKey)
{
    const char *pText = NULL;
    size_t nKeyLen = 0;
    const char *p = NULL;

    if ((pZip == NULL) || (pZip->pManifestText == NULL) || (pKey == NULL)) {
        return cd_strdup("");
    }

    pText = pZip->pManifestText;
    nKeyLen = x_strlen(pKey);

    /* Regex `pKey + ": (.*?)\n"`, unanchored, group 1: find `pKey: `, capture up
     * to the next '\n' (required — no newline means no match), strip '\r'. */
    for (p = pText; *p; p++) {
        if ((x_strncmp(p, pKey, nKeyLen) == 0) && (p[nKeyLen] == ':') && (p[nKeyLen + 1] == ' ')) {
            const char *pStart = p + nKeyLen + 2;
            const char *pEnd = pStart;
            CDBuf value;

            while (*pEnd && (*pEnd != '\n')) {
                pEnd++;
            }

            if (*pEnd != '\n') {
                return cd_strdup(""); /* the regex requires a terminating '\n' */
            }

            cdbuf_init(&value);

            while (pStart < pEnd) {
                if (*pStart != '\r') {
                    cdbuf_append_ch(&value, *pStart);
                }

                pStart++;
            }

            return cdbuf_detach(&value, NULL);
        }
    }

    return cd_strdup("");
}
