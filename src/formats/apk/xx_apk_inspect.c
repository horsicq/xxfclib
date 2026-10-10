/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/apk/xx_apk.h"
#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/data/xx_data.h"

/* ------------------------------------------------------------------- AXML */

/* Android binary XML chunk types (only the ones the parser acts on; the
 * remaining RES_XML_* codes - 0x0101 END_NAMESPACE,
 * 0x0180 RESOURCE_MAP - are skipped by chunk size). */
#define AXML_RES_STRING_POOL 0x0001
#define AXML_RES_XML 0x0003
#define AXML_RES_XML_START_NAMESPACE 0x0100
#define AXML_RES_XML_END_NAMESPACE 0x0101
#define AXML_RES_XML_START_ELEMENT 0x0102
#define AXML_RES_XML_END_ELEMENT 0x0103

/* ResStringPool flags. The encoding is selected by UTF8_FLAG alone; bit 0 is
 * SORTED_FLAG and says nothing about the encoding. */
#define AXML_STRING_POOL_UTF8_FLAG 0x0100

/* Total attributes decoded per manifest. Only crafted AXML, where many
 * START_ELEMENT chunks overlap and each declares 0xFFFF attributes, ever
 * approaches this; the largest real manifests use a few hundred. */
#define AXML_MAX_ATTRIBUTES 100000

/* Ceiling on the decoded manifest text. The attribute budget alone does not
 * bound the output: every one of those attributes may point at a string-pool
 * entry of up to 0x10000 bytes, so a 2 MB crafted manifest can still ask for
 * gigabytes. A real AndroidManifest.xml decodes to a few hundred KB at most
 * (the largest here is well under 1 MB), so this only ever trips on crafted
 * input. */
#define AXML_MAX_OUTPUT (16 * 1024 * 1024)
#define AXML_MAX_DEPTH 1024
#define AXML_ANDROID_URI "http://schemas.android.com/apk/res/android"

/* A parsed AXML string pool: strings live in a single joined buffer, indexed
 * through the offset array. */
typedef struct {
    const unsigned char *pData;
    size_t nSize;
    uint32_t nStringCount;
    uint32_t nFlags;
    size_t nEnd;         /* End of this pool chunk, not the whole manifest. */
    size_t nOffsetsBase; /* start of the u32 offset array */
    size_t nStringsBase; /* start of the string data       */
} AxmlPool;

static uint8_t rd8(const unsigned char *pData, size_t nSize, size_t nOffset)
{
    if (nOffset + 1 > nSize) {
        return 0;
    }

    return pData[nOffset];
}

/* Appends the pool string at nIndex to pOut as UTF-8. Out-of-range indices
 * (including 0xFFFFFFFF for "no string") append nothing. */
static void axml_append_string(const AxmlPool *pPool, uint32_t nIndex, xx_buf_t *pOut)
{
    size_t nStrOffset = 0;
    size_t nBase = 0;
    uint32_t nStrOffsetRel = 0;
    uint32_t nLen = 0;
    uint32_t k = 0;

    if (nIndex >= pPool->nStringCount) {
        return;
    }

    nStrOffsetRel = xx_data_get_u32(pPool->pData, pPool->nEnd, pPool->nOffsetsBase + (size_t)nIndex * 4, false);
    if (nStrOffsetRel >= pPool->nEnd - pPool->nStringsBase) {
        pOut->failed = true;
        return;
    }
    nStrOffset = pPool->nStringsBase + nStrOffsetRel;

    if (pPool->nFlags & AXML_STRING_POOL_UTF8_FLAG) {
        /* UTF-8 entry: a UTF-16 character count then a UTF-8 byte count, each
         * one or two bytes with the high bit of the first marking the longer
         * form (_readStringPoolString). The bytes that follow are already
         * UTF-8; read_utf8String caps the run at 0x10000 and stops at the
         * first NUL.                                                        */
        uint8_t nLen16 = 0;
        uint8_t nLen8 = 0;

        nBase = nStrOffset;
        if (nBase >= pPool->nEnd) {
            pOut->failed = true;
            return;
        }
        nLen16 = rd8(pPool->pData, pPool->nEnd, nBase);
        nBase += 1;

        if (nLen16 & 0x80) {
            if (nBase >= pPool->nEnd) {
                pOut->failed = true;
                return;
            }
            nBase += 1;
        }

        if (nBase >= pPool->nEnd) {
            pOut->failed = true;
            return;
        }
        nLen8 = rd8(pPool->pData, pPool->nEnd, nBase);
        nBase += 1;
        nLen = nLen8;

        if (nLen8 & 0x80) {
            if (nBase >= pPool->nEnd) {
                pOut->failed = true;
                return;
            }
            nLen = ((uint32_t)(nLen8 & 0x7F) << 8) | rd8(pPool->pData, pPool->nEnd, nBase);
            nBase += 1;
        }

        if (nBase >= pPool->nEnd || nLen > pPool->nEnd - nBase - 1 || pPool->pData[nBase + nLen] != 0) {
            pOut->failed = true;
            return;
        }

        for (k = 0; k < nLen; k++) {
            size_t nAt = nBase + k;
            unsigned char nByte = 0;

            if (nAt >= pPool->nEnd) {
                pOut->failed = true;
                return;
            }

            nByte = pPool->pData[nAt];

            if (nByte == 0) {
                break;
            }

            xx_buf_append_char(pOut, (char)nByte);
        }

        return;
    }

    /* UTF-16LE entry: a code-unit count of one or two units, 0x8000 marking
     * the longer form. read_unicodeString gives nothing at all for a count of
     * 0x10000 or more. */
    nBase = nStrOffset;
    if (pPool->nEnd - nBase < 2) {
        pOut->failed = true;
        return;
    }
    nLen = xx_data_get_u16(pPool->pData, pPool->nEnd, nBase, false);
    nBase += 2;

    if (nLen & 0x8000) {
        if (pPool->nEnd - nBase < 2) {
            pOut->failed = true;
            return;
        }
        nLen = ((nLen & 0x7FFF) << 16) | xx_data_get_u16(pPool->pData, pPool->nEnd, nBase, false);
        nBase += 2;
    }

    if (nLen >= 0x10000) {
        pOut->failed = true;
        return;
    }
    if (pPool->nEnd - nBase < 2 || nLen > (pPool->nEnd - nBase - 2) / 2 || xx_data_get_u16(pPool->pData, pPool->nEnd, nBase + (size_t)nLen * 2, false) != 0) {
        pOut->failed = true;
        return;
    }

    /* Decode to UTF-8, handling the surrogate pair range. */
    for (k = 0; k < nLen; k++) {
        size_t nAt = nBase + (size_t)k * 2;
        uint32_t nUnit = 0;

        if (nAt + 2 > pPool->nEnd) {
            pOut->failed = true;
            return;
        }

        nUnit = (uint32_t)pPool->pData[nAt] | ((uint32_t)pPool->pData[nAt + 1] << 8);

        /* read_unicodeString stops at the first NUL code unit, even when the
         * declared count is longer. */
        if (nUnit == 0) {
            break;
        }

        if ((nUnit >= 0xD800) && (nUnit <= 0xDBFF) && ((k + 1) < nLen)) {
            uint32_t nLow = xx_data_get_u16(pPool->pData, pPool->nEnd, nBase + (size_t)(k + 1) * 2, false);

            if ((nLow >= 0xDC00) && (nLow <= 0xDFFF)) {
                nUnit = 0x10000 + ((nUnit - 0xD800) << 10) + (nLow - 0xDC00);
                k++;
            }
        }

        if (nUnit < 0x80) {
            xx_buf_append_char(pOut, (char)nUnit);
        } else if (nUnit < 0x800) {
            xx_buf_append_char(pOut, (char)(0xC0 | (nUnit >> 6)));
            xx_buf_append_char(pOut, (char)(0x80 | (nUnit & 0x3F)));
        } else if (nUnit < 0x10000) {
            xx_buf_append_char(pOut, (char)(0xE0 | (nUnit >> 12)));
            xx_buf_append_char(pOut, (char)(0x80 | ((nUnit >> 6) & 0x3F)));
            xx_buf_append_char(pOut, (char)(0x80 | (nUnit & 0x3F)));
        } else {
            xx_buf_append_char(pOut, (char)(0xF0 | (nUnit >> 18)));
            xx_buf_append_char(pOut, (char)(0x80 | ((nUnit >> 12) & 0x3F)));
            xx_buf_append_char(pOut, (char)(0x80 | ((nUnit >> 6) & 0x3F)));
            xx_buf_append_char(pOut, (char)(0x80 | (nUnit & 0x3F)));
        }
    }
}

/* Appends an attribute value as text into pOut, XML-escaping it so the output
 * matches the reference's QXmlStreamWriter. Escaping keeps the regex results
 * identical when a value contains &, <, >, or ". */
static void axml_append_escaped(const char *pData, size_t nSize, xx_buf_t *pOut)
{
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        char c = pData[i];

        switch (c) {
            case '&': xx_buf_append_str(pOut, "&amp;"); break;
            case '<': xx_buf_append_str(pOut, "&lt;"); break;
            case '>': xx_buf_append_str(pOut, "&gt;"); break;
            case '"': xx_buf_append_str(pOut, "&quot;"); break;
            default: xx_buf_append_char(pOut, c); break;
        }
    }
}

/* Namespace map: uri string index -> prefix string index, filled from the
 * start-namespace chunks. The manifest declares just one (android), but a
 * small fixed table covers any file. */
#define AXML_MAX_NS 16

typedef struct {
    uint32_t nUri[AXML_MAX_NS];
    uint32_t nPrefix[AXML_MAX_NS];
    int nCount;
} AxmlNamespaces;

typedef struct {
    char *package_name, *launcher_activity, *activity;
    size_t application_depth, activity_depth, filter_depth;
    int alias, main_action, launcher;
} AxmlMetadata;

static char *axml_string(const AxmlPool *pool, uint32_t index)
{
    xx_buf_t value;
    if (index >= pool->nStringCount) return NULL;
    xx_buf_init(&value);
    axml_append_string(pool, index, &value);
    return xx_buf_detach(&value, NULL);
}

static int axml_string_equals(const AxmlPool *pool, uint32_t index, const char *text)
{
    char *value = axml_string(pool, index);
    int result = value && !xx_rt_strcmp(value, text);
    xx_mem_free(value);
    return result;
}

/* String-pool indices are references, not XML name identities. A legal pool
 * can contain equal entries at different indices. The common case needs no
 * allocation; uncommon comparisons use the same bounded string decoder. */
static int axml_pool_strings_equal(const AxmlPool *pool, uint32_t a, uint32_t b)
{
    char *left, *right;
    int result;
    if (a == b) return 1;
    if (a == UINT32_MAX || b == UINT32_MAX) return 0;
    left = axml_string(pool, a);
    right = axml_string(pool, b);
    result = left && right && !xx_rt_strcmp(left, right);
    xx_mem_free(left);
    xx_mem_free(right);
    return result;
}

static void axml_metadata_free(AxmlMetadata *metadata)
{
    xx_mem_free(metadata->package_name);
    xx_mem_free(metadata->launcher_activity);
    xx_mem_free(metadata->activity);
    xx_rt_memset(metadata, 0, sizeof(*metadata));
}

static char *axml_expand_activity(const char *package, const char *activity)
{
    xx_buf_t out;
    int relative = activity[0] == '.';
    int simple = !xx_rt_strchr(activity, '.');
    if (!*activity || ((relative || simple) && (!package || !*package))) return NULL;
    xx_buf_init(&out);
    if (relative || simple) {
        xx_buf_append_str(&out, package);
        if (simple) xx_buf_append_char(&out, '.');
    }
    xx_buf_append_str(&out, activity);
    return xx_buf_detach(&out, NULL);
}

/* Metadata is derived from AXML events and namespace URIs, never from the
 * escaped display text. Depth scopes prevent sibling activities and separate
 * intent filters from accidentally combining their declarations. */
static void axml_metadata_start(AxmlMetadata *metadata, const char *name, uint32_t name_namespace, size_t depth)
{
    if (name_namespace != UINT32_MAX) return;
    if (depth == 2 && !xx_rt_strcmp(name, "application")) {
        metadata->application_depth = depth;
    } else if (metadata->application_depth && depth == metadata->application_depth + 1 && (!xx_rt_strcmp(name, "activity") || !xx_rt_strcmp(name, "activity-alias"))) {
        metadata->activity_depth = depth;
        metadata->alias = !xx_rt_strcmp(name, "activity-alias");
        xx_mem_free(metadata->activity);
        metadata->activity = NULL;
    } else if (metadata->activity_depth && depth == metadata->activity_depth + 1 && !xx_rt_strcmp(name, "intent-filter")) {
        metadata->filter_depth = depth;
        metadata->main_action = metadata->launcher = 0;
    }
}

static int axml_metadata_attribute(AxmlMetadata *metadata, const char *element, uint32_t element_namespace, size_t depth, const char *name, int android_namespace,
                                   uint32_t attribute_namespace, const char *value)
{
    if (element_namespace != UINT32_MAX) return 1;
    if (depth == 1 && !xx_rt_strcmp(element, "manifest") && attribute_namespace == UINT32_MAX && !xx_rt_strcmp(name, "package")) {
        if (metadata->package_name) return 0;
        metadata->package_name = xx_str_create(value);
        return metadata->package_name != NULL;
    }
    if (!android_namespace) return 1;
    if (metadata->activity_depth == depth && !xx_rt_strcmp(name, metadata->alias ? "targetActivity" : "name")) {
        if (metadata->activity) return 0;
        metadata->activity = xx_str_create(value);
        return metadata->activity != NULL;
    }
    if (metadata->filter_depth && depth == metadata->filter_depth + 1 && !xx_rt_strcmp(name, "name")) {
        if (!xx_rt_strcmp(element, "action") && !xx_rt_strcmp(value, "android.intent.action.MAIN")) metadata->main_action = 1;
        if (!xx_rt_strcmp(element, "category") && !xx_rt_strcmp(value, "android.intent.category.LAUNCHER")) metadata->launcher = 1;
    }
    return 1;
}

static int axml_metadata_end(AxmlMetadata *metadata, size_t depth)
{
    if (depth == metadata->filter_depth) {
        if (!metadata->launcher_activity && metadata->main_action && metadata->launcher && metadata->activity && *metadata->activity) {
            int relative = metadata->activity[0] == '.' || !xx_rt_strchr(metadata->activity, '.');
            if (!relative || (metadata->package_name && *metadata->package_name)) {
                metadata->launcher_activity = axml_expand_activity(metadata->package_name, metadata->activity);
                if (!metadata->launcher_activity) return 0;
            }
        }
        metadata->filter_depth = 0;
        metadata->main_action = metadata->launcher = 0;
    }
    if (depth == metadata->activity_depth) {
        metadata->activity_depth = 0;
        xx_mem_free(metadata->activity);
        metadata->activity = NULL;
    }
    if (depth == metadata->application_depth) metadata->application_depth = 0;
    return 1;
}

static void axml_write_attr_name(const AxmlPool *pPool, const AxmlNamespaces *pNs, uint32_t nNsIndex, uint32_t nNameIndex, xx_buf_t *pOut)
{
    int i = 0;

    if (nNsIndex != UINT32_MAX) {
        /* Prefix spelling is not identity: a hostile unrelated namespace
         * may be declared as android, while the real URI uses another name. */
        if (axml_string_equals(pPool, nNsIndex, AXML_ANDROID_URI)) {
            xx_buf_append_str(pOut, "android:");
            axml_append_string(pPool, nNameIndex, pOut);
            return;
        }
        for (i = 0; i < pNs->nCount; i++) {
            if (pNs->nUri[i] == nNsIndex) {
                if (axml_string_equals(pPool, pNs->nPrefix[i], "android")) xx_buf_appendf(pOut, "ns%u", (unsigned)nNsIndex);
                else axml_append_string(pPool, pNs->nPrefix[i], pOut);
                xx_buf_append_char(pOut, ':');

                break;
            }
        }
    }

    axml_append_string(pPool, nNameIndex, pOut);
}

/* Decodes the AXML in pData into element/attribute text. */
static char *axml_decode(const unsigned char *pData, size_t nSize, AxmlMetadata *metadata, xx_pd_struct *pd)
{
    xx_buf_t out;
    AxmlPool pool;
    AxmlNamespaces ns;
    size_t nOffset = 0;
    int bHavePool = 0;
    size_t nAttrBudget = AXML_MAX_ATTRIBUTES;
    uint32_t element_names[AXML_MAX_DEPTH], element_namespaces[AXML_MAX_DEPTH];
    size_t depth = 0;
    int have_root = 0, root_is_manifest = 0;

    xx_rt_memset(&pool, 0, sizeof(pool));
    xx_rt_memset(&ns, 0, sizeof(ns));
    xx_buf_init(&out);

    pool.pData = pData;
    pool.nSize = nSize;

    /* Top chunk must be RES_XML. */
    if ((nSize < 8) || (xx_data_get_u16(pData, nSize, 0, false) != AXML_RES_XML)) {
        xx_buf_free(&out);

        return NULL;
    }

    if (xx_data_get_u16(pData, nSize, 2, false) != 8 || xx_data_get_u32(pData, nSize, 4, false) != nSize) goto invalid;
    nOffset = 8;

    while (nOffset + 8 <= nSize) {
        uint16_t nType = xx_data_get_u16(pData, nSize, nOffset, false);
        uint16_t nHeaderSize = xx_data_get_u16(pData, nSize, nOffset + 2, false);
        uint32_t nChunkSize = xx_data_get_u32(pData, nSize, nOffset + 4, false);

        if (xx_pd_is_stopped(pd) || nHeaderSize < 8 || nChunkSize < nHeaderSize || nChunkSize > nSize - nOffset) goto invalid;

        if (out.size >= AXML_MAX_OUTPUT || !xx_buf_ok(&out)) goto invalid;

        if (nType == AXML_RES_STRING_POOL) {
            if (bHavePool || depth || nHeaderSize < 28 || nChunkSize < 28) goto invalid;
            pool.nEnd = nOffset + nChunkSize;
            pool.nStringCount = xx_data_get_u32(pData, nSize, nOffset + 8, false);
            pool.nFlags = xx_data_get_u32(pData, nSize, nOffset + 16, false);
            pool.nOffsetsBase = nOffset + nHeaderSize;
            pool.nStringsBase = nOffset + xx_data_get_u32(pData, nSize, nOffset + 20, false);
            if (pool.nStringCount > (nChunkSize - nHeaderSize) / 4 || pool.nStringsBase < pool.nOffsetsBase + (size_t)pool.nStringCount * 4 ||
                pool.nStringsBase > pool.nEnd)
                goto invalid;
            bHavePool = 1;
        } else if (nType == AXML_RES_XML_START_NAMESPACE) {
            if (!bHavePool || nChunkSize < 24 || nHeaderSize != 16 || ns.nCount == AXML_MAX_NS) goto invalid;
            {
                ns.nPrefix[ns.nCount] = xx_data_get_u32(pData, nSize, nOffset + 16, false);
                ns.nUri[ns.nCount] = xx_data_get_u32(pData, nSize, nOffset + 20, false);
                if (ns.nPrefix[ns.nCount] >= pool.nStringCount || ns.nUri[ns.nCount] >= pool.nStringCount) goto invalid;
                ns.nCount++;
            }
        } else if (nType == AXML_RES_XML_END_NAMESPACE) {
            if (!bHavePool || nChunkSize < 24 || nHeaderSize != 16 || !ns.nCount) goto invalid;
            if (!axml_pool_strings_equal(&pool, ns.nPrefix[ns.nCount - 1], xx_data_get_u32(pData, nSize, nOffset + 16, false)) ||
                !axml_pool_strings_equal(&pool, ns.nUri[ns.nCount - 1], xx_data_get_u32(pData, nSize, nOffset + 20, false)))
                goto invalid;
            --ns.nCount;
        } else if (nType == AXML_RES_XML_START_ELEMENT) {
            uint32_t nName = xx_data_get_u32(pData, nSize, nOffset + 20, false);
            uint32_t nNamespace = xx_data_get_u32(pData, nSize, nOffset + 16, false);
            uint16_t nAttrCount = xx_data_get_u16(pData, nSize, nOffset + 28, false);
            uint16_t attr_start, attr_size;
            size_t nAttrOffset;
            char *element;
            if (!bHavePool || nHeaderSize != 16 || nChunkSize < 36 || nName >= pool.nStringCount || (nNamespace != UINT32_MAX && nNamespace >= pool.nStringCount) ||
                depth == AXML_MAX_DEPTH)
                goto invalid;
            if (!depth && have_root) goto invalid;
            element = axml_string(&pool, nName);
            if (!element) goto invalid;
            if (!depth) {
                have_root = 1;
                root_is_manifest = nNamespace == UINT32_MAX && !xx_rt_strcmp(element, "manifest");
            }
            element_names[depth] = nName;
            element_namespaces[depth] = nNamespace;
            ++depth;
            if (root_is_manifest) axml_metadata_start(metadata, element, nNamespace, depth);
            attr_start = xx_data_get_u16(pData, nSize, nOffset + 24, false);
            attr_size = xx_data_get_u16(pData, nSize, nOffset + 26, false);
            if (attr_start < 20 || attr_size < 20 || attr_start > nChunkSize - 16) {
                xx_mem_free(element);
                goto invalid;
            }
            nAttrOffset = nOffset + 16 + attr_start;
            if (nAttrCount > (nOffset + nChunkSize - nAttrOffset) / attr_size) {
                xx_mem_free(element);
                goto invalid;
            }
            uint16_t a = 0;

            xx_buf_append_char(&out, '<');
            axml_append_string(&pool, nName, &out);

            for (a = 0; a < nAttrCount; a++) {
                size_t nAt = nAttrOffset + (size_t)a * attr_size;
                uint32_t nAttrNs = 0;
                uint32_t nAttrName = 0;
                uint8_t nDataType = 0;
                uint32_t nAttrData = 0;

                if (nAt + 20 > nOffset + nChunkSize || xx_pd_is_stopped(pd)) {
                    xx_mem_free(element);
                    goto invalid;
                }

                /* Each attribute can emit a string-pool entry of up to
                 * 0x10000 bytes, so the attribute budget alone does not bound
                 * the decoded text. */
                if (out.size >= AXML_MAX_OUTPUT || !xx_buf_ok(&out)) {
                    xx_mem_free(element);
                    goto invalid;
                }

                /* Overlapping START_ELEMENT chunks that each declare 0xFFFF
                 * attributes make this quadratic, so the decode as a whole
                 * gets a budget. Real manifests use a few hundred.          */
                if (nAttrBudget == 0) {
                    xx_mem_free(element);
                    goto invalid;
                }

                nAttrBudget--;

                nAttrNs = xx_data_get_u32(pData, nSize, nAt + 0, false);
                nAttrName = xx_data_get_u32(pData, nSize, nAt + 4, false);
                nDataType = pData[nAt + 15]; /* HEADER_XML_ATTRIBUTE.dataType */
                nAttrData = xx_data_get_u32(pData, nSize, nAt + 16, false);
                if (nAttrName >= pool.nStringCount || (nAttrNs != UINT32_MAX && nAttrNs >= pool.nStringCount) || (nDataType == 3 && nAttrData >= pool.nStringCount)) {
                    xx_mem_free(element);
                    goto invalid;
                }

                xx_buf_append_char(&out, ' ');
                axml_write_attr_name(&pool, &ns, nAttrNs, nAttrName, &out);
                xx_buf_append_str(&out, "=\"");

                if (nDataType == 1) {
                    /* Reference: @hex. */
                    xx_buf_appendf(&out, "@%x", (unsigned)nAttrData);
                } else if (nDataType == 3) {
                    /* String: escape so the text matches the reference. */
                    xx_buf_t value;

                    xx_buf_init(&value);
                    axml_append_string(&pool, nAttrData, &value);
                    axml_append_escaped(value.data ? value.data : "", value.size, &out);
                    if (!xx_buf_ok(&value)) out.failed = true;
                    if (root_is_manifest && xx_buf_ok(&value)) {
                        char *name = axml_string(&pool, nAttrName);
                        int android_namespace = axml_string_equals(&pool, nAttrNs, AXML_ANDROID_URI);
                        int ok = name && axml_metadata_attribute(metadata, element, nNamespace, depth, name, android_namespace, nAttrNs, value.data ? value.data : "");
                        xx_mem_free(name);
                        if (!ok) {
                            xx_buf_free(&value);
                            xx_mem_free(element);
                            goto invalid;
                        }
                    }
                    xx_buf_free(&value);
                } else if (nDataType == 16) {
                    xx_buf_appendf(&out, "%d", (int)nAttrData);
                } else if (nDataType == 17) {
                    xx_buf_appendf(&out, "0x%x", (unsigned)nAttrData);
                } else if (nDataType == 18) {
                    xx_buf_append_str(&out, nAttrData != 0 ? "true" : "false");
                }

                xx_buf_append_char(&out, '"');
            }

            xx_buf_append_char(&out, '>');
            xx_buf_append_char(&out, '\n');
            xx_mem_free(element);
        } else if (nType == AXML_RES_XML_END_ELEMENT) {
            uint32_t nName, nNamespace;
            if (!bHavePool || nHeaderSize != 16 || nChunkSize < 24 || !depth) goto invalid;
            nName = xx_data_get_u32(pData, nSize, nOffset + 20, false);
            nNamespace = xx_data_get_u32(pData, nSize, nOffset + 16, false);
            if (!axml_pool_strings_equal(&pool, nName, element_names[depth - 1]) || !axml_pool_strings_equal(&pool, nNamespace, element_namespaces[depth - 1]))
                goto invalid;
            if (root_is_manifest && !axml_metadata_end(metadata, depth)) goto invalid;
            --depth;
            xx_buf_append_str(&out, "</");
            axml_append_string(&pool, nName, &out);
            xx_buf_append_str(&out, ">\n");
        }

        nOffset += nChunkSize;
    }

    if (!bHavePool || !have_root || nOffset != nSize || out.size > AXML_MAX_OUTPUT || !xx_buf_ok(&out) || xx_pd_is_stopped(pd)) goto invalid;
    /* Legacy inspection accepts START-only trees. Text stays available, but
     * the structured metadata requires a completely matched root tree. */
    if (depth) axml_metadata_free(metadata);
    return xx_buf_detach(&out, NULL);
invalid:
    xx_buf_free(&out);
    return NULL;
}

bool xx_apk_analyze(xx_apk *apk, xx_pd_struct *pd)
{
    uint8_t *data = NULL;
    size_t size = 0;
    AxmlMetadata metadata;
    char *text;
    if (!apk || xx_pd_is_stopped(pd)) return false;
    if (apk->manifest_text) return true;
    if (!xx_zip_read_file(&apk->zip, "AndroidManifest.xml", AXML_MAX_OUTPUT, &data, &size, pd)) return false;
    xx_rt_memset(&metadata, 0, sizeof(metadata));
    text = axml_decode(data, size, &metadata, pd);
    xx_mem_free(data);
    if (text) {
        apk->manifest_text = text;
        apk->package_name = metadata.package_name;
        metadata.package_name = NULL;
        apk->launcher_activity = metadata.launcher_activity;
        metadata.launcher_activity = NULL;
    }
    axml_metadata_free(&metadata);
    return text != NULL;
}
const char *xx_apk_get_manifest(const xx_apk *apk)
{
    return apk && apk->manifest_text ? apk->manifest_text : "";
}
const char *xx_apk_get_package_name(const xx_apk *apk)
{
    return apk && apk->package_name ? apk->package_name : "";
}
const char *xx_apk_get_launcher_activity(const xx_apk *apk)
{
    return apk && apk->launcher_activity ? apk->launcher_activity : "";
}
char *xx_apk_manifest_record(const xx_apk *apk, const char *key)
{
    const char *p = xx_apk_get_manifest(apk), *end = p + xx_rt_strlen(p);
    size_t n;
    if (!key) return xx_str_create("");
    n = xx_rt_strlen(key);
    for (; p < end; ++p) {
        size_t left = (size_t)(end - p);
        if (left >= 2 && n <= left - 2 && !xx_rt_strncmp(p, key, n) && p[n] == '=' && p[n + 1] == '"') {
            const char *start = p + n + 2, *finish = start;
            while (finish < end && *finish != '"') ++finish;
            char *result = xx_str_create_len((size_t)(finish - start));
            if (result) xx_rt_memcpy(result, start, (size_t)(finish - start));
            return result;
        }
    }
    return xx_str_create("");
}
