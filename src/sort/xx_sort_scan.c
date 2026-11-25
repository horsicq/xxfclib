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

/* Scan record and signature database sorting policies. */

#include "xx_sort_scan_internal.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/sort/xx_sort.h"

static int xx_scan_compare_record_priority(const void *pLeft, const void *pRight)
{
    const xx_scan_record *pA = (const xx_scan_record *)pLeft;
    const xx_scan_record *pB = (const xx_scan_record *)pRight;

    if (pA->priority != pB->priority) {
        return (pA->priority < pB->priority) ? -1 : 1;
    }

    return 0;
}

/* Stable insertion sort: std::sort in the reference engine behaves like a
 * stable sort for the small result lists produced by a single scan.        */
void xx_scan_sort_records(xx_scan_record *pRecords, int nCount)
{
    int i = 0;
    int j = 0;

    if (!pRecords || nCount < 2) return;

    for (i = 1; i < nCount; i++) {
        xx_scan_record key = pRecords[i];

        j = i - 1;

        while ((j >= 0) && (xx_scan_compare_record_priority(&pRecords[j], &key) > 0)) {
            pRecords[j + 1] = pRecords[j];
            j--;
        }

        pRecords[j + 1] = key;
    }
}

/* The C enum is compact; the reference's file-type enum also contains types
 * that have no signature directory. Preserve its relative order here because
 * the subsequent priority comparison is not a strict weak ordering. */
static int signature_file_type_order(XFileType fileType)
{
    static const int order[XFT_COUNT] = {
        [XFT_UNKNOWN] = 0,  [XFT_BINARY] = 4,    [XFT_COM] = 8,           [XFT_MSDOS] = 9,      [XFT_NE] = 12,      [XFT_LE] = 13,        [XFT_LX] = 14,
        [XFT_PE] = 15,      [XFT_PE32] = 16,     [XFT_PE64] = 17,         [XFT_ELF] = 18,       [XFT_ELF32] = 19,   [XFT_ELF64] = 20,     [XFT_MACHO] = 21,
        [XFT_MACHO32] = 22, [XFT_MACHO64] = 23,  [XFT_ZIP] = 68,          [XFT_JAR] = 52,       [XFT_APK] = 30,     [XFT_IPA] = 51,       [XFT_DEX] = 44,
        [XFT_NPM] = 84,     [XFT_MACHOFAT] = 54, [XFT_ARCHIVE] = 42,      [XFT_PDF] = 55,       [XFT_CFBF] = 90,    [XFT_IMAGE] = 48,     [XFT_JPEG] = 53,
        [XFT_PNG] = 58,     [XFT_RAR] = 59,      [XFT_ISO9660] = 102,     [XFT_AMIGAHUNK] = 24, [XFT_ATARIST] = 25, [XFT_JAVACLASS] = 88, [XFT_PYC] = 89,
        [XFT_DOS16M] = 10,  [XFT_DOS4G] = 11,    [XFT_CLI_ASSEMBLY] = 26,
    };

    return ((int)fileType >= 0 && fileType < XFT_COUNT) ? order[fileType] : (int)fileType;
}

/* Return the penultimate dot-separated section without truncating long
 * priorities. A section is present only when the name has at least two dots. */
static int signature_priority(const char *pName, const char **ppStart, size_t *pnSize)
{
    const char *pPrevious = NULL;
    const char *pLast = NULL;
    const char *p;

    for (p = pName; *p; ++p) {
        if (*p == '.') {
            pPrevious = pLast;
            pLast = p;
        }
    }

    if (!pPrevious) return 0;
    *ppStart = pPrevious + 1;
    *pnSize = (size_t)(pLast - pPrevious - 1);
    return 1;
}

/* XScanEngine::sort_signature_prio compares priorities only when BOTH names
 * have a priority section, and only when both sections are nonempty. This is
 * intentionally pair-dependent. Replacing it with per-name default priority
 * changes script execution order and therefore _getNumberOfResults queries.
 * Names use the existing C UTF-8 byte comparison; Detect-It-Easy database
 * names are ASCII, where this agrees with QString comparison. */
static int signature_less(const DBSignature *pA, const DBSignature *pB)
{
    const char *pNameA = pA->pName ? pA->pName : "";
    const char *pNameB = pB->pName ? pB->pName : "";
    const char *pPrioA = NULL;
    const char *pPrioB = NULL;
    size_t nPrioA = 0;
    size_t nPrioB = 0;
    int nTypeA = signature_file_type_order(pA->fileType);
    int nTypeB = signature_file_type_order(pB->fileType);

    if (nTypeA != nTypeB) return nTypeA < nTypeB;

    if (signature_priority(pNameA, &pPrioA, &nPrioA) && signature_priority(pNameB, &pPrioB, &nPrioB) && nPrioA && nPrioB) {
        size_t i;
        size_t nCommon = nPrioA < nPrioB ? nPrioA : nPrioB;

        for (i = 0; i < nCommon; ++i) {
            unsigned char a = (unsigned char)pPrioA[i];
            unsigned char b = (unsigned char)pPrioB[i];
            if (a != b) return a < b;
        }
        if (nPrioA != nPrioB) return nPrioA < nPrioB;
    }

    return xx_rt_strcmp(pNameA, pNameB) < 0;
}

/* Cache immutable comparison fields once per sort. The original index lets
 * the same sorting algorithm move these smaller rows, then permute records
 * in place without a second allocation or changing the public model. */
typedef struct {
    size_t nOriginalIndex;
    const char *pName;
    const char *pPriority;
    size_t nPrioritySize;
    int nFileTypeOrder;
} signature_cached_record;

static int signature_cached_less(const signature_cached_record *pA, const signature_cached_record *pB)
{
    if (pA->nFileTypeOrder != pB->nFileTypeOrder) return pA->nFileTypeOrder < pB->nFileTypeOrder;

    if (pA->nPrioritySize && pB->nPrioritySize) {
        size_t i;
        size_t nCommon = pA->nPrioritySize < pB->nPrioritySize ? pA->nPrioritySize : pB->nPrioritySize;

        for (i = 0; i < nCommon; ++i) {
            unsigned char a = (unsigned char)pA->pPriority[i];
            unsigned char b = (unsigned char)pB->pPriority[i];
            if (a != b) return a < b;
        }
        if (pA->nPrioritySize != pB->nPrioritySize) return pA->nPrioritySize < pB->nPrioritySize;
    }

    return xx_rt_strcmp(pA->pName, pB->pName) < 0;
}

/* Keep the original allocation-free path available if the optional cache
 * cannot be allocated. Both paths use exactly the same sorting algorithm. */
#define SIGNATURE_SORT_RECORD DBSignature
#define SIGNATURE_SORT_LESS signature_less
#define SIGNATURE_SORT_FUNCTION(name) signature_direct_##name
#include "xx_sort_scan_algorithm.inc"
#undef SIGNATURE_SORT_FUNCTION
#undef SIGNATURE_SORT_LESS
#undef SIGNATURE_SORT_RECORD

#define SIGNATURE_SORT_RECORD signature_cached_record
#define SIGNATURE_SORT_LESS signature_cached_less
#define SIGNATURE_SORT_FUNCTION(name) signature_cached_##name
#include "xx_sort_scan_algorithm.inc"
#undef SIGNATURE_SORT_FUNCTION
#undef SIGNATURE_SORT_LESS
#undef SIGNATURE_SORT_RECORD

void xx_scan_sort_database_records(DBase *database)
{
    size_t count;
    size_t i;
    DBSignature *pRecords;
    signature_cached_record *pCached = NULL;

    if (!database || !database->pRecords || database->nCount < 2) return;
    pRecords = database->pRecords;
    count = (size_t)database->nCount;

    if (count <= (size_t)-1 / sizeof(*pCached)) {
        pCached = (signature_cached_record *)xx_rt_malloc(count * sizeof(*pCached));
    }
    if (!pCached) {
        signature_direct_sort(pRecords, count);
        return;
    }

    for (i = 0; i < count; ++i) {
        pCached[i].nOriginalIndex = i;
        pCached[i].pName = pRecords[i].pName ? pRecords[i].pName : "";
        pCached[i].pPriority = NULL;
        pCached[i].nPrioritySize = 0;
        pCached[i].nFileTypeOrder = signature_file_type_order(pRecords[i].fileType);
        signature_priority(pCached[i].pName, &pCached[i].pPriority, &pCached[i].nPrioritySize);
    }
    signature_cached_sort(pCached, count);

    /* Each cached row identifies the original record for its destination.
     * Follow each permutation cycle, marking completed rows as fixed points. */
    for (i = 0; i < count; ++i) {
        size_t hole = i;
        DBSignature value;

        if (pCached[i].nOriginalIndex == i) continue;
        value = pRecords[i];
        while (pCached[hole].nOriginalIndex != i) {
            size_t source = pCached[hole].nOriginalIndex;
            pRecords[hole] = pRecords[source];
            pCached[hole].nOriginalIndex = hole;
            hole = source;
        }
        pRecords[hole] = value;
        pCached[hole].nOriginalIndex = hole;
    }
    xx_rt_free(pCached);
}

void db_sort(DBase *pDb)
{
    /* Sorting happens per database in db_load; kept for API symmetry. */
    (void)pDb;
}
